/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DIRENT_FORMAT_H
#define _BCACHEFS_DIRENT_FORMAT_H

/*
 * Dirents (and xattrs) have to implement string lookups; since our b-tree
 * doesn't support arbitrary length strings for the key, we instead index by a
 * 64 bit hash (currently truncated sha1) of the string, stored in the offset
 * field of the key - using linear probing to resolve hash collisions. This also
 * provides us with the readdir cookie posix requires.
 *
 * Linear probing requires us to use whiteouts for deletions, in the event of a
 * collision:
 */

struct bch_dirent {
	struct bch_val		v;

	/* Target inode number: */
	union {
	__le64			d_inum;
	struct {		/* DT_SUBVOL */
	__le32			d_child_subvol;
	__le32			d_parent_subvol;
	};
	};

	/*
	 * Copy of mode bits 12-15 from the target inode - so userspace can get
	 * the filetype without having to do a stat()
	 */
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8			d_type:5,
				d_unused:2,
				d_casefold:1;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u8			d_casefold:1,
				d_unused:2,
				d_type:5;
#endif

	union {
	struct {
		__u8		d_pad;
		__le16		d_name_len;
		__le16		d_cf_name_len;
		__u8		d_names[];
	} d_cf_name_block __packed;
	__DECLARE_FLEX_ARRAY(__u8, d_name);
	} __packed;
} __packed __aligned(8);

#define DT_SUBVOL	16
#define BCH_DT_MAX	17

#define BCH_NAME_MAX	512

#endif /* _BCACHEFS_DIRENT_FORMAT_H */
