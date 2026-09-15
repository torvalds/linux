/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hodge-podge collection of knfsd-related stuff.
 * I will sort this out later.
 *
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_NFSD_NFSD_H
#define LINUX_NFSD_NFSD_H

#include <linux/types.h>
#include <linux/mount.h>

#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svc_xprt.h>

#include <uapi/linux/nfsd/debug.h>

#undef ifdebug
#ifdef CONFIG_SUNRPC_DEBUG
# define ifdebug(flag)		if (nfsd_debug & NFSDDBG_##flag)
#else
# define ifdebug(flag)		if (0)
#endif

/*
 * nfsd version
 */
#define NFSD_MINVERS			2
#define	NFSD_MAXVERS			4
#define NFSD_SUPPORTED_MINOR_VERSION	2
bool nfsd_support_version(int vers);

/*
 * Default and maximum payload size (NFS READ or WRITE), in bytes.
 * The maximum is an implementation limit.
 */
enum {
	NFSSVC_DEFBLKSIZE       = 4 * 1024 * 1024,
	NFSSVC_MAXBLKSIZE       = RPCSVC_MAXPAYLOAD,
};

/* Maximum number of operations per session compound */
#define NFSD_MAX_OPS_PER_COMPOUND	200

extern struct svc_program	nfsd_programs[];
extern const struct svc_version	nfsd_version2, nfsd_version3, nfsd_version4;
extern struct mutex		nfsd_mutex;
extern atomic_t			nfsd_th_cnt;		/* number of available threads */

extern const struct seq_operations nfs_exports_op;

struct nfsd_thread_local_info {
	struct nfs4_client	**ntli_lease_breaker;
	int			ntli_cachetype;
};

/*
 * Common void argument and result helpers
 */
struct nfsd_voidargs { };
struct nfsd_voidres { };
bool		nfssvc_decode_voidarg(struct svc_rqst *rqstp,
				      struct xdr_stream *xdr);
bool		nfssvc_encode_voidres(struct svc_rqst *rqstp,
				      struct xdr_stream *xdr);

/*
 * Function prototypes.
 */
int		nfsd_svc(int n, int *nservers, struct net *net,
			 const struct cred *cred, const char *scope);
int		nfsd_dispatch(struct svc_rqst *rqstp);

int		nfsd_nrthreads(struct net *);
int		nfsd_nrpools(struct net *);
int		nfsd_get_nrthreads(int n, int *, struct net *);
int		nfsd_set_nrthreads(int n, int *, struct net *);
void		nfsd_shutdown_threads(struct net *net);

struct svc_rqst *nfsd_current_rqst(void);

struct nfsdfs_client {
	struct kref cl_ref;
	void (*cl_release)(struct kref *kref);
};

struct nfsd_net;

struct nfsdfs_client *get_nfsdfs_client(struct inode *);
struct dentry *nfsd_client_mkdir(struct nfsd_net *nn,
				 struct nfsdfs_client *ncl, u32 id,
				 const struct tree_descr *,
				 struct dentry **fdentries);
void nfsd_client_rmdir(struct dentry *dentry);
int nfsd_cache_notify(struct cache_detail *cd, struct cache_head *h, u32 cache_type);

#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
#ifdef CONFIG_NFSD_V2_ACL
extern const struct svc_version nfsd_acl_version2;
#else
#define nfsd_acl_version2 NULL
#endif
#ifdef CONFIG_NFSD_V3_ACL
extern const struct svc_version nfsd_acl_version3;
#else
#define nfsd_acl_version3 NULL
#endif
#endif

#if IS_ENABLED(CONFIG_NFS_LOCALIO)
extern const struct svc_version localio_version1;
#endif

enum vers_op {NFSD_SET, NFSD_CLEAR, NFSD_TEST, NFSD_AVAIL };
int nfsd_vers(struct nfsd_net *nn, int vers, enum vers_op change);
int nfsd_minorversion(struct nfsd_net *nn, u32 minorversion, enum vers_op change);
void nfsd_reset_versions(struct nfsd_net *nn);
int nfsd_create_serv(struct net *net, bool no_rpcbind);
void nfsd_destroy_serv(struct net *net);

#ifdef CONFIG_DEBUG_FS
void nfsd_debugfs_init(void);
void nfsd_debugfs_exit(void);
#else
static inline void nfsd_debugfs_init(void) {}
static inline void nfsd_debugfs_exit(void) {}
#endif

extern bool nfsd_disable_splice_read __read_mostly;
extern bool nfsd_delegts_enabled __read_mostly;

enum {
	/* Any new NFSD_IO enum value must be added at the end */
	NFSD_IO_BUFFERED,
	NFSD_IO_DONTCACHE,
	NFSD_IO_DIRECT,
};

extern u64 nfsd_io_cache_read __read_mostly;
extern u64 nfsd_io_cache_write __read_mostly;

extern int nfsd_max_blksize;

bool nfsd_v4client(struct svc_rqst *rqstp);

/*
 * lockd binding
 */
void		nfsd_lockd_init(void);
void		nfsd_lockd_shutdown(void);


#endif /* LINUX_NFSD_NFSD_H */
