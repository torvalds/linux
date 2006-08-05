/*
 * linux/include/linux/lockd/lockd.h
 *
 * General-purpose lockd include file.
 *
 * Copyright (C) 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_LOCKD_LOCKD_H
#define LINUX_LOCKD_LOCKD_H

#ifdef __KERNEL__

#include <linux/in.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/utsname.h>
#include <linux/nfsd/nfsfh.h>
#include <linux/lockd/bind.h>
#include <linux/lockd/xdr.h>
#ifdef CONFIG_LOCKD_V4
#include <linux/lockd/xdr4.h>
#endif
#include <linux/lockd/debug.h>

/*
 * Version string
 */
#define LOCKD_VERSION		"0.5"

/*
 * Default timeout for RPC calls (seconds)
 */
#define LOCKD_DFLT_TIMEO	10

/*
 * Lockd host handle (used both by the client and server personality).
 */
struct nlm_host {
	struct nlm_host *	h_next;		/* linked list (hash table) */
	struct sockaddr_in	h_addr;		/* peer address */
	struct rpc_clnt	*	h_rpcclnt;	/* RPC client to talk to peer */
	char			h_name[20];	/* remote hostname */
	u32			h_version;	/* interface version */
	unsigned short		h_proto;	/* transport proto */
	unsigned short		h_reclaiming : 1,
				h_server     : 1, /* server side, not client side */
				h_inuse      : 1,
				h_killed     : 1,
				h_monitored  : 1;
	wait_queue_head_t	h_gracewait;	/* wait while reclaiming */
	struct rw_semaphore	h_rwsem;	/* Reboot recovery lock */
	u32			h_state;	/* pseudo-state counter */
	u32			h_nsmstate;	/* true remote NSM state */
	u32			h_pidcount;	/* Pseudopids */
	atomic_t		h_count;	/* reference count */
	struct mutex		h_mutex;	/* mutex for pmap binding */
	unsigned long		h_nextrebind;	/* next portmap call */
	unsigned long		h_expires;	/* eligible for GC */
	struct list_head	h_lockowners;	/* Lockowners for the client */
	spinlock_t		h_lock;
	struct list_head	h_granted;	/* Locks in GRANTED state */
	struct list_head	h_reclaim;	/* Locks in RECLAIM state */
};

/*
 * Map an fl_owner_t into a unique 32-bit "pid"
 */
struct nlm_lockowner {
	struct list_head list;
	atomic_t count;

	struct nlm_host *host;
	fl_owner_t owner;
	uint32_t pid;
};

struct nlm_wait;

/*
 * Memory chunk for NLM client RPC request.
 */
#define NLMCLNT_OHSIZE		(sizeof(system_utsname.nodename)+10)
struct nlm_rqst {
	unsigned int		a_flags;	/* initial RPC task flags */
	struct nlm_host *	a_host;		/* host handle */
	struct nlm_args		a_args;		/* arguments */
	struct nlm_res		a_res;		/* result */
	struct nlm_block *	a_block;
	unsigned int		a_retries;	/* Retry count */
	u8			a_owner[NLMCLNT_OHSIZE];
};

/*
 * This struct describes a file held open by lockd on behalf of
 * an NFS client.
 */
struct nlm_file {
	struct nlm_file *	f_next;		/* linked list */
	struct nfs_fh		f_handle;	/* NFS file handle */
	struct file *		f_file;		/* VFS file pointer */
	struct nlm_share *	f_shares;	/* DOS shares */
	struct nlm_block *	f_blocks;	/* blocked locks */
	unsigned int		f_locks;	/* guesstimate # of locks */
	unsigned int		f_count;	/* reference count */
	struct semaphore	f_sema;		/* avoid concurrent access */
	int		       	f_hash;		/* hash of f_handle */
};

/*
 * This is a server block (i.e. a lock requested by some client which
 * couldn't be granted because of a conflicting lock).
 */
#define NLM_NEVER		(~(unsigned long) 0)
struct nlm_block {
	struct kref		b_count;	/* Reference count */
	struct nlm_block *	b_next;		/* linked list (all blocks) */
	struct nlm_block *	b_fnext;	/* linked list (per file) */
	struct nlm_rqst	*	b_call;		/* RPC args & callback info */
	struct svc_serv *	b_daemon;	/* NLM service */
	struct nlm_host *	b_host;		/* host handle for RPC clnt */
	unsigned long		b_when;		/* next re-xmit */
	unsigned int		b_id;		/* block id */
	unsigned char		b_queued;	/* re-queued */
	unsigned char		b_granted;	/* VFS granted lock */
	struct nlm_file *	b_file;		/* file in question */
};

/*
 * Valid actions for nlmsvc_traverse_files
 */
#define NLM_ACT_CHECK		0		/* check for locks */
#define NLM_ACT_MARK		1		/* mark & sweep */
#define NLM_ACT_UNLOCK		2		/* release all locks */

/*
 * Global variables
 */
extern struct rpc_program	nlm_program;
extern struct svc_procedure	nlmsvc_procedures[];
#ifdef CONFIG_LOCKD_V4
extern struct svc_procedure	nlmsvc_procedures4[];
#endif
extern int			nlmsvc_grace_period;
extern unsigned long		nlmsvc_timeout;

/*
 * Lockd client functions
 */
struct nlm_rqst * nlm_alloc_call(struct nlm_host *host);
void		  nlm_release_call(struct nlm_rqst *);
int		  nlm_async_call(struct nlm_rqst *, u32, const struct rpc_call_ops *);
int		  nlm_async_reply(struct nlm_rqst *, u32, const struct rpc_call_ops *);
struct nlm_wait * nlmclnt_prepare_block(struct nlm_host *host, struct file_lock *fl);
void		  nlmclnt_finish_block(struct nlm_wait *block);
int		  nlmclnt_block(struct nlm_wait *block, struct nlm_rqst *req, long timeout);
u32		  nlmclnt_grant(const struct sockaddr_in *addr, const struct nlm_lock *);
void		  nlmclnt_recovery(struct nlm_host *, u32);
int		  nlmclnt_reclaim(struct nlm_host *, struct file_lock *);

/*
 * Host cache
 */
struct nlm_host * nlmclnt_lookup_host(struct sockaddr_in *, int, int);
struct nlm_host * nlmsvc_lookup_host(struct svc_rqst *);
struct nlm_host * nlm_lookup_host(int server, struct sockaddr_in *, int, int);
struct rpc_clnt * nlm_bind_host(struct nlm_host *);
void		  nlm_rebind_host(struct nlm_host *);
struct nlm_host * nlm_get_host(struct nlm_host *);
void		  nlm_release_host(struct nlm_host *);
void		  nlm_shutdown_hosts(void);
extern struct nlm_host *nlm_find_client(void);


/*
 * Server-side lock handling
 */
u32		  nlmsvc_lock(struct svc_rqst *, struct nlm_file *,
					struct nlm_lock *, int, struct nlm_cookie *);
u32		  nlmsvc_unlock(struct nlm_file *, struct nlm_lock *);
u32		  nlmsvc_testlock(struct nlm_file *, struct nlm_lock *,
					struct nlm_lock *);
u32		  nlmsvc_cancel_blocked(struct nlm_file *, struct nlm_lock *);
unsigned long	  nlmsvc_retry_blocked(void);
void		  nlmsvc_traverse_blocks(struct nlm_host *, struct nlm_file *,
					int action);
void	  nlmsvc_grant_reply(struct svc_rqst *, struct nlm_cookie *, u32);

/*
 * File handling for the server personality
 */
u32		  nlm_lookup_file(struct svc_rqst *, struct nlm_file **,
					struct nfs_fh *);
void		  nlm_release_file(struct nlm_file *);
void		  nlmsvc_mark_resources(void);
void		  nlmsvc_free_host_resources(struct nlm_host *);
void		  nlmsvc_invalidate_all(void);

static __inline__ struct inode *
nlmsvc_file_inode(struct nlm_file *file)
{
	return file->f_file->f_dentry->d_inode;
}

/*
 * Compare two host addresses (needs modifying for ipv6)
 */
static __inline__ int
nlm_cmp_addr(const struct sockaddr_in *sin1, const struct sockaddr_in *sin2)
{
	return sin1->sin_addr.s_addr == sin2->sin_addr.s_addr;
}

/*
 * Compare two NLM locks.
 * When the second lock is of type F_UNLCK, this acts like a wildcard.
 */
static __inline__ int
nlm_compare_locks(const struct file_lock *fl1, const struct file_lock *fl2)
{
	return	fl1->fl_pid   == fl2->fl_pid
	     && fl1->fl_owner == fl2->fl_owner
	     && fl1->fl_start == fl2->fl_start
	     && fl1->fl_end   == fl2->fl_end
	     &&(fl1->fl_type  == fl2->fl_type || fl2->fl_type == F_UNLCK);
}

extern struct lock_manager_operations nlmsvc_lock_operations;

#endif /* __KERNEL__ */

#endif /* LINUX_LOCKD_LOCKD_H */
