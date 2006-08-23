/*
 * NFS internal definitions
 */

#include <linux/mount.h>

struct nfs_clone_mount {
	const struct super_block *sb;
	const struct dentry *dentry;
	struct nfs_fh *fh;
	struct nfs_fattr *fattr;
	char *hostname;
	char *mnt_path;
	struct sockaddr_in *addr;
	rpc_authflavor_t authflavor;
};

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

#ifdef CONFIG_NFS_DIRECTIO
extern int __init nfs_init_directcache(void);
extern void nfs_destroy_directcache(void);
#else
#define nfs_init_directcache() (0)
#define nfs_destroy_directcache() do {} while(0)
#endif

/* nfs2xdr.c */
extern int nfs_stat_to_errno(int);
extern struct rpc_procinfo nfs_procedures[];
extern u32 * nfs_decode_dirent(u32 *, struct nfs_entry *, int);

/* nfs3xdr.c */
extern struct rpc_procinfo nfs3_procedures[];
extern u32 *nfs3_decode_dirent(u32 *, struct nfs_entry *, int);

/* nfs4xdr.c */
#ifdef CONFIG_NFS_V4
extern u32 *nfs4_decode_dirent(u32 *p, struct nfs_entry *entry, int plus);
#endif

/* nfs4proc.c */
#ifdef CONFIG_NFS_V4
extern struct rpc_procinfo nfs4_procedures[];

extern int nfs4_proc_fs_locations(struct inode *dir, struct dentry *dentry,
				  struct nfs4_fs_locations *fs_locations,
				  struct page *page);
#endif

/* dir.c */
extern int nfs_access_cache_shrinker(int nr_to_scan, gfp_t gfp_mask);

/* inode.c */
extern struct inode *nfs_alloc_inode(struct super_block *sb);
extern void nfs_destroy_inode(struct inode *);
extern int nfs_write_inode(struct inode *,int);
extern void nfs_clear_inode(struct inode *);
#ifdef CONFIG_NFS_V4
extern void nfs4_clear_inode(struct inode *);
#endif

/* super.c */
extern struct file_system_type nfs_referral_nfs4_fs_type;
extern struct file_system_type clone_nfs_fs_type;
#ifdef CONFIG_NFS_V4
extern struct file_system_type clone_nfs4_fs_type;
#endif

extern struct rpc_stat nfs_rpcstat;

extern int __init register_nfs_fs(void);
extern void __exit unregister_nfs_fs(void);

/* namespace.c */
extern char *nfs_path(const char *base, const struct dentry *dentry,
		      char *buffer, ssize_t buflen);

/*
 * Determine the mount path as a string
 */
#ifdef CONFIG_NFS_V4
static inline char *
nfs4_path(const struct dentry *dentry, char *buffer, ssize_t buflen)
{
	return nfs_path(NFS_SB(dentry->d_sb)->mnt_path, dentry, buffer, buflen);
}
#endif

/*
 * Determine the device name as a string
 */
static inline char *nfs_devname(const struct vfsmount *mnt_parent,
			 const struct dentry *dentry,
			 char *buffer, ssize_t buflen)
{
	return nfs_path(mnt_parent->mnt_devname, dentry, buffer, buflen);
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
static inline unsigned long nfs_calc_block_size(u64 tsize)
{
	loff_t used = (tsize + 511) >> 9;
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
 * Check if the string represents a "valid" IPv4 address
 */
static inline int valid_ipaddr4(const char *buf)
{
	int rc, count, in[4];

	rc = sscanf(buf, "%d.%d.%d.%d", &in[0], &in[1], &in[2], &in[3]);
	if (rc != 4)
		return -EINVAL;
	for (count = 0; count < 4; count++) {
		if (in[count] > 255)
			return -EINVAL;
	}
	return 0;
}
