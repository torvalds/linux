/*
 *  linux/include/linux/nfs_fs.h
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  OS-specific nfs filesystem definitions and declarations
 */

#ifndef _LINUX_NFS_FS_H
#define _LINUX_NFS_FS_H

#include <linux/magic.h>

/*
 * Enable debugging support for nfs client.
 * Requires RPC_DEBUG.
 */
#ifdef RPC_DEBUG
# define NFS_DEBUG
#endif

/* Default timeout values */
#define NFS_MAX_UDP_TIMEOUT	(60*HZ)
#define NFS_MAX_TCP_TIMEOUT	(600*HZ)

/*
 * When flushing a cluster of dirty pages, there can be different
 * strategies:
 */
#define FLUSH_SYNC		1	/* file being synced, or contention */
#define FLUSH_STABLE		4	/* commit to stable storage */
#define FLUSH_LOWPRI		8	/* low priority background flush */
#define FLUSH_HIGHPRI		16	/* high priority memory reclaim flush */
#define FLUSH_NOCOMMIT		32	/* Don't send the NFSv3/v4 COMMIT */
#define FLUSH_INVALIDATE	64	/* Invalidate the page cache */

#ifdef __KERNEL__

#include <linux/in.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/wait.h>

#include <linux/sunrpc/debug.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/clnt.h>

#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/nfs_xdr.h>

#include <linux/nfs_fs_sb.h>

#include <linux/rwsem.h>
#include <linux/mempool.h>

/*
 * These are the default flags for swap requests
 */
#define NFS_RPC_SWAPFLAGS		(RPC_TASK_SWAPPER|RPC_TASK_ROOTCREDS)

/*
 * NFSv3/v4 Access mode cache entry
 */
struct nfs_access_entry {
	struct rb_node		rb_node;
	struct list_head	lru;
	unsigned long		jiffies;
	struct rpc_cred *	cred;
	int			mask;
};

struct nfs4_state;
struct nfs_open_context {
	atomic_t count;
	struct vfsmount *vfsmnt;
	struct dentry *dentry;
	struct rpc_cred *cred;
	struct nfs4_state *state;
	fl_owner_t lockowner;
	int mode;
	int error;

	struct list_head list;

	__u64 dir_cookie;
};

/*
 * NFSv4 delegation
 */
struct nfs_delegation;

struct posix_acl;

/*
 * nfs fs inode data in memory
 */
struct nfs_inode {
	/*
	 * The 64bit 'inode number'
	 */
	__u64 fileid;

	/*
	 * NFS file handle
	 */
	struct nfs_fh		fh;

	/*
	 * Various flags
	 */
	unsigned long		flags;			/* atomic bit ops */
	unsigned long		cache_validity;		/* bit mask */

	/*
	 * read_cache_jiffies is when we started read-caching this inode.
	 * attrtimeo is for how long the cached information is assumed
	 * to be valid. A successful attribute revalidation doubles
	 * attrtimeo (up to acregmax/acdirmax), a failure resets it to
	 * acregmin/acdirmin.
	 *
	 * We need to revalidate the cached attrs for this inode if
	 *
	 *	jiffies - read_cache_jiffies > attrtimeo
	 */
	unsigned long		read_cache_jiffies;
	unsigned long		attrtimeo;
	unsigned long		attrtimeo_timestamp;
	__u64			change_attr;		/* v4 only */

	unsigned long		last_updated;
	/* "Generation counter" for the attribute cache. This is
	 * bumped whenever we update the metadata on the
	 * server.
	 */
	unsigned long		cache_change_attribute;
	/*
	 * Counter indicating the number of outstanding requests that
	 * will cause a file data update.
	 */
	atomic_t		data_updates;

	struct rb_root		access_cache;
	struct list_head	access_cache_entry_lru;
	struct list_head	access_cache_inode_lru;
#ifdef CONFIG_NFS_V3_ACL
	struct posix_acl	*acl_access;
	struct posix_acl	*acl_default;
#endif

	/*
	 * This is the cookie verifier used for NFSv3 readdir
	 * operations
	 */
	__be32			cookieverf[2];

	/*
	 * This is the list of dirty unwritten pages.
	 */
	spinlock_t		req_lock;
	struct list_head	dirty;
	struct list_head	commit;
	struct radix_tree_root	nfs_page_tree;

	unsigned int		ndirty,
				ncommit,
				npages;

	/* Open contexts for shared mmap writes */
	struct list_head	open_files;

#ifdef CONFIG_NFS_V4
	struct nfs4_cached_acl	*nfs4_acl;
        /* NFSv4 state */
	struct list_head	open_states;
	struct nfs_delegation	*delegation;
	int			 delegation_state;
	struct rw_semaphore	rwsem;
#endif /* CONFIG_NFS_V4*/
	struct inode		vfs_inode;
};

/*
 * Cache validity bit flags
 */
#define NFS_INO_INVALID_ATTR	0x0001		/* cached attrs are invalid */
#define NFS_INO_INVALID_DATA	0x0002		/* cached data is invalid */
#define NFS_INO_INVALID_ATIME	0x0004		/* cached atime is invalid */
#define NFS_INO_INVALID_ACCESS	0x0008		/* cached access cred invalid */
#define NFS_INO_INVALID_ACL	0x0010		/* cached acls are invalid */
#define NFS_INO_REVAL_PAGECACHE	0x0020		/* must revalidate pagecache */

/*
 * Bit offsets in flags field
 */
#define NFS_INO_REVALIDATING	(0)		/* revalidating attrs */
#define NFS_INO_ADVISE_RDPLUS	(1)		/* advise readdirplus */
#define NFS_INO_STALE		(2)		/* possible stale inode */
#define NFS_INO_ACL_LRU_SET	(3)		/* Inode is on the LRU list */

static inline struct nfs_inode *NFS_I(struct inode *inode)
{
	return container_of(inode, struct nfs_inode, vfs_inode);
}
#define NFS_SB(s)		((struct nfs_server *)(s->s_fs_info))

#define NFS_FH(inode)			(&NFS_I(inode)->fh)
#define NFS_SERVER(inode)		(NFS_SB(inode->i_sb))
#define NFS_CLIENT(inode)		(NFS_SERVER(inode)->client)
#define NFS_PROTO(inode)		(NFS_SERVER(inode)->nfs_client->rpc_ops)
#define NFS_COOKIEVERF(inode)		(NFS_I(inode)->cookieverf)
#define NFS_READTIME(inode)		(NFS_I(inode)->read_cache_jiffies)
#define NFS_CHANGE_ATTR(inode)		(NFS_I(inode)->change_attr)
#define NFS_ATTRTIMEO(inode)		(NFS_I(inode)->attrtimeo)
#define NFS_MINATTRTIMEO(inode) \
	(S_ISDIR(inode->i_mode)? NFS_SERVER(inode)->acdirmin \
			       : NFS_SERVER(inode)->acregmin)
#define NFS_MAXATTRTIMEO(inode) \
	(S_ISDIR(inode->i_mode)? NFS_SERVER(inode)->acdirmax \
			       : NFS_SERVER(inode)->acregmax)
#define NFS_ATTRTIMEO_UPDATE(inode)	(NFS_I(inode)->attrtimeo_timestamp)

#define NFS_FLAGS(inode)		(NFS_I(inode)->flags)
#define NFS_STALE(inode)		(test_bit(NFS_INO_STALE, &NFS_FLAGS(inode)))

#define NFS_FILEID(inode)		(NFS_I(inode)->fileid)

static inline int nfs_caches_unstable(struct inode *inode)
{
	return atomic_read(&NFS_I(inode)->data_updates) != 0;
}

static inline void nfs_mark_for_revalidate(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&inode->i_lock);
	nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS;
	if (S_ISDIR(inode->i_mode))
		nfsi->cache_validity |= NFS_INO_REVAL_PAGECACHE|NFS_INO_INVALID_DATA;
	spin_unlock(&inode->i_lock);
}

static inline void NFS_CACHEINV(struct inode *inode)
{
	if (!nfs_caches_unstable(inode))
		nfs_mark_for_revalidate(inode);
}

static inline int nfs_server_capable(struct inode *inode, int cap)
{
	return NFS_SERVER(inode)->caps & cap;
}

static inline int NFS_USE_READDIRPLUS(struct inode *inode)
{
	return test_bit(NFS_INO_ADVISE_RDPLUS, &NFS_FLAGS(inode));
}

/**
 * nfs_save_change_attribute - Returns the inode attribute change cookie
 * @inode - pointer to inode
 * The "change attribute" is updated every time we finish an operation
 * that will result in a metadata change on the server.
 */
static inline long nfs_save_change_attribute(struct inode *inode)
{
	return NFS_I(inode)->cache_change_attribute;
}

/**
 * nfs_verify_change_attribute - Detects NFS inode cache updates
 * @inode - pointer to inode
 * @chattr - previously saved change attribute
 * Return "false" if metadata has been updated (or is in the process of
 * being updated) since the change attribute was saved.
 */
static inline int nfs_verify_change_attribute(struct inode *inode, unsigned long chattr)
{
	return !nfs_caches_unstable(inode)
		&& time_after_eq(chattr, NFS_I(inode)->cache_change_attribute);
}

/*
 * linux/fs/nfs/inode.c
 */
extern int nfs_sync_mapping(struct address_space *mapping);
extern void nfs_zap_mapping(struct inode *inode, struct address_space *mapping);
extern void nfs_zap_caches(struct inode *);
extern struct inode *nfs_fhget(struct super_block *, struct nfs_fh *,
				struct nfs_fattr *);
extern int nfs_refresh_inode(struct inode *, struct nfs_fattr *);
extern int nfs_post_op_update_inode(struct inode *inode, struct nfs_fattr *fattr);
extern int nfs_getattr(struct vfsmount *, struct dentry *, struct kstat *);
extern int nfs_permission(struct inode *, int, struct nameidata *);
extern int nfs_access_get_cached(struct inode *, struct rpc_cred *, struct nfs_access_entry *);
extern void nfs_access_add_cache(struct inode *, struct nfs_access_entry *);
extern void nfs_access_zap_cache(struct inode *inode);
extern int nfs_open(struct inode *, struct file *);
extern int nfs_release(struct inode *, struct file *);
extern int nfs_attribute_timeout(struct inode *inode);
extern int nfs_revalidate_inode(struct nfs_server *server, struct inode *inode);
extern int __nfs_revalidate_inode(struct nfs_server *, struct inode *);
extern int nfs_revalidate_mapping(struct inode *inode, struct address_space *mapping);
extern int nfs_setattr(struct dentry *, struct iattr *);
extern void nfs_setattr_update_inode(struct inode *inode, struct iattr *attr);
extern void nfs_begin_attr_update(struct inode *);
extern void nfs_end_attr_update(struct inode *);
extern void nfs_begin_data_update(struct inode *);
extern void nfs_end_data_update(struct inode *);
extern struct nfs_open_context *get_nfs_open_context(struct nfs_open_context *ctx);
extern void put_nfs_open_context(struct nfs_open_context *ctx);
extern struct nfs_open_context *nfs_find_open_context(struct inode *inode, struct rpc_cred *cred, int mode);

/* linux/net/ipv4/ipconfig.c: trims ip addr off front of name, too. */
extern __be32 root_nfs_parse_addr(char *name); /*__init*/

static inline void nfs_fattr_init(struct nfs_fattr *fattr)
{
	fattr->valid = 0;
	fattr->time_start = jiffies;
}

/*
 * linux/fs/nfs/file.c
 */
extern struct inode_operations nfs_file_inode_operations;
#ifdef CONFIG_NFS_V3
extern struct inode_operations nfs3_file_inode_operations;
#endif /* CONFIG_NFS_V3 */
extern const struct file_operations nfs_file_operations;
extern const struct address_space_operations nfs_file_aops;

static inline struct rpc_cred *nfs_file_cred(struct file *file)
{
	if (file != NULL) {
		struct nfs_open_context *ctx;

		ctx = (struct nfs_open_context*)file->private_data;
		return ctx->cred;
	}
	return NULL;
}

/*
 * linux/fs/nfs/xattr.c
 */
#ifdef CONFIG_NFS_V3_ACL
extern ssize_t nfs3_listxattr(struct dentry *, char *, size_t);
extern ssize_t nfs3_getxattr(struct dentry *, const char *, void *, size_t);
extern int nfs3_setxattr(struct dentry *, const char *,
			const void *, size_t, int);
extern int nfs3_removexattr (struct dentry *, const char *name);
#else
# define nfs3_listxattr NULL
# define nfs3_getxattr NULL
# define nfs3_setxattr NULL
# define nfs3_removexattr NULL
#endif

/*
 * linux/fs/nfs/direct.c
 */
extern ssize_t nfs_direct_IO(int, struct kiocb *, const struct iovec *, loff_t,
			unsigned long);
extern ssize_t nfs_file_direct_read(struct kiocb *iocb,
			const struct iovec *iov, unsigned long nr_segs,
			loff_t pos);
extern ssize_t nfs_file_direct_write(struct kiocb *iocb,
			const struct iovec *iov, unsigned long nr_segs,
			loff_t pos);

/*
 * linux/fs/nfs/dir.c
 */
extern struct inode_operations nfs_dir_inode_operations;
#ifdef CONFIG_NFS_V3
extern struct inode_operations nfs3_dir_inode_operations;
#endif /* CONFIG_NFS_V3 */
extern const struct file_operations nfs_dir_operations;
extern struct dentry_operations nfs_dentry_operations;

extern int nfs_instantiate(struct dentry *dentry, struct nfs_fh *fh, struct nfs_fattr *fattr);

/*
 * linux/fs/nfs/symlink.c
 */
extern struct inode_operations nfs_symlink_inode_operations;

/*
 * linux/fs/nfs/sysctl.c
 */
#ifdef CONFIG_SYSCTL
extern int nfs_register_sysctl(void);
extern void nfs_unregister_sysctl(void);
#else
#define nfs_register_sysctl() 0
#define nfs_unregister_sysctl() do { } while(0)
#endif

/*
 * linux/fs/nfs/namespace.c
 */
extern struct list_head nfs_automount_list;
extern struct inode_operations nfs_mountpoint_inode_operations;
extern struct inode_operations nfs_referral_inode_operations;
extern int nfs_mountpoint_expiry_timeout;
extern void nfs_release_automount_timer(void);

/*
 * linux/fs/nfs/unlink.c
 */
extern int  nfs_async_unlink(struct dentry *);
extern void nfs_complete_unlink(struct dentry *);

/*
 * linux/fs/nfs/write.c
 */
extern int  nfs_writepage(struct page *page, struct writeback_control *wbc);
extern int  nfs_writepages(struct address_space *, struct writeback_control *);
extern int  nfs_flush_incompatible(struct file *file, struct page *page);
extern int  nfs_updatepage(struct file *, struct page *, unsigned int, unsigned int);
extern int nfs_writeback_done(struct rpc_task *, struct nfs_write_data *);
extern void nfs_writedata_release(void *);

/*
 * Try to write back everything synchronously (but check the
 * return value!)
 */
extern long nfs_sync_mapping_wait(struct address_space *, struct writeback_control *, int);
extern int nfs_sync_mapping_range(struct address_space *, loff_t, loff_t, int);
extern int nfs_wb_all(struct inode *inode);
extern int nfs_wb_page(struct inode *inode, struct page* page);
#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
extern int  nfs_commit_inode(struct inode *, int);
extern struct nfs_write_data *nfs_commit_alloc(void);
extern void nfs_commit_free(struct nfs_write_data *wdata);
extern void nfs_commit_release(void *wdata);
#else
static inline int
nfs_commit_inode(struct inode *inode, int how)
{
	return 0;
}
#endif

static inline int
nfs_have_writebacks(struct inode *inode)
{
	return NFS_I(inode)->npages != 0;
}

/*
 * Allocate nfs_write_data structures
 */
extern struct nfs_write_data *nfs_writedata_alloc(size_t len);

/*
 * linux/fs/nfs/read.c
 */
extern int  nfs_readpage(struct file *, struct page *);
extern int  nfs_readpages(struct file *, struct address_space *,
		struct list_head *, unsigned);
extern int  nfs_readpage_result(struct rpc_task *, struct nfs_read_data *);
extern void nfs_readdata_release(void *data);

/*
 * Allocate nfs_read_data structures
 */
extern struct nfs_read_data *nfs_readdata_alloc(size_t len);

/*
 * linux/fs/nfs3proc.c
 */
#ifdef CONFIG_NFS_V3_ACL
extern struct posix_acl *nfs3_proc_getacl(struct inode *inode, int type);
extern int nfs3_proc_setacl(struct inode *inode, int type,
			    struct posix_acl *acl);
extern int nfs3_proc_set_default_acl(struct inode *dir, struct inode *inode,
		mode_t mode);
extern void nfs3_forget_cached_acls(struct inode *inode);
#else
static inline int nfs3_proc_set_default_acl(struct inode *dir,
					    struct inode *inode,
					    mode_t mode)
{
	return 0;
}

static inline void nfs3_forget_cached_acls(struct inode *inode)
{
}
#endif /* CONFIG_NFS_V3_ACL */

/*
 * linux/fs/mount_clnt.c
 * (Used only by nfsroot module)
 */
extern int  nfsroot_mount(struct sockaddr_in *, char *, struct nfs_fh *,
		int, int);

/*
 * inline functions
 */

static inline loff_t
nfs_size_to_loff_t(__u64 size)
{
	loff_t maxsz = (((loff_t) ULONG_MAX) << PAGE_CACHE_SHIFT) + PAGE_CACHE_SIZE - 1;
	if (size > maxsz)
		return maxsz;
	return (loff_t) size;
}

static inline ino_t
nfs_fileid_to_ino_t(u64 fileid)
{
	ino_t ino = (ino_t) fileid;
	if (sizeof(ino_t) < sizeof(u64))
		ino ^= fileid >> (sizeof(u64)-sizeof(ino_t)) * 8;
	return ino;
}

/* NFS root */

extern void * nfs_root_data(void);

#define nfs_wait_event(clnt, wq, condition)				\
({									\
	int __retval = 0;						\
	if (clnt->cl_intr) {						\
		sigset_t oldmask;					\
		rpc_clnt_sigmask(clnt, &oldmask);			\
		__retval = wait_event_interruptible(wq, condition);	\
		rpc_clnt_sigunmask(clnt, &oldmask);			\
	} else								\
		wait_event(wq, condition);				\
	__retval;							\
})

#define NFS_JUKEBOX_RETRY_TIME (5 * HZ)

#endif /* __KERNEL__ */

/*
 * NFS debug flags
 */
#define NFSDBG_VFS		0x0001
#define NFSDBG_DIRCACHE		0x0002
#define NFSDBG_LOOKUPCACHE	0x0004
#define NFSDBG_PAGECACHE	0x0008
#define NFSDBG_PROC		0x0010
#define NFSDBG_XDR		0x0020
#define NFSDBG_FILE		0x0040
#define NFSDBG_ROOT		0x0080
#define NFSDBG_CALLBACK		0x0100
#define NFSDBG_CLIENT		0x0200
#define NFSDBG_ALL		0xFFFF

#ifdef __KERNEL__
# undef ifdebug
# ifdef NFS_DEBUG
#  define ifdebug(fac)		if (unlikely(nfs_debug & NFSDBG_##fac))
# else
#  define ifdebug(fac)		if (0)
# endif
#endif /* __KERNEL */

#endif
