/* Ordinary launcher. No private input/adapter data crosses the OCALL boundary.
 * Independent SDK run loops sleep at OCALLs; the coordinator releases only one at a
 * time. Enclave/LOC checks remain authoritative for all supplied object IDs.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <condition_variable>
#include "host/keystone.h"
#include "edge/edge_common.h"
#include "sharing_app.h"

struct Slot {
    Keystone::Enclave *enclave=nullptr;
    std::thread worker;
    share_reply reply{};
    share_command command{};
    bool ready=false, go=false, done=false;
};
static Slot slots[SHARE_TENANTS];
static std::mutex gate;
static std::condition_variable changed;
static thread_local unsigned current;
static void need(bool ok, const char *where) {
    if (!ok) { std::fprintf(stderr,"SHARING_FAIL phase=%s\n",where); std::fflush(nullptr); std::_Exit(1); }
}
static void dispatch(void *buffer) {
    Slot &s=slots[current];
    auto *edge=static_cast<edge_call *>(buffer);
    const size_t size=s.enclave->getSharedBufferSize();
    need(edge->call_id==1 && edge->call_arg_size==sizeof(share_reply) &&
        edge->call_arg_offset<=size && sizeof(share_reply)<=size-edge->call_arg_offset,"ocall_bounds");
    std::unique_lock<std::mutex> lock(gate);
    std::memcpy(&s.reply,static_cast<char *>(buffer)+edge->call_arg_offset,sizeof(s.reply));
    s.ready=true; changed.notify_all();
    changed.wait(lock,[&s]{return s.go;});
    s.go=false;
    const size_t offset=sizeof(edge_call);
    need(sizeof(s.command)<=size-offset,"return_bounds");
    std::memcpy(static_cast<char *>(buffer)+offset,&s.command,sizeof(s.command));
    edge->return_data.call_ret_offset=offset;
    edge->return_data.call_ret_size=sizeof(s.command);
    edge->return_data.call_status=CALL_STATUS_OK;
}
static void start(unsigned i,const char *app,const char *rt) {
    sp_begin(SP_CREATE);
    Slot &s=slots[i]; s.enclave=new Keystone::Enclave;
    Keystone::Params params; params.setFreeMemSize(SHARE_FREE_BYTES);
    params.setUntrustedMem(SHARE_UTM,SHARE_UTM_BYTES);
    need(s.enclave->init(app,rt,params)==Keystone::Error::Success,"create");
    need(s.enclave->registerOcallDispatch(dispatch)==Keystone::Error::Success,"dispatch");
    sp_end(SP_CREATE);
    sp_begin(SP_ENTER);
    s.worker=std::thread([i]{
        current=i; uintptr_t value=UINTPTR_MAX;
        need(slots[i].enclave->run(&value)==Keystone::Error::Success,"run");
        if (value!=SHARE_PASS) std::fprintf(stderr,"SHARING_APP_ERROR tenant=%u code=%#lx\n",i,value);
        need(value==SHARE_PASS,"eapp_result");
        sp_end(SP_EXIT);
        std::lock_guard<std::mutex> lock(gate); slots[i].done=true; changed.notify_all();
    });
    std::unique_lock<std::mutex> lock(gate);
    changed.wait(lock,[&s]{return s.ready || s.done;});
    need(s.ready && s.reply.action==SA_HELLO && s.reply.principal,"hello");
}
static share_reply step(unsigned i,uint64_t action,uint64_t principal=0,
                        uint64_t input0=0,uint64_t input1=0,uint64_t foreign=0) {
    Slot &s=slots[i]; std::unique_lock<std::mutex> lock(gate);
    need(s.ready && !s.go && !s.done,"step_ready");
    s.command={action,i,principal,{input0,input1},foreign};
    s.ready=false; sp_begin(SP_SWITCH); s.go=true; changed.notify_all();
    changed.wait(lock,[&s]{return s.ready || s.done;});
    if(action==SA_STOP) { need(s.done,"stopped"); return s.reply; }
    need(s.ready && s.reply.action==action,"reply_action");
    std::printf("SHARING_ACTION tenant=%u action=%lu principal=%lu input0=%lu input1=%lu private0=%lu private1=%lu checks=%lu\n",
        i,action,s.reply.principal,s.reply.input[0],s.reply.input[1],
        s.reply.workspace[0],s.reply.workspace[1],s.reply.checks);
    if(action==SA_VERIFY) {
        std::printf("SHARING_RESULT tenant=%u digest=%lu values=",i,s.reply.digest);
        for(unsigned j=0;j<6;++j) std::printf("%s%lu",j?",":"",s.reply.result[j]);
        std::printf("\n");
    }
    return s.reply;
}
static void destroy(unsigned i) {
    slots[i].worker.join(); sp_begin(SP_DESTROY);
    need(slots[i].enclave->destroy()==Keystone::Error::Success,"destroy"); sp_end(SP_DESTROY);
}
int main(int argc,char **argv) {
    need(argc==3,"arguments"); sp_end(SP_SETUP);
    for(unsigned t=0;t<SHARE_TENANTS;++t) {
        start(t,argv[1],argv[2]);
        for(unsigned j=0;j<t;++j) need(slots[t].reply.principal!=slots[j].reply.principal,"distinct_principals");
    }
    for(unsigned t=0;t<SHARE_TENANTS;++t) step(t,SA_INIT);
    sp_begin(SP_WINDOW_BEGIN); sp_end(SP_WINDOW_BEGIN);
    auto owner=step(0,SA_LOAD);
    for(unsigned t=1;t<SHARE_TENANTS;++t) {
        if(SHARE_DUPLICATED) step(t,SA_LOAD);
        else {
            step(0,SA_GRANT,slots[t].reply.principal);
            step(t,SA_ATTACH,0,owner.input[0],owner.input[1]);
        }
    }
    for(unsigned t=0;t<SHARE_TENANTS;++t) step(t,SA_PROJECT);
    if(SHARE_DIAGNOSTIC) {
        step(0,SA_DENIED,0,0,0,slots[1].reply.workspace[0]);
        step(1,SA_DENIED,0,0,0,owner.workspace[0]);
        step(0,SA_REVOKE,slots[1].reply.principal);
        step(1,SA_REVOKED);
        /* Tenant 2 retains access after tenant 1 loses it. */
        step(2,SA_PROJECT);
    }
    if(!SHARE_DUPLICATED)
        for(unsigned t=1;t<SHARE_TENANTS;++t)
            if(!SHARE_DIAGNOSTIC || t!=1) step(0,SA_REVOKE,slots[t].reply.principal);
    for(unsigned t=SHARE_TENANTS;t-->0;) step(t,SA_RELEASE);
    sp_begin(SP_WINDOW_END); sp_end(SP_WINDOW_END);
    for(unsigned t=0;t<SHARE_TENANTS;++t) step(t,SA_VERIFY);
    for(unsigned t=SHARE_TENANTS;t-->0;) {
        step(t,SA_FREE_HOST); step(t,SA_STOP); destroy(t);
    }
    std::printf("SHARING_PASS kind=%d n=%lu tenants=%d duplicated=%d diagnostic=%d projections=%d sequential=1 immutable_grants=1\n",
        SHARE_KIND,(unsigned long)SHARE_N,SHARE_TENANTS,SHARE_DUPLICATED,
        SHARE_DIAGNOSTIC,SHARE_TENANTS+(SHARE_DIAGNOSTIC?1:0));
    std::fflush(nullptr); std::_Exit(0);
}
