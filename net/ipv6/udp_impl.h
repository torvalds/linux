#ifndef _UDP6_IMPL_H
#define _UDP6_IMPL_H
#include <net/udp.h>
#include <net/udplite.h>
#include <net/protocol.h>
#include <net/addrconf.h>
#include <net/inet_common.h>
#include <net/transp_v6.h>

extern int  	__udp6_lib_rcv(struct sk_buff *, struct hlist_head [], int );
extern void 	__udp6_lib_err(struct sk_buff *, struct inet6_skb_parm *,
			       int , int , int , __be32 , struct hlist_head []);

extern int	udp_v6_get_port(struct sock *sk, unsigned short snum);

extern int	udpv6_getsockopt(struct sock *sk, int level, int optname,
				 char __user *optval, int __user *optlen);
extern int	udpv6_setsockopt(struct sock *sk, int level, int optname,
				 char __user *optval, int optlen);
#ifdef CONFIG_COMPAT
extern int	compat_udpv6_setsockopt(struct sock *sk, int level, int optname,
					char __user *optval, int optlen);
extern int	compat_udpv6_getsockopt(struct sock *sk, int level, int optname,
				       char __user *optval, int __user *optlen);
#endif
extern int	udpv6_sendmsg(struct kiocb *iocb, struct sock *sk,
			      struct msghdr *msg, size_t len);
extern int	udpv6_recvmsg(struct kiocb *iocb, struct sock *sk,
			      struct msghdr *msg, size_t len,
			      int noblock, int flags, int *addr_len);
extern int	udpv6_queue_rcv_skb(struct sock * sk, struct sk_buff *skb);
extern void	udpv6_destroy_sock(struct sock *sk);

#ifdef CONFIG_PROC_FS
extern int	udp6_seq_show(struct seq_file *seq, void *v);
#endif
#endif	/* _UDP6_IMPL_H */
