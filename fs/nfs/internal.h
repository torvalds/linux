/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NFS internal definitions
 */

#include "nfs4_fs.h"
#include <linux/fs_context.h>
#include <linux/security.h>
#include <linux/crc32.h>
#include <linux/sunrpc/addr.h>
#include <linux/nfs_page.h>
#include <linux/wait_bit.h>

#define NFS_SB_MASK (SB_RDONLY|SB_NOSUID|SB_NODEV|SB_NOEXEC|SB_SYNCHRONOUS)

extern const struct export_operations nfs_export_ops;

struct nfs_string;
struct nfs_pageio_descriptor;

static inline void nfs_attr_check_mountpoint(struct super_block *parent, struct nfs_fattr *fattr)
{
	if (!nfs_fsid_equal(&NFS_SB(parent)->fsid, &fattr->fsid))
		fattr->valid |= NFS_ATTR_FATTR_MOUNTPOINT;
}

static inline int nfs_attr_use_mounted_on_fileid(struct nfs_fattr *fattr)
{
	if (((fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID) == 0) ||
	    (((fattr->valid & NFS_ATTR_FATTR_MOUNTPOINT) == 0) &&
	     ((fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL) == 0)))
		return 0;
	return 1;
}

static inline bool nfs_lookup_is_soft_revalidate(const struct dentry *dentry)
{
	if (!(NFS_SB(dentry->d_sb)->flags & NFS_MOUNT_SOFTREVAL))
		return false;
	if (!d_is_positive(dentry) || !NFS_FH(d_inode(dentry))->size)
		return false;
	return true;
}

static inline fmode_t flags_to_mode(int flags)
{
	fmode_t res = (__force fmode_t)flags & FMODE_EXEC;
	if ((flags & O_ACCMODE) != O_WRONLY)
		res |= FMODE_READ;
	if ((flags & O_ACCMODE) != O_RDONLY)
		res |= FMODE_WRITE;
	return res;
}

/*
 * Note: RFC 1813 doesn't limit the number of auth flavors that
 * a server can return, so make something up.
 */
#define NFS_MAX_SECFLAVORS	(12)

/*
 * Value used if the user did not specify a port value.
 */
#define NFS_UNSPEC_PORT		(-1)

#define NFS_UNSPEC_RETRANS	(UINT_MAX)
#define NFS_UNSPEC_TIMEO	(UINT_MAX)

struct nfs_client_initdata {
	unsigned long init_flags;
	const char *hostname;			/* Hostname of the server */
	const struct sockaddr_storage *addr;	/* Address of the server */
	const char *nodename;			/* Hostname of the client */
	const char *ip_addr;			/* IP address of the client */
	size_t addrlen;
	struct nfs_subversion *nfs_mod;
	int proto;
	u32 minorversion;
	unsigned int nconnect;
	unsigned int max_connect;
	struct net *net;
	const struct rpc_timeout *timeparms;
	const struct cred *cred;
	struct xprtsec_parms xprtsec;
	unsigned long connect_timeout;
	unsigned long reconnect_timeout;
};

/*
 * In-kernel mount arguments
 */
struct nfs_fs_context {
	bool			internal;
	bool			skip_reconfig_option_check;
	bool			need_mount;
	bool			sloppy;
	unsigned int		flags;		/* NFS{,4}_MOUNT_* flags */
	unsigned int		rsize, wsize;
	unsigned int		timeo, retrans;
	unsigned int		acregmin, acregmax;
	unsigned int		acdirmin, acdirmax;
	unsigned int		namlen;
	unsigned int		options;
	unsigned int		bsize;
	struct nfs_auth_info	auth_info;
	rpc_authflavor_t	selected_flavor;
	struct xprtsec_parms	xprtsec;
	char			*client_address;
	unsigned int		version;
	unsigned int		minorversion;
	char			*fscache_uniq;
	unsigned short		protofamily;
	unsigned short		mountfamily;
	bool			has_sec_mnt_opts;

	struct {
		union {
			struct sockaddr	address;
			struct sockaddr_storage	_address;
		};
		size_t			addrlen;
		char			*hostname;
		u32			version;
		int			port;
		unsigned short		protocol;
	} mount_server;

	struct {
		union {
			struct sockaddr	address;
			struct sockaddr_storage	_address;
		};
		size_t			addrlen;
		char			*hostname;
		char			*export_path;
		int			port;
		unsigned short		protocol;
		unsigned short		nconnect;
		unsigned short		max_connect;
		unsigned short		export_path_len;
	} nfs_server;

	struct nfs_fh		*mntfh;
	struct nfs_server	*server;
	struct nfs_subversion	*nfs_mod;

	/* Information for a cloned mount. */
	struct nfs_clone_mount {
		struct super_block	*sb;
		struct dentry		*dentry;
		struct nfs_fattr	*fattr;
		unsigned int		inherited_bsize;
	} clone_data;
};

#define nfs_errorf(fc, fmt, ...) ((fc)->log.log ?		\
	errorf(fc, fmt, ## __VA_ARGS__) :			\
	({ dprintk(fmt "\n", ## __VA_ARGS__); }))

#define nfs_ferrorf(fc, fac, fmt, ...) ((fc)->log.log ?		\
	errorf(fc, fmt, ## __VA_ARGS__) :			\
	({ dfprintk(fac, fmt "\n", ## __VA_ARGS__); }))

#define nfs_invalf(fc, fmt, ...) ((fc)->log.log ?		\
	invalf(fc, fmt, ## __VA_ARGS__) :			\
	({ dprintk(fmt "\n", ## __VA_ARGS__);  -EINVAL; }))

#define nfs_finvalf(fc, fac, fmt, ...) ((fc)->log.log ?		\
	invalf(fc, fmt, ## __VA_ARGS__) :			\
	({ dfprintk(fac, fmt "\n", ## __VA_ARGS__);  -EINVAL; }))

#define nfs_warnf(fc, fmt, ...) ((fc)->log.log ?		\
	warnf(fc, fmt, ## __VA_ARGS__) :			\
	({ dprintk(fmt "\n", ## __VA_ARGS__); }))

#define nfs_fwarnf(fc, fac, fmt, ...) ((fc)->log.log ?		\
	warnf(fc, fmt, ## __VA_ARGS__) :			\
	({ dfprintk(fac, fmt "\n", ## __VA_ARGS__); }))

static inline struct nfs_fs_context *nfs_fc2context(const struct fs_context *fc)
{
	return fc->fs_private;
}

/* mount_clnt.c */
struct nfs_mount_request {
	struct sockaddr_storage	*sap;
	size_t			salen;
	char			*hostname;
	char			*dirpath;
	u32			version;
	unsigned short		protocol;
	struct nfs_fh		*fh;
	int			noresvport;
	unsigned int		*auth_flav_len;
	rpc_authflavor_t	*auth_flavs;
	struct net		*net;
};

extern int nfs_mount(struct nfs_mount_request *info, int timeo, int retrans);
extern void nfs_umount(const struct nfs_mount_request *info);

/* client.c */
extern const struct rpc_program nfs_program;
extern void nfs_clients_init(struct net *net);
extern void nfs_clients_exit(struct net *net);
extern struct nfs_client *nfs_alloc_client(const struct nfs_client_initdata *);
int nfs_create_rpc_client(struct nfs_client *, const struct nfs_client_initdata *, rpc_authflavor_t);
struct nfs_client *nfs_get_client(const struct nfs_client_initdata *);
int nfs_probe_server(struct nfs_server *, struct nfs_fh *);
void nfs_server_insert_lists(struct nfs_server *);
void nfs_server_remove_lists(struct nfs_server *);
void nfs_init_timeout_values(struct rpc_timeout *to, int proto, int timeo, int retrans);
int nfs_init_server_rpcclient(struct nfs_server *, const struct rpc_timeout *t,
		rpc_authflavor_t);
struct nfs_server *nfs_alloc_server(void);
void nfs_server_copy_userdata(struct nfs_server *, struct nfs_server *);

extern void nfs_put_client(struct nfs_client *);
extern void nfs_free_client(struct nfs_client *);
extern struct nfs_client *nfs4_find_client_ident(struct net *, int);
extern struct nfs_client *
nfs4_find_client_sessionid(struct net *, const struct sockaddr *,
				struct nfs4_sessionid *, u32);
extern struct nfs_server *nfs_create_server(struct fs_context *);
extern void nfs4_server_set_init_caps(struct nfs_server *);
extern struct nfs_server *nfs4_create_server(struct fs_context *);
extern struct nfs_server *nfs4_create_referral_server(struct fs_context *);
extern int nfs4_update_server(struct nfs_server *server, const char *hostname,
					struct sockaddr_storage *sap, size_t salen,
					struct net *net);
extern void nfs_free_server(struct nfs_server *server);
extern struct nfs_server *nfs_clone_server(struct nfs_server *,
					   struct nfs_fh *,
					   struct nfs_fattr *,
					   rpc_authflavor_t);
extern bool nfs_client_init_is_complete(const struct nfs_client *clp);
extern int nfs_client_init_status(const struct nfs_client *clp);
extern int nfs_wait_client_init_complete(const struct nfs_client *clp);
extern void nfs_mark_client_ready(struct nfs_client *clp, int state);
extern struct nfs_client *nfs4_set_ds_client(struct nfs_server *mds_srv,
					     const struct sockaddr_storage *ds_addr,
					     int ds_addrlen, int ds_proto,
					     unsigned int ds_timeo,
					     unsigned int ds_retrans,
					     u32 minor_version);
extern struct rpc_clnt *nfs4_find_or_create_ds_client(struct nfs_client *,
						struct inode *);
extern struct nfs_client *nfs3_set_ds_client(struct nfs_server *mds_srv,
			const struct sockaddr_storage *ds_addr, int ds_addrlen,
			int ds_proto, unsigned int ds_timeo,
			unsigned int ds_retrans);
#ifdef CONFIG_PROC_FS
extern int __init nfs_fs_proc_init(void);
extern void nfs_fs_proc_exit(void);
extern int nfs_fs_proc_net_init(struct net *net);
extern void nfs_fs_proc_net_exit(struct net *net);
#else
static inline int nfs_fs_proc_net_init(struct net *net)
{
	return 0;
}
static inline void nfs_fs_proc_net_exit(struct net *net)
{
}
static inline int nfs_fs_proc_init(void)
{
	return 0;
}
static inline void nfs_fs_proc_exit(void)
{
}
#endif

/* callback_xdr.c */
extern const struct svc_version nfs4_callback_version1;
extern const struct svc_version nfs4_callback_version4;

/* fs_context.c */
extern struct file_system_type nfs_fs_type;

/* pagelist.c */
extern int __init nfs_init_nfspagecache(void);
extern void nfs_destroy_nfspagecache(void);
extern int __init nfs_init_readpagecache(void);
extern void nfs_destroy_readpagecache(void);
extern int __init nfs_init_writepagecache(void);
extern void nfs_destroy_writepagecache(void);

extern int __init nfs_init_directcache(void);
extern void nfs_destroy_directcache(void);
extern void nfs_pgheader_init(struct nfs_pageio_descriptor *desc,
			      struct nfs_pgio_header *hdr,
			      void (*release)(struct nfs_pgio_header *hdr));
void nfs_set_pgio_error(struct nfs_pgio_header *hdr, int error, loff_t pos);
int nfs_iocounter_wait(struct nfs_lock_context *l_ctx);

extern const struct nfs_pageio_ops nfs_pgio_rw_ops;
struct nfs_pgio_header *nfs_pgio_header_alloc(const struct nfs_rw_ops *);
void nfs_pgio_header_free(struct nfs_pgio_header *);
int nfs_generic_pgio(struct nfs_pageio_descriptor *, struct nfs_pgio_header *);
int nfs_initiate_pgio(struct rpc_clnt *clnt, struct nfs_pgio_header *hdr,
		      const struct cred *cred, const struct nfs_rpc_ops *rpc_ops,
		      const struct rpc_call_ops *call_ops, int how, int flags);
void nfs_free_request(struct nfs_page *req);
struct nfs_pgio_mirror *
nfs_pgio_current_mirror(struct nfs_pageio_descriptor *desc);

static inline bool nfs_match_open_context(const struct nfs_open_context *ctx1,
		const struct nfs_open_context *ctx2)
{
	return cred_fscmp(ctx1->cred, ctx2->cred) == 0 && ctx1->state == ctx2->state;
}

/* nfs2xdr.c */
extern const struct rpc_procinfo nfs_procedures[];
extern int nfs2_decode_dirent(struct xdr_stream *,
				struct nfs_entry *, bool);

/* nfs3xdr.c */
extern const struct rpc_procinfo nfs3_procedures[];
extern int nfs3_decode_dirent(struct xdr_stream *,
				struct nfs_entry *, bool);

/* nfs4xdr.c */
#if IS_ENABLED(CONFIG_NFS_V4)
extern int nfs4_decode_dirent(struct xdr_stream *,
				struct nfs_entry *, bool);
#endif
#ifdef CONFIG_NFS_V4_1
extern const u32 nfs41_maxread_overhead;
extern const u32 nfs41_maxwrite_overhead;
extern const u32 nfs41_maxgetdevinfo_overhead;
#endif

/* nfs4proc.c */
#if IS_ENABLED(CONFIG_NFS_V4)
extern const struct rpc_procinfo nfs4_procedures[];
#endif

#ifdef CONFIG_NFS_V4_SECURITY_LABEL
extern struct nfs4_label *nfs4_label_alloc(struct nfs_server *server, gfp_t flags);
static inline struct nfs4_label *
nfs4_label_copy(struct nfs4_label *dst, struct nfs4_label *src)
{
	if (!dst || !src)
		return NULL;

	if (src->len > NFS4_MAXLABELLEN)
		return NULL;

	dst->lfs = src->lfs;
	dst->pi = src->pi;
	dst->len = src->len;
	memcpy(dst->label, src->label, src->len);

	return dst;
}

static inline void nfs_zap_label_cache_locked(struct nfs_inode *nfsi)
{
	if (nfs_server_capable(&nfsi->vfs_inode, NFS_CAP_SECURITY_LABEL))
		nfsi->cache_validity |= NFS_INO_INVALID_LABEL;
}
#else
static inline struct nfs4_label *nfs4_label_alloc(struct nfs_server *server, gfp_t flags) { return NULL; }
static inline void nfs_zap_label_cache_locked(struct nfs_inode *nfsi)
{
}
static inline struct nfs4_label *
nfs4_label_copy(struct nfs4_label *dst, struct nfs4_label *src)
{
	return NULL;
}
#endif /* CONFIG_NFS_V4_SECURITY_LABEL */

/* proc.c */
void nfs_close_context(struct nfs_open_context *ctx, int is_sync);
extern struct nfs_client *nfs_init_client(struct nfs_client *clp,
			   const struct nfs_client_initdata *);

/* dir.c */
extern void nfs_readdir_record_entry_cache_hit(struct inode *dir);
extern void nfs_readdir_record_entry_cache_miss(struct inode *dir);
extern unsigned long nfs_access_cache_count(struct shrinker *shrink,
					    struct shrink_control *sc);
extern unsigned long nfs_access_cache_scan(struct shrinker *shrink,
					   struct shrink_control *sc);
struct dentry *nfs_lookup(struct inode *, struct dentry *, unsigned int);
void nfs_d_prune_case_insensitive_aliases(struct inode *inode);
int nfs_create(struct mnt_idmap *, struct inode *, struct dentry *,
	       umode_t, bool);
int nfs_mkdir(struct mnt_idmap *, struct inode *, struct dentry *,
	      umode_t);
int nfs_rmdir(struct inode *, struct dentry *);
int nfs_unlink(struct inode *, struct dentry *);
int nfs_symlink(struct mnt_idmap *, struct inode *, struct dentry *,
		const char *);
int nfs_link(struct dentry *, struct inode *, struct dentry *);
int nfs_mknod(struct mnt_idmap *, struct inode *, struct dentry *, umode_t,
	      dev_t);
int nfs_rename(struct mnt_idmap *, struct inode *, struct dentry *,
	       struct inode *, struct dentry *, unsigned int);

#ifdef CONFIG_NFS_V4_2
static inline __u32 nfs_access_xattr_mask(const struct nfs_server *server)
{
	if (!(server->caps & NFS_CAP_XATTR))
		return 0;
	return NFS4_ACCESS_XAREAD | NFS4_ACCESS_XAWRITE | NFS4_ACCESS_XALIST;
}
#else
static inline __u32 nfs_access_xattr_mask(const struct nfs_server *server)
{
	return 0;
}
#endif

/* file.c */
int nfs_file_fsync(struct file *file, loff_t start, loff_t end, int datasync);
loff_t nfs_file_llseek(struct file *, loff_t, int);
ssize_t nfs_file_read(struct kiocb *, struct iov_iter *);
ssize_t nfs_file_splice_read(struct file *in, loff_t *ppos, struct pipe_inode_info *pipe,
			     size_t len, unsigned int flags);
int nfs_file_mmap(struct file *, struct vm_area_struct *);
ssize_t nfs_file_write(struct kiocb *, struct iov_iter *);
int nfs_file_release(struct inode *, struct file *);
int nfs_lock(struct file *, int, struct file_lock *);
int nfs_flock(struct file *, int, struct file_lock *);
int nfs_check_flags(int);

/* inode.c */
extern struct workqueue_struct *nfsiod_workqueue;
extern struct inode *nfs_alloc_inode(struct super_block *sb);
extern void nfs_free_inode(struct inode *);
extern int nfs_write_inode(struct inode *, struct writeback_control *);
extern int nfs_drop_inode(struct inode *);
extern void nfs_clear_inode(struct inode *);
extern void nfs_evict_inode(struct inode *);
extern void nfs_zap_acl_cache(struct inode *inode);
extern void nfs_set_cache_invalid(struct inode *inode, unsigned long flags);
extern bool nfs_check_cache_invalid(struct inode *, unsigned long);
extern int nfs_wait_bit_killable(struct wait_bit_key *key, int mode);

/* super.c */
extern const struct super_operations nfs_sops;
bool nfs_auth_info_match(const struct nfs_auth_info *, rpc_authflavor_t);
int nfs_try_get_tree(struct fs_context *);
int nfs_get_tree_common(struct fs_context *);
void nfs_kill_super(struct super_block *);

extern struct rpc_stat nfs_rpcstat;

extern int __init register_nfs_fs(void);
extern void __exit unregister_nfs_fs(void);
extern bool nfs_sb_active(struct super_block *sb);
extern void nfs_sb_deactive(struct super_block *sb);
extern int nfs_client_for_each_server(struct nfs_client *clp,
				      int (*fn)(struct nfs_server *, void *),
				      void *data);
#ifdef CONFIG_NFS_FSCACHE
extern const struct netfs_request_ops nfs_netfs_ops;
#endif

/* io.c */
extern void nfs_start_io_read(struct inode *inode);
extern void nfs_end_io_read(struct inode *inode);
extern void nfs_start_io_write(struct inode *inode);
extern void nfs_end_io_write(struct inode *inode);
extern void nfs_start_io_direct(struct inode *inode);
extern void nfs_end_io_direct(struct inode *inode);

static inline bool nfs_file_io_is_buffered(struct nfs_inode *nfsi)
{
	return test_bit(NFS_INO_ODIRECT, &nfsi->flags) == 0;
}

/* namespace.c */
#define NFS_PATH_CANONICAL 1
extern char *nfs_path(char **p, struct dentry *dentry,
		      char *buffer, ssize_t buflen, unsigned flags);
extern struct vfsmount *nfs_d_automount(struct path *path);
int nfs_submount(struct fs_context *, struct nfs_server *);
int nfs_do_submount(struct fs_context *);

/* getroot.c */
extern int nfs_get_root(struct super_block *s, struct fs_context *fc);
#if IS_ENABLED(CONFIG_NFS_V4)
extern int nfs4_get_rootfh(struct nfs_server *server, struct nfs_fh *mntfh, bool);
#endif

struct nfs_pgio_completion_ops;
/* read.c */
extern const struct nfs_pgio_completion_ops nfs_async_read_completion_ops;
extern void nfs_pageio_init_read(struct nfs_pageio_descriptor *pgio,
			struct inode *inode, bool force_mds,
			const struct nfs_pgio_completion_ops *compl_ops);
extern bool nfs_read_alloc_scratch(struct nfs_pgio_header *hdr, size_t size);
extern int nfs_read_add_folio(struct nfs_pageio_descriptor *pgio,
			       struct nfs_open_context *ctx,
			       struct folio *folio);
extern void nfs_pageio_complete_read(struct nfs_pageio_descriptor *pgio);
extern void nfs_read_prepare(struct rpc_task *task, void *calldata);
extern void nfs_pageio_reset_read_mds(struct nfs_pageio_descriptor *pgio);

/* super.c */
void nfs_umount_begin(struct super_block *);
int  nfs_statfs(struct dentry *, struct kstatfs *);
int  nfs_show_options(struct seq_file *, struct dentry *);
int  nfs_show_devname(struct seq_file *, struct dentry *);
int  nfs_show_path(struct seq_file *, struct dentry *);
int  nfs_show_stats(struct seq_file *, struct dentry *);
int  nfs_reconfigure(struct fs_context *);

/* write.c */
extern void nfs_pageio_init_write(struct nfs_pageio_descriptor *pgio,
			struct inode *inode, int ioflags, bool force_mds,
			const struct nfs_pgio_completion_ops *compl_ops);
extern void nfs_pageio_reset_write_mds(struct nfs_pageio_descriptor *pgio);
extern void nfs_commit_free(struct nfs_commit_data *p);
extern void nfs_commit_prepare(struct rpc_task *task, void *calldata);
extern int nfs_initiate_commit(struct rpc_clnt *clnt,
			       struct nfs_commit_data *data,
			       const struct nfs_rpc_ops *nfs_ops,
			       const struct rpc_call_ops *call_ops,
			       int how, int flags);
extern void nfs_init_commit(struct nfs_commit_data *data,
			    struct list_head *head,
			    struct pnfs_layout_segment *lseg,
			    struct nfs_commit_info *cinfo);
int nfs_scan_commit_list(struct list_head *src, struct list_head *dst,
			 struct nfs_commit_info *cinfo, int max);
unsigned long nfs_reqs_to_commit(struct nfs_commit_info *);
int nfs_scan_commit(struct inode *inode, struct list_head *dst,
		    struct nfs_commit_info *cinfo);
void nfs_mark_request_commit(struct nfs_page *req,
			     struct pnfs_layout_segment *lseg,
			     struct nfs_commit_info *cinfo,
			     u32 ds_commit_idx);
int nfs_write_need_commit(struct nfs_pgio_header *);
void nfs_writeback_update_inode(struct nfs_pgio_header *hdr);
int nfs_generic_commit_list(struct inode *inode, struct list_head *head,
			    int how, struct nfs_commit_info *cinfo);
void nfs_retry_commit(struct list_head *page_list,
		      struct pnfs_layout_segment *lseg,
		      struct nfs_commit_info *cinfo,
		      u32 ds_commit_idx);
void nfs_commitdata_release(struct nfs_commit_data *data);
void nfs_request_add_commit_list(struct nfs_page *req,
				 struct nfs_commit_info *cinfo);
void nfs_request_add_commit_list_locked(struct nfs_page *req,
		struct list_head *dst,
		struct nfs_commit_info *cinfo);
void nfs_request_remove_commit_list(struct nfs_page *req,
				    struct nfs_commit_info *cinfo);
void nfs_init_cinfo(struct nfs_commit_info *cinfo,
		    struct inode *inode,
		    struct nfs_direct_req *dreq);
int nfs_key_timeout_notify(struct file *filp, struct inode *inode);
bool nfs_ctx_key_to_expire(struct nfs_open_context *ctx, struct inode *inode);
void nfs_pageio_stop_mirroring(struct nfs_pageio_descriptor *pgio);

int nfs_filemap_write_and_wait_range(struct address_space *mapping,
		loff_t lstart, loff_t lend);

#ifdef CONFIG_NFS_V4_1
static inline void
pnfs_bucket_clear_pnfs_ds_commit_verifiers(struct pnfs_commit_bucket *buckets,
		unsigned int nbuckets)
{
	unsigned int i;

	for (i = 0; i < nbuckets; i++)
		buckets[i].direct_verf.committed = NFS_INVALID_STABLE_HOW;
}
static inline
void nfs_clear_pnfs_ds_commit_verifiers(struct pnfs_ds_commit_info *cinfo)
{
	struct pnfs_commit_array *array;

	rcu_read_lock();
	list_for_each_entry_rcu(array, &cinfo->commits, cinfo_list)
		pnfs_bucket_clear_pnfs_ds_commit_verifiers(array->buckets,
				array->nbuckets);
	rcu_read_unlock();
}
#else
static inline
void nfs_clear_pnfs_ds_commit_verifiers(struct pnfs_ds_commit_info *cinfo)
{
}
#endif

#ifdef CONFIG_MIGRATION
int nfs_migrate_folio(struct address_space *, struct folio *dst,
		struct folio *src, enum migrate_mode);
#else
#define nfs_migrate_folio NULL
#endif

static inline int
nfs_write_verifier_cmp(const struct nfs_write_verifier *v1,
		const struct nfs_write_verifier *v2)
{
	return memcmp(v1->data, v2->data, sizeof(v1->data));
}

static inline bool
nfs_write_match_verf(const struct nfs_writeverf *verf,
		struct nfs_page *req)
{
	return verf->committed > NFS_UNSTABLE &&
		!nfs_write_verifier_cmp(&req->wb_verf, &verf->verifier);
}

static inline gfp_t nfs_io_gfp_mask(void)
{
	if (current->flags & PF_WQ_WORKER)
		return GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN;
	return GFP_KERNEL;
}

/*
 * Special version of should_remove_suid() that ignores capabilities.
 */
static inline int nfs_should_remove_suid(const struct inode *inode)
{
	umode_t mode = inode->i_mode;
	int kill = 0;

	/* suid always must be killed */
	if (unlikely(mode & S_ISUID))
		kill = ATTR_KILL_SUID;

	/*
	 * sgid without any exec bits is just a mandatory locking mark; leave
	 * it alone.  If some exec bits are set, it's a real sgid; kill it.
	 */
	if (unlikely((mode & S_ISGID) && (mode & S_IXGRP)))
		kill |= ATTR_KILL_SGID;

	if (unlikely(kill && S_ISREG(mode)))
		return kill;

	return 0;
}

/* unlink.c */
extern struct rpc_task *
nfs_async_rename(struct inode *old_dir, struct inode *new_dir,
		 struct dentry *old_dentry, struct dentry *new_dentry,
		 void (*complete)(struct rpc_task *, struct nfs_renamedata *));
extern int nfs_sillyrename(struct inode *dir, struct dentry *dentry);

/* direct.c */
void nfs_init_cinfo_from_dreq(struct nfs_commit_info *cinfo,
			      struct nfs_direct_req *dreq);
extern ssize_t nfs_dreq_bytes_left(struct nfs_direct_req *dreq, loff_t offset);

/* nfs4proc.c */
extern struct nfs_client *nfs4_init_client(struct nfs_client *clp,
			    const struct nfs_client_initdata *);
extern int nfs40_walk_client_list(struct nfs_client *clp,
				struct nfs_client **result,
				const struct cred *cred);
extern int nfs41_walk_client_list(struct nfs_client *clp,
				struct nfs_client **result,
				const struct cred *cred);
extern void nfs4_test_session_trunk(struct rpc_clnt *clnt,
				struct rpc_xprt *xprt,
				void *data);

static inline struct inode *nfs_igrab_and_active(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	if (sb && nfs_sb_active(sb)) {
		if (igrab(inode))
			return inode;
		nfs_sb_deactive(sb);
	}
	return NULL;
}

static inline void nfs_iput_and_deactive(struct inode *inode)
{
	if (inode != NULL) {
		struct super_block *sb = inode->i_sb;

		iput(inode);
		nfs_sb_deactive(sb);
	}
}

/*
 * Determine the device name as a string
 */
static inline char *nfs_devname(struct dentry *dentry,
				char *buffer, ssize_t buflen)
{
	char *dummy;
	return nfs_path(&dummy, dentry, buffer, buflen, NFS_PATH_CANONICAL);
}

/*
 * Determine the actual block size (and log2 thereof)
 */
static inline
unsigned long nfs_block_bits(unsigned long bsize, unsigned char *nrbitsp)
{
	/* make sure blocksize is a power of two */
	if ((bsize & (bsize - 1)) || nrbitsp) {
		unsigned char	nrbits;

		for (nrbits = 31; nrbits && !(bsize & (1 << nrbits)); nrbits--)
			;
		bsize = 1 << nrbits;
		if (nrbitsp)
			*nrbitsp = nrbits;
	}

	return bsize;
}

/*
 * Calculate the number of 512byte blocks used.
 */
static inline blkcnt_t nfs_calc_block_size(u64 tsize)
{
	blkcnt_t used = (tsize + 511) >> 9;
	return (used > ULONG_MAX) ? ULONG_MAX : used;
}

/*
 * Compute and set NFS server blocksize
 */
static inline
unsigned long nfs_block_size(unsigned long bsize, unsigned char *nrbitsp)
{
	if (bsize < NFS_MIN_FILE_IO_SIZE)
		bsize = NFS_DEF_FILE_IO_SIZE;
	else if (bsize >= NFS_MAX_FILE_IO_SIZE)
		bsize = NFS_MAX_FILE_IO_SIZE;

	return nfs_block_bits(bsize, nrbitsp);
}

/*
 * Compute and set NFS server rsize / wsize
 */
static inline
unsigned long nfs_io_size(unsigned long iosize, enum xprt_transports proto)
{
	if (iosize < NFS_MIN_FILE_IO_SIZE)
		iosize = NFS_DEF_FILE_IO_SIZE;
	else if (iosize >= NFS_MAX_FILE_IO_SIZE)
		iosize = NFS_MAX_FILE_IO_SIZE;

	if (proto == XPRT_TRANSPORT_UDP || iosize < PAGE_SIZE)
		return nfs_block_bits(iosize, NULL);
	return iosize & PAGE_MASK;
}

/*
 * Determine the maximum file size for a superblock
 */
static inline
void nfs_super_set_maxbytes(struct super_block *sb, __u64 maxfilesize)
{
	sb->s_maxbytes = (loff_t)maxfilesize;
	if (sb->s_maxbytes > MAX_LFS_FILESIZE || sb->s_maxbytes <= 0)
		sb->s_maxbytes = MAX_LFS_FILESIZE;
}

/*
 * Record the page as unstable (an extra writeback period) and mark its
 * inode as dirty.
 */
static inline void nfs_folio_mark_unstable(struct folio *folio,
					   struct nfs_commit_info *cinfo)
{
	if (folio && !cinfo->dreq) {
		struct inode *inode = folio_file_mapping(folio)->host;
		long nr = folio_nr_pages(folio);

		/* This page is really still in write-back - just that the
		 * writeback is happening on the server now.
		 */
		node_stat_mod_folio(folio, NR_WRITEBACK, nr);
		wb_stat_mod(&inode_to_bdi(inode)->wb, WB_WRITEBACK, nr);
		__mark_inode_dirty(inode, I_DIRTY_DATASYNC);
	}
}

/*
 * Determine the number of bytes of data the page contains
 */
static inline
unsigned int nfs_page_length(struct page *page)
{
	loff_t i_size = i_size_read(page_file_mapping(page)->host);

	if (i_size > 0) {
		pgoff_t index = page_index(page);
		pgoff_t end_index = (i_size - 1) >> PAGE_SHIFT;
		if (index < end_index)
			return PAGE_SIZE;
		if (index == end_index)
			return ((i_size - 1) & ~PAGE_MASK) + 1;
	}
	return 0;
}

/*
 * Determine the number of bytes of data the page contains
 */
static inline size_t nfs_folio_length(struct folio *folio)
{
	loff_t i_size = i_size_read(folio_file_mapping(folio)->host);

	if (i_size > 0) {
		pgoff_t index = folio_index(folio) >> folio_order(folio);
		pgoff_t end_index = (i_size - 1) >> folio_shift(folio);
		if (index < end_index)
			return folio_size(folio);
		if (index == end_index)
			return offset_in_folio(folio, i_size - 1) + 1;
	}
	return 0;
}

/*
 * Convert a umode to a dirent->d_type
 */
static inline
unsigned char nfs_umode_to_dtype(umode_t mode)
{
	return (mode >> 12) & 15;
}

/*
 * Determine the number of pages in an array of length 'len' and
 * with a base offset of 'base'
 */
static inline unsigned int nfs_page_array_len(unsigned int base, size_t len)
{
	return ((unsigned long)len + (unsigned long)base + PAGE_SIZE - 1) >>
	       PAGE_SHIFT;
}

/*
 * Convert a struct timespec64 into a 64-bit change attribute
 *
 * This does approximately the same thing as timespec64_to_ns(),
 * but for calculation efficiency, we multiply the seconds by
 * 1024*1024*1024.
 */
static inline
u64 nfs_timespec_to_change_attr(const struct timespec64 *ts)
{
	return ((u64)ts->tv_sec << 30) + ts->tv_nsec;
}

#ifdef CONFIG_CRC32
static inline u32 nfs_stateid_hash(const nfs4_stateid *stateid)
{
	return ~crc32_le(0xFFFFFFFF, &stateid->other[0],
				NFS4_STATEID_OTHER_SIZE);
}
#else
static inline u32 nfs_stateid_hash(nfs4_stateid *stateid)
{
	return 0;
}
#endif

static inline bool nfs_error_is_fatal(int err)
{
	switch (err) {
	case -ERESTARTSYS:
	case -EINTR:
	case -EACCES:
	case -EDQUOT:
	case -EFBIG:
	case -EIO:
	case -ENOSPC:
	case -EROFS:
	case -ESTALE:
	case -E2BIG:
	case -ENOMEM:
	case -ETIMEDOUT:
		return true;
	default:
		return false;
	}
}

static inline bool nfs_error_is_fatal_on_server(int err)
{
	switch (err) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
	case -ENOMEM:
		return false;
	}
	return nfs_error_is_fatal(err);
}

/*
 * Select between a default port value and a user-specified port value.
 * If a zero value is set, then autobind will be used.
 */
static inline void nfs_set_port(struct sockaddr_storage *sap, int *port,
				const unsigned short default_port)
{
	if (*port == NFS_UNSPEC_PORT)
		*port = default_port;

	rpc_set_port((struct sockaddr *)sap, *port);
}

struct nfs_direct_req {
	struct kref		kref;		/* release manager */

	/* I/O parameters */
	struct nfs_open_context	*ctx;		/* file open context info */
	struct nfs_lock_context *l_ctx;		/* Lock context info */
	struct kiocb *		iocb;		/* controlling i/o request */
	struct inode *		inode;		/* target file of i/o */

	/* completion state */
	atomic_t		io_count;	/* i/os we're waiting for */
	spinlock_t		lock;		/* protect completion state */

	loff_t			io_start;	/* Start offset for I/O */
	ssize_t			count,		/* bytes actually processed */
				max_count,	/* max expected count */
				bytes_left,	/* bytes left to be sent */
				error;		/* any reported error */
	struct completion	completion;	/* wait for i/o completion */

	/* commit state */
	struct nfs_mds_commit_info mds_cinfo;	/* Storage for cinfo */
	struct pnfs_ds_commit_info ds_cinfo;	/* Storage for cinfo */
	struct work_struct	work;
	int			flags;
	/* for write */
#define NFS_ODIRECT_DO_COMMIT		(1)	/* an unstable reply was received */
#define NFS_ODIRECT_RESCHED_WRITES	(2)	/* write verification failed */
	/* for read */
#define NFS_ODIRECT_SHOULD_DIRTY	(3)	/* dirty user-space page after read */
#define NFS_ODIRECT_DONE		INT_MAX	/* write verification failed */
};
