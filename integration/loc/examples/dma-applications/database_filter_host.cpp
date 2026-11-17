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
#include "database_filter.h"

struct Slot {
    Keystone::Enclave *enclave=nullptr;
    std::thread worker;
    db_reply reply{};
    db_command command{};
    bool ready=false, go=false, done=false;
};
static Slot slots[DB_TENANTS];
static std::mutex gate;
static std::condition_variable changed;
static thread_local unsigned current;
static void need(bool ok, const char *where) {
    if (!ok) { std::fprintf(stderr,"DATABASE_FAIL phase=%s\n",where); std::fflush(nullptr); std::_Exit(1); }
}
static void dispatch(void *buffer) {
    Slot &s=slots[current];
    auto *edge=static_cast<edge_call *>(buffer);
    const size_t size=s.enclave->getSharedBufferSize();
    need(edge->call_id==1 && edge->call_arg_size==sizeof(db_reply) &&
        edge->call_arg_offset<=size && sizeof(db_reply)<=size-edge->call_arg_offset,"ocall_bounds");
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
    Keystone::Params params; params.setFreeMemSize(DB_FREE_BYTES);
    params.setUntrustedMem(SHARE_UTM,SHARE_UTM_BYTES);
    need(s.enclave->init(app,rt,params)==Keystone::Error::Success,"create");
    need(s.enclave->registerOcallDispatch(dispatch)==Keystone::Error::Success,"dispatch");
    sp_end(SP_CREATE);
    sp_begin(SP_ENTER);
    s.worker=std::thread([i]{
        current=i; uintptr_t value=UINTPTR_MAX;
        need(slots[i].enclave->run(&value)==Keystone::Error::Success,"run");
        if (value!=DB_PASS) std::fprintf(stderr,"DATABASE_APP_ERROR tenant=%u code=%#lx\n",i,value);
        need(value==DB_PASS,"eapp_result");
        sp_end(SP_EXIT);
        std::lock_guard<std::mutex> lock(gate); slots[i].done=true; changed.notify_all();
    });
    std::unique_lock<std::mutex> lock(gate);
    changed.wait(lock,[&s]{return s.ready || s.done;});
    need(s.ready && s.reply.action==DB_HELLO && s.reply.principal,"hello");
}
static db_reply step(unsigned t,uint64_t action,uint64_t principal=0,const uint64_t *inputs=nullptr) {
    Slot &s=slots[t]; std::unique_lock<std::mutex> lock(gate);
    need(s.ready && !s.go && !s.done,"step_ready");
    s.command={}; s.command.action=action; s.command.tenant=t; s.command.principal=principal;
    if(inputs) std::memcpy(s.command.input,inputs,sizeof(s.command.input));
    s.ready=false; sp_begin(SP_SWITCH); s.go=true; changed.notify_all();
    changed.wait(lock,[&s]{return s.ready || s.done;});
    if(action==DB_STOP) { need(s.done,"stopped"); return s.reply; }
    need(s.ready && s.reply.action==action,"reply_action");
    std::printf("DATABASE_ACTION tenant=%u action=%lu principal=%lu workspace=%lu checks=%lu input_digest=%lu\n",
        t,action,s.reply.principal,s.reply.workspace,s.reply.checks,
        share_hash((const unsigned char *)s.reply.input,sizeof(s.reply.input)));
    if(action==DB_VERIFY)
        std::printf("DATABASE_RESULT tenant=%u digest=%lu matches=%lu sum=%lu\n",t,s.reply.digest,s.reply.matches,s.reply.sum);
    return s.reply;
}
static void destroy(unsigned i) {
    slots[i].worker.join(); sp_begin(SP_DESTROY);
    need(slots[i].enclave->destroy()==Keystone::Error::Success,"destroy"); sp_end(SP_DESTROY);
}
int main(int argc,char **argv) {
    need(argc==3,"arguments"); sp_end(SP_SETUP);
    for(unsigned t=0;t<DB_TENANTS;++t) {
        start(t,argv[1],argv[2]);
        for(unsigned u=0;u<t;++u) need(slots[t].reply.principal!=slots[u].reply.principal,"distinct_principals");
    }
    for(unsigned t=0;t<DB_TENANTS;++t) step(t,DB_INIT);
    sp_begin(SP_WINDOW_BEGIN); sp_end(SP_WINDOW_BEGIN);
    auto owner=step(0,DB_LOAD);
    for(unsigned t=1;t<DB_TENANTS;++t) {
        step(0,DB_GRANT,slots[t].reply.principal);
        step(t,DB_ATTACH,0,owner.input);
    }
    for(unsigned t=0;t<DB_TENANTS;++t) step(t,DB_QUERY);
    for(unsigned t=1;t<DB_TENANTS;++t) step(t,DB_PRESSURE);
    step(0,DB_REVOKE,slots[1].reply.principal);
    step(1,DB_REVOKED);
    for(unsigned t=0;t<DB_TENANTS;++t) if(t!=1) step(t,DB_SURVIVE);
    // Fresh pressure before owner-free prevents diagnostic accesses warming targets.
    for(unsigned t=1;t<DB_TENANTS;++t) step(t,DB_PRESSURE);
    step(0,DB_FREE_INPUT);
    for(unsigned t=0;t<DB_TENANTS;++t) step(t,DB_FREED);
    for(unsigned t=1;t<DB_TENANTS;++t) step(t,DB_CLEAR_PRESSURE);
    for(unsigned t=0;t<DB_TENANTS;++t) step(t,DB_FREE_PRIVATE);
    sp_begin(SP_WINDOW_END); sp_end(SP_WINDOW_END);
    for(unsigned t=0;t<DB_TENANTS;++t) step(t,DB_VERIFY);
    for(unsigned t=DB_TENANTS;t-->0;) { step(t,DB_FREE_HOST); step(t,DB_STOP); destroy(t); }
    std::printf("DATABASE_PASS n=%lu partitions=%lu tenants=4 queries=4 sequential=1 immutable_grants=1\n",
        (unsigned long)DB_N,(unsigned long)DB_PARTS);
    std::fflush(nullptr); std::_Exit(0);
}
