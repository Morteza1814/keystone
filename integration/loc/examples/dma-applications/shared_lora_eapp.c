/* Two independent Eyrie instances; only public handles/control use OCALL. */
#include <asm/unistd.h>
#include <sys/mman.h>
#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "loc_pim.h"
#include "host_kernels.h"
#include "shared_lora.h"

#define INPUT_RIGHTS (0x01ULL | 0x08ULL)
#define ACC_RIGHTS (INPUT_RIGHTS | 0x02ULL | M5_PIM_PERM_DMA_READ_OBJECT | M5_PIM_PERM_DMA_WRITE_OBJECT)
static uintptr_t arena;
static uint64_t tenant, arena_bytes, weights, accumulator, checks;
static int32_t *w, *x, *a, *b, *ax, *correction, *bias, *output;
static struct shared_reply reply;

static void need(int ok, unsigned long error) { if (!ok) { EAPP_RETURN(error); } }
static void denied(int ok, unsigned long error) { need(ok, error); ++checks; }
static uintptr_t alloc_arena(unsigned long bytes) {
    register uintptr_t a0 __asm__("a0")=0, a1 __asm__("a1")=bytes;
    register uintptr_t a2 __asm__("a2")=PROT_READ|PROT_WRITE;
    register uintptr_t a3 __asm__("a3")=MAP_PRIVATE|MAP_ANONYMOUS;
    register uintptr_t a4 __asm__("a4")=(uintptr_t)-1, a5 __asm__("a5")=0;
    register uintptr_t a7 __asm__("a7")=__NR_mmap;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a5),"r"(a7) : "memory");
    need(a0 && a0 != UINTPTR_MAX && !(a0 & 4095), 0xfb01);
    return a0;
}
static void initialize(uint64_t id) {
    need(!arena && id < 2, 0xfb02); tenant=id;
    sh_begin(SH_ALLOC_HOST);
    arena_bytes=SHARED_ARENA_BYTES(SHARED_HOST_WEIGHTS(tenant));
    arena=alloc_arena(arena_bytes);
    x=(int32_t *)(arena + 64); a=x+SHARED_N; b=a+SHARED_RANK*SHARED_N;
    ax=b+SHARED_N*SHARED_RANK; correction=ax+SHARED_RANK;
    bias=correction+SHARED_N; output=bias+SHARED_N; w=output+SHARED_N;
    need((uintptr_t)(w+(SHARED_HOST_WEIGHTS(tenant) ? SHARED_N*SHARED_N : 0)) < arena+arena_bytes-64, 0xfb03);
    sh_end(SH_ALLOC_HOST);
    sh_begin(SH_INITIALIZE);
    for (unsigned long i=0; i<64; ++i) { ((volatile unsigned char *)arena)[i]=0xd3; ((volatile unsigned char *)(arena+arena_bytes-64))[i]=0xd3; }
    for (unsigned long i=0; i<SHARED_RANK; ++i)
        for (unsigned long j=0; j<SHARED_N; ++j) a[i*SHARED_N+j]=sh_a(tenant,i,j);
    for (unsigned long row=0; row<SHARED_N; ++row) {
        bias[row]=sh_bias(tenant,row); output[row]=0;
        for (unsigned long i=0; i<SHARED_RANK; ++i) b[row*SHARED_RANK+i]=sh_b(tenant,row,i);
    }
    if (SHARED_HOST_WEIGHTS(tenant)) for (unsigned long col=0; col<SHARED_N; ++col)
        for (unsigned long row=0; row<SHARED_N; ++row) w[col*SHARED_N+row]=sh_w(row,col);
    sh_end(SH_INITIALIZE);
}
static void load(void) {
    need(SHARED_OWNS_WEIGHTS(tenant) && !weights, 0xfb10);
    sh_begin(SH_ALLOC_PIM);
    weights=loc_pim_alloc_placed(SHARED_N,SHARED_N,0);
    need(weights && !loc_pim_grant(weights,INPUT_RIGHTS|M5_PIM_PERM_DMA_WRITE_OBJECT),0xfb11);
    accumulator=loc_pim_alloc_placed(SHARED_N,1,weights);
    need(accumulator && !loc_pim_grant(accumulator,ACC_RIGHTS),0xfb12);
    sh_end(SH_ALLOC_PIM);
    for (unsigned long col=0; col<SHARED_N; col+=16) {
        unsigned long count=SHARED_N-col < 16 ? SHARED_N-col : 16;
        sh_begin(SH_UPLOAD);
        need(!loc_pim_upload_at(weights,col*SHARED_VECTOR_BYTES,w+col*SHARED_N,count*SHARED_VECTOR_BYTES),0xfb13);
        sh_end(SH_UPLOAD);
    }
}
static void project(unsigned long round) {
    need(weights && accumulator && round<3,0xfb20);
    sh_begin(SH_INITIALIZE);
    for (unsigned long j=0;j<SHARED_N;++j) { x[j]=sh_x(tenant,round,j); output[j]=0; }
    sh_end(SH_INITIALIZE);
    sh_begin(SH_UPLOAD);
    need(!loc_pim_upload(accumulator,output,SHARED_VECTOR_BYTES),0xfb21);
    sh_end(SH_UPLOAD);
    sh_begin(SH_COMPUTE);
    for (unsigned long col=0;col<SHARED_N;++col) {
        uint64_t q=loc_pim_axpy_range(weights,accumulator,x[col],col*SHARED_VECTOR_BYTES,SHARED_VECTOR_BYTES);
        need(q && !loc_pim_complete(q),0xfb22);
    }
    sh_end(SH_COMPUTE);
    sh_begin(SH_DOWNLOAD);
    need(!loc_pim_download(output,accumulator,SHARED_VECTOR_BYTES),0xfb23);
    sh_end(SH_DOWNLOAD);
    sh_begin(SH_ADAPTER);
    need(loc_host_lora_adapter(a,b,x,ax,correction,SHARED_N,SHARED_N,SHARED_RANK),0xfb24);
    sh_end(SH_ADAPTER);
    sh_begin(SH_COMBINE);
    need(loc_host_lora_combine(output,correction,bias,reply.output,SHARED_N,1),0xfb25);
    sh_end(SH_COMBINE);
    sh_begin(SH_CHECK);
    if (LOC_DMA_APP_VERIFY_RESULTS) {
    int64_t ref_ax[SHARED_RANK];
    for (unsigned long i=0;i<SHARED_RANK;++i) {
        int64_t sum=0;
        for (unsigned long col=0;col<SHARED_N;++col) sum+=(int64_t)sh_a(tenant,i,col)*sh_x(tenant,round,col);
        need(sum>=INT32_MIN && sum<=INT32_MAX && ax[i]==sum,0xfb26); ref_ax[i]=sum;
    }
    for (unsigned long row=0;row<SHARED_N;++row) {
        int64_t base=0,delta=0;
        for (unsigned long col=0;col<SHARED_N;++col) base+=(int64_t)sh_w(row,col)*sh_x(tenant,round,col);
        for (unsigned long i=0;i<SHARED_RANK;++i) delta+=(int64_t)sh_b(tenant,row,i)*ref_ax[i];
        int64_t expected=base+delta+sh_bias(tenant,row);
        need(base==output[row] && delta==correction[row] && expected>=INT32_MIN && expected<=INT32_MAX && expected==reply.output[row],0xfb27);
    }
    for (unsigned long i=0;i<64;++i) need(((volatile unsigned char *)arena)[i]==0xd3 && ((volatile unsigned char *)(arena+arena_bytes-64))[i]==0xd3,0xfb28);
    }
    sh_end(SH_CHECK); reply.round=round;
}
static void denial_tests(const struct shared_command *c) {
    sh_begin(SH_DENIALS);
    uint64_t own=loc_pim_principal();
    denied(loc_pim_free(c->accumulator)!=0,0xfb30);
    denied(loc_pim_grant(c->accumulator,ACC_RIGHTS)!=0,0xfb31);
    denied(loc_pim_grant_to(c->accumulator,own,ACC_RIGHTS)!=0,0xfb32);
    denied(loc_pim_download(output,c->accumulator,SHARED_VECTOR_BYTES)!=0,0xfb33);
    denied(loc_pim_upload(c->accumulator,output,SHARED_VECTOR_BYTES)!=0,0xfb34);
    uint64_t q=loc_pim_axpy_range(weights,c->accumulator,1,0,SHARED_VECTOR_BYTES);
    denied(q && loc_pim_complete(q)!=0,0xfb35);
    q=loc_pim_axpy_range(weights,accumulator,1,SHARED_WEIGHT_BYTES,SHARED_VECTOR_BYTES);
    denied(q && loc_pim_complete(q)!=0,0xfb36);
    denied(loc_pim_download((void *)SHARED_UTM,accumulator,SHARED_VECTOR_BYTES)!=0,0xfb37);
    m5_pim_set_tokens(0,c->principal);
    denied(loc_pim_principal()==own,0xfb38);
    if (tenant) {
        denied(loc_pim_free(weights)!=0,0xfb39);
        denied(loc_pim_grant_to(weights,own,ACC_RIGHTS)!=0,0xfb3a);
        denied(loc_pim_revoke_from(weights,c->principal)!=0,0xfb3b);
        denied(loc_pim_upload_at(weights,0,output,SHARED_VECTOR_BYTES)!=0,0xfb3c);
        denied(loc_pim_download(output,weights,SHARED_VECTOR_BYTES)!=0,0xfb3d);
    }
    sh_end(SH_DENIALS);
}
void EAPP_ENTRY eapp_entry(void) {
    sh_end(SH_ENTER);
    struct shared_command c={0};
    reply.principal=loc_pim_principal(); need(reply.principal,0xfb00);
    for (;;) {
        reply.weights=weights; reply.accumulator=accumulator; reply.checks=checks;
        need(!ocall(1,&reply,sizeof(reply),&c,sizeof(c)),0xfb40);
        sh_end(SH_SWITCH);
        reply.action=c.action;
        if (c.action==SH_INIT) initialize(c.tenant);
        else if (c.action==SH_LOAD) load();
        else if (c.action==SH_BEFORE_GRANT) {
            sh_begin(SH_DENIALS);
            denied(!loc_pim_alloc_placed(SHARED_N,1,c.weights),0xfb41);
            denied(loc_pim_grant_to(c.weights,reply.principal,INPUT_RIGHTS)!=0,0xfb42);
            sh_end(SH_DENIALS);
        } else if (c.action==SH_GRANT) {
            need(!tenant && c.principal!=reply.principal,0xfb43);
            sh_begin(SH_SHARE); need(!loc_pim_grant_to(weights,c.principal,INPUT_RIGHTS),0xfb44); sh_end(SH_SHARE);
        } else if (c.action==SH_ATTACH) {
            need(tenant && !weights && !accumulator,0xfb45); weights=c.weights;
            sh_begin(SH_ALLOC_PIM); accumulator=loc_pim_alloc_placed(SHARED_N,1,weights);
            need(accumulator && !loc_pim_grant(accumulator,ACC_RIGHTS),0xfb46); sh_end(SH_ALLOC_PIM);
        } else if (c.action==SH_PROJECT) project(c.round);
        else if (c.action==SH_DENIED) denial_tests(&c);
        else if (c.action==SH_REVOKE) {
            need(!tenant,0xfb47); sh_begin(SH_UNSHARE);
            need(!loc_pim_revoke_from(weights,c.principal),0xfb48); sh_end(SH_UNSHARE);
        } else if (c.action==SH_REVOKED) {
            sh_begin(SH_DENIALS);
            uint64_t q=loc_pim_axpy_range(weights,accumulator,1,0,SHARED_VECTOR_BYTES);
            denied(q && loc_pim_complete(q)!=0,0xfb49);
            denied(!loc_pim_alloc_placed(SHARED_N,1,weights),0xfb4a);
            denied(loc_pim_grant_to(weights,reply.principal,INPUT_RIGHTS)!=0,0xfb4b);
            /* Failed requests must leave the prior private result intact. */
            need(!loc_pim_download(output,accumulator,SHARED_VECTOR_BYTES),0xfb4c);
            for (unsigned long row=0;row<SHARED_N;++row) {
                int64_t base=0;
                for (unsigned long col=0;col<SHARED_N;++col) base+=(int64_t)sh_w(row,col)*sh_x(tenant,reply.round,col);
                need(output[row]==base,0xfb4d);
            }
            sh_end(SH_DENIALS);
        } else if (c.action==SH_STALE) {
            sh_begin(SH_DENIALS);
            denied(loc_pim_grant_to(weights,c.principal,INPUT_RIGHTS)!=0,0xfb4e);
            sh_end(SH_DENIALS);
        } else if (c.action==SH_RELEASE) {
            sh_begin(SH_FREE_PIM); need(!loc_pim_free(accumulator),0xfb50); accumulator=0;
            if (SHARED_OWNS_WEIGHTS(tenant)) need(!loc_pim_free(weights),0xfb51);
            weights=0; sh_end(SH_FREE_PIM);
            sh_begin(SH_FREE_HOST); need(!SYSCALL_2(__NR_munmap,arena,arena_bytes),0xfb52); arena=0; sh_end(SH_FREE_HOST);
        } else if (c.action==SH_STOP) {
            need(!arena && !accumulator && !weights,0xfb53); sh_begin(SH_EXIT); EAPP_RETURN(SHARED_PASS);
        } else need(0,0xfb54);
    }
}
