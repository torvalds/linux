#ifndef _X86_64_CURRENT_H
#define _X86_64_CURRENT_H

#if !defined(__ASSEMBLY__)
struct task_struct;

#include <asm/pda.h>

static inline struct task_struct *get_current(void)
{
	struct task_struct *t = read_pda(pcurrent);
	return t;
}

#define current get_current()

#else

#ifndef ASM_OFFSET_H
#include <asm/asm-offsets.h>
#endif

#define GET_CURRENT(reg) movq %gs:(pda_pcurrent),reg

#endif

#endif /* !(_X86_64_CURRENT_H) */
