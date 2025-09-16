/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _UDP4_IMPL_H
#define _UDP4_IMPL_H
#include <net/aligned_data.h>
#include <net/udp.h>
#include <net/udplite.h>
#include <net/protocol.h>
#include <net/inet_common.h>

int __udp4_lib_rcv(struct sk_buff *, struct udp_table *, int);
int __udp4_lib_err(struct sk_buff *, u32, struct udp_table *);

int udp_v4_get_port(struct sock *sk, unsigned short snum);
void udp_v4_rehash(struct sock *sk);

int udp_setsockopt(struct sock *sk, int level, int optname, sockptr_t optval,
		   unsigned int optlen);
int udp_getsockopt(struct sock *sk, int level, int optname,
		   char __user *optval, int __user *optlen);

int udp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len, int flags,
		int *addr_len);
void udp_destroy_sock(struct sock *sk);

#ifdef CONFIG_PROC_FS
int udp4_seq_show(struct seq_file *seq, void *v);
#endif
#endif	/* _UDP4_IMPL_H */
