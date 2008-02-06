/* x86 architecture timex specifications */
#ifndef _ASM_X86_TIMEX_H
#define _ASM_X86_TIMEX_H

#include <asm/processor.h>
#include <asm/tsc.h>

#ifdef CONFIG_X86_ELAN
#  define PIT_TICK_RATE 1189200 /* AMD Elan has different frequency! */
#elif defined(CONFIG_X86_RDC321X)
#  define PIT_TICK_RATE 1041667 /* Underlying HZ for R8610 */
#else
#  define PIT_TICK_RATE 1193182 /* Underlying HZ */
#endif
#define CLOCK_TICK_RATE	PIT_TICK_RATE

#define ARCH_HAS_READ_CURRENT_TIMER

#endif
