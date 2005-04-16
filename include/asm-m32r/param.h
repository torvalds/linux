#ifndef _ASM_M32R_PARAM_H
#define _ASM_M32R_PARAM_H

/* $Id$ */

/* orig : i386 2.5.67 */

#ifdef __KERNEL__
# define HZ		100		/* Internal kernel timer frequency */
# define USER_HZ	100		/* .. some user interfaces are in "ticks" */
# define CLOCKS_PER_SEC	(USER_HZ)	/* like times() */
#endif

#ifndef HZ
#define HZ 100
#endif

#define EXEC_PAGESIZE	4096

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif /* _ASM_M32R_PARAM_H */

