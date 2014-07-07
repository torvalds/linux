/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef XFS_SYNC_H
#define XFS_SYNC_H 1

struct xfs_mount;
struct xfs_perag;

struct xfs_eofblocks {
	__u32		eof_flags;
	kuid_t		eof_uid;
	kgid_t		eof_gid;
	prid_t		eof_prid;
	__u64		eof_min_file_size;
};

#define SYNC_WAIT		0x0001	/* wait for i/o to complete */
#define SYNC_TRYLOCK		0x0002  /* only try to lock inodes */

/*
 * Flags for xfs_iget()
 */
#define XFS_IGET_CREATE		0x1
#define XFS_IGET_UNTRUSTED	0x2
#define XFS_IGET_DONTCACHE	0x4

int xfs_iget(struct xfs_mount *mp, struct xfs_trans *tp, xfs_ino_t ino,
	     uint flags, uint lock_flags, xfs_inode_t **ipp);

/* recovery needs direct inode allocation capability */
struct xfs_inode * xfs_inode_alloc(struct xfs_mount *mp, xfs_ino_t ino);
void xfs_inode_free(struct xfs_inode *ip);

void xfs_reclaim_worker(struct work_struct *work);

int xfs_reclaim_inodes(struct xfs_mount *mp, int mode);
int xfs_reclaim_inodes_count(struct xfs_mount *mp);
long xfs_reclaim_inodes_nr(struct xfs_mount *mp, int nr_to_scan);

void xfs_inode_set_reclaim_tag(struct xfs_inode *ip);

void xfs_inode_set_eofblocks_tag(struct xfs_inode *ip);
void xfs_inode_clear_eofblocks_tag(struct xfs_inode *ip);
int xfs_icache_free_eofblocks(struct xfs_mount *, struct xfs_eofblocks *);
void xfs_eofblocks_worker(struct work_struct *);

int xfs_inode_ag_iterator(struct xfs_mount *mp,
	int (*execute)(struct xfs_inode *ip, int flags, void *args),
	int flags, void *args);
int xfs_inode_ag_iterator_tag(struct xfs_mount *mp,
	int (*execute)(struct xfs_inode *ip, int flags, void *args),
	int flags, void *args, int tag);

static inline int
xfs_fs_eofblocks_from_user(
	struct xfs_fs_eofblocks		*src,
	struct xfs_eofblocks		*dst)
{
	if (src->eof_version != XFS_EOFBLOCKS_VERSION)
		return EINVAL;

	if (src->eof_flags & ~XFS_EOF_FLAGS_VALID)
		return EINVAL;

	if (memchr_inv(&src->pad32, 0, sizeof(src->pad32)) ||
	    memchr_inv(src->pad64, 0, sizeof(src->pad64)))
		return EINVAL;

	dst->eof_flags = src->eof_flags;
	dst->eof_prid = src->eof_prid;
	dst->eof_min_file_size = src->eof_min_file_size;

	dst->eof_uid = INVALID_UID;
	if (src->eof_flags & XFS_EOF_FLAGS_UID) {
		dst->eof_uid = make_kuid(current_user_ns(), src->eof_uid);
		if (!uid_valid(dst->eof_uid))
			return EINVAL;
	}

	dst->eof_gid = INVALID_GID;
	if (src->eof_flags & XFS_EOF_FLAGS_GID) {
		dst->eof_gid = make_kgid(current_user_ns(), src->eof_gid);
		if (!gid_valid(dst->eof_gid))
			return EINVAL;
	}
	return 0;
}

#endif
