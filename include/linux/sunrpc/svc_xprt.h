/*
 * linux/include/linux/sunrpc/svc_xprt.h
 *
 * RPC server transport I/O
 */

#ifndef SUNRPC_SVC_XPRT_H
#define SUNRPC_SVC_XPRT_H

#include <linux/sunrpc/svc.h>
#include <linux/module.h>

struct svc_xprt_ops {
	struct svc_xprt	*(*xpo_create)(struct svc_serv *,
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
};

struct svc_xprt_class {
	const char		*xcl_name;
	struct module		*xcl_owner;
	struct svc_xprt_ops	*xcl_ops;
	struct list_head	xcl_list;
	u32			xcl_max_payload;
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

	struct svc_pool		*xpt_pool;	/* current pool iff queued */
	struct svc_serv		*xpt_server;	/* service for transport */
	atomic_t    	    	xpt_reserved;	/* space on outq that is rsvd */
	struct mutex		xpt_mutex;	/* to serialize sending data */
};

int	svc_reg_xprt_class(struct svc_xprt_class *);
void	svc_unreg_xprt_class(struct svc_xprt_class *);
void	svc_xprt_init(struct svc_xprt_class *, struct svc_xprt *,
		      struct svc_serv *);
int	svc_create_xprt(struct svc_serv *, char *, unsigned short, int);
void	svc_xprt_received(struct svc_xprt *);
void	svc_xprt_put(struct svc_xprt *xprt);
static inline void svc_xprt_get(struct svc_xprt *xprt)
{
	kref_get(&xprt->xpt_ref);
}

#endif /* SUNRPC_SVC_XPRT_H */
