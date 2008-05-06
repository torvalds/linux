#ifndef _NFS_FS_SB
#define _NFS_FS_SB

#include <linux/list.h>
#include <linux/backing-dev.h>
#include <linux/wait.h>

#include <asm/atomic.h>

struct nfs_iostats;
struct nlm_host;

/*
 * The nfs_client identifies our client state to the server.
 */
struct nfs_client {
	atomic_t		cl_count;
	int			cl_cons_state;	/* current construction state (-ve: init error) */
#define NFS_CS_READY		0		/* ready to be used */
#define NFS_CS_INITING		1		/* busy initialising */
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

	struct rpc_cred		*cl_machine_cred;

#ifdef CONFIG_NFS_V4
	u64			cl_clientid;	/* constant */
	nfs4_verifier		cl_confirm;
	unsigned long		cl_state;

	struct rb_root		cl_openowner_id;
	struct rb_root		cl_lockowner_id;

	/*
	 * The following rwsem ensures exclusive access to the server
	 * while we recover the state following a lease expiration.
	 */
	struct rw_semaphore	cl_sem;

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

	struct nfs_fsid		fsid;
	__u64			maxfilesize;	/* maximum file size */
	unsigned long		mount_time;	/* when this fs was mounted */
	dev_t			s_dev;		/* superblock dev numbers */

#ifdef CONFIG_NFS_V4
	u32			attr_bitmask[2];/* V4 bitmask representing the set
						   of attributes supported on this
						   filesystem */
	u32			acl_bitmask;	/* V4 bitmask representing the ACEs
						   that are supported on this
						   filesystem */
#endif
	void (*destroy)(struct nfs_server *);

	atomic_t active; /* Keep trace of any activity to this server */
	wait_queue_head_t active_wq;  /* Wait for any activity to stop  */

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

#endif
