#ifndef _NF_TPROXY_CORE_H
#define _NF_TPROXY_CORE_H

#include <linux/types.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/inet_sock.h>
#include <net/tcp.h>

/* look up and get a reference to a matching socket */
extern struct sock *
nf_tproxy_get_sock_v4(struct net *net, const u8 protocol,
		      const __be32 saddr, const __be32 daddr,
		      const __be16 sport, const __be16 dport,
		      const struct net_device *in, bool listening);

static inline void
nf_tproxy_put_sock(struct sock *sk)
{
	/* TIME_WAIT inet sockets have to be handled differently */
	if ((sk->sk_protocol == IPPROTO_TCP) && (sk->sk_state == TCP_TIME_WAIT))
		inet_twsk_put(inet_twsk(sk));
	else
		sock_put(sk);
}

/* assign a socket to the skb -- consumes sk */
int
nf_tproxy_assign_sock(struct sk_buff *skb, struct sock *sk);

#endif
