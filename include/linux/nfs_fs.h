/*
 *  linux/include/linux/nfs_fs.h
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  OS-specific nfs filesystem definitions and declarations
 */

#ifndef _LINUX_NFS_FS_H
#define _LINUX_NFS_FS_H

#include <linux/config.h>
#include <linux/in.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rwsem.h>
#include <linux/wait.h>
#include <linux/uio.h>

#include <linux/nfs_fs_sb.h>

#include <linux/sunrpc/debug.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/clnt.h>

#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/nfs_xdr.h>
#include <linux/rwsem.h>
#include <linux/workqueue.h>
#include <linux/mempool.h>

/*
 * Enable debugging support for nfs client.
 * Requires RPC_DEBUG.
 */
#ifdef RPC_DEBUG
# define NFS_DEBUG
#endif

#define NFS_MAX_FILE_IO_BUFFER_SIZE	32768
#define NFS_DEF_FILE_IO_BUFFER_SIZE	4096

/*
 * The upper limit on timeouts for the exponential backoff algorithm.
 */
#define NFS_WRITEBACK_DELAY		(5*HZ)
#define NFS_WRITEBACK_LOCKDELAY		(60*HZ)
#define NFS_COMMIT_DELAY		(5*HZ)

/*
 * superblock magic number for NFS
 */
#define NFS_SUPER_MAGIC			0x6969

/*
 * These are the default flags for swap requests
 */
#define NFS_RPC_SWAPFLAGS		(RPC_TASK_SWAPPER|RPC_TASK_ROOTCREDS)

#define NFS_RW_SYNC		0x0001	/* O_SYNC handling */
#define NFS_RW_SWAP		0x0002	/* This is a swap request */

/*
 * When flushing a cluster of dirty pages, there can be different
 * strategies:
 */
#define FLUSH_AGING		0	/* only flush old buffers */
#define FLUSH_SYNC		1	/* file being synced, or contention */
#define FLUSH_WAIT		2	/* wait for completion */
#define FLUSH_STABLE		4	/* commit to stable storage */
#define FLUSH_LOWPRI		8	/* low priority background flush */
#define FLUSH_HIGHPRI		16	/* high priority memory reclaim flush */

#ifdef __KERNEL__

/*
 * NFSv3/v4 Access mode cache entry
 */
struct nfs_access_entry {
	unsigned long		jiffies;
	struct rpc_cred *	cred;
	int			mask;
};

struct nfs4_state;
struct nfs_open_context {
	atomic_t count;
	struct dentry *dentry;
	struct rpc_cred *cred;
	struct nfs4_state *state;
	fl_owner_t lockowner;
	int mode;
	int error;

	struct list_head list;
	wait_queue_head_t waitq;
};

/*
 * NFSv4 delegation
 */
struct nfs_delegation;

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
	unsigned int		flags;

	/*
	 * read_cache_jiffies is when we started read-caching this inode,
	 * and read_cache_mtime is the mtime of the inode at that time.
	 * attrtimeo is for how long the cached information is assumed
	 * to be valid. A successful attribute revalidation doubles
	 * attrtimeo (up to acregmax/acdirmax), a failure resets it to
	 * acregmin/acdirmin.
	 *
	 * We need to revalidate the cached attrs for this inode if
	 *
	 *	jiffies - read_cache_jiffies > attrtimeo
	 *
	 * and invalidate any cached data/flush out any dirty pages if
	 * we find that
	 *
	 *	mtime != read_cache_mtime
	 */
	unsigned long		readdir_timestamp;
	unsigned long		read_cache_jiffies;
	unsigned long		attrtimeo;
	unsigned long		attrtimeo_timestamp;
	__u64			change_attr;		/* v4 only */

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

	struct nfs_access_entry	cache_access;

	/*
	 * This is the cookie verifier used for NFSv3 readdir
	 * operations
	 */
	__u32			cookieverf[2];

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

	wait_queue_head_t	nfs_i_wait;

#ifdef CONFIG_NFS_V4
        /* NFSv4 state */
	struct list_head	open_states;
	struct nfs_delegation	*delegation;
	int			 delegation_state;
	struct rw_semaphore	rwsem;
#endif /* CONFIG_NFS_V4*/

	struct inode		vfs_inode;
};

/*
 * Legal inode flag values
 */
#define NFS_INO_STALE		0x0001		/* possible stale inode */
#define NFS_INO_ADVISE_RDPLUS   0x0002          /* advise readdirplus */
#define NFS_INO_REVALIDATING	0x0004		/* revalidating attrs */
#define NFS_INO_INVALID_ATTR	0x0008		/* cached attrs are invalid */
#define NFS_INO_INVALID_DATA	0x0010		/* cached data is invalid */
#define NFS_INO_INVALID_ATIME	0x0020		/* cached atime is invalid */
#define NFS_INO_INVALID_ACCESS	0x0040		/* cached access cred invalid */

static inline struct nfs_inode *NFS_I(struct inode *inode)
{
	return container_of(inode, struct nfs_inode, vfs_inode);
}
#define NFS_SB(s)		((struct nfs_server *)(s->s_fs_info))

#define NFS_FH(inode)			(&NFS_I(inode)->fh)
#define NFS_SERVER(inode)		(NFS_SB(inode->i_sb))
#define NFS_CLIENT(inode)		(NFS_SERVER(inode)->client)
#define NFS_PROTO(inode)		(NFS_SERVER(inode)->rpc_ops)
#define NFS_ADDR(inode)			(RPC_PEERADDR(NFS_CLIENT(inode)))
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
#define NFS_REVALIDATING(inode)		(NFS_FLAGS(inode) & NFS_INO_REVALIDATING)
#define NFS_STALE(inode)		(NFS_FLAGS(inode) & NFS_INO_STALE)

#define NFS_FILEID(inode)		(NFS_I(inode)->fileid)

static inline int nfs_caches_unstable(struct inode *inode)
{
	return atomic_read(&NFS_I(inode)->data_updates) != 0;
}

static inline void NFS_CACHEINV(struct inode *inode)
{
	if (!nfs_caches_unstable(inode))
		NFS_FLAGS(inode) |= NFS_INO_INVALID_ATTR | NFS_INO_INVALID_ACCESS;
}

static inline int nfs_server_capable(struct inode *inode, int cap)
{
	return NFS_SERVER(inode)->caps & cap;
}

static inline int NFS_USE_READDIRPLUS(struct inode *inode)
{
	return NFS_FLAGS(inode) & NFS_INO_ADVISE_RDPLUS;
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
		&& chattr == NFS_I(inode)->cache_change_attribute;
}

/*
 * linux/fs/nfs/inode.c
 */
extern void nfs_zap_caches(struct inode *);
extern struct inode *nfs_fhget(struct super_block *, struct nfs_fh *,
				struct nfs_fattr *);
extern int nfs_refresh_inode(struct inode *, struct nfs_fattr *);
extern int nfs_getattr(struct vfsmount *, struct dentry *, struct kstat *);
extern int nfs_permission(struct inode *, int, struct nameidata *);
extern int nfs_access_get_cached(struct inode *, struct rpc_cred *, struct nfs_access_entry *);
extern void nfs_access_add_cache(struct inode *, struct nfs_access_entry *);
extern int nfs_open(struct inode *, struct file *);
extern int nfs_release(struct inode *, struct file *);
extern int nfs_attribute_timeout(struct inode *inode);
extern int nfs_revalidate_inode(struct nfs_server *server, struct inode *inode);
extern int __nfs_revalidate_inode(struct nfs_server *, struct inode *);
extern int nfs_setattr(struct dentry *, struct iattr *);
extern void nfs_begin_attr_update(struct inode *);
extern void nfs_end_attr_update(struct inode *);
extern void nfs_begin_data_update(struct inode *);
extern void nfs_end_data_update(struct inode *);
extern void nfs_end_data_update_defer(struct inode *);
extern struct nfs_open_context *alloc_nfs_open_context(struct dentry *dentry, struct rpc_cred *cred);
extern struct nfs_open_context *get_nfs_open_context(struct nfs_open_context *ctx);
extern void put_nfs_open_context(struct nfs_open_context *ctx);
extern void nfs_file_set_open_context(struct file *filp, struct nfs_open_context *ctx);
extern struct nfs_open_context *nfs_find_open_context(struct inode *inode, int mode);
extern void nfs_file_clear_open_context(struct file *filp);

/* linux/net/ipv4/ipconfig.c: trims ip addr off front of name, too. */
extern u32 root_nfs_parse_addr(char *name); /*__init*/

/*
 * linux/fs/nfs/file.c
 */
extern struct inode_operations nfs_file_inode_operations;
extern struct file_operations nfs_file_operations;
extern struct address_space_operations nfs_file_aops;

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
 * linux/fs/nfs/direct.c
 */
extern ssize_t nfs_direct_IO(int, struct kiocb *, const struct iovec *, loff_t,
			unsigned long);
extern ssize_t nfs_file_direct_read(struct kiocb *iocb, char __user *buf,
			size_t count, loff_t pos);
extern ssize_t nfs_file_direct_write(struct kiocb *iocb, const char __user *buf,
			size_t count, loff_t pos);

/*
 * linux/fs/nfs/dir.c
 */
extern struct inode_operations nfs_dir_inode_operations;
extern struct file_operations nfs_dir_operations;
extern struct dentry_operations nfs_dentry_operations;

extern int nfs_instantiate(struct dentry *dentry, struct nfs_fh *fh, struct nfs_fattr *fattr);

/*
 * linux/fs/nfs/symlink.c
 */
extern struct inode_operations nfs_symlink_inode_operations;

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
extern void nfs_writeback_done(struct rpc_task *task);

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
extern void nfs_commit_done(struct rpc_task *);
#endif

/*
 * Try to write back everything synchronously (but check the
 * return value!)
 */
extern int  nfs_sync_inode(struct inode *, unsigned long, unsigned int, int);
#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
extern int  nfs_commit_inode(struct inode *, unsigned long, unsigned int, int);
#else
static inline int
nfs_commit_inode(struct inode *inode, unsigned long idx_start, unsigned int npages, int how)
{
	return 0;
}
#endif

static inline int
nfs_have_writebacks(struct inode *inode)
{
	return NFS_I(inode)->npages != 0;
}

static inline int
nfs_wb_all(struct inode *inode)
{
	int error = nfs_sync_inode(inode, 0, 0, FLUSH_WAIT);
	return (error < 0) ? error : 0;
}

/*
 * Write back all requests on one page - we do this before reading it.
 */
static inline int nfs_wb_page_priority(struct inode *inode, struct page* page, int how)
{
	int error = nfs_sync_inode(inode, page->index, 1,
			how | FLUSH_WAIT | FLUSH_STABLE);
	return (error < 0) ? error : 0;
}

static inline int nfs_wb_page(struct inode *inode, struct page* page)
{
	return nfs_wb_page_priority(inode, page, 0);
}

/*
 * Allocate and free nfs_write_data structures
 */
extern mempool_t *nfs_wdata_mempool;

static inline struct nfs_write_data *nfs_writedata_alloc(void)
{
	struct nfs_write_data *p = mempool_alloc(nfs_wdata_mempool, SLAB_NOFS);
	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
	}
	return p;
}

static inline void nfs_writedata_free(struct nfs_write_data *p)
{
	mempool_free(p, nfs_wdata_mempool);
}

/* Hack for future NFS swap support */
#ifndef IS_SWAPFILE
# define IS_SWAPFILE(inode)	(0)
#endif

/*
 * linux/fs/nfs/read.c
 */
extern int  nfs_readpage(struct file *, struct page *);
extern int  nfs_readpages(struct file *, struct address_space *,
		struct list_head *, unsigned);
extern void nfs_readpage_result(struct rpc_task *);

/*
 * Allocate and free nfs_read_data structures
 */
extern mempool_t *nfs_rdata_mempool;

static inline struct nfs_read_data *nfs_readdata_alloc(void)
{
	struct nfs_read_data *p = mempool_alloc(nfs_rdata_mempool, SLAB_NOFS);
	if (p)
		memset(p, 0, sizeof(*p));
	return p;
}

static inline void nfs_readdata_free(struct nfs_read_data *p)
{
	mempool_free(p, nfs_rdata_mempool);
}

extern void  nfs_readdata_release(struct rpc_task *task);

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

#ifdef CONFIG_NFS_V4

struct idmap;

/*
 * In a seqid-mutating op, this macro controls which error return
 * values trigger incrementation of the seqid.
 *
 * from rfc 3010:
 * The client MUST monotonically increment the sequence number for the
 * CLOSE, LOCK, LOCKU, OPEN, OPEN_CONFIRM, and OPEN_DOWNGRADE
 * operations.  This is true even in the event that the previous
 * operation that used the sequence number received an error.  The only
 * exception to this rule is if the previous operation received one of
 * the following errors: NFSERR_STALE_CLIENTID, NFSERR_STALE_STATEID,
 * NFSERR_BAD_STATEID, NFSERR_BAD_SEQID, NFSERR_BADXDR,
 * NFSERR_RESOURCE, NFSERR_NOFILEHANDLE.
 *
 */
#define seqid_mutating_err(err)       \
(((err) != NFSERR_STALE_CLIENTID) &&  \
 ((err) != NFSERR_STALE_STATEID)  &&  \
 ((err) != NFSERR_BAD_STATEID)    &&  \
 ((err) != NFSERR_BAD_SEQID)      &&  \
 ((err) != NFSERR_BAD_XDR)        &&  \
 ((err) != NFSERR_RESOURCE)       &&  \
 ((err) != NFSERR_NOFILEHANDLE))

enum nfs4_client_state {
	NFS4CLNT_OK  = 0,
};

/*
 * The nfs4_client identifies our client state to the server.
 */
struct nfs4_client {
	struct list_head	cl_servers;	/* Global list of servers */
	struct in_addr		cl_addr;	/* Server identifier */
	u64			cl_clientid;	/* constant */
	nfs4_verifier		cl_confirm;
	unsigned long		cl_state;

	u32			cl_lockowner_id;

	/*
	 * The following rwsem ensures exclusive access to the server
	 * while we recover the state following a lease expiration.
	 */
	struct rw_semaphore	cl_sem;

	struct list_head	cl_delegations;
	struct list_head	cl_state_owners;
	struct list_head	cl_unused;
	int			cl_nunused;
	spinlock_t		cl_lock;
	atomic_t		cl_count;

	struct rpc_clnt *	cl_rpcclient;
	struct rpc_cred *	cl_cred;

	struct list_head	cl_superblocks;	/* List of nfs_server structs */

	unsigned long		cl_lease_time;
	unsigned long		cl_last_renewal;
	struct work_struct	cl_renewd;
	struct work_struct	cl_recoverd;

	wait_queue_head_t	cl_waitq;
	struct rpc_wait_queue	cl_rpcwaitq;

	/* used for the setclientid verifier */
	struct timespec		cl_boot_time;

	/* idmapper */
	struct idmap *		cl_idmap;

	/* Our own IP address, as a null-terminated string.
	 * This is used to generate the clientid, and the callback address.
	 */
	char			cl_ipaddr[16];
	unsigned char		cl_id_uniquifier;
};

/*
 * NFS4 state_owners and lock_owners are simply labels for ordered
 * sequences of RPC calls. Their sole purpose is to provide once-only
 * semantics by allowing the server to identify replayed requests.
 *
 * The ->so_sema is held during all state_owner seqid-mutating operations:
 * OPEN, OPEN_DOWNGRADE, and CLOSE. Its purpose is to properly serialize
 * so_seqid.
 */
struct nfs4_state_owner {
	struct list_head     so_list;	 /* per-clientid list of state_owners */
	struct nfs4_client   *so_client;
	u32                  so_id;      /* 32-bit identifier, unique */
	struct semaphore     so_sema;
	u32                  so_seqid;   /* protected by so_sema */
	atomic_t	     so_count;

	struct rpc_cred	     *so_cred;	 /* Associated cred */
	struct list_head     so_states;
	struct list_head     so_delegations;
};

/*
 * struct nfs4_state maintains the client-side state for a given
 * (state_owner,inode) tuple (OPEN) or state_owner (LOCK).
 *
 * OPEN:
 * In order to know when to OPEN_DOWNGRADE or CLOSE the state on the server,
 * we need to know how many files are open for reading or writing on a
 * given inode. This information too is stored here.
 *
 * LOCK: one nfs4_state (LOCK) to hold the lock stateid nfs4_state(OPEN)
 */

struct nfs4_lock_state {
	struct list_head	ls_locks;	/* Other lock stateids */
	fl_owner_t		ls_owner;	/* POSIX lock owner */
#define NFS_LOCK_INITIALIZED 1
	int			ls_flags;
	u32			ls_seqid;
	u32			ls_id;
	nfs4_stateid		ls_stateid;
	atomic_t		ls_count;
};

/* bits for nfs4_state->flags */
enum {
	LK_STATE_IN_USE,
	NFS_DELEGATED_STATE,
};

struct nfs4_state {
	struct list_head open_states;	/* List of states for the same state_owner */
	struct list_head inode_states;	/* List of states for the same inode */
	struct list_head lock_states;	/* List of subservient lock stateids */

	struct nfs4_state_owner *owner;	/* Pointer to the open owner */
	struct inode *inode;		/* Pointer to the inode */

	unsigned long flags;		/* Do we hold any locks? */
	struct semaphore lock_sema;	/* Serializes file locking operations */
	rwlock_t state_lock;		/* Protects the lock_states list */

	nfs4_stateid stateid;

	unsigned int nreaders;
	unsigned int nwriters;
	int state;			/* State on the server (R,W, or RW) */
	atomic_t count;
};


struct nfs4_exception {
	long timeout;
	int retry;
};

struct nfs4_state_recovery_ops {
	int (*recover_open)(struct nfs4_state_owner *, struct nfs4_state *);
	int (*recover_lock)(struct nfs4_state *, struct file_lock *);
};

extern struct dentry_operations nfs4_dentry_operations;
extern struct inode_operations nfs4_dir_inode_operations;

/* nfs4proc.c */
extern int nfs4_map_errors(int err);
extern int nfs4_proc_setclientid(struct nfs4_client *, u32, unsigned short);
extern int nfs4_proc_setclientid_confirm(struct nfs4_client *);
extern int nfs4_proc_async_renew(struct nfs4_client *);
extern int nfs4_proc_renew(struct nfs4_client *);
extern int nfs4_do_close(struct inode *inode, struct nfs4_state *state, mode_t mode);
extern struct inode *nfs4_atomic_open(struct inode *, struct dentry *, struct nameidata *);
extern int nfs4_open_revalidate(struct inode *, struct dentry *, int);

extern struct nfs4_state_recovery_ops nfs4_reboot_recovery_ops;
extern struct nfs4_state_recovery_ops nfs4_network_partition_recovery_ops;

/* nfs4renewd.c */
extern void nfs4_schedule_state_renewal(struct nfs4_client *);
extern void nfs4_renewd_prepare_shutdown(struct nfs_server *);
extern void nfs4_kill_renewd(struct nfs4_client *);

/* nfs4state.c */
extern void init_nfsv4_state(struct nfs_server *);
extern void destroy_nfsv4_state(struct nfs_server *);
extern struct nfs4_client *nfs4_get_client(struct in_addr *);
extern void nfs4_put_client(struct nfs4_client *clp);
extern int nfs4_init_client(struct nfs4_client *clp);
extern struct nfs4_client *nfs4_find_client(struct in_addr *);
extern u32 nfs4_alloc_lockowner_id(struct nfs4_client *);

extern struct nfs4_state_owner * nfs4_get_state_owner(struct nfs_server *, struct rpc_cred *);
extern void nfs4_put_state_owner(struct nfs4_state_owner *);
extern void nfs4_drop_state_owner(struct nfs4_state_owner *);
extern struct nfs4_state * nfs4_get_open_state(struct inode *, struct nfs4_state_owner *);
extern void nfs4_put_open_state(struct nfs4_state *);
extern void nfs4_close_state(struct nfs4_state *, mode_t);
extern struct nfs4_state *nfs4_find_state(struct inode *, struct rpc_cred *, mode_t mode);
extern void nfs4_increment_seqid(int status, struct nfs4_state_owner *sp);
extern void nfs4_schedule_state_recovery(struct nfs4_client *);
extern struct nfs4_lock_state *nfs4_find_lock_state(struct nfs4_state *state, fl_owner_t);
extern struct nfs4_lock_state *nfs4_get_lock_state(struct nfs4_state *state, fl_owner_t);
extern void nfs4_put_lock_state(struct nfs4_lock_state *state);
extern void nfs4_increment_lock_seqid(int status, struct nfs4_lock_state *ls);
extern void nfs4_notify_setlk(struct nfs4_state *, struct file_lock *, struct nfs4_lock_state *);
extern void nfs4_notify_unlck(struct nfs4_state *, struct file_lock *, struct nfs4_lock_state *);
extern void nfs4_copy_stateid(nfs4_stateid *, struct nfs4_state *, fl_owner_t);



struct nfs4_mount_data;
#else
#define init_nfsv4_state(server)  do { } while (0)
#define destroy_nfsv4_state(server)       do { } while (0)
#define nfs4_put_state_owner(inode, owner) do { } while (0)
#define nfs4_put_open_state(state) do { } while (0)
#define nfs4_close_state(a, b) do { } while (0)
#define nfs4_renewd_prepare_shutdown(server) do { } while (0)
#endif

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
