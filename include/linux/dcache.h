#ifndef __LINUX_DCACHE_H
#define __LINUX_DCACHE_H

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/rculist_bl.h>
#include <linux/spinlock.h>
#include <linux/seqlock.h>
#include <linux/cache.h>
#include <linux/rcupdate.h>
#include <linux/lockref.h>

struct path;
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
 #define HASH_LEN_DECLARE u32 hash; u32 len;
 #define bytemask_from_count(cnt)	(~(~0ul << (cnt)*8))
#else
 #define HASH_LEN_DECLARE u32 len; u32 hash;
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
#define hashlen_hash(hashlen) ((u32) (hashlen))
#define hashlen_len(hashlen)  ((u32)((hashlen) >> 32))
#define hashlen_create(hash,len) (((u64)(len)<<32)|(u32)(hash))

struct dentry_stat_t {
	long nr_dentry;
	long nr_unused;
	long age_limit;          /* age in seconds */
	long want_pages;         /* pages requested by system */
	long dummy[2];
};
extern struct dentry_stat_t dentry_stat;

/* Name hashing routines. Initial hash value */
/* Hash courtesy of the R5 hash in reiserfs modulo sign bits */
#define init_name_hash()		0

/* partial hash update function. Assume roughly 4 bits per character */
static inline unsigned long
partial_name_hash(unsigned long c, unsigned long prevhash)
{
	return (prevhash + (c << 4) + (c >> 4)) * 11;
}

/*
 * Finally: cut down the number of bits to a int value (and try to avoid
 * losing bits)
 */
static inline unsigned long end_name_hash(unsigned long hash)
{
	return (unsigned int) hash;
}

/* Compute the hash for a name string. */
extern unsigned int full_name_hash(const unsigned char *, unsigned int);

/*
 * Try to keep struct dentry aligned on 64 byte cachelines (this will
 * give reasonable cacheline footprint with larger lines without the
 * large memory footprint increase).
 */
#ifdef CONFIG_64BIT
# define DNAME_INLINE_LEN 32 /* 192 bytes */
#else
# ifdef CONFIG_SMP
#  define DNAME_INLINE_LEN 36 /* 128 bytes */
# else
#  define DNAME_INLINE_LEN 40 /* 128 bytes */
# endif
#endif

#define d_lock	d_lockref.lock

struct dentry {
	/* RCU lookup touched fields */
	unsigned int d_flags;		/* protected by d_lock */
	seqcount_t d_seq;		/* per dentry seqlock */
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

	struct list_head d_lru;		/* LRU list */
	struct list_head d_child;	/* child of parent list */
	struct list_head d_subdirs;	/* our children */
	/*
	 * d_alias and d_rcu can share memory
	 */
	union {
		struct hlist_node d_alias;	/* inode alias list */
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
	int (*d_compare)(const struct dentry *, const struct dentry *,
			unsigned int, const char *, const struct qstr *);
	int (*d_delete)(const struct dentry *);
	void (*d_release)(struct dentry *);
	void (*d_prune)(struct dentry *);
	void (*d_iput)(struct dentry *, struct inode *);
	char *(*d_dname)(struct dentry *, char *, int);
	struct vfsmount *(*d_automount)(struct path *);
	int (*d_manage)(struct dentry *, bool);
} ____cacheline_aligned;

/*
 * Locking rules for dentry_operations callbacks are to be found in
 * Documentation/filesystems/Locking. Keep it updated!
 *
 * FUrther descriptions are found in Documentation/filesystems/vfs.txt.
 * Keep it updated too!
 */

/* d_flags entries */
#define DCACHE_OP_HASH			0x00000001
#define DCACHE_OP_COMPARE		0x00000002
#define DCACHE_OP_REVALIDATE		0x00000004
#define DCACHE_OP_DELETE		0x00000008
#define DCACHE_OP_PRUNE			0x00000010

#define	DCACHE_DISCONNECTED		0x00000020
     /* This dentry is possibly not currently connected to the dcache tree, in
      * which case its parent will either be itself, or will have this flag as
      * well.  nfsd will not use a dentry with this bit set, but will first
      * endeavour to clear the bit either by discovering that it is connected,
      * or by performing lookup operations.   Any filesystem which supports
      * nfsd_operations MUST have a lookup function which, if it finds a
      * directory inode with a DCACHE_DISCONNECTED dentry, will d_move that
      * dentry into place and return that dentry rather than the passed one,
      * typically using d_splice_alias. */

#define DCACHE_REFERENCED		0x00000040 /* Recently used, don't discard. */
#define DCACHE_RCUACCESS		0x00000080 /* Entry has ever been RCU-visible */

#define DCACHE_CANT_MOUNT		0x00000100
#define DCACHE_GENOCIDE			0x00000200
#define DCACHE_SHRINK_LIST		0x00000400

#define DCACHE_OP_WEAK_REVALIDATE	0x00000800

#define DCACHE_NFSFS_RENAMED		0x00001000
     /* this dentry has been "silly renamed" and has to be deleted on the last
      * dput() */
#define DCACHE_COOKIE			0x00002000 /* For use by dcookie subsystem */
#define DCACHE_FSNOTIFY_PARENT_WATCHED	0x00004000
     /* Parent inode is watched by some fsnotify listener */

#define DCACHE_DENTRY_KILLED		0x00008000

#define DCACHE_MOUNTED			0x00010000 /* is a mountpoint */
#define DCACHE_NEED_AUTOMOUNT		0x00020000 /* handle automount on this dir */
#define DCACHE_MANAGE_TRANSIT		0x00040000 /* manage transit from this dirent */
#define DCACHE_MANAGED_DENTRY \
	(DCACHE_MOUNTED|DCACHE_NEED_AUTOMOUNT|DCACHE_MANAGE_TRANSIT)

#define DCACHE_LRU_LIST			0x00080000

#define DCACHE_ENTRY_TYPE		0x00700000
#define DCACHE_MISS_TYPE		0x00000000 /* Negative dentry (maybe fallthru to nowhere) */
#define DCACHE_WHITEOUT_TYPE		0x00100000 /* Whiteout dentry (stop pathwalk) */
#define DCACHE_DIRECTORY_TYPE		0x00200000 /* Normal directory */
#define DCACHE_AUTODIR_TYPE		0x00300000 /* Lookupless directory (presumed automount) */
#define DCACHE_REGULAR_TYPE		0x00400000 /* Regular file type (or fallthru to such) */
#define DCACHE_SPECIAL_TYPE		0x00500000 /* Other file type (or fallthru to such) */
#define DCACHE_SYMLINK_TYPE		0x00600000 /* Symlink (or fallthru to such) */

#define DCACHE_MAY_FREE			0x00800000
#define DCACHE_FALLTHRU			0x01000000 /* Fall through to lower layer */

extern seqlock_t rename_lock;

/*
 * These are the low-level FS interfaces to the dcache..
 */
extern void d_instantiate(struct dentry *, struct inode *);
extern struct dentry * d_instantiate_unique(struct dentry *, struct inode *);
extern int d_instantiate_no_diralias(struct dentry *, struct inode *);
extern void __d_drop(struct dentry *dentry);
extern void d_drop(struct dentry *dentry);
extern void d_delete(struct dentry *);
extern void d_set_d_op(struct dentry *dentry, const struct dentry_operations *op);

/* allocate/de-allocate */
extern struct dentry * d_alloc(struct dentry *, const struct qstr *);
extern struct dentry * d_alloc_pseudo(struct super_block *, const struct qstr *);
extern struct dentry * d_splice_alias(struct inode *, struct dentry *);
extern struct dentry * d_add_ci(struct dentry *, struct inode *, struct qstr *);
extern struct dentry *d_find_any_alias(struct inode *inode);
extern struct dentry * d_obtain_alias(struct inode *);
extern struct dentry * d_obtain_root(struct inode *);
extern void shrink_dcache_sb(struct super_block *);
extern void shrink_dcache_parent(struct dentry *);
extern void shrink_dcache_for_umount(struct super_block *);
extern void d_invalidate(struct dentry *);

/* only used at mount-time */
extern struct dentry * d_make_root(struct inode *);

/* <clickety>-<click> the ramfs-type tree */
extern void d_genocide(struct dentry *);

extern void d_tmpfile(struct dentry *, struct inode *);

extern struct dentry *d_find_alias(struct inode *);
extern void d_prune_aliases(struct inode *);

/* test whether we have any submounts in a subdir tree */
extern int have_submounts(struct dentry *);

/*
 * This adds the entry to the hash queues.
 */
extern void d_rehash(struct dentry *);

/**
 * d_add - add dentry to hash queues
 * @entry: dentry to add
 * @inode: The inode to attach to this dentry
 *
 * This adds the entry to the hash queues and initializes @inode.
 * The entry was actually filled in earlier during d_alloc().
 */
 
static inline void d_add(struct dentry *entry, struct inode *inode)
{
	d_instantiate(entry, inode);
	d_rehash(entry);
}

/**
 * d_add_unique - add dentry to hash queues without aliasing
 * @entry: dentry to add
 * @inode: The inode to attach to this dentry
 *
 * This adds the entry to the hash queues and initializes @inode.
 * The entry was actually filled in earlier during d_alloc().
 */
static inline struct dentry *d_add_unique(struct dentry *entry, struct inode *inode)
{
	struct dentry *res;

	res = d_instantiate_unique(entry, inode);
	d_rehash(res != NULL ? res : entry);
	return res;
}

extern void dentry_update_name_case(struct dentry *, struct qstr *);

/* used for rename() and baskets */
extern void d_move(struct dentry *, struct dentry *);
extern void d_exchange(struct dentry *, struct dentry *);
extern struct dentry *d_ancestor(struct dentry *, struct dentry *);

/* appendix may either be NULL or be used for transname suffixes */
extern struct dentry *d_lookup(const struct dentry *, const struct qstr *);
extern struct dentry *d_hash_and_lookup(struct dentry *, struct qstr *);
extern struct dentry *__d_lookup(const struct dentry *, const struct qstr *);
extern struct dentry *__d_lookup_rcu(const struct dentry *parent,
				const struct qstr *name, unsigned *seq);

static inline unsigned d_count(const struct dentry *dentry)
{
	return dentry->d_lockref.count;
}

/*
 * helper function for dentry_operations.d_dname() members
 */
extern char *dynamic_dname(struct dentry *, char *, int, const char *, ...);
extern char *simple_dname(struct dentry *, char *, int);

extern char *__d_path(const struct path *, const struct path *, char *, int);
extern char *d_absolute_path(const struct path *, char *, int);
extern char *d_path(const struct path *, char *, int);
extern char *dentry_path_raw(struct dentry *, char *, int);
extern char *dentry_path(struct dentry *, char *, int);

/* Allocation counts.. */

/**
 *	dget, dget_dlock -	get a reference to a dentry
 *	@dentry: dentry to get a reference to
 *
 *	Given a dentry or %NULL pointer increment the reference count
 *	if appropriate and return the dentry. A dentry will not be 
 *	destroyed when it has references.
 */
static inline struct dentry *dget_dlock(struct dentry *dentry)
{
	if (dentry)
		dentry->d_lockref.count++;
	return dentry;
}

static inline struct dentry *dget(struct dentry *dentry)
{
	if (dentry)
		lockref_get(&dentry->d_lockref);
	return dentry;
}

extern struct dentry *dget_parent(struct dentry *dentry);

/**
 *	d_unhashed -	is dentry hashed
 *	@dentry: entry to check
 *
 *	Returns true if the dentry passed is not currently hashed.
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
static inline void __d_set_type(struct dentry *dentry, unsigned type)
{
	dentry->d_flags = (dentry->d_flags & ~DCACHE_ENTRY_TYPE) | type;
}

static inline void __d_clear_type(struct dentry *dentry)
{
	__d_set_type(dentry, DCACHE_MISS_TYPE);
}

static inline void d_set_type(struct dentry *dentry, unsigned type)
{
	spin_lock(&dentry->d_lock);
	__d_set_type(dentry, type);
	spin_unlock(&dentry->d_lock);
}

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

static inline bool d_is_positive(const struct dentry *dentry)
{
	return !d_is_negative(dentry);
}

extern void d_set_fallthru(struct dentry *dentry);

static inline bool d_is_fallthru(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_FALLTHRU;
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
 * d_inode_rcu - Get the actual inode of this dentry with ACCESS_ONCE()
 * @dentry: The dentry to query
 *
 * This is the helper normal filesystems should use to get at their own inodes
 * in their own dentries and ignore the layering superimposed upon them.
 */
static inline struct inode *d_inode_rcu(const struct dentry *dentry)
{
	return ACCESS_ONCE(dentry->d_inode);
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
 * d_backing_dentry - Get upper or lower dentry we should be using
 * @upper: The upper layer
 *
 * This is the helper that should be used to get the dentry of the inode that
 * will be used if this dentry were opened as a file.  It may be the upper
 * dentry or it may be a lower dentry pinned by the upper.
 *
 * Normal filesystems should not use this to access their own dentries.
 */
static inline struct dentry *d_backing_dentry(struct dentry *upper)
{
	return upper;
}

#endif	/* __LINUX_DCACHE_H */
