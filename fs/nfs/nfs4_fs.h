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

enum nfs4_client_state {
	NFS4CLNT_MANAGER_RUNNING  = 0,
	NFS4CLNT_CHECK_LEASE,
	NFS4CLNT_LEASE_EXPIRED,
	NFS4CLNT_RECLAIM_REBOOT,
	NFS4CLNT_RECLAIM_NOGRACE,
	NFS4CLNT_DELEGRETURN,
	NFS4CLNT_LAYOUTRECALL,
	NFS4CLNT_SESSION_RESET,
	NFS4CLNT_RECALL_SLOT,
	NFS4CLNT_LEASE_CONFIRM,
	NFS4CLNT_SERVER_SCOPE_MISMATCH,
};

enum nfs4_session_state {
	NFS4_SESSION_INITING,
	NFS4_SESSION_DRAINING,
};

#define NFS4_RENEW_TIMEOUT		0x01
#define NFS4_RENEW_DELEGATION_CB	0x02

struct nfs4_minor_version_ops {
	u32	minor_version;

	int	(*call_sync)(struct rpc_clnt *clnt,
			struct nfs_server *server,
			struct rpc_message *msg,
			struct nfs4_sequence_args *args,
			struct nfs4_sequence_res *res,
			int cache_reply);
	int	(*validate_stateid)(struct nfs_delegation *,
			const nfs4_stateid *);
	int	(*find_root_sec)(struct nfs_server *, struct nfs_fh *,
			struct nfs_fsinfo *);
	const struct nfs4_state_recovery_ops *reboot_recovery_ops;
	const struct nfs4_state_recovery_ops *nograce_recovery_ops;
	const struct nfs4_state_maintenance_ops *state_renewal_ops;
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
	struct nfs_server    *so_server;
	struct list_head     so_lru;
	unsigned long        so_expires;
	struct rb_node	     so_server_node;

	struct rpc_cred	     *so_cred;	 /* Associated cred */

	spinlock_t	     so_lock;
	atomic_t	     so_count;
	unsigned long	     so_flags;
	struct list_head     so_states;
	struct nfs_seqid_counter so_seqid;
	struct rpc_sequence  so_sequence;
};

enum {
	NFS_OWNER_RECLAIM_REBOOT,
	NFS_OWNER_RECLAIM_NOGRACE
};

#define NFS_LOCK_NEW		0
#define NFS_LOCK_RECLAIM	1
#define NFS_LOCK_EXPIRED	2

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

struct nfs4_lock_owner {
	unsigned int lo_type;
#define NFS4_ANY_LOCK_TYPE	(0U)
#define NFS4_FLOCK_LOCK_TYPE	(1U << 0)
#define NFS4_POSIX_LOCK_TYPE	(1U << 1)
	union {
		fl_owner_t posix_owner;
		pid_t flock_owner;
	} lo_u;
};

struct nfs4_lock_state {
	struct list_head	ls_locks;	/* Other lock stateids */
	struct nfs4_state *	ls_state;	/* Pointer to open state */
#define NFS_LOCK_INITIALIZED 1
	int			ls_flags;
	struct nfs_seqid_counter	ls_seqid;
	struct rpc_sequence	ls_sequence;
	struct nfs_unique_id	ls_id;
	nfs4_stateid		ls_stateid;
	atomic_t		ls_count;
	struct nfs4_lock_owner	ls_owner;
};

/* bits for nfs4_state->flags */
enum {
	LK_STATE_IN_USE,
	NFS_DELEGATED_STATE,		/* Current stateid is delegation */
	NFS_O_RDONLY_STATE,		/* OPEN stateid has read-only state */
	NFS_O_WRONLY_STATE,		/* OPEN stateid has write-only state */
	NFS_O_RDWR_STATE,		/* OPEN stateid has read/write state */
	NFS_STATE_RECLAIM_REBOOT,	/* OPEN stateid server rebooted */
	NFS_STATE_RECLAIM_NOGRACE,	/* OPEN stateid needs to recover state */
	NFS_STATE_POSIX_LOCKS,		/* Posix locks are supported */
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
	fmode_t state;			/* State on the server (R,W, or RW) */
	atomic_t count;
};


struct nfs4_exception {
	long timeout;
	int retry;
	struct nfs4_state *state;
};

struct nfs4_state_recovery_ops {
	int owner_flag_bit;
	int state_flag_bit;
	int (*recover_open)(struct nfs4_state_owner *, struct nfs4_state *);
	int (*recover_lock)(struct nfs4_state *, struct file_lock *);
	int (*establish_clid)(struct nfs_client *, struct rpc_cred *);
	struct rpc_cred * (*get_clid_cred)(struct nfs_client *);
	int (*reclaim_complete)(struct nfs_client *);
};

struct nfs4_state_maintenance_ops {
	int (*sched_state_renewal)(struct nfs_client *, struct rpc_cred *, unsigned);
	struct rpc_cred * (*get_state_renewal_cred_locked)(struct nfs_client *);
	int (*renew_lease)(struct nfs_client *, struct rpc_cred *);
};

extern const struct dentry_operations nfs4_dentry_operations;
extern const struct inode_operations nfs4_dir_inode_operations;

/* nfs4proc.c */
extern int nfs4_proc_setclientid(struct nfs_client *, u32, unsigned short, struct rpc_cred *, struct nfs4_setclientid_res *);
extern int nfs4_proc_setclientid_confirm(struct nfs_client *, struct nfs4_setclientid_res *arg, struct rpc_cred *);
extern int nfs4_proc_exchange_id(struct nfs_client *clp, struct rpc_cred *cred);
extern int nfs4_init_clientid(struct nfs_client *, struct rpc_cred *);
extern int nfs41_init_clientid(struct nfs_client *, struct rpc_cred *);
extern int nfs4_do_close(struct nfs4_state *state, gfp_t gfp_mask, int wait, bool roc);
extern int nfs4_server_capabilities(struct nfs_server *server, struct nfs_fh *fhandle);
extern int nfs4_proc_fs_locations(struct inode *dir, const struct qstr *name,
		struct nfs4_fs_locations *fs_locations, struct page *page);
extern void nfs4_release_lockowner(const struct nfs4_lock_state *);
extern const struct xattr_handler *nfs4_xattr_handlers[];

#if defined(CONFIG_NFS_V4_1)
static inline struct nfs4_session *nfs4_get_session(const struct nfs_server *server)
{
	return server->nfs_client->cl_session;
}

extern int nfs4_setup_sequence(const struct nfs_server *server,
		struct nfs4_sequence_args *args, struct nfs4_sequence_res *res,
		int cache_reply, struct rpc_task *task);
extern int nfs41_setup_sequence(struct nfs4_session *session,
		struct nfs4_sequence_args *args, struct nfs4_sequence_res *res,
		int cache_reply, struct rpc_task *task);
extern void nfs4_destroy_session(struct nfs4_session *session);
extern struct nfs4_session *nfs4_alloc_session(struct nfs_client *clp);
extern int nfs4_proc_create_session(struct nfs_client *);
extern int nfs4_proc_destroy_session(struct nfs4_session *);
extern int nfs4_init_session(struct nfs_server *server);
extern int nfs4_proc_get_lease_time(struct nfs_client *clp,
		struct nfs_fsinfo *fsinfo);
extern int nfs4_proc_layoutcommit(struct nfs4_layoutcommit_data *data,
				  bool sync);

static inline bool
is_ds_only_client(struct nfs_client *clp)
{
	return (clp->cl_exchange_flags & EXCHGID4_FLAG_MASK_PNFS) ==
		EXCHGID4_FLAG_USE_PNFS_DS;
}

static inline bool
is_ds_client(struct nfs_client *clp)
{
	return clp->cl_exchange_flags & EXCHGID4_FLAG_USE_PNFS_DS;
}
#else /* CONFIG_NFS_v4_1 */
static inline struct nfs4_session *nfs4_get_session(const struct nfs_server *server)
{
	return NULL;
}

static inline int nfs4_setup_sequence(const struct nfs_server *server,
		struct nfs4_sequence_args *args, struct nfs4_sequence_res *res,
		int cache_reply, struct rpc_task *task)
{
	return 0;
}

static inline int nfs4_init_session(struct nfs_server *server)
{
	return 0;
}

static inline bool
is_ds_only_client(struct nfs_client *clp)
{
	return false;
}

static inline bool
is_ds_client(struct nfs_client *clp)
{
	return false;
}
#endif /* CONFIG_NFS_V4_1 */

extern const struct nfs4_minor_version_ops *nfs_v4_minor_ops[];

extern const u32 nfs4_fattr_bitmap[2];
extern const u32 nfs4_statfs_bitmap[2];
extern const u32 nfs4_pathconf_bitmap[2];
extern const u32 nfs4_fsinfo_bitmap[3];
extern const u32 nfs4_fs_locations_bitmap[2];

/* nfs4renewd.c */
extern void nfs4_schedule_state_renewal(struct nfs_client *);
extern void nfs4_renewd_prepare_shutdown(struct nfs_server *);
extern void nfs4_kill_renewd(struct nfs_client *);
extern void nfs4_renew_state(struct work_struct *);

/* nfs4state.c */
struct rpc_cred *nfs4_get_setclientid_cred(struct nfs_client *clp);
struct rpc_cred *nfs4_get_renew_cred_locked(struct nfs_client *clp);
#if defined(CONFIG_NFS_V4_1)
struct rpc_cred *nfs4_get_machine_cred_locked(struct nfs_client *clp);
struct rpc_cred *nfs4_get_exchange_id_cred(struct nfs_client *clp);
extern void nfs4_schedule_session_recovery(struct nfs4_session *);
#else
static inline void nfs4_schedule_session_recovery(struct nfs4_session *session)
{
}
#endif /* CONFIG_NFS_V4_1 */

extern struct nfs4_state_owner * nfs4_get_state_owner(struct nfs_server *, struct rpc_cred *);
extern void nfs4_put_state_owner(struct nfs4_state_owner *);
extern void nfs4_purge_state_owners(struct nfs_server *);
extern struct nfs4_state * nfs4_get_open_state(struct inode *, struct nfs4_state_owner *);
extern void nfs4_put_open_state(struct nfs4_state *);
extern void nfs4_close_state(struct nfs4_state *, fmode_t);
extern void nfs4_close_sync(struct nfs4_state *, fmode_t);
extern void nfs4_state_set_mode_locked(struct nfs4_state *, fmode_t);
extern void nfs4_schedule_lease_recovery(struct nfs_client *);
extern void nfs4_schedule_state_manager(struct nfs_client *);
extern void nfs4_schedule_path_down_recovery(struct nfs_client *clp);
extern void nfs4_schedule_stateid_recovery(const struct nfs_server *, struct nfs4_state *);
extern void nfs41_handle_sequence_flag_errors(struct nfs_client *clp, u32 flags);
extern void nfs41_handle_recall_slot(struct nfs_client *clp);
extern void nfs41_handle_server_scope(struct nfs_client *,
				      struct server_scope **);
extern void nfs4_put_lock_state(struct nfs4_lock_state *lsp);
extern int nfs4_set_lock_state(struct nfs4_state *state, struct file_lock *fl);
extern void nfs4_copy_stateid(nfs4_stateid *, struct nfs4_state *, fl_owner_t, pid_t);

extern struct nfs_seqid *nfs_alloc_seqid(struct nfs_seqid_counter *counter, gfp_t gfp_mask);
extern int nfs_wait_on_sequence(struct nfs_seqid *seqid, struct rpc_task *task);
extern void nfs_increment_open_seqid(int status, struct nfs_seqid *seqid);
extern void nfs_increment_lock_seqid(int status, struct nfs_seqid *seqid);
extern void nfs_release_seqid(struct nfs_seqid *seqid);
extern void nfs_free_seqid(struct nfs_seqid *seqid);

extern const nfs4_stateid zero_stateid;

/* nfs4xdr.c */
extern struct rpc_procinfo nfs4_procedures[];

struct nfs4_mount_data;

/* callback_xdr.c */
extern struct svc_version nfs4_callback_version1;
extern struct svc_version nfs4_callback_version4;

#else

#define nfs4_close_state(a, b) do { } while (0)
#define nfs4_close_sync(a, b) do { } while (0)

#endif /* CONFIG_NFS_V4 */
#endif /* __LINUX_FS_NFS_NFS4_FS.H */
