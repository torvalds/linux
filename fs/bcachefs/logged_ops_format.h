/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_LOGGED_OPS_FORMAT_H
#define _BCACHEFS_LOGGED_OPS_FORMAT_H

enum logged_ops_inums {
	LOGGED_OPS_INUM_logged_ops,
	LOGGED_OPS_INUM_inode_cursors,
};

struct bch_logged_op_truncate {
	struct bch_val		v;
	__le32			subvol;
	__le32			pad;
	__le64			inum;
	__le64			new_i_size;
};

enum logged_op_finsert_state {
	LOGGED_OP_FINSERT_start,
	LOGGED_OP_FINSERT_shift_extents,
	LOGGED_OP_FINSERT_finish,
};

struct bch_logged_op_finsert {
	struct bch_val		v;
	__u8			state;
	__u8			pad[3];
	__le32			subvol;
	__le64			inum;
	__le64			dst_offset;
	__le64			src_offset;
	__le64			pos;
};

#endif /* _BCACHEFS_LOGGED_OPS_FORMAT_H */
