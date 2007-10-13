#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/cmpxchg.h>

#ifdef __KERNEL__

#define __STR(x) #x
#define STR(x) __STR(x)

#define __SAVE(reg,offset) "movq %%" #reg ",(14-" #offset ")*8(%%rsp)\n\t"
#define __RESTORE(reg,offset) "movq (14-" #offset ")*8(%%rsp),%%" #reg "\n\t"

/* frame pointer must be last for get_wchan */
#define SAVE_CONTEXT    "pushf ; pushq %%rbp ; movq %%rsi,%%rbp\n\t"
#define RESTORE_CONTEXT "movq %%rbp,%%rsi ; popq %%rbp ; popf\t"

#define __EXTRA_CLOBBER  \
	,"rcx","rbx","rdx","r8","r9","r10","r11","r12","r13","r14","r15"

/* Save restore flags to clear handle leaking NT */
#define switch_to(prev,next,last) \
	asm volatile(SAVE_CONTEXT						    \
		     "movq %%rsp,%P[threadrsp](%[prev])\n\t" /* save RSP */	  \
		     "movq %P[threadrsp](%[next]),%%rsp\n\t" /* restore RSP */	  \
		     "call __switch_to\n\t"					  \
		     ".globl thread_return\n"					\
		     "thread_return:\n\t"					    \
		     "movq %%gs:%P[pda_pcurrent],%%rsi\n\t"			  \
		     "movq %P[thread_info](%%rsi),%%r8\n\t"			  \
		     LOCK_PREFIX "btr  %[tif_fork],%P[ti_flags](%%r8)\n\t"	  \
		     "movq %%rax,%%rdi\n\t" 					  \
		     "jc   ret_from_fork\n\t"					  \
		     RESTORE_CONTEXT						    \
		     : "=a" (last)					  	  \
		     : [next] "S" (next), [prev] "D" (prev),			  \
		       [threadrsp] "i" (offsetof(struct task_struct, thread.rsp)), \
		       [ti_flags] "i" (offsetof(struct thread_info, flags)),\
		       [tif_fork] "i" (TIF_FORK),			  \
		       [thread_info] "i" (offsetof(struct task_struct, stack)), \
		       [pda_pcurrent] "i" (offsetof(struct x8664_pda, pcurrent))   \
		     : "memory", "cc" __EXTRA_CLOBBER)
    
extern void load_gs_index(unsigned); 

/*
 * Load a segment. Fall back on loading the zero
 * segment if something goes wrong..
 */
#define loadsegment(seg,value)	\
	asm volatile("\n"			\
		"1:\t"				\
		"movl %k0,%%" #seg "\n"		\
		"2:\n"				\
		".section .fixup,\"ax\"\n"	\
		"3:\t"				\
		"movl %1,%%" #seg "\n\t" 	\
		"jmp 2b\n"			\
		".previous\n"			\
		".section __ex_table,\"a\"\n\t"	\
		".align 8\n\t"			\
		".quad 1b,3b\n"			\
		".previous"			\
		: :"r" (value), "r" (0))

/*
 * Clear and set 'TS' bit respectively
 */
#define clts() __asm__ __volatile__ ("clts")

static inline unsigned long read_cr0(void)
{ 
	unsigned long cr0;
	asm volatile("movq %%cr0,%0" : "=r" (cr0));
	return cr0;
}

static inline void write_cr0(unsigned long val) 
{ 
	asm volatile("movq %0,%%cr0" :: "r" (val));
}

static inline unsigned long read_cr2(void)
{
	unsigned long cr2;
	asm("movq %%cr2,%0" : "=r" (cr2));
	return cr2;
}

static inline void write_cr2(unsigned long val)
{
	asm volatile("movq %0,%%cr2" :: "r" (val));
}

static inline unsigned long read_cr3(void)
{ 
	unsigned long cr3;
	asm("movq %%cr3,%0" : "=r" (cr3));
	return cr3;
}

static inline void write_cr3(unsigned long val)
{
	asm volatile("movq %0,%%cr3" :: "r" (val) : "memory");
}

static inline unsigned long read_cr4(void)
{ 
	unsigned long cr4;
	asm("movq %%cr4,%0" : "=r" (cr4));
	return cr4;
}

static inline void write_cr4(unsigned long val)
{ 
	asm volatile("movq %0,%%cr4" :: "r" (val) : "memory");
}

static inline unsigned long read_cr8(void)
{
	unsigned long cr8;
	asm("movq %%cr8,%0" : "=r" (cr8));
	return cr8;
}

static inline void write_cr8(unsigned long val)
{
	asm volatile("movq %0,%%cr8" :: "r" (val) : "memory");
}

#define stts() write_cr0(8 | read_cr0())

#define wbinvd() \
	__asm__ __volatile__ ("wbinvd": : :"memory")

#endif	/* __KERNEL__ */

#define nop() __asm__ __volatile__ ("nop")

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do {} while(0)
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do {} while(0)
#endif

    
/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 */
#define mb() 	asm volatile("mfence":::"memory")
#define rmb()	asm volatile("lfence":::"memory")
#define wmb()	asm volatile("sfence" ::: "memory")

#define read_barrier_depends()	do {} while(0)
#define set_mb(var, value) do { (void) xchg(&var, value); } while (0)

#define warn_if_not_ulong(x) do { unsigned long foo; (void) (&(x) == &foo); } while (0)

#include <linux/irqflags.h>

void cpu_idle_wait(void);

extern unsigned long arch_align_stack(unsigned long sp);
extern void free_init_pages(char *what, unsigned long begin, unsigned long end);

#endif
