#ifndef _NET_RAWV6_H
#define _NET_RAWV6_H

#ifdef __KERNEL__

#define RAWV6_HTABLE_SIZE	MAX_INET_PROTOS
extern struct hlist_head raw_v6_htable[RAWV6_HTABLE_SIZE];
extern rwlock_t raw_v6_lock;

extern int ipv6_raw_deliver(struct sk_buff *skb, int nexthdr);

extern struct sock *__raw_v6_lookup(struct sock *sk, unsigned short num,
				    struct in6_addr *loc_addr, struct in6_addr *rmt_addr,
				    int dif);

extern int			rawv6_rcv(struct sock *sk,
					  struct sk_buff *skb);


extern void			rawv6_err(struct sock *sk,
					  struct sk_buff *skb,
					  struct inet6_skb_parm *opt,
					  int type, int code, 
					  int offset, __be32 info);

#endif

#endif
