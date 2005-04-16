/*
 * linux/include/asm-cris/timex.h
 *
 * CRIS architecture timex specifications
 */

#ifndef _ASM_CRIS_TIMEX_H
#define _ASM_CRIS_TIMEX_H

#include <asm/arch/timex.h>

/*
 * We don't have a cycle-counter.. but we do not support SMP anyway where this is
 * used so it does not matter.
 */

typedef unsigned int cycles_t;

extern inline cycles_t get_cycles(void)
{
        return 0;
}

#endif
