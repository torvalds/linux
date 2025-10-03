/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/ns_common.h>
#include <linux/fs_pin.h>

extern struct list_head notify_list;

struct mnt_namespace {
	struct ns_common	ns;
	struct mount *	root;
	struct {
		struct rb_root	mounts;		 /* Protected by namespace_sem */
		struct rb_node	*mnt_last_node;	 /* last (rightmost) mount in the rbtree */
		struct rb_node	*mnt_first_node; /* first (leftmost) mount in the rbtree */
	};
	struct user_namespace	*user_ns;
	struct ucounts		*ucounts;
	wait_queue_head_t	poll;
	u64			seq_origin; /* Sequence number of origin mount namespace */
	u64 event;
#ifdef CONFIG_FSNOTIFY
	__u32			n_fsnotify_mask;
	struct fsnotify_mark_connector __rcu *n_fsnotify_marks;
#endif
	unsigned int		nr_mounts; /* # of mounts in the namespace */
	unsigned int		pending_mounts;
	refcount_t		passive; /* number references not pinning @mounts */
} __randomize_layout;

struct mnt_pcp {
	int mnt_count;
	int mnt_writers;
};

struct mountpoint {
	struct hlist_node m_hash;
	struct dentry *m_dentry;
	struct hlist_head m_list;
};

struct mount {
	struct hlist_node mnt_hash;
	struct mount *mnt_parent;
	struct dentry *mnt_mountpoint;
	struct vfsmount mnt;
	union {
		struct rb_node mnt_node; /* node in the ns->mounts rbtree */
		struct rcu_head mnt_rcu;
		struct llist_node mnt_llist;
	};
#ifdef CONFIG_SMP
	struct mnt_pcp __percpu *mnt_pcp;
#else
	int mnt_count;
	int mnt_writers;
#endif
	struct list_head mnt_mounts;	/* list of children, anchored here */
	struct list_head mnt_child;	/* and going through their mnt_child */
	struct mount *mnt_next_for_sb;	/* the next two fields are hlist_node, */
	struct mount * __aligned(1) *mnt_pprev_for_sb;
					/* except that LSB of pprev is stolen */
#define WRITE_HOLD 1			/* ... for use by mnt_hold_writers() */
	const char *mnt_devname;	/* Name of device e.g. /dev/dsk/hda1 */
	struct list_head mnt_list;
	struct list_head mnt_expire;	/* link in fs-specific expiry list */
	struct list_head mnt_share;	/* circular list of shared mounts */
	struct hlist_head mnt_slave_list;/* list of slave mounts */
	struct hlist_node mnt_slave;	/* slave list entry */
	struct mount *mnt_master;	/* slave is on master->mnt_slave_list */
	struct mnt_namespace *mnt_ns;	/* containing namespace */
	struct mountpoint *mnt_mp;	/* where is it mounted */
	union {
		struct hlist_node mnt_mp_list;	/* list mounts with the same mountpoint */
		struct hlist_node mnt_umount;
	};
#ifdef CONFIG_FSNOTIFY
	struct fsnotify_mark_connector __rcu *mnt_fsnotify_marks;
	__u32 mnt_fsnotify_mask;
	struct list_head to_notify;	/* need to queue notification */
	struct mnt_namespace *prev_ns;	/* previous namespace (NULL if none) */
#endif
	int mnt_t_flags;		/* namespace_sem-protected flags */
	int mnt_id;			/* mount identifier, reused */
	u64 mnt_id_unique;		/* mount ID unique until reboot */
	int mnt_group_id;		/* peer group identifier */
	int mnt_expiry_mark;		/* true if marked for expiry */
	struct hlist_head mnt_pins;
	struct hlist_head mnt_stuck_children;
	struct mount *overmount;	/* mounted on ->mnt_root */
} __randomize_layout;

enum {
	T_SHARED		= 1, /* mount is shared */
	T_UNBINDABLE		= 2, /* mount is unbindable */
	T_MARKED		= 4, /* internal mark for propagate_... */
	T_UMOUNT_CANDIDATE	= 8, /* for propagate_umount */

	/*
	 * T_SHARED_MASK is the set of flags that should be cleared when a
	 * mount becomes shared.  Currently, this is only the flag that says a
	 * mount cannot be bind mounted, since this is how we create a mount
	 * that shares events with another mount.  If you add a new T_*
	 * flag, consider how it interacts with shared mounts.
	 */
	T_SHARED_MASK	= T_UNBINDABLE,
};

#define MNT_NS_INTERNAL ERR_PTR(-EINVAL) /* distinct from any mnt_namespace */

static inline struct mount *real_mount(struct vfsmount *mnt)
{
	return container_of(mnt, struct mount, mnt);
}

static inline int mnt_has_parent(const struct mount *mnt)
{
	return mnt != mnt->mnt_parent;
}

static inline int is_mounted(struct vfsmount *mnt)
{
	/* neither detached nor internal? */
	return !IS_ERR_OR_NULL(real_mount(mnt)->mnt_ns);
}

extern struct mount *__lookup_mnt(struct vfsmount *, struct dentry *);

extern int __legitimize_mnt(struct vfsmount *, unsigned);

static inline bool __path_is_mountpoint(const struct path *path)
{
	struct mount *m = __lookup_mnt(path->mnt, path->dentry);
	return m && likely(!(m->mnt.mnt_flags & MNT_SYNC_UMOUNT));
}

extern void __detach_mounts(struct dentry *dentry);

static inline void detach_mounts(struct dentry *dentry)
{
	if (!d_mountpoint(dentry))
		return;
	__detach_mounts(dentry);
}

static inline void get_mnt_ns(struct mnt_namespace *ns)
{
	ns_ref_inc(ns);
}

extern seqlock_t mount_lock;

DEFINE_LOCK_GUARD_0(mount_writer, write_seqlock(&mount_lock),
		    write_sequnlock(&mount_lock))
DEFINE_LOCK_GUARD_0(mount_locked_reader, read_seqlock_excl(&mount_lock),
		    read_sequnlock_excl(&mount_lock))

struct proc_mounts {
	struct mnt_namespace *ns;
	struct path root;
	int (*show)(struct seq_file *, struct vfsmount *);
};

extern const struct seq_operations mounts_op;

extern bool __is_local_mountpoint(const struct dentry *dentry);
static inline bool is_local_mountpoint(const struct dentry *dentry)
{
	if (!d_mountpoint(dentry))
		return false;

	return __is_local_mountpoint(dentry);
}

static inline bool is_anon_ns(struct mnt_namespace *ns)
{
	return ns->ns.ns_id == 0;
}

static inline bool anon_ns_root(const struct mount *m)
{
	struct mnt_namespace *ns = READ_ONCE(m->mnt_ns);

	return !IS_ERR_OR_NULL(ns) && is_anon_ns(ns) && m == ns->root;
}

static inline bool mnt_ns_attached(const struct mount *mnt)
{
	return !RB_EMPTY_NODE(&mnt->mnt_node);
}

static inline bool mnt_ns_empty(const struct mnt_namespace *ns)
{
	return RB_EMPTY_ROOT(&ns->mounts);
}

static inline void move_from_ns(struct mount *mnt)
{
	struct mnt_namespace *ns = mnt->mnt_ns;
	WARN_ON(!mnt_ns_attached(mnt));
	if (ns->mnt_last_node == &mnt->mnt_node)
		ns->mnt_last_node = rb_prev(&mnt->mnt_node);
	if (ns->mnt_first_node == &mnt->mnt_node)
		ns->mnt_first_node = rb_next(&mnt->mnt_node);
	rb_erase(&mnt->mnt_node, &ns->mounts);
	RB_CLEAR_NODE(&mnt->mnt_node);
}

bool has_locked_children(struct mount *mnt, struct dentry *dentry);
struct mnt_namespace *get_sequential_mnt_ns(struct mnt_namespace *mnt_ns,
					    bool previous);

static inline struct mnt_namespace *to_mnt_ns(struct ns_common *ns)
{
	return container_of(ns, struct mnt_namespace, ns);
}

#ifdef CONFIG_FSNOTIFY
static inline void mnt_notify_add(struct mount *m)
{
	/* Optimize the case where there are no watches */
	if ((m->mnt_ns && m->mnt_ns->n_fsnotify_marks) ||
	    (m->prev_ns && m->prev_ns->n_fsnotify_marks))
		list_add_tail(&m->to_notify, &notify_list);
	else
		m->prev_ns = m->mnt_ns;
}
#else
static inline void mnt_notify_add(struct mount *m)
{
}
#endif

static inline struct mount *topmost_overmount(struct mount *m)
{
	while (m->overmount)
		m = m->overmount;
	return m;
}

static inline bool __test_write_hold(struct mount * __aligned(1) *val)
{
	return (unsigned long)val & WRITE_HOLD;
}

static inline bool test_write_hold(const struct mount *m)
{
	return __test_write_hold(m->mnt_pprev_for_sb);
}

static inline void set_write_hold(struct mount *m)
{
	m->mnt_pprev_for_sb = (void *)((unsigned long)m->mnt_pprev_for_sb
				       | WRITE_HOLD);
}

static inline void clear_write_hold(struct mount *m)
{
	m->mnt_pprev_for_sb = (void *)((unsigned long)m->mnt_pprev_for_sb
				       & ~WRITE_HOLD);
}

struct mnt_namespace *mnt_ns_from_dentry(struct dentry *dentry);
