/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file just defines deging masks to be used with the gossip
 * logging utility.  All deging masks for ORANGEFS are kept here to make
 * sure we don't have collisions.
 */

#ifndef __ORANGEFS_DE_H
#define __ORANGEFS_DE_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/kernel.h>
#else
#include <stdint.h>
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#define	GOSSIP_NO_DE			(__u64)0

#define GOSSIP_SUPER_DE		((__u64)1 << 0)
#define GOSSIP_INODE_DE		((__u64)1 << 1)
#define GOSSIP_FILE_DE		((__u64)1 << 2)
#define GOSSIP_DIR_DE		((__u64)1 << 3)
#define GOSSIP_UTILS_DE		((__u64)1 << 4)
#define GOSSIP_WAIT_DE		((__u64)1 << 5)
#define GOSSIP_ACL_DE		((__u64)1 << 6)
#define GOSSIP_DCACHE_DE		((__u64)1 << 7)
#define GOSSIP_DEV_DE		((__u64)1 << 8)
#define GOSSIP_NAME_DE		((__u64)1 << 9)
#define GOSSIP_BUFMAP_DE		((__u64)1 << 10)
#define GOSSIP_CACHE_DE		((__u64)1 << 11)
#define GOSSIP_DEFS_DE		((__u64)1 << 12)
#define GOSSIP_XATTR_DE		((__u64)1 << 13)
#define GOSSIP_INIT_DE		((__u64)1 << 14)
#define GOSSIP_SYSFS_DE		((__u64)1 << 15)

#define GOSSIP_MAX_NR                 16
#define GOSSIP_MAX_DE              (((__u64)1 << GOSSIP_MAX_NR) - 1)

/* a private internal type */
struct __keyword_mask_s {
	const char *keyword;
	__u64 mask_val;
};

/*
 * Map all kmod keywords to kmod de masks here. Keep this
 * structure "packed":
 *
 *   "all" is always last...
 *
 *   keyword     mask_val     index
 *     foo          1           0
 *     bar          2           1
 *     baz          4           2
 *     qux          8           3
 *      .           .           .
 */
static struct __keyword_mask_s s_kmod_keyword_mask_map[] = {
	{"super", GOSSIP_SUPER_DE},
	{"inode", GOSSIP_INODE_DE},
	{"file", GOSSIP_FILE_DE},
	{"dir", GOSSIP_DIR_DE},
	{"utils", GOSSIP_UTILS_DE},
	{"wait", GOSSIP_WAIT_DE},
	{"acl", GOSSIP_ACL_DE},
	{"dcache", GOSSIP_DCACHE_DE},
	{"dev", GOSSIP_DEV_DE},
	{"name", GOSSIP_NAME_DE},
	{"bufmap", GOSSIP_BUFMAP_DE},
	{"cache", GOSSIP_CACHE_DE},
	{"defs", GOSSIP_DEFS_DE},
	{"xattr", GOSSIP_XATTR_DE},
	{"init", GOSSIP_INIT_DE},
	{"sysfs", GOSSIP_SYSFS_DE},
	{"none", GOSSIP_NO_DE},
	{"all", GOSSIP_MAX_DE}
};

static const int num_kmod_keyword_mask_map = (int)
	(ARRAY_SIZE(s_kmod_keyword_mask_map));

#endif /* __ORANGEFS_DE_H */
