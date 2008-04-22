#ifndef _INET_COMMON_H
#define _INET_COMMON_H

extern const struct proto_ops		inet_stream_ops;
extern const struct proto_ops		inet_dgram_ops;

/*
 *	INET4 prototypes used by INET6
 */

struct msghdr;
struct sock;
struct sockaddr;
struct socket;

extern int			inet_release(struct socket *sock);
extern int			inet_stream_connect(struct socket *sock,
						    struct sockaddr * uaddr,
						    int addr_len, int flags);
extern int			inet_dgram_connect(struct socket *sock, 
						   struct sockaddr * uaddr,
						   int addr_len, int flags);
extern int			inet_accept(struct socket *sock, 
					    struct socket *newsock, int flags);
extern int			inet_sendmsg(struct kiocb *iocb,
					     struct socket *sock, 
					     struct msghdr *msg, 
					     size_t size);
extern int			inet_shutdown(struct socket *sock, int how);
extern int			inet_listen(struct socket *sock, int backlog);

extern void			inet_sock_destruct(struct sock *sk);

extern int			inet_bind(struct socket *sock, 
					  struct sockaddr *uaddr, int addr_len);
extern int			inet_getname(struct socket *sock, 
					     struct sockaddr *uaddr, 
					     int *uaddr_len, int peer);
extern int			inet_ioctl(struct socket *sock, 
					   unsigned int cmd, unsigned long arg);

extern int			inet_ctl_sock_create(struct sock **sk,
						     unsigned short family,
						     unsigned short type,
						     unsigned char protocol,
						     struct net *net);

static inline void inet_ctl_sock_destroy(struct sock *sk)
{
	sk_release_kernel(sk);
}

#endif


