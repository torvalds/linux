/*
 * include/linux/sunrpc/cache.h
 *
 * Generic code for various authentication-related caches
 * used by sunrpc clients and servers.
 *
 * Copyright (C) 2002 Neil Brown <neilb@cse.unsw.edu.au>
 *
 * Released under terms in GPL version 2.  See COPYING.
 *
 */

#ifndef _LINUX_SUNRPC_CACHE_H_
#define _LINUX_SUNRPC_CACHE_H_

#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/proc_fs.h>

/*
 * Each cache requires:
 *  - A 'struct cache_detail' which contains information specific to the cache
 *    for common code to use.
 *  - An item structure that must contain a "struct cache_head"
 *  - A lookup function defined using DefineCacheLookup
 *  - A 'put' function that can release a cache item. It will only
 *    be called after cache_put has succeed, so there are guarantee
 *    to be no references.
 *  - A function to calculate a hash of an item's key.
 *
 * as well as assorted code fragments (e.g. compare keys) and numbers
 * (e.g. hash size, goal_age, etc).
 *
 * Each cache must be registered so that it can be cleaned regularly.
 * When the cache is unregistered, it is flushed completely.
 *
 * Entries have a ref count and a 'hashed' flag which counts the existence
 * in the hash table.
 * We only expire entries when refcount is zero.
 * Existence in the cache is counted  the refcount.
 */

/* Every cache item has a common header that is used
 * for expiring and refreshing entries.
 * 
 */
struct cache_head {
	struct hlist_node	cache_list;
	time_t		expiry_time;	/* After time time, don't use the data */
	time_t		last_refresh;   /* If CACHE_PENDING, this is when upcall was
					 * sent, else this is when update was
					 * received, though it is alway set to
					 * be *after* ->flush_time.
					 */
	struct kref	ref;
	unsigned long	flags;
};
#define	CACHE_VALID	0	/* Entry contains valid data */
#define	CACHE_NEGATIVE	1	/* Negative entry - there is no match for the key */
#define	CACHE_PENDING	2	/* An upcall has been sent but no reply received yet*/
#define	CACHE_CLEANED	3	/* Entry has been cleaned from cache */

#define	CACHE_NEW_EXPIRY 120	/* keep new things pending confirmation for 120 seconds */

struct cache_detail {
	struct module *		owner;
	int			hash_size;
	struct hlist_head *	hash_table;
	rwlock_t		hash_lock;

	char			*name;
	void			(*cache_put)(struct kref *);

	int			(*cache_upcall)(struct cache_detail *,
						struct cache_head *);

	void			(*cache_request)(struct cache_detail *cd,
						 struct cache_head *ch,
						 char **bpp, int *blen);

	int			(*cache_parse)(struct cache_detail *,
					       char *buf, int len);

	int			(*cache_show)(struct seq_file *m,
					      struct cache_detail *cd,
					      struct cache_head *h);
	void			(*warn_no_listener)(struct cache_detail *cd,
					      int has_died);

	struct cache_head *	(*alloc)(void);
	int			(*match)(struct cache_head *orig, struct cache_head *new);
	void			(*init)(struct cache_head *orig, struct cache_head *new);
	void			(*update)(struct cache_head *orig, struct cache_head *new);

	/* fields below this comment are for internal use
	 * and should not be touched by cache owners
	 */
	time_t			flush_time;		/* flush all cache items with
							 * last_refresh at or earlier
							 * than this.  last_refresh
							 * is never set at or earlier
							 * than this.
							 */
	struct list_head	others;
	time_t			nextcheck;
	int			entries;

	/* fields for communication over channel */
	struct list_head	queue;

	atomic_t		readers;		/* how many time is /chennel open */
	time_t			last_close;		/* if no readers, when did last close */
	time_t			last_warn;		/* when we last warned about no readers */

	union {
		struct proc_dir_entry	*procfs;
		struct dentry		*pipefs;
	};
	struct net		*net;
};


/* this must be embedded in any request structure that
 * identifies an object that will want a callback on
 * a cache fill
 */
struct cache_req {
	struct cache_deferred_req *(*defer)(struct cache_req *req);
	int thread_wait;  /* How long (jiffies) we can block the
			   * current thread to wait for updates.
			   */
};
/* this must be embedded in a deferred_request that is being
 * delayed awaiting cache-fill
 */
struct cache_deferred_req {
	struct hlist_node	hash;	/* on hash chain */
	struct list_head	recent; /* on fifo */
	struct cache_head	*item;  /* cache item we wait on */
	void			*owner; /* we might need to discard all defered requests
					 * owned by someone */
	void			(*revisit)(struct cache_deferred_req *req,
					   int too_many);
};

/*
 * timestamps kept in the cache are expressed in seconds
 * since boot.  This is the best for measuring differences in
 * real time.
 */
static inline time_t seconds_since_boot(void)
{
	struct timespec boot;
	getboottime(&boot);
	return get_seconds() - boot.tv_sec;
}

static inline time_t convert_to_wallclock(time_t sinceboot)
{
	struct timespec boot;
	getboottime(&boot);
	return boot.tv_sec + sinceboot;
}

extern const struct file_operations cache_file_operations_pipefs;
extern const struct file_operations content_file_operations_pipefs;
extern const struct file_operations cache_flush_operations_pipefs;

extern struct cache_head *
sunrpc_cache_lookup(struct cache_detail *detail,
		    struct cache_head *key, int hash);
extern struct cache_head *
sunrpc_cache_update(struct cache_detail *detail,
		    struct cache_head *new, struct cache_head *old, int hash);

extern int
sunrpc_cache_pipe_upcall(struct cache_detail *detail, struct cache_head *h);


extern void cache_clean_deferred(void *owner);

static inline struct cache_head  *cache_get(struct cache_head *h)
{
	kref_get(&h->ref);
	return h;
}


static inline void cache_put(struct cache_head *h, struct cache_detail *cd)
{
	if (kref_read(&h->ref) <= 2 &&
	    h->expiry_time < cd->nextcheck)
		cd->nextcheck = h->expiry_time;
	kref_put(&h->ref, cd->cache_put);
}

static inline bool cache_is_expired(struct cache_detail *detail, struct cache_head *h)
{
	if (!test_bit(CACHE_VALID, &h->flags))
		return false;

	return  (h->expiry_time < seconds_since_boot()) ||
		(detail->flush_time >= h->last_refresh);
}

extern int cache_check(struct cache_detail *detail,
		       struct cache_head *h, struct cache_req *rqstp);
extern void cache_flush(void);
extern void cache_purge(struct cache_detail *detail);
#define NEVER (0x7FFFFFFF)
extern void __init cache_initialize(void);
extern int cache_register_net(struct cache_detail *cd, struct net *net);
extern void cache_unregister_net(struct cache_detail *cd, struct net *net);

extern struct cache_detail *cache_create_net(struct cache_detail *tmpl, struct net *net);
extern void cache_destroy_net(struct cache_detail *cd, struct net *net);

extern void sunrpc_init_cache_detail(struct cache_detail *cd);
extern void sunrpc_destroy_cache_detail(struct cache_detail *cd);
extern int sunrpc_cache_register_pipefs(struct dentry *parent, const char *,
					umode_t, struct cache_detail *);
extern void sunrpc_cache_unregister_pipefs(struct cache_detail *);
extern void sunrpc_cache_unhash(struct cache_detail *, struct cache_head *);

/* Must store cache_detail in seq_file->private if using next three functions */
extern void *cache_seq_start(struct seq_file *file, loff_t *pos);
extern void *cache_seq_next(struct seq_file *file, void *p, loff_t *pos);
extern void cache_seq_stop(struct seq_file *file, void *p);

extern void qword_add(char **bpp, int *lp, char *str);
extern void qword_addhex(char **bpp, int *lp, char *buf, int blen);
extern int qword_get(char **bpp, char *dest, int bufsize);

static inline int get_int(char **bpp, int *anint)
{
	char buf[50];
	char *ep;
	int rv;
	int len = qword_get(bpp, buf, sizeof(buf));

	if (len < 0)
		return -EINVAL;
	if (len == 0)
		return -ENOENT;

	rv = simple_strtol(buf, &ep, 0);
	if (*ep)
		return -EINVAL;

	*anint = rv;
	return 0;
}

static inline int get_uint(char **bpp, unsigned int *anint)
{
	char buf[50];
	int len = qword_get(bpp, buf, sizeof(buf));

	if (len < 0)
		return -EINVAL;
	if (len == 0)
		return -ENOENT;

	if (kstrtouint(buf, 0, anint))
		return -EINVAL;

	return 0;
}

static inline int get_time(char **bpp, time_t *time)
{
	char buf[50];
	long long ll;
	int len = qword_get(bpp, buf, sizeof(buf));

	if (len < 0)
		return -EINVAL;
	if (len == 0)
		return -ENOENT;

	if (kstrtoll(buf, 0, &ll))
		return -EINVAL;

	*time = (time_t)ll;
	return 0;
}

static inline time_t get_expiry(char **bpp)
{
	time_t rv;
	struct timespec boot;

	if (get_time(bpp, &rv))
		return 0;
	if (rv < 0)
		return 0;
	getboottime(&boot);
	return rv - boot.tv_sec;
}

#endif /*  _LINUX_SUNRPC_CACHE_H_ */
