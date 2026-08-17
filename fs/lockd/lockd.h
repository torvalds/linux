/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LOCKD_LOCKD_H
#define _LOCKD_LOCKD_H

#include <linux/exportfs.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <net/ipv6.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/refcount.h>
#include <linux/utsname.h>
#include "nlm.h"
#include <linux/lockd/bind.h>
#include "xdr.h"
#include <linux/sunrpc/debug.h>
#include <linux/sunrpc/svc.h>

/*
 * Enable lockd debugging.
 * Requires CONFIG_SUNRPC_DEBUG.
 */
#undef ifdebug
#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define ifdebug(flag)		if (unlikely(nlm_debug & NLMDBG_##flag))
#else
# define ifdebug(flag)		if (0)
#endif

#define NLMDBG_SVC		0x0001
#define NLMDBG_CLIENT		0x0002
#define NLMDBG_CLNTLOCK		0x0004
#define NLMDBG_SVCLOCK		0x0008
#define NLMDBG_MONITOR		0x0010
#define NLMDBG_CLNTSUBS		0x0020
#define NLMDBG_SVCSUBS		0x0040
#define NLMDBG_HOSTCACHE	0x0080
#define NLMDBG_XDR		0x0100
#define NLMDBG_ALL		0x7fff

/*
 * Version string
 */
#define LOCKD_VERSION		"0.5"

/*
 * Default timeout for RPC calls (seconds)
 */
#define LOCKD_DFLT_TIMEO	10

/*
 * Number of leading bytes of nfs_fh.data that file_hash()
 * digests when bucketing nlm_files[]. Sized for historical
 * NFSv2 handles; nfs_fh.data must be initialized at least
 * this far before lookup, regardless of fh.size.
 */
#define LOCKD_FH_HASH_SIZE	32

/* error codes new to NLMv4 */
#define	nlm4_deadlock		cpu_to_be32(NLM_DEADLCK)
#define	nlm4_rofs		cpu_to_be32(NLM_ROFS)
#define	nlm4_stale_fh		cpu_to_be32(NLM_STALE_FH)
#define	nlm4_fbig		cpu_to_be32(NLM_FBIG)
#define	nlm4_failed		cpu_to_be32(NLM_FAILED)

/*
 * Internal-use status codes, not to be placed on the wire.
 * Version handlers translate these to appropriate wire values.
 */
#define nlm__int__drop_reply	cpu_to_be32(30000)
#define nlm__int__deadlock	cpu_to_be32(30001)
#define nlm__int__stale_fh	cpu_to_be32(30002)
#define nlm__int__failed	cpu_to_be32(30003)

/*
 * Lockd host handle (used both by the client and server personality).
 */
struct nlm_host {
	struct hlist_node	h_hash;		/* doubly linked list */
	struct sockaddr_storage	h_addr;		/* peer address */
	size_t			h_addrlen;
	struct sockaddr_storage	h_srcaddr;	/* our address (optional) */
	size_t			h_srcaddrlen;
	struct rpc_clnt		*h_rpcclnt;	/* RPC client to talk to peer */
	char			*h_name;		/* remote hostname */
	u32			h_version;	/* interface version */
	unsigned short		h_proto;	/* transport proto */
	unsigned short		h_reclaiming : 1,
				h_server     : 1, /* server side, not client side */
				h_noresvport : 1,
				h_inuse      : 1;
	wait_queue_head_t	h_gracewait;	/* wait while reclaiming */
	struct rw_semaphore	h_rwsem;	/* Reboot recovery lock */
	u32			h_state;	/* pseudo-state counter */
	u32			h_nsmstate;	/* true remote NSM state */
	u32			h_pidcount;	/* Pseudopids */
	refcount_t		h_count;	/* reference count */
	struct mutex		h_mutex;	/* mutex for pmap binding */
	unsigned long		h_nextrebind;	/* next portmap call */
	unsigned long		h_expires;	/* eligible for GC */
	struct list_head	h_lockowners;	/* Lockowners for the client */
	spinlock_t		h_lock;
	struct list_head	h_granted;	/* Locks in GRANTED state */
	struct list_head	h_reclaim;	/* Locks in RECLAIM state */
	struct nsm_handle	*h_nsmhandle;	/* NSM status handle */
	char			*h_addrbuf;	/* address eyecatcher */
	struct net		*net;		/* host net */
	const struct cred	*h_cred;
	char			nodename[UNX_MAXNODENAME + 1];
	const struct nlmclnt_operations	*h_nlmclnt_ops;	/* Callback ops for NLM users */
};

/*
 * The largest string sm_addrbuf should hold is a full-size IPv6 address
 * (no "::" anywhere) with a scope ID.  The buffer size is computed to
 * hold eight groups of colon-separated four-hex-digit numbers, a
 * percent sign, a scope id (at most 32 bits, in decimal), and NUL.
 */
#define NSM_ADDRBUF		((8 * 4 + 7) + (1 + 10) + 1)

struct nsm_handle {
	struct list_head	sm_link;
	refcount_t		sm_count;
	char			*sm_mon_name;
	char			*sm_name;
	struct sockaddr_storage	sm_addr;
	size_t			sm_addrlen;
	unsigned int		sm_monitored : 1,
				sm_sticky : 1;	/* don't unmonitor */
	struct nsm_private	sm_priv;
	char			sm_addrbuf[NSM_ADDRBUF];
};

/*
 * Rigorous type checking on sockaddr type conversions
 */
static inline struct sockaddr *nlm_addr(const struct nlm_host *host)
{
	return (struct sockaddr *)&host->h_addr;
}

static inline struct sockaddr *nlm_srcaddr(const struct nlm_host *host)
{
	return (struct sockaddr *)&host->h_srcaddr;
}

/*
 * Map an fl_owner_t into a unique 32-bit "pid"
 */
struct nlm_lockowner {
	struct list_head list;
	refcount_t count;

	struct nlm_host *host;
	fl_owner_t owner;
	uint32_t pid;
};

/*
 * This is the representation of a blocked client lock.
 */
struct nlm_wait {
	struct list_head	b_list;		/* linked list */
	wait_queue_head_t	b_wait;		/* where to wait on */
	struct nlm_host		*b_host;
	struct file_lock	*b_lock;	/* local file lock */
	__be32			b_status;	/* grant callback status */
};

/*
 * Memory chunk for NLM client RPC request.
 */
#define NLMCLNT_OHSIZE		((__NEW_UTS_LEN) + 10u)
struct nlm_rqst {
	refcount_t		a_count;
	unsigned int		a_flags;	/* initial RPC task flags */
	struct nlm_host *	a_host;		/* host handle */
	struct lockd_args	a_args;		/* arguments */
	struct lockd_res	a_res;		/* result */
	struct nlm_block *	a_block;
	unsigned int		a_retries;	/* Retry count */
	u8			a_owner[NLMCLNT_OHSIZE];
	void *	a_callback_data; /* sent to nlmclnt_operations callbacks */
};

struct lockd_share;

/*
 * This struct describes a file held open by lockd on behalf of
 * an NFS client.
 */
struct nlm_file {
	struct hlist_node	f_list;		/* linked list */
	struct nfs_fh		f_handle;	/* NFS file handle */
	struct file *		f_file[2];	/* VFS file pointers,
						   indexed by O_ flags */
	struct lockd_share *	f_shares;	/* DOS shares */
	struct list_head	f_blocks;	/* blocked locks */
	unsigned int		f_locks;	/* guesstimate # of locks */
	unsigned int		f_count;	/* reference count */
	struct mutex		f_mutex;	/* avoid concurrent access */
};

/*
 * This is a server block (i.e. a lock requested by some client which
 * couldn't be granted because of a conflicting lock).
 */
#define NLM_NEVER		(~(unsigned long) 0)
/* timeout on non-blocking call: */
#define NLM_TIMEOUT		(7 * HZ)

struct nlm_block {
	struct kref		b_count;	/* Reference count */
	struct list_head	b_list;		/* linked list of all blocks */
	struct list_head	b_flist;	/* linked list (per file) */
	struct nlm_rqst	*	b_call;		/* RPC args & callback info */
	struct svc_serv *	b_daemon;	/* NLM service */
	struct nlm_host *	b_host;		/* host handle for RPC clnt */
	unsigned long		b_when;		/* next re-xmit */
	unsigned int		b_id;		/* block id */
	unsigned char		b_granted;	/* VFS granted lock */
	struct nlm_file *	b_file;		/* file in question */
	struct cache_req *	b_cache_req;	/* deferred request handling */
	struct cache_deferred_req * b_deferred_req;
	unsigned int		b_flags;	/* block flags */
#define B_QUEUED		1	/* lock queued */
#define B_GOT_CALLBACK		2	/* got lock or conflicting lock */
#define B_TIMED_OUT		4	/* filesystem too slow to respond */
};

/*
 * Global variables
 */
extern const struct rpc_program	nlm_program;
extern const struct svc_version nlmsvc_version1;
extern const struct svc_version nlmsvc_version3;
#ifdef CONFIG_LOCKD_V4
extern const struct svc_version nlmsvc_version4;
#endif
extern int			nlmsvc_grace_period;
extern unsigned long		nlm_timeout;
extern bool			nsm_use_hostnames;
extern u32			nsm_local_state;

extern struct timer_list	nlmsvc_retry;

/*
 * Lockd client functions
 */
struct nlm_rqst * nlm_alloc_call(struct nlm_host *host);
int		  nlm_async_call(struct nlm_rqst *, u32, const struct rpc_call_ops *);
int		  nlm_async_reply(struct nlm_rqst *, u32, const struct rpc_call_ops *);
void		  nlmclnt_release_call(struct nlm_rqst *);
void		  nlmclnt_prepare_block(struct nlm_wait *block, struct nlm_host *host,
					struct file_lock *fl);
void		  nlmclnt_queue_block(struct nlm_wait *block);
__be32		  nlmclnt_dequeue_block(struct nlm_wait *block);
int		  nlmclnt_wait(struct nlm_wait *block, struct nlm_rqst *req, long timeout);
__be32		  nlmclnt_grant(const struct sockaddr *addr,
				const struct lockd_lock *lock);
void		  nlmclnt_recovery(struct nlm_host *);
int		  nlmclnt_reclaim(struct nlm_host *, struct file_lock *,
				  struct nlm_rqst *);
void		  nlmclnt_next_cookie(struct lockd_cookie *);

#ifdef CONFIG_LOCKD_V4
extern const struct rpc_version nlm_version4;
#endif

/*
 * Host cache
 */
struct nlm_host  *nlmclnt_lookup_host(const struct sockaddr *sap,
					const size_t salen,
					const unsigned short protocol,
					const u32 version,
					const char *hostname,
					int noresvport,
					struct net *net,
					const struct cred *cred);
void		  nlmclnt_release_host(struct nlm_host *);
struct nlm_host  *nlmsvc_lookup_host(const struct svc_rqst *rqstp,
					const char *hostname,
					const size_t hostname_len);
void		  nlmsvc_release_host(struct nlm_host *);
struct rpc_clnt * nlm_bind_host(struct nlm_host *);
void		  nlm_rebind_host(struct nlm_host *);
struct nlm_host * nlm_get_host(struct nlm_host *);
void		  nlm_shutdown_hosts(void);
void		  nlm_shutdown_hosts_net(struct net *net);
void		  nlm_host_rebooted(const struct net *net,
					const struct lockd_reboot *);

/*
 * Host monitoring
 */
int		  nsm_monitor(const struct nlm_host *host);
void		  nsm_unmonitor(const struct nlm_host *host);

struct nsm_handle *nsm_get_handle(const struct net *net,
					const struct sockaddr *sap,
					const size_t salen,
					const char *hostname,
					const size_t hostname_len);
struct nsm_handle *nsm_reboot_lookup(const struct net *net,
					const struct lockd_reboot *info);
void		  nsm_release(struct nsm_handle *nsm);

/*
 * This is used in garbage collection and resource reclaim
 * A return value != 0 means destroy the lock/block/share
 */
typedef int	  (*nlm_host_match_fn_t)(void *cur, struct nlm_host *ref);

/*
 * Server-side lock handling
 */
int		  lock_to_openmode(struct file_lock *);
__be32		  nlmsvc_lock(struct svc_rqst *, struct nlm_file *,
			      struct nlm_host *, struct lockd_lock *, int,
			      struct lockd_cookie *, int);
__be32		  nlmsvc_unlock(struct net *net, struct nlm_file *, struct lockd_lock *);
__be32		  nlmsvc_testlock(struct svc_rqst *rqstp, struct nlm_file *file,
			struct nlm_host *host, struct lockd_lock *lock,
			struct lockd_lock *conflock);
__be32		  nlmsvc_cancel_blocked(struct net *net, struct nlm_file *, struct lockd_lock *);
void		  nlmsvc_retry_blocked(struct svc_rqst *rqstp);
void		  nlmsvc_traverse_blocks(struct nlm_host *, struct nlm_file *,
					nlm_host_match_fn_t match);
void		  nlmsvc_grant_reply(struct lockd_cookie *, __be32);
void		  nlmsvc_release_call(struct nlm_rqst *);
void		  nlmsvc_locks_init_private(struct file_lock *, struct nlm_host *, pid_t);
int		  nlmsvc_dispatch(struct svc_rqst *rqstp);

/*
 * File handling for the server personality
 */
__be32		  nlm_lookup_file(struct svc_rqst *, struct nlm_file **,
				  struct lockd_lock *, int);
void		  nlm_release_file(struct nlm_file *);
void		  nlmsvc_put_lockowner(struct nlm_lockowner *);
void		  nlmsvc_release_lockowner(struct lockd_lock *);
void		  nlmsvc_mark_resources(struct net *);
void		  nlmsvc_free_host_resources(struct nlm_host *);
void		  nlmsvc_invalidate_all(void);

static inline struct file *nlmsvc_file_file(const struct nlm_file *file)
{
	return file->f_file[O_RDONLY] ?
	       file->f_file[O_RDONLY] : file->f_file[O_WRONLY];
}

static inline struct inode *nlmsvc_file_inode(struct nlm_file *file)
{
	return file_inode(nlmsvc_file_file(file));
}

static inline bool
nlmsvc_file_cannot_lock(const struct nlm_file *file)
{
	return exportfs_cannot_lock(nlmsvc_file_file(file)->f_path.dentry->d_sb->s_export_op);
}

static inline int __nlm_privileged_request4(const struct sockaddr *sap)
{
	const struct sockaddr_in *sin = (struct sockaddr_in *)sap;

	if (ntohs(sin->sin_port) > 1023)
		return 0;

	return ipv4_is_loopback(sin->sin_addr.s_addr);
}

#if IS_ENABLED(CONFIG_IPV6)
static inline int __nlm_privileged_request6(const struct sockaddr *sap)
{
	const struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sap;

	if (ntohs(sin6->sin6_port) > 1023)
		return 0;

	if (ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_MAPPED)
		return ipv4_is_loopback(sin6->sin6_addr.s6_addr32[3]);

	return ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LOOPBACK;
}
#else	/* IS_ENABLED(CONFIG_IPV6) */
static inline int __nlm_privileged_request6(const struct sockaddr *sap)
{
	return 0;
}
#endif	/* IS_ENABLED(CONFIG_IPV6) */

/*
 * Ensure incoming requests are from local privileged callers.
 *
 * Return TRUE if sender is local and is connecting via a privileged port;
 * otherwise return FALSE.
 */
static inline int nlm_privileged_requester(const struct svc_rqst *rqstp)
{
	const struct sockaddr *sap = svc_addr(rqstp);

	switch (sap->sa_family) {
	case AF_INET:
		return __nlm_privileged_request4(sap);
	case AF_INET6:
		return __nlm_privileged_request6(sap);
	default:
		return 0;
	}
}

/*
 * Compare two NLM locks.
 * When the second lock is of type F_UNLCK, this acts like a wildcard.
 */
static inline int nlm_compare_locks(const struct file_lock *fl1,
				    const struct file_lock *fl2)
{
	return file_inode(fl1->c.flc_file) == file_inode(fl2->c.flc_file)
	     && fl1->c.flc_pid   == fl2->c.flc_pid
	     && fl1->c.flc_owner == fl2->c.flc_owner
	     && fl1->fl_start == fl2->fl_start
	     && fl1->fl_end   == fl2->fl_end
	     &&(fl1->c.flc_type  == fl2->c.flc_type || fl2->c.flc_type == F_UNLCK);
}

/**
 * lockd_set_file_lock_range3 - set the byte range of a file_lock
 * @fl: file_lock whose length fields are to be initialized
 * @off: starting offset of the lock, in bytes
 * @len: length of the byte range, in bytes, or zero
 *
 * NLMv3 uses a (start, length) representation for lock byte ranges,
 * while the kernel's file_lock uses (start, end). Treat a length of
 * zero or arithmetic overflow (end wrapping negative when the sum
 * exceeds S32_MAX) as "lock to end of file."
 */
static inline void
lockd_set_file_lock_range3(struct file_lock *fl, u32 off, u32 len)
{
	s32 end = off + len - 1;

	fl->fl_start = off;
	if (len == 0 || end < 0)
		fl->fl_end = OFFSET_MAX;
	else
		fl->fl_end = end;
}

/**
 * lockd_set_file_lock_range4 - set the byte range of a file_lock
 * @fl: file_lock whose length fields are to be initialized
 * @off: starting offset of the lock, in bytes
 * @len: length of the byte range, in bytes, or zero
 *
 * The NLMv4 protocol represents lock byte ranges as (start, length),
 * where length zero means "lock to end of file." The kernel's file_lock
 * structure uses (start, end) representation. Convert from NLMv4 format
 * to file_lock format, clamping the starting offset and treating
 * arithmetic overflow as "lock to EOF."
 */
static inline void
lockd_set_file_lock_range4(struct file_lock *fl, u64 off, u64 len)
{
	u64 clamped_off = (off > OFFSET_MAX) ? OFFSET_MAX : off;
	s64 end = clamped_off + len - 1;

	fl->fl_start = clamped_off;
	if (len == 0 || end < 0)
		fl->fl_end = OFFSET_MAX;
	else
		fl->fl_end = end;
}

extern const struct lock_manager_operations nlmsvc_lock_operations;

#endif /* _LOCKD_LOCKD_H */
