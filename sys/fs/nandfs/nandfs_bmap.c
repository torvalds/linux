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
#include <sys/ktr.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vnode_pager.h>

#include <machine/_inttypes.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vnode_pager.h>

#include "nandfs_mount.h"
#include "nandfs.h"
#include "nandfs_subr.h"
#include "bmap.h"

nandfs_lbn_t
nandfs_get_maxfilesize(struct nandfs_device *fsdev)
{

	return (get_maxfilesize(fsdev));
}

int
nandfs_bmap_lookup(struct nandfs_node *node, nandfs_lbn_t lblk,
    nandfs_daddr_t *vblk)
{
	int error = 0;

	if (node->nn_ino == NANDFS_GC_INO && lblk >= 0)
		*vblk = lblk;
	else
		error = bmap_lookup(node, lblk, vblk);

	DPRINTF(TRANSLATE, ("%s: error %d ino %#jx lblocknr %#jx -> %#jx\n",
	    __func__, error, (uintmax_t)node->nn_ino, (uintmax_t)lblk,
	    (uintmax_t)*vblk));

	if (error)
		nandfs_error("%s: returned %d", __func__, error);

	return (error);
}

int
nandfs_bmap_insert_block(struct nandfs_node *node, nandfs_lbn_t lblk,
    struct buf *bp)
{
	struct nandfs_device *fsdev;
	nandfs_daddr_t vblk;
	int error;

	fsdev = node->nn_nandfsdev;

	vblk = 0;
	if (node->nn_ino != NANDFS_DAT_INO) {
		error = nandfs_vblock_alloc(fsdev, &vblk);
		if (error)
			return (error);
	}

	nandfs_buf_set(bp, NANDFS_VBLK_ASSIGNED);
	nandfs_vblk_set(bp, vblk);

	error = bmap_insert_block(node, lblk, vblk);
	if (error) {
		nandfs_vblock_free(fsdev, vblk);
		return (error);
	}

	return (0);
}

int
nandfs_bmap_dirty_blocks(struct nandfs_node *node, struct buf *bp, int force)
{
	int error;

	error = bmap_dirty_meta(node, bp->b_lblkno, force);
	if (error)
		nandfs_error("%s: cannot dirty buffer %p\n",
		    __func__, bp);

	return (error);
}

static int
nandfs_bmap_update_mapping(struct nandfs_node *node, nandfs_lbn_t lblk,
    nandfs_daddr_t blknr)
{
	int error;

	DPRINTF(BMAP,
	    ("%s: node: %p ino: %#jx lblk: %#jx vblk: %#jx\n",
	    __func__, node, (uintmax_t)node->nn_ino, (uintmax_t)lblk,
	    (uintmax_t)blknr));

	error = bmap_insert_block(node, lblk, blknr);

	return (error);
}

int
nandfs_bmap_update_block(struct nandfs_node *node, struct buf *bp,
    nandfs_lbn_t blknr)
{
	nandfs_lbn_t lblk;
	int error;

	lblk = bp->b_lblkno;
	nandfs_vblk_set(bp, blknr);

	DPRINTF(BMAP, ("%s: node: %p ino: %#jx bp: %p lblk: %#jx blk: %#jx\n",
	    __func__, node, (uintmax_t)node->nn_ino, bp,
	    (uintmax_t)lblk, (uintmax_t)blknr));

	error = nandfs_bmap_update_mapping(node, lblk, blknr);
	if (error) {
		nandfs_error("%s: cannot update lblk:%jx to blk:%jx for "
		    "node:%p, error:%d\n", __func__, (uintmax_t)lblk,
		    (uintmax_t)blknr, node, error);
		return (error);
	}

	return (error);
}

int
nandfs_bmap_update_dat(struct nandfs_node *node, nandfs_daddr_t oldblk,
    struct buf *bp)
{
	struct nandfs_device *fsdev;
	nandfs_daddr_t vblk = 0;
	int error;

	if (node->nn_ino == NANDFS_DAT_INO)
		return (0);

	if (nandfs_buf_check(bp, NANDFS_VBLK_ASSIGNED)) {
		nandfs_buf_clear(bp, NANDFS_VBLK_ASSIGNED);
		return (0);
	}

	fsdev = node->nn_nandfsdev;

	/* First alloc new virtual block.... */
	error = nandfs_vblock_alloc(fsdev, &vblk);
	if (error)
		return (error);

	error = nandfs_bmap_update_block(node, bp, vblk);
	if (error)
		return (error);

	/* Then we can end up with old one */
	nandfs_vblock_end(fsdev, oldblk);

	DPRINTF(BMAP,
	    ("%s: ino %#jx block %#jx: update vblk %#jx to %#jx\n",
	    __func__, (uintmax_t)node->nn_ino, (uintmax_t)bp->b_lblkno,
	    (uintmax_t)oldblk, (uintmax_t)vblk));
	return (error);
}

int
nandfs_bmap_truncate_mapping(struct nandfs_node *node, nandfs_lbn_t oblk,
    nandfs_lbn_t nblk)
{
	nandfs_lbn_t todo;
	int error;

	todo = oblk - nblk;

	DPRINTF(BMAP, ("%s: node %p oblk %jx nblk %jx truncate by %jx\n",
	    __func__, node, oblk, nblk, todo));

	error = bmap_truncate_mapping(node, oblk, todo);
	if (error)
		return (error);

	return (error);
}
