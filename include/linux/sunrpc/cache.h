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

#include <linux/slab.h>
#include <asm/atomic.h>
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
 * Entries have a ref count and a 'hashed' flag which counts the existance
 * in the hash table.
 * We only expire entries when refcount is zero.
 * Existance in the cache is counted  the refcount.
 */

/* Every cache item has a common header that is used
 * for expiring and refreshing entries.
 * 
 */
struct cache_head {
	struct cache_head * next;
	time_t		expiry_time;	/* After time time, don't use the data */
	time_t		last_refresh;   /* If CACHE_PENDING, this is when upcall 
					 * was sent, else this is when update was received
					 */
	atomic_t 	refcnt;
	unsigned long	flags;
};
#define	CACHE_VALID	0	/* Entry contains valid data */
#define	CACHE_NEGATIVE	1	/* Negative entry - there is no match for the key */
#define	CACHE_PENDING	2	/* An upcall has been sent but no reply received yet*/

#define	CACHE_NEW_EXPIRY 120	/* keep new things pending confirmation for 120 seconds */

struct cache_detail {
	int			hash_size;
	struct cache_head **	hash_table;
	rwlock_t		hash_lock;

	atomic_t		inuse; /* active user-space update or lookup */

	char			*name;
	void			(*cache_put)(struct cache_head *,
					     struct cache_detail*);

	void			(*cache_request)(struct cache_detail *cd,
						 struct cache_head *h,
						 char **bpp, int *blen);
	int			(*cache_parse)(struct cache_detail *,
					       char *buf, int len);

	int			(*cache_show)(struct seq_file *m,
					      struct cache_detail *cd,
					      struct cache_head *h);

	/* fields below this comment are for internal use
	 * and should not be touched by cache owners
	 */
	time_t			flush_time;		/* flush all cache items with last_refresh
							 * earlier than this */
	struct list_head	others;
	time_t			nextcheck;
	int			entries;

	/* fields for communication over channel */
	struct list_head	queue;
	struct proc_dir_entry	*proc_ent;
	struct proc_dir_entry   *flush_ent, *channel_ent, *content_ent;

	atomic_t		readers;		/* how many time is /chennel open */
	time_t			last_close;		/* if no readers, when did last close */
	time_t			last_warn;		/* when we last warned about no readers */
	void			(*warn_no_listener)(struct cache_detail *cd);
};


/* this must be embedded in any request structure that
 * identifies an object that will want a callback on
 * a cache fill
 */
struct cache_req {
	struct cache_deferred_req *(*defer)(struct cache_req *req);
};
/* this must be embedded in a deferred_request that is being
 * delayed awaiting cache-fill
 */
struct cache_deferred_req {
	struct list_head	hash;	/* on hash chain */
	struct list_head	recent; /* on fifo */
	struct cache_head	*item;  /* cache item we wait on */
	time_t			recv_time;
	void			*owner; /* we might need to discard all defered requests
					 * owned by someone */
	void			(*revisit)(struct cache_deferred_req *req,
					   int too_many);
};

/*
 * just like a template in C++, this macro does cache lookup
 * for us.
 * The function is passed some sort of HANDLE from which a cache_detail
 * structure can be determined (via SETUP, DETAIL), a template
 * cache entry (type RTN*), and a "set" flag.  Using the HASHFN and the 
 * TEST, the function will try to find a matching cache entry in the cache.
 * If "set" == 0 :
 *    If an entry is found, it is returned
 *    If no entry is found, a new non-VALID entry is created.
 * If "set" == 1 and INPLACE == 0 :
 *    If no entry is found a new one is inserted with data from "template"
 *    If a non-CACHE_VALID entry is found, it is updated from template using UPDATE
 *    If a CACHE_VALID entry is found, a new entry is swapped in with data
 *       from "template"
 * If set == 1, and INPLACE == 1 :
 *    As above, except that if a CACHE_VALID entry is found, we UPDATE in place
 *       instead of swapping in a new entry.
 *
 * If the passed handle has the CACHE_NEGATIVE flag set, then UPDATE is not
 * run but insteead CACHE_NEGATIVE is set in any new item.

 *  In any case, the new entry is returned with a reference count.
 *
 *    
 * RTN is a struct type for a cache entry
 * MEMBER is the member of the cache which is cache_head, which must be first
 * FNAME is the name for the function	
 * ARGS are arguments to function and must contain RTN *item, int set.  May
 *   also contain something to be usedby SETUP or DETAIL to find cache_detail.
 * SETUP  locates the cache detail and makes it available as...
 * DETAIL identifies the cache detail, possibly set up by SETUP
 * HASHFN returns a hash value of the cache entry "item"
 * TEST  tests if "tmp" matches "item"
 * INIT copies key information from "item" to "new"
 * UPDATE copies content information from "item" to "tmp"
 * INPLACE is true if updates can happen inplace rather than allocating a new structure
 *
 * WARNING: any substantial changes to this must be reflected in
 *   net/sunrpc/svcauth.c(auth_domain_lookup)
 *  which is a similar routine that is open-coded.
 */
#define DefineCacheLookup(RTN,MEMBER,FNAME,ARGS,SETUP,DETAIL,HASHFN,TEST,INIT,UPDATE,INPLACE)	\
RTN *FNAME ARGS										\
{											\
	RTN *tmp, *new=NULL;								\
	struct cache_head **hp, **head;							\
	SETUP;										\
	head = &(DETAIL)->hash_table[HASHFN];						\
 retry:											\
	if (set||new) write_lock(&(DETAIL)->hash_lock);					\
	else read_lock(&(DETAIL)->hash_lock);						\
	for(hp=head; *hp != NULL; hp = &tmp->MEMBER.next) {				\
		tmp = container_of(*hp, RTN, MEMBER);					\
		if (TEST) { /* found a match */						\
											\
			if (set && !INPLACE && test_bit(CACHE_VALID, &tmp->MEMBER.flags) && !new) \
				break;							\
											\
			if (new)							\
				{INIT;}							\
			if (set) {							\
				if (!INPLACE && test_bit(CACHE_VALID, &tmp->MEMBER.flags))\
				{ /* need to swap in new */				\
					RTN *t2;					\
											\
					new->MEMBER.next = tmp->MEMBER.next;		\
					*hp = &new->MEMBER;				\
					tmp->MEMBER.next = NULL;			\
					t2 = tmp; tmp = new; new = t2;			\
				}							\
				if (test_bit(CACHE_NEGATIVE,  &item->MEMBER.flags))	\
					set_bit(CACHE_NEGATIVE, &tmp->MEMBER.flags);	\
				else {							\
					UPDATE;						\
					clear_bit(CACHE_NEGATIVE, &tmp->MEMBER.flags);	\
				}							\
			}								\
			cache_get(&tmp->MEMBER);					\
			if (set||new) write_unlock(&(DETAIL)->hash_lock);		\
			else read_unlock(&(DETAIL)->hash_lock);				\
			if (set)							\
				cache_fresh(DETAIL, &tmp->MEMBER, item->MEMBER.expiry_time); \
			if (set && !INPLACE && new) cache_fresh(DETAIL, &new->MEMBER, 0);	\
			if (new) (DETAIL)->cache_put(&new->MEMBER, DETAIL);		\
			return tmp;							\
		}									\
	}										\
	/* Didn't find anything */							\
	if (new) {									\
		INIT;									\
		new->MEMBER.next = *head;						\
		*head = &new->MEMBER;							\
		(DETAIL)->entries ++;							\
		cache_get(&new->MEMBER);						\
		if (set) {								\
			tmp = new;							\
			if (test_bit(CACHE_NEGATIVE, &item->MEMBER.flags))		\
				set_bit(CACHE_NEGATIVE, &tmp->MEMBER.flags);		\
			else {UPDATE;}							\
		}									\
	}										\
	if (set||new) write_unlock(&(DETAIL)->hash_lock);				\
	else read_unlock(&(DETAIL)->hash_lock);						\
	if (new && set)									\
		cache_fresh(DETAIL, &new->MEMBER, item->MEMBER.expiry_time);		\
	if (new)				       					\
		return new;								\
	new = kmalloc(sizeof(*new), GFP_KERNEL);					\
	if (new) {									\
		cache_init(&new->MEMBER);						\
		goto retry;								\
	}										\
	return NULL;									\
}

#define DefineSimpleCacheLookup(STRUCT,INPLACE)	\
	DefineCacheLookup(struct STRUCT, h, STRUCT##_lookup, (struct STRUCT *item, int set), /*no setup */,	\
			  & STRUCT##_cache, STRUCT##_hash(item), STRUCT##_match(item, tmp),\
			  STRUCT##_init(new, item), STRUCT##_update(tmp, item),INPLACE)

#define cache_for_each(pos, detail, index, member) 						\
	for (({read_lock(&(detail)->hash_lock); index = (detail)->hash_size;}) ;		\
	     ({if (index==0)read_unlock(&(detail)->hash_lock); index--;});			\
		)										\
		for (pos = container_of((detail)->hash_table[index], typeof(*pos), member);	\
		     &pos->member;								\
		     pos = container_of(pos->member.next, typeof(*pos), member))

	     

extern void cache_clean_deferred(void *owner);

static inline struct cache_head  *cache_get(struct cache_head *h)
{
	atomic_inc(&h->refcnt);
	return h;
}


static inline int cache_put(struct cache_head *h, struct cache_detail *cd)
{
	if (atomic_read(&h->refcnt) <= 2 &&
	    h->expiry_time < cd->nextcheck)
		cd->nextcheck = h->expiry_time;
	return atomic_dec_and_test(&h->refcnt);
}

extern void cache_init(struct cache_head *h);
extern void cache_fresh(struct cache_detail *detail,
			struct cache_head *head, time_t expiry);
extern int cache_check(struct cache_detail *detail,
		       struct cache_head *h, struct cache_req *rqstp);
extern void cache_flush(void);
extern void cache_purge(struct cache_detail *detail);
#define NEVER (0x7FFFFFFF)
extern void cache_register(struct cache_detail *cd);
extern int cache_unregister(struct cache_detail *cd);

extern void qword_add(char **bpp, int *lp, char *str);
extern void qword_addhex(char **bpp, int *lp, char *buf, int blen);
extern int qword_get(char **bpp, char *dest, int bufsize);

static inline int get_int(char **bpp, int *anint)
{
	char buf[50];
	char *ep;
	int rv;
	int len = qword_get(bpp, buf, 50);
	if (len < 0) return -EINVAL;
	if (len ==0) return -ENOENT;
	rv = simple_strtol(buf, &ep, 0);
	if (*ep) return -EINVAL;
	*anint = rv;
	return 0;
}

static inline time_t get_expiry(char **bpp)
{
	int rv;
	if (get_int(bpp, &rv))
		return 0;
	if (rv < 0)
		return 0;
	return rv;
}

#endif /*  _LINUX_SUNRPC_CACHE_H_ */
