#ifndef __LINUX_CPUTIME_H
#define __LINUX_CPUTIME_H

#include <asm/cputime.h>

#ifndef nsecs_to_cputime
# define nsecs_to_cputime(__nsecs)	\
	usecs_to_cputime((__nsecs) / NSEC_PER_USEC)
#endif

#endif /* __LINUX_CPUTIME_H */
