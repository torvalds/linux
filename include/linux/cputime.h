#ifndef __LINUX_CPUTIME_H
#define __LINUX_CPUTIME_H

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
#include <asm/cputime.h>

#ifndef cputime_to_nsecs
# define cputime_to_nsecs(__ct)	\
	(cputime_to_usecs(__ct) * NSEC_PER_USEC)
#endif

#endif /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */
#endif /* __LINUX_CPUTIME_H */
