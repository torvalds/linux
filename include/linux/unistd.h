#ifndef _LINUX_UNISTD_H_
#define _LINUX_UNISTD_H_

#ifdef __KERNEL__
extern int errno;
#endif

/*
 * Include machine specific syscallX macros
 */
#include <asm/unistd.h>

#endif /* _LINUX_UNISTD_H_ */
