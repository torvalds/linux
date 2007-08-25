#ifndef _UDP4_IMPL_H
#define _UDP4_IMPL_H
#include <net/udp.h>
#include <net/udplite.h>
#include <net/protocol.h>
#include <net/inet_common.h>

extern int  	__udp4_lib_rcv(struct sk_buff *, struct hlist_head [], int );
extern void 	__udp4_lib_err(struct sk_buff *, u32, struct hlist_head []);

extern int	__udp_lib_get_port(struct sock *sk, unsigned short snum,
				   struct hlist_head udptable[],
				   int (*)(const struct sock*,const struct sock*));
extern int	ipv4_rcv_saddr_equal(const struct sock *, const struct sock *);


extern int	udp_setsockopt(struct sock *sk, int level, int optname,
			       char __user *optval, int optlen);
extern int	udp_getsockopt(struct sock *sk, int level, int optname,
			       char __user *optval, int __user *optlen);

#ifdef CONFIG_COMPAT
extern int	compat_udp_setsockopt(struct sock *sk, int level, int optname,
				      char __user *optval, int optlen);
extern int	compat_udp_getsockopt(struct sock *sk, int level, int optname,
				      char __user *optval, int __user *optlen);
#endif
extern int	udp_recvmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
			    size_t len, int noblock, int flags, int *addr_len);
extern int	udp_sendpage(struct sock *sk, struct page *page, int offset,
			     size_t size, int flags);
extern int	udp_queue_rcv_skb(struct sock * sk, struct sk_buff *skb);
extern int	udp_destroy_sock(struct sock *sk);

#ifdef CONFIG_PROC_FS
extern int	udp4_seq_show(struct seq_file *seq, void *v);
#endif
#endif	/* _UDP4_IMPL_H */
