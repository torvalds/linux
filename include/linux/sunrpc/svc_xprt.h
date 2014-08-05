/*
 * linux/include/linux/sunrpc/svc_xprt.h
 *
 * RPC server transport I/O
 */

#ifndef SUNRPC_SVC_XPRT_H
#define SUNRPC_SVC_XPRT_H

#include <linux/sunrpc/svc.h>

struct module;

struct svc_xprt_ops {
	struct svc_xprt	*(*xpo_create)(struct svc_serv *,
				       struct net *net,
				       struct sockaddr *, int,
				       int);
	struct svc_xprt	*(*xpo_accept)(struct svc_xprt *);
	int		(*xpo_has_wspace)(struct svc_xprt *);
	int		(*xpo_recvfrom)(struct svc_rqst *);
	void		(*xpo_prep_reply_hdr)(struct svc_rqst *);
	int		(*xpo_sendto)(struct svc_rqst *);
	void		(*xpo_release_rqst)(struct svc_rqst *);
	void		(*xpo_detach)(struct svc_xprt *);
	void		(*xpo_free)(struct svc_xprt *);
	int		(*xpo_secure_port)(struct svc_rqst *);
};

struct svc_xprt_class {
	const char		*xcl_name;
	struct module		*xcl_owner;
	struct svc_xprt_ops	*xcl_ops;
	struct list_head	xcl_list;
	u32			xcl_max_payload;
};

/*
 * This is embedded in an object that wants a callback before deleting
 * an xprt; intended for use by NFSv4.1, which needs to know when a
 * client's tcp connection (and hence possibly a backchannel) goes away.
 */
struct svc_xpt_user {
	struct list_head list;
	void (*callback)(struct svc_xpt_user *);
};

struct svc_xprt {
	struct svc_xprt_class	*xpt_class;
	struct svc_xprt_ops	*xpt_ops;
	struct kref		xpt_ref;
	struct list_head	xpt_list;
	struct list_head	xpt_ready;
	unsigned long		xpt_flags;
#define	XPT_BUSY	0		/* enqueued/receiving */
#define	XPT_CONN	1		/* conn pending */
#define	XPT_CLOSE	2		/* dead or dying */
#define	XPT_DATA	3		/* data pending */
#define	XPT_TEMP	4		/* connected transport */
#define	XPT_DEAD	6		/* transport closed */
#define	XPT_CHNGBUF	7		/* need to change snd/rcv buf sizes */
#define	XPT_DEFERRED	8		/* deferred request pending */
#define	XPT_OLD		9		/* used for xprt aging mark+sweep */
#define	XPT_DETACHED	10		/* detached from tempsocks list */
#define XPT_LISTENER	11		/* listening endpoint */
#define XPT_CACHE_AUTH	12		/* cache auth info */
#define XPT_LOCAL	13		/* connection from loopback interface */

	struct svc_serv		*xpt_server;	/* service for transport */
	atomic_t    	    	xpt_reserved;	/* space on outq that is rsvd */
	struct mutex		xpt_mutex;	/* to serialize sending data */
	spinlock_t		xpt_lock;	/* protects sk_deferred
						 * and xpt_auth_cache */
	void			*xpt_auth_cache;/* auth cache */
	struct list_head	xpt_deferred;	/* deferred requests that need
						 * to be revisted */
	struct sockaddr_storage	xpt_local;	/* local address */
	size_t			xpt_locallen;	/* length of address */
	struct sockaddr_storage	xpt_remote;	/* remote peer's address */
	size_t			xpt_remotelen;	/* length of address */
	struct rpc_wait_queue	xpt_bc_pending;	/* backchannel wait queue */
	struct list_head	xpt_users;	/* callbacks on free */

	struct net		*xpt_net;
	struct rpc_xprt		*xpt_bc_xprt;	/* NFSv4.1 backchannel */
};

static inline void unregister_xpt_user(struct svc_xprt *xpt, struct svc_xpt_user *u)
{
	spin_lock(&xpt->xpt_lock);
	list_del_init(&u->list);
	spin_unlock(&xpt->xpt_lock);
}

static inline int register_xpt_user(struct svc_xprt *xpt, struct svc_xpt_user *u)
{
	spin_lock(&xpt->xpt_lock);
	if (test_bit(XPT_CLOSE, &xpt->xpt_flags)) {
		/*
		 * The connection is about to be deleted soon (or,
		 * worse, may already be deleted--in which case we've
		 * already notified the xpt_users).
		 */
		spin_unlock(&xpt->xpt_lock);
		return -ENOTCONN;
	}
	list_add(&u->list, &xpt->xpt_users);
	spin_unlock(&xpt->xpt_lock);
	return 0;
}

int	svc_reg_xprt_class(struct svc_xprt_class *);
void	svc_unreg_xprt_class(struct svc_xprt_class *);
void	svc_xprt_init(struct net *, struct svc_xprt_class *, struct svc_xprt *,
		      struct svc_serv *);
int	svc_create_xprt(struct svc_serv *, const char *, struct net *,
			const int, const unsigned short, int);
void	svc_xprt_enqueue(struct svc_xprt *xprt);
void	svc_xprt_put(struct svc_xprt *xprt);
void	svc_xprt_copy_addrs(struct svc_rqst *rqstp, struct svc_xprt *xprt);
void	svc_close_xprt(struct svc_xprt *xprt);
int	svc_port_is_privileged(struct sockaddr *sin);
int	svc_print_xprts(char *buf, int maxlen);
struct	svc_xprt *svc_find_xprt(struct svc_serv *serv, const char *xcl_name,
			struct net *net, const sa_family_t af,
			const unsigned short port);
int	svc_xprt_names(struct svc_serv *serv, char *buf, const int buflen);
void	svc_add_new_perm_xprt(struct svc_serv *serv, struct svc_xprt *xprt);

static inline void svc_xprt_get(struct svc_xprt *xprt)
{
	kref_get(&xprt->xpt_ref);
}
static inline void svc_xprt_set_local(struct svc_xprt *xprt,
				      const struct sockaddr *sa,
				      const size_t salen)
{
	memcpy(&xprt->xpt_local, sa, salen);
	xprt->xpt_locallen = salen;
}
static inline void svc_xprt_set_remote(struct svc_xprt *xprt,
				       const struct sockaddr *sa,
				       const size_t salen)
{
	memcpy(&xprt->xpt_remote, sa, salen);
	xprt->xpt_remotelen = salen;
}
static inline unsigned short svc_addr_port(const struct sockaddr *sa)
{
	const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
	const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;

	switch (sa->sa_family) {
	case AF_INET:
		return ntohs(sin->sin_port);
	case AF_INET6:
		return ntohs(sin6->sin6_port);
	}

	return 0;
}

static inline size_t svc_addr_len(const struct sockaddr *sa)
{
	switch (sa->sa_family) {
	case AF_INET:
		return sizeof(struct sockaddr_in);
	case AF_INET6:
		return sizeof(struct sockaddr_in6);
	}
	BUG();
}

static inline unsigned short svc_xprt_local_port(const struct svc_xprt *xprt)
{
	return svc_addr_port((const struct sockaddr *)&xprt->xpt_local);
}

static inline unsigned short svc_xprt_remote_port(const struct svc_xprt *xprt)
{
	return svc_addr_port((const struct sockaddr *)&xprt->xpt_remote);
}

static inline char *__svc_print_addr(const struct sockaddr *addr,
				     char *buf, const size_t len)
{
	const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
	const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)addr;

	switch (addr->sa_family) {
	case AF_INET:
		snprintf(buf, len, "%pI4, port=%u", &sin->sin_addr,
			ntohs(sin->sin_port));
		break;

	case AF_INET6:
		snprintf(buf, len, "%pI6, port=%u",
			 &sin6->sin6_addr,
			ntohs(sin6->sin6_port));
		break;

	default:
		snprintf(buf, len, "unknown address type: %d", addr->sa_family);
		break;
	}

	return buf;
}
#endif /* SUNRPC_SVC_XPRT_H */
