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
#include <linux/sunrpc/svc_xprt.h>

/*
 * RPC server socket.
 */
struct svc_sock {
	struct svc_xprt		sk_xprt;
	struct socket *		sk_sock;	/* berkeley socket layer */
	struct sock *		sk_sk;		/* INET layer */

	atomic_t    	    	sk_reserved;	/* space on outq that is reserved */

	spinlock_t		sk_lock;	/* protects sk_deferred and
						 * sk_info_authunix */
	struct list_head	sk_deferred;	/* deferred requests that need to
						 * be revisted */
	struct mutex		sk_mutex;	/* to serialize sending data */

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
void		svc_close_all(struct list_head *);
int		svc_recv(struct svc_rqst *, long);
int		svc_send(struct svc_rqst *);
void		svc_drop(struct svc_rqst *);
void		svc_sock_update_bufs(struct svc_serv *serv);
int		svc_sock_names(char *buf, struct svc_serv *serv, char *toclose);
int		svc_addsock(struct svc_serv *serv,
			    int fd,
			    char *name_return,
			    int *proto);
void		svc_init_xprt_sock(void);
void		svc_cleanup_xprt_sock(void);

/*
 * svc_makesock socket characteristics
 */
#define SVC_SOCK_DEFAULTS	(0U)
#define SVC_SOCK_ANONYMOUS	(1U << 0)	/* don't register with pmap */
#define SVC_SOCK_TEMPORARY	(1U << 1)	/* flag socket as temporary */

#endif /* SUNRPC_SVCSOCK_H */
