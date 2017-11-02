/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NFS_FS_SB
#define _NFS_FS_SB

#include <linux/list.h>
#include <linux/backing-dev.h>
#include <linux/idr.h>
#include <linux/wait.h>
#include <linux/nfs_xdr.h>
#include <linux/sunrpc/xprt.h>

#include <linux/atomic.h>

struct nfs4_session;
struct nfs_iostats;
struct nlm_host;
struct nfs4_sequence_args;
struct nfs4_sequence_res;
struct nfs_server;
struct nfs4_minor_version_ops;
struct nfs41_server_scope;
struct nfs41_impl_id;

/*
 * The nfs_client identifies our client state to the server.
 */
struct nfs_client {
	atomic_t		cl_count;
	atomic_t		cl_mds_count;
	int			cl_cons_state;	/* current construction state (-ve: init error) */
#define NFS_CS_READY		0		/* ready to be used */
#define NFS_CS_INITING		1		/* busy initialising */
#define NFS_CS_SESSION_INITING	2		/* busy initialising  session */
	unsigned long		cl_res_state;	/* NFS resources state */
#define NFS_CS_CALLBACK		1		/* - callback started */
#define NFS_CS_IDMAP		2		/* - idmap started */
#define NFS_CS_RENEWD		3		/* - renewd started */
#define NFS_CS_STOP_RENEW	4		/* no more state to renew */
#define NFS_CS_CHECK_LEASE_TIME	5		/* need to check lease time */
	unsigned long		cl_flags;	/* behavior switches */
#define NFS_CS_NORESVPORT	0		/* - use ephemeral src port */
#define NFS_CS_DISCRTRY		1		/* - disconnect on RPC retry */
#define NFS_CS_MIGRATION	2		/* - transparent state migr */
#define NFS_CS_INFINITE_SLOTS	3		/* - don't limit TCP slots */
#define NFS_CS_NO_RETRANS_TIMEOUT	4	/* - Disable retransmit timeouts */
#define NFS_CS_TSM_POSSIBLE	5		/* - Maybe state migration */
	struct sockaddr_storage	cl_addr;	/* server identifier */
	size_t			cl_addrlen;
	char *			cl_hostname;	/* hostname of server */
	char *			cl_acceptor;	/* GSSAPI acceptor name */
	struct list_head	cl_share_link;	/* link in global client list */
	struct list_head	cl_superblocks;	/* List of nfs_server structs */

	struct rpc_clnt *	cl_rpcclient;
	const struct nfs_rpc_ops *rpc_ops;	/* NFS protocol vector */
	int			cl_proto;	/* Network transport protocol */
	struct nfs_subversion *	cl_nfs_mod;	/* pointer to nfs version module */

	u32			cl_minorversion;/* NFSv4 minorversion */
	struct rpc_cred		*cl_machine_cred;

#if IS_ENABLED(CONFIG_NFS_V4)
	struct list_head	cl_ds_clients; /* auth flavor data servers */
	u64			cl_clientid;	/* constant */
	nfs4_verifier		cl_confirm;	/* Clientid verifier */
	unsigned long		cl_state;

	spinlock_t		cl_lock;

	unsigned long		cl_lease_time;
	unsigned long		cl_last_renewal;
	struct delayed_work	cl_renewd;

	struct rpc_wait_queue	cl_rpcwaitq;

	/* idmapper */
	struct idmap *		cl_idmap;

	/* Client owner identifier */
	const char *		cl_owner_id;

	u32			cl_cb_ident;	/* v4.0 callback identifier */
	const struct nfs4_minor_version_ops *cl_mvops;
	unsigned long		cl_mig_gen;

	/* NFSv4.0 transport blocking */
	struct nfs4_slot_table	*cl_slot_tbl;

	/* The sequence id to use for the next CREATE_SESSION */
	u32			cl_seqid;
	/* The flags used for obtaining the clientid during EXCHANGE_ID */
	u32			cl_exchange_flags;
	struct nfs4_session	*cl_session;	/* shared session */
	bool			cl_preserve_clid;
	struct nfs41_server_owner *cl_serverowner;
	struct nfs41_server_scope *cl_serverscope;
	struct nfs41_impl_id	*cl_implid;
	/* nfs 4.1+ state protection modes: */
	unsigned long		cl_sp4_flags;
#define NFS_SP4_MACH_CRED_MINIMAL  1	/* Minimal sp4_mach_cred - state ops
					 * must use machine cred */
#define NFS_SP4_MACH_CRED_CLEANUP  2	/* CLOSE and LOCKU */
#define NFS_SP4_MACH_CRED_SECINFO  3	/* SECINFO and SECINFO_NO_NAME */
#define NFS_SP4_MACH_CRED_STATEID  4	/* TEST_STATEID and FREE_STATEID */
#define NFS_SP4_MACH_CRED_WRITE    5	/* WRITE */
#define NFS_SP4_MACH_CRED_COMMIT   6	/* COMMIT */
#define NFS_SP4_MACH_CRED_PNFS_CLEANUP  7 /* LAYOUTRETURN */
#if IS_ENABLED(CONFIG_NFS_V4_1)
	wait_queue_head_t	cl_lock_waitq;
#endif /* CONFIG_NFS_V4_1 */
#endif /* CONFIG_NFS_V4 */

	/* Our own IP address, as a null-terminated string.
	 * This is used to generate the mv0 callback address.
	 */
	char			cl_ipaddr[48];

#ifdef CONFIG_NFS_FSCACHE
	struct fscache_cookie	*fscache;	/* client index cache cookie */
#endif

	struct net		*cl_net;
};

/*
 * NFS client parameters stored in the superblock.
 */
struct nfs_server {
	struct nfs_client *	nfs_client;	/* shared client and NFS4 state */
	struct list_head	client_link;	/* List of other nfs_server structs
						 * that share the same client
						 */
	struct list_head	master_link;	/* link in master servers list */
	struct rpc_clnt *	client;		/* RPC client handle */
	struct rpc_clnt *	client_acl;	/* ACL RPC client handle */
	struct nlm_host		*nlm_host;	/* NLM client handle */
	struct nfs_iostats __percpu *io_stats;	/* I/O statistics */
	atomic_long_t		writeback;	/* number of writeback pages */
	int			flags;		/* various flags */
	unsigned int		caps;		/* server capabilities */
	unsigned int		rsize;		/* read size */
	unsigned int		rpages;		/* read size (in pages) */
	unsigned int		wsize;		/* write size */
	unsigned int		wpages;		/* write size (in pages) */
	unsigned int		wtmult;		/* server disk block size */
	unsigned int		dtsize;		/* readdir size */
	unsigned short		port;		/* "port=" setting */
	unsigned int		bsize;		/* server block size */
	unsigned int		acregmin;	/* attr cache timeouts */
	unsigned int		acregmax;
	unsigned int		acdirmin;
	unsigned int		acdirmax;
	unsigned int		namelen;
	unsigned int		options;	/* extra options enabled by mount */
	unsigned int		clone_blksize;	/* granularity of a CLONE operation */
#define NFS_OPTION_FSCACHE	0x00000001	/* - local caching enabled */
#define NFS_OPTION_MIGRATION	0x00000002	/* - NFSv4 migration enabled */

	struct nfs_fsid		fsid;
	__u64			maxfilesize;	/* maximum file size */
	struct timespec		time_delta;	/* smallest time granularity */
	unsigned long		mount_time;	/* when this fs was mounted */
	struct super_block	*super;		/* VFS super block */
	dev_t			s_dev;		/* superblock dev numbers */
	struct nfs_auth_info	auth_info;	/* parsed auth flavors */

#ifdef CONFIG_NFS_FSCACHE
	struct nfs_fscache_key	*fscache_key;	/* unique key for superblock */
	struct fscache_cookie	*fscache;	/* superblock cookie */
#endif

	u32			pnfs_blksize;	/* layout_blksize attr */
#if IS_ENABLED(CONFIG_NFS_V4)
	u32			attr_bitmask[3];/* V4 bitmask representing the set
						   of attributes supported on this
						   filesystem */
	u32			attr_bitmask_nl[3];
						/* V4 bitmask representing the
						   set of attributes supported
						   on this filesystem excluding
						   the label support bit. */
	u32			exclcreat_bitmask[3];
						/* V4 bitmask representing the
						   set of attributes supported
						   on this filesystem for the
						   exclusive create. */
	u32			cache_consistency_bitmask[3];
						/* V4 bitmask representing the subset
						   of change attribute, size, ctime
						   and mtime attributes supported by
						   the server */
	u32			acl_bitmask;	/* V4 bitmask representing the ACEs
						   that are supported on this
						   filesystem */
	u32			fh_expire_type;	/* V4 bitmask representing file
						   handle volatility type for
						   this filesystem */
	struct pnfs_layoutdriver_type  *pnfs_curr_ld; /* Active layout driver */
	struct rpc_wait_queue	roc_rpcwaitq;
	void			*pnfs_ld_data;	/* per mount point data */

	/* the following fields are protected by nfs_client->cl_lock */
	struct rb_root		state_owners;
#endif
	struct ida		openowner_id;
	struct ida		lockowner_id;
	struct list_head	state_owners_lru;
	struct list_head	layouts;
	struct list_head	delegations;

	unsigned long		mig_gen;
	unsigned long		mig_status;
#define NFS_MIG_IN_TRANSITION		(1)
#define NFS_MIG_FAILED			(2)
#define NFS_MIG_TSM_POSSIBLE		(3)

	void (*destroy)(struct nfs_server *);

	atomic_t active; /* Keep trace of any activity to this server */

	/* mountd-related mount options */
	struct sockaddr_storage	mountd_address;
	size_t			mountd_addrlen;
	u32			mountd_version;
	unsigned short		mountd_port;
	unsigned short		mountd_protocol;
	struct rpc_wait_queue	uoc_rpcwaitq;
};

/* Server capabilities */
#define NFS_CAP_READDIRPLUS	(1U << 0)
#define NFS_CAP_HARDLINKS	(1U << 1)
#define NFS_CAP_SYMLINKS	(1U << 2)
#define NFS_CAP_ACLS		(1U << 3)
#define NFS_CAP_ATOMIC_OPEN	(1U << 4)
/* #define NFS_CAP_CHANGE_ATTR	(1U << 5) */
#define NFS_CAP_FILEID		(1U << 6)
#define NFS_CAP_MODE		(1U << 7)
#define NFS_CAP_NLINK		(1U << 8)
#define NFS_CAP_OWNER		(1U << 9)
#define NFS_CAP_OWNER_GROUP	(1U << 10)
#define NFS_CAP_ATIME		(1U << 11)
#define NFS_CAP_CTIME		(1U << 12)
#define NFS_CAP_MTIME		(1U << 13)
#define NFS_CAP_POSIX_LOCK	(1U << 14)
#define NFS_CAP_UIDGID_NOMAP	(1U << 15)
#define NFS_CAP_STATEID_NFSV41	(1U << 16)
#define NFS_CAP_ATOMIC_OPEN_V1	(1U << 17)
#define NFS_CAP_SECURITY_LABEL	(1U << 18)
#define NFS_CAP_SEEK		(1U << 19)
#define NFS_CAP_ALLOCATE	(1U << 20)
#define NFS_CAP_DEALLOCATE	(1U << 21)
#define NFS_CAP_LAYOUTSTATS	(1U << 22)
#define NFS_CAP_CLONE		(1U << 23)
#define NFS_CAP_COPY		(1U << 24)

#endif
