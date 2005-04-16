#ifndef _UM_PARAM_H
#define _UM_PARAM_H

#define EXEC_PAGESIZE   4096

#ifndef NOGROUP
#define NOGROUP         (-1)
#endif

#define MAXHOSTNAMELEN  64      /* max length of hostname */

#ifdef __KERNEL__
#define HZ 100
#define USER_HZ	100	   /* .. some user interfaces are in "ticks" */
#define CLOCKS_PER_SEC (USER_HZ)  /* frequency at which times() counts */
#endif

#endif
