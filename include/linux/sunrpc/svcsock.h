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

	/* We keep the old state_change and data_ready CB's here */
	void			(*sk_ostate)(struct sock *);
	void			(*sk_odata)(struct sock *, int bytes);
	void			(*sk_owspace)(struct sock *);

	/* private TCP part */
	u32			sk_reclen;	/* length of record */
	u32			sk_tcplen;	/* current read length */
	struct page *		sk_pages[RPCSVC_MAXPAGES];	/* received data */
};

/*
 * Function prototypes.
 */
void		svc_close_net(struct svc_serv *, struct net *);
int		svc_recv(struct svc_rqst *, long);
int		svc_send(struct svc_rqst *);
void		svc_drop(struct svc_rqst *);
void		svc_sock_update_bufs(struct svc_serv *serv);
int		svc_sock_names(struct svc_serv *serv, char *buf,
					const size_t buflen,
					const char *toclose);
int		svc_addsock(struct svc_serv *serv, const int fd,
					char *name_return, const size_t len);
void		svc_init_xprt_sock(void);
void		svc_cleanup_xprt_sock(void);
struct svc_xprt *svc_sock_create(struct svc_serv *serv, int prot);
void		svc_sock_destroy(struct svc_xprt *);

/*
 * svc_makesock socket characteristics
 */
#define SVC_SOCK_DEFAULTS	(0U)
#define SVC_SOCK_ANONYMOUS	(1U << 0)	/* don't register with pmap */
#define SVC_SOCK_TEMPORARY	(1U << 1)	/* flag socket as temporary */

#endif /* SUNRPC_SVCSOCK_H */
