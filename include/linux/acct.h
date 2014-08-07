/*
 *  BSD Process Accounting for Linux - Definitions
 *
 *  Author: Marco van Wieringen (mvw@planets.elm.net)
 *
 *  This header file contains the definitions needed to implement
 *  BSD-style process accounting. The kernel accounting code and all
 *  user-level programs that try to do something useful with the
 *  process accounting log must include this file.
 *
 *  Copyright (C) 1995 - 1997 Marco van Wieringen - ELM Consultancy B.V.
 *
 */
#ifndef _LINUX_ACCT_H
#define _LINUX_ACCT_H

#include <uapi/linux/acct.h>



#ifdef CONFIG_BSD_PROCESS_ACCT
struct vfsmount;
struct super_block;
struct pacct_struct;
struct pid_namespace;
extern int acct_parm[]; /* for sysctl */
extern void acct_collect(long exitcode, int group_dead);
extern void acct_process(void);
extern void acct_exit_ns(struct pid_namespace *);
#else
#define acct_collect(x,y)	do { } while (0)
#define acct_process()		do { } while (0)
#define acct_exit_ns(ns)	do { } while (0)
#endif
extern void acct_auto_close(struct hlist_head *);
extern void acct_auto_close_mnt(struct hlist_head *);

/*
 * ACCT_VERSION numbers as yet defined:
 * 0: old format (until 2.6.7) with 16 bit uid/gid
 * 1: extended variant (binary compatible on M68K)
 * 2: extended variant (binary compatible on everything except M68K)
 * 3: new binary incompatible format (64 bytes)
 * 4: new binary incompatible format (128 bytes)
 * 5: new binary incompatible format (128 bytes, second half)
 *
 */

#undef ACCT_VERSION
#undef AHZ

#ifdef CONFIG_BSD_PROCESS_ACCT_V3
#define ACCT_VERSION	3
#define AHZ		100
typedef struct acct_v3 acct_t;
#else
#ifdef CONFIG_M68K
#define ACCT_VERSION	1
#else
#define ACCT_VERSION	2
#endif
#define AHZ		(USER_HZ)
typedef struct acct acct_t;
#endif

#include <linux/jiffies.h>
/*
 * Yet another set of HZ to *HZ helper functions.
 * See <linux/jiffies.h> for the original.
 */

static inline u32 jiffies_to_AHZ(unsigned long x)
{
#if (TICK_NSEC % (NSEC_PER_SEC / AHZ)) == 0
# if HZ < AHZ
	return x * (AHZ / HZ);
# else
	return x / (HZ / AHZ);
# endif
#else
        u64 tmp = (u64)x * TICK_NSEC;
        do_div(tmp, (NSEC_PER_SEC / AHZ));
        return (long)tmp;
#endif
}

static inline u64 nsec_to_AHZ(u64 x)
{
#if (NSEC_PER_SEC % AHZ) == 0
	do_div(x, (NSEC_PER_SEC / AHZ));
#elif (AHZ % 512) == 0
	x *= AHZ/512;
	do_div(x, (NSEC_PER_SEC / 512));
#else
	/*
         * max relative error 5.7e-8 (1.8s per year) for AHZ <= 1024,
         * overflow after 64.99 years.
         * exact for AHZ=60, 72, 90, 120, 144, 180, 300, 600, 900, ...
         */
	x *= 9;
	do_div(x, (unsigned long)((9ull * NSEC_PER_SEC + (AHZ/2))
	                          / AHZ));
#endif
	return x;
}

#endif	/* _LINUX_ACCT_H */
