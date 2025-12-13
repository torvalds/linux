// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/pnode.c
 *
 * (C) Copyright IBM Corporation 2005.
 *	Author : Ram Pai (linuxram@us.ibm.com)
 */
#include <linux/mnt_namespace.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include <linux/nsproxy.h>
#include <uapi/linux/mount.h>
#include "internal.h"
#include "pnode.h"

/* return the next shared peer mount of @p */
static inline struct mount *next_peer(struct mount *p)
{
	return list_entry(p->mnt_share.next, struct mount, mnt_share);
}

static inline struct mount *first_slave(struct mount *p)
{
	return hlist_entry(p->mnt_slave_list.first, struct mount, mnt_slave);
}

static inline struct mount *next_slave(struct mount *p)
{
	return hlist_entry(p->mnt_slave.next, struct mount, mnt_slave);
}

/* locks: namespace_shared && is_mounted(mnt) */
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
 * locks: namespace_shared
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

static inline bool will_be_unmounted(struct mount *m)
{
	return m->mnt.mnt_flags & MNT_UMOUNT;
}

static void transfer_propagation(struct mount *mnt, struct mount *to)
{
	struct hlist_node *p = NULL, *n;
	struct mount *m;

	hlist_for_each_entry_safe(m, n, &mnt->mnt_slave_list, mnt_slave) {
		m->mnt_master = to;
		if (!to)
			hlist_del_init(&m->mnt_slave);
		else
			p = &m->mnt_slave;
	}
	if (p)
		hlist_splice_init(&mnt->mnt_slave_list, p, &to->mnt_slave_list);
}

/*
 * EXCL[namespace_sem]
 */
void change_mnt_propagation(struct mount *mnt, int type)
{
	struct mount *m = mnt->mnt_master;

	if (type == MS_SHARED) {
		set_mnt_shared(mnt);
		return;
	}
	if (IS_MNT_SHARED(mnt)) {
		if (list_empty(&mnt->mnt_share)) {
			mnt_release_group_id(mnt);
		} else {
			m = next_peer(mnt);
			list_del_init(&mnt->mnt_share);
			mnt->mnt_group_id = 0;
		}
		CLEAR_MNT_SHARED(mnt);
		transfer_propagation(mnt, m);
	}
	hlist_del_init(&mnt->mnt_slave);
	if (type == MS_SLAVE) {
		mnt->mnt_master = m;
		if (m)
			hlist_add_head(&mnt->mnt_slave, &m->mnt_slave_list);
	} else {
		mnt->mnt_master = NULL;
		if (type == MS_UNBINDABLE)
			mnt->mnt_t_flags |= T_UNBINDABLE;
		else
			mnt->mnt_t_flags &= ~T_UNBINDABLE;
	}
}

static struct mount *trace_transfers(struct mount *m)
{
	while (1) {
		struct mount *next = next_peer(m);

		if (next != m) {
			list_del_init(&m->mnt_share);
			m->mnt_group_id = 0;
			m->mnt_master = next;
		} else {
			if (IS_MNT_SHARED(m))
				mnt_release_group_id(m);
			next = m->mnt_master;
		}
		hlist_del_init(&m->mnt_slave);
		CLEAR_MNT_SHARED(m);
		SET_MNT_MARK(m);

		if (!next || !will_be_unmounted(next))
			return next;
		if (IS_MNT_MARKED(next))
			return next->mnt_master;
		m = next;
	}
}

static void set_destinations(struct mount *m, struct mount *master)
{
	struct mount *next;

	while ((next = m->mnt_master) != master) {
		m->mnt_master = master;
		m = next;
	}
}

void bulk_make_private(struct list_head *set)
{
	struct mount *m;

	list_for_each_entry(m, set, mnt_list)
		if (!IS_MNT_MARKED(m))
			set_destinations(m, trace_transfers(m));

	list_for_each_entry(m, set, mnt_list) {
		transfer_propagation(m, m->mnt_master);
		m->mnt_master = NULL;
		CLEAR_MNT_MARK(m);
	}
}

static struct mount *__propagation_next(struct mount *m,
					 struct mount *origin)
{
	while (1) {
		struct mount *master = m->mnt_master;

		if (master == origin->mnt_master) {
			struct mount *next = next_peer(m);
			return (next == origin) ? NULL : next;
		} else if (m->mnt_slave.next)
			return next_slave(m);

		/* back at master */
		m = master;
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
	if (!IS_MNT_NEW(m) && !hlist_empty(&m->mnt_slave_list))
		return first_slave(m);

	return __propagation_next(m, origin);
}

static struct mount *skip_propagation_subtree(struct mount *m,
						struct mount *origin)
{
	/*
	 * Advance m past everything that gets propagation from it.
	 */
	struct mount *p = __propagation_next(m, origin);

	while (p && peers(m, p))
		p = __propagation_next(p, origin);

	return p;
}

static struct mount *next_group(struct mount *m, struct mount *origin)
{
	while (1) {
		while (1) {
			struct mount *next;
			if (!IS_MNT_NEW(m) && !hlist_empty(&m->mnt_slave_list))
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
			if (m->mnt_slave.next)
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

static bool need_secondary(struct mount *m, struct mountpoint *dest_mp)
{
	/* skip ones added by this propagate_mnt() */
	if (IS_MNT_NEW(m))
		return false;
	/* skip if mountpoint isn't visible in m */
	if (!is_subdir(dest_mp->m_dentry, m->mnt.mnt_root))
		return false;
	/* skip if m is in the anon_ns */
	if (is_anon_ns(m->mnt_ns))
		return false;
	return true;
}

static struct mount *find_master(struct mount *m,
				struct mount *last_copy,
				struct mount *original)
{
	struct mount *p;

	// ascend until there's a copy for something with the same master
	for (;;) {
		p = m->mnt_master;
		if (!p || IS_MNT_MARKED(p))
			break;
		m = p;
	}
	while (!peers(last_copy, original)) {
		struct mount *parent = last_copy->mnt_parent;
		if (parent->mnt_master == p) {
			if (!peers(parent, m))
				last_copy = last_copy->mnt_master;
			break;
		}
		last_copy = last_copy->mnt_master;
	}
	return last_copy;
}

/**
 * propagate_mnt() - create secondary copies for tree attachment
 * @dest_mnt:    destination mount.
 * @dest_mp:     destination mountpoint.
 * @source_mnt:  source mount.
 * @tree_list:   list of secondaries to be attached.
 *
 * Create secondary copies for attaching a tree with root @source_mnt
 * at mount @dest_mnt with mountpoint @dest_mp.  Link all new mounts
 * into a propagation graph.  Set mountpoints for all secondaries,
 * link their roots into @tree_list via ->mnt_hash.
 */
int propagate_mnt(struct mount *dest_mnt, struct mountpoint *dest_mp,
		  struct mount *source_mnt, struct hlist_head *tree_list)
{
	struct mount *m, *n, *copy, *this;
	int err = 0, type;

	if (dest_mnt->mnt_master)
		SET_MNT_MARK(dest_mnt->mnt_master);

	/* iterate over peer groups, depth first */
	for (m = dest_mnt; m && !err; m = next_group(m, dest_mnt)) {
		if (m == dest_mnt) { // have one for dest_mnt itself
			copy = source_mnt;
			type = CL_MAKE_SHARED;
			n = next_peer(m);
			if (n == m)
				continue;
		} else {
			type = CL_SLAVE;
			/* beginning of peer group among the slaves? */
			if (IS_MNT_SHARED(m))
				type |= CL_MAKE_SHARED;
			n = m;
		}
		do {
			if (!need_secondary(n, dest_mp))
				continue;
			if (type & CL_SLAVE) // first in this peer group
				copy = find_master(n, copy, source_mnt);
			this = copy_tree(copy, copy->mnt.mnt_root, type);
			if (IS_ERR(this)) {
				err = PTR_ERR(this);
				break;
			}
			scoped_guard(mount_locked_reader)
				mnt_set_mountpoint(n, dest_mp, this);
			if (n->mnt_master)
				SET_MNT_MARK(n->mnt_master);
			copy = this;
			hlist_add_head(&this->mnt_hash, tree_list);
			err = count_mounts(n->mnt_ns, this);
			if (err)
				break;
			type = CL_MAKE_SHARED;
		} while ((n = next_peer(n)) != m);
	}

	hlist_for_each_entry(n, tree_list, mnt_hash) {
		m = n->mnt_parent;
		if (m->mnt_master)
			CLEAR_MNT_MARK(m->mnt_master);
	}
	if (dest_mnt->mnt_master)
		CLEAR_MNT_MARK(dest_mnt->mnt_master);
	return err;
}

/*
 * return true if the refcount is greater than count
 */
static inline int do_refcount_check(struct mount *mnt, int count)
{
	return mnt_get_count(mnt) > count;
}

/**
 * propagation_would_overmount - check whether propagation from @from
 *                               would overmount @to
 * @from: shared mount
 * @to:   mount to check
 * @mp:   future mountpoint of @to on @from
 *
 * If @from propagates mounts to @to, @from and @to must either be peers
 * or one of the masters in the hierarchy of masters of @to must be a
 * peer of @from.
 *
 * If the root of the @to mount is equal to the future mountpoint @mp of
 * the @to mount on @from then @to will be overmounted by whatever is
 * propagated to it.
 *
 * Context: This function expects namespace_lock() to be held and that
 *          @mp is stable.
 * Return: If @from overmounts @to, true is returned, false if not.
 */
bool propagation_would_overmount(const struct mount *from,
				 const struct mount *to,
				 const struct mountpoint *mp)
{
	if (!IS_MNT_SHARED(from))
		return false;

	if (to->mnt.mnt_root != mp->m_dentry)
		return false;

	for (const struct mount *m = to; m; m = m->mnt_master) {
		if (peers(from, m))
			return true;
	}

	return false;
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
	struct mount *parent = mnt->mnt_parent;

	/*
	 * quickly check if the current mount can be unmounted.
	 * If not, we don't have to go checking for all other
	 * mounts
	 */
	if (!list_empty(&mnt->mnt_mounts) || do_refcount_check(mnt, refcnt))
		return 1;

	if (mnt == parent)
		return 0;

	for (struct mount *m = propagation_next(parent, parent); m;
	     		m = propagation_next(m, parent)) {
		struct list_head *head;
		struct mount *child = __lookup_mnt(&m->mnt, mnt->mnt_mountpoint);

		if (!child)
			continue;

		head = &child->mnt_mounts;
		if (!list_empty(head)) {
			/*
			 * a mount that covers child completely wouldn't prevent
			 * it being pulled out; any other would.
			 */
			if (!list_is_singular(head) || !child->overmount)
				continue;
		}
		if (do_refcount_check(child, 1))
			return 1;
	}
	return 0;
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
		child = __lookup_mnt(&m->mnt, mnt->mnt_mountpoint);
		if (child)
			child->mnt.mnt_flags &= ~MNT_LOCKED;
	}
}

static inline bool is_candidate(struct mount *m)
{
	return m->mnt_t_flags & T_UMOUNT_CANDIDATE;
}

static void umount_one(struct mount *m, struct list_head *to_umount)
{
	m->mnt.mnt_flags |= MNT_UMOUNT;
	list_del_init(&m->mnt_child);
	move_from_ns(m);
	list_add_tail(&m->mnt_list, to_umount);
}

static void remove_from_candidate_list(struct mount *m)
{
	m->mnt_t_flags &= ~(T_MARKED | T_UMOUNT_CANDIDATE);
	list_del_init(&m->mnt_list);
}

static void gather_candidates(struct list_head *set,
			      struct list_head *candidates)
{
	struct mount *m, *p, *q;

	list_for_each_entry(m, set, mnt_list) {
		if (is_candidate(m))
			continue;
		m->mnt_t_flags |= T_UMOUNT_CANDIDATE;
		p = m->mnt_parent;
		q = propagation_next(p, p);
		while (q) {
			struct mount *child = __lookup_mnt(&q->mnt,
							   m->mnt_mountpoint);
			if (child) {
				/*
				 * We might've already run into this one.  That
				 * must've happened on earlier iteration of the
				 * outer loop; in that case we can skip those
				 * parents that get propagation from q - there
				 * will be nothing new on those as well.
				 */
				if (is_candidate(child)) {
					q = skip_propagation_subtree(q, p);
					continue;
				}
				child->mnt_t_flags |= T_UMOUNT_CANDIDATE;
				if (!will_be_unmounted(child))
					list_add(&child->mnt_list, candidates);
			}
			q = propagation_next(q, p);
		}
	}
	list_for_each_entry(m, set, mnt_list)
		m->mnt_t_flags &= ~T_UMOUNT_CANDIDATE;
}

/*
 * We know that some child of @m can't be unmounted.  In all places where the
 * chain of descent of @m has child not overmounting the root of parent,
 * the parent can't be unmounted either.
 */
static void trim_ancestors(struct mount *m)
{
	struct mount *p;

	for (p = m->mnt_parent; is_candidate(p); m = p, p = p->mnt_parent) {
		if (IS_MNT_MARKED(m))	// all candidates beneath are overmounts
			return;
		SET_MNT_MARK(m);
		if (m != p->overmount)
			p->mnt_t_flags &= ~T_UMOUNT_CANDIDATE;
	}
}

/*
 * Find and exclude all umount candidates forbidden by @m
 * (see Documentation/filesystems/propagate_umount.txt)
 * If we can immediately tell that @m is OK to unmount (unlocked
 * and all children are already committed to unmounting) commit
 * to unmounting it.
 * Only @m itself might be taken from the candidates list;
 * anything found by trim_ancestors() is marked non-candidate
 * and left on the list.
 */
static void trim_one(struct mount *m, struct list_head *to_umount)
{
	bool remove_this = false, found = false, umount_this = false;
	struct mount *n;

	if (!is_candidate(m)) { // trim_ancestors() left it on list
		remove_from_candidate_list(m);
		return;
	}

	list_for_each_entry(n, &m->mnt_mounts, mnt_child) {
		if (!is_candidate(n)) {
			found = true;
			if (n != m->overmount) {
				remove_this = true;
				break;
			}
		}
	}
	if (found) {
		trim_ancestors(m);
	} else if (!IS_MNT_LOCKED(m) && list_empty(&m->mnt_mounts)) {
		remove_this = true;
		umount_this = true;
	}
	if (remove_this) {
		remove_from_candidate_list(m);
		if (umount_this)
			umount_one(m, to_umount);
	}
}

static void handle_locked(struct mount *m, struct list_head *to_umount)
{
	struct mount *cutoff = m, *p;

	if (!is_candidate(m)) { // trim_ancestors() left it on list
		remove_from_candidate_list(m);
		return;
	}
	for (p = m; is_candidate(p); p = p->mnt_parent) {
		remove_from_candidate_list(p);
		if (!IS_MNT_LOCKED(p))
			cutoff = p->mnt_parent;
	}
	if (will_be_unmounted(p))
		cutoff = p;
	while (m != cutoff) {
		umount_one(m, to_umount);
		m = m->mnt_parent;
	}
}

/*
 * @m is not to going away, and it overmounts the top of a stack of mounts
 * that are going away.  We know that all of those are fully overmounted
 * by the one above (@m being the topmost of the chain), so @m can be slid
 * in place where the bottom of the stack is attached.
 *
 * NOTE: here we temporarily violate a constraint - two mounts end up with
 * the same parent and mountpoint; that will be remedied as soon as we
 * return from propagate_umount() - its caller (umount_tree()) will detach
 * the stack from the parent it (and now @m) is attached to.  umount_tree()
 * might choose to keep unmounted pieces stuck to each other, but it always
 * detaches them from the mounts that remain in the tree.
 */
static void reparent(struct mount *m)
{
	struct mount *p = m;
	struct mountpoint *mp;

	do {
		mp = p->mnt_mp;
		p = p->mnt_parent;
	} while (will_be_unmounted(p));

	mnt_change_mountpoint(p, mp, m);
	mnt_notify_add(m);
}

/**
 * propagate_umount - apply propagation rules to the set of mounts for umount()
 * @set: the list of mounts to be unmounted.
 *
 * Collect all mounts that receive propagation from the mount in @set and have
 * no obstacles to being unmounted.  Add these additional mounts to the set.
 *
 * See Documentation/filesystems/propagate_umount.txt if you do anything in
 * this area.
 *
 * Locks held:
 * mount_lock (write_seqlock), namespace_sem (exclusive).
 */
void propagate_umount(struct list_head *set)
{
	struct mount *m, *p;
	LIST_HEAD(to_umount);	// committed to unmounting
	LIST_HEAD(candidates);	// undecided umount candidates

	// collect all candidates
	gather_candidates(set, &candidates);

	// reduce the set until it's non-shifting
	list_for_each_entry_safe(m, p, &candidates, mnt_list)
		trim_one(m, &to_umount);

	// ... and non-revealing
	while (!list_empty(&candidates)) {
		m = list_first_entry(&candidates,struct mount, mnt_list);
		handle_locked(m, &to_umount);
	}

	// now to_umount consists of all acceptable candidates
	// deal with reparenting of surviving overmounts on those
	list_for_each_entry(m, &to_umount, mnt_list) {
		struct mount *over = m->overmount;
		if (over && !will_be_unmounted(over))
			reparent(over);
	}

	// and fold them into the set
	list_splice_tail_init(&to_umount, set);
}
