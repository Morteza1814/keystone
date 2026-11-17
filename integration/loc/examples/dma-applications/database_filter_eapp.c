#include <asm/unistd.h>
#include <sys/mman.h>
#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "loc_pim.h"
#include "host_kernels.h"
#include "database_filter.h"

#define INPUT_RIGHTS (0x01ULL|0x08ULL)
#define PRIVATE_RIGHTS (INPUT_RIGHTS|0x02ULL|M5_PIM_PERM_DMA_READ_OBJECT|M5_PIM_PERM_DMA_WRITE_OBJECT)
static uintptr_t arena;
static uint64_t tenant, input[DB_PARTS], workspace, churn[DB_CHURN];
static uint64_t *keys;
static unsigned char *output;
static unsigned churn_live, input_live;
static struct db_reply reply;
static void need(int ok,unsigned long code) { if(!ok) { EAPP_RETURN(code); } }
static void deny(int ok,unsigned long code) { need(ok,code); ++reply.checks; }
static void complete(loc_pim_request q) { need(q && !loc_pim_complete(q),0xfd01); }
static void initialize(uint64_t t) {
    need(!arena && t<DB_TENANTS,0xfd02); tenant=t;
    sp_begin(SP_ALLOC_HOST);
    register uintptr_t a0 __asm__("a0")=0,a1 __asm__("a1")=DB_ARENA_BYTES;
    register uintptr_t a2 __asm__("a2")=PROT_READ|PROT_WRITE;
    register uintptr_t a3 __asm__("a3")=MAP_PRIVATE|MAP_ANONYMOUS;
    register uintptr_t a4 __asm__("a4")=(uintptr_t)-1,a5 __asm__("a5")=0;
    register uintptr_t a7 __asm__("a7")=__NR_mmap;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a5),"r"(a7) : "memory");
    arena=a0; need(arena && arena!=UINTPTR_MAX && !(arena&4095),0xfd03);
    keys=(uint64_t *)(arena+64); output=(unsigned char *)(keys+DB_N);
    need((uintptr_t)output+DB_MASK_BYTES<=arena+DB_ARENA_BYTES-64,0xfd04);
    sp_end(SP_ALLOC_HOST); sp_begin(SP_INITIALIZE);
    for(unsigned i=0;i<64;++i) {
        ((unsigned char *)arena)[i]=0xd3;
        ((unsigned char *)(arena+DB_ARENA_BYTES-64))[i]=0xd3;
    }
    for(unsigned long i=0;i<DB_N;++i) keys[i]=db_key(i);
    sp_end(SP_INITIALIZE);
}
static void allocate_private(void) {
    need(!workspace,0xfd05); sp_begin(SP_ALLOC_PIM);
    workspace=loc_pim_alloc(DB_OUTPUT_BYTES); need(workspace,0xfd06);
    sp_end(SP_ALLOC_PIM); sp_begin(SP_OWN_GRANT);
    need(!loc_pim_grant(workspace,PRIVATE_RIGHTS),0xfd07);
    sp_end(SP_OWN_GRANT);
}
static void load(void) {
    need(!tenant && !input_live,0xfd08);
    sp_begin(SP_ALLOC_PIM);
    for(unsigned i=0;i<DB_PARTS;++i) { input[i]=loc_pim_alloc(DB_INPUT_BYTES); need(input[i],0xfd09); }
    input_live=1; sp_end(SP_ALLOC_PIM); sp_begin(SP_OWN_GRANT);
    for(unsigned i=0;i<DB_PARTS;++i)
        need(!loc_pim_grant(input[i],INPUT_RIGHTS|M5_PIM_PERM_DMA_WRITE_OBJECT),0xfd0a);
    sp_end(SP_OWN_GRANT); allocate_private(); sp_begin(SP_UPLOAD);
    for(unsigned i=0;i<DB_PARTS;++i)
        need(!loc_pim_upload(input[i],keys+i*DB_KEYS_PER_PART,DB_INPUT_BYTES),0xfd0b);
    sp_end(SP_UPLOAD);
}
static loc_pim_request submit(unsigned i) {
    return loc_pim_submit_scalar(M5_PIM_OP_LT_SCALAR_UINT64_BOOL,input[i],
                                (int64_t)db_threshold(tenant),workspace);
}
static void query(void) {
    need(workspace && input[0],0xfd10);
    for(unsigned i=0;i<DB_PARTS;++i) {
        sp_begin(SP_COMPUTE); complete(submit(i)); sp_end(SP_COMPUTE);
        sp_begin(SP_DOWNLOAD);
        need(!loc_pim_download(output+i*DB_OUTPUT_BYTES,workspace,DB_OUTPUT_BYTES),0xfd11);
        sp_end(SP_DOWNLOAD);
    }
    sp_begin(SP_COMBINE);
    loc_host_filter_aggregate_unrolled(keys,output,DB_N,&reply.matches,&reply.sum);
    sp_end(SP_COMBINE);
}
static void clear_pressure(void) {
    need(tenant && churn_live,0xfd12); sp_begin(SP_FREE_PIM);
    for(unsigned i=0;i<DB_CHURN;++i) { need(!loc_pim_free(churn[i]),0xfd13); churn[i]=0; }
    churn_live=0; sp_end(SP_FREE_PIM);
}
static void pressure(void) {
    need(tenant,0xfd14);
    if(churn_live) clear_pressure();
    sp_begin(SP_ALLOC_PIM);
    for(unsigned i=0;i<DB_CHURN;++i) { churn[i]=loc_pim_alloc(64); need(churn[i],0xfd15); }
    sp_end(SP_ALLOC_PIM); sp_begin(SP_OWN_GRANT);
    for(unsigned i=0;i<DB_CHURN;++i) need(!loc_pim_grant(churn[i],PRIVATE_RIGHTS),0xfd16);
    churn_live=1; sp_end(SP_OWN_GRANT);
}
static void reject_inputs(void) {
    for(unsigned i=0;i<DB_PARTS;++i) {
        loc_pim_request q=submit(i);
        deny(q && loc_pim_complete(q)!=0,0xfd20);
    }
}
void EAPP_ENTRY eapp_entry(void) {
    sp_end(SP_ENTER); struct db_command c={0};
    reply.principal=loc_pim_principal(); need(reply.principal,0xfd00);
    for(;;) {
        for(unsigned i=0;i<DB_PARTS;++i) reply.input[i]=input[i];
        reply.workspace=workspace;
        need(!ocall(1,&reply,sizeof(reply),&c,sizeof(c)),0xfd30);
        sp_end(SP_SWITCH); reply.action=c.action;
        switch(c.action) {
        case DB_INIT: initialize(c.tenant); break;
        case DB_LOAD: load(); break;
        case DB_GRANT:
            need(!tenant && c.principal!=reply.principal,0xfd31); sp_begin(SP_GRANT);
            for(unsigned i=0;i<DB_PARTS;++i) need(!loc_pim_grant_to(input[i],c.principal,INPUT_RIGHTS),0xfd32);
            sp_end(SP_GRANT); break;
        case DB_ATTACH:
            need(tenant && !input[0],0xfd33);
            for(unsigned i=0;i<DB_PARTS;++i) { need(c.input[i],0xfd34); input[i]=c.input[i]; }
            allocate_private(); break;
        case DB_QUERY: query(); break;
        case DB_PRESSURE: pressure(); break;
        case DB_REVOKE:
            need(!tenant,0xfd35); sp_begin(SP_REVOKE);
            for(unsigned i=0;i<DB_PARTS;++i) need(!loc_pim_revoke_from(input[i],c.principal),0xfd36);
            sp_end(SP_REVOKE); break;
        case DB_REVOKED:
            need(tenant==1,0xfd37); sp_begin(SP_DENIALS); reject_inputs();
            /* The private result of the prior query remains accessible. */
            need(!loc_pim_download(output+(DB_PARTS-1)*DB_OUTPUT_BYTES,workspace,DB_OUTPUT_BYTES),0xfd38);
            sp_end(SP_DENIALS); break;
        case DB_SURVIVE:
            need(tenant!=1,0xfd39); sp_begin(SP_DENIALS);
            complete(submit(0)); sp_end(SP_DENIALS); break;
        case DB_FREE_INPUT:
            need(!tenant && input_live,0xfd3a); sp_begin(SP_FREE_PIM);
            for(unsigned i=0;i<DB_PARTS;++i) need(!loc_pim_free(input[i]),0xfd3b);
            input_live=0; sp_end(SP_FREE_PIM); break;
        case DB_FREED: sp_begin(SP_DENIALS); reject_inputs(); sp_end(SP_DENIALS); break;
        case DB_CLEAR_PRESSURE: clear_pressure(); break;
        case DB_FREE_PRIVATE:
            sp_begin(SP_FREE_PIM); need(!loc_pim_free(workspace),0xfd3c); workspace=0;
            sp_end(SP_FREE_PIM); break;
        case DB_VERIFY:
            sp_begin(SP_CHECK); reply.digest=share_hash(output,DB_MASK_BYTES);
            for(unsigned i=0;i<64;++i)
                need(((unsigned char *)arena)[i]==0xd3 && ((unsigned char *)(arena+DB_ARENA_BYTES-64))[i]==0xd3,0xfd3d);
            sp_end(SP_CHECK); break;
        case DB_FREE_HOST:
            sp_begin(SP_FREE_HOST); need(!SYSCALL_2(__NR_munmap,arena,DB_ARENA_BYTES),0xfd3e);
            arena=0; sp_end(SP_FREE_HOST); break;
        case DB_STOP:
            need(!arena && !workspace && !churn_live && !input_live,0xfd3f);
            sp_begin(SP_EXIT); EAPP_RETURN(DB_PASS); break;
        default: need(0,0xfd40);
        }
    }
}
