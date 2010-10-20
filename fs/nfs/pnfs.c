/*
 *  pNFS functions to call and manage layout drivers.
 *
 *  Copyright (c) 2002 [year of first publication]
 *  The Regents of the University of Michigan
 *  All Rights Reserved
 *
 *  Dean Hildebrand <dhildebz@umich.edu>
 *
 *  Permission is granted to use, copy, create derivative works, and
 *  redistribute this software and such derivative works for any purpose,
 *  so long as the name of the University of Michigan is not used in
 *  any advertising or publicity pertaining to the use or distribution
 *  of this software without specific, written prior authorization. If
 *  the above copyright notice or any other identification of the
 *  University of Michigan is included in any copy of any portion of
 *  this software, then the disclaimer below must also be included.
 *
 *  This software is provided as is, without representation or warranty
 *  of any kind either express or implied, including without limitation
 *  the implied warranties of merchantability, fitness for a particular
 *  purpose, or noninfringement.  The Regents of the University of
 *  Michigan shall not be liable for any damages, including special,
 *  indirect, incidental, or consequential damages, with respect to any
 *  claim arising out of or in connection with the use of the software,
 *  even if it has been or is hereafter advised of the possibility of
 *  such damages.
 */

#include <linux/nfs_fs.h>
#include "pnfs.h"

#define NFSDBG_FACILITY		NFSDBG_PNFS

/* Locking:
 *
 * pnfs_spinlock:
 *      protects pnfs_modules_tbl.
 */
static DEFINE_SPINLOCK(pnfs_spinlock);

/*
 * pnfs_modules_tbl holds all pnfs modules
 */
static LIST_HEAD(pnfs_modules_tbl);

/* Return the registered pnfs layout driver module matching given id */
static struct pnfs_layoutdriver_type *
find_pnfs_driver_locked(u32 id)
{
	struct pnfs_layoutdriver_type *local;

	list_for_each_entry(local, &pnfs_modules_tbl, pnfs_tblid)
		if (local->id == id)
			goto out;
	local = NULL;
out:
	dprintk("%s: Searching for id %u, found %p\n", __func__, id, local);
	return local;
}

static struct pnfs_layoutdriver_type *
find_pnfs_driver(u32 id)
{
	struct pnfs_layoutdriver_type *local;

	spin_lock(&pnfs_spinlock);
	local = find_pnfs_driver_locked(id);
	spin_unlock(&pnfs_spinlock);
	return local;
}

void
unset_pnfs_layoutdriver(struct nfs_server *nfss)
{
	if (nfss->pnfs_curr_ld) {
		nfss->pnfs_curr_ld->uninitialize_mountpoint(nfss);
		module_put(nfss->pnfs_curr_ld->owner);
	}
	nfss->pnfs_curr_ld = NULL;
}

/*
 * Try to set the server's pnfs module to the pnfs layout type specified by id.
 * Currently only one pNFS layout driver per filesystem is supported.
 *
 * @id layout type. Zero (illegal layout type) indicates pNFS not in use.
 */
void
set_pnfs_layoutdriver(struct nfs_server *server, u32 id)
{
	struct pnfs_layoutdriver_type *ld_type = NULL;

	if (id == 0)
		goto out_no_driver;
	if (!(server->nfs_client->cl_exchange_flags &
		 (EXCHGID4_FLAG_USE_NON_PNFS | EXCHGID4_FLAG_USE_PNFS_MDS))) {
		printk(KERN_ERR "%s: id %u cl_exchange_flags 0x%x\n", __func__,
		       id, server->nfs_client->cl_exchange_flags);
		goto out_no_driver;
	}
	ld_type = find_pnfs_driver(id);
	if (!ld_type) {
		request_module("%s-%u", LAYOUT_NFSV4_1_MODULE_PREFIX, id);
		ld_type = find_pnfs_driver(id);
		if (!ld_type) {
			dprintk("%s: No pNFS module found for %u.\n",
				__func__, id);
			goto out_no_driver;
		}
	}
	if (!try_module_get(ld_type->owner)) {
		dprintk("%s: Could not grab reference on module\n", __func__);
		goto out_no_driver;
	}
	server->pnfs_curr_ld = ld_type;
	if (ld_type->initialize_mountpoint(server)) {
		printk(KERN_ERR
		       "%s: Error initializing mount point for layout driver %u.\n",
		       __func__, id);
		module_put(ld_type->owner);
		goto out_no_driver;
	}
	dprintk("%s: pNFS module for %u set\n", __func__, id);
	return;

out_no_driver:
	dprintk("%s: Using NFSv4 I/O\n", __func__);
	server->pnfs_curr_ld = NULL;
}

int
pnfs_register_layoutdriver(struct pnfs_layoutdriver_type *ld_type)
{
	int status = -EINVAL;
	struct pnfs_layoutdriver_type *tmp;

	if (ld_type->id == 0) {
		printk(KERN_ERR "%s id 0 is reserved\n", __func__);
		return status;
	}

	spin_lock(&pnfs_spinlock);
	tmp = find_pnfs_driver_locked(ld_type->id);
	if (!tmp) {
		list_add(&ld_type->pnfs_tblid, &pnfs_modules_tbl);
		status = 0;
		dprintk("%s Registering id:%u name:%s\n", __func__, ld_type->id,
			ld_type->name);
	} else {
		printk(KERN_ERR "%s Module with id %d already loaded!\n",
			__func__, ld_type->id);
	}
	spin_unlock(&pnfs_spinlock);

	return status;
}
EXPORT_SYMBOL_GPL(pnfs_register_layoutdriver);

void
pnfs_unregister_layoutdriver(struct pnfs_layoutdriver_type *ld_type)
{
	dprintk("%s Deregistering id:%u\n", __func__, ld_type->id);
	spin_lock(&pnfs_spinlock);
	list_del(&ld_type->pnfs_tblid);
	spin_unlock(&pnfs_spinlock);
}
EXPORT_SYMBOL_GPL(pnfs_unregister_layoutdriver);

static void
get_layout_hdr_locked(struct pnfs_layout_hdr *lo)
{
	assert_spin_locked(&lo->inode->i_lock);
	lo->refcount++;
}

static void
put_layout_hdr_locked(struct pnfs_layout_hdr *lo)
{
	assert_spin_locked(&lo->inode->i_lock);
	BUG_ON(lo->refcount == 0);

	lo->refcount--;
	if (!lo->refcount) {
		dprintk("%s: freeing layout cache %p\n", __func__, lo);
		NFS_I(lo->inode)->layout = NULL;
		kfree(lo);
	}
}

void
pnfs_destroy_layout(struct nfs_inode *nfsi)
{
	struct pnfs_layout_hdr *lo;

	spin_lock(&nfsi->vfs_inode.i_lock);
	lo = nfsi->layout;
	if (lo) {
		/* Matched by refcount set to 1 in alloc_init_layout_hdr */
		put_layout_hdr_locked(lo);
	}
	spin_unlock(&nfsi->vfs_inode.i_lock);
}

/* STUB - pretend LAYOUTGET to server failed */
static struct pnfs_layout_segment *
send_layoutget(struct pnfs_layout_hdr *lo,
	   struct nfs_open_context *ctx,
	   u32 iomode)
{
	struct inode *ino = lo->inode;

	set_bit(lo_fail_bit(iomode), &lo->state);
	spin_lock(&ino->i_lock);
	put_layout_hdr_locked(lo);
	spin_unlock(&ino->i_lock);
	return NULL;
}

static struct pnfs_layout_hdr *
alloc_init_layout_hdr(struct inode *ino)
{
	struct pnfs_layout_hdr *lo;

	lo = kzalloc(sizeof(struct pnfs_layout_hdr), GFP_KERNEL);
	if (!lo)
		return NULL;
	lo->refcount = 1;
	lo->inode = ino;
	return lo;
}

static struct pnfs_layout_hdr *
pnfs_find_alloc_layout(struct inode *ino)
{
	struct nfs_inode *nfsi = NFS_I(ino);
	struct pnfs_layout_hdr *new = NULL;

	dprintk("%s Begin ino=%p layout=%p\n", __func__, ino, nfsi->layout);

	assert_spin_locked(&ino->i_lock);
	if (nfsi->layout)
		return nfsi->layout;

	spin_unlock(&ino->i_lock);
	new = alloc_init_layout_hdr(ino);
	spin_lock(&ino->i_lock);

	if (likely(nfsi->layout == NULL))	/* Won the race? */
		nfsi->layout = new;
	else
		kfree(new);
	return nfsi->layout;
}

/* STUB - LAYOUTGET never succeeds, so cache is empty */
static struct pnfs_layout_segment *
pnfs_has_layout(struct pnfs_layout_hdr *lo, u32 iomode)
{
	return NULL;
}

/*
 * Layout segment is retreived from the server if not cached.
 * The appropriate layout segment is referenced and returned to the caller.
 */
struct pnfs_layout_segment *
pnfs_update_layout(struct inode *ino,
		   struct nfs_open_context *ctx,
		   enum pnfs_iomode iomode)
{
	struct nfs_inode *nfsi = NFS_I(ino);
	struct pnfs_layout_hdr *lo;
	struct pnfs_layout_segment *lseg = NULL;

	if (!pnfs_enabled_sb(NFS_SERVER(ino)))
		return NULL;
	spin_lock(&ino->i_lock);
	lo = pnfs_find_alloc_layout(ino);
	if (lo == NULL) {
		dprintk("%s ERROR: can't get pnfs_layout_hdr\n", __func__);
		goto out_unlock;
	}

	/* Check to see if the layout for the given range already exists */
	lseg = pnfs_has_layout(lo, iomode);
	if (lseg) {
		dprintk("%s: Using cached lseg %p for iomode %d)\n",
			__func__, lseg, iomode);
		goto out_unlock;
	}

	/* if LAYOUTGET already failed once we don't try again */
	if (test_bit(lo_fail_bit(iomode), &nfsi->layout->state))
		goto out_unlock;

	get_layout_hdr_locked(lo);
	spin_unlock(&ino->i_lock);

	lseg = send_layoutget(lo, ctx, iomode);
out:
	dprintk("%s end, state 0x%lx lseg %p\n", __func__,
		nfsi->layout->state, lseg);
	return lseg;
out_unlock:
	spin_unlock(&ino->i_lock);
	goto out;
}
