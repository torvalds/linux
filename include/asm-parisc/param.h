#ifndef _ASMPARISC_PARAM_H
#define _ASMPARISC_PARAM_H

#ifdef __KERNEL__
# ifdef CONFIG_PA20
#  define HZ		1000		/* Faster machines */
# else
#  define HZ		100		/* Internal kernel timer frequency */
# endif
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

#endif
