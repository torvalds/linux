/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUPER_TYPES_H
#define _BCACHEFS_SUPER_TYPES_H

struct bch_sb_handle {
	struct bch_sb		*sb;
	struct file		*s_bdev_file;
	struct block_device	*bdev;
	char			*sb_name;
	struct bio		*bio;
	void			*holder;
	size_t			buffer_size;
	blk_mode_t		mode;
	unsigned		have_layout:1;
	unsigned		have_bio:1;
	unsigned		fs_sb:1;
	u64			seq;
};

struct bch_devs_mask {
	unsigned long d[BITS_TO_LONGS(BCH_SB_MEMBERS_MAX)];
};

struct bch_devs_list {
	u8			nr;
	u8			data[BCH_BKEY_PTRS_MAX];
};

#endif /* _BCACHEFS_SUPER_TYPES_H */
