#ifndef _FS_NFSD_FILECACHE_H
#define _FS_NFSD_FILECACHE_H

#include <linux/fsnotify_backend.h>

/*
 * This is the fsnotify_mark container that nfsd attaches to the files that it
 * is holding open. Note that we have a separate refcount here aside from the
 * one in the fsnotify_mark. We only want a single fsnotify_mark attached to
 * the inode, and for each nfsd_file to hold a reference to it.
 *
 * The fsnotify_mark is itself refcounted, but that's not sufficient to tell us
 * how to put that reference. If there are still outstanding nfsd_files that
 * reference the mark, then we would want to call fsnotify_put_mark on it.
 * If there were not, then we'd need to call fsnotify_destroy_mark. Since we
 * can't really tell the difference, we use the nfm_mark to keep track of how
 * many nfsd_files hold references to the mark. When that counter goes to zero
 * then we know to call fsnotify_destroy_mark on it.
 */
struct nfsd_file_mark {
	struct fsnotify_mark	nfm_mark;
	refcount_t		nfm_ref;
};

/*
 * A representation of a file that has been opened by knfsd. These are hashed
 * in the hashtable by inode pointer value. Note that this object doesn't
 * hold a reference to the inode by itself, so the nf_inode pointer should
 * never be dereferenced, only used for comparison.
 */
struct nfsd_file {
	struct rhlist_head	nf_rlist;
	void			*nf_inode;
	struct file		*nf_file;
	const struct cred	*nf_cred;
	struct net		*nf_net;
#define NFSD_FILE_HASHED	(0)
#define NFSD_FILE_PENDING	(1)
#define NFSD_FILE_REFERENCED	(2)
#define NFSD_FILE_GC		(3)
	unsigned long		nf_flags;
	refcount_t		nf_ref;
	unsigned char		nf_may;

	struct nfsd_file_mark	*nf_mark;
	struct list_head	nf_lru;
	struct rcu_head		nf_rcu;
	ktime_t			nf_birthtime;
};

int nfsd_file_cache_init(void);
void nfsd_file_cache_purge(struct net *);
void nfsd_file_cache_shutdown(void);
int nfsd_file_cache_start_net(struct net *net);
void nfsd_file_cache_shutdown_net(struct net *net);
void nfsd_file_put(struct nfsd_file *nf);
struct nfsd_file *nfsd_file_get(struct nfsd_file *nf);
void nfsd_file_close_inode_sync(struct inode *inode);
bool nfsd_file_is_cached(struct inode *inode);
__be32 nfsd_file_acquire_gc(struct svc_rqst *rqstp, struct svc_fh *fhp,
		  unsigned int may_flags, struct nfsd_file **nfp);
__be32 nfsd_file_acquire(struct svc_rqst *rqstp, struct svc_fh *fhp,
		  unsigned int may_flags, struct nfsd_file **nfp);
__be32 nfsd_file_acquire_opened(struct svc_rqst *rqstp, struct svc_fh *fhp,
		  unsigned int may_flags, struct file *file,
		  struct nfsd_file **nfp);
int nfsd_file_cache_stats_show(struct seq_file *m, void *v);
#endif /* _FS_NFSD_FILECACHE_H */
