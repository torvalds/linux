/*
 *  include/asm-s390/param.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/param.h"
 */

#ifndef _ASMS390_PARAM_H
#define _ASMS390_PARAM_H

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

#endif
