#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <asm/segment.h>
#include <asm/cpufeature.h>
#include <asm/cmpxchg.h>

#ifdef __KERNEL__
#define AT_VECTOR_SIZE_ARCH 2 /* entries in ARCH_DLINFO */

struct task_struct;	/* one of the stranger aspects of C forward declarations.. */
extern struct task_struct * FASTCALL(__switch_to(struct task_struct *prev, struct task_struct *next));

/*
 * Saving eflags is important. It switches not only IOPL between tasks,
 * it also protects other tasks from NT leaking through sysenter etc.
 */
#define switch_to(prev,next,last) do {					\
	unsigned long esi,edi;						\
	asm volatile("pushfl\n\t"		/* Save flags */	\
		     "pushl %%ebp\n\t"					\
		     "movl %%esp,%0\n\t"	/* save ESP */		\
		     "movl %5,%%esp\n\t"	/* restore ESP */	\
		     "movl $1f,%1\n\t"		/* save EIP */		\
		     "pushl %6\n\t"		/* restore EIP */	\
		     "jmp __switch_to\n"				\
		     "1:\t"						\
		     "popl %%ebp\n\t"					\
		     "popfl"						\
		     :"=m" (prev->thread.sp),"=m" (prev->thread.ip),	\
		      "=a" (last),"=S" (esi),"=D" (edi)			\
		     :"m" (next->thread.sp),"m" (next->thread.ip),	\
		      "2" (prev), "d" (next));				\
} while (0)

#endif	/* __KERNEL__ */


#include <linux/irqflags.h>

/*
 * disable hlt during certain critical i/o operations
 */
#define HAVE_DISABLE_HLT

#endif
