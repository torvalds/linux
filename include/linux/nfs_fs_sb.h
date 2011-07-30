#ifndef _NFS_FS_SB
#define _NFS_FS_SB

#include <linux/list.h>
#include <linux/backing-dev.h>
#include <linux/wait.h>
#include <linux/nfs_xdr.h>
#include <linux/sunrpc/xprt.h>

#include <asm/atomic.h>

struct nfs4_session;
struct nfs_iostats;
struct nlm_host;
struct nfs4_sequence_args;
struct nfs4_sequence_res;
struct nfs_server;

/*
 * The nfs_client identifies our client state to the server.
 */
struct nfs_client {
	atomic_t		cl_count;
	int			cl_cons_state;	/* current construction state (-ve: init error) */
#define NFS_CS_READY		0		/* ready to be used */
#define NFS_CS_INITING		1		/* busy initialising */
#define NFS_CS_SESSION_INITING	2		/* busy initialising  session */
	unsigned long		cl_res_state;	/* NFS resources state */
#define NFS_CS_CALLBACK		1		/* - callback started */
#define NFS_CS_IDMAP		2		/* - idmap started */
#define NFS_CS_RENEWD		3		/* - renewd started */
	struct sockaddr_storage	cl_addr;	/* server identifier */
	size_t			cl_addrlen;
	char *			cl_hostname;	/* hostname of server */
	struct list_head	cl_share_link;	/* link in global client list */
	struct list_head	cl_superblocks;	/* List of nfs_server structs */

	struct rpc_clnt *	cl_rpcclient;
	const struct nfs_rpc_ops *rpc_ops;	/* NFS protocol vector */
	int			cl_proto;	/* Network transport protocol */

	u32			cl_minorversion;/* NFSv4 minorversion */
	struct rpc_cred		*cl_machine_cred;

#ifdef CONFIG_NFS_V4
	u64			cl_clientid;	/* constant */
	nfs4_verifier		cl_confirm;
	unsigned long		cl_state;

	struct rb_root		cl_openowner_id;
	struct rb_root		cl_lockowner_id;

	struct list_head	cl_delegations;
	struct rb_root		cl_state_owners;
	spinlock_t		cl_lock;

	unsigned long		cl_lease_time;
	unsigned long		cl_last_renewal;
	struct delayed_work	cl_renewd;

	struct rpc_wait_queue	cl_rpcwaitq;

	/* used for the setclientid verifier */
	struct timespec		cl_boot_time;

	/* idmapper */
	struct idmap *		cl_idmap;

	/* Our own IP address, as a null-terminated string.
	 * This is used to generate the clientid, and the callback address.
	 */
	char			cl_ipaddr[48];
	unsigned char		cl_id_uniquifier;
	int		     (* cl_call_sync)(struct nfs_server *server,
					      struct rpc_message *msg,
					      struct nfs4_sequence_args *args,
					      struct nfs4_sequence_res *res,
					      int cache_reply);
#endif /* CONFIG_NFS_V4 */

#ifdef CONFIG_NFS_V4_1
	/* clientid returned from EXCHANGE_ID, used by session operations */
	u64			cl_ex_clid;
	/* The sequence id to use for the next CREATE_SESSION */
	u32			cl_seqid;
	/* The flags used for obtaining the clientid during EXCHANGE_ID */
	u32			cl_exchange_flags;
	struct nfs4_session	*cl_session; 	/* sharred session */
#endif /* CONFIG_NFS_V4_1 */

#ifdef CONFIG_NFS_FSCACHE
	struct fscache_cookie	*fscache;	/* client index cache cookie */
#endif
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
	struct nfs_iostats *	io_stats;	/* I/O statistics */
	struct backing_dev_info	backing_dev_info;
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
#define NFS_OPTION_FSCACHE	0x00000001	/* - local caching enabled */

	struct nfs_fsid		fsid;
	__u64			maxfilesize;	/* maximum file size */
	unsigned long		mount_time;	/* when this fs was mounted */
	dev_t			s_dev;		/* superblock dev numbers */

#ifdef CONFIG_NFS_FSCACHE
	struct nfs_fscache_key	*fscache_key;	/* unique key for superblock */
	struct fscache_cookie	*fscache;	/* superblock cookie */
#endif

#ifdef CONFIG_NFS_V4
	u32			attr_bitmask[2];/* V4 bitmask representing the set
						   of attributes supported on this
						   filesystem */
	u32			cache_consistency_bitmask[2];
						/* V4 bitmask representing the subset
						   of change attribute, size, ctime
						   and mtime attributes supported by
						   the server */
	u32			acl_bitmask;	/* V4 bitmask representing the ACEs
						   that are supported on this
						   filesystem */
#endif
	void (*destroy)(struct nfs_server *);

	atomic_t active; /* Keep trace of any activity to this server */

	/* mountd-related mount options */
	struct sockaddr_storage	mountd_address;
	size_t			mountd_addrlen;
	u32			mountd_version;
	unsigned short		mountd_port;
	unsigned short		mountd_protocol;
};

/* Server capabilities */
#define NFS_CAP_READDIRPLUS	(1U << 0)
#define NFS_CAP_HARDLINKS	(1U << 1)
#define NFS_CAP_SYMLINKS	(1U << 2)
#define NFS_CAP_ACLS		(1U << 3)
#define NFS_CAP_ATOMIC_OPEN	(1U << 4)
#define NFS_CAP_CHANGE_ATTR	(1U << 5)
#define NFS_CAP_FILEID		(1U << 6)
#define NFS_CAP_MODE		(1U << 7)
#define NFS_CAP_NLINK		(1U << 8)
#define NFS_CAP_OWNER		(1U << 9)
#define NFS_CAP_OWNER_GROUP	(1U << 10)
#define NFS_CAP_ATIME		(1U << 11)
#define NFS_CAP_CTIME		(1U << 12)
#define NFS_CAP_MTIME		(1U << 13)


/* maximum number of slots to use */
#define NFS4_MAX_SLOT_TABLE RPC_MAX_SLOT_TABLE

#if defined(CONFIG_NFS_V4_1)

/* Sessions */
#define SLOT_TABLE_SZ (NFS4_MAX_SLOT_TABLE/(8*sizeof(long)))
struct nfs4_slot_table {
	struct nfs4_slot *slots;		/* seqid per slot */
	unsigned long   used_slots[SLOT_TABLE_SZ]; /* used/unused bitmap */
	spinlock_t	slot_tbl_lock;
	struct rpc_wait_queue	slot_tbl_waitq;	/* allocators may wait here */
	int		max_slots;		/* # slots in table */
	int		highest_used_slotid;	/* sent to server on each SEQ.
						 * op for dynamic resizing */
};

static inline int slot_idx(struct nfs4_slot_table *tbl, struct nfs4_slot *sp)
{
	return sp - tbl->slots;
}

/*
 * Session related parameters
 */
struct nfs4_session {
	struct nfs4_sessionid		sess_id;
	u32				flags;
	unsigned long			session_state;
	u32				hash_alg;
	u32				ssv_len;

	/* The fore and back channel */
	struct nfs4_channel_attrs	fc_attrs;
	struct nfs4_slot_table		fc_slot_table;
	struct nfs4_channel_attrs	bc_attrs;
	struct nfs4_slot_table		bc_slot_table;
	struct nfs_client		*clp;
};

#endif /* CONFIG_NFS_V4_1 */
#endif
