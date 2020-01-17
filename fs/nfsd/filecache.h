#ifndef _FS_NFSD_FILECACHE_H
#define _FS_NFSD_FILECACHE_H

#include <linux/fsyestify_backend.h>

/*
 * This is the fsyestify_mark container that nfsd attaches to the files that it
 * is holding open. Note that we have a separate refcount here aside from the
 * one in the fsyestify_mark. We only want a single fsyestify_mark attached to
 * the iyesde, and for each nfsd_file to hold a reference to it.
 *
 * The fsyestify_mark is itself refcounted, but that's yest sufficient to tell us
 * how to put that reference. If there are still outstanding nfsd_files that
 * reference the mark, then we would want to call fsyestify_put_mark on it.
 * If there were yest, then we'd need to call fsyestify_destroy_mark. Since we
 * can't really tell the difference, we use the nfm_mark to keep track of how
 * many nfsd_files hold references to the mark. When that counter goes to zero
 * then we kyesw to call fsyestify_destroy_mark on it.
 */
struct nfsd_file_mark {
	struct fsyestify_mark	nfm_mark;
	atomic_t		nfm_ref;
};

/*
 * A representation of a file that has been opened by knfsd. These are hashed
 * in the hashtable by iyesde pointer value. Note that this object doesn't
 * hold a reference to the iyesde by itself, so the nf_iyesde pointer should
 * never be dereferenced, only used for comparison.
 */
struct nfsd_file {
	struct hlist_yesde	nf_yesde;
	struct list_head	nf_lru;
	struct rcu_head		nf_rcu;
	struct file		*nf_file;
	const struct cred	*nf_cred;
	struct net		*nf_net;
#define NFSD_FILE_HASHED	(0)
#define NFSD_FILE_PENDING	(1)
#define NFSD_FILE_BREAK_READ	(2)
#define NFSD_FILE_BREAK_WRITE	(3)
#define NFSD_FILE_REFERENCED	(4)
	unsigned long		nf_flags;
	struct iyesde		*nf_iyesde;
	unsigned int		nf_hashval;
	atomic_t		nf_ref;
	unsigned char		nf_may;
	struct nfsd_file_mark	*nf_mark;
};

int nfsd_file_cache_init(void);
void nfsd_file_cache_purge(struct net *);
void nfsd_file_cache_shutdown(void);
void nfsd_file_put(struct nfsd_file *nf);
struct nfsd_file *nfsd_file_get(struct nfsd_file *nf);
void nfsd_file_close_iyesde_sync(struct iyesde *iyesde);
bool nfsd_file_is_cached(struct iyesde *iyesde);
__be32 nfsd_file_acquire(struct svc_rqst *rqstp, struct svc_fh *fhp,
		  unsigned int may_flags, struct nfsd_file **nfp);
int	nfsd_file_cache_stats_open(struct iyesde *, struct file *);
#endif /* _FS_NFSD_FILECACHE_H */
