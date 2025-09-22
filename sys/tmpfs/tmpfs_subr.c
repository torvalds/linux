/*	$OpenBSD: tmpfs_subr.c,v 1.27 2024/09/12 09:04:51 claudio Exp $	*/
/*	$NetBSD: tmpfs_subr.c,v 1.79 2012/03/13 18:40:50 elad Exp $	*/

/*
 * Copyright (c) 2005-2011 The NetBSD Foundation, Inc.
 * Copyright (c) 2013 Pedro Martelletto
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program, and by Mindaugas Rasiukevicius.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Efficient memory file system: interfaces for inode and directory entry
 * construction, destruction and manipulation.
 *
 * Reference counting
 *
 *	The link count of inode (tmpfs_node_t::tn_links) is used as a
 *	reference counter.  However, it has slightly different semantics.
 *
 *	For directories - link count represents directory entries, which
 *	refer to the directories.  In other words, it represents the count
 *	of sub-directories.  It also takes into account the virtual '.'
 *	entry (which has no real entry in the list).  For files - link count
 *	represents the hard links.  Since only empty directories can be
 *	removed - link count aligns the reference counting requirements
 *	enough.  Note: to check whether directory is not empty, the inode
 *	size (tmpfs_node_t::tn_size) can be used.
 *
 *	The inode itself, as an object, gathers its first reference when
 *	directory entry is attached via tmpfs_dir_attach(9).  For instance,
 *	after regular tmpfs_create(), a file would have a link count of 1,
 *	while directory after tmpfs_mkdir() would have 2 (due to '.').
 *
 * Reclamation
 *
 *	It should be noted that tmpfs inodes rely on a combination of vnode
 *	reference counting and link counting.  That is, an inode can only be
 *	destroyed if its associated vnode is inactive.  The destruction is
 *	done on vnode reclamation i.e. tmpfs_reclaim().  It should be noted
 *	that tmpfs_node_t::tn_links being 0 is a destruction criterion.
 *
 *	If an inode has references within the file system (tn_links > 0) and
 *	its inactive vnode gets reclaimed/recycled - then the association is
 *	broken in tmpfs_reclaim().  In such case, an inode will always pass
 *	tmpfs_lookup() and thus tmpfs_vnode_get() to associate a new vnode.
 *
 * Lock order
 *
 *	tmpfs_node_t::tn_nlock ->
 *		struct vnode::v_vlock ->
 *			struct vnode::v_interlock
 */

#include <sys/param.h>
#include <sys/dirent.h>
#include <sys/event.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/vnode.h>

#include <uvm/uvm_aobj.h>

#include <tmpfs/tmpfs.h>
#include <tmpfs/tmpfs_vnops.h>


/* Local functions. */
void	tmpfs_dir_putseq(tmpfs_node_t *, tmpfs_dirent_t *);
int	tmpfs_dir_getdotents(tmpfs_node_t *, struct dirent *, struct uio *);

/*
 * tmpfs_alloc_node: allocate a new inode of a specified type and
 * insert it into the list of specified mount point.
 */
int
tmpfs_alloc_node(tmpfs_mount_t *tmp, enum vtype type, uid_t uid, gid_t gid,
    mode_t mode, char *target, dev_t rdev, tmpfs_node_t **node)
{
	tmpfs_node_t *nnode;
	struct uvm_object *uobj;

	nnode = tmpfs_node_get(tmp);
	if (nnode == NULL) {
		return ENOSPC;
	}

	/* Initially, no references and no associations. */
	nnode->tn_links = 0;
	nnode->tn_vnode = NULL;
	nnode->tn_dirent_hint = NULL;

	rw_enter_write(&tmp->tm_acc_lock);
	nnode->tn_id = ++tmp->tm_highest_inode;
	if (nnode->tn_id == 0) {
		--tmp->tm_highest_inode;
		rw_exit_write(&tmp->tm_acc_lock);
		tmpfs_node_put(tmp, nnode);
		return ENOSPC;
	}
	 rw_exit_write(&tmp->tm_acc_lock);

	/* Generic initialization. */
	nnode->tn_type = type;
	nnode->tn_size = 0;
	nnode->tn_flags = 0;
	nnode->tn_lockf = NULL;
	nnode->tn_gen = TMPFS_NODE_GEN_MASK & arc4random();

	nanotime(&nnode->tn_atime);
	nnode->tn_birthtime = nnode->tn_atime;
	nnode->tn_ctime = nnode->tn_atime;
	nnode->tn_mtime = nnode->tn_atime;

	KASSERT(uid != VNOVAL && gid != VNOVAL && mode != VNOVAL);

	nnode->tn_uid = uid;
	nnode->tn_gid = gid;
	nnode->tn_mode = mode;

	/* Type-specific initialization. */
	switch (nnode->tn_type) {
	case VBLK:
	case VCHR:
		/* Character/block special device. */
		KASSERT(rdev != VNOVAL);
		nnode->tn_spec.tn_dev.tn_rdev = rdev;
		break;
	case VDIR:
		/* Directory. */
		TAILQ_INIT(&nnode->tn_spec.tn_dir.tn_dir);
		nnode->tn_spec.tn_dir.tn_parent = NULL;
		nnode->tn_spec.tn_dir.tn_next_seq = TMPFS_DIRSEQ_START;
		nnode->tn_spec.tn_dir.tn_readdir_lastp = NULL;

		/* Extra link count for the virtual '.' entry. */
		nnode->tn_links++;
		break;
	case VFIFO:
	case VSOCK:
		break;
	case VLNK:
		/* Symbolic link.  Target specifies the file name. */
		KASSERT(target && strlen(target) < MAXPATHLEN);

		nnode->tn_size = strlen(target);
		if (nnode->tn_size == 0) {
			nnode->tn_spec.tn_lnk.tn_link = NULL;
			break;
		}
		nnode->tn_spec.tn_lnk.tn_link =
		    tmpfs_strname_alloc(tmp, nnode->tn_size);
		if (nnode->tn_spec.tn_lnk.tn_link == NULL) {
			tmpfs_node_put(tmp, nnode);
			return ENOSPC;
		}
		memcpy(nnode->tn_spec.tn_lnk.tn_link, target, nnode->tn_size);
		break;
	case VREG:
		/* Regular file.  Create an underlying UVM object. */
		uobj = uao_create(0, UAO_FLAG_CANFAIL);
		if (uobj == NULL) {
			tmpfs_node_put(tmp, nnode);
			return ENOSPC;
		}
		nnode->tn_spec.tn_reg.tn_aobj = uobj;
		nnode->tn_spec.tn_reg.tn_aobj_pages = 0;
		nnode->tn_spec.tn_reg.tn_aobj_pgptr = (vaddr_t)NULL;
		nnode->tn_spec.tn_reg.tn_aobj_pgnum = (voff_t)-1;
		break;
	default:
		KASSERT(0);
	}

	rw_init(&nnode->tn_nlock, "tvlk");

	rw_enter_write(&tmp->tm_lock);
	LIST_INSERT_HEAD(&tmp->tm_nodes, nnode, tn_entries);
	rw_exit_write(&tmp->tm_lock);

	*node = nnode;
	return 0;
}

/*
 * tmpfs_free_node: remove the inode from a list in the mount point and
 * destroy the inode structures.
 */
void
tmpfs_free_node(tmpfs_mount_t *tmp, tmpfs_node_t *node)
{
	size_t objsz;

	rw_enter_write(&tmp->tm_lock);
	LIST_REMOVE(node, tn_entries);
	rw_exit_write(&tmp->tm_lock);

	switch (node->tn_type) {
	case VLNK:
		if (node->tn_size > 0) {
			KASSERT(node->tn_size <= SIZE_MAX);
			tmpfs_strname_free(tmp, node->tn_spec.tn_lnk.tn_link,
			    node->tn_size);
		}
		break;
	case VREG:
		/*
		 * Calculate the size of inode data, decrease the used-memory
		 * counter, and destroy the underlying UVM object (if any).
		 */
		objsz = PAGE_SIZE * node->tn_spec.tn_reg.tn_aobj_pages;
		if (objsz != 0) {
			tmpfs_mem_decr(tmp, objsz);
		}
		if (node->tn_spec.tn_reg.tn_aobj != NULL) {
			uao_detach(node->tn_spec.tn_reg.tn_aobj);
			node->tn_spec.tn_reg.tn_aobj = NULL;
		}
		break;
	case VDIR:
		KASSERT(TAILQ_EMPTY(&node->tn_spec.tn_dir.tn_dir));
		KASSERT(node->tn_spec.tn_dir.tn_parent == NULL ||
		    node == tmp->tm_root);
		break;
	default:
		break;
	}

	rw_enter_write(&tmp->tm_acc_lock);
	if (node->tn_id == tmp->tm_highest_inode)
		--tmp->tm_highest_inode;
	rw_exit_write(&tmp->tm_acc_lock);

	/* mutex_destroy(&node->tn_nlock); */
	tmpfs_node_put(tmp, node);
}

/*
 * tmpfs_vnode_get: allocate or reclaim a vnode for a specified inode.
 *
 * => Must be called with tmpfs_node_t::tn_nlock held.
 * => Returns vnode (*vpp) locked.
 */
int
tmpfs_vnode_get(struct mount *mp, tmpfs_node_t *node, struct vnode **vpp)
{
	struct vnode *vp, *nvp;
	/* kmutex_t *slock; */
	int error;
again:
	/* If there is already a vnode, try to reclaim it. */
	if ((vp = node->tn_vnode) != NULL) {
		/* atomic_or_ulong(&node->tn_gen, TMPFS_RECLAIMING_BIT); */
		node->tn_gen |= TMPFS_RECLAIMING_BIT;
		rw_exit_write(&node->tn_nlock);
		error = vget(vp, LK_EXCLUSIVE);
		if (error == ENOENT) {
			rw_enter_write(&node->tn_nlock);
			goto again;
		}
		/* atomic_and_ulong(&node->tn_gen, ~TMPFS_RECLAIMING_BIT); */
		node->tn_gen &= ~TMPFS_RECLAIMING_BIT;
		*vpp = vp;
		return error;
	}
	if (TMPFS_NODE_RECLAIMING(node)) {
		/* atomic_and_ulong(&node->tn_gen, ~TMPFS_RECLAIMING_BIT); */
		node->tn_gen &= ~TMPFS_RECLAIMING_BIT;
	}

	/*
	 * Get a new vnode and associate it with our inode.  Share the
	 * lock with underlying UVM object, if there is one (VREG case).
	 */
#if 0
	if (node->tn_type == VREG) {
		struct uvm_object *uobj = node->tn_spec.tn_reg.tn_aobj;
		slock = uobj->vmobjlock;
	} else {
		slock = NULL;
	}
#endif
	error = getnewvnode(VT_TMPFS, mp, &tmpfs_vops, &vp);
	if (error) {
		rw_exit_write(&node->tn_nlock);
		return error;
	}

	rrw_init_flags(&node->tn_vlock, "tnode", RWL_DUPOK | RWL_IS_VNODE);
	vp->v_type = node->tn_type;

	/* Type-specific initialization. */
	switch (node->tn_type) {
	case VBLK:
	case VCHR:
		vp->v_op = &tmpfs_specvops;
		if ((nvp = checkalias(vp, node->tn_spec.tn_dev.tn_rdev, mp))) {
			nvp->v_data = vp->v_data;
			vp->v_data = NULL;
			vp->v_op = &spec_vops;
			vrele(vp);
			vgone(vp);
			vp = nvp;
			node->tn_vnode = vp;
		}
		break;
	case VDIR:
		vp->v_flag |= node->tn_spec.tn_dir.tn_parent == node ?
		    VROOT : 0;
		break;
#ifdef FIFO
	case VFIFO:
		vp->v_op = &tmpfs_fifovops;
		break;
#endif
	case VLNK:
	case VREG:
	case VSOCK:
		break;
	default:
		KASSERT(0);
	}

	uvm_vnp_setsize(vp, node->tn_size);
	vp->v_data = node;
	node->tn_vnode = vp;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	rw_exit_write(&node->tn_nlock);

	KASSERT(VOP_ISLOCKED(vp));
	*vpp = vp;
	return 0;
}

/*
 * tmpfs_alloc_file: allocate a new file of specified type and adds it
 * into the parent directory.
 *
 * => Credentials of the caller are used.
 */
int
tmpfs_alloc_file(struct vnode *dvp, struct vnode **vpp, struct vattr *vap,
    struct componentname *cnp, char *target)
{
	tmpfs_mount_t *tmp = VFS_TO_TMPFS(dvp->v_mount);
	tmpfs_node_t *dnode = VP_TO_TMPFS_DIR(dvp), *node;
	tmpfs_dirent_t *de;
	int error;

	KASSERT(VOP_ISLOCKED(dvp));
	*vpp = NULL;

	/* Check for the maximum number of links limit. */
	if (vap->va_type == VDIR) {
		/* Check for maximum links limit. */
		if (dnode->tn_links == LINK_MAX) {
			error = EMLINK;
			goto out;
		}
		KASSERT(dnode->tn_links < LINK_MAX);
	}

	if (TMPFS_DIRSEQ_FULL(dnode)) {
		error = ENOSPC;
		goto out;
	}

	if (dnode->tn_links == 0) {
		error = ENOENT;
		goto out;
	}

	/* Allocate a node that represents the new file. */
	error = tmpfs_alloc_node(tmp, vap->va_type, cnp->cn_cred->cr_uid,
	    dnode->tn_gid, vap->va_mode, target, vap->va_rdev, &node);
	if (error)
		goto out;

	/* Allocate a directory entry that points to the new file. */
	error = tmpfs_alloc_dirent(tmp, cnp->cn_nameptr, cnp->cn_namelen, &de);
	if (error) {
		tmpfs_free_node(tmp, node);
		goto out;
	}

	/* Get a vnode for the new file. */
	rw_enter_write(&node->tn_nlock);
	error = tmpfs_vnode_get(dvp->v_mount, node, vpp);
	if (error) {
		tmpfs_free_dirent(tmp, de);
		tmpfs_free_node(tmp, node);
		goto out;
	}

	/* Associate inode and attach the entry into the directory. */
	tmpfs_dir_attach(dnode, de, node);

out:
	if (error == 0 && (cnp->cn_flags & SAVESTART) == 0)
		pool_put(&namei_pool, cnp->cn_pnbuf);
	return error;
}

/*
 * tmpfs_alloc_dirent: allocates a new directory entry for the inode.
 * The directory entry contains a path name component.
 */
int
tmpfs_alloc_dirent(tmpfs_mount_t *tmp, const char *name, uint16_t len,
    tmpfs_dirent_t **de)
{
	tmpfs_dirent_t *nde;

	nde = tmpfs_dirent_get(tmp);
	if (nde == NULL)
		return ENOSPC;

	nde->td_name = tmpfs_strname_alloc(tmp, len);
	if (nde->td_name == NULL) {
		tmpfs_dirent_put(tmp, nde);
		return ENOSPC;
	}
	nde->td_namelen = len;
	memcpy(nde->td_name, name, len);
	nde->td_seq = TMPFS_DIRSEQ_NONE;

	*de = nde;
	return 0;
}

/*
 * tmpfs_free_dirent: free a directory entry.
 */
void
tmpfs_free_dirent(tmpfs_mount_t *tmp, tmpfs_dirent_t *de)
{
	KASSERT(de->td_node == NULL);
	KASSERT(de->td_seq == TMPFS_DIRSEQ_NONE);
	tmpfs_strname_free(tmp, de->td_name, de->td_namelen);
	tmpfs_dirent_put(tmp, de);
}

/*
 * tmpfs_dir_attach: associate directory entry with a specified inode,
 * and attach the entry into the directory, specified by vnode.
 *
 * => Increases link count on the associated node.
 * => Increases link count on directory node, if our node is VDIR.
 *    It is caller's responsibility to check for the LINK_MAX limit.
 * => Triggers kqueue events here.
 */
void
tmpfs_dir_attach(tmpfs_node_t *dnode, tmpfs_dirent_t *de, tmpfs_node_t *node)
{
	struct vnode *dvp = dnode->tn_vnode;
	int events = NOTE_WRITE;

	KASSERT(dvp != NULL);
	KASSERT(VOP_ISLOCKED(dvp));

	/* Get a new sequence number. */
	KASSERT(de->td_seq == TMPFS_DIRSEQ_NONE);
	de->td_seq = tmpfs_dir_getseq(dnode, de);

	/* Associate directory entry and the inode. */
	de->td_node = node;
	KASSERT(node->tn_links < LINK_MAX);
	node->tn_links++;

	/* Save the hint (might overwrite). */
	node->tn_dirent_hint = de;

	/* Insert the entry to the directory (parent of inode). */
	TAILQ_INSERT_TAIL(&dnode->tn_spec.tn_dir.tn_dir, de, td_entries);
	dnode->tn_size += sizeof(tmpfs_dirent_t);
	tmpfs_update(dnode, TMPFS_NODE_STATUSALL);
	uvm_vnp_setsize(dvp, dnode->tn_size);

	if (node->tn_type == VDIR) {
		/* Set parent. */
		KASSERT(node->tn_spec.tn_dir.tn_parent == NULL);
		node->tn_spec.tn_dir.tn_parent = dnode;

		/* Increase the link count of parent. */
		KASSERT(dnode->tn_links < LINK_MAX);
		dnode->tn_links++;
		events |= NOTE_LINK;

		TMPFS_VALIDATE_DIR(node);
	}
	VN_KNOTE(dvp, events);
}

/*
 * tmpfs_dir_detach: disassociate directory entry and its inode,
 * and detach the entry from the directory, specified by vnode.
 *
 * => Decreases link count on the associated node.
 * => Decreases the link count on directory node, if our node is VDIR.
 * => Triggers kqueue events here.
 */
void
tmpfs_dir_detach(tmpfs_node_t *dnode, tmpfs_dirent_t *de)
{
	tmpfs_node_t *node = de->td_node;
	struct vnode *vp, *dvp = dnode->tn_vnode;
	int events = NOTE_WRITE;

	KASSERT(dvp == NULL || VOP_ISLOCKED(dvp));

	/* Deassociate the inode and entry. */
	de->td_node = NULL;
	node->tn_dirent_hint = NULL;

	KASSERT(node->tn_links > 0);
	node->tn_links--;
	if ((vp = node->tn_vnode) != NULL) {
		KASSERT(VOP_ISLOCKED(vp));
		VN_KNOTE(vp, node->tn_links ?  NOTE_LINK : NOTE_DELETE);
	}

	/* If directory - decrease the link count of parent. */
	if (node->tn_type == VDIR) {
		KASSERT(node->tn_spec.tn_dir.tn_parent == dnode);
		node->tn_spec.tn_dir.tn_parent = NULL;

		KASSERT(dnode->tn_links > 0);
		dnode->tn_links--;
		events |= NOTE_LINK;
	}

	/* Remove the entry from the directory. */
	if (dnode->tn_spec.tn_dir.tn_readdir_lastp == de) {
		dnode->tn_spec.tn_dir.tn_readdir_lastp = NULL;
	}
	TAILQ_REMOVE(&dnode->tn_spec.tn_dir.tn_dir, de, td_entries);

	dnode->tn_size -= sizeof(tmpfs_dirent_t);
	tmpfs_update(dnode, TMPFS_NODE_MODIFIED | TMPFS_NODE_CHANGED);
	tmpfs_dir_putseq(dnode, de);
	if (dvp) {
		tmpfs_update(dnode, 0);
		uvm_vnp_setsize(dvp, dnode->tn_size);
		VN_KNOTE(dvp, events);
	}
}

/*
 * tmpfs_dir_lookup: find a directory entry in the specified inode.
 *
 * Note that the . and .. components are not allowed as they do not
 * physically exist within directories.
 */
tmpfs_dirent_t *
tmpfs_dir_lookup(tmpfs_node_t *node, struct componentname *cnp)
{
	const char *name = cnp->cn_nameptr;
	const uint16_t nlen = cnp->cn_namelen;
	tmpfs_dirent_t *de;

	KASSERT(VOP_ISLOCKED(node->tn_vnode));
	KASSERT(nlen != 1 || !(name[0] == '.'));
	KASSERT(nlen != 2 || !(name[0] == '.' && name[1] == '.'));
	TMPFS_VALIDATE_DIR(node);

	TAILQ_FOREACH(de, &node->tn_spec.tn_dir.tn_dir, td_entries) {
		if (de->td_namelen != nlen)
			continue;
		if (memcmp(de->td_name, name, nlen) != 0)
			continue;
		break;
	}
	tmpfs_update(node, TMPFS_NODE_ACCESSED);
	return de;
}

/*
 * tmpfs_dir_cached: get a cached directory entry if it is valid.  Used to
 * avoid unnecessary tmpfs_dir_lookup().
 *
 * => The vnode must be locked.
 */
tmpfs_dirent_t *
tmpfs_dir_cached(tmpfs_node_t *node)
{
	tmpfs_dirent_t *de = node->tn_dirent_hint;

	KASSERT(VOP_ISLOCKED(node->tn_vnode));

	if (de == NULL) {
		return NULL;
	}
	KASSERT(de->td_node == node);

	/*
	 * Directories always have a valid hint.  For files, check if there
	 * are any hard links.  If there are - hint might be invalid.
	 */
	return (node->tn_type != VDIR && node->tn_links > 1) ? NULL : de;
}

/*
 * tmpfs_dir_getseq: get a per-directory sequence number for the entry.
 */
uint64_t
tmpfs_dir_getseq(tmpfs_node_t *dnode, tmpfs_dirent_t *de)
{
	uint64_t seq = de->td_seq;

	TMPFS_VALIDATE_DIR(dnode);

	if (__predict_true(seq != TMPFS_DIRSEQ_NONE)) {
		/* Already set. */
		KASSERT(seq >= TMPFS_DIRSEQ_START);
		return seq;
	}

	/*
	 * The "." and ".." and the end-of-directory have reserved numbers.
	 * The other sequence numbers are allocated incrementally.
	 */

	seq = dnode->tn_spec.tn_dir.tn_next_seq;
	KASSERT(seq >= TMPFS_DIRSEQ_START);
	KASSERT(seq < TMPFS_DIRSEQ_END);
	dnode->tn_spec.tn_dir.tn_next_seq++;
	return seq;
}

void
tmpfs_dir_putseq(tmpfs_node_t *dnode, tmpfs_dirent_t *de)
{
	uint64_t seq = de->td_seq;

	TMPFS_VALIDATE_DIR(dnode);
	KASSERT(seq == TMPFS_DIRSEQ_NONE || seq >= TMPFS_DIRSEQ_START);
	KASSERT(seq == TMPFS_DIRSEQ_NONE || seq < TMPFS_DIRSEQ_END);

	de->td_seq = TMPFS_DIRSEQ_NONE;

	/* Empty?  We can reset. */
	if (dnode->tn_size == 0) {
		dnode->tn_spec.tn_dir.tn_next_seq = TMPFS_DIRSEQ_START;
	} else if (seq != TMPFS_DIRSEQ_NONE &&
	    seq == dnode->tn_spec.tn_dir.tn_next_seq - 1) {
		dnode->tn_spec.tn_dir.tn_next_seq--;
	}
}

/*
 * tmpfs_dir_lookupbyseq: lookup a directory entry by the sequence number.
 */
tmpfs_dirent_t *
tmpfs_dir_lookupbyseq(tmpfs_node_t *node, off_t seq)
{
	tmpfs_dirent_t *de = node->tn_spec.tn_dir.tn_readdir_lastp;

	TMPFS_VALIDATE_DIR(node);

	/*
	 * First, check the cache.  If does not match - perform a lookup.
	 */
	if (de && de->td_seq == seq) {
		KASSERT(de->td_seq >= TMPFS_DIRSEQ_START);
		KASSERT(de->td_seq != TMPFS_DIRSEQ_NONE);
		return de;
	}
	TAILQ_FOREACH(de, &node->tn_spec.tn_dir.tn_dir, td_entries) {
		KASSERT(de->td_seq >= TMPFS_DIRSEQ_START);
		KASSERT(de->td_seq != TMPFS_DIRSEQ_NONE);
		if (de->td_seq == seq)
			return de;
	}
	return NULL;
}

/*
 * tmpfs_dir_getdotents: helper function for tmpfs_readdir() to get the
 * dot meta entries, that is, "." or "..".  Copy it to the UIO space.
 */
int
tmpfs_dir_getdotents(tmpfs_node_t *node, struct dirent *dp, struct uio *uio)
{
	tmpfs_dirent_t *de;
	off_t next = 0;
	int error;

	switch (uio->uio_offset) {
	case TMPFS_DIRSEQ_DOT:
		dp->d_fileno = node->tn_id;
		strlcpy(dp->d_name, ".", sizeof(dp->d_name));
		next = TMPFS_DIRSEQ_DOTDOT;
		break;
	case TMPFS_DIRSEQ_DOTDOT:
		dp->d_fileno = node->tn_spec.tn_dir.tn_parent->tn_id;
		strlcpy(dp->d_name, "..", sizeof(dp->d_name));
		de = TAILQ_FIRST(&node->tn_spec.tn_dir.tn_dir);
		next = de ? tmpfs_dir_getseq(node, de) : TMPFS_DIRSEQ_EOF;
		break;
	default:
		KASSERT(false);
	}
	dp->d_type = DT_DIR;
	dp->d_namlen = strlen(dp->d_name);
	dp->d_reclen = DIRENT_SIZE(dp);
	dp->d_off = next;

	if (dp->d_reclen > uio->uio_resid) {
		return EJUSTRETURN;
	}

	if ((error = uiomove(dp, dp->d_reclen, uio)) != 0) {
		return error;
	}

	uio->uio_offset = next;
	return error;
}

/*
 * tmpfs_dir_getdents: helper function for tmpfs_readdir.
 *
 * => Returns as much directory entries as can fit in the uio space.
 * => The read starts at uio->uio_offset.
 */
int
tmpfs_dir_getdents(tmpfs_node_t *node, struct uio *uio)
{
	tmpfs_dirent_t *de, *next_de;
	struct dirent dent;
	int error = 0;

	KASSERT(VOP_ISLOCKED(node->tn_vnode));
	TMPFS_VALIDATE_DIR(node);
	memset(&dent, 0, sizeof(dent));

	if (uio->uio_offset == TMPFS_DIRSEQ_DOT) {
		if ((error = tmpfs_dir_getdotents(node, &dent, uio)) != 0) {
			goto done;
		}
	}
	if (uio->uio_offset == TMPFS_DIRSEQ_DOTDOT) {
		if ((error = tmpfs_dir_getdotents(node, &dent, uio)) != 0) {
			goto done;
		}
	}
	/* Done if we reached the end. */
	if (uio->uio_offset == TMPFS_DIRSEQ_EOF) {
		goto done;
	}

	/* Locate the directory entry given by the given sequence number. */
	de = tmpfs_dir_lookupbyseq(node, uio->uio_offset);
	if (de == NULL) {
		error = EINVAL;
		goto done;
	}

	/*
	 * Read as many entries as possible; i.e., until we reach the end
	 * of the directory or we exhaust UIO space.
	 */
	do {
		dent.d_fileno = de->td_node->tn_id;
		switch (de->td_node->tn_type) {
		case VBLK:
			dent.d_type = DT_BLK;
			break;
		case VCHR:
			dent.d_type = DT_CHR;
			break;
		case VDIR:
			dent.d_type = DT_DIR;
			break;
		case VFIFO:
			dent.d_type = DT_FIFO;
			break;
		case VLNK:
			dent.d_type = DT_LNK;
			break;
		case VREG:
			dent.d_type = DT_REG;
			break;
		case VSOCK:
			dent.d_type = DT_SOCK;
			break;
		default:
			KASSERT(0);
		}
		dent.d_namlen = de->td_namelen;
		KASSERT(de->td_namelen < sizeof(dent.d_name));
		memcpy(dent.d_name, de->td_name, de->td_namelen);
		dent.d_name[de->td_namelen] = '\0';
		dent.d_reclen = DIRENT_SIZE(&dent);

		if (memchr(dent.d_name, '/', dent.d_namlen) != NULL) {
			error = EINVAL;
			break;
		}

		next_de = TAILQ_NEXT(de, td_entries);
		if (next_de == NULL)
			dent.d_off = TMPFS_DIRSEQ_EOF;
		else
			dent.d_off = tmpfs_dir_getseq(node, next_de);

		if (dent.d_reclen > uio->uio_resid) {
			/* Exhausted UIO space. */
			error = EJUSTRETURN;
			break;
		}

		/* Copy out the directory entry and continue. */
		error = uiomove(&dent, dent.d_reclen, uio);
		if (error) {
			break;
		}
		de = TAILQ_NEXT(de, td_entries);

	} while (uio->uio_resid > 0 && de);

	/* Cache the last entry or clear and mark EOF. */
	uio->uio_offset = de ? tmpfs_dir_getseq(node, de) : TMPFS_DIRSEQ_EOF;
	node->tn_spec.tn_dir.tn_readdir_lastp = de;
done:
	tmpfs_update(node, TMPFS_NODE_ACCESSED);

	if (error == EJUSTRETURN) {
		/* Exhausted UIO space - just return. */
		error = 0;
	}
	KASSERT(error >= 0);
	return error;
}

/*
 * tmpfs_reg_resize: resize the underlying UVM object associated with the
 * specified regular file.
 */

int
tmpfs_reg_resize(struct vnode *vp, off_t newsize)
{
	tmpfs_mount_t *tmp = VFS_TO_TMPFS(vp->v_mount);
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);
	struct uvm_object *uobj = node->tn_spec.tn_reg.tn_aobj;
	size_t newpages, oldpages, bytes;
	off_t oldsize;
	vaddr_t pgoff;
	int error;

	KASSERT(vp->v_type == VREG);
	KASSERT(newsize >= 0);

	oldsize = node->tn_size;
	oldpages = round_page(oldsize) >> PAGE_SHIFT;
	newpages = round_page(newsize) >> PAGE_SHIFT;
	KASSERT(oldpages == node->tn_spec.tn_reg.tn_aobj_pages);

	if (newpages > oldpages) {
		/* Increase the used-memory counter if getting extra pages. */
		bytes = (newpages - oldpages) << PAGE_SHIFT;
		if (tmpfs_mem_incr(tmp, bytes) == 0)
			return ENOSPC;
		rw_enter(uobj->vmobjlock, RW_WRITE);
		error = uao_grow(uobj, newpages);
		rw_exit(uobj->vmobjlock);
		if (error) {
			tmpfs_mem_decr(tmp, bytes);
			return ENOSPC;
		}
	}

	node->tn_spec.tn_reg.tn_aobj_pages = newpages;
	node->tn_size = newsize;
	uvm_vnp_setsize(vp, newsize);
	uvm_vnp_uncache(vp);

	/*
	 * Free "backing store".
	 */
	if (newpages < oldpages) {
		if (tmpfs_uio_cached(node))
			tmpfs_uio_uncache(node);
		rw_enter(uobj->vmobjlock, RW_WRITE);
		if (uao_shrink(uobj, newpages))
			panic("shrink failed");
		rw_exit(uobj->vmobjlock);
		/* Decrease the used-memory counter. */
		tmpfs_mem_decr(tmp, (oldpages - newpages) << PAGE_SHIFT);
	}
	if (newsize > oldsize) {
		if (tmpfs_uio_cached(node))
			tmpfs_uio_uncache(node);
		pgoff = oldsize & PAGE_MASK;
		if (pgoff != 0) {
			/*
			 * Growing from an offset which is not at a page
			 * boundary; zero out unused bytes in current page.
			 */
			error = tmpfs_zeropg(node, trunc_page(oldsize), pgoff);
			if (error)
				panic("tmpfs_zeropg: error %d", error);
		}
		VN_KNOTE(vp, NOTE_EXTEND);
	}
	return 0;
}

/*
 * tmpfs_chflags: change flags of the given vnode.
 *
 */
int
tmpfs_chflags(struct vnode *vp, int flags, struct ucred *cred, struct proc *p)
{
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);
	int error;

	KASSERT(VOP_ISLOCKED(vp));

	/* Disallow this operation if the file system is mounted read-only. */
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;

	if (cred->cr_uid != node->tn_uid && (error = suser_ucred(cred)))
		return error;

	if (cred->cr_uid == 0) {
		if (node->tn_flags & (SF_IMMUTABLE | SF_APPEND) &&
		    securelevel > 0)
			return EPERM;
		node->tn_flags = flags;
	} else {
		if (node->tn_flags & (SF_IMMUTABLE | SF_APPEND) ||
		    (flags & UF_SETTABLE) != flags)
			return EPERM;
		node->tn_flags &= SF_SETTABLE;
		node->tn_flags |= (flags & UF_SETTABLE);
	}

	tmpfs_update(node, TMPFS_NODE_CHANGED);
	VN_KNOTE(vp, NOTE_ATTRIB);
	return 0;
}

/*
 * tmpfs_chmod: change access mode on the given vnode.
 *
 */
int
tmpfs_chmod(struct vnode *vp, mode_t mode, struct ucred *cred, struct proc *p)
{
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);
	int error;

	KASSERT(VOP_ISLOCKED(vp));

	/* Disallow this operation if the file system is mounted read-only. */
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;

	/* Immutable or append-only files cannot be modified, either. */
	if (node->tn_flags & (IMMUTABLE | APPEND))
		return EPERM;

	if (cred->cr_uid != node->tn_uid && (error = suser_ucred(cred)))
		return error;
	if (cred->cr_uid != 0) {
		if (vp->v_type != VDIR && (mode & S_ISTXT))
			return EFTYPE;
		if (!groupmember(node->tn_gid, cred) && (mode & S_ISGID))
			return EPERM;
	}

	node->tn_mode = (mode & ALLPERMS);
	tmpfs_update(node, TMPFS_NODE_CHANGED);
	if ((vp->v_flag & VTEXT) && (node->tn_mode & S_ISTXT) == 0)
		uvm_vnp_uncache(vp);
	VN_KNOTE(vp, NOTE_ATTRIB);
	return 0;
}

/*
 * tmpfs_chown: change ownership of the given vnode.
 *
 * => At least one of uid or gid must be different than VNOVAL.
 * => Attribute is unchanged for VNOVAL case.
 */
int
tmpfs_chown(struct vnode *vp, uid_t uid, gid_t gid, struct ucred *cred, struct proc *p)
{
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);
	int error;

	KASSERT(VOP_ISLOCKED(vp));

	/* Assign default values if they are unknown. */
	KASSERT(uid != VNOVAL || gid != VNOVAL);
	if (uid == VNOVAL) {
		uid = node->tn_uid;
	}
	if (gid == VNOVAL) {
		gid = node->tn_gid;
	}

	/* Disallow this operation if the file system is mounted read-only. */
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;

	/* Immutable or append-only files cannot be modified, either. */
	if (node->tn_flags & (IMMUTABLE | APPEND))
		return EPERM;

	if ((cred->cr_uid != node->tn_uid || uid != node->tn_uid ||
	    (gid != node->tn_gid && !groupmember(gid, cred))) &&
	    (error = suser_ucred(cred)))
	    	return error;

	node->tn_uid = uid;
	node->tn_gid = gid;
	tmpfs_update(node, TMPFS_NODE_CHANGED);
	VN_KNOTE(vp, NOTE_ATTRIB);
	return 0;
}

/*
 * tmpfs_chsize: change size of the given vnode.
 */
int
tmpfs_chsize(struct vnode *vp, u_quad_t size, struct ucred *cred, struct proc *p)
{
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);

	KASSERT(VOP_ISLOCKED(vp));

	/* Decide whether this is a valid operation based on the file type. */
	switch (vp->v_type) {
	case VDIR:
		return EISDIR;
	case VREG:
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			return EROFS;
		}
		break;
	case VBLK:
	case VCHR:
	case VFIFO:
		/*
		 * Allow modifications of special files even if in the file
		 * system is mounted read-only (we are not modifying the
		 * files themselves, but the objects they represent).
		 */
		return 0;
	default:
		return EOPNOTSUPP;
	}

	/* Immutable or append-only files cannot be modified, either. */
	if (node->tn_flags & (IMMUTABLE | APPEND)) {
		return EPERM;
	}

	/* Note: tmpfs_truncate() will raise NOTE_EXTEND and NOTE_ATTRIB. */
	return tmpfs_truncate(vp, size);
}

/*
 * tmpfs_chtimes: change access and modification times for vnode.
 */
int
tmpfs_chtimes(struct vnode *vp, const struct timespec *atime,
    const struct timespec *mtime, int vaflags, struct ucred *cred,
    struct proc *p)
{
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);
	int error;

	KASSERT(VOP_ISLOCKED(vp));

	/* Disallow this operation if the file system is mounted read-only. */
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return EROFS;

	/* Immutable or append-only files cannot be modified, either. */
	if (node->tn_flags & (IMMUTABLE | APPEND))
		return EPERM;

	if (cred->cr_uid != node->tn_uid && (error = suser_ucred(cred)) &&
	    ((vaflags & VA_UTIMES_NULL) == 0 ||
	    (error = VOP_ACCESS(vp, VWRITE, cred, p))))
	    	return error;

	if (atime->tv_nsec != VNOVAL)
		node->tn_atime = *atime;

	if (mtime->tv_nsec != VNOVAL)
		node->tn_mtime = *mtime;

	if (mtime->tv_nsec != VNOVAL || (vaflags & VA_UTIMES_CHANGE))
		tmpfs_update(VP_TO_TMPFS_NODE(vp), TMPFS_NODE_CHANGED);

	VN_KNOTE(vp, NOTE_ATTRIB);

	return 0;
}

/*
 * tmpfs_update: update timestamps, et al.
 */
void
tmpfs_update(tmpfs_node_t *node, int flags)
{
	struct timespec nowtm;

	nanotime(&nowtm);

	if (flags & TMPFS_NODE_ACCESSED) {
		node->tn_atime = nowtm;
	}
	if (flags & TMPFS_NODE_MODIFIED) {
		node->tn_mtime = nowtm;
	}
	if (flags & TMPFS_NODE_CHANGED) {
		node->tn_ctime = nowtm;
	}
}

int
tmpfs_truncate(struct vnode *vp, off_t length)
{
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);
	int error;

	if (length < 0) {
		error = EINVAL;
		goto out;
	}
	if (node->tn_size == length) {
		error = 0;
		goto out;
	}
	error = tmpfs_reg_resize(vp, length);
	if (error == 0) {
		tmpfs_update(node, TMPFS_NODE_CHANGED | TMPFS_NODE_MODIFIED);
	}
out:
	return error;
}

int
tmpfs_uio_cached(tmpfs_node_t *node)
{
	int pgnum_valid = (node->tn_pgnum != (voff_t)-1);
	int pgptr_valid = (node->tn_pgptr != (vaddr_t)NULL);
	KASSERT(pgnum_valid == pgptr_valid);
	return pgnum_valid && pgptr_valid;
}

vaddr_t
tmpfs_uio_lookup(tmpfs_node_t *node, voff_t pgnum)
{
	if (tmpfs_uio_cached(node) == 1 && node->tn_pgnum == pgnum)
		return node->tn_pgptr;

	return (vaddr_t)NULL;
}

void
tmpfs_uio_uncache(tmpfs_node_t *node)
{
	KASSERT(node->tn_pgnum != (voff_t)-1);
	KASSERT(node->tn_pgptr != (vaddr_t)NULL);
	uvm_unmap(kernel_map, node->tn_pgptr, node->tn_pgptr + PAGE_SIZE);
	node->tn_pgnum = (voff_t)-1;
	node->tn_pgptr = (vaddr_t)NULL;
}

void
tmpfs_uio_cache(tmpfs_node_t *node, voff_t pgnum, vaddr_t pgptr)
{
	KASSERT(node->tn_pgnum == (voff_t)-1);
	KASSERT(node->tn_pgptr == (vaddr_t)NULL);
	node->tn_pgnum = pgnum;
	node->tn_pgptr = pgptr;
}

/*
 * Be gentle to kernel_map, don't allow more than 4MB in a single transaction.
 */
#define TMPFS_UIO_MAXBYTES	((1 << 22) - PAGE_SIZE)

int
tmpfs_uiomove(tmpfs_node_t *node, struct uio *uio, vsize_t len)
{
	vaddr_t va, pgoff;
	int error, adv;
	voff_t pgnum;
	vsize_t sz;

	pgnum = trunc_page(uio->uio_offset);
	pgoff = uio->uio_offset & PAGE_MASK;

	if (pgoff + len < PAGE_SIZE) {
		va = tmpfs_uio_lookup(node, pgnum);
		if (va != (vaddr_t)NULL)
			return uiomove((void *)va + pgoff, len, uio);
	}

	if (len >= TMPFS_UIO_MAXBYTES) {
		sz = TMPFS_UIO_MAXBYTES;
		adv = MADV_NORMAL;
	} else {
		sz = len;
		adv = MADV_SEQUENTIAL;
	}

	if (tmpfs_uio_cached(node))
		tmpfs_uio_uncache(node);

	uao_reference(node->tn_uobj);

	error = uvm_map(kernel_map, &va, round_page(pgoff + sz), node->tn_uobj,
	    trunc_page(uio->uio_offset), 0, UVM_MAPFLAG(PROT_READ | PROT_WRITE,
	    PROT_READ | PROT_WRITE, MAP_INHERIT_NONE, adv, 0));
	if (error) {
		uao_detach(node->tn_uobj); /* Drop reference. */
		return error;
	}

	error = uiomove((void *)va + pgoff, sz, uio);
	if (error == 0 && pgoff + sz < PAGE_SIZE)
		tmpfs_uio_cache(node, pgnum, va);
	else
		uvm_unmap(kernel_map, va, va + round_page(pgoff + sz));

	return error;
}

int
tmpfs_zeropg(tmpfs_node_t *node, voff_t pgnum, vaddr_t pgoff)
{
	vaddr_t va;
	int error;

	KASSERT(tmpfs_uio_cached(node) == 0);

	uao_reference(node->tn_uobj);

	error = uvm_map(kernel_map, &va, PAGE_SIZE, node->tn_uobj, pgnum, 0,
	    UVM_MAPFLAG(PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE,
	    MAP_INHERIT_NONE, MADV_NORMAL, 0));
	if (error) {
		uao_detach(node->tn_uobj); /* Drop reference. */
		return error;
	}

	bzero((void *)va + pgoff, PAGE_SIZE - pgoff);
	uvm_unmap(kernel_map, va, va + PAGE_SIZE);

	return 0;
}
