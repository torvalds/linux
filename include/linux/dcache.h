/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_DCACHE_H
#define __LINUX_DCACHE_H

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/math.h>
#include <linux/rculist.h>
#include <linux/rculist_bl.h>
#include <linux/spinlock.h>
#include <linux/seqlock.h>
#include <linux/cache.h>
#include <linux/rcupdate.h>
#include <linux/lockref.h>
#include <linux/stringhash.h>
#include <linux/wait.h>

struct path;
struct file;
struct vfsmount;

/*
 * linux/include/linux/dcache.h
 *
 * Dirent cache data structures
 *
 * (C) Copyright 1997 Thomas Schoebel-Theuer,
 * with heavy changes by Linus Torvalds
 */

#define IS_ROOT(x) ((x) == (x)->d_parent)

/* The hash is always the low bits of hash_len */
#ifdef __LITTLE_ENDIAN
 #define HASH_LEN_DECLARE u32 hash; u32 len
 #define bytemask_from_count(cnt)	(~(~0ul << (cnt)*8))
#else
 #define HASH_LEN_DECLARE u32 len; u32 hash
 #define bytemask_from_count(cnt)	(~(~0ul >> (cnt)*8))
#endif

/*
 * "quick string" -- eases parameter passing, but more importantly
 * saves "metadata" about the string (ie length and the hash).
 *
 * hash comes first so it snuggles against d_parent in the
 * dentry.
 */
struct qstr {
	union {
		struct {
			HASH_LEN_DECLARE;
		};
		u64 hash_len;
	};
	const unsigned char *name;
};

#define QSTR_INIT(n,l) { { { .len = l } }, .name = n }

extern const struct qstr empty_name;
extern const struct qstr slash_name;
extern const struct qstr dotdot_name;

/*
 * Try to keep struct dentry aligned on 64 byte cachelines (this will
 * give reasonable cacheline footprint with larger lines without the
 * large memory footprint increase).
 */
#ifdef CONFIG_64BIT
# define DNAME_INLINE_LEN 40 /* 192 bytes */
#else
# ifdef CONFIG_SMP
#  define DNAME_INLINE_LEN 40 /* 128 bytes */
# else
#  define DNAME_INLINE_LEN 44 /* 128 bytes */
# endif
#endif

#define d_lock	d_lockref.lock

struct dentry {
	/* RCU lookup touched fields */
	unsigned int d_flags;		/* protected by d_lock */
	seqcount_spinlock_t d_seq;	/* per dentry seqlock */
	struct hlist_bl_node d_hash;	/* lookup hash list */
	struct dentry *d_parent;	/* parent directory */
	struct qstr d_name;
	struct inode *d_inode;		/* Where the name belongs to - NULL is
					 * negative */
	unsigned char d_iname[DNAME_INLINE_LEN];	/* small names */

	/* Ref lookup also touches following */
	struct lockref d_lockref;	/* per-dentry lock and refcount */
	const struct dentry_operations *d_op;
	struct super_block *d_sb;	/* The root of the dentry tree */
	unsigned long d_time;		/* used by d_revalidate */
	void *d_fsdata;			/* fs-specific data */

	union {
		struct list_head d_lru;		/* LRU list */
		wait_queue_head_t *d_wait;	/* in-lookup ones only */
	};
	struct hlist_node d_sib;	/* child of parent list */
	struct hlist_head d_children;	/* our children */
	/*
	 * d_alias and d_rcu can share memory
	 */
	union {
		struct hlist_node d_alias;	/* inode alias list */
		struct hlist_bl_node d_in_lookup_hash;	/* only for in-lookup ones */
	 	struct rcu_head d_rcu;
	} d_u;
};

/*
 * dentry->d_lock spinlock nesting subclasses:
 *
 * 0: normal
 * 1: nested
 */
enum dentry_d_lock_class
{
	DENTRY_D_LOCK_NORMAL, /* implicitly used by plain spin_lock() APIs. */
	DENTRY_D_LOCK_NESTED
};

struct dentry_operations {
	int (*d_revalidate)(struct dentry *, unsigned int);
	int (*d_weak_revalidate)(struct dentry *, unsigned int);
	int (*d_hash)(const struct dentry *, struct qstr *);
	int (*d_compare)(const struct dentry *,
			unsigned int, const char *, const struct qstr *);
	int (*d_delete)(const struct dentry *);
	int (*d_init)(struct dentry *);
	void (*d_release)(struct dentry *);
	void (*d_prune)(struct dentry *);
	void (*d_iput)(struct dentry *, struct inode *);
	char *(*d_dname)(struct dentry *, char *, int);
	struct vfsmount *(*d_automount)(struct path *);
	int (*d_manage)(const struct path *, bool);
	struct dentry *(*d_real)(struct dentry *, const struct inode *);
} ____cacheline_aligned;

/*
 * Locking rules for dentry_operations callbacks are to be found in
 * Documentation/filesystems/locking.rst. Keep it updated!
 *
 * FUrther descriptions are found in Documentation/filesystems/vfs.rst.
 * Keep it updated too!
 */

/* d_flags entries */
#define DCACHE_OP_HASH			BIT(0)
#define DCACHE_OP_COMPARE		BIT(1)
#define DCACHE_OP_REVALIDATE		BIT(2)
#define DCACHE_OP_DELETE		BIT(3)
#define DCACHE_OP_PRUNE			BIT(4)

#define	DCACHE_DISCONNECTED		BIT(5)
     /* This dentry is possibly not currently connected to the dcache tree, in
      * which case its parent will either be itself, or will have this flag as
      * well.  nfsd will not use a dentry with this bit set, but will first
      * endeavour to clear the bit either by discovering that it is connected,
      * or by performing lookup operations.   Any filesystem which supports
      * nfsd_operations MUST have a lookup function which, if it finds a
      * directory inode with a DCACHE_DISCONNECTED dentry, will d_move that
      * dentry into place and return that dentry rather than the passed one,
      * typically using d_splice_alias. */

#define DCACHE_REFERENCED		BIT(6) /* Recently used, don't discard. */

#define DCACHE_DONTCACHE		BIT(7) /* Purge from memory on final dput() */

#define DCACHE_CANT_MOUNT		BIT(8)
#define DCACHE_SHRINK_LIST		BIT(10)

#define DCACHE_OP_WEAK_REVALIDATE	BIT(11)

#define DCACHE_NFSFS_RENAMED		BIT(12)
     /* this dentry has been "silly renamed" and has to be deleted on the last
      * dput() */
#define DCACHE_FSNOTIFY_PARENT_WATCHED	BIT(14)
     /* Parent inode is watched by some fsnotify listener */

#define DCACHE_DENTRY_KILLED		BIT(15)

#define DCACHE_MOUNTED			BIT(16) /* is a mountpoint */
#define DCACHE_NEED_AUTOMOUNT		BIT(17) /* handle automount on this dir */
#define DCACHE_MANAGE_TRANSIT		BIT(18) /* manage transit from this dirent */
#define DCACHE_MANAGED_DENTRY \
	(DCACHE_MOUNTED|DCACHE_NEED_AUTOMOUNT|DCACHE_MANAGE_TRANSIT)

#define DCACHE_LRU_LIST			BIT(19)

#define DCACHE_ENTRY_TYPE		(7 << 20) /* bits 20..22 are for storing type: */
#define DCACHE_MISS_TYPE		(0 << 20) /* Negative dentry */
#define DCACHE_WHITEOUT_TYPE		(1 << 20) /* Whiteout dentry (stop pathwalk) */
#define DCACHE_DIRECTORY_TYPE		(2 << 20) /* Normal directory */
#define DCACHE_AUTODIR_TYPE		(3 << 20) /* Lookupless directory (presumed automount) */
#define DCACHE_REGULAR_TYPE		(4 << 20) /* Regular file type */
#define DCACHE_SPECIAL_TYPE		(5 << 20) /* Other file type */
#define DCACHE_SYMLINK_TYPE		(6 << 20) /* Symlink */

#define DCACHE_NOKEY_NAME		BIT(25) /* Encrypted name encoded without key */
#define DCACHE_OP_REAL			BIT(26)

#define DCACHE_PAR_LOOKUP		BIT(28) /* being looked up (with parent locked shared) */
#define DCACHE_DENTRY_CURSOR		BIT(29)
#define DCACHE_NORCU			BIT(30) /* No RCU delay for freeing */

extern seqlock_t rename_lock;

/*
 * These are the low-level FS interfaces to the dcache..
 */
extern void d_instantiate(struct dentry *, struct inode *);
extern void d_instantiate_new(struct dentry *, struct inode *);
extern void __d_drop(struct dentry *dentry);
extern void d_drop(struct dentry *dentry);
extern void d_delete(struct dentry *);
extern void d_set_d_op(struct dentry *dentry, const struct dentry_operations *op);

/* allocate/de-allocate */
extern struct dentry * d_alloc(struct dentry *, const struct qstr *);
extern struct dentry * d_alloc_anon(struct super_block *);
extern struct dentry * d_alloc_parallel(struct dentry *, const struct qstr *,
					wait_queue_head_t *);
extern struct dentry * d_splice_alias(struct inode *, struct dentry *);
extern struct dentry * d_add_ci(struct dentry *, struct inode *, struct qstr *);
extern bool d_same_name(const struct dentry *dentry, const struct dentry *parent,
			const struct qstr *name);
extern struct dentry * d_exact_alias(struct dentry *, struct inode *);
extern struct dentry *d_find_any_alias(struct inode *inode);
extern struct dentry * d_obtain_alias(struct inode *);
extern struct dentry * d_obtain_root(struct inode *);
extern void shrink_dcache_sb(struct super_block *);
extern void shrink_dcache_parent(struct dentry *);
extern void d_invalidate(struct dentry *);

/* only used at mount-time */
extern struct dentry * d_make_root(struct inode *);

extern void d_mark_tmpfile(struct file *, struct inode *);
extern void d_tmpfile(struct file *, struct inode *);

extern struct dentry *d_find_alias(struct inode *);
extern void d_prune_aliases(struct inode *);

extern struct dentry *d_find_alias_rcu(struct inode *);

/* test whether we have any submounts in a subdir tree */
extern int path_has_submounts(const struct path *);

/*
 * This adds the entry to the hash queues.
 */
extern void d_rehash(struct dentry *);
 
extern void d_add(struct dentry *, struct inode *);

/* used for rename() and baskets */
extern void d_move(struct dentry *, struct dentry *);
extern void d_exchange(struct dentry *, struct dentry *);
extern struct dentry *d_ancestor(struct dentry *, struct dentry *);

extern struct dentry *d_lookup(const struct dentry *, const struct qstr *);
extern struct dentry *d_hash_and_lookup(struct dentry *, struct qstr *);

static inline unsigned d_count(const struct dentry *dentry)
{
	return dentry->d_lockref.count;
}

/*
 * helper function for dentry_operations.d_dname() members
 */
extern __printf(3, 4)
char *dynamic_dname(char *, int, const char *, ...);

extern char *__d_path(const struct path *, const struct path *, char *, int);
extern char *d_absolute_path(const struct path *, char *, int);
extern char *d_path(const struct path *, char *, int);
extern char *dentry_path_raw(const struct dentry *, char *, int);
extern char *dentry_path(const struct dentry *, char *, int);

/* Allocation counts.. */

/**
 * dget_dlock -	get a reference to a dentry
 * @dentry: dentry to get a reference to
 *
 * Given a live dentry, increment the reference count and return the dentry.
 * Caller must hold @dentry->d_lock.  Making sure that dentry is alive is
 * caller's resonsibility.  There are many conditions sufficient to guarantee
 * that; e.g. anything with non-negative refcount is alive, so's anything
 * hashed, anything positive, anyone's parent, etc.
 */
static inline struct dentry *dget_dlock(struct dentry *dentry)
{
	dentry->d_lockref.count++;
	return dentry;
}


/**
 * dget - get a reference to a dentry
 * @dentry: dentry to get a reference to
 *
 * Given a dentry or %NULL pointer increment the reference count
 * if appropriate and return the dentry.  A dentry will not be
 * destroyed when it has references.  Conversely, a dentry with
 * no references can disappear for any number of reasons, starting
 * with memory pressure.  In other words, that primitive is
 * used to clone an existing reference; using it on something with
 * zero refcount is a bug.
 *
 * NOTE: it will spin if @dentry->d_lock is held.  From the deadlock
 * avoidance point of view it is equivalent to spin_lock()/increment
 * refcount/spin_unlock(), so calling it under @dentry->d_lock is
 * always a bug; so's calling it under ->d_lock on any of its descendents.
 *
 */
static inline struct dentry *dget(struct dentry *dentry)
{
	if (dentry)
		lockref_get(&dentry->d_lockref);
	return dentry;
}

extern struct dentry *dget_parent(struct dentry *dentry);

/**
 * d_unhashed - is dentry hashed
 * @dentry: entry to check
 *
 * Returns true if the dentry passed is not currently hashed.
 */
static inline int d_unhashed(const struct dentry *dentry)
{
	return hlist_bl_unhashed(&dentry->d_hash);
}

static inline int d_unlinked(const struct dentry *dentry)
{
	return d_unhashed(dentry) && !IS_ROOT(dentry);
}

static inline int cant_mount(const struct dentry *dentry)
{
	return (dentry->d_flags & DCACHE_CANT_MOUNT);
}

static inline void dont_mount(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	dentry->d_flags |= DCACHE_CANT_MOUNT;
	spin_unlock(&dentry->d_lock);
}

extern void __d_lookup_unhash_wake(struct dentry *dentry);

static inline int d_in_lookup(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_PAR_LOOKUP;
}

static inline void d_lookup_done(struct dentry *dentry)
{
	if (unlikely(d_in_lookup(dentry)))
		__d_lookup_unhash_wake(dentry);
}

extern void dput(struct dentry *);

static inline bool d_managed(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_MANAGED_DENTRY;
}

static inline bool d_mountpoint(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_MOUNTED;
}

/*
 * Directory cache entry type accessor functions.
 */
static inline unsigned __d_entry_type(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_ENTRY_TYPE;
}

static inline bool d_is_miss(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_MISS_TYPE;
}

static inline bool d_is_whiteout(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_WHITEOUT_TYPE;
}

static inline bool d_can_lookup(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_DIRECTORY_TYPE;
}

static inline bool d_is_autodir(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_AUTODIR_TYPE;
}

static inline bool d_is_dir(const struct dentry *dentry)
{
	return d_can_lookup(dentry) || d_is_autodir(dentry);
}

static inline bool d_is_symlink(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_SYMLINK_TYPE;
}

static inline bool d_is_reg(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_REGULAR_TYPE;
}

static inline bool d_is_special(const struct dentry *dentry)
{
	return __d_entry_type(dentry) == DCACHE_SPECIAL_TYPE;
}

static inline bool d_is_file(const struct dentry *dentry)
{
	return d_is_reg(dentry) || d_is_special(dentry);
}

static inline bool d_is_negative(const struct dentry *dentry)
{
	// TODO: check d_is_whiteout(dentry) also.
	return d_is_miss(dentry);
}

static inline bool d_flags_negative(unsigned flags)
{
	return (flags & DCACHE_ENTRY_TYPE) == DCACHE_MISS_TYPE;
}

static inline bool d_is_positive(const struct dentry *dentry)
{
	return !d_is_negative(dentry);
}

/**
 * d_really_is_negative - Determine if a dentry is really negative (ignoring fallthroughs)
 * @dentry: The dentry in question
 *
 * Returns true if the dentry represents either an absent name or a name that
 * doesn't map to an inode (ie. ->d_inode is NULL).  The dentry could represent
 * a true miss, a whiteout that isn't represented by a 0,0 chardev or a
 * fallthrough marker in an opaque directory.
 *
 * Note!  (1) This should be used *only* by a filesystem to examine its own
 * dentries.  It should not be used to look at some other filesystem's
 * dentries.  (2) It should also be used in combination with d_inode() to get
 * the inode.  (3) The dentry may have something attached to ->d_lower and the
 * type field of the flags may be set to something other than miss or whiteout.
 */
static inline bool d_really_is_negative(const struct dentry *dentry)
{
	return dentry->d_inode == NULL;
}

/**
 * d_really_is_positive - Determine if a dentry is really positive (ignoring fallthroughs)
 * @dentry: The dentry in question
 *
 * Returns true if the dentry represents a name that maps to an inode
 * (ie. ->d_inode is not NULL).  The dentry might still represent a whiteout if
 * that is represented on medium as a 0,0 chardev.
 *
 * Note!  (1) This should be used *only* by a filesystem to examine its own
 * dentries.  It should not be used to look at some other filesystem's
 * dentries.  (2) It should also be used in combination with d_inode() to get
 * the inode.
 */
static inline bool d_really_is_positive(const struct dentry *dentry)
{
	return dentry->d_inode != NULL;
}

static inline int simple_positive(const struct dentry *dentry)
{
	return d_really_is_positive(dentry) && !d_unhashed(dentry);
}

extern int sysctl_vfs_cache_pressure;

static inline unsigned long vfs_pressure_ratio(unsigned long val)
{
	return mult_frac(val, sysctl_vfs_cache_pressure, 100);
}

/**
 * d_inode - Get the actual inode of this dentry
 * @dentry: The dentry to query
 *
 * This is the helper normal filesystems should use to get at their own inodes
 * in their own dentries and ignore the layering superimposed upon them.
 */
static inline struct inode *d_inode(const struct dentry *dentry)
{
	return dentry->d_inode;
}

/**
 * d_inode_rcu - Get the actual inode of this dentry with READ_ONCE()
 * @dentry: The dentry to query
 *
 * This is the helper normal filesystems should use to get at their own inodes
 * in their own dentries and ignore the layering superimposed upon them.
 */
static inline struct inode *d_inode_rcu(const struct dentry *dentry)
{
	return READ_ONCE(dentry->d_inode);
}

/**
 * d_backing_inode - Get upper or lower inode we should be using
 * @upper: The upper layer
 *
 * This is the helper that should be used to get at the inode that will be used
 * if this dentry were to be opened as a file.  The inode may be on the upper
 * dentry or it may be on a lower dentry pinned by the upper.
 *
 * Normal filesystems should not use this to access their own inodes.
 */
static inline struct inode *d_backing_inode(const struct dentry *upper)
{
	struct inode *inode = upper->d_inode;

	return inode;
}

/**
 * d_real - Return the real dentry
 * @dentry: the dentry to query
 * @inode: inode to select the dentry from multiple layers (can be NULL)
 *
 * If dentry is on a union/overlay, then return the underlying, real dentry.
 * Otherwise return the dentry itself.
 *
 * See also: Documentation/filesystems/vfs.rst
 */
static inline struct dentry *d_real(struct dentry *dentry,
				    const struct inode *inode)
{
	if (unlikely(dentry->d_flags & DCACHE_OP_REAL))
		return dentry->d_op->d_real(dentry, inode);
	else
		return dentry;
}

/**
 * d_real_inode - Return the real inode
 * @dentry: The dentry to query
 *
 * If dentry is on a union/overlay, then return the underlying, real inode.
 * Otherwise return d_inode().
 */
static inline struct inode *d_real_inode(const struct dentry *dentry)
{
	/* This usage of d_real() results in const dentry */
	return d_backing_inode(d_real((struct dentry *) dentry, NULL));
}

struct name_snapshot {
	struct qstr name;
	unsigned char inline_name[DNAME_INLINE_LEN];
};
void take_dentry_name_snapshot(struct name_snapshot *, struct dentry *);
void release_dentry_name_snapshot(struct name_snapshot *);

static inline struct dentry *d_first_child(const struct dentry *dentry)
{
	return hlist_entry_safe(dentry->d_children.first, struct dentry, d_sib);
}

static inline struct dentry *d_next_sibling(const struct dentry *dentry)
{
	return hlist_entry_safe(dentry->d_sib.next, struct dentry, d_sib);
}

#endif	/* __LINUX_DCACHE_H */
