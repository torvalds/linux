/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_PR_H
#define LINUX_PR_H

#include <uapi/linux/pr.h>

struct pr_ops {
	int (*pr_register)(struct block_device *bdev, u64 old_key, u64 new_key,
			u32 flags);
	int (*pr_reserve)(struct block_device *bdev, u64 key,
			enum pr_type type, u32 flags);
	int (*pr_release)(struct block_device *bdev, u64 key,
			enum pr_type type);
	int (*pr_preempt)(struct block_device *bdev, u64 old_key, u64 new_key,
			enum pr_type type, bool abort);
	int (*pr_clear)(struct block_device *bdev, u64 key);
};

#endif /* LINUX_PR_H */
