#ifndef X86_64_PDA_H
#define X86_64_PDA_H

#ifndef __ASSEMBLY__
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/cache.h>

/* Per processor datastructure. %gs points to it while the kernel runs */ 
struct x8664_pda {
	struct task_struct *pcurrent;	/* Current process */
	unsigned long data_offset;	/* Per cpu data offset from linker address */
	unsigned long kernelstack;  /* top of kernel stack for current */ 
	unsigned long oldrsp; 	    /* user rsp for system call */
        int irqcount;		    /* Irq nesting counter. Starts with -1 */  	
	int cpunumber;		    /* Logical CPU number */
	char *irqstackptr;	/* top of irqstack */
	unsigned int __softirq_pending;
	unsigned int __nmi_count;	/* number of NMI on this CPUs */
	struct mm_struct *active_mm;
	int mmu_state;     
	unsigned apic_timer_irqs;
} ____cacheline_aligned_in_smp;


#define IRQSTACK_ORDER 2
#define IRQSTACKSIZE (PAGE_SIZE << IRQSTACK_ORDER) 

extern struct x8664_pda cpu_pda[];

/* 
 * There is no fast way to get the base address of the PDA, all the accesses
 * have to mention %fs/%gs.  So it needs to be done this Torvaldian way.
 */ 
#define sizeof_field(type,field)  (sizeof(((type *)0)->field))
#define typeof_field(type,field)  typeof(((type *)0)->field)

extern void __bad_pda_field(void);

#define pda_offset(field) offsetof(struct x8664_pda, field)

#define pda_to_op(op,field,val) do { \
	typedef typeof_field(struct x8664_pda, field) T__; \
       switch (sizeof_field(struct x8664_pda, field)) { 		\
case 2: \
asm volatile(op "w %0,%%gs:%P1"::"ri" ((T__)val),"i"(pda_offset(field)):"memory"); break; \
case 4: \
asm volatile(op "l %0,%%gs:%P1"::"ri" ((T__)val),"i"(pda_offset(field)):"memory"); break; \
case 8: \
asm volatile(op "q %0,%%gs:%P1"::"ri" ((T__)val),"i"(pda_offset(field)):"memory"); break; \
       default: __bad_pda_field(); 					\
       } \
       } while (0)

/* 
 * AK: PDA read accesses should be neither volatile nor have an memory clobber.
 * Unfortunately removing them causes all hell to break lose currently.
 */
#define pda_from_op(op,field) ({ \
       typeof_field(struct x8664_pda, field) ret__; \
       switch (sizeof_field(struct x8664_pda, field)) { 		\
case 2: \
asm volatile(op "w %%gs:%P1,%0":"=r" (ret__):"i"(pda_offset(field)):"memory"); break;\
case 4: \
asm volatile(op "l %%gs:%P1,%0":"=r" (ret__):"i"(pda_offset(field)):"memory"); break;\
case 8: \
asm volatile(op "q %%gs:%P1,%0":"=r" (ret__):"i"(pda_offset(field)):"memory"); break;\
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
