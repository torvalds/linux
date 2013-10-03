/*
 *  linux/fs/pnode.c
 *
 * (C) Copyright IBM Corporation 2005.
 *	Released under GPL v2.
 *	Author : Ram Pai (linuxram@us.ibm.com)
 *
 */
#include <linux/mnt_namespace.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include <linux/nsproxy.h>
#include "internal.h"
#include "pnode.h"

/* return the next shared peer mount of @p */
static inline struct mount *next_peer(struct mount *p)
{
	return list_entry(p->mnt_share.next, struct mount, mnt_share);
}

static inline struct mount *first_slave(struct mount *p)
{
	return list_entry(p->mnt_slave_list.next, struct mount, mnt_slave);
}

static inline struct mount *next_slave(struct mount *p)
{
	return list_entry(p->mnt_slave.next, struct mount, mnt_slave);
}

static struct mount *get_peer_under_root(struct mount *mnt,
					 struct mnt_namespace *ns,
					 const struct path *root)
{
	struct mount *m = mnt;

	do {
		/* Check the namespace first for optimization */
		if (m->mnt_ns == ns && is_path_reachable(m, m->mnt.mnt_root, root))
			return m;

		m = next_peer(m);
	} while (m != mnt);

	return NULL;
}

/*
 * Get ID of closest dominating peer group having a representative
 * under the given root.
 *
 * Caller must hold namespace_sem
 */
int get_dominating_id(struct mount *mnt, const struct path *root)
{
	struct mount *m;

	for (m = mnt->mnt_master; m != NULL; m = m->mnt_master) {
		struct mount *d = get_peer_under_root(m, mnt->mnt_ns, root);
		if (d)
			return d->mnt_group_id;
	}

	return 0;
}

static int do_make_slave(struct mount *mnt)
{
	struct mount *peer_mnt = mnt, *master = mnt->mnt_master;
	struct mount *slave_mnt;

	/*
	 * slave 'mnt' to a peer mount that has the
	 * same root dentry. If none is available then
	 * slave it to anything that is available.
	 */
	while ((peer_mnt = next_peer(peer_mnt)) != mnt &&
	       peer_mnt->mnt.mnt_root != mnt->mnt.mnt_root) ;

	if (peer_mnt == mnt) {
		peer_mnt = next_peer(mnt);
		if (peer_mnt == mnt)
			peer_mnt = NULL;
	}
	if (mnt->mnt_group_id && IS_MNT_SHARED(mnt) &&
	    list_empty(&mnt->mnt_share))
		mnt_release_group_id(mnt);

	list_del_init(&mnt->mnt_share);
	mnt->mnt_group_id = 0;

	if (peer_mnt)
		master = peer_mnt;

	if (master) {
		list_for_each_entry(slave_mnt, &mnt->mnt_slave_list, mnt_slave)
			slave_mnt->mnt_master = master;
		list_move(&mnt->mnt_slave, &master->mnt_slave_list);
		list_splice(&mnt->mnt_slave_list, master->mnt_slave_list.prev);
		INIT_LIST_HEAD(&mnt->mnt_slave_list);
	} else {
		struct list_head *p = &mnt->mnt_slave_list;
		while (!list_empty(p)) {
                        slave_mnt = list_first_entry(p,
					struct mount, mnt_slave);
			list_del_init(&slave_mnt->mnt_slave);
			slave_mnt->mnt_master = NULL;
		}
	}
	mnt->mnt_master = master;
	CLEAR_MNT_SHARED(mnt);
	return 0;
}

/*
 * vfsmount lock must be held for write
 */
void change_mnt_propagation(struct mount *mnt, int type)
{
	if (type == MS_SHARED) {
		set_mnt_shared(mnt);
		return;
	}
	do_make_slave(mnt);
	if (type != MS_SLAVE) {
		list_del_init(&mnt->mnt_slave);
		mnt->mnt_master = NULL;
		if (type == MS_UNBINDABLE)
			mnt->mnt.mnt_flags |= MNT_UNBINDABLE;
		else
			mnt->mnt.mnt_flags &= ~MNT_UNBINDABLE;
	}
}

/*
 * get the next mount in the propagation tree.
 * @m: the mount seen last
 * @origin: the original mount from where the tree walk initiated
 *
 * Note that peer groups form contiguous segments of slave lists.
 * We rely on that in get_source() to be able to find out if
 * vfsmount found while iterating with propagation_next() is
 * a peer of one we'd found earlier.
 */
static struct mount *propagation_next(struct mount *m,
					 struct mount *origin)
{
	/* are there any slaves of this mount? */
	if (!IS_MNT_NEW(m) && !list_empty(&m->mnt_slave_list))
		return first_slave(m);

	while (1) {
		struct mount *master = m->mnt_master;

		if (master == origin->mnt_master) {
			struct mount *next = next_peer(m);
			return (next == origin) ? NULL : next;
		} else if (m->mnt_slave.next != &master->mnt_slave_list)
			return next_slave(m);

		/* back at master */
		m = master;
	}
}

/*
 * return the source mount to be used for cloning
 *
 * @dest 	the current destination mount
 * @last_dest  	the last seen destination mount
 * @last_src  	the last seen source mount
 * @type	return CL_SLAVE if the new mount has to be
 * 		cloned as a slave.
 */
static struct mount *get_source(struct mount *dest,
				struct mount *last_dest,
				struct mount *last_src,
				int *type)
{
	struct mount *p_last_src = NULL;
	struct mount *p_last_dest = NULL;

	while (last_dest != dest->mnt_master) {
		p_last_dest = last_dest;
		p_last_src = last_src;
		last_dest = last_dest->mnt_master;
		last_src = last_src->mnt_master;
	}

	if (p_last_dest) {
		do {
			p_last_dest = next_peer(p_last_dest);
		} while (IS_MNT_NEW(p_last_dest));
		/* is that a peer of the earlier? */
		if (dest == p_last_dest) {
			*type = CL_MAKE_SHARED;
			return p_last_src;
		}
	}
	/* slave of the earlier, then */
	*type = CL_SLAVE;
	/* beginning of peer group among the slaves? */
	if (IS_MNT_SHARED(dest))
		*type |= CL_MAKE_SHARED;
	return last_src;
}

/*
 * mount 'source_mnt' under the destination 'dest_mnt' at
 * dentry 'dest_dentry'. And propagate that mount to
 * all the peer and slave mounts of 'dest_mnt'.
 * Link all the new mounts into a propagation tree headed at
 * source_mnt. Also link all the new mounts using ->mnt_list
 * headed at source_mnt's ->mnt_list
 *
 * @dest_mnt: destination mount.
 * @dest_dentry: destination dentry.
 * @source_mnt: source mount.
 * @tree_list : list of heads of trees to be attached.
 */
int propagate_mnt(struct mount *dest_mnt, struct mountpoint *dest_mp,
		    struct mount *source_mnt, struct list_head *tree_list)
{
	struct user_namespace *user_ns = current->nsproxy->mnt_ns->user_ns;
	struct mount *m, *child;
	int ret = 0;
	struct mount *prev_dest_mnt = dest_mnt;
	struct mount *prev_src_mnt  = source_mnt;
	LIST_HEAD(tmp_list);

	for (m = propagation_next(dest_mnt, dest_mnt); m;
			m = propagation_next(m, dest_mnt)) {
		int type;
		struct mount *source;

		if (IS_MNT_NEW(m))
			continue;

		source =  get_source(m, prev_dest_mnt, prev_src_mnt, &type);

		/* Notice when we are propagating across user namespaces */
		if (m->mnt_ns->user_ns != user_ns)
			type |= CL_UNPRIVILEGED;

		child = copy_tree(source, source->mnt.mnt_root, type);
		if (IS_ERR(child)) {
			ret = PTR_ERR(child);
			list_splice(tree_list, tmp_list.prev);
			goto out;
		}

		if (is_subdir(dest_mp->m_dentry, m->mnt.mnt_root)) {
			mnt_set_mountpoint(m, dest_mp, child);
			list_add_tail(&child->mnt_hash, tree_list);
		} else {
			/*
			 * This can happen if the parent mount was bind mounted
			 * on some subdirectory of a shared/slave mount.
			 */
			list_add_tail(&child->mnt_hash, &tmp_list);
		}
		prev_dest_mnt = m;
		prev_src_mnt  = child;
	}
out:
	br_write_lock(&vfsmount_lock);
	while (!list_empty(&tmp_list)) {
		child = list_first_entry(&tmp_list, struct mount, mnt_hash);
		umount_tree(child, 0);
	}
	br_write_unlock(&vfsmount_lock);
	return ret;
}

/*
 * return true if the refcount is greater than count
 */
static inline int do_refcount_check(struct mount *mnt, int count)
{
	int mycount = mnt_get_count(mnt) - mnt->mnt_ghosts;
	return (mycount > count);
}

/*
 * check if the mount 'mnt' can be unmounted successfully.
 * @mnt: the mount to be checked for unmount
 * NOTE: unmounting 'mnt' would naturally propagate to all
 * other mounts its parent propagates to.
 * Check if any of these mounts that **do not have submounts**
 * have more references than 'refcnt'. If so return busy.
 *
 * vfsmount lock must be held for write
 */
int propagate_mount_busy(struct mount *mnt, int refcnt)
{
	struct mount *m, *child;
	struct mount *parent = mnt->mnt_parent;
	int ret = 0;

	if (mnt == parent)
		return do_refcount_check(mnt, refcnt);

	/*
	 * quickly check if the current mount can be unmounted.
	 * If not, we don't have to go checking for all other
	 * mounts
	 */
	if (!list_empty(&mnt->mnt_mounts) || do_refcount_check(mnt, refcnt))
		return 1;

	for (m = propagation_next(parent, parent); m;
	     		m = propagation_next(m, parent)) {
		child = __lookup_mnt(&m->mnt, mnt->mnt_mountpoint, 0);
		if (child && list_empty(&child->mnt_mounts) &&
		    (ret = do_refcount_check(child, 1)))
			break;
	}
	return ret;
}

/*
 * NOTE: unmounting 'mnt' naturally propagates to all other mounts its
 * parent propagates to.
 */
static void __propagate_umount(struct mount *mnt)
{
	struct mount *parent = mnt->mnt_parent;
	struct mount *m;

	BUG_ON(parent == mnt);

	for (m = propagation_next(parent, parent); m;
			m = propagation_next(m, parent)) {

		struct mount *child = __lookup_mnt(&m->mnt,
					mnt->mnt_mountpoint, 0);
		/*
		 * umount the child only if the child has no
		 * other children
		 */
		if (child && list_empty(&child->mnt_mounts))
			list_move_tail(&child->mnt_hash, &mnt->mnt_hash);
	}
}

/*
 * collect all mounts that receive propagation from the mount in @list,
 * and return these additional mounts in the same list.
 * @list: the list of mounts to be unmounted.
 *
 * vfsmount lock must be held for write
 */
int propagate_umount(struct list_head *list)
{
	struct mount *mnt;

	list_for_each_entry(mnt, list, mnt_hash)
		__propagate_umount(mnt);
	return 0;
}
