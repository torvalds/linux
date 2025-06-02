/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file just defines debugging masks to be used with the gossip
 * logging utility.  All debugging masks for ORANGEFS are kept here to make
 * sure we don't have collisions.
 */

#ifndef __ORANGEFS_DEBUG_H
#define __ORANGEFS_DEBUG_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/kernel.h>
#else
#include <stdint.h>
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#define	GOSSIP_NO_DEBUG			(__u64)0

#define GOSSIP_SUPER_DEBUG		((__u64)1 << 0)
#define GOSSIP_INODE_DEBUG		((__u64)1 << 1)
#define GOSSIP_FILE_DEBUG		((__u64)1 << 2)
#define GOSSIP_DIR_DEBUG		((__u64)1 << 3)
#define GOSSIP_UTILS_DEBUG		((__u64)1 << 4)
#define GOSSIP_WAIT_DEBUG		((__u64)1 << 5)
#define GOSSIP_ACL_DEBUG		((__u64)1 << 6)
#define GOSSIP_DCACHE_DEBUG		((__u64)1 << 7)
#define GOSSIP_DEV_DEBUG		((__u64)1 << 8)
#define GOSSIP_NAME_DEBUG		((__u64)1 << 9)
#define GOSSIP_BUFMAP_DEBUG		((__u64)1 << 10)
#define GOSSIP_CACHE_DEBUG		((__u64)1 << 11)
#define GOSSIP_DEBUGFS_DEBUG		((__u64)1 << 12)
#define GOSSIP_XATTR_DEBUG		((__u64)1 << 13)
#define GOSSIP_INIT_DEBUG		((__u64)1 << 14)
#define GOSSIP_SYSFS_DEBUG		((__u64)1 << 15)

#define GOSSIP_MAX_NR                 16
#define GOSSIP_MAX_DEBUG              (((__u64)1 << GOSSIP_MAX_NR) - 1)

#endif /* __ORANGEFS_DEBUG_H */
