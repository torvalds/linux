#ifndef ASM_X86__SYSTEM_64_H
#define ASM_X86__SYSTEM_64_H

#include <asm/segment.h>
#include <asm/cmpxchg.h>


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

#endif /* ASM_X86__SYSTEM_64_H */
