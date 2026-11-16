#include <asm/unistd.h>
#include <sys/mman.h>
#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "loc_pim.h"
#include "host_kernels.h"
#include "sharing_app.h"

#define IN_RIGHTS (0x01ULL|0x08ULL)
#define WORK_RIGHTS (IN_RIGHTS|0x02ULL|M5_PIM_PERM_DMA_READ_OBJECT|M5_PIM_PERM_DMA_WRITE_OBJECT)
static uintptr_t arena;
static uint64_t tenant, input[2], workspace[2], checks;
static unsigned char *output;
static struct share_reply reply;
#if SHARE_KIND == 1
static int32_t *w,*x,*a,*b,*ax,*correction,*bias,*base;
#elif SHARE_KIND == 2
static int32_t *features[2];
static uint32_t *labels;
static unsigned long neighbors[5];
#else
static uint64_t *keys;
#endif
static void need(int ok,unsigned long code) { if(!ok) { EAPP_RETURN(code); } }
static void deny(int ok,unsigned long code) { need(ok,code); ++checks; }
static void complete(loc_pim_request q) { need(q && !loc_pim_complete(q),0xfc01); }
static uintptr_t alloc_arena(void) {
    register uintptr_t a0 __asm__("a0")=0,a1 __asm__("a1")=SHARE_ARENA_BYTES;
    register uintptr_t a2 __asm__("a2")=PROT_READ|PROT_WRITE;
    register uintptr_t a3 __asm__("a3")=MAP_PRIVATE|MAP_ANONYMOUS;
    register uintptr_t a4 __asm__("a4")=(uintptr_t)-1,a5 __asm__("a5")=0;
    register uintptr_t a7 __asm__("a7")=__NR_mmap;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a5),"r"(a7) : "memory");
    need(a0 && a0!=UINTPTR_MAX && !(a0&4095),0xfc02); return a0;
}
static void initialize(uint64_t t) {
    need(!arena && t<SHARE_TENANTS,0xfc03); tenant=t;
    sp_begin(SP_ALLOC_HOST); arena=alloc_arena();
#if SHARE_KIND == 1
    w=(int32_t *)(arena+64); x=w+SHARE_N*SHARE_N; a=x+SHARE_N;
    b=a+8*SHARE_N; ax=b+8*SHARE_N; correction=ax+8;
    bias=correction+SHARE_N; base=bias+SHARE_N; output=(unsigned char *)(base+SHARE_N);
#elif SHARE_KIND == 2
    features[0]=(int32_t *)(arena+64); features[1]=features[0]+SHARE_N;
    labels=(uint32_t *)(features[1]+SHARE_N); output=(unsigned char *)(labels+SHARE_N);
#else
    keys=(uint64_t *)(arena+64); output=(unsigned char *)(keys+SHARE_N);
#endif
    need((uintptr_t)output+SHARE_OUTPUT_BYTES<arena+SHARE_ARENA_BYTES-64,0xfc04);
    sp_end(SP_ALLOC_HOST); sp_begin(SP_INITIALIZE);
    for(unsigned i=0;i<64;++i) { ((unsigned char *)arena)[i]=0xd3; ((unsigned char *)(arena+SHARE_ARENA_BYTES-64))[i]=0xd3; }
#if SHARE_KIND == 1
    for(unsigned long c=0;c<SHARE_N;++c) {
        x[c]=share_x(t,c); bias[c]=share_bias(t,c); base[c]=0;
        for(unsigned long r=0;r<SHARE_N;++r) w[c*SHARE_N+r]=share_w(r,c);
        for(unsigned long k=0;k<8;++k) { a[k*SHARE_N+c]=share_a(t,k,c); b[c*8+k]=share_b(t,c,k); }
    }
#elif SHARE_KIND == 2
    for(unsigned long i=0;i<SHARE_N;++i) {
        features[0][i]=share_feature(i,0); features[1][i]=share_feature(i,1);
        labels[i]=(i+t)%3;
    }
#else
    for(unsigned long i=0;i<SHARE_N;++i) keys[i]=share_key(i);
#endif
    sp_end(SP_INITIALIZE);
}
static void allocate_private(void) {
    for(unsigned long j=0;j<SHARE_PRIVATE;++j) {
#if SHARE_KIND == 1
        workspace[j]=loc_pim_alloc_placed(SHARE_N,1,input[0]);
#else
        workspace[j]=loc_pim_alloc(SHARE_OUTPUT_BYTES);
#endif
        need(workspace[j],0xfc05);
    }
}
static void grant_private(void) {
    for(unsigned long j=0;j<SHARE_PRIVATE;++j) need(!loc_pim_grant(workspace[j],WORK_RIGHTS),0xfc06);
}
static void load(void) {
    need((SHARE_DUPLICATED || !tenant) && !input[0],0xfc07);
    sp_begin(SP_ALLOC_PIM);
    for(unsigned long j=0;j<SHARE_INPUTS;++j) {
#if SHARE_KIND == 1
        input[j]=loc_pim_alloc_placed(SHARE_N,SHARE_N,0);
#else
        input[j]=loc_pim_alloc(SHARE_INPUT_BYTES);
#endif
        need(input[j],0xfc08);
    }
    /* Associated placement requires an installed input capability. Keep the
     * owner grant explicit before allocating the associated accumulator. */
    sp_end(SP_ALLOC_PIM); sp_begin(SP_OWN_GRANT);
    for(unsigned long j=0;j<SHARE_INPUTS;++j) need(!loc_pim_grant(input[j],IN_RIGHTS|M5_PIM_PERM_DMA_WRITE_OBJECT),0xfc09);
    sp_end(SP_OWN_GRANT); sp_begin(SP_ALLOC_PIM);
    allocate_private(); sp_end(SP_ALLOC_PIM);
    sp_begin(SP_OWN_GRANT); grant_private(); sp_end(SP_OWN_GRANT);
    sp_begin(SP_UPLOAD);
#if SHARE_KIND == 1
    for(unsigned long c=0;c<SHARE_N;c+=16) {
        unsigned long n=SHARE_N-c<16 ? SHARE_N-c : 16;
        need(!loc_pim_upload_at(input[0],c*4*SHARE_N,w+c*SHARE_N,n*4*SHARE_N),0xfc0a);
    }
#elif SHARE_KIND == 2
    for(unsigned long j=0;j<2;++j) need(!loc_pim_upload(input[j],features[j],SHARE_INPUT_BYTES),0xfc0b);
#else
    need(!loc_pim_upload(input[0],keys,SHARE_INPUT_BYTES),0xfc0c);
#endif
    sp_end(SP_UPLOAD);
}
static void project(void) {
    need(input[0] && workspace[0],0xfc10);
#if SHARE_KIND == 1
    sp_begin(SP_PREPARE);
    for(unsigned long i=0;i<SHARE_N;++i) base[i]=0;
    sp_end(SP_PREPARE); sp_begin(SP_UPLOAD);
    need(!loc_pim_upload(workspace[0],base,SHARE_OUTPUT_BYTES),0xfc11);
    sp_end(SP_UPLOAD);
#endif
    sp_begin(SP_COMPUTE);
#if SHARE_KIND == 1
    for(unsigned long c=0;c<SHARE_N;++c) complete(loc_pim_axpy_range(input[0],workspace[0],x[c],c*4*SHARE_N,4*SHARE_N));
#elif SHARE_KIND == 2
    for(unsigned long j=0;j<2;++j) {
        complete(loc_pim_submit_scalar(M5_PIM_OP_SUB_SCALAR_INT32,input[j],share_query(tenant,j),workspace[j]));
        complete(loc_pim_submit(M5_PIM_OP_ABS_INT32,workspace[j],0,workspace[j]));
    }
    complete(loc_pim_submit(M5_PIM_OP_ADD_INT32,workspace[0],workspace[1],workspace[0]));
#else
    complete(loc_pim_submit_scalar(M5_PIM_OP_LT_SCALAR_UINT64_BOOL,input[0],(int64_t)share_threshold(tenant),workspace[0]));
#endif
    sp_end(SP_COMPUTE); sp_begin(SP_DOWNLOAD);
#if SHARE_KIND == 1
    need(!loc_pim_download(base,workspace[0],SHARE_OUTPUT_BYTES),0xfc12);
#else
    need(!loc_pim_download(output,workspace[0],SHARE_OUTPUT_BYTES),0xfc13);
#endif
    sp_end(SP_DOWNLOAD);
#if SHARE_KIND == 1
    sp_begin(SP_ADAPTER);
    need(loc_host_lora_adapter(a,b,x,ax,correction,SHARE_N,SHARE_N,8),0xfc14);
    sp_end(SP_ADAPTER); sp_begin(SP_COMBINE);
    need(loc_host_lora_combine(base,correction,bias,(int32_t *)output,SHARE_N,1),0xfc15);
#elif SHARE_KIND == 2
    sp_begin(SP_COMBINE);
    reply.result[0]=loc_host_knn_heap_vote(labels,(int32_t *)output,neighbors,SHARE_N);
    for(unsigned k=0;k<5;++k) reply.result[k+1]=neighbors[k];
#else
    sp_begin(SP_COMBINE);
    loc_host_filter_aggregate_unrolled(keys,output,SHARE_N,&reply.result[0],&reply.result[1]);
#endif
    sp_end(SP_COMBINE);
}
static void revoked(void) {
    sp_begin(SP_DENIALS);
#if SHARE_KIND == 1
    loc_pim_request q=loc_pim_axpy_range(input[0],workspace[0],1,0,4*SHARE_N);
#elif SHARE_KIND == 2
    loc_pim_request q=loc_pim_submit_scalar(M5_PIM_OP_SUB_SCALAR_INT32,input[0],share_query(tenant,0),workspace[0]);
#else
    loc_pim_request q=loc_pim_submit_scalar(M5_PIM_OP_LT_SCALAR_UINT64_BOOL,input[0],(int64_t)share_threshold(tenant),workspace[0]);
#endif
    deny(q && loc_pim_complete(q)!=0,0xfc20);
    deny(loc_pim_upload(input[0],output,8)!=0,0xfc21);
    /* Revocation of shared input must not revoke this tenant's private output. */
#if SHARE_KIND == 1
    need(!loc_pim_download(base,workspace[0],SHARE_OUTPUT_BYTES),0xfc22);
#else
    need(!loc_pim_download(output,workspace[0],SHARE_OUTPUT_BYTES),0xfc22);
#endif
    sp_end(SP_DENIALS);
}
static void verify(void) {
    sp_begin(SP_CHECK);
    /* Linear digest outside the measured region; an independent INT64 Python
     * reference checks all output bytes without simulating a quadratic oracle. */
    reply.digest=share_hash(output,SHARE_OUTPUT_BYTES);
    for(unsigned i=0;i<64;++i) need(((unsigned char *)arena)[i]==0xd3 && ((unsigned char *)(arena+SHARE_ARENA_BYTES-64))[i]==0xd3,0xfc30);
    sp_end(SP_CHECK);
}
void EAPP_ENTRY eapp_entry(void) {
    sp_end(SP_ENTER); struct share_command c={0};
    reply.principal=loc_pim_principal(); need(reply.principal,0xfc00);
    for(;;) {
        for(unsigned j=0;j<2;++j) { reply.input[j]=input[j]; reply.workspace[j]=workspace[j]; }
        reply.checks=checks;
        need(!ocall(1,&reply,sizeof(reply),&c,sizeof(c)),0xfc40); sp_end(SP_SWITCH);
        reply.action=c.action;
        switch(c.action) {
        case SA_INIT: initialize(c.tenant); break;
        case SA_LOAD: load(); break;
        case SA_ATTACH:
            need(tenant && !input[0],0xfc41);
            for(unsigned j=0;j<SHARE_INPUTS;++j) { need(c.input[j],0xfc42); input[j]=c.input[j]; }
            sp_begin(SP_ALLOC_PIM); allocate_private(); sp_end(SP_ALLOC_PIM);
            sp_begin(SP_OWN_GRANT); grant_private(); sp_end(SP_OWN_GRANT); break;
        case SA_GRANT:
            need(!tenant && c.principal!=reply.principal,0xfc43); sp_begin(SP_GRANT);
            for(unsigned j=0;j<SHARE_INPUTS;++j) need(!loc_pim_grant_to(input[j],c.principal,IN_RIGHTS),0xfc44);
            sp_end(SP_GRANT); break;
        case SA_PROJECT: project(); break;
        case SA_REVOKE:
            need(!tenant,0xfc45); sp_begin(SP_REVOKE);
            for(unsigned j=0;j<SHARE_INPUTS;++j) need(!loc_pim_revoke_from(input[j],c.principal),0xfc46);
            sp_end(SP_REVOKE); break;
        case SA_REVOKED: revoked(); break;
        case SA_DENIED:
            sp_begin(SP_DENIALS);
            deny(loc_pim_free(c.foreign)!=0,0xfc47);
            deny(loc_pim_download(output,c.foreign,8)!=0,0xfc48);
            deny(loc_pim_upload(c.foreign,output,8)!=0,0xfc49);
            sp_end(SP_DENIALS); break;
        case SA_RELEASE:
            sp_begin(SP_FREE_PIM);
            for(unsigned j=0;j<SHARE_PRIVATE;++j) { need(!loc_pim_free(workspace[j]),0xfc50); workspace[j]=0; }
            if(SHARE_DUPLICATED || !tenant)
                for(unsigned j=0;j<SHARE_INPUTS;++j) need(!loc_pim_free(input[j]),0xfc51);
            input[0]=input[1]=0; sp_end(SP_FREE_PIM); break;
        case SA_VERIFY: verify(); break;
        case SA_FREE_HOST:
            sp_begin(SP_FREE_HOST); need(!SYSCALL_2(__NR_munmap,arena,SHARE_ARENA_BYTES),0xfc52);
            arena=0; sp_end(SP_FREE_HOST); break;
        case SA_STOP:
            need(!arena && !workspace[0] && !input[0],0xfc53);
            sp_begin(SP_EXIT); EAPP_RETURN(SHARE_PASS); break;
        default: need(0,0xfc54);
        }
    }
}
