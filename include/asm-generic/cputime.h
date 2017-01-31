#ifndef _ASM_GENERIC_CPUTIME_H
#define _ASM_GENERIC_CPUTIME_H

#include <linux/time.h>
#include <linux/jiffies.h>

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
# include <asm-generic/cputime_nsecs.h>
#endif

#endif
