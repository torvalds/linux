#ifndef __TOOLS_LINUX_ASM_ALPHA_BARRIER_H
#define __TOOLS_LINUX_ASM_ALPHA_BARRIER_H

#define mb()	__asm__ __volatile__("mb": : :"memory")
#define rmb()	__asm__ __volatile__("mb": : :"memory")
#define wmb()	__asm__ __volatile__("wmb": : :"memory")

#endif		/* __TOOLS_LINUX_ASM_ALPHA_BARRIER_H */
