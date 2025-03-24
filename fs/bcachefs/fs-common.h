/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_COMMON_H
#define _BCACHEFS_FS_COMMON_H

#include "dirent.h"

struct posix_acl;

#define BCH_CREATE_TMPFILE		(1U << 0)
#define BCH_CREATE_SUBVOL		(1U << 1)
#define BCH_CREATE_SNAPSHOT		(1U << 2)
#define BCH_CREATE_SNAPSHOT_RO		(1U << 3)

int bch2_create_trans(struct btree_trans *, subvol_inum,
		      struct bch_inode_unpacked *,
		      struct bch_inode_unpacked *,
		      const struct qstr *,
		      uid_t, gid_t, umode_t, dev_t,
		      struct posix_acl *,
		      struct posix_acl *,
		      subvol_inum, unsigned);

int bch2_link_trans(struct btree_trans *,
		    subvol_inum, struct bch_inode_unpacked *,
		    subvol_inum, struct bch_inode_unpacked *,
		    const struct qstr *);

int bch2_unlink_trans(struct btree_trans *, subvol_inum,
		      struct bch_inode_unpacked *,
		      struct bch_inode_unpacked *,
		      const struct qstr *, bool);

int bch2_rename_trans(struct btree_trans *,
		      subvol_inum, struct bch_inode_unpacked *,
		      subvol_inum, struct bch_inode_unpacked *,
		      struct bch_inode_unpacked *,
		      struct bch_inode_unpacked *,
		      const struct qstr *,
		      const struct qstr *,
		      enum bch_rename_mode);

bool bch2_reinherit_attrs(struct bch_inode_unpacked *,
			  struct bch_inode_unpacked *);

int bch2_inum_to_path(struct btree_trans *, subvol_inum, struct printbuf *);

#endif /* _BCACHEFS_FS_COMMON_H */
