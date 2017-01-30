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
	struct mount *master, *slave_mnt;

	if (list_empty(&mnt->mnt_share)) {
		if (IS_MNT_SHARED(mnt)) {
			mnt_release_group_id(mnt);
			CLEAR_MNT_SHARED(mnt);
		}
		master = mnt->mnt_master;
		if (!master) {
			struct list_head *p = &mnt->mnt_slave_list;
			while (!list_empty(p)) {
				slave_mnt = list_first_entry(p,
						struct mount, mnt_slave);
				list_del_init(&slave_mnt->mnt_slave);
				slave_mnt->mnt_master = NULL;
			}
			return 0;
		}
	} else {
		struct mount *m;
		/*
		 * slave 'mnt' to a peer mount that has the
		 * same root dentry. If none is available then
		 * slave it to anything that is available.
		 */
		for (m = master = next_peer(mnt); m != mnt; m = next_peer(m)) {
			if (m->mnt.mnt_root == mnt->mnt.mnt_root) {
				master = m;
				break;
			}
		}
		list_del_init(&mnt->mnt_share);
		mnt->mnt_group_id = 0;
		CLEAR_MNT_SHARED(mnt);
	}
	list_for_each_entry(slave_mnt, &mnt->mnt_slave_list, mnt_slave)
		slave_mnt->mnt_master = master;
	list_move(&mnt->mnt_slave, &master->mnt_slave_list);
	list_splice(&mnt->mnt_slave_list, master->mnt_slave_list.prev);
	INIT_LIST_HEAD(&mnt->mnt_slave_list);
	mnt->mnt_master = master;
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

static struct mount *next_group(struct mount *m, struct mount *origin)
{
	while (1) {
		while (1) {
			struct mount *next;
			if (!IS_MNT_NEW(m) && !list_empty(&m->mnt_slave_list))
				return first_slave(m);
			next = next_peer(m);
			if (m->mnt_group_id == origin->mnt_group_id) {
				if (next == origin)
					return NULL;
			} else if (m->mnt_slave.next != &next->mnt_slave)
				break;
			m = next;
		}
		/* m is the last peer */
		while (1) {
			struct mount *master = m->mnt_master;
			if (m->mnt_slave.next != &master->mnt_slave_list)
				return next_slave(m);
			m = next_peer(master);
			if (master->mnt_group_id == origin->mnt_group_id)
				break;
			if (master->mnt_slave.next == &m->mnt_slave)
				break;
			m = master;
		}
		if (m == origin)
			return NULL;
	}
}

/* all accesses are serialized by namespace_sem */
static struct user_namespace *user_ns;
static struct mount *last_dest, *first_source, *last_source, *dest_master;
static struct mountpoint *mp;
static struct hlist_head *list;

static inline bool peers(struct mount *m1, struct mount *m2)
{
	return m1->mnt_group_id == m2->mnt_group_id && m1->mnt_group_id;
}

static int propagate_one(struct mount *m)
{
	struct mount *child;
	int type;
	/* skip ones added by this propagate_mnt() */
	if (IS_MNT_NEW(m))
		return 0;
	/* skip if mountpoint isn't covered by it */
	if (!is_subdir(mp->m_dentry, m->mnt.mnt_root))
		return 0;
	if (peers(m, last_dest)) {
		type = CL_MAKE_SHARED;
	} else {
		struct mount *n, *p;
		bool done;
		for (n = m; ; n = p) {
			p = n->mnt_master;
			if (p == dest_master || IS_MNT_MARKED(p))
				break;
		}
		do {
			struct mount *parent = last_source->mnt_parent;
			if (last_source == first_source)
				break;
			done = parent->mnt_master == p;
			if (done && peers(n, parent))
				break;
			last_source = last_source->mnt_master;
		} while (!done);

		type = CL_SLAVE;
		/* beginning of peer group among the slaves? */
		if (IS_MNT_SHARED(m))
			type |= CL_MAKE_SHARED;
	}
		
	/* Notice when we are propagating across user namespaces */
	if (m->mnt_ns->user_ns != user_ns)
		type |= CL_UNPRIVILEGED;
	child = copy_tree(last_source, last_source->mnt.mnt_root, type);
	if (IS_ERR(child))
		return PTR_ERR(child);
	child->mnt.mnt_flags &= ~MNT_LOCKED;
	mnt_set_mountpoint(m, mp, child);
	last_dest = m;
	last_source = child;
	if (m->mnt_master != dest_master) {
		read_seqlock_excl(&mount_lock);
		SET_MNT_MARK(m->mnt_master);
		read_sequnlock_excl(&mount_lock);
	}
	hlist_add_head(&child->mnt_hash, list);
	return count_mounts(m->mnt_ns, child);
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
		    struct mount *source_mnt, struct hlist_head *tree_list)
{
	struct mount *m, *n;
	int ret = 0;

	/*
	 * we don't want to bother passing tons of arguments to
	 * propagate_one(); everything is serialized by namespace_sem,
	 * so globals will do just fine.
	 */
	user_ns = current->nsproxy->mnt_ns->user_ns;
	last_dest = dest_mnt;
	first_source = source_mnt;
	last_source = source_mnt;
	mp = dest_mp;
	list = tree_list;
	dest_master = dest_mnt->mnt_master;

	/* all peers of dest_mnt, except dest_mnt itself */
	for (n = next_peer(dest_mnt); n != dest_mnt; n = next_peer(n)) {
		ret = propagate_one(n);
		if (ret)
			goto out;
	}

	/* all slave groups */
	for (m = next_group(dest_mnt, dest_mnt); m;
			m = next_group(m, dest_mnt)) {
		/* everything in that slave group */
		n = m;
		do {
			ret = propagate_one(n);
			if (ret)
				goto out;
			n = next_peer(n);
		} while (n != m);
	}
out:
	read_seqlock_excl(&mount_lock);
	hlist_for_each_entry(n, tree_list, mnt_hash) {
		m = n->mnt_parent;
		if (m->mnt_master != dest_mnt->mnt_master)
			CLEAR_MNT_MARK(m->mnt_master);
	}
	read_sequnlock_excl(&mount_lock);
	return ret;
}

/*
 * return true if the refcount is greater than count
 */
static inline int do_refcount_check(struct mount *mnt, int count)
{
	return mnt_get_count(mnt) > count;
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
		child = __lookup_mnt_last(&m->mnt, mnt->mnt_mountpoint);
		if (child && list_empty(&child->mnt_mounts) &&
		    (ret = do_refcount_check(child, 1)))
			break;
	}
	return ret;
}

/*
 * Clear MNT_LOCKED when it can be shown to be safe.
 *
 * mount_lock lock must be held for write
 */
void propagate_mount_unlock(struct mount *mnt)
{
	struct mount *parent = mnt->mnt_parent;
	struct mount *m, *child;

	BUG_ON(parent == mnt);

	for (m = propagation_next(parent, parent); m;
			m = propagation_next(m, parent)) {
		child = __lookup_mnt_last(&m->mnt, mnt->mnt_mountpoint);
		if (child)
			child->mnt.mnt_flags &= ~MNT_LOCKED;
	}
}

/*
 * Mark all mounts that the MNT_LOCKED logic will allow to be unmounted.
 */
static void mark_umount_candidates(struct mount *mnt)
{
	struct mount *parent = mnt->mnt_parent;
	struct mount *m;

	BUG_ON(parent == mnt);

	for (m = propagation_next(parent, parent); m;
			m = propagation_next(m, parent)) {
		struct mount *child = __lookup_mnt_last(&m->mnt,
						mnt->mnt_mountpoint);
		if (child && (!IS_MNT_LOCKED(child) || IS_MNT_MARKED(m))) {
			SET_MNT_MARK(child);
		}
	}
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

		struct mount *child = __lookup_mnt_last(&m->mnt,
						mnt->mnt_mountpoint);
		/*
		 * umount the child only if the child has no children
		 * and the child is marked safe to unmount.
		 */
		if (!child || !IS_MNT_MARKED(child))
			continue;
		CLEAR_MNT_MARK(child);
		if (list_empty(&child->mnt_mounts)) {
			list_del_init(&child->mnt_child);
			child->mnt.mnt_flags |= MNT_UMOUNT;
			list_move_tail(&child->mnt_list, &mnt->mnt_list);
		}
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

	list_for_each_entry_reverse(mnt, list, mnt_list)
		mark_umount_candidates(mnt);

	list_for_each_entry(mnt, list, mnt_list)
		__propagate_umount(mnt);
	return 0;
}
