/* General netfs cache on cache files internal defs
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/fscache-cache.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/security.h>

struct cachefiles_cache;
struct cachefiles_object;

extern unsigned cachefiles_debug;
#define CACHEFILES_DEBUG_KENTER	1
#define CACHEFILES_DEBUG_KLEAVE	2
#define CACHEFILES_DEBUG_KDEBUG	4

/*
 * node records
 */
struct cachefiles_object {
	struct fscache_object		fscache;	/* fscache handle */
	struct cachefiles_lookup_data	*lookup_data;	/* cached lookup data */
	struct dentry			*dentry;	/* the file/dir representing this object */
	struct dentry			*backer;	/* backing file */
	loff_t				i_size;		/* object size */
	unsigned long			flags;
#define CACHEFILES_OBJECT_ACTIVE	0		/* T if marked active */
#define CACHEFILES_OBJECT_BURIED	1		/* T if preemptively buried */
	atomic_t			usage;		/* object usage count */
	uint8_t				type;		/* object type */
	uint8_t				new;		/* T if object new */
	spinlock_t			work_lock;
	struct rb_node			active_node;	/* link in active tree (dentry is key) */
};

extern struct kmem_cache *cachefiles_object_jar;

/*
 * Cache files cache definition
 */
struct cachefiles_cache {
	struct fscache_cache		cache;		/* FS-Cache record */
	struct vfsmount			*mnt;		/* mountpoint holding the cache */
	struct dentry			*graveyard;	/* directory into which dead objects go */
	struct file			*cachefilesd;	/* manager daemon handle */
	const struct cred		*cache_cred;	/* security override for accessing cache */
	struct mutex			daemon_mutex;	/* command serialisation mutex */
	wait_queue_head_t		daemon_pollwq;	/* poll waitqueue for daemon */
	struct rb_root			active_nodes;	/* active nodes (can't be culled) */
	rwlock_t			active_lock;	/* lock for active_nodes */
	atomic_t			gravecounter;	/* graveyard uniquifier */
	unsigned			frun_percent;	/* when to stop culling (% files) */
	unsigned			fcull_percent;	/* when to start culling (% files) */
	unsigned			fstop_percent;	/* when to stop allocating (% files) */
	unsigned			brun_percent;	/* when to stop culling (% blocks) */
	unsigned			bcull_percent;	/* when to start culling (% blocks) */
	unsigned			bstop_percent;	/* when to stop allocating (% blocks) */
	unsigned			bsize;		/* cache's block size */
	unsigned			bshift;		/* min(ilog2(PAGE_SIZE / bsize), 0) */
	uint64_t			frun;		/* when to stop culling */
	uint64_t			fcull;		/* when to start culling */
	uint64_t			fstop;		/* when to stop allocating */
	sector_t			brun;		/* when to stop culling */
	sector_t			bcull;		/* when to start culling */
	sector_t			bstop;		/* when to stop allocating */
	unsigned long			flags;
#define CACHEFILES_READY		0	/* T if cache prepared */
#define CACHEFILES_DEAD			1	/* T if cache dead */
#define CACHEFILES_CULLING		2	/* T if cull engaged */
#define CACHEFILES_STATE_CHANGED	3	/* T if state changed (poll trigger) */
	char				*rootdirname;	/* name of cache root directory */
	char				*secctx;	/* LSM security context */
	char				*tag;		/* cache binding tag */
};

/*
 * backing file read tracking
 */
struct cachefiles_one_read {
	wait_queue_t			monitor;	/* link into monitored waitqueue */
	struct page			*back_page;	/* backing file page we're waiting for */
	struct page			*netfs_page;	/* netfs page we're going to fill */
	struct fscache_retrieval	*op;		/* retrieval op covering this */
	struct list_head		op_link;	/* link in op's todo list */
};

/*
 * backing file write tracking
 */
struct cachefiles_one_write {
	struct page			*netfs_page;	/* netfs page to copy */
	struct cachefiles_object	*object;
	struct list_head		obj_link;	/* link in object's lists */
	fscache_rw_complete_t		end_io_func;
	void				*context;
};

/*
 * auxiliary data xattr buffer
 */
struct cachefiles_xattr {
	uint16_t			len;
	uint8_t				type;
	uint8_t				data[];
};

/*
 * note change of state for daemon
 */
static inline void cachefiles_state_changed(struct cachefiles_cache *cache)
{
	set_bit(CACHEFILES_STATE_CHANGED, &cache->flags);
	wake_up_all(&cache->daemon_pollwq);
}

/*
 * bind.c
 */
extern int cachefiles_daemon_bind(struct cachefiles_cache *cache, char *args);
extern void cachefiles_daemon_unbind(struct cachefiles_cache *cache);

/*
 * daemon.c
 */
extern const struct file_operations cachefiles_daemon_fops;

extern int cachefiles_has_space(struct cachefiles_cache *cache,
				unsigned fnr, unsigned bnr);

/*
 * interface.c
 */
extern const struct fscache_cache_ops cachefiles_cache_ops;

/*
 * key.c
 */
extern char *cachefiles_cook_key(const u8 *raw, int keylen, uint8_t type);

/*
 * namei.c
 */
extern int cachefiles_delete_object(struct cachefiles_cache *cache,
				    struct cachefiles_object *object);
extern int cachefiles_walk_to_object(struct cachefiles_object *parent,
				     struct cachefiles_object *object,
				     const char *key,
				     struct cachefiles_xattr *auxdata);
extern struct dentry *cachefiles_get_directory(struct cachefiles_cache *cache,
					       struct dentry *dir,
					       const char *name);

extern int cachefiles_cull(struct cachefiles_cache *cache, struct dentry *dir,
			   char *filename);

extern int cachefiles_check_in_use(struct cachefiles_cache *cache,
				   struct dentry *dir, char *filename);

/*
 * proc.c
 */
#ifdef CONFIG_CACHEFILES_HISTOGRAM
extern atomic_t cachefiles_lookup_histogram[HZ];
extern atomic_t cachefiles_mkdir_histogram[HZ];
extern atomic_t cachefiles_create_histogram[HZ];

extern int __init cachefiles_proc_init(void);
extern void cachefiles_proc_cleanup(void);
static inline
void cachefiles_hist(atomic_t histogram[], unsigned long start_jif)
{
	unsigned long jif = jiffies - start_jif;
	if (jif >= HZ)
		jif = HZ - 1;
	atomic_inc(&histogram[jif]);
}

#else
#define cachefiles_proc_init()		(0)
#define cachefiles_proc_cleanup()	do {} while (0)
#define cachefiles_hist(hist, start_jif) do {} while (0)
#endif

/*
 * rdwr.c
 */
extern int cachefiles_read_or_alloc_page(struct fscache_retrieval *,
					 struct page *, gfp_t);
extern int cachefiles_read_or_alloc_pages(struct fscache_retrieval *,
					  struct list_head *, unsigned *,
					  gfp_t);
extern int cachefiles_allocate_page(struct fscache_retrieval *, struct page *,
				    gfp_t);
extern int cachefiles_allocate_pages(struct fscache_retrieval *,
				     struct list_head *, unsigned *, gfp_t);
extern int cachefiles_write_page(struct fscache_storage *, struct page *);
extern void cachefiles_uncache_page(struct fscache_object *, struct page *);

/*
 * security.c
 */
extern int cachefiles_get_security_ID(struct cachefiles_cache *cache);
extern int cachefiles_determine_cache_security(struct cachefiles_cache *cache,
					       struct dentry *root,
					       const struct cred **_saved_cred);

static inline void cachefiles_begin_secure(struct cachefiles_cache *cache,
					   const struct cred **_saved_cred)
{
	*_saved_cred = override_creds(cache->cache_cred);
}

static inline void cachefiles_end_secure(struct cachefiles_cache *cache,
					 const struct cred *saved_cred)
{
	revert_creds(saved_cred);
}

/*
 * xattr.c
 */
extern int cachefiles_check_object_type(struct cachefiles_object *object);
extern int cachefiles_set_object_xattr(struct cachefiles_object *object,
				       struct cachefiles_xattr *auxdata);
extern int cachefiles_update_object_xattr(struct cachefiles_object *object,
					  struct cachefiles_xattr *auxdata);
extern int cachefiles_check_object_xattr(struct cachefiles_object *object,
					 struct cachefiles_xattr *auxdata);
extern int cachefiles_remove_object_xattr(struct cachefiles_cache *cache,
					  struct dentry *dentry);


/*
 * error handling
 */
#define kerror(FMT, ...) printk(KERN_ERR "CacheFiles: "FMT"\n", ##__VA_ARGS__)

#define cachefiles_io_error(___cache, FMT, ...)		\
do {							\
	kerror("I/O Error: " FMT, ##__VA_ARGS__);	\
	fscache_io_error(&(___cache)->cache);		\
	set_bit(CACHEFILES_DEAD, &(___cache)->flags);	\
} while (0)

#define cachefiles_io_error_obj(object, FMT, ...)			\
do {									\
	struct cachefiles_cache *___cache;				\
									\
	___cache = container_of((object)->fscache.cache,		\
				struct cachefiles_cache, cache);	\
	cachefiles_io_error(___cache, FMT, ##__VA_ARGS__);		\
} while (0)


/*
 * debug tracing
 */
#define dbgprintk(FMT, ...) \
	printk(KERN_DEBUG "[%-6.6s] "FMT"\n", current->comm, ##__VA_ARGS__)

/* make sure we maintain the format strings, even when debugging is disabled */
static inline void _dbprintk(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));
static inline void _dbprintk(const char *fmt, ...)
{
}

#define kenter(FMT, ...) dbgprintk("==> %s("FMT")", __func__, ##__VA_ARGS__)
#define kleave(FMT, ...) dbgprintk("<== %s()"FMT"", __func__, ##__VA_ARGS__)
#define kdebug(FMT, ...) dbgprintk(FMT, ##__VA_ARGS__)


#if defined(__KDEBUG)
#define _enter(FMT, ...) kenter(FMT, ##__VA_ARGS__)
#define _leave(FMT, ...) kleave(FMT, ##__VA_ARGS__)
#define _debug(FMT, ...) kdebug(FMT, ##__VA_ARGS__)

#elif defined(CONFIG_CACHEFILES_DEBUG)
#define _enter(FMT, ...)				\
do {							\
	if (cachefiles_debug & CACHEFILES_DEBUG_KENTER)	\
		kenter(FMT, ##__VA_ARGS__);		\
} while (0)

#define _leave(FMT, ...)				\
do {							\
	if (cachefiles_debug & CACHEFILES_DEBUG_KLEAVE)	\
		kleave(FMT, ##__VA_ARGS__);		\
} while (0)

#define _debug(FMT, ...)				\
do {							\
	if (cachefiles_debug & CACHEFILES_DEBUG_KDEBUG)	\
		kdebug(FMT, ##__VA_ARGS__);		\
} while (0)

#else
#define _enter(FMT, ...) _dbprintk("==> %s("FMT")", __func__, ##__VA_ARGS__)
#define _leave(FMT, ...) _dbprintk("<== %s()"FMT"", __func__, ##__VA_ARGS__)
#define _debug(FMT, ...) _dbprintk(FMT, ##__VA_ARGS__)
#endif

#if 1 /* defined(__KDEBUGALL) */

#define ASSERT(X)							\
do {									\
	if (unlikely(!(X))) {						\
		printk(KERN_ERR "\n");					\
		printk(KERN_ERR "CacheFiles: Assertion failed\n");	\
		BUG();							\
	}								\
} while (0)

#define ASSERTCMP(X, OP, Y)						\
do {									\
	if (unlikely(!((X) OP (Y)))) {					\
		printk(KERN_ERR "\n");					\
		printk(KERN_ERR "CacheFiles: Assertion failed\n");	\
		printk(KERN_ERR "%lx " #OP " %lx is false\n",		\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while (0)

#define ASSERTIF(C, X)							\
do {									\
	if (unlikely((C) && !(X))) {					\
		printk(KERN_ERR "\n");					\
		printk(KERN_ERR "CacheFiles: Assertion failed\n");	\
		BUG();							\
	}								\
} while (0)

#define ASSERTIFCMP(C, X, OP, Y)					\
do {									\
	if (unlikely((C) && !((X) OP (Y)))) {				\
		printk(KERN_ERR "\n");					\
		printk(KERN_ERR "CacheFiles: Assertion failed\n");	\
		printk(KERN_ERR "%lx " #OP " %lx is false\n",		\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while (0)

#else

#define ASSERT(X)			do {} while (0)
#define ASSERTCMP(X, OP, Y)		do {} while (0)
#define ASSERTIF(C, X)			do {} while (0)
#define ASSERTIFCMP(C, X, OP, Y)	do {} while (0)

#endif
