/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INET_COMMON_H
#define _INET_COMMON_H

#include <linux/indirect_call_wrapper.h>
#include <linux/net.h>
#include <linux/netdev_features.h>
#include <linux/types.h>
#include <net/sock.h>

extern const struct proto_ops inet_stream_ops;
extern const struct proto_ops inet_dgram_ops;

/*
 *	INET4 prototypes used by INET6
 */

struct msghdr;
struct net;
struct page;
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
void __inet_accept(struct socket *sock, struct socket *newsock,
		   struct sock *newsk);
int inet_send_prepare(struct sock *sk);
int inet_sendmsg(struct socket *sock, struct msghdr *msg, size_t size);
void inet_splice_eof(struct socket *sock);
int inet_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
		 int flags);
int inet_shutdown(struct socket *sock, int how);
int inet_listen(struct socket *sock, int backlog);
void inet_sock_destruct(struct sock *sk);
int inet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len);
/* Don't allocate port at this moment, defer to connect. */
#define BIND_FORCE_ADDRESS_NO_PORT	(1 << 0)
/* Grab and release socket lock. */
#define BIND_WITH_LOCK			(1 << 1)
/* Called from BPF program. */
#define BIND_FROM_BPF			(1 << 2)
/* Skip CAP_NET_BIND_SERVICE check. */
#define BIND_NO_CAP_NET_BIND_SERVICE	(1 << 3)
int __inet_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len,
		u32 flags);
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

#define indirect_call_gro_receive(f2, f1, cb, head, skb)	\
({								\
	unlikely(gro_recursion_inc_test(skb)) ?			\
		NAPI_GRO_CB(skb)->flush |= 1, NULL :		\
		INDIRECT_CALL_2(cb, f2, f1, head, skb);		\
})

#endif
