/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/bio.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>

#include <fs/nandfs/nandfs_mount.h>
#include <fs/nandfs/nandfs.h>
#include <fs/nandfs/nandfs_subr.h>

int
nandfs_node_create(struct nandfsmount *nmp, struct nandfs_node **node,
    uint16_t mode)
{
	struct nandfs_alloc_request req;
	struct nandfs_device *nandfsdev;
	struct nandfs_mdt *mdt;
	struct nandfs_node *ifile;
	struct nandfs_inode *inode;
	struct vnode *vp;
	uint32_t entry;
	int error = 0;

	nandfsdev = nmp->nm_nandfsdev;
	mdt = &nandfsdev->nd_ifile_mdt;
	ifile = nmp->nm_ifile_node;
	vp = NTOV(ifile);

	VOP_LOCK(vp, LK_EXCLUSIVE);
	/* Allocate new inode in ifile */
	req.entrynum = nandfsdev->nd_last_ino + 1;
	error = nandfs_find_free_entry(mdt, ifile, &req);
	if (error) {
		VOP_UNLOCK(vp, 0);
		return (error);
	}

	error = nandfs_get_entry_block(mdt, ifile, &req, &entry, 1);
	if (error) {
		VOP_UNLOCK(vp, 0);
		return (error);
	}

	/* Inode initialization */
	inode = ((struct nandfs_inode *) req.bp_entry->b_data) + entry;
	nandfs_inode_init(inode, mode);

	error = nandfs_alloc_entry(mdt, &req);
	if (error) {
		VOP_UNLOCK(vp, 0);
		return (error);
	}

	VOP_UNLOCK(vp, 0);

	nandfsdev->nd_last_ino = req.entrynum;
	error = nandfs_get_node(nmp, req.entrynum, node);
	DPRINTF(IFILE, ("%s: node: %p ino: %#jx\n",
	    __func__, node, (uintmax_t)((*node)->nn_ino)));

	return (error);
}

int
nandfs_node_destroy(struct nandfs_node *node)
{
	struct nandfs_alloc_request req;
	struct nandfsmount *nmp;
	struct nandfs_mdt *mdt;
	struct nandfs_node *ifile;
	struct vnode *vp;
	int error = 0;

	nmp = node->nn_nmp;
	req.entrynum = node->nn_ino;
	mdt = &nmp->nm_nandfsdev->nd_ifile_mdt;
	ifile = nmp->nm_ifile_node;
	vp = NTOV(ifile);

	DPRINTF(IFILE, ("%s: destroy node: %p ino: %#jx\n",
	    __func__, node, (uintmax_t)node->nn_ino));
	VOP_LOCK(vp, LK_EXCLUSIVE);

	error = nandfs_find_entry(mdt, ifile, &req);
	if (error) {
		nandfs_error("%s: finding entry error:%d node %p(%jx)",
		    __func__, error, node, node->nn_ino);
		VOP_UNLOCK(vp, 0);
		return (error);
	}

	nandfs_inode_destroy(&node->nn_inode);

	error = nandfs_free_entry(mdt, &req);
	if (error) {
		nandfs_error("%s: freing entry error:%d node %p(%jx)",
		    __func__, error, node, node->nn_ino);
		VOP_UNLOCK(vp, 0);
		return (error);
	}

	VOP_UNLOCK(vp, 0);
	DPRINTF(IFILE, ("%s: freed node %p ino %#jx\n",
	    __func__, node, (uintmax_t)node->nn_ino));
	return (error);
}

int
nandfs_node_update(struct nandfs_node *node)
{
	struct nandfs_alloc_request req;
	struct nandfsmount *nmp;
	struct nandfs_mdt *mdt;
	struct nandfs_node *ifile;
	struct nandfs_inode *inode;
	uint32_t index;
	int error = 0;

	nmp = node->nn_nmp;
	ifile = nmp->nm_ifile_node;
	ASSERT_VOP_LOCKED(NTOV(ifile), __func__);

	req.entrynum = node->nn_ino;
	mdt = &nmp->nm_nandfsdev->nd_ifile_mdt;

	DPRINTF(IFILE, ("%s: node:%p ino:%#jx\n",
	    __func__, &node->nn_inode, (uintmax_t)node->nn_ino));

	error = nandfs_get_entry_block(mdt, ifile, &req, &index, 0);
	if (error) {
		printf("nandfs_get_entry_block returned with ERROR=%d\n",
		    error);
		return (error);
	}

	inode = ((struct nandfs_inode *) req.bp_entry->b_data) + index;
	memcpy(inode, &node->nn_inode, sizeof(*inode));
	error = nandfs_dirty_buf(req.bp_entry, 0);

	return (error);
}

int
nandfs_get_node_entry(struct nandfsmount *nmp, struct nandfs_inode **inode,
    uint64_t ino, struct buf **bp)
{
	struct nandfs_alloc_request req;
	struct nandfs_mdt *mdt;
	struct nandfs_node *ifile;
	struct vnode *vp;
	uint32_t index;
	int error = 0;

	req.entrynum = ino;
	mdt = &nmp->nm_nandfsdev->nd_ifile_mdt;
	ifile = nmp->nm_ifile_node;
	vp = NTOV(ifile);

	VOP_LOCK(vp, LK_EXCLUSIVE);
	error = nandfs_get_entry_block(mdt, ifile, &req, &index, 0);
	if (error) {
		VOP_UNLOCK(vp, 0);
		return (error);
	}

	*inode = ((struct nandfs_inode *) req.bp_entry->b_data) + index;
	*bp = req.bp_entry;
	VOP_UNLOCK(vp, 0);
	return (0);
}

