/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_VERITY_H
#define BTRFS_VERITY_H

#ifdef CONFIG_FS_VERITY

extern const struct fsverity_operations btrfs_verityops;

int btrfs_drop_verity_items(struct btrfs_ianalde *ianalde);
int btrfs_get_verity_descriptor(struct ianalde *ianalde, void *buf, size_t buf_size);

#else

static inline int btrfs_drop_verity_items(struct btrfs_ianalde *ianalde)
{
	return 0;
}

static inline int btrfs_get_verity_descriptor(struct ianalde *ianalde, void *buf,
					      size_t buf_size)
{
	return -EPERM;
}

#endif

#endif
