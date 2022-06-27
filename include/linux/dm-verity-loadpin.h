/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_DM_VERITY_LOADPIN_H
#define __LINUX_DM_VERITY_LOADPIN_H

#include <linux/list.h>

struct block_device;

extern struct list_head dm_verity_loadpin_trusted_root_digests;

struct dm_verity_loadpin_trusted_root_digest {
	struct list_head node;
	unsigned int len;
	u8 data[];
};

#if IS_ENABLED(CONFIG_SECURITY_LOADPIN_VERITY)
bool dm_verity_loadpin_is_bdev_trusted(struct block_device *bdev);
#else
static inline bool dm_verity_loadpin_is_bdev_trusted(struct block_device *bdev)
{
	return false;
}
#endif

#endif /* __LINUX_DM_VERITY_LOADPIN_H */
