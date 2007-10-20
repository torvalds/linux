/*
 * linux/fs/nfs/nfs4_fs.h
 *
 * Copyright (C) 2005 Trond Myklebust
 *
 * NFSv4-specific filesystem definitions and declarations
 */

#ifndef __LINUX_FS_NFS_NFS4_FS_H
#define __LINUX_FS_NFS_NFS4_FS_H

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
	NFS4CLNT_STATE_RECOVER  = 0,
	NFS4CLNT_LEASE_EXPIRED,
};

/*
 * struct rpc_sequence ensures that RPC calls are sent in the exact
 * order that they appear on the list.
 */
struct rpc_sequence {
	struct rpc_wait_queue	wait;	/* RPC call delay queue */
	spinlock_t lock;		/* Protects the list */
	struct list_head list;		/* Defines sequence of RPC calls */
};

#define NFS_SEQID_CONFIRMED 1
struct nfs_seqid_counter {
	struct rpc_sequence *sequence;
	int flags;
	u32 counter;
};

struct nfs_seqid {
	struct nfs_seqid_counter *sequence;
	struct list_head list;
};

static inline void nfs_confirm_seqid(struct nfs_seqid_counter *seqid, int status)
{
	if (seqid_mutating_err(-status))
		seqid->flags |= NFS_SEQID_CONFIRMED;
}

struct nfs_unique_id {
	struct rb_node rb_node;
	__u64 id;
};

/*
 * NFS4 state_owners and lock_owners are simply labels for ordered
 * sequences of RPC calls. Their sole purpose is to provide once-only
 * semantics by allowing the server to identify replayed requests.
 */
struct nfs4_state_owner {
	struct nfs_unique_id so_owner_id;
	struct nfs_client    *so_client;
	struct nfs_server    *so_server;
	struct rb_node	     so_client_node;

	struct rpc_cred	     *so_cred;	 /* Associated cred */

	spinlock_t	     so_lock;
	atomic_t	     so_count;
	struct list_head     so_states;
	struct list_head     so_delegations;
	struct nfs_seqid_counter so_seqid;
	struct rpc_sequence  so_sequence;
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
	struct nfs4_state *	ls_state;	/* Pointer to open state */
	fl_owner_t		ls_owner;	/* POSIX lock owner */
#define NFS_LOCK_INITIALIZED 1
	int			ls_flags;
	struct nfs_seqid_counter	ls_seqid;
	struct nfs_unique_id	ls_id;
	nfs4_stateid		ls_stateid;
	atomic_t		ls_count;
};

/* bits for nfs4_state->flags */
enum {
	LK_STATE_IN_USE,
	NFS_DELEGATED_STATE,		/* Current stateid is delegation */
	NFS_O_RDONLY_STATE,		/* OPEN stateid has read-only state */
	NFS_O_WRONLY_STATE,		/* OPEN stateid has write-only state */
	NFS_O_RDWR_STATE,		/* OPEN stateid has read/write state */
};

struct nfs4_state {
	struct list_head open_states;	/* List of states for the same state_owner */
	struct list_head inode_states;	/* List of states for the same inode */
	struct list_head lock_states;	/* List of subservient lock stateids */

	struct nfs4_state_owner *owner;	/* Pointer to the open owner */
	struct inode *inode;		/* Pointer to the inode */

	unsigned long flags;		/* Do we hold any locks? */
	spinlock_t state_lock;		/* Protects the lock_states list */

	seqlock_t seqlock;		/* Protects the stateid/open_stateid */
	nfs4_stateid stateid;		/* Current stateid: may be delegation */
	nfs4_stateid open_stateid;	/* OPEN stateid */

	/* The following 3 fields are protected by owner->so_lock */
	unsigned int n_rdonly;		/* Number of read-only references */
	unsigned int n_wronly;		/* Number of write-only references */
	unsigned int n_rdwr;		/* Number of read/write references */
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
extern const struct inode_operations nfs4_dir_inode_operations;

/* inode.c */
extern ssize_t nfs4_getxattr(struct dentry *, const char *, void *, size_t);
extern int nfs4_setxattr(struct dentry *, const char *, const void *, size_t, int);
extern ssize_t nfs4_listxattr(struct dentry *, char *, size_t);


/* nfs4proc.c */
extern int nfs4_map_errors(int err);
extern int nfs4_proc_setclientid(struct nfs_client *, u32, unsigned short, struct rpc_cred *);
extern int nfs4_proc_setclientid_confirm(struct nfs_client *, struct rpc_cred *);
extern int nfs4_proc_async_renew(struct nfs_client *, struct rpc_cred *);
extern int nfs4_proc_renew(struct nfs_client *, struct rpc_cred *);
extern int nfs4_do_close(struct path *path, struct nfs4_state *state, int wait);
extern struct dentry *nfs4_atomic_open(struct inode *, struct dentry *, struct nameidata *);
extern int nfs4_open_revalidate(struct inode *, struct dentry *, int, struct nameidata *);
extern int nfs4_server_capabilities(struct nfs_server *server, struct nfs_fh *fhandle);
extern int nfs4_proc_fs_locations(struct inode *dir, const struct qstr *name,
		struct nfs4_fs_locations *fs_locations, struct page *page);

extern struct nfs4_state_recovery_ops nfs4_reboot_recovery_ops;
extern struct nfs4_state_recovery_ops nfs4_network_partition_recovery_ops;

extern const u32 nfs4_fattr_bitmap[2];
extern const u32 nfs4_statfs_bitmap[2];
extern const u32 nfs4_pathconf_bitmap[2];
extern const u32 nfs4_fsinfo_bitmap[2];
extern const u32 nfs4_fs_locations_bitmap[2];

/* nfs4renewd.c */
extern void nfs4_schedule_state_renewal(struct nfs_client *);
extern void nfs4_renewd_prepare_shutdown(struct nfs_server *);
extern void nfs4_kill_renewd(struct nfs_client *);
extern void nfs4_renew_state(struct work_struct *);

/* nfs4state.c */
struct rpc_cred *nfs4_get_renew_cred(struct nfs_client *clp);

extern struct nfs4_state_owner * nfs4_get_state_owner(struct nfs_server *, struct rpc_cred *);
extern void nfs4_put_state_owner(struct nfs4_state_owner *);
extern void nfs4_drop_state_owner(struct nfs4_state_owner *);
extern struct nfs4_state * nfs4_get_open_state(struct inode *, struct nfs4_state_owner *);
extern void nfs4_put_open_state(struct nfs4_state *);
extern void nfs4_close_state(struct path *, struct nfs4_state *, mode_t);
extern void nfs4_close_sync(struct path *, struct nfs4_state *, mode_t);
extern void nfs4_state_set_mode_locked(struct nfs4_state *, mode_t);
extern void nfs4_schedule_state_recovery(struct nfs_client *);
extern void nfs4_put_lock_state(struct nfs4_lock_state *lsp);
extern int nfs4_set_lock_state(struct nfs4_state *state, struct file_lock *fl);
extern void nfs4_copy_stateid(nfs4_stateid *, struct nfs4_state *, fl_owner_t);

extern struct nfs_seqid *nfs_alloc_seqid(struct nfs_seqid_counter *counter);
extern int nfs_wait_on_sequence(struct nfs_seqid *seqid, struct rpc_task *task);
extern void nfs_increment_open_seqid(int status, struct nfs_seqid *seqid);
extern void nfs_increment_lock_seqid(int status, struct nfs_seqid *seqid);
extern void nfs_free_seqid(struct nfs_seqid *seqid);

extern const nfs4_stateid zero_stateid;

/* nfs4xdr.c */
extern __be32 *nfs4_decode_dirent(__be32 *p, struct nfs_entry *entry, int plus);
extern struct rpc_procinfo nfs4_procedures[];

struct nfs4_mount_data;

/* callback_xdr.c */
extern struct svc_version nfs4_callback_version1;

#else

#define nfs4_close_state(a, b, c) do { } while (0)
#define nfs4_close_sync(a, b, c) do { } while (0)

#endif /* CONFIG_NFS_V4 */
#endif /* __LINUX_FS_NFS_NFS4_FS.H */
