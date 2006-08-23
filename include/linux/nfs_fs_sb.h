#ifndef _NFS_FS_SB
#define _NFS_FS_SB

#include <linux/list.h>
#include <linux/backing-dev.h>

struct nfs_iostats;

/*
 * The nfs_client identifies our client state to the server.
 */
struct nfs_client {
	atomic_t		cl_count;
	int			cl_cons_state;	/* current construction state (-ve: init error) */
#define NFS_CS_READY		0		/* ready to be used */
#define NFS_CS_INITING		1		/* busy initialising */
	int			cl_nfsversion;	/* NFS protocol version */
	unsigned long		cl_res_state;	/* NFS resources state */
#define NFS_CS_RPCIOD		0		/* - rpciod started */
#define NFS_CS_CALLBACK		1		/* - callback started */
#define NFS_CS_IDMAP		2		/* - idmap started */
	struct sockaddr_in	cl_addr;	/* server identifier */
	char *			cl_hostname;	/* hostname of server */
	struct list_head	cl_share_link;	/* link in global client list */
	struct list_head	cl_superblocks;	/* List of nfs_server structs */

	struct rpc_clnt *	cl_rpcclient;
	const struct nfs_rpc_ops *rpc_ops;	/* NFS protocol vector */

#ifdef CONFIG_NFS_V4
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

	unsigned long		cl_lease_time;
	unsigned long		cl_last_renewal;
	struct work_struct	cl_renewd;
	struct work_struct	cl_recoverd;

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
#endif
};

/*
 * NFS client parameters stored in the superblock.
 */
struct nfs_server {
	struct nfs_client *	nfs_client;	/* shared client and NFS4 state */
	struct rpc_clnt *	client;		/* RPC client handle */
	struct rpc_clnt *	client_sys;	/* 2nd handle for FSINFO */
	struct rpc_clnt *	client_acl;	/* ACL RPC client handle */
	struct nfs_iostats *	io_stats;	/* I/O statistics */
	struct backing_dev_info	backing_dev_info;
	int			flags;		/* various flags */
	unsigned int		caps;		/* server capabilities */
	unsigned int		rsize;		/* read size */
	unsigned int		rpages;		/* read size (in pages) */
	unsigned int		wsize;		/* write size */
	unsigned int		wpages;		/* write size (in pages) */
	unsigned int		wtmult;		/* server disk block size */
	unsigned int		dtsize;		/* readdir size */
	unsigned int		bsize;		/* server block size */
	unsigned int		acregmin;	/* attr cache timeouts */
	unsigned int		acregmax;
	unsigned int		acdirmin;
	unsigned int		acdirmax;
	unsigned long		retrans_timeo;	/* retransmit timeout */
	unsigned int		retrans_count;	/* number of retransmit tries */
	unsigned int		namelen;
	char *			hostname;	/* remote hostname */
	struct nfs_fh		fh;
	struct sockaddr_in	addr;
	struct nfs_fsid		fsid;
	unsigned long		mount_time;	/* when this fs was mounted */
#ifdef CONFIG_NFS_V4
	/* Our own IP address, as a null-terminated string.
	 * This is used to generate the clientid, and the callback address.
	 */
	char			ip_addr[16];
	char *			mnt_path;
	struct list_head	nfs4_siblings;	/* List of other nfs_server structs
						 * that share the same clientid
						 */
	u32			attr_bitmask[2];/* V4 bitmask representing the set
						   of attributes supported on this
						   filesystem */
	u32			acl_bitmask;	/* V4 bitmask representing the ACEs
						   that are supported on this
						   filesystem */
#endif
};

/* Server capabilities */
#define NFS_CAP_READDIRPLUS	(1U << 0)
#define NFS_CAP_HARDLINKS	(1U << 1)
#define NFS_CAP_SYMLINKS	(1U << 2)
#define NFS_CAP_ACLS		(1U << 3)
#define NFS_CAP_ATOMIC_OPEN	(1U << 4)

#endif
