/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_INODE_FORMAT_H
#define _BCACHEFS_INODE_FORMAT_H

#define BLOCKDEV_INODE_MAX	4096
#define BCACHEFS_ROOT_INO	4096

struct bch_inode {
	struct bch_val		v;

	__le64			bi_hash_seed;
	__le32			bi_flags;
	__le16			bi_mode;
	__u8			fields[];
} __packed __aligned(8);

struct bch_inode_v2 {
	struct bch_val		v;

	__le64			bi_journal_seq;
	__le64			bi_hash_seed;
	__le64			bi_flags;
	__le16			bi_mode;
	__u8			fields[];
} __packed __aligned(8);

struct bch_inode_v3 {
	struct bch_val		v;

	__le64			bi_journal_seq;
	__le64			bi_hash_seed;
	__le64			bi_flags;
	__le64			bi_sectors;
	__le64			bi_size;
	__le64			bi_version;
	__u8			fields[];
} __packed __aligned(8);

#define INODEv3_FIELDS_START_INITIAL	6
#define INODEv3_FIELDS_START_CUR	(offsetof(struct bch_inode_v3, fields) / sizeof(__u64))

struct bch_inode_generation {
	struct bch_val		v;

	__le32			bi_generation;
	__le32			pad;
} __packed __aligned(8);

/*
 * bi_subvol and bi_parent_subvol are only set for subvolume roots:
 */

#define BCH_INODE_FIELDS_v2()			\
	x(bi_atime,			96)	\
	x(bi_ctime,			96)	\
	x(bi_mtime,			96)	\
	x(bi_otime,			96)	\
	x(bi_size,			64)	\
	x(bi_sectors,			64)	\
	x(bi_uid,			32)	\
	x(bi_gid,			32)	\
	x(bi_nlink,			32)	\
	x(bi_generation,		32)	\
	x(bi_dev,			32)	\
	x(bi_data_checksum,		8)	\
	x(bi_compression,		8)	\
	x(bi_project,			32)	\
	x(bi_background_compression,	8)	\
	x(bi_data_replicas,		8)	\
	x(bi_promote_target,		16)	\
	x(bi_foreground_target,		16)	\
	x(bi_background_target,		16)	\
	x(bi_erasure_code,		16)	\
	x(bi_fields_set,		16)	\
	x(bi_dir,			64)	\
	x(bi_dir_offset,		64)	\
	x(bi_subvol,			32)	\
	x(bi_parent_subvol,		32)

#define BCH_INODE_FIELDS_v3()			\
	x(bi_atime,			96)	\
	x(bi_ctime,			96)	\
	x(bi_mtime,			96)	\
	x(bi_otime,			96)	\
	x(bi_uid,			32)	\
	x(bi_gid,			32)	\
	x(bi_nlink,			32)	\
	x(bi_generation,		32)	\
	x(bi_dev,			32)	\
	x(bi_data_checksum,		8)	\
	x(bi_compression,		8)	\
	x(bi_project,			32)	\
	x(bi_background_compression,	8)	\
	x(bi_data_replicas,		8)	\
	x(bi_promote_target,		16)	\
	x(bi_foreground_target,		16)	\
	x(bi_background_target,		16)	\
	x(bi_erasure_code,		16)	\
	x(bi_fields_set,		16)	\
	x(bi_dir,			64)	\
	x(bi_dir_offset,		64)	\
	x(bi_subvol,			32)	\
	x(bi_parent_subvol,		32)	\
	x(bi_nocow,			8)

/* subset of BCH_INODE_FIELDS */
#define BCH_INODE_OPTS()			\
	x(data_checksum,		8)	\
	x(compression,			8)	\
	x(project,			32)	\
	x(background_compression,	8)	\
	x(data_replicas,		8)	\
	x(promote_target,		16)	\
	x(foreground_target,		16)	\
	x(background_target,		16)	\
	x(erasure_code,			16)	\
	x(nocow,			8)

enum inode_opt_id {
#define x(name, ...)				\
	Inode_opt_##name,
	BCH_INODE_OPTS()
#undef  x
	Inode_opt_nr,
};

#define BCH_INODE_FLAGS()			\
	x(sync,				0)	\
	x(immutable,			1)	\
	x(append,			2)	\
	x(nodump,			3)	\
	x(noatime,			4)	\
	x(i_size_dirty,			5)	\
	x(i_sectors_dirty,		6)	\
	x(unlinked,			7)	\
	x(backptr_untrusted,		8)	\
	x(has_child_snapshot,		9)

/* bits 20+ reserved for packed fields below: */

enum bch_inode_flags {
#define x(t, n)	BCH_INODE_##t = 1U << n,
	BCH_INODE_FLAGS()
#undef x
};

enum __bch_inode_flags {
#define x(t, n)	__BCH_INODE_##t = n,
	BCH_INODE_FLAGS()
#undef x
};

LE32_BITMASK(INODEv1_STR_HASH,	struct bch_inode, bi_flags, 20, 24);
LE32_BITMASK(INODEv1_NR_FIELDS,	struct bch_inode, bi_flags, 24, 31);
LE32_BITMASK(INODEv1_NEW_VARINT,struct bch_inode, bi_flags, 31, 32);

LE64_BITMASK(INODEv2_STR_HASH,	struct bch_inode_v2, bi_flags, 20, 24);
LE64_BITMASK(INODEv2_NR_FIELDS,	struct bch_inode_v2, bi_flags, 24, 31);

LE64_BITMASK(INODEv3_STR_HASH,	struct bch_inode_v3, bi_flags, 20, 24);
LE64_BITMASK(INODEv3_NR_FIELDS,	struct bch_inode_v3, bi_flags, 24, 31);

LE64_BITMASK(INODEv3_FIELDS_START,
				struct bch_inode_v3, bi_flags, 31, 36);
LE64_BITMASK(INODEv3_MODE,	struct bch_inode_v3, bi_flags, 36, 52);

#endif /* _BCACHEFS_INODE_FORMAT_H */
