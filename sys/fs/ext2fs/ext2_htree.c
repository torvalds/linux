/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010, 2012 Zheng Liu <lz@freebsd.org>
 * Copyright (c) 2012, Vyacheslav Matyushin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/sysctl.h>

#include <ufs/ufs/dir.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_dir.h>
#include <fs/ext2fs/htree.h>

static void	ext2_append_entry(char *block, uint32_t blksize,
		    struct ext2fs_direct_2 *last_entry,
		    struct ext2fs_direct_2 *new_entry, int csum_size);
static int	ext2_htree_append_block(struct vnode *vp, char *data,
		    struct componentname *cnp, uint32_t blksize);
static int	ext2_htree_check_next(struct inode *ip, uint32_t hash,
		    const char *name, struct ext2fs_htree_lookup_info *info);
static int	ext2_htree_cmp_sort_entry(const void *e1, const void *e2);
static int	ext2_htree_find_leaf(struct inode *ip, const char *name,
		    int namelen, uint32_t *hash, uint8_t *hash_version,
		    struct ext2fs_htree_lookup_info *info);
static uint32_t ext2_htree_get_block(struct ext2fs_htree_entry *ep);
static uint16_t	ext2_htree_get_count(struct ext2fs_htree_entry *ep);
static uint32_t ext2_htree_get_hash(struct ext2fs_htree_entry *ep);
static uint16_t	ext2_htree_get_limit(struct ext2fs_htree_entry *ep);
static void	ext2_htree_insert_entry_to_level(struct ext2fs_htree_lookup_level *level,
		    uint32_t hash, uint32_t blk);
static void	ext2_htree_insert_entry(struct ext2fs_htree_lookup_info *info,
		    uint32_t hash, uint32_t blk);
static uint32_t	ext2_htree_node_limit(struct inode *ip);
static void	ext2_htree_set_block(struct ext2fs_htree_entry *ep,
		    uint32_t blk);
static void	ext2_htree_set_count(struct ext2fs_htree_entry *ep,
		    uint16_t cnt);
static void	ext2_htree_set_hash(struct ext2fs_htree_entry *ep,
		    uint32_t hash);
static void	ext2_htree_set_limit(struct ext2fs_htree_entry *ep,
		    uint16_t limit);
static int	ext2_htree_split_dirblock(struct inode *ip,
		    char *block1, char *block2, uint32_t blksize,
		    uint32_t *hash_seed, uint8_t hash_version,
		    uint32_t *split_hash, struct  ext2fs_direct_2 *entry);
static void	ext2_htree_release(struct ext2fs_htree_lookup_info *info);
static uint32_t	ext2_htree_root_limit(struct inode *ip, int len);
static int	ext2_htree_writebuf(struct inode *ip,
		    struct ext2fs_htree_lookup_info *info);

int
ext2_htree_has_idx(struct inode *ip)
{
	if (EXT2_HAS_COMPAT_FEATURE(ip->i_e2fs, EXT2F_COMPAT_DIRHASHINDEX) &&
	    ip->i_flag & IN_E3INDEX)
		return (1);
	else
		return (0);
}

static int
ext2_htree_check_next(struct inode *ip, uint32_t hash, const char *name,
    struct ext2fs_htree_lookup_info *info)
{
	struct vnode *vp = ITOV(ip);
	struct ext2fs_htree_lookup_level *level;
	struct buf *bp;
	uint32_t next_hash;
	int idx = info->h_levels_num - 1;
	int levels = 0;

	do {
		level = &info->h_levels[idx];
		level->h_entry++;
		if (level->h_entry < level->h_entries +
		    ext2_htree_get_count(level->h_entries))
			break;
		if (idx == 0)
			return (0);
		idx--;
		levels++;
	} while (1);

	next_hash = ext2_htree_get_hash(level->h_entry);
	if ((hash & 1) == 0) {
		if (hash != (next_hash & ~1))
			return (0);
	}

	while (levels > 0) {
		levels--;
		if (ext2_blkatoff(vp, ext2_htree_get_block(level->h_entry) *
		    ip->i_e2fs->e2fs_bsize, NULL, &bp) != 0)
			return (0);
		level = &info->h_levels[idx + 1];
		brelse(level->h_bp);
		level->h_bp = bp;
		level->h_entry = level->h_entries =
		    ((struct ext2fs_htree_node *)bp->b_data)->h_entries;
	}

	return (1);
}

static uint32_t
ext2_htree_get_block(struct ext2fs_htree_entry *ep)
{
	return (ep->h_blk & 0x00FFFFFF);
}

static void
ext2_htree_set_block(struct ext2fs_htree_entry *ep, uint32_t blk)
{
	ep->h_blk = blk;
}

static uint16_t
ext2_htree_get_count(struct ext2fs_htree_entry *ep)
{
	return (((struct ext2fs_htree_count *)(ep))->h_entries_num);
}

static void
ext2_htree_set_count(struct ext2fs_htree_entry *ep, uint16_t cnt)
{
	((struct ext2fs_htree_count *)(ep))->h_entries_num = cnt;
}

static uint32_t
ext2_htree_get_hash(struct ext2fs_htree_entry *ep)
{
	return (ep->h_hash);
}

static uint16_t
ext2_htree_get_limit(struct ext2fs_htree_entry *ep)
{
	return (((struct ext2fs_htree_count *)(ep))->h_entries_max);
}

static void
ext2_htree_set_hash(struct ext2fs_htree_entry *ep, uint32_t hash)
{
	ep->h_hash = hash;
}

static void
ext2_htree_set_limit(struct ext2fs_htree_entry *ep, uint16_t limit)
{
	((struct ext2fs_htree_count *)(ep))->h_entries_max = limit;
}

static void
ext2_htree_release(struct ext2fs_htree_lookup_info *info)
{
	u_int i;

	for (i = 0; i < info->h_levels_num; i++) {
		struct buf *bp = info->h_levels[i].h_bp;

		if (bp != NULL)
			brelse(bp);
	}
}

static uint32_t
ext2_htree_root_limit(struct inode *ip, int len)
{
	struct m_ext2fs *fs;
	uint32_t space;

	fs = ip->i_e2fs;
	space = ip->i_e2fs->e2fs_bsize - EXT2_DIR_REC_LEN(1) -
	    EXT2_DIR_REC_LEN(2) - len;

	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		space -= sizeof(struct ext2fs_htree_tail);

	return (space / sizeof(struct ext2fs_htree_entry));
}

static uint32_t
ext2_htree_node_limit(struct inode *ip)
{
	struct m_ext2fs *fs;
	uint32_t space;

	fs = ip->i_e2fs;
	space = fs->e2fs_bsize - EXT2_DIR_REC_LEN(0);

	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		space -= sizeof(struct ext2fs_htree_tail);

	return (space / sizeof(struct ext2fs_htree_entry));
}

static int
ext2_htree_find_leaf(struct inode *ip, const char *name, int namelen,
    uint32_t *hash, uint8_t *hash_ver,
    struct ext2fs_htree_lookup_info *info)
{
	struct vnode *vp;
	struct ext2fs *fs;
	struct m_ext2fs *m_fs;
	struct buf *bp = NULL;
	struct ext2fs_htree_root *rootp;
	struct ext2fs_htree_entry *entp, *start, *end, *middle, *found;
	struct ext2fs_htree_lookup_level *level_info;
	uint32_t hash_major = 0, hash_minor = 0;
	uint32_t levels, cnt;
	uint8_t hash_version;

	if (name == NULL || info == NULL)
		return (-1);

	vp = ITOV(ip);
	fs = ip->i_e2fs->e2fs;
	m_fs = ip->i_e2fs;

	if (ext2_blkatoff(vp, 0, NULL, &bp) != 0)
		return (-1);

	info->h_levels_num = 1;
	info->h_levels[0].h_bp = bp;
	rootp = (struct ext2fs_htree_root *)bp->b_data;
	if (rootp->h_info.h_hash_version != EXT2_HTREE_LEGACY &&
	    rootp->h_info.h_hash_version != EXT2_HTREE_HALF_MD4 &&
	    rootp->h_info.h_hash_version != EXT2_HTREE_TEA)
		goto error;

	hash_version = rootp->h_info.h_hash_version;
	if (hash_version <= EXT2_HTREE_TEA)
		hash_version += m_fs->e2fs_uhash;
	*hash_ver = hash_version;

	ext2_htree_hash(name, namelen, fs->e3fs_hash_seed,
	    hash_version, &hash_major, &hash_minor);
	*hash = hash_major;

	if ((levels = rootp->h_info.h_ind_levels) > 1)
		goto error;

	entp = (struct ext2fs_htree_entry *)(((char *)&rootp->h_info) +
	    rootp->h_info.h_info_len);

	if (ext2_htree_get_limit(entp) !=
	    ext2_htree_root_limit(ip, rootp->h_info.h_info_len))
		goto error;

	while (1) {
		cnt = ext2_htree_get_count(entp);
		if (cnt == 0 || cnt > ext2_htree_get_limit(entp))
			goto error;

		start = entp + 1;
		end = entp + cnt - 1;
		while (start <= end) {
			middle = start + (end - start) / 2;
			if (ext2_htree_get_hash(middle) > hash_major)
				end = middle - 1;
			else
				start = middle + 1;
		}
		found = start - 1;

		level_info = &(info->h_levels[info->h_levels_num - 1]);
		level_info->h_bp = bp;
		level_info->h_entries = entp;
		level_info->h_entry = found;
		if (levels == 0)
			return (0);
		levels--;
		if (ext2_blkatoff(vp,
		    ext2_htree_get_block(found) * m_fs->e2fs_bsize,
		    NULL, &bp) != 0)
			goto error;
		entp = ((struct ext2fs_htree_node *)bp->b_data)->h_entries;
		info->h_levels_num++;
		info->h_levels[info->h_levels_num - 1].h_bp = bp;
	}

error:
	ext2_htree_release(info);
	return (-1);
}

/*
 * Try to lookup a directory entry in HTree index
 */
int
ext2_htree_lookup(struct inode *ip, const char *name, int namelen,
    struct buf **bpp, int *entryoffp, doff_t *offp,
    doff_t *prevoffp, doff_t *endusefulp,
    struct ext2fs_searchslot *ss)
{
	struct vnode *vp;
	struct ext2fs_htree_lookup_info info;
	struct ext2fs_htree_entry *leaf_node;
	struct m_ext2fs *m_fs;
	struct buf *bp;
	uint32_t blk;
	uint32_t dirhash;
	uint32_t bsize;
	uint8_t hash_version;
	int search_next;
	int found = 0;

	m_fs = ip->i_e2fs;
	bsize = m_fs->e2fs_bsize;
	vp = ITOV(ip);

	/* TODO: print error msg because we don't lookup '.' and '..' */

	memset(&info, 0, sizeof(info));
	if (ext2_htree_find_leaf(ip, name, namelen, &dirhash,
	    &hash_version, &info))
		return (-1);

	do {
		leaf_node = info.h_levels[info.h_levels_num - 1].h_entry;
		blk = ext2_htree_get_block(leaf_node);
		if (ext2_blkatoff(vp, blk * bsize, NULL, &bp) != 0) {
			ext2_htree_release(&info);
			return (-1);
		}

		*offp = blk * bsize;
		*entryoffp = 0;
		*prevoffp = blk * bsize;
		*endusefulp = blk * bsize;

		if (ss->slotstatus == NONE) {
			ss->slotoffset = -1;
			ss->slotfreespace = 0;
		}

		if (ext2_search_dirblock(ip, bp->b_data, &found,
		    name, namelen, entryoffp, offp, prevoffp,
		    endusefulp, ss) != 0) {
			brelse(bp);
			ext2_htree_release(&info);
			return (-1);
		}

		if (found) {
			*bpp = bp;
			ext2_htree_release(&info);
			return (0);
		}

		brelse(bp);
		search_next = ext2_htree_check_next(ip, dirhash, name, &info);
	} while (search_next);

	ext2_htree_release(&info);
	return (ENOENT);
}

static int
ext2_htree_append_block(struct vnode *vp, char *data,
    struct componentname *cnp, uint32_t blksize)
{
	struct iovec aiov;
	struct uio auio;
	struct inode *dp = VTOI(vp);
	uint64_t cursize, newsize;
	int error;

	cursize = roundup(dp->i_size, blksize);
	newsize = cursize + blksize;

	auio.uio_offset = cursize;
	auio.uio_resid = blksize;
	aiov.iov_len = blksize;
	aiov.iov_base = data;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_SYSSPACE;
	error = VOP_WRITE(vp, &auio, IO_SYNC, cnp->cn_cred);
	if (!error)
		dp->i_size = newsize;

	return (error);
}

static int
ext2_htree_writebuf(struct inode* ip, struct ext2fs_htree_lookup_info *info)
{
	int i, error;

	for (i = 0; i < info->h_levels_num; i++) {
		struct buf *bp = info->h_levels[i].h_bp;
		ext2_dx_csum_set(ip, (struct ext2fs_direct_2 *)bp->b_data);
		error = bwrite(bp);
		if (error)
			return (error);
	}

	return (0);
}

static void
ext2_htree_insert_entry_to_level(struct ext2fs_htree_lookup_level *level,
    uint32_t hash, uint32_t blk)
{
	struct ext2fs_htree_entry *target;
	int entries_num;

	target = level->h_entry + 1;
	entries_num = ext2_htree_get_count(level->h_entries);

	memmove(target + 1, target, (char *)(level->h_entries + entries_num) -
	    (char *)target);
	ext2_htree_set_block(target, blk);
	ext2_htree_set_hash(target, hash);
	ext2_htree_set_count(level->h_entries, entries_num + 1);
}

/*
 * Insert an index entry to the index node.
 */
static void
ext2_htree_insert_entry(struct ext2fs_htree_lookup_info *info,
    uint32_t hash, uint32_t blk)
{
	struct ext2fs_htree_lookup_level *level;

	level = &info->h_levels[info->h_levels_num - 1];
	ext2_htree_insert_entry_to_level(level, hash, blk);
}

/*
 * Compare two entry sort descriptors by name hash value.
 * This is used together with qsort.
 */
static int
ext2_htree_cmp_sort_entry(const void *e1, const void *e2)
{
	const struct ext2fs_htree_sort_entry *entry1, *entry2;

	entry1 = (const struct ext2fs_htree_sort_entry *)e1;
	entry2 = (const struct ext2fs_htree_sort_entry *)e2;

	if (entry1->h_hash < entry2->h_hash)
		return (-1);
	if (entry1->h_hash > entry2->h_hash)
		return (1);
	return (0);
}

/*
 * Append an entry to the end of the directory block.
 */
static void
ext2_append_entry(char *block, uint32_t blksize,
    struct ext2fs_direct_2 *last_entry,
    struct ext2fs_direct_2 *new_entry, int csum_size)
{
	uint16_t entry_len;

	entry_len = EXT2_DIR_REC_LEN(last_entry->e2d_namlen);
	last_entry->e2d_reclen = entry_len;
	last_entry = (struct ext2fs_direct_2 *)((char *)last_entry + entry_len);
	new_entry->e2d_reclen = block + blksize - (char *)last_entry - csum_size;
	memcpy(last_entry, new_entry, EXT2_DIR_REC_LEN(new_entry->e2d_namlen));
}

/*
 * Move half of entries from the old directory block to the new one.
 */
static int
ext2_htree_split_dirblock(struct inode *ip, char *block1, char *block2,
    uint32_t blksize, uint32_t *hash_seed, uint8_t hash_version,
    uint32_t *split_hash, struct ext2fs_direct_2 *entry)
{
	struct m_ext2fs *fs;
	int entry_cnt = 0;
	int size = 0, csum_size = 0;
	int i, k;
	uint32_t offset;
	uint16_t entry_len = 0;
	uint32_t entry_hash;
	struct ext2fs_direct_2 *ep, *last;
	char *dest;
	struct ext2fs_htree_sort_entry *sort_info;

	fs = ip->i_e2fs;
	ep = (struct ext2fs_direct_2 *)block1;
	dest = block2;
	sort_info = (struct ext2fs_htree_sort_entry *)
	    ((char *)block2 + blksize);

	if (EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_METADATA_CKSUM))
		csum_size = sizeof(struct ext2fs_direct_tail);

	/*
	 * Calculate name hash value for the entry which is to be added.
	 */
	ext2_htree_hash(entry->e2d_name, entry->e2d_namlen, hash_seed,
	    hash_version, &entry_hash, NULL);

	/*
	 * Fill in directory entry sort descriptors.
	 */
	while ((char *)ep < block1 + blksize - csum_size) {
		if (ep->e2d_ino && ep->e2d_namlen) {
			entry_cnt++;
			sort_info--;
			sort_info->h_size = ep->e2d_reclen;
			sort_info->h_offset = (char *)ep - block1;
			ext2_htree_hash(ep->e2d_name, ep->e2d_namlen,
			    hash_seed, hash_version,
			    &sort_info->h_hash, NULL);
		}
		ep = (struct ext2fs_direct_2 *)
		    ((char *)ep + ep->e2d_reclen);
	}

	/*
	 * Sort directory entry descriptors by name hash value.
	 */
	qsort(sort_info, entry_cnt, sizeof(struct ext2fs_htree_sort_entry),
	    ext2_htree_cmp_sort_entry);

	/*
	 * Count the number of entries to move to directory block 2.
	 */
	for (i = entry_cnt - 1; i >= 0; i--) {
		if (sort_info[i].h_size + size > blksize / 2)
			break;
		size += sort_info[i].h_size;
	}

	*split_hash = sort_info[i + 1].h_hash;

	/*
	 * Set collision bit.
	 */
	if (*split_hash == sort_info[i].h_hash)
		*split_hash += 1;

	/*
	 * Move half of directory entries from block 1 to block 2.
	 */
	for (k = i + 1; k < entry_cnt; k++) {
		ep = (struct ext2fs_direct_2 *)((char *)block1 +
		    sort_info[k].h_offset);
		entry_len = EXT2_DIR_REC_LEN(ep->e2d_namlen);
		memcpy(dest, ep, entry_len);
		((struct ext2fs_direct_2 *)dest)->e2d_reclen = entry_len;
		/* Mark directory entry as unused. */
		ep->e2d_ino = 0;
		dest += entry_len;
	}
	dest -= entry_len;

	/* Shrink directory entries in block 1. */
	last = (struct ext2fs_direct_2 *)block1;
	entry_len = 0;
	for (offset = 0; offset < blksize - csum_size; ) {
		ep = (struct ext2fs_direct_2 *)(block1 + offset);
		offset += ep->e2d_reclen;
		if (ep->e2d_ino) {
			last = (struct ext2fs_direct_2 *)
			    ((char *)last + entry_len);
			entry_len = EXT2_DIR_REC_LEN(ep->e2d_namlen);
			memcpy((void *)last, (void *)ep, entry_len);
			last->e2d_reclen = entry_len;
		}
	}

	if (entry_hash >= *split_hash) {
		/* Add entry to block 2. */
		ext2_append_entry(block2, blksize,
		    (struct ext2fs_direct_2 *)dest, entry, csum_size);

		/* Adjust length field of last entry of block 1. */
		last->e2d_reclen = block1 + blksize - (char *)last - csum_size;
	} else {
		/* Add entry to block 1. */
		ext2_append_entry(block1, blksize, last, entry, csum_size);

		/* Adjust length field of last entry of block 2. */
		((struct ext2fs_direct_2 *)dest)->e2d_reclen =
		    block2 + blksize - dest - csum_size;
	}

	if (csum_size) {
		ext2_init_dirent_tail(EXT2_DIRENT_TAIL(block1, blksize));
		ext2_init_dirent_tail(EXT2_DIRENT_TAIL(block2, blksize));
	}

	return (0);
}

/*
 * Create an HTree index for a directory
 */
int
ext2_htree_create_index(struct vnode *vp, struct componentname *cnp,
    struct ext2fs_direct_2 *new_entry)
{
	struct buf *bp = NULL;
	struct inode *dp;
	struct ext2fs *fs;
	struct m_ext2fs *m_fs;
	struct ext2fs_direct_2 *ep, *dotdot;
	struct ext2fs_htree_root *root;
	struct ext2fs_htree_lookup_info info;
	uint32_t blksize, dirlen, split_hash;
	uint8_t hash_version;
	char *buf1 = NULL;
	char *buf2 = NULL;
	int error = 0;

	dp = VTOI(vp);
	fs = dp->i_e2fs->e2fs;
	m_fs = dp->i_e2fs;
	blksize = m_fs->e2fs_bsize;

	buf1 = malloc(blksize, M_TEMP, M_WAITOK | M_ZERO);
	buf2 = malloc(blksize, M_TEMP, M_WAITOK | M_ZERO);

	if ((error = ext2_blkatoff(vp, 0, NULL, &bp)) != 0)
		goto out;

	root = (struct ext2fs_htree_root *)bp->b_data;
	dotdot = (struct ext2fs_direct_2 *)((char *)&(root->h_dotdot));
	ep = (struct ext2fs_direct_2 *)((char *)dotdot + dotdot->e2d_reclen);
	dirlen = (char *)root + blksize - (char *)ep;
	memcpy(buf1, ep, dirlen);
	ep = (struct ext2fs_direct_2 *)buf1;
	while ((char *)ep < buf1 + dirlen)
		ep = (struct ext2fs_direct_2 *)
		    ((char *)ep + ep->e2d_reclen);
	ep->e2d_reclen = buf1 + blksize - (char *)ep;

	dp->i_flag |= IN_E3INDEX;

	/*
	 * Initialize index root.
	 */
	dotdot->e2d_reclen = blksize - EXT2_DIR_REC_LEN(1);
	memset(&root->h_info, 0, sizeof(root->h_info));
	root->h_info.h_hash_version = fs->e3fs_def_hash_version;
	root->h_info.h_info_len = sizeof(root->h_info);
	ext2_htree_set_block(root->h_entries, 1);
	ext2_htree_set_count(root->h_entries, 1);
	ext2_htree_set_limit(root->h_entries,
	    ext2_htree_root_limit(dp, sizeof(root->h_info)));

	memset(&info, 0, sizeof(info));
	info.h_levels_num = 1;
	info.h_levels[0].h_entries = root->h_entries;
	info.h_levels[0].h_entry = root->h_entries;

	hash_version = root->h_info.h_hash_version;
	if (hash_version <= EXT2_HTREE_TEA)
		hash_version += m_fs->e2fs_uhash;
	ext2_htree_split_dirblock(dp, buf1, buf2, blksize, fs->e3fs_hash_seed,
	    hash_version, &split_hash, new_entry);
	ext2_htree_insert_entry(&info, split_hash, 2);

	/*
	 * Write directory block 0.
	 */
	ext2_dx_csum_set(dp, (struct ext2fs_direct_2 *)bp->b_data);
	if (DOINGASYNC(vp)) {
		bdwrite(bp);
		error = 0;
	} else {
		error = bwrite(bp);
	}
	dp->i_flag |= IN_CHANGE | IN_UPDATE;
	if (error)
		goto out;

	/*
	 * Write directory block 1.
	 */
	ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)buf1);
	error = ext2_htree_append_block(vp, buf1, cnp, blksize);
	if (error)
		goto out1;

	/*
	 * Write directory block 2.
	 */
	ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)buf2);
	error = ext2_htree_append_block(vp, buf2, cnp, blksize);

	free(buf1, M_TEMP);
	free(buf2, M_TEMP);
	return (error);
out:
	if (bp != NULL)
		brelse(bp);
out1:
	free(buf1, M_TEMP);
	free(buf2, M_TEMP);
	return (error);
}

/*
 * Add an entry to the directory using htree index.
 */
int
ext2_htree_add_entry(struct vnode *dvp, struct ext2fs_direct_2 *entry,
    struct componentname *cnp)
{
	struct ext2fs_htree_entry *entries, *leaf_node;
	struct ext2fs_htree_lookup_info info;
	struct buf *bp = NULL;
	struct ext2fs *fs;
	struct m_ext2fs *m_fs;
	struct inode *ip;
	uint16_t ent_num;
	uint32_t dirhash, split_hash;
	uint32_t blksize, blknum;
	uint64_t cursize, dirsize;
	uint8_t hash_version;
	char *newdirblock = NULL;
	char *newidxblock = NULL;
	struct ext2fs_htree_node *dst_node;
	struct ext2fs_htree_entry *dst_entries;
	struct ext2fs_htree_entry *root_entires;
	struct buf *dst_bp = NULL;
	int error, write_bp = 0, write_dst_bp = 0, write_info = 0;

	ip = VTOI(dvp);
	m_fs = ip->i_e2fs;
	fs = m_fs->e2fs;
	blksize = m_fs->e2fs_bsize;

	if (ip->i_count != 0)
		return ext2_add_entry(dvp, entry);

	/* Target directory block is full, split it */
	memset(&info, 0, sizeof(info));
	error = ext2_htree_find_leaf(ip, entry->e2d_name, entry->e2d_namlen,
	    &dirhash, &hash_version, &info);
	if (error)
		return (error);

	entries = info.h_levels[info.h_levels_num - 1].h_entries;
	ent_num = ext2_htree_get_count(entries);
	if (ent_num == ext2_htree_get_limit(entries)) {
		/* Split the index node. */
		root_entires = info.h_levels[0].h_entries;
		newidxblock = malloc(blksize, M_TEMP, M_WAITOK | M_ZERO);
		dst_node = (struct ext2fs_htree_node *)newidxblock;
		memset(&dst_node->h_fake_dirent, 0,
		    sizeof(dst_node->h_fake_dirent));
		dst_node->h_fake_dirent.e2d_reclen = blksize;

		cursize = roundup(ip->i_size, blksize);
		dirsize = cursize + blksize;
		blknum = dirsize / blksize - 1;
		ext2_dx_csum_set(ip, (struct ext2fs_direct_2 *)newidxblock);
		error = ext2_htree_append_block(dvp, newidxblock,
		    cnp, blksize);
		if (error)
			goto finish;
		error = ext2_blkatoff(dvp, cursize, NULL, &dst_bp);
		if (error)
			goto finish;
		dst_node = (struct ext2fs_htree_node *)dst_bp->b_data;
		dst_entries = dst_node->h_entries;

		if (info.h_levels_num == 2) {
			uint16_t src_ent_num, dst_ent_num;

			if (ext2_htree_get_count(root_entires) ==
			    ext2_htree_get_limit(root_entires)) {
				/* Directory index is full */
				error = EIO;
				goto finish;
			}

			src_ent_num = ent_num / 2;
			dst_ent_num = ent_num - src_ent_num;
			split_hash = ext2_htree_get_hash(entries + src_ent_num);

			/* Move half of index entries to the new index node */
			memcpy(dst_entries, entries + src_ent_num,
			    dst_ent_num * sizeof(struct ext2fs_htree_entry));
			ext2_htree_set_count(entries, src_ent_num);
			ext2_htree_set_count(dst_entries, dst_ent_num);
			ext2_htree_set_limit(dst_entries,
			    ext2_htree_node_limit(ip));

			if (info.h_levels[1].h_entry >= entries + src_ent_num) {
				struct buf *tmp = info.h_levels[1].h_bp;

				info.h_levels[1].h_bp = dst_bp;
				dst_bp = tmp;

				info.h_levels[1].h_entry =
				    info.h_levels[1].h_entry -
				    (entries + src_ent_num) +
				    dst_entries;
				info.h_levels[1].h_entries = dst_entries;
			}
			ext2_htree_insert_entry_to_level(&info.h_levels[0],
			    split_hash, blknum);

			/* Write new index node to disk */
			ext2_dx_csum_set(ip,
			    (struct ext2fs_direct_2 *)dst_bp->b_data);
			error = bwrite(dst_bp);
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
			if (error)
				goto finish;
			write_dst_bp = 1;
		} else {
			/* Create second level for htree index */
			struct ext2fs_htree_root *idx_root;

			memcpy(dst_entries, entries,
			    ent_num * sizeof(struct ext2fs_htree_entry));
			ext2_htree_set_limit(dst_entries,
			    ext2_htree_node_limit(ip));

			idx_root = (struct ext2fs_htree_root *)
			    info.h_levels[0].h_bp->b_data;
			idx_root->h_info.h_ind_levels = 1;

			ext2_htree_set_count(entries, 1);
			ext2_htree_set_block(entries, blknum);

			info.h_levels_num = 2;
			info.h_levels[1].h_entries = dst_entries;
			info.h_levels[1].h_entry = info.h_levels[0].h_entry -
			    info.h_levels[0].h_entries + dst_entries;
			info.h_levels[1].h_bp = dst_bp;
			dst_bp = NULL;
		}
	}

	leaf_node = info.h_levels[info.h_levels_num - 1].h_entry;
	blknum = ext2_htree_get_block(leaf_node);
	error = ext2_blkatoff(dvp, blknum * blksize, NULL, &bp);
	if (error)
		goto finish;

	/* Split target directory block */
	newdirblock = malloc(blksize, M_TEMP, M_WAITOK | M_ZERO);
	ext2_htree_split_dirblock(ip, (char *)bp->b_data, newdirblock, blksize,
	    fs->e3fs_hash_seed, hash_version, &split_hash, entry);
	cursize = roundup(ip->i_size, blksize);
	dirsize = cursize + blksize;
	blknum = dirsize / blksize - 1;

	/* Add index entry for the new directory block */
	ext2_htree_insert_entry(&info, split_hash, blknum);

	/* Write the new directory block to the end of the directory */
	ext2_dirent_csum_set(ip, (struct ext2fs_direct_2 *)newdirblock);
	error = ext2_htree_append_block(dvp, newdirblock, cnp, blksize);
	if (error)
		goto finish;

	/* Write the target directory block */
	ext2_dirent_csum_set(ip, (struct ext2fs_direct_2 *)bp->b_data);
	error = bwrite(bp);
	ip->i_flag |= IN_CHANGE | IN_UPDATE;
	if (error)
		goto finish;
	write_bp = 1;

	/* Write the index block */
	error = ext2_htree_writebuf(ip, &info);
	if (!error)
		write_info = 1;

finish:
	if (dst_bp != NULL && !write_dst_bp)
		brelse(dst_bp);
	if (bp != NULL && !write_bp)
		brelse(bp);
	if (newdirblock != NULL)
		free(newdirblock, M_TEMP);
	if (newidxblock != NULL)
		free(newidxblock, M_TEMP);
	if (!write_info)
		ext2_htree_release(&info);
	return (error);
}
