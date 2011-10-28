/*
 *  Device operations for the pnfs client.
 *
 *  Copyright (c) 2002
 *  The Regents of the University of Michigan
 *  All Rights Reserved
 *
 *  Dean Hildebrand <dhildebz@umich.edu>
 *  Garth Goodson   <Garth.Goodson@netapp.com>
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

#include "pnfs.h"

#define NFSDBG_FACILITY		NFSDBG_PNFS

/*
 * Device ID RCU cache. A device ID is unique per server and layout type.
 */
#define NFS4_DEVICE_ID_HASH_BITS	5
#define NFS4_DEVICE_ID_HASH_SIZE	(1 << NFS4_DEVICE_ID_HASH_BITS)
#define NFS4_DEVICE_ID_HASH_MASK	(NFS4_DEVICE_ID_HASH_SIZE - 1)

static struct hlist_head nfs4_deviceid_cache[NFS4_DEVICE_ID_HASH_SIZE];
static DEFINE_SPINLOCK(nfs4_deviceid_lock);

void
nfs4_print_deviceid(const struct nfs4_deviceid *id)
{
	u32 *p = (u32 *)id;

	dprintk("%s: device id= [%x%x%x%x]\n", __func__,
		p[0], p[1], p[2], p[3]);
}
EXPORT_SYMBOL_GPL(nfs4_print_deviceid);

static inline u32
nfs4_deviceid_hash(const struct nfs4_deviceid *id)
{
	unsigned char *cptr = (unsigned char *)id->data;
	unsigned int nbytes = NFS4_DEVICEID4_SIZE;
	u32 x = 0;

	while (nbytes--) {
		x *= 37;
		x += *cptr++;
	}
	return x & NFS4_DEVICE_ID_HASH_MASK;
}

static struct nfs4_deviceid_node *
_lookup_deviceid(const struct pnfs_layoutdriver_type *ld,
		 const struct nfs_client *clp, const struct nfs4_deviceid *id,
		 long hash)
{
	struct nfs4_deviceid_node *d;
	struct hlist_node *n;

	hlist_for_each_entry_rcu(d, n, &nfs4_deviceid_cache[hash], node)
		if (d->ld == ld && d->nfs_client == clp &&
		    !memcmp(&d->deviceid, id, sizeof(*id))) {
			if (atomic_read(&d->ref))
				return d;
			else
				continue;
		}
	return NULL;
}

/*
 * Lookup a deviceid in cache and get a reference count on it if found
 *
 * @clp nfs_client associated with deviceid
 * @id deviceid to look up
 */
struct nfs4_deviceid_node *
_find_get_deviceid(const struct pnfs_layoutdriver_type *ld,
		   const struct nfs_client *clp, const struct nfs4_deviceid *id,
		   long hash)
{
	struct nfs4_deviceid_node *d;

	rcu_read_lock();
	d = _lookup_deviceid(ld, clp, id, hash);
	if (d && !atomic_inc_not_zero(&d->ref))
		d = NULL;
	rcu_read_unlock();
	return d;
}

struct nfs4_deviceid_node *
nfs4_find_get_deviceid(const struct pnfs_layoutdriver_type *ld,
		       const struct nfs_client *clp, const struct nfs4_deviceid *id)
{
	return _find_get_deviceid(ld, clp, id, nfs4_deviceid_hash(id));
}
EXPORT_SYMBOL_GPL(nfs4_find_get_deviceid);

/*
 * Unhash and put deviceid
 *
 * @clp nfs_client associated with deviceid
 * @id the deviceid to unhash
 *
 * @ret the unhashed node, if found and dereferenced to zero, NULL otherwise.
 */
struct nfs4_deviceid_node *
nfs4_unhash_put_deviceid(const struct pnfs_layoutdriver_type *ld,
			 const struct nfs_client *clp, const struct nfs4_deviceid *id)
{
	struct nfs4_deviceid_node *d;

	spin_lock(&nfs4_deviceid_lock);
	rcu_read_lock();
	d = _lookup_deviceid(ld, clp, id, nfs4_deviceid_hash(id));
	rcu_read_unlock();
	if (!d) {
		spin_unlock(&nfs4_deviceid_lock);
		return NULL;
	}
	hlist_del_init_rcu(&d->node);
	spin_unlock(&nfs4_deviceid_lock);
	synchronize_rcu();

	/* balance the initial ref set in pnfs_insert_deviceid */
	if (atomic_dec_and_test(&d->ref))
		return d;

	return NULL;
}
EXPORT_SYMBOL_GPL(nfs4_unhash_put_deviceid);

/*
 * Delete a deviceid from cache
 *
 * @clp struct nfs_client qualifying the deviceid
 * @id deviceid to delete
 */
void
nfs4_delete_deviceid(const struct pnfs_layoutdriver_type *ld,
		     const struct nfs_client *clp, const struct nfs4_deviceid *id)
{
	struct nfs4_deviceid_node *d;

	d = nfs4_unhash_put_deviceid(ld, clp, id);
	if (!d)
		return;
	d->ld->free_deviceid_node(d);
}
EXPORT_SYMBOL_GPL(nfs4_delete_deviceid);

void
nfs4_init_deviceid_node(struct nfs4_deviceid_node *d,
			const struct pnfs_layoutdriver_type *ld,
			const struct nfs_client *nfs_client,
			const struct nfs4_deviceid *id)
{
	INIT_HLIST_NODE(&d->node);
	INIT_HLIST_NODE(&d->tmpnode);
	d->ld = ld;
	d->nfs_client = nfs_client;
	d->deviceid = *id;
	atomic_set(&d->ref, 1);
}
EXPORT_SYMBOL_GPL(nfs4_init_deviceid_node);

/*
 * Uniquely initialize and insert a deviceid node into cache
 *
 * @new new deviceid node
 *      Note that the caller must set up the following members:
 *        new->ld
 *        new->nfs_client
 *        new->deviceid
 *
 * @ret the inserted node, if none found, otherwise, the found entry.
 */
struct nfs4_deviceid_node *
nfs4_insert_deviceid_node(struct nfs4_deviceid_node *new)
{
	struct nfs4_deviceid_node *d;
	long hash;

	spin_lock(&nfs4_deviceid_lock);
	hash = nfs4_deviceid_hash(&new->deviceid);
	d = _find_get_deviceid(new->ld, new->nfs_client, &new->deviceid, hash);
	if (d) {
		spin_unlock(&nfs4_deviceid_lock);
		return d;
	}

	hlist_add_head_rcu(&new->node, &nfs4_deviceid_cache[hash]);
	spin_unlock(&nfs4_deviceid_lock);
	atomic_inc(&new->ref);

	return new;
}
EXPORT_SYMBOL_GPL(nfs4_insert_deviceid_node);

/*
 * Dereference a deviceid node and delete it when its reference count drops
 * to zero.
 *
 * @d deviceid node to put
 *
 * @ret true iff the node was deleted
 */
bool
nfs4_put_deviceid_node(struct nfs4_deviceid_node *d)
{
	if (!atomic_dec_and_lock(&d->ref, &nfs4_deviceid_lock))
		return false;
	hlist_del_init_rcu(&d->node);
	spin_unlock(&nfs4_deviceid_lock);
	synchronize_rcu();
	d->ld->free_deviceid_node(d);
	return true;
}
EXPORT_SYMBOL_GPL(nfs4_put_deviceid_node);

static void
_deviceid_purge_client(const struct nfs_client *clp, long hash)
{
	struct nfs4_deviceid_node *d;
	struct hlist_node *n;
	HLIST_HEAD(tmp);

	spin_lock(&nfs4_deviceid_lock);
	rcu_read_lock();
	hlist_for_each_entry_rcu(d, n, &nfs4_deviceid_cache[hash], node)
		if (d->nfs_client == clp && atomic_read(&d->ref)) {
			hlist_del_init_rcu(&d->node);
			hlist_add_head(&d->tmpnode, &tmp);
		}
	rcu_read_unlock();
	spin_unlock(&nfs4_deviceid_lock);

	if (hlist_empty(&tmp))
		return;

	synchronize_rcu();
	while (!hlist_empty(&tmp)) {
		d = hlist_entry(tmp.first, struct nfs4_deviceid_node, tmpnode);
		hlist_del(&d->tmpnode);
		if (atomic_dec_and_test(&d->ref))
			d->ld->free_deviceid_node(d);
	}
}

void
nfs4_deviceid_purge_client(const struct nfs_client *clp)
{
	long h;

	if (!(clp->cl_exchange_flags & EXCHGID4_FLAG_USE_PNFS_MDS))
		return;
	for (h = 0; h < NFS4_DEVICE_ID_HASH_SIZE; h++)
		_deviceid_purge_client(clp, h);
}
