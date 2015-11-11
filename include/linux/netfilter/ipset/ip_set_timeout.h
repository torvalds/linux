#ifndef _IP_SET_TIMEOUT_H
#define _IP_SET_TIMEOUT_H

/* Copyright (C) 2003-2013 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
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

/* Entry is set with no timeout value */
#define IPSET_ELEM_PERMANENT	0

/* Set is defined with timeout support: timeout value may be 0 */
#define IPSET_NO_TIMEOUT	UINT_MAX

#define ip_set_adt_opt_timeout(opt, set)	\
((opt)->ext.timeout != IPSET_NO_TIMEOUT ? (opt)->ext.timeout : (set)->timeout)

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

static inline bool
ip_set_timeout_expired(unsigned long *t)
{
	return *t != IPSET_ELEM_PERMANENT && time_is_before_jiffies(*t);
}

static inline void
ip_set_timeout_set(unsigned long *timeout, u32 value)
{
	unsigned long t;

	if (!value) {
		*timeout = IPSET_ELEM_PERMANENT;
		return;
	}

	t = msecs_to_jiffies(value * MSEC_PER_SEC) + jiffies;
	if (t == IPSET_ELEM_PERMANENT)
		/* Bingo! :-) */
		t--;
	*timeout = t;
}

static inline u32
ip_set_timeout_get(unsigned long *timeout)
{
	return *timeout == IPSET_ELEM_PERMANENT ? 0 :
		jiffies_to_msecs(*timeout - jiffies)/MSEC_PER_SEC;
}

#endif	/* __KERNEL__ */
#endif /* _IP_SET_TIMEOUT_H */
