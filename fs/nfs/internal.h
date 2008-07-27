/*
 * NFS internal definitions
 */

#include <linux/mount.h>
#include <linux/security.h>

struct nfs_string;

/* Maximum number of readahead requests
 * FIXME: this should really be a sysctl so that users may tune it to suit
 *        their needs. People that do NFS over a slow network, might for
 *        instance want to reduce it to something closer to 1 for improved
 *        interactive response.
 */
#define NFS_MAX_READAHEAD	(RPC_DEF_SLOT_TABLE - 1)

struct nfs_clone_mount {
	const struct super_block *sb;
	const struct dentry *dentry;
	struct nfs_fh *fh;
	struct nfs_fattr *fattr;
	char *hostname;
	char *mnt_path;
	struct sockaddr *addr;
	size_t addrlen;
	rpc_authflavor_t authflavor;
};

/*
 * In-kernel mount arguments
 */
struct nfs_parsed_mount_data {
	int			flags;
	int			rsize, wsize;
	int			timeo, retrans;
	int			acregmin, acregmax,
				acdirmin, acdirmax;
	int			namlen;
	unsigned int		bsize;
	unsigned int		auth_flavor_len;
	rpc_authflavor_t	auth_flavors[1];
	char			*client_address;

	struct {
		struct sockaddr_storage	address;
		size_t			addrlen;
		char			*hostname;
		u32			version;
		unsigned short		port;
		unsigned short		protocol;
	} mount_server;

	struct {
		struct sockaddr_storage	address;
		size_t			addrlen;
		char			*hostname;
		char			*export_path;
		unsigned short		port;
		unsigned short		protocol;
	} nfs_server;

	struct security_mnt_opts lsm_opts;
};

/* client.c */
extern struct rpc_program nfs_program;

extern void nfs_put_client(struct nfs_client *);
extern struct nfs_client *nfs_find_client(const struct sockaddr *, u32);
extern struct nfs_client *nfs_find_client_next(struct nfs_client *);
extern struct nfs_server *nfs_create_server(
					const struct nfs_parsed_mount_data *,
					struct nfs_fh *);
extern struct nfs_server *nfs4_create_server(
					const struct nfs_parsed_mount_data *,
					struct nfs_fh *);
extern struct nfs_server *nfs4_create_referral_server(struct nfs_clone_mount *,
						      struct nfs_fh *);
extern void nfs_free_server(struct nfs_server *server);
extern struct nfs_server *nfs_clone_server(struct nfs_server *,
					   struct nfs_fh *,
					   struct nfs_fattr *);
#ifdef CONFIG_PROC_FS
extern int __init nfs_fs_proc_init(void);
extern void nfs_fs_proc_exit(void);
#else
static inline int nfs_fs_proc_init(void)
{
	return 0;
}
static inline void nfs_fs_proc_exit(void)
{
}
#endif

/* nfs4namespace.c */
#ifdef CONFIG_NFS_V4
extern struct vfsmount *nfs_do_refmount(const struct vfsmount *mnt_parent, struct dentry *dentry);
#else
static inline
struct vfsmount *nfs_do_refmount(const struct vfsmount *mnt_parent, struct dentry *dentry)
{
	return ERR_PTR(-ENOENT);
}
#endif

/* callback_xdr.c */
extern struct svc_version nfs4_callback_version1;

/* pagelist.c */
extern int __init nfs_init_nfspagecache(void);
extern void nfs_destroy_nfspagecache(void);
extern int __init nfs_init_readpagecache(void);
extern void nfs_destroy_readpagecache(void);
extern int __init nfs_init_writepagecache(void);
extern void nfs_destroy_writepagecache(void);

extern int __init nfs_init_directcache(void);
extern void nfs_destroy_directcache(void);

/* nfs2xdr.c */
extern int nfs_stat_to_errno(int);
extern struct rpc_procinfo nfs_procedures[];
extern __be32 * nfs_decode_dirent(__be32 *, struct nfs_entry *, int);

/* nfs3xdr.c */
extern struct rpc_procinfo nfs3_procedures[];
extern __be32 *nfs3_decode_dirent(__be32 *, struct nfs_entry *, int);

/* nfs4xdr.c */
#ifdef CONFIG_NFS_V4
extern __be32 *nfs4_decode_dirent(__be32 *p, struct nfs_entry *entry, int plus);
#endif

/* nfs4proc.c */
#ifdef CONFIG_NFS_V4
extern struct rpc_procinfo nfs4_procedures[];
#endif

/* dir.c */
extern int nfs_access_cache_shrinker(int nr_to_scan, gfp_t gfp_mask);

/* inode.c */
extern struct workqueue_struct *nfsiod_workqueue;
extern struct inode *nfs_alloc_inode(struct super_block *sb);
extern void nfs_destroy_inode(struct inode *);
extern int nfs_write_inode(struct inode *,int);
extern void nfs_clear_inode(struct inode *);
#ifdef CONFIG_NFS_V4
extern void nfs4_clear_inode(struct inode *);
#endif
void nfs_zap_acl_cache(struct inode *inode);

/* super.c */
extern struct file_system_type nfs_xdev_fs_type;
#ifdef CONFIG_NFS_V4
extern struct file_system_type nfs4_xdev_fs_type;
extern struct file_system_type nfs4_referral_fs_type;
#endif

extern struct rpc_stat nfs_rpcstat;

extern int __init register_nfs_fs(void);
extern void __exit unregister_nfs_fs(void);
extern void nfs_sb_active(struct super_block *sb);
extern void nfs_sb_deactive(struct super_block *sb);

/* namespace.c */
extern char *nfs_path(const char *base,
		      const struct dentry *droot,
		      const struct dentry *dentry,
		      char *buffer, ssize_t buflen);

/* getroot.c */
extern struct dentry *nfs_get_root(struct super_block *, struct nfs_fh *);
#ifdef CONFIG_NFS_V4
extern struct dentry *nfs4_get_root(struct super_block *, struct nfs_fh *);

extern int nfs4_path_walk(struct nfs_server *server,
			  struct nfs_fh *mntfh,
			  const char *path);
#endif

/*
 * Determine the device name as a string
 */
static inline char *nfs_devname(const struct vfsmount *mnt_parent,
				const struct dentry *dentry,
				char *buffer, ssize_t buflen)
{
	return nfs_path(mnt_parent->mnt_devname, mnt_parent->mnt_root,
			dentry, buffer, buflen);
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
 * Determine the number of bytes of data the page contains
 */
static inline
unsigned int nfs_page_length(struct page *page)
{
	loff_t i_size = i_size_read(page->mapping->host);

	if (i_size > 0) {
		pgoff_t end_index = (i_size - 1) >> PAGE_CACHE_SHIFT;
		if (page->index < end_index)
			return PAGE_CACHE_SIZE;
		if (page->index == end_index)
			return ((i_size - 1) & ~PAGE_CACHE_MASK) + 1;
	}
	return 0;
}

/*
 * Determine the number of pages in an array of length 'len' and
 * with a base offset of 'base'
 */
static inline
unsigned int nfs_page_array_len(unsigned int base, size_t len)
{
	return ((unsigned long)len + (unsigned long)base +
		PAGE_SIZE - 1) >> PAGE_SHIFT;
}

