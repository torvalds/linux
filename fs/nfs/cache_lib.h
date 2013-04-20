/*
 * Helper routines for the NFS client caches
 *
 * Copyright (c) 2009 Trond Myklebust <Trond.Myklebust@netapp.com>
 */

#include <linux/completion.h>
#include <linux/sunrpc/cache.h>
#include <linux/atomic.h>

/*
 * Deferred request handling
 */
struct nfs_cache_defer_req {
	struct cache_req req;
	struct cache_deferred_req deferred_req;
	struct completion completion;
	atomic_t count;
};

extern int nfs_cache_upcall(struct cache_detail *cd, char *entry_name);
extern struct nfs_cache_defer_req *nfs_cache_defer_req_alloc(void);
extern void nfs_cache_defer_req_put(struct nfs_cache_defer_req *dreq);
extern int nfs_cache_wait_for_upcall(struct nfs_cache_defer_req *dreq);

extern int nfs_cache_register_net(struct net *net, struct cache_detail *cd);
extern void nfs_cache_unregister_net(struct net *net, struct cache_detail *cd);
extern int nfs_cache_register_sb(struct super_block *sb,
				 struct cache_detail *cd);
extern void nfs_cache_unregister_sb(struct super_block *sb,
				    struct cache_detail *cd);
