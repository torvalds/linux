/* SPDX-License-Identifier: GPL-2.0-or-later */
/* General netfs cache on cache files internal defs
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "CacheFiles: " fmt


#include <linux/fscache-cache.h>
#include <linux/cred.h>
#include <linux/security.h>

struct cachefiles_cache;
struct cachefiles_object;

enum cachefiles_content {
	/* These values are saved on disk */
	CACHEFILES_CONTENT_NO_DATA	= 0, /* No content stored */
	CACHEFILES_CONTENT_SINGLE	= 1, /* Content is monolithic, all is present */
	CACHEFILES_CONTENT_ALL		= 2, /* Content is all present, no map */
	CACHEFILES_CONTENT_BACKFS_MAP	= 3, /* Content is piecemeal, mapped through backing fs */
	CACHEFILES_CONTENT_DIRTY	= 4, /* Content is dirty (only seen on disk) */
	nr__cachefiles_content
};

/*
 * Cached volume representation.
 */
struct cachefiles_volume {
	struct cachefiles_cache		*cache;
	struct list_head		cache_link;	/* Link in cache->volumes */
	struct fscache_volume		*vcookie;	/* The netfs's representation */
	struct dentry			*dentry;	/* The volume dentry */
	struct dentry			*fanout[256];	/* Fanout subdirs */
};

/*
 * Backing file state.
 */
struct cachefiles_object {
	struct fscache_cookie		*cookie;	/* Netfs data storage object cookie */
	struct cachefiles_volume	*volume;	/* Cache volume that holds this object */
	struct list_head		cache_link;	/* Link in cache->*_list */
	struct file			*file;		/* The file representing this object */
	char				*d_name;	/* Backing file name */
	int				debug_id;
	spinlock_t			lock;
	refcount_t			ref;
	u8				d_name_len;	/* Length of filename */
	enum cachefiles_content		content_info:8;	/* Info about content presence */
	unsigned long			flags;
#define CACHEFILES_OBJECT_USING_TMPFILE	0		/* Have an unlinked tmpfile */
};

/*
 * Cache files cache definition
 */
struct cachefiles_cache {
	struct fscache_cache		*cache;		/* Cache cookie */
	struct vfsmount			*mnt;		/* mountpoint holding the cache */
	struct dentry			*store;		/* Directory into which live objects go */
	struct dentry			*graveyard;	/* directory into which dead objects go */
	struct file			*cachefilesd;	/* manager daemon handle */
	struct list_head		volumes;	/* List of volume objects */
	spinlock_t			object_list_lock; /* Lock for volumes and object_list */
	const struct cred		*cache_cred;	/* security override for accessing cache */
	struct mutex			daemon_mutex;	/* command serialisation mutex */
	wait_queue_head_t		daemon_pollwq;	/* poll waitqueue for daemon */
	atomic_t			gravecounter;	/* graveyard uniquifier */
	atomic_t			f_released;	/* number of objects released lately */
	atomic_long_t			b_released;	/* number of blocks released lately */
	atomic_long_t			b_writing;	/* Number of blocks being written */
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

#include <trace/events/cachefiles.h>

/*
 * note change of state for daemon
 */
static inline void cachefiles_state_changed(struct cachefiles_cache *cache)
{
	set_bit(CACHEFILES_STATE_CHANGED, &cache->flags);
	wake_up_all(&cache->daemon_pollwq);
}

/*
 * cache.c
 */
extern int cachefiles_add_cache(struct cachefiles_cache *cache);
extern int cachefiles_has_space(struct cachefiles_cache *cache,
				unsigned fnr, unsigned bnr);
extern void cachefiles_withdraw_cache(struct cachefiles_cache *cache);

/*
 * daemon.c
 */
extern const struct file_operations cachefiles_daemon_fops;

/*
 * error_inject.c
 */
#ifdef CONFIG_CACHEFILES_ERROR_INJECTION
extern unsigned int cachefiles_error_injection_state;
extern int cachefiles_register_error_injection(void);
extern void cachefiles_unregister_error_injection(void);

#else
#define cachefiles_error_injection_state 0

static inline int cachefiles_register_error_injection(void)
{
	return 0;
}

static inline void cachefiles_unregister_error_injection(void)
{
}
#endif


static inline int cachefiles_inject_read_error(void)
{
	return cachefiles_error_injection_state & 2 ? -EIO : 0;
}

static inline int cachefiles_inject_write_error(void)
{
	return cachefiles_error_injection_state & 2 ? -EIO :
		cachefiles_error_injection_state & 1 ? -ENOSPC :
		0;
}

static inline int cachefiles_inject_remove_error(void)
{
	return cachefiles_error_injection_state & 2 ? -EIO : 0;
}

/*
 * interface.c
 */
extern const struct fscache_cache_ops cachefiles_cache_ops;
extern void cachefiles_see_object(struct cachefiles_object *object,
				  enum cachefiles_obj_ref_trace why);
extern struct cachefiles_object *cachefiles_grab_object(struct cachefiles_object *object,
							enum cachefiles_obj_ref_trace why);
extern void cachefiles_put_object(struct cachefiles_object *object,
				  enum cachefiles_obj_ref_trace why);

/*
 * key.c
 */
extern bool cachefiles_cook_key(struct cachefiles_object *object);

/*
 * main.c
 */
extern struct kmem_cache *cachefiles_object_jar;

/*
 * namei.c
 */
extern void cachefiles_unmark_inode_in_use(struct cachefiles_object *object,
					   struct file *file);
extern int cachefiles_bury_object(struct cachefiles_cache *cache,
				  struct cachefiles_object *object,
				  struct dentry *dir,
				  struct dentry *rep,
				  enum fscache_why_object_killed why);
extern struct dentry *cachefiles_get_directory(struct cachefiles_cache *cache,
					       struct dentry *dir,
					       const char *name,
					       bool *_is_new);
extern void cachefiles_put_directory(struct dentry *dir);

extern int cachefiles_cull(struct cachefiles_cache *cache, struct dentry *dir,
			   char *filename);

extern int cachefiles_check_in_use(struct cachefiles_cache *cache,
				   struct dentry *dir, char *filename);

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
 * volume.c
 */
void cachefiles_acquire_volume(struct fscache_volume *volume);
void cachefiles_free_volume(struct fscache_volume *volume);
void cachefiles_withdraw_volume(struct cachefiles_volume *volume);

/*
 * xattr.c
 */
extern int cachefiles_set_object_xattr(struct cachefiles_object *object);
extern int cachefiles_check_auxdata(struct cachefiles_object *object,
				    struct file *file);
extern int cachefiles_remove_object_xattr(struct cachefiles_cache *cache,
					  struct cachefiles_object *object,
					  struct dentry *dentry);
extern void cachefiles_prepare_to_write(struct fscache_cookie *cookie);

/*
 * Error handling
 */
#define cachefiles_io_error(___cache, FMT, ...)		\
do {							\
	pr_err("I/O Error: " FMT"\n", ##__VA_ARGS__);	\
	fscache_io_error((___cache)->cache);		\
	set_bit(CACHEFILES_DEAD, &(___cache)->flags);	\
} while (0)

#define cachefiles_io_error_obj(object, FMT, ...)			\
do {									\
	struct cachefiles_cache *___cache;				\
									\
	___cache = (object)->volume->cache;				\
	cachefiles_io_error(___cache, FMT " [o=%08x]", ##__VA_ARGS__,	\
			    (object)->debug_id);			\
} while (0)


/*
 * Debug tracing
 */
extern unsigned cachefiles_debug;
#define CACHEFILES_DEBUG_KENTER	1
#define CACHEFILES_DEBUG_KLEAVE	2
#define CACHEFILES_DEBUG_KDEBUG	4

#define dbgprintk(FMT, ...) \
	printk(KERN_DEBUG "[%-6.6s] "FMT"\n", current->comm, ##__VA_ARGS__)

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
#define _enter(FMT, ...) no_printk("==> %s("FMT")", __func__, ##__VA_ARGS__)
#define _leave(FMT, ...) no_printk("<== %s()"FMT"", __func__, ##__VA_ARGS__)
#define _debug(FMT, ...) no_printk(FMT, ##__VA_ARGS__)
#endif

#if 1 /* defined(__KDEBUGALL) */

#define ASSERT(X)							\
do {									\
	if (unlikely(!(X))) {						\
		pr_err("\n");						\
		pr_err("Assertion failed\n");		\
		BUG();							\
	}								\
} while (0)

#define ASSERTCMP(X, OP, Y)						\
do {									\
	if (unlikely(!((X) OP (Y)))) {					\
		pr_err("\n");						\
		pr_err("Assertion failed\n");		\
		pr_err("%lx " #OP " %lx is false\n",			\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while (0)

#define ASSERTIF(C, X)							\
do {									\
	if (unlikely((C) && !(X))) {					\
		pr_err("\n");						\
		pr_err("Assertion failed\n");		\
		BUG();							\
	}								\
} while (0)

#define ASSERTIFCMP(C, X, OP, Y)					\
do {									\
	if (unlikely((C) && !((X) OP (Y)))) {				\
		pr_err("\n");						\
		pr_err("Assertion failed\n");		\
		pr_err("%lx " #OP " %lx is false\n",			\
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
