/*
 * linux/fs/nfs/nfs4_fs.h
 *
 * Copyright (C) 2005 Trond Myklebust
 *
 * NFSv4-specific filesystem definitions and declarations
 */

#ifndef __LINUX_FS_NFS_NFS4_FS_H
#define __LINUX_FS_NFS_NFS4_FS_H

#if defined(CONFIG_NFS_V4_2)
#define NFS4_MAX_MINOR_VERSION 2
#elif defined(CONFIG_NFS_V4_1)
#define NFS4_MAX_MINOR_VERSION 1
#else
#define NFS4_MAX_MINOR_VERSION 0
#endif

#if IS_ENABLED(CONFIG_NFS_V4)

#define NFS4_MAX_LOOP_ON_RECOVER (10)

#include <linux/seqlock.h>

struct idmap;

enum nfs4_client_state {
	NFS4CLNT_MANAGER_RUNNING  = 0,
	NFS4CLNT_CHECK_LEASE,
	NFS4CLNT_LEASE_EXPIRED,
	NFS4CLNT_RECLAIM_REBOOT,
	NFS4CLNT_RECLAIM_NOGRACE,
	NFS4CLNT_DELEGRETURN,
	NFS4CLNT_SESSION_RESET,
	NFS4CLNT_LEASE_CONFIRM,
	NFS4CLNT_SERVER_SCOPE_MISMATCH,
	NFS4CLNT_PURGE_STATE,
	NFS4CLNT_BIND_CONN_TO_SESSION,
	NFS4CLNT_MOVED,
	NFS4CLNT_LEASE_MOVED,
	NFS4CLNT_DELEGATION_EXPIRED,
};

#define NFS4_RENEW_TIMEOUT		0x01
#define NFS4_RENEW_DELEGATION_CB	0x02

struct nfs_seqid_counter;
struct nfs4_minor_version_ops {
	u32	minor_version;
	unsigned init_caps;

	int	(*init_client)(struct nfs_client *);
	void	(*shutdown_client)(struct nfs_client *);
	bool	(*match_stateid)(const nfs4_stateid *,
			const nfs4_stateid *);
	int	(*find_root_sec)(struct nfs_server *, struct nfs_fh *,
			struct nfs_fsinfo *);
	void	(*free_lock_state)(struct nfs_server *,
			struct nfs4_lock_state *);
	int	(*test_and_free_expired)(struct nfs_server *,
			nfs4_stateid *, struct rpc_cred *);
	struct nfs_seqid *
		(*alloc_seqid)(struct nfs_seqid_counter *, gfp_t);
	int	(*session_trunk)(struct rpc_clnt *, struct rpc_xprt *, void *);
	const struct rpc_call_ops *call_sync_ops;
	const struct nfs4_state_recovery_ops *reboot_recovery_ops;
	const struct nfs4_state_recovery_ops *nograce_recovery_ops;
	const struct nfs4_state_maintenance_ops *state_renewal_ops;
	const struct nfs4_mig_recovery_ops *mig_recovery_ops;
};

#define NFS_SEQID_CONFIRMED 1
struct nfs_seqid_counter {
	ktime_t create_time;
	int owner_id;
	int flags;
	u32 counter;
	spinlock_t lock;		/* Protects the list */
	struct list_head list;		/* Defines sequence of RPC calls */
	struct rpc_wait_queue	wait;	/* RPC call delay queue */
};

struct nfs_seqid {
	struct nfs_seqid_counter *sequence;
	struct list_head list;
	struct rpc_task *task;
};

static inline void nfs_confirm_seqid(struct nfs_seqid_counter *seqid, int status)
{
	if (seqid_mutating_err(-status))
		seqid->flags |= NFS_SEQID_CONFIRMED;
}

/*
 * NFS4 state_owners and lock_owners are simply labels for ordered
 * sequences of RPC calls. Their sole purpose is to provide once-only
 * semantics by allowing the server to identify replayed requests.
 */
struct nfs4_state_owner {
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
	seqcount_t	     so_reclaim_seqcount;
	struct mutex	     so_delegreturn_mutex;
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

struct nfs4_lock_state {
	struct list_head	ls_locks;	/* Other lock stateids */
	struct nfs4_state *	ls_state;	/* Pointer to open state */
#define NFS_LOCK_INITIALIZED 0
#define NFS_LOCK_LOST        1
	unsigned long		ls_flags;
	struct nfs_seqid_counter	ls_seqid;
	nfs4_stateid		ls_stateid;
	refcount_t		ls_count;
	fl_owner_t		ls_owner;
};

/* bits for nfs4_state->flags */
enum {
	LK_STATE_IN_USE,
	NFS_DELEGATED_STATE,		/* Current stateid is delegation */
	NFS_OPEN_STATE,			/* OPEN stateid is set */
	NFS_O_RDONLY_STATE,		/* OPEN stateid has read-only state */
	NFS_O_WRONLY_STATE,		/* OPEN stateid has write-only state */
	NFS_O_RDWR_STATE,		/* OPEN stateid has read/write state */
	NFS_STATE_RECLAIM_REBOOT,	/* OPEN stateid server rebooted */
	NFS_STATE_RECLAIM_NOGRACE,	/* OPEN stateid needs to recover state */
	NFS_STATE_POSIX_LOCKS,		/* Posix locks are supported */
	NFS_STATE_RECOVERY_FAILED,	/* OPEN stateid state recovery failed */
	NFS_STATE_MAY_NOTIFY_LOCK,	/* server may CB_NOTIFY_LOCK */
	NFS_STATE_CHANGE_WAIT,		/* A state changing operation is outstanding */
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

	wait_queue_head_t waitq;
};


struct nfs4_exception {
	struct nfs4_state *state;
	struct inode *inode;
	nfs4_stateid *stateid;
	long timeout;
	unsigned char delay : 1,
		      recovering : 1,
		      retry : 1;
};

struct nfs4_state_recovery_ops {
	int owner_flag_bit;
	int state_flag_bit;
	int (*recover_open)(struct nfs4_state_owner *, struct nfs4_state *);
	int (*recover_lock)(struct nfs4_state *, struct file_lock *);
	int (*establish_clid)(struct nfs_client *, struct rpc_cred *);
	int (*reclaim_complete)(struct nfs_client *, struct rpc_cred *);
	int (*detect_trunking)(struct nfs_client *, struct nfs_client **,
		struct rpc_cred *);
};

struct nfs4_add_xprt_data {
	struct nfs_client	*clp;
	struct rpc_cred		*cred;
};

struct nfs4_state_maintenance_ops {
	int (*sched_state_renewal)(struct nfs_client *, struct rpc_cred *, unsigned);
	struct rpc_cred * (*get_state_renewal_cred_locked)(struct nfs_client *);
	int (*renew_lease)(struct nfs_client *, struct rpc_cred *);
};

struct nfs4_mig_recovery_ops {
	int (*get_locations)(struct inode *, struct nfs4_fs_locations *,
		struct page *, struct rpc_cred *);
	int (*fsid_present)(struct inode *, struct rpc_cred *);
};

extern const struct dentry_operations nfs4_dentry_operations;

/* dir.c */
int nfs_atomic_open(struct inode *, struct dentry *, struct file *,
		    unsigned, umode_t, int *);

/* super.c */
extern struct file_system_type nfs4_fs_type;

/* nfs4namespace.c */
struct rpc_clnt *nfs4_negotiate_security(struct rpc_clnt *, struct inode *,
					 const struct qstr *);
struct vfsmount *nfs4_submount(struct nfs_server *, struct dentry *,
			       struct nfs_fh *, struct nfs_fattr *);
int nfs4_replace_transport(struct nfs_server *server,
				const struct nfs4_fs_locations *locations);

/* nfs4proc.c */
extern int nfs4_handle_exception(struct nfs_server *, int, struct nfs4_exception *);
extern int nfs4_call_sync(struct rpc_clnt *, struct nfs_server *,
			  struct rpc_message *, struct nfs4_sequence_args *,
			  struct nfs4_sequence_res *, int);
extern void nfs4_init_sequence(struct nfs4_sequence_args *, struct nfs4_sequence_res *, int);
extern int nfs4_proc_setclientid(struct nfs_client *, u32, unsigned short, struct rpc_cred *, struct nfs4_setclientid_res *);
extern int nfs4_proc_setclientid_confirm(struct nfs_client *, struct nfs4_setclientid_res *arg, struct rpc_cred *);
extern int nfs4_proc_get_rootfh(struct nfs_server *, struct nfs_fh *, struct nfs_fsinfo *, bool);
extern int nfs4_proc_bind_conn_to_session(struct nfs_client *, struct rpc_cred *cred);
extern int nfs4_proc_exchange_id(struct nfs_client *clp, struct rpc_cred *cred);
extern int nfs4_destroy_clientid(struct nfs_client *clp);
extern int nfs4_init_clientid(struct nfs_client *, struct rpc_cred *);
extern int nfs41_init_clientid(struct nfs_client *, struct rpc_cred *);
extern int nfs4_do_close(struct nfs4_state *state, gfp_t gfp_mask, int wait);
extern int nfs4_server_capabilities(struct nfs_server *server, struct nfs_fh *fhandle);
extern int nfs4_proc_fs_locations(struct rpc_clnt *, struct inode *, const struct qstr *,
				  struct nfs4_fs_locations *, struct page *);
extern int nfs4_proc_get_locations(struct inode *, struct nfs4_fs_locations *,
		struct page *page, struct rpc_cred *);
extern int nfs4_proc_fsid_present(struct inode *, struct rpc_cred *);
extern struct rpc_clnt *nfs4_proc_lookup_mountpoint(struct inode *, const struct qstr *,
			    struct nfs_fh *, struct nfs_fattr *);
extern int nfs4_proc_secinfo(struct inode *, const struct qstr *, struct nfs4_secinfo_flavors *);
extern const struct xattr_handler *nfs4_xattr_handlers[];
extern int nfs4_set_rw_stateid(nfs4_stateid *stateid,
		const struct nfs_open_context *ctx,
		const struct nfs_lock_context *l_ctx,
		fmode_t fmode);

#if defined(CONFIG_NFS_V4_1)
extern int nfs41_sequence_done(struct rpc_task *, struct nfs4_sequence_res *);
extern int nfs4_proc_create_session(struct nfs_client *, struct rpc_cred *);
extern int nfs4_proc_destroy_session(struct nfs4_session *, struct rpc_cred *);
extern int nfs4_proc_get_lease_time(struct nfs_client *clp,
		struct nfs_fsinfo *fsinfo);
extern int nfs4_proc_layoutcommit(struct nfs4_layoutcommit_data *data,
				  bool sync);
extern int nfs4_detect_session_trunking(struct nfs_client *clp,
		struct nfs41_exchange_id_res *res, struct rpc_xprt *xprt);

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

static inline bool
_nfs4_state_protect(struct nfs_client *clp, unsigned long sp4_mode,
		    struct rpc_clnt **clntp, struct rpc_message *msg)
{
	struct rpc_cred *newcred = NULL;
	rpc_authflavor_t flavor;

	if (sp4_mode == NFS_SP4_MACH_CRED_CLEANUP ||
	    sp4_mode == NFS_SP4_MACH_CRED_PNFS_CLEANUP) {
		/* Using machine creds for cleanup operations
		 * is only relevent if the client credentials
		 * might expire. So don't bother for
		 * RPC_AUTH_UNIX.  If file was only exported to
		 * sec=sys, the PUTFH would fail anyway.
		 */
		if ((*clntp)->cl_auth->au_flavor == RPC_AUTH_UNIX)
			return false;
	}
	if (test_bit(sp4_mode, &clp->cl_sp4_flags)) {
		spin_lock(&clp->cl_lock);
		if (clp->cl_machine_cred != NULL)
			/* don't call get_rpccred on the machine cred -
			 * a reference will be held for life of clp */
			newcred = clp->cl_machine_cred;
		spin_unlock(&clp->cl_lock);
		msg->rpc_cred = newcred;

		flavor = clp->cl_rpcclient->cl_auth->au_flavor;
		WARN_ON_ONCE(flavor != RPC_AUTH_GSS_KRB5I &&
			     flavor != RPC_AUTH_GSS_KRB5P);
		*clntp = clp->cl_rpcclient;

		return true;
	}
	return false;
}

/*
 * Function responsible for determining if an rpc_message should use the
 * machine cred under SP4_MACH_CRED and if so switching the credential and
 * authflavor (using the nfs_client's rpc_clnt which will be krb5i/p).
 * Should be called before rpc_call_sync/rpc_call_async.
 */
static inline void
nfs4_state_protect(struct nfs_client *clp, unsigned long sp4_mode,
		   struct rpc_clnt **clntp, struct rpc_message *msg)
{
	_nfs4_state_protect(clp, sp4_mode, clntp, msg);
}

/*
 * Special wrapper to nfs4_state_protect for write.
 * If WRITE can use machine cred but COMMIT cannot, make sure all writes
 * that use machine cred use NFS_FILE_SYNC.
 */
static inline void
nfs4_state_protect_write(struct nfs_client *clp, struct rpc_clnt **clntp,
			 struct rpc_message *msg, struct nfs_pgio_header *hdr)
{
	if (_nfs4_state_protect(clp, NFS_SP4_MACH_CRED_WRITE, clntp, msg) &&
	    !test_bit(NFS_SP4_MACH_CRED_COMMIT, &clp->cl_sp4_flags))
		hdr->args.stable = NFS_FILE_SYNC;
}
#else /* CONFIG_NFS_v4_1 */
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

static inline void
nfs4_state_protect(struct nfs_client *clp, unsigned long sp4_flags,
		   struct rpc_clnt **clntp, struct rpc_message *msg)
{
}

static inline void
nfs4_state_protect_write(struct nfs_client *clp, struct rpc_clnt **clntp,
			 struct rpc_message *msg, struct nfs_pgio_header *hdr)
{
}
#endif /* CONFIG_NFS_V4_1 */

extern const struct nfs4_minor_version_ops *nfs_v4_minor_ops[];

extern const u32 nfs4_fattr_bitmap[3];
extern const u32 nfs4_statfs_bitmap[3];
extern const u32 nfs4_pathconf_bitmap[3];
extern const u32 nfs4_fsinfo_bitmap[3];
extern const u32 nfs4_fs_locations_bitmap[3];

void nfs40_shutdown_client(struct nfs_client *);
void nfs41_shutdown_client(struct nfs_client *);
int nfs40_init_client(struct nfs_client *);
int nfs41_init_client(struct nfs_client *);
void nfs4_free_client(struct nfs_client *);

struct nfs_client *nfs4_alloc_client(const struct nfs_client_initdata *);

/* nfs4renewd.c */
extern void nfs4_schedule_state_renewal(struct nfs_client *);
extern void nfs4_renewd_prepare_shutdown(struct nfs_server *);
extern void nfs4_kill_renewd(struct nfs_client *);
extern void nfs4_renew_state(struct work_struct *);
extern void nfs4_set_lease_period(struct nfs_client *clp,
		unsigned long lease,
		unsigned long lastrenewed);


/* nfs4state.c */
struct rpc_cred *nfs4_get_clid_cred(struct nfs_client *clp);
struct rpc_cred *nfs4_get_machine_cred_locked(struct nfs_client *clp);
struct rpc_cred *nfs4_get_renew_cred_locked(struct nfs_client *clp);
int nfs4_discover_server_trunking(struct nfs_client *clp,
			struct nfs_client **);
int nfs40_discover_server_trunking(struct nfs_client *clp,
			struct nfs_client **, struct rpc_cred *);
#if defined(CONFIG_NFS_V4_1)
int nfs41_discover_server_trunking(struct nfs_client *clp,
			struct nfs_client **, struct rpc_cred *);
extern void nfs4_schedule_session_recovery(struct nfs4_session *, int);
extern void nfs41_notify_server(struct nfs_client *);
#else
static inline void nfs4_schedule_session_recovery(struct nfs4_session *session, int err)
{
}
#endif /* CONFIG_NFS_V4_1 */

extern struct nfs4_state_owner *nfs4_get_state_owner(struct nfs_server *, struct rpc_cred *, gfp_t);
extern void nfs4_put_state_owner(struct nfs4_state_owner *);
extern void nfs4_purge_state_owners(struct nfs_server *);
extern struct nfs4_state * nfs4_get_open_state(struct inode *, struct nfs4_state_owner *);
extern void nfs4_put_open_state(struct nfs4_state *);
extern void nfs4_close_state(struct nfs4_state *, fmode_t);
extern void nfs4_close_sync(struct nfs4_state *, fmode_t);
extern void nfs4_state_set_mode_locked(struct nfs4_state *, fmode_t);
extern void nfs_inode_find_state_and_recover(struct inode *inode,
		const nfs4_stateid *stateid);
extern int nfs4_state_mark_reclaim_nograce(struct nfs_client *, struct nfs4_state *);
extern void nfs4_schedule_lease_recovery(struct nfs_client *);
extern int nfs4_wait_clnt_recover(struct nfs_client *clp);
extern int nfs4_client_recover_expired_lease(struct nfs_client *clp);
extern void nfs4_schedule_state_manager(struct nfs_client *);
extern void nfs4_schedule_path_down_recovery(struct nfs_client *clp);
extern int nfs4_schedule_stateid_recovery(const struct nfs_server *, struct nfs4_state *);
extern int nfs4_schedule_migration_recovery(const struct nfs_server *);
extern void nfs4_schedule_lease_moved_recovery(struct nfs_client *);
extern void nfs41_handle_sequence_flag_errors(struct nfs_client *clp, u32 flags, bool);
extern void nfs41_handle_server_scope(struct nfs_client *,
				      struct nfs41_server_scope **);
extern void nfs4_put_lock_state(struct nfs4_lock_state *lsp);
extern int nfs4_set_lock_state(struct nfs4_state *state, struct file_lock *fl);
extern int nfs4_select_rw_stateid(struct nfs4_state *, fmode_t,
		const struct nfs_lock_context *, nfs4_stateid *,
		struct rpc_cred **);
extern bool nfs4_refresh_open_stateid(nfs4_stateid *dst,
		struct nfs4_state *state);
extern bool nfs4_copy_open_stateid(nfs4_stateid *dst,
		struct nfs4_state *state);

extern struct nfs_seqid *nfs_alloc_seqid(struct nfs_seqid_counter *counter, gfp_t gfp_mask);
extern int nfs_wait_on_sequence(struct nfs_seqid *seqid, struct rpc_task *task);
extern void nfs_increment_open_seqid(int status, struct nfs_seqid *seqid);
extern void nfs_increment_lock_seqid(int status, struct nfs_seqid *seqid);
extern void nfs_release_seqid(struct nfs_seqid *seqid);
extern void nfs_free_seqid(struct nfs_seqid *seqid);
extern int nfs4_setup_sequence(struct nfs_client *client,
				struct nfs4_sequence_args *args,
				struct nfs4_sequence_res *res,
				struct rpc_task *task);
extern int nfs4_sequence_done(struct rpc_task *task,
			      struct nfs4_sequence_res *res);

extern void nfs4_free_lock_state(struct nfs_server *server, struct nfs4_lock_state *lsp);

extern const nfs4_stateid zero_stateid;
extern const nfs4_stateid invalid_stateid;

/* nfs4super.c */
struct nfs_mount_info;
extern struct nfs_subversion nfs_v4;
struct dentry *nfs4_try_mount(int, const char *, struct nfs_mount_info *, struct nfs_subversion *);
extern bool nfs4_disable_idmapping;
extern unsigned short max_session_slots;
extern unsigned short max_session_cb_slots;
extern unsigned short send_implementation_id;
extern bool recover_lost_locks;

#define NFS4_CLIENT_ID_UNIQ_LEN		(64)
extern char nfs4_client_id_uniquifier[NFS4_CLIENT_ID_UNIQ_LEN];

/* nfs4sysctl.c */
#ifdef CONFIG_SYSCTL
int nfs4_register_sysctl(void);
void nfs4_unregister_sysctl(void);
#else
static inline int nfs4_register_sysctl(void)
{
	return 0;
}

static inline void nfs4_unregister_sysctl(void)
{
}
#endif

/* nfs4xdr.c */
extern const struct rpc_procinfo nfs4_procedures[];

struct nfs4_mount_data;

/* callback_xdr.c */
extern const struct svc_version nfs4_callback_version1;
extern const struct svc_version nfs4_callback_version4;

static inline void nfs4_stateid_copy(nfs4_stateid *dst, const nfs4_stateid *src)
{
	memcpy(dst->data, src->data, sizeof(dst->data));
	dst->type = src->type;
}

static inline bool nfs4_stateid_match(const nfs4_stateid *dst, const nfs4_stateid *src)
{
	if (dst->type != src->type)
		return false;
	return memcmp(dst->data, src->data, sizeof(dst->data)) == 0;
}

static inline bool nfs4_stateid_match_other(const nfs4_stateid *dst, const nfs4_stateid *src)
{
	return memcmp(dst->other, src->other, NFS4_STATEID_OTHER_SIZE) == 0;
}

static inline bool nfs4_stateid_is_newer(const nfs4_stateid *s1, const nfs4_stateid *s2)
{
	return (s32)(be32_to_cpu(s1->seqid) - be32_to_cpu(s2->seqid)) > 0;
}

static inline bool nfs4_valid_open_stateid(const struct nfs4_state *state)
{
	return test_bit(NFS_STATE_RECOVERY_FAILED, &state->flags) == 0;
}

static inline bool nfs4_state_match_open_stateid_other(const struct nfs4_state *state,
		const nfs4_stateid *stateid)
{
	return test_bit(NFS_OPEN_STATE, &state->flags) &&
		nfs4_stateid_match_other(&state->open_stateid, stateid);
}

#else

#define nfs4_close_state(a, b) do { } while (0)
#define nfs4_close_sync(a, b) do { } while (0)
#define nfs4_state_protect(a, b, c, d) do { } while (0)
#define nfs4_state_protect_write(a, b, c, d) do { } while (0)

#endif /* CONFIG_NFS_V4 */
#endif /* __LINUX_FS_NFS_NFS4_FS.H */
