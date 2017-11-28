/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __LINUX_BRIDGE_EBT_AMONG_H
#define __LINUX_BRIDGE_EBT_AMONG_H

#include <linux/types.h>

#define EBT_AMONG_DST 0x01
#define EBT_AMONG_SRC 0x02

/* Grzegorz Borowiak <grzes@gnu.univ.gda.pl> 2003
 * 
 * Write-once-read-many hash table, used for checking if a given
 * MAC address belongs to a set or not and possibly for checking
 * if it is related with a given IPv4 address.
 *
 * The hash value of an address is its last byte.
 * 
 * In real-world ethernet addresses, values of the last byte are
 * evenly distributed and there is no need to consider other bytes.
 * It would only slow the routines down.
 *
 * For MAC address comparison speedup reasons, we introduce a trick.
 * MAC address is mapped onto an array of two 32-bit integers.
 * This pair of integers is compared with MAC addresses in the
 * hash table, which are stored also in form of pairs of integers
 * (in `cmp' array). This is quick as it requires only two elementary
 * number comparisons in worst case. Further, we take advantage of
 * fact that entropy of 3 last bytes of address is larger than entropy
 * of 3 first bytes. So first we compare 4 last bytes of addresses and
 * if they are the same we compare 2 first.
 *
 * Yes, it is a memory overhead, but in 2003 AD, who cares?
 */

struct ebt_mac_wormhash_tuple {
	__u32 cmp[2];
	__be32 ip;
};

struct ebt_mac_wormhash {
	int table[257];
	int poolsize;
	struct ebt_mac_wormhash_tuple pool[0];
};

#define ebt_mac_wormhash_size(x) ((x) ? sizeof(struct ebt_mac_wormhash) \
		+ (x)->poolsize * sizeof(struct ebt_mac_wormhash_tuple) : 0)

struct ebt_among_info {
	int wh_dst_ofs;
	int wh_src_ofs;
	int bitmask;
};

#define EBT_AMONG_DST_NEG 0x1
#define EBT_AMONG_SRC_NEG 0x2

#define ebt_among_wh_dst(x) ((x)->wh_dst_ofs ? \
	(struct ebt_mac_wormhash*)((char*)(x) + (x)->wh_dst_ofs) : NULL)
#define ebt_among_wh_src(x) ((x)->wh_src_ofs ? \
	(struct ebt_mac_wormhash*)((char*)(x) + (x)->wh_src_ofs) : NULL)

#define EBT_AMONG_MATCH "among"

#endif
