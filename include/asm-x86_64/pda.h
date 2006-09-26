#ifndef X86_64_PDA_H
#define X86_64_PDA_H

#ifndef __ASSEMBLY__
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/cache.h>
#include <asm/page.h>

/* Per processor datastructure. %gs points to it while the kernel runs */ 
struct x8664_pda {
	struct task_struct *pcurrent;	/* Current process */
	unsigned long data_offset;	/* Per cpu data offset from linker address */
	unsigned long kernelstack;  /* top of kernel stack for current */ 
	unsigned long oldrsp; 	    /* user rsp for system call */
#if DEBUG_STKSZ > EXCEPTION_STKSZ
	unsigned long debugstack;   /* #DB/#BP stack. */
#endif
        int irqcount;		    /* Irq nesting counter. Starts with -1 */  	
	int cpunumber;		    /* Logical CPU number */
	char *irqstackptr;	/* top of irqstack */
	int nodenumber;		    /* number of current node */
	unsigned int __softirq_pending;
	unsigned int __nmi_count;	/* number of NMI on this CPUs */
	int mmu_state;     
	struct mm_struct *active_mm;
	unsigned apic_timer_irqs;
} ____cacheline_aligned_in_smp;

extern struct x8664_pda *_cpu_pda[];
extern struct x8664_pda boot_cpu_pda[];

#define cpu_pda(i) (_cpu_pda[i])

/* 
 * There is no fast way to get the base address of the PDA, all the accesses
 * have to mention %fs/%gs.  So it needs to be done this Torvaldian way.
 */ 
extern void __bad_pda_field(void);

/* proxy_pda doesn't actually exist, but tell gcc it is accessed
   for all PDA accesses so it gets read/write dependencies right. */
extern struct x8664_pda _proxy_pda;

#define pda_offset(field) offsetof(struct x8664_pda, field)

#define pda_to_op(op,field,val) do { \
	typedef typeof(_proxy_pda.field) T__; \
       switch (sizeof(_proxy_pda.field)) { 		\
case 2: \
asm(op "w %1,%%gs:%P2" : "+m" (_proxy_pda.field) : \
	"ri" ((T__)val),"i"(pda_offset(field))); break; \
case 4: \
asm(op "l %1,%%gs:%P2" : "+m" (_proxy_pda.field) : \
	"ri" ((T__)val),"i"(pda_offset(field))); break; \
case 8: \
asm(op "q %1,%%gs:%P2": "+m" (_proxy_pda.field) : \
	 "ri" ((T__)val),"i"(pda_offset(field))); break; \
default: __bad_pda_field(); 					\
       } \
       } while (0)

#define pda_from_op(op,field) ({ \
       typeof(_proxy_pda.field) ret__; \
       switch (sizeof(_proxy_pda.field)) { 		\
case 2: \
asm(op "w %%gs:%P1,%0":"=r" (ret__):\
	"i" (pda_offset(field)), "m" (_proxy_pda.field)); break;\
case 4: \
asm(op "l %%gs:%P1,%0":"=r" (ret__):\
	"i" (pda_offset(field)), "m" (_proxy_pda.field)); break;\
case 8: \
asm(op "q %%gs:%P1,%0":"=r" (ret__):\
	"i" (pda_offset(field)), "m" (_proxy_pda.field)); break;\
default: __bad_pda_field(); 					\
       } \
       ret__; })


#define read_pda(field) pda_from_op("mov",field)
#define write_pda(field,val) pda_to_op("mov",field,val)
#define add_pda(field,val) pda_to_op("add",field,val)
#define sub_pda(field,val) pda_to_op("sub",field,val)
#define or_pda(field,val) pda_to_op("or",field,val)

#endif

#define PDA_STACKOFFSET (5*8)

#endif
