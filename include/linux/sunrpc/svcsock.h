/*
 * linux/include/linux/sunrpc/svcsock.h
 *
 * RPC server socket I/O.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef SUNRPC_SVCSOCK_H
#define SUNRPC_SVCSOCK_H

#include <linux/sunrpc/svc.h>

/*
 * RPC server socket.
 */
struct svc_sock {
	struct list_head	sk_ready;	/* list of ready sockets */
	struct list_head	sk_list;	/* list of all sockets */
	struct socket *		sk_sock;	/* berkeley socket layer */
	struct sock *		sk_sk;		/* INET layer */

	struct svc_pool *	sk_pool;	/* current pool iff queued */
	struct svc_serv *	sk_server;	/* service for this socket */
	atomic_t		sk_inuse;	/* use count */
	unsigned long		sk_flags;
#define	SK_BUSY		0			/* enqueued/receiving */
#define	SK_CONN		1			/* conn pending */
#define	SK_CLOSE	2			/* dead or dying */
#define	SK_DATA		3			/* data pending */
#define	SK_TEMP		4			/* temp (TCP) socket */
#define	SK_DEAD		6			/* socket closed */
#define	SK_CHNGBUF	7			/* need to change snd/rcv buffer sizes */
#define	SK_DEFERRED	8			/* request on sk_deferred */
#define	SK_OLD		9			/* used for temp socket aging mark+sweep */
#define	SK_DETACHED	10			/* detached from tempsocks list */

	atomic_t    	    	sk_reserved;	/* space on outq that is reserved */

	spinlock_t		sk_lock;	/* protects sk_deferred and
						 * sk_info_authunix */
	struct list_head	sk_deferred;	/* deferred requests that need to
						 * be revisted */
	struct mutex		sk_mutex;	/* to serialize sending data */

	int			(*sk_recvfrom)(struct svc_rqst *rqstp);
	int			(*sk_sendto)(struct svc_rqst *rqstp);

	/* We keep the old state_change and data_ready CB's here */
	void			(*sk_ostate)(struct sock *);
	void			(*sk_odata)(struct sock *, int bytes);
	void			(*sk_owspace)(struct sock *);

	/* private TCP part */
	int			sk_reclen;	/* length of record */
	int			sk_tcplen;	/* current read length */
	time_t			sk_lastrecv;	/* time of last received request */

	/* cache of various info for TCP sockets */
	void			*sk_info_authunix;

	struct sockaddr_storage	sk_local;	/* local address */
	struct sockaddr_storage	sk_remote;	/* remote peer's address */
	int			sk_remotelen;	/* length of address */
};

/*
 * Function prototypes.
 */
int		svc_makesock(struct svc_serv *, int, unsigned short, int flags);
void		svc_force_close_socket(struct svc_sock *);
int		svc_recv(struct svc_rqst *, long);
int		svc_send(struct svc_rqst *);
void		svc_drop(struct svc_rqst *);
void		svc_sock_update_bufs(struct svc_serv *serv);
int		svc_sock_names(char *buf, struct svc_serv *serv, char *toclose);
int		svc_addsock(struct svc_serv *serv,
			    int fd,
			    char *name_return,
			    int *proto);

/*
 * svc_makesock socket characteristics
 */
#define SVC_SOCK_DEFAULTS	(0U)
#define SVC_SOCK_ANONYMOUS	(1U << 0)	/* don't register with pmap */
#define SVC_SOCK_TEMPORARY	(1U << 1)	/* flag socket as temporary */

#endif /* SUNRPC_SVCSOCK_H */
