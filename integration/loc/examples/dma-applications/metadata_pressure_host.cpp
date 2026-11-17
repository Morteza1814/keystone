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
#include "metadata_pressure.h"

struct Slot {
    Keystone::Enclave *enclave=nullptr;
    std::thread worker;
    pressure_reply reply{};
    pressure_command command{};
    bool ready=false, go=false, done=false;
};
static Slot slots[PRESSURE_TENANTS];
static std::mutex gate;
static std::condition_variable changed;
static thread_local unsigned current;
static void need(bool ok, const char *where) {
    if (!ok) { std::fprintf(stderr,"METADATA_PRESSURE_FAIL phase=%s\n",where); std::fflush(nullptr); std::_Exit(1); }
}
static void dispatch(void *buffer) {
    Slot &s=slots[current];
    auto *edge=static_cast<edge_call *>(buffer);
    const size_t size=s.enclave->getSharedBufferSize();
    need(edge->call_id==1 && edge->call_arg_size==sizeof(pressure_reply) &&
        edge->call_arg_offset<=size && sizeof(pressure_reply)<=size-edge->call_arg_offset,"ocall_bounds");
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
    mp_begin(MP_CREATE);
    Slot &s=slots[i]; s.enclave=new Keystone::Enclave;
    Keystone::Params params; params.setFreeMemSize(2UL*1024*1024);
    params.setUntrustedMem(PRESSURE_UTM,PRESSURE_UTM_BYTES);
    need(s.enclave->init(app,rt,params)==Keystone::Error::Success,"create");
    need(s.enclave->registerOcallDispatch(dispatch)==Keystone::Error::Success,"dispatch");
    mp_end(MP_CREATE);
    mp_begin(MP_ENTER);
    s.worker=std::thread([i]{
        current=i; uintptr_t value=UINTPTR_MAX;
        need(slots[i].enclave->run(&value)==Keystone::Error::Success,"run");
        if (value!=PRESSURE_PASS) std::fprintf(stderr,"METADATA_PRESSURE_APP_ERROR tenant=%u code=%#lx\n",i,value);
        need(value==PRESSURE_PASS,"eapp_result");
        mp_end(MP_EXIT);
        std::lock_guard<std::mutex> lock(gate); slots[i].done=true; changed.notify_all();
    });
    std::unique_lock<std::mutex> lock(gate);
    changed.wait(lock,[&s]{return s.ready || s.done;});
    need(s.ready && s.reply.action==MP_HELLO && s.reply.principal,"hello");
}
static pressure_reply step(unsigned i, uint64_t action, uint64_t count=0,
    uint64_t principal=0, const uint64_t *objects=nullptr, uint64_t round=0, uint64_t variant=0) {
    Slot &s=slots[i]; std::unique_lock<std::mutex> lock(gate);
    need(s.ready && !s.go && !s.done,"step_ready");
    s.command={}; s.command.action=action; s.command.tenant=i;
    s.command.count=count; s.command.principal=principal;
    s.command.round=round; s.command.variant=variant;
    if(objects) std::memcpy(s.command.objects,objects,count*sizeof(uint64_t));
    s.ready=false; mp_begin(MP_SWITCH); s.go=true; changed.notify_all();
    changed.wait(lock,[&s]{return s.ready || s.done;});
    if(action==MP_STOP) { need(s.done,"stopped"); return s.reply; }
    need(s.ready && s.reply.action==action,"reply_action");
    std::printf("METADATA_ACTION tenant=%u action=%lu count=%lu round=%lu principal=%lu calls=%lu checks=%lu variant=%lu\n",
        i,action,count,round,s.reply.principal,s.reply.calls,s.reply.checks,variant);
    return s.reply;
}
static void destroy(unsigned i) {
    slots[i].worker.join(); mp_begin(MP_DESTROY);
    need(slots[i].enclave->destroy()==Keystone::Error::Success,"destroy"); mp_end(MP_DESTROY);
}
#if PRESSURE_REVOCATION
static void revocation_trial(unsigned count,unsigned cold,unsigned selective,unsigned repeat) {
    std::printf("REVOCATION_CASE objects=%u cold=%u selective=%u repeat=%u\n",count,cold,selective,repeat);
    mp_begin(MP_WINDOW_BEGIN); mp_end(MP_WINDOW_BEGIN);
    auto owner=step(0,MP_ALLOC,count); step(0,MP_OWNER_GRANT,count); step(0,MP_UPLOAD,count);
    for(unsigned t=1;t<PRESSURE_TENANTS;++t) {
        step(t,MP_ATTACH,count,0,owner.objects);
        step(t,MP_AUDIT,count,0,nullptr,0,0); // No grant yet.
        step(0,MP_GRANT,count,slots[t].reply.principal);
        step(t,MP_VERIFY,count);
    }
    if(cold) for(unsigned t=1;t<PRESSURE_TENANTS;++t)
        step(t,MP_CHURN,PRESSURE_CHURN/(PRESSURE_TENANTS-1));
    if(selective) {
        step(0,MP_REVOKE,count,slots[1].reply.principal);
        for(unsigned t=0;t<PRESSURE_TENANTS;++t)
            step(t,MP_AUDIT,count,0,nullptr,0,t!=1);
        step(0,MP_FREE,count,0,nullptr,0,1); // Unmeasured cleanup; grants remain for peers.
    } else step(0,MP_FREE,count);
    for(unsigned t=0;t<PRESSURE_TENANTS;++t) step(t,MP_AUDIT,count,0,nullptr,0,0);
    if(cold) for(unsigned t=1;t<PRESSURE_TENANTS;++t)
        step(t,MP_CHURN_FREE,PRESSURE_CHURN/(PRESSURE_TENANTS-1));
    mp_begin(MP_WINDOW_END); mp_end(MP_WINDOW_END);
}
#endif
int main(int argc,char **argv) {
    need(argc==3,"arguments"); mp_end(MP_SETUP);
    for(unsigned t=0;t<PRESSURE_TENANTS;++t) start(t,argv[1],argv[2]);
    for(unsigned t=0;t<PRESSURE_TENANTS;++t) {
        for(unsigned u=0;u<t;++u) need(slots[t].reply.principal!=slots[u].reply.principal,"distinct_principals");
        step(t,MP_INIT);
    }
    const unsigned counts[]={PRESSURE_CASES};
#if PRESSURE_REVOCATION
    for(unsigned count:counts)
        for(unsigned cold=0;cold<2;++cold) {
            if(!cold && count*PRESSURE_TENANTS>64) continue; // Genuine cache-only reference.
            for(unsigned selective=0;selective<2;++selective)
                for(unsigned repeat=0;repeat<PRESSURE_MEASURED;++repeat)
                    revocation_trial(count,cold,selective,repeat);
        }
#else
    for(unsigned count:counts) {
        auto owner=step(0,MP_ALLOC,count); step(0,MP_OWNER_GRANT,count); step(0,MP_UPLOAD,count);
        for(unsigned t=1;t<PRESSURE_TENANTS;++t) {
            step(t,MP_ATTACH,count,0,owner.objects); step(t,MP_DENIED,count,0,nullptr,0,0);
            step(0,MP_GRANT,count,slots[t].reply.principal);
            step(t,MP_VERIFY,count); step(t,MP_DENIED,count,0,nullptr,0,2);
        }
        for(unsigned round=0;round<PRESSURE_WARMUPS+PRESSURE_MEASURED;++round)
            for(unsigned t=0;t<PRESSURE_TENANTS;++t) step(t,MP_ACCESS,count,0,nullptr,round);
        for(unsigned t=1;t<PRESSURE_TENANTS;++t) {
            step(0,MP_REVOKE,count,slots[t].reply.principal);
            step(t,MP_DENIED,count,0,nullptr,0,1);
        }
        step(0,MP_VERIFY,count); step(0,MP_FREE,count);
    }
#endif
    for(unsigned t=0;t<PRESSURE_TENANTS;++t) { step(t,MP_HOST_FREE); step(t,MP_STOP); destroy(t); }
    std::printf("%s tenants=%u cases=%lu calls=%lu warmups=%u measured=%u bytes=%lu\n",
        PRESSURE_REVOCATION?"REVOCATION_PRESSURE_PASS":"METADATA_PRESSURE_PASS",
        PRESSURE_TENANTS,sizeof(counts)/sizeof(counts[0]),PRESSURE_CALLS,PRESSURE_REVOCATION?0:PRESSURE_WARMUPS,PRESSURE_MEASURED,PRESSURE_BYTES);
    std::fflush(nullptr); std::_Exit(0);
}
