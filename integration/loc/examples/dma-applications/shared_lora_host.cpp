/* Ordinary launcher. No private input/adapter data crosses the OCALL boundary.
 * Two SDK run loops sleep at OCALLs; the coordinator releases only one at a
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
#include "shared_lora.h"

struct Slot {
    Keystone::Enclave *enclave=nullptr;
    std::thread worker;
    shared_reply reply{};
    shared_command command{};
    bool ready=false, go=false, done=false;
};
static Slot slots[2];
static std::mutex gate;
static std::condition_variable changed;
static thread_local unsigned current;
static void need(bool ok, const char *where) {
    if (!ok) { std::fprintf(stderr,"SHARED_LORA_FAIL phase=%s\n",where); std::fflush(nullptr); std::_Exit(1); }
}
static void dispatch(void *buffer) {
    Slot &s=slots[current];
    auto *edge=static_cast<edge_call *>(buffer);
    const size_t size=s.enclave->getSharedBufferSize();
    need(edge->call_id==1 && edge->call_arg_size==sizeof(shared_reply) &&
        edge->call_arg_offset<=size && sizeof(shared_reply)<=size-edge->call_arg_offset,"ocall_bounds");
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
    sh_begin(SH_CREATE);
    Slot &s=slots[i]; s.enclave=new Keystone::Enclave;
    Keystone::Params params; params.setFreeMemSize(SHARED_FREE_BYTES(SHARED_HOST_WEIGHTS(i)));
    params.setUntrustedMem(SHARED_UTM,SHARED_UTM_BYTES);
    need(s.enclave->init(app,rt,params)==Keystone::Error::Success,"create");
    need(s.enclave->registerOcallDispatch(dispatch)==Keystone::Error::Success,"dispatch");
    sh_end(SH_CREATE);
    sh_begin(SH_ENTER);
    s.worker=std::thread([i]{
        current=i; uintptr_t value=UINTPTR_MAX;
        need(slots[i].enclave->run(&value)==Keystone::Error::Success,"run");
        if (value!=SHARED_PASS) std::fprintf(stderr,"SHARED_LORA_APP_ERROR tenant=%u code=%#lx\n",i,value);
        need(value==SHARED_PASS,"eapp_result");
        sh_end(SH_EXIT);
        std::lock_guard<std::mutex> lock(gate); slots[i].done=true; changed.notify_all();
    });
    std::unique_lock<std::mutex> lock(gate);
    changed.wait(lock,[&s]{return s.ready || s.done;});
    need(s.ready && s.reply.action==SH_HELLO && s.reply.principal,"hello");
}
static shared_reply step(unsigned i,uint64_t action,uint64_t principal=0,
                         uint64_t weights=0,uint64_t accumulator=0,uint64_t round=0) {
    Slot &s=slots[i];
    std::unique_lock<std::mutex> lock(gate);
    need(s.ready && !s.go && !s.done,"step_ready");
    s.command={action,i,principal,weights,accumulator,round};
    s.ready=false; sh_begin(SH_SWITCH); s.go=true; changed.notify_all();
    changed.wait(lock,[&s]{return s.ready || s.done;});
    if (action==SH_STOP) { need(s.done,"stopped"); return s.reply; }
    need(s.ready && s.reply.action==action,"reply_action");
    std::printf("SHARED_LORA_ACTION tenant=%u action=%lu principal=%lu weights=%lu accumulator=%lu checks=%lu round=%lu\n",
        i,action,s.reply.principal,s.reply.weights,s.reply.accumulator,s.reply.checks,s.reply.round);
    if (action==SH_PROJECT) {
        std::printf("SHARED_LORA_OUTPUT tenant=%u round=%lu values=",i,round);
        for (unsigned long row=0;row<SHARED_N;++row) std::printf("%s%d",row?",":"",s.reply.output[row]);
        std::printf("\n");
    }
    return s.reply;
}
static void destroy(unsigned i) {
    slots[i].worker.join(); sh_begin(SH_DESTROY);
    need(slots[i].enclave->destroy()==Keystone::Error::Success,"destroy"); sh_end(SH_DESTROY);
}
int main(int argc,char **argv) {
    need(argc==3,"arguments"); sh_end(SH_SETUP);
    start(0,argv[1],argv[2]); start(1,argv[1],argv[2]);
    uint64_t p0=slots[0].reply.principal,p1=slots[1].reply.principal;
    need(p0 && p1 && p0!=p1,"distinct_principals");
    step(0,SH_INIT); step(1,SH_INIT);
    auto owner=step(0,SH_LOAD);
#if SHARED_BENCHMARK
#if SHARED_DUPLICATED
    auto peer=step(1,SH_LOAD);
    need(owner.weights!=peer.weights && owner.accumulator!=peer.accumulator,"private_objects");
#else
    step(0,SH_GRANT,p1);
    auto peer=step(1,SH_ATTACH,0,owner.weights);
    need(owner.weights==peer.weights && owner.accumulator!=peer.accumulator,"shared_and_private");
#endif
    step(0,SH_PROJECT,0,0,0,0); step(1,SH_PROJECT,0,0,0,1);
#if !SHARED_DUPLICATED
    step(0,SH_REVOKE,p1);
#endif
    step(1,SH_RELEASE); step(1,SH_STOP); destroy(1);
    step(0,SH_RELEASE); step(0,SH_STOP); destroy(0);
    std::printf("SHARED_LORA_PASS elements=%lu rank=8 tenants=2 projections=2 weight_uploads=%lu objects=%u grants=4 immutable_grants=1 sequential=1 independent_int64=1 mode=%s\n",
        (unsigned long)SHARED_N,(unsigned long)((SHARED_N+15)/16)*(SHARED_DUPLICATED?2:1),
        SHARED_DUPLICATED?4:3, SHARED_DUPLICATED?"duplicated":"shared");
#else
    step(1,SH_BEFORE_GRANT,0,owner.weights);
    step(0,SH_GRANT,p1);
    auto peer=step(1,SH_ATTACH,0,owner.weights);
    need(owner.weights==peer.weights && owner.accumulator!=peer.accumulator,"shared_and_private");
    step(0,SH_PROJECT,0,0,0,0); step(1,SH_PROJECT,0,0,0,1);
    step(0,SH_DENIED,p1,0,peer.accumulator);
    step(1,SH_DENIED,p0,0,owner.accumulator);
    step(0,SH_REVOKE,p1);
    step(1,SH_REVOKED);
    step(1,SH_RELEASE); step(1,SH_STOP); destroy(1);
    step(0,SH_STALE,p1);
    step(0,SH_PROJECT,0,0,0,2);
    auto final=step(0,SH_RELEASE);
    step(0,SH_STOP); destroy(0);
    std::printf("SHARED_LORA_PASS elements=%lu rank=8 tenants=2 projections=3 weight_uploads=%lu objects=3 grants=4 selective_revocations=1 owner_checks=%lu immutable_grants=1 sequential=1 independent_int64=1\n",
        (unsigned long)SHARED_N,(unsigned long)(SHARED_N+15)/16,final.checks);
#endif
    std::fflush(nullptr); std::_Exit(0);
}
