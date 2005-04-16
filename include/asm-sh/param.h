#ifndef __ASM_SH_PARAM_H
#define __ASM_SH_PARAM_H

#ifdef __KERNEL__
# ifdef CONFIG_SH_WDT
#  define HZ		1000		/* Needed for high-res WOVF */
# else
#  define HZ		100
# endif
# define USER_HZ	100		/* User interfaces are in "ticks" */
# define CLOCKS_PER_SEC	(USER_HZ)	/* frequency at which times() counts */
#endif

#ifndef HZ
#define HZ 100
#endif

#define EXEC_PAGESIZE	4096

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif /* __ASM_SH_PARAM_H */
