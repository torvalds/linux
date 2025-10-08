/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _NET_ALIGNED_DATA_H
#define _NET_ALIGNED_DATA_H

#include <linux/atomic.h>
#include <linux/types.h>

/* Structure holding cacheline aligned fields on SMP builds.
 * Each field or group should have an ____cacheline_aligned_in_smp
 * attribute to ensure no accidental false sharing can happen.
 */
struct net_aligned_data {
	atomic64_t	net_cookie ____cacheline_aligned_in_smp;
#if defined(CONFIG_INET)
	atomic_long_t tcp_memory_allocated ____cacheline_aligned_in_smp;
	atomic_long_t udp_memory_allocated ____cacheline_aligned_in_smp;
#endif
};

extern struct net_aligned_data net_aligned_data;

#endif /* _NET_ALIGNED_DATA_H */
