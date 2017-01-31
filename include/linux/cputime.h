#ifndef __LINUX_CPUTIME_H
#define __LINUX_CPUTIME_H

#include <asm/cputime.h>

#ifndef cputime_to_nsecs
# define cputime_to_nsecs(__ct)	\
	(cputime_to_usecs(__ct) * NSEC_PER_USEC)
#endif

#endif /* __LINUX_CPUTIME_H */
