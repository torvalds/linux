#ifndef _NF_TPROXY_H_
#define _NF_TPROXY_H_

#include <net/tcp.h>

enum nf_tproxy_lookup_t {
	 NF_TPROXY_LOOKUP_LISTENER,
	 NF_TPROXY_LOOKUP_ESTABLISHED,
};

static inline bool nf_tproxy_sk_is_transparent(struct sock *sk)
{
	if (inet_sk_transparent(sk))
		return true;

	sock_gen_put(sk);
	return false;
}

static inline void nf_tproxy_twsk_deschedule_put(struct inet_timewait_sock *tw)
{
	local_bh_disable();
	inet_twsk_deschedule_put(tw);
	local_bh_enable();
}

/* assign a socket to the skb -- consumes sk */
static inline void nf_tproxy_assign_sock(struct sk_buff *skb, struct sock *sk)
{
	skb_orphan(skb);
	skb->sk = sk;
	skb->destructor = sock_edemux;
}

__be32 nf_tproxy_laddr4(struct sk_buff *skb, __be32 user_laddr, __be32 daddr);

/**
 * nf_tproxy_handle_time_wait4 - handle IPv4 TCP TIME_WAIT reopen redirections
 * @net:	The network namespace.
 * @skb:	The skb being processed.
 * @laddr:	IPv4 address to redirect to or zero.
 * @lport:	TCP port to redirect to or zero.
 * @sk:		The TIME_WAIT TCP socket found by the lookup.
 *
 * We have to handle SYN packets arriving to TIME_WAIT sockets
 * differently: instead of reopening the connection we should rather
 * redirect the new connection to the proxy if there's a listener
 * socket present.
 *
 * nf_tproxy_handle_time_wait4() consumes the socket reference passed in.
 *
 * Returns the listener socket if there's one, the TIME_WAIT socket if
 * no such listener is found, or NULL if the TCP header is incomplete.
 */
struct sock *
nf_tproxy_handle_time_wait4(struct net *net, struct sk_buff *skb,
			    __be32 laddr, __be16 lport, struct sock *sk);

/*
 * This is used when the user wants to intercept a connection matching
 * an explicit iptables rule. In this case the sockets are assumed
 * matching in preference order:
 *
 *   - match: if there's a fully established connection matching the
 *     _packet_ tuple, it is returned, assuming the redirection
 *     already took place and we process a packet belonging to an
 *     established connection
 *
 *   - match: if there's a listening socket matching the redirection
 *     (e.g. on-port & on-ip of the connection), it is returned,
 *     regardless if it was bound to 0.0.0.0 or an explicit
 *     address. The reasoning is that if there's an explicit rule, it
 *     does not really matter if the listener is bound to an interface
 *     or to 0. The user already stated that he wants redirection
 *     (since he added the rule).
 *
 * Please note that there's an overlap between what a TPROXY target
 * and a socket match will match. Normally if you have both rules the
 * "socket" match will be the first one, effectively all packets
 * belonging to established connections going through that one.
 */
struct sock *
nf_tproxy_get_sock_v4(struct net *net, struct sk_buff *skb,
		      const u8 protocol,
		      const __be32 saddr, const __be32 daddr,
		      const __be16 sport, const __be16 dport,
		      const struct net_device *in,
		      const enum nf_tproxy_lookup_t lookup_type);

const struct in6_addr *
nf_tproxy_laddr6(struct sk_buff *skb, const struct in6_addr *user_laddr,
		 const struct in6_addr *daddr);

/**
 * nf_tproxy_handle_time_wait6 - handle IPv6 TCP TIME_WAIT reopen redirections
 * @skb:	The skb being processed.
 * @tproto:	Transport protocol.
 * @thoff:	Transport protocol header offset.
 * @net:	Network namespace.
 * @laddr:	IPv6 address to redirect to.
 * @lport:	TCP port to redirect to or zero.
 * @sk:		The TIME_WAIT TCP socket found by the lookup.
 *
 * We have to handle SYN packets arriving to TIME_WAIT sockets
 * differently: instead of reopening the connection we should rather
 * redirect the new connection to the proxy if there's a listener
 * socket present.
 *
 * nf_tproxy_handle_time_wait6() consumes the socket reference passed in.
 *
 * Returns the listener socket if there's one, the TIME_WAIT socket if
 * no such listener is found, or NULL if the TCP header is incomplete.
 */
struct sock *
nf_tproxy_handle_time_wait6(struct sk_buff *skb, int tproto, int thoff,
			    struct net *net,
			    const struct in6_addr *laddr,
			    const __be16 lport,
			    struct sock *sk);

struct sock *
nf_tproxy_get_sock_v6(struct net *net, struct sk_buff *skb, int thoff,
		      const u8 protocol,
		      const struct in6_addr *saddr, const struct in6_addr *daddr,
		      const __be16 sport, const __be16 dport,
		      const struct net_device *in,
		      const enum nf_tproxy_lookup_t lookup_type);

#endif /* _NF_TPROXY_H_ */
