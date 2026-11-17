/* Permission checks inside independent Keystone enclaves. */
#include <asm/unistd.h>
#include <sys/mman.h>
#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "loc_pim.h"
#include "metadata_pressure.h"
#define PEER_RIGHTS (0x01ULL | M5_PIM_PERM_DMA_READ_OBJECT)
#define OWNER_RIGHTS (PEER_RIGHTS | M5_PIM_PERM_DMA_WRITE_OBJECT)
static uintptr_t arena;
static uint32_t *buffer;
static uint64_t tenant;
static struct pressure_reply reply;
static uint64_t churn[PRESSURE_CHURN], churn_count;
static void need(int ok, unsigned long code) { if (!ok) { EAPP_RETURN(code); } }
static void denied(int ok) { need(ok,0xfc40); ++reply.checks; }
static void initialize(uint64_t id) {
    need(!arena && id<PRESSURE_TENANTS,0xfc01); tenant=id;
    mp_begin(MP_HOST_ALLOC);
    register uintptr_t a0 __asm__("a0")=0, a1 __asm__("a1")=4096;
    register uintptr_t a2 __asm__("a2")=PROT_READ|PROT_WRITE;
    register uintptr_t a3 __asm__("a3")=MAP_PRIVATE|MAP_ANONYMOUS;
    register uintptr_t a4 __asm__("a4")=(uintptr_t)-1, a5 __asm__("a5")=0;
    register uintptr_t a7 __asm__("a7")=__NR_mmap;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a5),"r"(a7) : "memory");
    arena=a0;
    need(arena && arena!=UINTPTR_MAX && !(arena&4095),0xfc02);
    buffer=(uint32_t *)(arena+64); mp_end(MP_HOST_ALLOC);
    mp_begin(MP_INITIALIZE);
    for(unsigned j=0;j<16;++j) buffer[j]=0;
    mp_end(MP_INITIALIZE);
}
void EAPP_ENTRY eapp_entry(void) {
    mp_end(MP_ENTER);
    struct pressure_command c={0};
    reply.principal=loc_pim_principal(); need(reply.principal,0xfc00);
    for (;;) {
        need(!ocall(1,&reply,sizeof(reply),&c,sizeof(c)),0xfc03);
        mp_end(MP_SWITCH); reply.action=c.action;
        if(c.action==MP_INIT) initialize(c.tenant);
        else if(c.action==MP_ALLOC) {
            need(!tenant && !reply.count && c.count && c.count<=PRESSURE_MAX_OBJECTS
                && PRESSURE_CALLS%c.count==0,0xfc04);
            reply.count=c.count; mp_begin(MP_PIM_ALLOC);
            for(unsigned i=0;i<c.count;++i) {
                reply.objects[i]=loc_pim_alloc(PRESSURE_BYTES);
                need(reply.objects[i],0xfc05);
            }
            mp_end(MP_PIM_ALLOC);
        } else if(c.action==MP_OWNER_GRANT || c.action==MP_GRANT || c.action==MP_REVOKE) {
            need(!tenant && reply.count==c.count,0xfc06);
            unsigned phase=c.action==MP_OWNER_GRANT?MP_OWNER_CAP:c.action==MP_GRANT?MP_SHARE:MP_UNSHARE;
            mp_begin(phase);
            for(unsigned i=0;i<reply.count;++i) {
                uint64_t status=c.action==MP_OWNER_GRANT ? loc_pim_grant(reply.objects[i],OWNER_RIGHTS) :
                    c.action==MP_GRANT ? loc_pim_grant_to(reply.objects[i],c.principal,PEER_RIGHTS) :
                    loc_pim_revoke_from(reply.objects[i],c.principal);
                need(!status,0xfc07);
            }
            mp_end(phase);
        } else if(c.action==MP_UPLOAD) {
            need(!tenant,0xfc08); mp_begin(MP_LOAD);
            for(unsigned i=0;i<reply.count;++i) {
                for(unsigned j=0;j<16;++j) buffer[j]=mp_value(i,j);
                need(!loc_pim_upload(reply.objects[i],buffer,PRESSURE_BYTES),0xfc09);
            }
            mp_end(MP_LOAD);
        } else if(c.action==MP_ATTACH) {
            need(tenant && c.count && c.count<=PRESSURE_MAX_OBJECTS,0xfc0a);
            reply.count=c.count;
            for(unsigned i=0;i<c.count;++i) reply.objects[i]=c.objects[i];
        } else if(c.action==MP_ACCESS) {
            need(reply.count==c.count && reply.count,0xfc0b);
            mp_begin(MP_PROBE);
            for(unsigned long i=0;i<PRESSURE_CALLS;++i)
                need(!loc_pim_check_buffer(reply.objects[i%reply.count],buffer,PRESSURE_BYTES,LOC_PIM_TO_HOST),0xfc0c);
            mp_end(MP_PROBE); reply.calls+=PRESSURE_CALLS; reply.round=c.round;
        } else if(c.action==MP_VERIFY) {
            mp_begin(MP_CHECK);
            for(unsigned i=0;i<reply.count;i+=(reply.count>1?reply.count-1:1)) {
                need(!loc_pim_download(buffer,reply.objects[i],PRESSURE_BYTES),0xfc0d);
                for(unsigned j=0;j<16;++j) need(buffer[j]==mp_value(i,j),0xfc0e);
            }
            mp_end(MP_CHECK);
        } else if(c.action==MP_DENIED) {
            need(tenant && reply.count,0xfc0f); mp_begin(MP_NEGATIVE);
            if(c.variant==0 || c.variant==1)
                denied(loc_pim_check_buffer(reply.objects[0],buffer,PRESSURE_BYTES,LOC_PIM_TO_HOST)!=0);
            else {
                denied(loc_pim_free(reply.objects[0])!=0);
                denied(loc_pim_check_buffer(reply.objects[0],buffer,PRESSURE_BYTES,LOC_PIM_TO_DEVICE)!=0);
                denied(loc_pim_check_buffer(reply.objects[0],(void *)PRESSURE_UTM,PRESSURE_BYTES,LOC_PIM_TO_HOST)!=0);
                denied(loc_pim_check_buffer(reply.objects[0],buffer,PRESSURE_BYTES+1,LOC_PIM_TO_HOST)!=0);
            }
            mp_end(MP_NEGATIVE);
        } else if(c.action==MP_CHURN) {
            need(tenant && !churn_count && c.count && c.count<=PRESSURE_CHURN,0xfc20);
            mp_begin(MP_CONDITION);
            for(unsigned i=0;i<c.count;++i) {
                churn[i]=loc_pim_alloc(PRESSURE_BYTES); need(churn[i],0xfc21);
                need(!loc_pim_grant(churn[i],OWNER_RIGHTS),0xfc22);
            }
            churn_count=c.count; mp_end(MP_CONDITION);
        } else if(c.action==MP_CHURN_FREE) {
            need(tenant && churn_count==c.count && churn_count,0xfc23);
            mp_begin(MP_CLEANUP);
            for(unsigned i=0;i<churn_count;++i) need(!loc_pim_free(churn[i]),0xfc24);
            churn_count=0; mp_end(MP_CLEANUP);
        } else if(c.action==MP_AUDIT) {
            need(c.count && c.count<=PRESSURE_MAX_OBJECTS &&
                (reply.count==c.count || (!tenant && !reply.count && !c.variant)),0xfc25);
            unsigned phase=c.variant?MP_CHECK:MP_NEGATIVE;
            mp_begin(phase);
            for(unsigned i=0;i<c.count;++i) {
                uint64_t status=loc_pim_check_buffer(reply.objects[i],buffer,PRESSURE_BYTES,LOC_PIM_TO_HOST);
                if(c.variant) { need(!status,0xfc26); ++reply.calls; }
                else denied(status!=0);
            }
            mp_end(phase);
        } else if(c.action==MP_FREE) {
            unsigned phase=c.variant?MP_CLEANUP:MP_PIM_FREE;
            need(!tenant && reply.count,0xfc10); mp_begin(phase);
            for(unsigned i=0;i<reply.count;++i) need(!loc_pim_free(reply.objects[i]),0xfc11);
            reply.count=0; mp_end(phase);
        } else if(c.action==MP_HOST_FREE) {
            mp_begin(MP_RELEASE_HOST); need(!SYSCALL_2(__NR_munmap,arena,4096),0xfc12);
            arena=0; reply.count=0; mp_end(MP_RELEASE_HOST);
        } else if(c.action==MP_STOP) {
            need(!arena && !reply.count && !churn_count,0xfc13); mp_begin(MP_EXIT); EAPP_RETURN(PRESSURE_PASS);
        } else need(0,0xfc14);
    }
}
