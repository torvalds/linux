#ifndef __ASM_FPU_H
#define __ASM_FPU_H

#include <linux/config.h>

/*
 * MAX floating point unit state size (FSAVE/FRESTORE)
 */

#define kernel_fpu_end() do { asm volatile("bar":::"memory"); preempt_enable(); } while(0)

#endif /* __ASM_FPU_H */
