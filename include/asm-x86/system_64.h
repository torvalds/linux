#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <asm/segment.h>
#include <asm/cmpxchg.h>

#ifdef __KERNEL__

/* entries in ARCH_DLINFO: */
#ifdef CONFIG_IA32_EMULATION
# define AT_VECTOR_SIZE_ARCH 2
#else
# define AT_VECTOR_SIZE_ARCH 1
#endif

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
		       [threadrsp] "i" (offsetof(struct task_struct, thread.sp)), \
		       [ti_flags] "i" (offsetof(struct thread_info, flags)),\
		       [tif_fork] "i" (TIF_FORK),			  \
		       [thread_info] "i" (offsetof(struct task_struct, stack)), \
		       [pda_pcurrent] "i" (offsetof(struct x8664_pda, pcurrent))   \
		     : "memory", "cc" __EXTRA_CLOBBER)
    
#endif	/* __KERNEL__ */

static inline unsigned long read_cr8(void)
{
	unsigned long cr8;
	asm volatile("movq %%cr8,%0" : "=r" (cr8));
	return cr8;
}

static inline void write_cr8(unsigned long val)
{
	asm volatile("movq %0,%%cr8" :: "r" (val) : "memory");
}

#include <linux/irqflags.h>

#endif
