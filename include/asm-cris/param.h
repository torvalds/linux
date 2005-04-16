#ifndef _ASMCRIS_PARAM_H
#define _ASMCRIS_PARAM_H

/* Currently we assume that HZ=100 is good for CRIS. */
#ifdef __KERNEL__
# define HZ		100		/* Internal kernel timer frequency */
# define USER_HZ	100		/* .. some user interfaces are in "ticks" */
# define CLOCKS_PER_SEC	(USER_HZ)	/* like times() */
#endif

#ifndef HZ
#define HZ 100
#endif

#define EXEC_PAGESIZE	8192

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif
