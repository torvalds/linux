/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_COMMON_H
#define _BCACHEFS_FS_COMMON_H

struct posix_acl;

int bch2_create_trans(struct btree_trans *, u64,
		      struct bch_inode_unpacked *,
		      struct bch_inode_unpacked *,
		      const struct qstr *,
		      uid_t, gid_t, umode_t, dev_t,
		      struct posix_acl *,
		      struct posix_acl *);

int bch2_link_trans(struct btree_trans *,
		    u64,
		    u64, struct bch_inode_unpacked *,
		    const struct qstr *);

int bch2_unlink_trans(struct btree_trans *,
		      u64, struct bch_inode_unpacked *,
		      struct bch_inode_unpacked *,
		      const struct qstr *);

int bch2_rename_trans(struct btree_trans *,
		      u64, struct bch_inode_unpacked *,
		      u64, struct bch_inode_unpacked *,
		      struct bch_inode_unpacked *,
		      struct bch_inode_unpacked *,
		      const struct qstr *,
		      const struct qstr *,
		      enum bch_rename_mode);

bool bch2_reinherit_attrs(struct bch_inode_unpacked *,
			  struct bch_inode_unpacked *);

#endif /* _BCACHEFS_FS_COMMON_H */
