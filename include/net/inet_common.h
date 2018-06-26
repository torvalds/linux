/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INET_COMMON_H
#define _INET_COMMON_H

extern const struct proto_ops inet_stream_ops;
extern const struct proto_ops inet_dgram_ops;

/*
 *	INET4 prototypes used by INET6
 */

struct msghdr;
struct sock;
struct sockaddr;
struct socket;

int inet_release(struct socket *sock);
int inet_stream_connect(struct socket *sock, struct sockaddr *uaddr,
			int addr_len, int flags);
int __inet_stream_connect(struct socket *sock, struct sockaddr *uaddr,
			  int addr_len, int flags, int is_sendmsg);
int inet_dgram_connect(struct socket *sock, struct sockaddr *uaddr,
		       int addr_len, int flags);
int inet_accept(struct socket *sock, struct socket *newsock, int flags,
		bool kern);
int inet_sendmsg(struct socket *sock, struct msghdr *msg, size_t size);
ssize_t inet_sendpage(struct socket *sock, struct page *page, int offset,
		      size_t size, int flags);
int inet_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
		 int flags);
int inet_shutdown(struct socket *sock, int how);
int inet_listen(struct socket *sock, int backlog);
void inet_sock_destruct(struct sock *sk);
int inet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len);
int __inet_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len,
		bool force_bind_address_no_port, bool with_lock);
int inet_getname(struct socket *sock, struct sockaddr *uaddr,
		 int peer);
int inet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
int inet_ctl_sock_create(struct sock **sk, unsigned short family,
			 unsigned short type, unsigned char protocol,
			 struct net *net);
int inet_recv_error(struct sock *sk, struct msghdr *msg, int len,
		    int *addr_len);

struct sk_buff *inet_gro_receive(struct list_head *head, struct sk_buff *skb);
int inet_gro_complete(struct sk_buff *skb, int nhoff);
struct sk_buff *inet_gso_segment(struct sk_buff *skb,
				 netdev_features_t features);

static inline void inet_ctl_sock_destroy(struct sock *sk)
{
	if (sk)
		sock_release(sk->sk_socket);
}

#endif
