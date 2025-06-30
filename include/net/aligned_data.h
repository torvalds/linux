/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _NET_ALIGNED_DATA_H
#define _NET_ALIGNED_DATA_H

#include <linux/types.h>

/* Structure holding cacheline aligned fields on SMP builds.
 * Each field or group should have an ____cacheline_aligned_in_smp
 * attribute to ensure no accidental false sharing can happen.
 */
struct net_aligned_data {
};

extern struct net_aligned_data net_aligned_data;

#endif /* _NET_ALIGNED_DATA_H */
