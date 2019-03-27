/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017, Fedor Uporov
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
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/conf.h>
#include <sys/extattr.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2_extattr.h>
#include <fs/ext2fs/ext2_extern.h>

static int
ext2_extattr_attrnamespace_to_bsd(int attrnamespace)
{

	switch (attrnamespace) {
	case EXT4_XATTR_INDEX_SYSTEM:
		return (EXTATTR_NAMESPACE_SYSTEM);

	case EXT4_XATTR_INDEX_USER:
		return (EXTATTR_NAMESPACE_USER);

	case EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT:
		return (POSIX1E_ACL_DEFAULT_EXTATTR_NAMESPACE);

	case EXT4_XATTR_INDEX_POSIX_ACL_ACCESS:
		return (POSIX1E_ACL_ACCESS_EXTATTR_NAMESPACE);
	}

	return (EXTATTR_NAMESPACE_EMPTY);
}

static const char *
ext2_extattr_name_to_bsd(int attrnamespace, const char *name, int* name_len)
{

	if (attrnamespace == EXT4_XATTR_INDEX_SYSTEM)
		return (name);
	else if (attrnamespace == EXT4_XATTR_INDEX_USER)
		return (name);
	else if (attrnamespace == EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT) {
		*name_len = strlen(POSIX1E_ACL_DEFAULT_EXTATTR_NAME);
		return (POSIX1E_ACL_DEFAULT_EXTATTR_NAME);
	} else if (attrnamespace == EXT4_XATTR_INDEX_POSIX_ACL_ACCESS) {
		*name_len = strlen(POSIX1E_ACL_ACCESS_EXTATTR_NAME);
		return (POSIX1E_ACL_ACCESS_EXTATTR_NAME);
	}

	/*
	 * XXX: Not all linux namespaces are mapped to bsd for now,
	 * return NULL, which will be converted to ENOTSUP on upper layer.
	 */
#ifdef EXT2FS_DEBUG
	printf("can not convert ext2fs name to bsd: namespace=%d\n", attrnamespace);
#endif

	return (NULL);
}

static int
ext2_extattr_attrnamespace_to_linux(int attrnamespace, const char *name)
{

	if (attrnamespace == POSIX1E_ACL_DEFAULT_EXTATTR_NAMESPACE &&
	    !strcmp(name, POSIX1E_ACL_DEFAULT_EXTATTR_NAME))
		return (EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT);

	if (attrnamespace == POSIX1E_ACL_ACCESS_EXTATTR_NAMESPACE &&
	    !strcmp(name, POSIX1E_ACL_ACCESS_EXTATTR_NAME))
		return (EXT4_XATTR_INDEX_POSIX_ACL_ACCESS);

	switch (attrnamespace) {
	case EXTATTR_NAMESPACE_SYSTEM:
		return (EXT4_XATTR_INDEX_SYSTEM);

	case EXTATTR_NAMESPACE_USER:
		return (EXT4_XATTR_INDEX_USER);
	}

	/*
	 * In this case namespace conversion should be unique,
	 * so this point is unreachable.
	 */
	return (-1);
}

static const char *
ext2_extattr_name_to_linux(int attrnamespace, const char *name)
{

	if (attrnamespace == POSIX1E_ACL_DEFAULT_EXTATTR_NAMESPACE ||
	    attrnamespace == POSIX1E_ACL_ACCESS_EXTATTR_NAMESPACE)
		return ("");
	else
		return (name);
}

int
ext2_extattr_valid_attrname(int attrnamespace, const char *attrname)
{
	if (attrnamespace == EXTATTR_NAMESPACE_EMPTY)
		return (EINVAL);

	if (strlen(attrname) == 0)
		return (EINVAL);

	if (strlen(attrname) + 1 > EXT2_EXTATTR_NAMELEN_MAX)
		return (ENAMETOOLONG);

	return (0);
}

static int
ext2_extattr_check(struct ext2fs_extattr_entry *entry, char *end)
{
	struct ext2fs_extattr_entry *next;

	while (!EXT2_IS_LAST_ENTRY(entry)) {
		next = EXT2_EXTATTR_NEXT(entry);
		if ((char *)next >= end)
			return (EIO);

		entry = next;
	}

	return (0);
}

static int
ext2_extattr_block_check(struct inode *ip, struct buf *bp)
{
	struct ext2fs_extattr_header *header;
	int error;

	header = (struct ext2fs_extattr_header *)bp->b_data;

	error = ext2_extattr_check(EXT2_IFIRST(header),
	    bp->b_data + bp->b_bufsize);
	if (error)
		return (error);

	return (ext2_extattr_blk_csum_verify(ip, bp));
}

int
ext2_extattr_inode_list(struct inode *ip, int attrnamespace,
    struct uio *uio, size_t *size)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_dinode_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	if ((error = bread(ip->i_devvp,
	    fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
	    (int)fs->e2fs_bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return (error);
	}

	struct ext2fs_dinode *dinode = (struct ext2fs_dinode *)
	    ((char *)bp->b_data +
	    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ip->i_number));

	/* Check attributes magic value */
	header = (struct ext2fs_extattr_dinode_header *)((char *)dinode +
	    E2FS_REV0_INODE_SIZE + dinode->e2di_extra_isize);

	if (header->h_magic != EXTATTR_MAGIC) {
		brelse(bp);
		return (0);
	}

	error = ext2_extattr_check(EXT2_IFIRST(header),
	    (char *)dinode + EXT2_INODE_SIZE(fs));
	if (error) {
		brelse(bp);
		return (error);
	}

	for (entry = EXT2_IFIRST(header); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (size != NULL)
			*size += name_len + 1;

		if (uio != NULL) {
			char *name = malloc(name_len + 1, M_TEMP, M_WAITOK);
			name[0] = name_len;
			memcpy(&name[1], attr_name, name_len);
			error = uiomove(name, name_len + 1, uio);
			free(name, M_TEMP);
			if (error)
				break;
		}
	}

	brelse(bp);

	return (error);
}

int
ext2_extattr_block_list(struct inode *ip, int attrnamespace,
    struct uio *uio, size_t *size)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	error = bread(ip->i_devvp, fsbtodb(fs, ip->i_facl),
	    fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	/* Check attributes magic value */
	header = EXT2_HDR(bp);
	if (header->h_magic != EXTATTR_MAGIC || header->h_blocks != 1) {
		brelse(bp);
		return (EINVAL);
	}

	error = ext2_extattr_block_check(ip, bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	for (entry = EXT2_FIRST_ENTRY(bp); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (size != NULL)
			*size += name_len + 1;

		if (uio != NULL) {
			char *name = malloc(name_len + 1, M_TEMP, M_WAITOK);
			name[0] = name_len;
			memcpy(&name[1], attr_name, name_len);
			error = uiomove(name, name_len + 1, uio);
			free(name, M_TEMP);
			if (error)
				break;
		}
	}

	brelse(bp);

	return (error);
}

int
ext2_extattr_inode_get(struct inode *ip, int attrnamespace,
    const char *name, struct uio *uio, size_t *size)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_dinode_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	if ((error = bread(ip->i_devvp,
	    fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
	    (int)fs->e2fs_bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return (error);
	}

	struct ext2fs_dinode *dinode = (struct ext2fs_dinode *)
	    ((char *)bp->b_data +
	    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ip->i_number));

	/* Check attributes magic value */
	header = (struct ext2fs_extattr_dinode_header *)((char *)dinode +
	    E2FS_REV0_INODE_SIZE + dinode->e2di_extra_isize);

	if (header->h_magic != EXTATTR_MAGIC) {
		brelse(bp);
		return (ENOATTR);
	}

	error = ext2_extattr_check(EXT2_IFIRST(header),
	    (char *)dinode + EXT2_INODE_SIZE(fs));
	if (error) {
		brelse(bp);
		return (error);
	}

	for (entry = EXT2_IFIRST(header); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			if (size != NULL)
				*size += entry->e_value_size;

			if (uio != NULL)
				error = uiomove(((char *)EXT2_IFIRST(header)) +
				    entry->e_value_offs, entry->e_value_size, uio);

			brelse(bp);
			return (error);
		}
	 }

	brelse(bp);

	return (ENOATTR);
}

int
ext2_extattr_block_get(struct inode *ip, int attrnamespace,
    const char *name, struct uio *uio, size_t *size)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	error = bread(ip->i_devvp, fsbtodb(fs, ip->i_facl),
	    fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	/* Check attributes magic value */
	header = EXT2_HDR(bp);
	if (header->h_magic != EXTATTR_MAGIC || header->h_blocks != 1) {
		brelse(bp);
		return (EINVAL);
	}

	error = ext2_extattr_block_check(ip, bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	for (entry = EXT2_FIRST_ENTRY(bp); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			if (size != NULL)
				*size += entry->e_value_size;

			if (uio != NULL)
				error = uiomove(bp->b_data + entry->e_value_offs,
				    entry->e_value_size, uio);

			brelse(bp);
			return (error);
		}
	 }

	brelse(bp);

	return (ENOATTR);
}

static uint16_t
ext2_extattr_delete_value(char *off,
    struct ext2fs_extattr_entry *first_entry,
    struct ext2fs_extattr_entry *entry, char *end)
{
	uint16_t min_offs;
	struct ext2fs_extattr_entry *next;

	min_offs = end - off;
	next = first_entry;
	while (!EXT2_IS_LAST_ENTRY(next)) {
		if (min_offs > next->e_value_offs && next->e_value_offs > 0)
			min_offs = next->e_value_offs;

		next = EXT2_EXTATTR_NEXT(next);
	}

	if (entry->e_value_size == 0)
		return (min_offs);

	memmove(off + min_offs + EXT2_EXTATTR_SIZE(entry->e_value_size),
	    off + min_offs, entry->e_value_offs - min_offs);

	/* Adjust all value offsets */
	next = first_entry;
	while (!EXT2_IS_LAST_ENTRY(next))
	{
		if (next->e_value_offs > 0 &&
		    next->e_value_offs < entry->e_value_offs)
			next->e_value_offs +=
			    EXT2_EXTATTR_SIZE(entry->e_value_size);

		next = EXT2_EXTATTR_NEXT(next);
	}

	min_offs += EXT2_EXTATTR_SIZE(entry->e_value_size);

	return (min_offs);
}

static void
ext2_extattr_delete_entry(char *off,
    struct ext2fs_extattr_entry *first_entry,
    struct ext2fs_extattr_entry *entry, char *end)
{
	char *pad;
	struct ext2fs_extattr_entry *next;

	/* Clean entry value */
	ext2_extattr_delete_value(off, first_entry, entry, end);

	/* Clean the entry */
	next = first_entry;
	while (!EXT2_IS_LAST_ENTRY(next))
		next = EXT2_EXTATTR_NEXT(next);

	pad = (char*)next + sizeof(uint32_t);

	memmove(entry, (char *)entry + EXT2_EXTATTR_LEN(entry->e_name_len),
	    pad - ((char *)entry + EXT2_EXTATTR_LEN(entry->e_name_len)));
}

int
ext2_extattr_inode_delete(struct inode *ip, int attrnamespace, const char *name)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_dinode_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	if ((error = bread(ip->i_devvp,
	    fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
	    (int)fs->e2fs_bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return (error);
	}

	struct ext2fs_dinode *dinode = (struct ext2fs_dinode *)
	    ((char *)bp->b_data +
	    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ip->i_number));

	/* Check attributes magic value */
	header = (struct ext2fs_extattr_dinode_header *)((char *)dinode +
	    E2FS_REV0_INODE_SIZE + dinode->e2di_extra_isize);

	if (header->h_magic != EXTATTR_MAGIC) {
		brelse(bp);
		return (ENOATTR);
	}

	error = ext2_extattr_check(EXT2_IFIRST(header),
	    (char *)dinode + EXT2_INODE_SIZE(fs));
	if (error) {
		brelse(bp);
		return (error);
	}

	/* If I am last entry, just make magic zero */
	entry = EXT2_IFIRST(header);
	if ((EXT2_IS_LAST_ENTRY(EXT2_EXTATTR_NEXT(entry))) &&
	    (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) ==
	    attrnamespace)) {

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			memset(header, 0, sizeof(struct ext2fs_extattr_dinode_header));

			return (bwrite(bp));
		}
	}

	for (entry = EXT2_IFIRST(header); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			ext2_extattr_delete_entry((char *)EXT2_IFIRST(header),
			    EXT2_IFIRST(header), entry,
			    (char *)dinode + EXT2_INODE_SIZE(fs));

			return (bwrite(bp));
		}
	}

	brelse(bp);

	return (ENOATTR);
}

static int
ext2_extattr_block_clone(struct inode *ip, struct buf **bpp)
{
	struct m_ext2fs *fs;
	struct buf *sbp;
	struct buf *cbp;
	struct ext2fs_extattr_header *header;
	uint64_t facl;

	fs = ip->i_e2fs;
	sbp = *bpp;

	header = EXT2_HDR(sbp);
	if (header->h_magic != EXTATTR_MAGIC || header->h_refcount == 1)
		return (EINVAL);

	facl = ext2_alloc_meta(ip);
	if (!facl)
		return (ENOSPC);

	cbp = getblk(ip->i_devvp, fsbtodb(fs, facl), fs->e2fs_bsize, 0, 0, 0);
	if (!cbp) {
		ext2_blkfree(ip, facl, fs->e2fs_bsize);
		return (EIO);
	}

	memcpy(cbp->b_data, sbp->b_data, fs->e2fs_bsize);
	header->h_refcount--;
	bwrite(sbp);

	ip->i_facl = facl;
	ext2_update(ip->i_vnode, 1);

	header = EXT2_HDR(cbp);
	header->h_refcount = 1;

	*bpp = cbp;

	return (0);
}

int
ext2_extattr_block_delete(struct inode *ip, int attrnamespace, const char *name)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	int error;

	fs = ip->i_e2fs;

	error = bread(ip->i_devvp, fsbtodb(fs, ip->i_facl),
	    fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	/* Check attributes magic value */
	header = EXT2_HDR(bp);
	if (header->h_magic != EXTATTR_MAGIC || header->h_blocks != 1) {
		brelse(bp);
		return (EINVAL);
	}

	error = ext2_extattr_block_check(ip, bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	if (header->h_refcount > 1) {
		error = ext2_extattr_block_clone(ip, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
	}

	/* If I am last entry, clean me and free the block */
	entry = EXT2_FIRST_ENTRY(bp);
	if (EXT2_IS_LAST_ENTRY(EXT2_EXTATTR_NEXT(entry)) &&
	    (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) ==
	    attrnamespace)) {

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			ip->i_blocks -= btodb(fs->e2fs_bsize);
			ext2_blkfree(ip, ip->i_facl, fs->e2fs_bsize);
			ip->i_facl = 0;
			error = ext2_update(ip->i_vnode, 1);

			brelse(bp);
			return (error);
		}
	}

	for (entry = EXT2_FIRST_ENTRY(bp); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len)) {
			ext2_extattr_delete_entry(bp->b_data,
			    EXT2_FIRST_ENTRY(bp), entry,
			    bp->b_data + bp->b_bufsize);

			return (bwrite(bp));
		}
	}

	brelse(bp);

	return (ENOATTR);
}

static struct ext2fs_extattr_entry *
allocate_entry(const char *name, int attrnamespace, uint16_t offs,
    uint32_t size, uint32_t hash)
{
	const char *attr_name;
	int name_len;
	struct ext2fs_extattr_entry *entry;

	attr_name = ext2_extattr_name_to_linux(attrnamespace, name);
	name_len = strlen(attr_name);

	entry = malloc(sizeof(struct ext2fs_extattr_entry) + name_len,
	    M_TEMP, M_WAITOK);

	entry->e_name_len = name_len;
	entry->e_name_index = ext2_extattr_attrnamespace_to_linux(attrnamespace, name);
	entry->e_value_offs = offs;
	entry->e_value_block = 0;
	entry->e_value_size = size;
	entry->e_hash = hash;
	memcpy(entry->e_name, name, name_len);

	return (entry);
}

static void
free_entry(struct ext2fs_extattr_entry *entry)
{

	free(entry, M_TEMP);
}

static int
ext2_extattr_get_size(struct ext2fs_extattr_entry *first_entry,
    struct ext2fs_extattr_entry *exist_entry, int header_size,
    int name_len, int new_size)
{
	struct ext2fs_extattr_entry *entry;
	int size;

	size = header_size;
	size += sizeof(uint32_t);

	if (NULL == exist_entry) {
		size += EXT2_EXTATTR_LEN(name_len);
		size += EXT2_EXTATTR_SIZE(new_size);
	}

	if (first_entry)
		for (entry = first_entry; !EXT2_IS_LAST_ENTRY(entry);
		    entry = EXT2_EXTATTR_NEXT(entry)) {
			if (entry != exist_entry)
				size += EXT2_EXTATTR_LEN(entry->e_name_len) +
				    EXT2_EXTATTR_SIZE(entry->e_value_size);
			else
				size += EXT2_EXTATTR_LEN(entry->e_name_len) +
				    EXT2_EXTATTR_SIZE(new_size);
		}

	return (size);
}

static void
ext2_extattr_set_exist_entry(char *off,
    struct ext2fs_extattr_entry *first_entry,
    struct ext2fs_extattr_entry *entry,
    char *end, struct uio *uio)
{
	uint16_t min_offs;

	min_offs = ext2_extattr_delete_value(off, first_entry, entry, end);

	entry->e_value_size = uio->uio_resid;
	if (entry->e_value_size)
		entry->e_value_offs = min_offs -
		    EXT2_EXTATTR_SIZE(uio->uio_resid);
	else
		entry->e_value_offs = 0;

	uiomove(off + entry->e_value_offs, entry->e_value_size, uio);
}

static struct ext2fs_extattr_entry *
ext2_extattr_set_new_entry(char *off, struct ext2fs_extattr_entry *first_entry,
    const char *name, int attrnamespace, char *end, struct uio *uio)
{
	int name_len;
	char *pad;
	uint16_t min_offs;
	struct ext2fs_extattr_entry *entry;
	struct ext2fs_extattr_entry *new_entry;

	/* Find pad's */
	min_offs = end - off;
	entry = first_entry;
	while (!EXT2_IS_LAST_ENTRY(entry)) {
		if (min_offs > entry->e_value_offs && entry->e_value_offs > 0)
			min_offs = entry->e_value_offs;

		entry = EXT2_EXTATTR_NEXT(entry);
	}

	pad = (char*)entry + sizeof(uint32_t);

	/* Find entry insert position */
	name_len = strlen(name);
	entry = first_entry;
	while (!EXT2_IS_LAST_ENTRY(entry)) {
		if (!(attrnamespace - entry->e_name_index) &&
		    !(name_len - entry->e_name_len))
			if (memcmp(name, entry->e_name, name_len) <= 0)
				break;

		entry = EXT2_EXTATTR_NEXT(entry);
	}

	/* Create new entry and insert it */
	new_entry = allocate_entry(name, attrnamespace, 0, uio->uio_resid, 0);
	memmove((char *)entry + EXT2_EXTATTR_LEN(new_entry->e_name_len), entry,
	    pad - (char*)entry);

	memcpy(entry, new_entry, EXT2_EXTATTR_LEN(new_entry->e_name_len));
	free_entry(new_entry);

	new_entry = entry;
	if (new_entry->e_value_size > 0)
		new_entry->e_value_offs = min_offs -
		    EXT2_EXTATTR_SIZE(new_entry->e_value_size);

	uiomove(off + new_entry->e_value_offs, new_entry->e_value_size, uio);

	return (new_entry);
}

int
ext2_extattr_inode_set(struct inode *ip, int attrnamespace,
    const char *name, struct uio *uio)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_dinode_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	size_t size = 0, max_size;
	int error;

	fs = ip->i_e2fs;

	if ((error = bread(ip->i_devvp,
	    fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
	    (int)fs->e2fs_bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return (error);
	}

	struct ext2fs_dinode *dinode = (struct ext2fs_dinode *)
	    ((char *)bp->b_data +
	    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ip->i_number));

	/* Check attributes magic value */
	header = (struct ext2fs_extattr_dinode_header *)((char *)dinode +
	    E2FS_REV0_INODE_SIZE + dinode->e2di_extra_isize);

	if (header->h_magic != EXTATTR_MAGIC) {
		brelse(bp);
		return (ENOSPC);
	}

	error = ext2_extattr_check(EXT2_IFIRST(header), (char *)dinode +
	    EXT2_INODE_SIZE(fs));
	if (error) {
		brelse(bp);
		return (error);
	}

	/* Find if entry exist */
	for (entry = EXT2_IFIRST(header); !EXT2_IS_LAST_ENTRY(entry);
	    entry = EXT2_EXTATTR_NEXT(entry)) {
		if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
		    attrnamespace)
			continue;

		name_len = entry->e_name_len;
		attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
		    entry->e_name, &name_len);
		if (!attr_name) {
			brelse(bp);
			return (ENOTSUP);
		}

		if (strlen(name) == name_len &&
		    0 == strncmp(attr_name, name, name_len))
			break;
	}

	max_size = EXT2_INODE_SIZE(fs) - E2FS_REV0_INODE_SIZE -
	    dinode->e2di_extra_isize;

	if (!EXT2_IS_LAST_ENTRY(entry)) {
		size = ext2_extattr_get_size(EXT2_IFIRST(header), entry,
		    sizeof(struct ext2fs_extattr_dinode_header),
		    entry->e_name_len, uio->uio_resid);
		if (size > max_size) {
			brelse(bp);
			return (ENOSPC);
		}

		ext2_extattr_set_exist_entry((char *)EXT2_IFIRST(header),
		    EXT2_IFIRST(header), entry, (char *)header + max_size, uio);
	} else {
		/* Ensure that the same entry does not exist in the block */
		if (ip->i_facl) {
			error = ext2_extattr_block_get(ip, attrnamespace, name,
			    NULL, &size);
			if (error != ENOATTR || size > 0) {
				brelse(bp);
				if (size > 0)
					error = ENOSPC;

				return (error);
			}
		}

		size = ext2_extattr_get_size(EXT2_IFIRST(header), NULL,
		    sizeof(struct ext2fs_extattr_dinode_header),
		    entry->e_name_len, uio->uio_resid);
		if (size > max_size) {
			brelse(bp);
			return (ENOSPC);
		}

		ext2_extattr_set_new_entry((char *)EXT2_IFIRST(header),
		    EXT2_IFIRST(header), name, attrnamespace,
		    (char *)header + max_size, uio);
	}

	return (bwrite(bp));
}

static void
ext2_extattr_hash_entry(struct ext2fs_extattr_header *header,
    struct ext2fs_extattr_entry *entry)
{
	uint32_t hash = 0;
	char *name = entry->e_name;
	int n;

	for (n=0; n < entry->e_name_len; n++) {
		hash = (hash << EXT2_EXTATTR_NAME_HASH_SHIFT) ^
		    (hash >> (8*sizeof(hash) - EXT2_EXTATTR_NAME_HASH_SHIFT)) ^
		    (*name++);
	}

	if (entry->e_value_block == 0 && entry->e_value_size != 0) {
		uint32_t *value = (uint32_t *)((char *)header + entry->e_value_offs);
		for (n = (entry->e_value_size +
		    EXT2_EXTATTR_ROUND) >> EXT2_EXTATTR_PAD_BITS; n; n--) {
			hash = (hash << EXT2_EXTATTR_VALUE_HASH_SHIFT) ^
			    (hash >> (8*sizeof(hash) - EXT2_EXTATTR_VALUE_HASH_SHIFT)) ^
			    (*value++);
		}
	}

	entry->e_hash = hash;
}

static void
ext2_extattr_rehash(struct ext2fs_extattr_header *header,
    struct ext2fs_extattr_entry *entry)
{
	struct ext2fs_extattr_entry *here;
	uint32_t hash = 0;

	ext2_extattr_hash_entry(header, entry);

	here = EXT2_ENTRY(header+1);
	while (!EXT2_IS_LAST_ENTRY(here)) {
		if (!here->e_hash) {
			/* Block is not shared if an entry's hash value == 0 */
			hash = 0;
			break;
		}

		hash = (hash << EXT2_EXTATTR_BLOCK_HASH_SHIFT) ^
		    (hash >> (8*sizeof(hash) - EXT2_EXTATTR_BLOCK_HASH_SHIFT)) ^
		    here->e_hash;

		here = EXT2_EXTATTR_NEXT(here);
	}

	header->h_hash = hash;
}

int
ext2_extattr_block_set(struct inode *ip, int attrnamespace,
    const char *name, struct uio *uio)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_header *header;
	struct ext2fs_extattr_entry *entry;
	const char *attr_name;
	int name_len;
	size_t size;
	int error;

	fs = ip->i_e2fs;

	if (ip->i_facl) {
		error = bread(ip->i_devvp, fsbtodb(fs, ip->i_facl),
		    fs->e2fs_bsize, NOCRED, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}

		/* Check attributes magic value */
		header = EXT2_HDR(bp);
		if (header->h_magic != EXTATTR_MAGIC || header->h_blocks != 1) {
			brelse(bp);
			return (EINVAL);
		}

		error = ext2_extattr_block_check(ip, bp);
		if (error) {
			brelse(bp);
			return (error);
		}

		if (header->h_refcount > 1) {
			error = ext2_extattr_block_clone(ip, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}

			header = EXT2_HDR(bp);
		}

		/* Find if entry exist */
		for (entry = EXT2_FIRST_ENTRY(bp); !EXT2_IS_LAST_ENTRY(entry);
		    entry = EXT2_EXTATTR_NEXT(entry)) {
			if (ext2_extattr_attrnamespace_to_bsd(entry->e_name_index) !=
			    attrnamespace)
				continue;

			name_len = entry->e_name_len;
			attr_name = ext2_extattr_name_to_bsd(entry->e_name_index,
			    entry->e_name, &name_len);
			if (!attr_name) {
				brelse(bp);
				return (ENOTSUP);
			}

			if (strlen(name) == name_len &&
			    0 == strncmp(attr_name, name, name_len))
				break;
		}

		if (!EXT2_IS_LAST_ENTRY(entry)) {
			size = ext2_extattr_get_size(EXT2_FIRST_ENTRY(bp), entry,
			    sizeof(struct ext2fs_extattr_header),
			    entry->e_name_len, uio->uio_resid);
			if (size > bp->b_bufsize) {
				brelse(bp);
				return (ENOSPC);
			}

			ext2_extattr_set_exist_entry(bp->b_data, EXT2_FIRST_ENTRY(bp),
			    entry, bp->b_data + bp->b_bufsize, uio);
		} else {
			size = ext2_extattr_get_size(EXT2_FIRST_ENTRY(bp), NULL,
			    sizeof(struct ext2fs_extattr_header),
			    strlen(name), uio->uio_resid);
			if (size > bp->b_bufsize) {
				brelse(bp);
				return (ENOSPC);
			}

			entry = ext2_extattr_set_new_entry(bp->b_data, EXT2_FIRST_ENTRY(bp),
			    name, attrnamespace, bp->b_data + bp->b_bufsize, uio);

			/* Clean the same entry in the inode */
			error = ext2_extattr_inode_delete(ip, attrnamespace, name);
			if (error && error != ENOATTR) {
				brelse(bp);
				return (error);
			}
		}

		ext2_extattr_rehash(header, entry);
		ext2_extattr_blk_csum_set(ip, bp);

		return (bwrite(bp));
	}

	size = ext2_extattr_get_size(NULL, NULL,
	    sizeof(struct ext2fs_extattr_header),
	    strlen(ext2_extattr_name_to_linux(attrnamespace, name)), uio->uio_resid);
	if (size > fs->e2fs_bsize)
		return (ENOSPC);

	/* Allocate block, fill EA header and insert entry */
	ip->i_facl = ext2_alloc_meta(ip);
	if (0 == ip->i_facl)
		return (ENOSPC);

	ip->i_blocks += btodb(fs->e2fs_bsize);
	ext2_update(ip->i_vnode, 1);

	bp = getblk(ip->i_devvp, fsbtodb(fs, ip->i_facl), fs->e2fs_bsize, 0, 0, 0);
	if (!bp) {
		ext2_blkfree(ip, ip->i_facl, fs->e2fs_bsize);
		ip->i_blocks -= btodb(fs->e2fs_bsize);
		ip->i_facl = 0;
		ext2_update(ip->i_vnode, 1);
		return (EIO);
	}

	header = EXT2_HDR(bp);
	header->h_magic = EXTATTR_MAGIC;
	header->h_refcount = 1;
	header->h_blocks = 1;
	header->h_hash = 0;
	memset(header->h_reserved, 0, sizeof(header->h_reserved));
	memcpy(bp->b_data, header, sizeof(struct ext2fs_extattr_header));
	memset(EXT2_FIRST_ENTRY(bp), 0, sizeof(uint32_t));

	entry = ext2_extattr_set_new_entry(bp->b_data, EXT2_FIRST_ENTRY(bp),
	    name, attrnamespace, bp->b_data + bp->b_bufsize, uio);

	/* Clean the same entry in the inode */
	error = ext2_extattr_inode_delete(ip, attrnamespace, name);
	if (error && error != ENOATTR) {
		brelse(bp);
		return (error);
	}

	ext2_extattr_rehash(header, entry);
	ext2_extattr_blk_csum_set(ip, bp);

	return (bwrite(bp));
}

int ext2_extattr_free(struct inode *ip)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct ext2fs_extattr_header *header;
	int error;

	fs = ip->i_e2fs;

	if (!ip->i_facl)
		return (0);

	error = bread(ip->i_devvp, fsbtodb(fs, ip->i_facl),
	    fs->e2fs_bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	/* Check attributes magic value */
	header = EXT2_HDR(bp);
	if (header->h_magic != EXTATTR_MAGIC || header->h_blocks != 1) {
		brelse(bp);
		return (EINVAL);
	}

	error = ext2_extattr_check(EXT2_FIRST_ENTRY(bp),
	    bp->b_data + bp->b_bufsize);
	if (error) {
		brelse(bp);
		return (error);
	}

	if (header->h_refcount > 1) {
		header->h_refcount--;
		bwrite(bp);
	} else {
		ext2_blkfree(ip, ip->i_facl, ip->i_e2fs->e2fs_bsize);
		brelse(bp);
	}

	ip->i_blocks -= btodb(ip->i_e2fs->e2fs_bsize);
	ip->i_facl = 0;
	ext2_update(ip->i_vnode, 1);

	return (0);
}
