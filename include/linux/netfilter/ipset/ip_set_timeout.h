#ifndef _IP_SET_TIMEOUT_H
#define _IP_SET_TIMEOUT_H

/* Copyright (C) 2003-2011 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef __KERNEL__

/* How often should the gc be run by default */
#define IPSET_GC_TIME			(3 * 60)

/* Timeout period depending on the timeout value of the given set */
#define IPSET_GC_PERIOD(timeout) \
	((timeout/3) ? min_t(u32, (timeout)/3, IPSET_GC_TIME) : 1)

/* Set is defined without timeout support: timeout value may be 0 */
#define IPSET_NO_TIMEOUT	UINT_MAX

#define with_timeout(timeout)	((timeout) != IPSET_NO_TIMEOUT)

#define opt_timeout(opt, map)	\
	(with_timeout((opt)->timeout) ? (opt)->timeout : (map)->timeout)

static inline unsigned int
ip_set_timeout_uget(struct nlattr *tb)
{
	unsigned int timeout = ip_set_get_h32(tb);

	/* Normalize to fit into jiffies */
	if (timeout > UINT_MAX/MSEC_PER_SEC)
		timeout = UINT_MAX/MSEC_PER_SEC;

	/* Userspace supplied TIMEOUT parameter: adjust crazy size */
	return timeout == IPSET_NO_TIMEOUT ? IPSET_NO_TIMEOUT - 1 : timeout;
}

#ifdef IP_SET_BITMAP_TIMEOUT

/* Bitmap specific timeout constants and macros for the entries */

/* Bitmap entry is unset */
#define IPSET_ELEM_UNSET	0
/* Bitmap entry is set with no timeout value */
#define IPSET_ELEM_PERMANENT	(UINT_MAX/2)

static inline bool
ip_set_timeout_test(unsigned long timeout)
{
	return timeout != IPSET_ELEM_UNSET &&
	       (timeout == IPSET_ELEM_PERMANENT ||
		time_is_after_jiffies(timeout));
}

static inline bool
ip_set_timeout_expired(unsigned long timeout)
{
	return timeout != IPSET_ELEM_UNSET &&
	       timeout != IPSET_ELEM_PERMANENT &&
	       time_is_before_jiffies(timeout);
}

static inline unsigned long
ip_set_timeout_set(u32 timeout)
{
	unsigned long t;

	if (!timeout)
		return IPSET_ELEM_PERMANENT;

	t = msecs_to_jiffies(timeout * 1000) + jiffies;
	if (t == IPSET_ELEM_UNSET || t == IPSET_ELEM_PERMANENT)
		/* Bingo! */
		t++;

	return t;
}

static inline u32
ip_set_timeout_get(unsigned long timeout)
{
	return timeout == IPSET_ELEM_PERMANENT ? 0 :
		jiffies_to_msecs(timeout - jiffies)/1000;
}

#else

/* Hash specific timeout constants and macros for the entries */

/* Hash entry is set with no timeout value */
#define IPSET_ELEM_PERMANENT	0

static inline bool
ip_set_timeout_test(unsigned long timeout)
{
	return timeout == IPSET_ELEM_PERMANENT ||
	       time_is_after_jiffies(timeout);
}

static inline bool
ip_set_timeout_expired(unsigned long timeout)
{
	return timeout != IPSET_ELEM_PERMANENT &&
	       time_is_before_jiffies(timeout);
}

static inline unsigned long
ip_set_timeout_set(u32 timeout)
{
	unsigned long t;

	if (!timeout)
		return IPSET_ELEM_PERMANENT;

	t = msecs_to_jiffies(timeout * 1000) + jiffies;
	if (t == IPSET_ELEM_PERMANENT)
		/* Bingo! :-) */
		t++;

	return t;
}

static inline u32
ip_set_timeout_get(unsigned long timeout)
{
	return timeout == IPSET_ELEM_PERMANENT ? 0 :
		jiffies_to_msecs(timeout - jiffies)/1000;
}
#endif /* ! IP_SET_BITMAP_TIMEOUT */

#endif	/* __KERNEL__ */

#endif /* _IP_SET_TIMEOUT_H */
