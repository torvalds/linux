/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _UDP6_IMPL_H
#define _UDP6_IMPL_H
#include <net/udp.h>
#include <net/udplite.h>
#include <net/protocol.h>
#include <net/addrconf.h>
#include <net/inet_common.h>
#include <net/transp_v6.h>

int __udp6_lib_rcv(struct sk_buff *, struct udp_table *, int);
int __udp6_lib_err(struct sk_buff *, struct inet6_skb_parm *, u8, u8, int,
		   __be32, struct udp_table *);

int udpv6_init_sock(struct sock *sk);
int udp_v6_get_port(struct sock *sk, unsigned short snum);
void udp_v6_rehash(struct sock *sk);

int udpv6_getsockopt(struct sock *sk, int level, int optname,
		     char __user *optval, int __user *optlen);
int udpv6_setsockopt(struct sock *sk, int level, int optname, sockptr_t optval,
		     unsigned int optlen);
int udpv6_sendmsg(struct sock *sk, struct msghdr *msg, size_t len);
int udpv6_recvmsg(struct sock *sk, struct msghdr *msg, size_t len, int flags,
		  int *addr_len);
void udpv6_destroy_sock(struct sock *sk);

#ifdef CONFIG_PROC_FS
int udp6_seq_show(struct seq_file *seq, void *v);
#endif
#endif	/* _UDP6_IMPL_H */
