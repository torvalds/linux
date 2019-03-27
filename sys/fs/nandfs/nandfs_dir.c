/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf
 * Copyright (c) 2008, 2009 Reinoud Zandijk
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * From: NetBSD: nilfs_subr.c,v 1.4 2009/07/29 17:06:57 reinoud
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/bio.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/signalvar.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/lockf.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include "nandfs_mount.h"
#include "nandfs.h"
#include "nandfs_subr.h"

int
nandfs_add_dirent(struct vnode *dvp, uint64_t ino, char *nameptr, long namelen,
    uint8_t type)
{
	struct nandfs_node *dir_node = VTON(dvp);
	struct nandfs_dir_entry *dirent, *pdirent;
	uint32_t blocksize = dir_node->nn_nandfsdev->nd_blocksize;
	uint64_t filesize = dir_node->nn_inode.i_size;
	uint64_t inode_blks = dir_node->nn_inode.i_blocks;
	uint32_t off, rest;
	uint8_t *pos;
	struct buf *bp;
	int error;

	pdirent = NULL;
	bp = NULL;
	if (inode_blks) {
		error = nandfs_bread(dir_node, inode_blks - 1, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}

		pos = bp->b_data;
		off = 0;
		while (off < blocksize) {
			pdirent = (struct nandfs_dir_entry *) (pos + off);
			if (!pdirent->rec_len) {
				pdirent = NULL;
				break;
			}
			off += pdirent->rec_len;
		}

		if (pdirent)
			rest = pdirent->rec_len -
			    NANDFS_DIR_REC_LEN(pdirent->name_len);
		else
			rest = blocksize;

		if (rest < NANDFS_DIR_REC_LEN(namelen)) {
			/* Do not update pdirent as new block is created */
			pdirent = NULL;
			brelse(bp);
			/* Set to NULL to create new */
			bp = NULL;
			filesize += rest;
		}
	}

	/* If no bp found create new */
	if (!bp) {
		error = nandfs_bcreate(dir_node, inode_blks, NOCRED, 0, &bp);
		if (error)
			return (error);
		off = 0;
		pos = bp->b_data;
	}

	/* Modify pdirent if exists */
	if (pdirent) {
		DPRINTF(LOOKUP, ("modify pdirent %p\n", pdirent));
		/* modify last de */
		off -= pdirent->rec_len;
		pdirent->rec_len =
		    NANDFS_DIR_REC_LEN(pdirent->name_len);
		off += pdirent->rec_len;
	}

	/* Create new dirent */
	dirent = (struct nandfs_dir_entry *) (pos + off);
	dirent->rec_len = blocksize - off;
	dirent->inode = ino;
	dirent->name_len = namelen;
	memset(dirent->name, 0, NANDFS_DIR_NAME_LEN(namelen));
	memcpy(dirent->name, nameptr, namelen);
	dirent->file_type = type;

	filesize += NANDFS_DIR_REC_LEN(dirent->name_len);

	DPRINTF(LOOKUP, ("create dir_entry '%.*s' at %p with size %x "
	    "new filesize: %jx\n",
	    (int)namelen, dirent->name, dirent, dirent->rec_len,
	    (uintmax_t)filesize));

	error = nandfs_dirty_buf(bp, 0);
	if (error)
		return (error);

	dir_node->nn_inode.i_size = filesize;
	dir_node->nn_flags |= IN_CHANGE | IN_UPDATE;
	vnode_pager_setsize(dvp, filesize);

	return (0);
}

int
nandfs_remove_dirent(struct vnode *dvp, struct nandfs_node *node,
    struct componentname *cnp)
{
	struct nandfs_node *dir_node;
	struct nandfs_dir_entry *dirent, *pdirent;
	struct buf *bp;
	uint64_t filesize, blocknr, ino, offset;
	uint32_t blocksize, limit, off;
	uint16_t newsize;
	uint8_t *pos;
	int error, found;

	dir_node = VTON(dvp);
	filesize = dir_node->nn_inode.i_size;
	if (!filesize)
		return (0);

	if (node) {
		offset = node->nn_diroff;
		ino = node->nn_ino;
	} else {
		offset = dir_node->nn_diroff;
		ino = NANDFS_WHT_INO;
	}

	dirent = pdirent = NULL;
	blocksize = dir_node->nn_nandfsdev->nd_blocksize;
	blocknr = offset / blocksize;

	DPRINTF(LOOKUP, ("rm direntry dvp %p node %p ino %#jx at off %#jx\n",
	    dvp, node, (uintmax_t)ino, (uintmax_t)offset));

	error = nandfs_bread(dir_node, blocknr, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	pos = bp->b_data;
	off = 0;
	found = 0;
	limit = offset % blocksize;
	pdirent = (struct nandfs_dir_entry *) bp->b_data;
	while (off <= limit) {
		dirent = (struct nandfs_dir_entry *) (pos + off);

		if ((off == limit) &&
		    (dirent->inode == ino)) {
			found = 1;
			break;
		}
		if (dirent->inode != 0)
			pdirent = dirent;
		off += dirent->rec_len;
	}

	if (!found) {
		nandfs_error("cannot find entry to remove");
		brelse(bp);
		return (error);
	}
	DPRINTF(LOOKUP,
	    ("rm dirent ino %#jx at %#x with size %#x\n",
	    (uintmax_t)dirent->inode, off, dirent->rec_len));

	newsize = (uintptr_t)dirent - (uintptr_t)pdirent;
	newsize += dirent->rec_len;
	pdirent->rec_len = newsize;
	dirent->inode = 0;
	error = nandfs_dirty_buf(bp, 0);
	if (error)
		return (error);

	dir_node->nn_flags |= IN_CHANGE | IN_UPDATE;
	/* If last one modify filesize */
	if ((offset + NANDFS_DIR_REC_LEN(dirent->name_len)) == filesize) {
		filesize = blocknr * blocksize +
		    ((uintptr_t)pdirent - (uintptr_t)pos) +
		    NANDFS_DIR_REC_LEN(pdirent->name_len);
		dir_node->nn_inode.i_size = filesize;
	}

	return (0);
}

int
nandfs_update_parent_dir(struct vnode *dvp, uint64_t newparent)
{
	struct nandfs_dir_entry *dirent;
	struct nandfs_node *dir_node;
	struct buf *bp;
	int error;

	dir_node = VTON(dvp);
	error = nandfs_bread(dir_node, 0, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	dirent = (struct nandfs_dir_entry *)bp->b_data;
	dirent->inode = newparent;
	error = nandfs_dirty_buf(bp, 0);
	if (error)
		return (error);

	return (0);
}

int
nandfs_update_dirent(struct vnode *dvp, struct nandfs_node *fnode,
    struct nandfs_node *tnode)
{
	struct nandfs_node *dir_node;
	struct nandfs_dir_entry *dirent;
	struct buf *bp;
	uint64_t file_size, blocknr;
	uint32_t blocksize, off;
	uint8_t *pos;
	int error;

	dir_node = VTON(dvp);
	file_size = dir_node->nn_inode.i_size;
	if (!file_size)
		return (0);

	DPRINTF(LOOKUP,
	    ("chg direntry dvp %p ino %#jx  to in %#jx at off %#jx\n",
	    dvp, (uintmax_t)tnode->nn_ino, (uintmax_t)fnode->nn_ino,
	    (uintmax_t)tnode->nn_diroff));

	blocksize = dir_node->nn_nandfsdev->nd_blocksize;
	blocknr = tnode->nn_diroff / blocksize;
	off = tnode->nn_diroff % blocksize;
	error = nandfs_bread(dir_node, blocknr, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	pos = bp->b_data;
	dirent = (struct nandfs_dir_entry *) (pos + off);
	KASSERT((dirent->inode == tnode->nn_ino),
	    ("direntry mismatch"));

	dirent->inode = fnode->nn_ino;
	error = nandfs_dirty_buf(bp, 0);
	if (error)
		return (error);

	return (0);
}

int
nandfs_init_dir(struct vnode *dvp, uint64_t ino, uint64_t parent_ino)
{

	if (nandfs_add_dirent(dvp, parent_ino, "..", 2, DT_DIR) ||
	    nandfs_add_dirent(dvp, ino, ".", 1, DT_DIR)) {
		nandfs_error("%s: cannot initialize dir ino:%jd(pino:%jd)\n",
		    __func__, ino, parent_ino);
		return (-1);
	}
	return (0);
}
