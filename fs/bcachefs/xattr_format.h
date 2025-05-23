/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_XATTR_FORMAT_H
#define _BCACHEFS_XATTR_FORMAT_H

#define KEY_TYPE_XATTR_INDEX_USER		0
#define KEY_TYPE_XATTR_INDEX_POSIX_ACL_ACCESS	1
#define KEY_TYPE_XATTR_INDEX_POSIX_ACL_DEFAULT	2
#define KEY_TYPE_XATTR_INDEX_TRUSTED		3
#define KEY_TYPE_XATTR_INDEX_SECURITY	        4

struct bch_xattr {
	struct bch_val		v;
	__u8			x_type;
	__u8			x_name_len;
	__le16			x_val_len;
	/*
	 * x_name contains the name and value counted by
	 * x_name_len + x_val_len. The introduction of
	 * __counted_by(x_name_len) caused a false positive
	 * detection of an out of bounds write.
	 */
	__u8			x_name[];
} __packed __aligned(8);

#endif /* _BCACHEFS_XATTR_FORMAT_H */
