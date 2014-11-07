/*
 * ip_vs_proto_tcp.c:	TCP load balancing support for IPVS
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *              Julian Anastasov <ja@ssi.bg>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:     Hans Schillstrom <hans.schillstrom@ericsson.com>
 *
 *              Network name space (netns) aware.
 *              Global data moved to netns i.e struct netns_ipvs
 *              tcp_timeouts table has copy per netns in a hash table per
 *              protocol ip_vs_proto_data and is handled by netns
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/tcp.h>                  /* for tcphdr */
#include <net/ip.h>
#include <net/tcp.h>                    /* for csum_tcpudp_magic */
#include <net/ip6_checksum.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <net/ip_vs.h>

static int
tcp_conn_schedule(int af, struct sk_buff *skb, struct ip_vs_proto_data *pd,
		  int *verdict, struct ip_vs_conn **cpp,
		  struct ip_vs_iphdr *iph)
{
	struct net *net;
	struct ip_vs_service *svc;
	struct tcphdr _tcph, *th;
	struct netns_ipvs *ipvs;

	th = skb_header_pointer(skb, iph->len, sizeof(_tcph), &_tcph);
	if (th == NULL) {
		*verdict = NF_DROP;
		return 0;
	}
	net = skb_net(skb);
	ipvs = net_ipvs(net);
	/* No !th->ack check to allow scheduling on SYN+ACK for Active FTP */
	rcu_read_lock();
	if ((th->syn || sysctl_sloppy_tcp(ipvs)) && !th->rst &&
	    (svc = ip_vs_service_find(net, af, skb->mark, iph->protocol,
				      &iph->daddr, th->dest))) {
		int ignored;

		if (ip_vs_todrop(ipvs)) {
			/*
			 * It seems that we are very loaded.
			 * We have to drop this packet :(
			 */
			rcu_read_unlock();
			*verdict = NF_DROP;
			return 0;
		}

		/*
		 * Let the virtual server select a real server for the
		 * incoming connection, and create a connection entry.
		 */
		*cpp = ip_vs_schedule(svc, skb, pd, &ignored, iph);
		if (!*cpp && ignored <= 0) {
			if (!ignored)
				*verdict = ip_vs_leave(svc, skb, pd, iph);
			else
				*verdict = NF_DROP;
			rcu_read_unlock();
			return 0;
		}
	}
	rcu_read_unlock();
	/* NF_ACCEPT */
	return 1;
}


static inline void
tcp_fast_csum_update(int af, struct tcphdr *tcph,
		     const union nf_inet_addr *oldip,
		     const union nf_inet_addr *newip,
		     __be16 oldport, __be16 newport)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		tcph->check =
			csum_fold(ip_vs_check_diff16(oldip->ip6, newip->ip6,
					 ip_vs_check_diff2(oldport, newport,
						~csum_unfold(tcph->check))));
	else
#endif
	tcph->check =
		csum_fold(ip_vs_check_diff4(oldip->ip, newip->ip,
				 ip_vs_check_diff2(oldport, newport,
						~csum_unfold(tcph->check))));
}


static inline void
tcp_partial_csum_update(int af, struct tcphdr *tcph,
		     const union nf_inet_addr *oldip,
		     const union nf_inet_addr *newip,
		     __be16 oldlen, __be16 newlen)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		tcph->check =
			~csum_fold(ip_vs_check_diff16(oldip->ip6, newip->ip6,
					 ip_vs_check_diff2(oldlen, newlen,
						csum_unfold(tcph->check))));
	else
#endif
	tcph->check =
		~csum_fold(ip_vs_check_diff4(oldip->ip, newip->ip,
				ip_vs_check_diff2(oldlen, newlen,
						csum_unfold(tcph->check))));
}


static int
tcp_snat_handler(struct sk_buff *skb, struct ip_vs_protocol *pp,
		 struct ip_vs_conn *cp, struct ip_vs_iphdr *iph)
{
	struct tcphdr *tcph;
	unsigned int tcphoff = iph->len;
	int oldlen;
	int payload_csum = 0;

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6 && iph->fragoffs)
		return 1;
#endif
	oldlen = skb->len - tcphoff;

	/* csum_check requires unshared skb */
	if (!skb_make_writable(skb, tcphoff+sizeof(*tcph)))
		return 0;

	if (unlikely(cp->app != NULL)) {
		int ret;

		/* Some checks before mangling */
		if (pp->csum_check && !pp->csum_check(cp->af, skb, pp))
			return 0;

		/* Call application helper if needed */
		if (!(ret = ip_vs_app_pkt_out(cp, skb)))
			return 0;
		/* ret=2: csum update is needed after payload mangling */
		if (ret == 1)
			oldlen = skb->len - tcphoff;
		else
			payload_csum = 1;
	}

	tcph = (void *)skb_network_header(skb) + tcphoff;
	tcph->source = cp->vport;

	/* Adjust TCP checksums */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		tcp_partial_csum_update(cp->af, tcph, &cp->daddr, &cp->vaddr,
					htons(oldlen),
					htons(skb->len - tcphoff));
	} else if (!payload_csum) {
		/* Only port and addr are changed, do fast csum update */
		tcp_fast_csum_update(cp->af, tcph, &cp->daddr, &cp->vaddr,
				     cp->dport, cp->vport);
		if (skb->ip_summed == CHECKSUM_COMPLETE)
			skb->ip_summed = (cp->app && pp->csum_check) ?
					 CHECKSUM_UNNECESSARY : CHECKSUM_NONE;
	} else {
		/* full checksum calculation */
		tcph->check = 0;
		skb->csum = skb_checksum(skb, tcphoff, skb->len - tcphoff, 0);
#ifdef CONFIG_IP_VS_IPV6
		if (cp->af == AF_INET6)
			tcph->check = csum_ipv6_magic(&cp->vaddr.in6,
						      &cp->caddr.in6,
						      skb->len - tcphoff,
						      cp->protocol, skb->csum);
		else
#endif
			tcph->check = csum_tcpudp_magic(cp->vaddr.ip,
							cp->caddr.ip,
							skb->len - tcphoff,
							cp->protocol,
							skb->csum);
		skb->ip_summed = CHECKSUM_UNNECESSARY;

		IP_VS_DBG(11, "O-pkt: %s O-csum=%d (+%zd)\n",
			  pp->name, tcph->check,
			  (char*)&(tcph->check) - (char*)tcph);
	}
	return 1;
}


static int
tcp_dnat_handler(struct sk_buff *skb, struct ip_vs_protocol *pp,
		 struct ip_vs_conn *cp, struct ip_vs_iphdr *iph)
{
	struct tcphdr *tcph;
	unsigned int tcphoff = iph->len;
	int oldlen;
	int payload_csum = 0;

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6 && iph->fragoffs)
		return 1;
#endif
	oldlen = skb->len - tcphoff;

	/* csum_check requires unshared skb */
	if (!skb_make_writable(skb, tcphoff+sizeof(*tcph)))
		return 0;

	if (unlikely(cp->app != NULL)) {
		int ret;

		/* Some checks before mangling */
		if (pp->csum_check && !pp->csum_check(cp->af, skb, pp))
			return 0;

		/*
		 *	Attempt ip_vs_app call.
		 *	It will fix ip_vs_conn and iph ack_seq stuff
		 */
		if (!(ret = ip_vs_app_pkt_in(cp, skb)))
			return 0;
		/* ret=2: csum update is needed after payload mangling */
		if (ret == 1)
			oldlen = skb->len - tcphoff;
		else
			payload_csum = 1;
	}

	tcph = (void *)skb_network_header(skb) + tcphoff;
	tcph->dest = cp->dport;

	/*
	 *	Adjust TCP checksums
	 */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		tcp_partial_csum_update(cp->af, tcph, &cp->vaddr, &cp->daddr,
					htons(oldlen),
					htons(skb->len - tcphoff));
	} else if (!payload_csum) {
		/* Only port and addr are changed, do fast csum update */
		tcp_fast_csum_update(cp->af, tcph, &cp->vaddr, &cp->daddr,
				     cp->vport, cp->dport);
		if (skb->ip_summed == CHECKSUM_COMPLETE)
			skb->ip_summed = (cp->app && pp->csum_check) ?
					 CHECKSUM_UNNECESSARY : CHECKSUM_NONE;
	} else {
		/* full checksum calculation */
		tcph->check = 0;
		skb->csum = skb_checksum(skb, tcphoff, skb->len - tcphoff, 0);
#ifdef CONFIG_IP_VS_IPV6
		if (cp->af == AF_INET6)
			tcph->check = csum_ipv6_magic(&cp->caddr.in6,
						      &cp->daddr.in6,
						      skb->len - tcphoff,
						      cp->protocol, skb->csum);
		else
#endif
			tcph->check = csum_tcpudp_magic(cp->caddr.ip,
							cp->daddr.ip,
							skb->len - tcphoff,
							cp->protocol,
							skb->csum);
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
	return 1;
}


static int
tcp_csum_check(int af, struct sk_buff *skb, struct ip_vs_protocol *pp)
{
	unsigned int tcphoff;

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		tcphoff = sizeof(struct ipv6hdr);
	else
#endif
		tcphoff = ip_hdrlen(skb);

	switch (skb->ip_summed) {
	case CHECKSUM_NONE:
		skb->csum = skb_checksum(skb, tcphoff, skb->len - tcphoff, 0);
	case CHECKSUM_COMPLETE:
#ifdef CONFIG_IP_VS_IPV6
		if (af == AF_INET6) {
			if (csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
					    &ipv6_hdr(skb)->daddr,
					    skb->len - tcphoff,
					    ipv6_hdr(skb)->nexthdr,
					    skb->csum)) {
				IP_VS_DBG_RL_PKT(0, af, pp, skb, 0,
						 "Failed checksum for");
				return 0;
			}
		} else
#endif
			if (csum_tcpudp_magic(ip_hdr(skb)->saddr,
					      ip_hdr(skb)->daddr,
					      skb->len - tcphoff,
					      ip_hdr(skb)->protocol,
					      skb->csum)) {
				IP_VS_DBG_RL_PKT(0, af, pp, skb, 0,
						 "Failed checksum for");
				return 0;
			}
		break;
	default:
		/* No need to checksum. */
		break;
	}

	return 1;
}


#define TCP_DIR_INPUT		0
#define TCP_DIR_OUTPUT		4
#define TCP_DIR_INPUT_ONLY	8

static const int tcp_state_off[IP_VS_DIR_LAST] = {
	[IP_VS_DIR_INPUT]		=	TCP_DIR_INPUT,
	[IP_VS_DIR_OUTPUT]		=	TCP_DIR_OUTPUT,
	[IP_VS_DIR_INPUT_ONLY]		=	TCP_DIR_INPUT_ONLY,
};

/*
 *	Timeout table[state]
 */
static const int tcp_timeouts[IP_VS_TCP_S_LAST+1] = {
	[IP_VS_TCP_S_NONE]		=	2*HZ,
	[IP_VS_TCP_S_ESTABLISHED]	=	15*60*HZ,
	[IP_VS_TCP_S_SYN_SENT]		=	2*60*HZ,
	[IP_VS_TCP_S_SYN_RECV]		=	1*60*HZ,
	[IP_VS_TCP_S_FIN_WAIT]		=	2*60*HZ,
	[IP_VS_TCP_S_TIME_WAIT]		=	2*60*HZ,
	[IP_VS_TCP_S_CLOSE]		=	10*HZ,
	[IP_VS_TCP_S_CLOSE_WAIT]	=	60*HZ,
	[IP_VS_TCP_S_LAST_ACK]		=	30*HZ,
	[IP_VS_TCP_S_LISTEN]		=	2*60*HZ,
	[IP_VS_TCP_S_SYNACK]		=	120*HZ,
	[IP_VS_TCP_S_LAST]		=	2*HZ,
};

static const char *const tcp_state_name_table[IP_VS_TCP_S_LAST+1] = {
	[IP_VS_TCP_S_NONE]		=	"NONE",
	[IP_VS_TCP_S_ESTABLISHED]	=	"ESTABLISHED",
	[IP_VS_TCP_S_SYN_SENT]		=	"SYN_SENT",
	[IP_VS_TCP_S_SYN_RECV]		=	"SYN_RECV",
	[IP_VS_TCP_S_FIN_WAIT]		=	"FIN_WAIT",
	[IP_VS_TCP_S_TIME_WAIT]		=	"TIME_WAIT",
	[IP_VS_TCP_S_CLOSE]		=	"CLOSE",
	[IP_VS_TCP_S_CLOSE_WAIT]	=	"CLOSE_WAIT",
	[IP_VS_TCP_S_LAST_ACK]		=	"LAST_ACK",
	[IP_VS_TCP_S_LISTEN]		=	"LISTEN",
	[IP_VS_TCP_S_SYNACK]		=	"SYNACK",
	[IP_VS_TCP_S_LAST]		=	"BUG!",
};

#define sNO IP_VS_TCP_S_NONE
#define sES IP_VS_TCP_S_ESTABLISHED
#define sSS IP_VS_TCP_S_SYN_SENT
#define sSR IP_VS_TCP_S_SYN_RECV
#define sFW IP_VS_TCP_S_FIN_WAIT
#define sTW IP_VS_TCP_S_TIME_WAIT
#define sCL IP_VS_TCP_S_CLOSE
#define sCW IP_VS_TCP_S_CLOSE_WAIT
#define sLA IP_VS_TCP_S_LAST_ACK
#define sLI IP_VS_TCP_S_LISTEN
#define sSA IP_VS_TCP_S_SYNACK

struct tcp_states_t {
	int next_state[IP_VS_TCP_S_LAST];
};

static const char * tcp_state_name(int state)
{
	if (state >= IP_VS_TCP_S_LAST)
		return "ERR!";
	return tcp_state_name_table[state] ? tcp_state_name_table[state] : "?";
}

static struct tcp_states_t tcp_states [] = {
/*	INPUT */
/*        sNO, sES, sSS, sSR, sFW, sTW, sCL, sCW, sLA, sLI, sSA	*/
/*syn*/ {{sSR, sES, sES, sSR, sSR, sSR, sSR, sSR, sSR, sSR, sSR }},
/*fin*/ {{sCL, sCW, sSS, sTW, sTW, sTW, sCL, sCW, sLA, sLI, sTW }},
/*ack*/ {{sES, sES, sSS, sES, sFW, sTW, sCL, sCW, sCL, sLI, sES }},
/*rst*/ {{sCL, sCL, sCL, sSR, sCL, sCL, sCL, sCL, sLA, sLI, sSR }},

/*	OUTPUT */
/*        sNO, sES, sSS, sSR, sFW, sTW, sCL, sCW, sLA, sLI, sSA	*/
/*syn*/ {{sSS, sES, sSS, sSR, sSS, sSS, sSS, sSS, sSS, sLI, sSR }},
/*fin*/ {{sTW, sFW, sSS, sTW, sFW, sTW, sCL, sTW, sLA, sLI, sTW }},
/*ack*/ {{sES, sES, sSS, sES, sFW, sTW, sCL, sCW, sLA, sES, sES }},
/*rst*/ {{sCL, sCL, sSS, sCL, sCL, sTW, sCL, sCL, sCL, sCL, sCL }},

/*	INPUT-ONLY */
/*        sNO, sES, sSS, sSR, sFW, sTW, sCL, sCW, sLA, sLI, sSA	*/
/*syn*/ {{sSR, sES, sES, sSR, sSR, sSR, sSR, sSR, sSR, sSR, sSR }},
/*fin*/ {{sCL, sFW, sSS, sTW, sFW, sTW, sCL, sCW, sLA, sLI, sTW }},
/*ack*/ {{sES, sES, sSS, sES, sFW, sTW, sCL, sCW, sCL, sLI, sES }},
/*rst*/ {{sCL, sCL, sCL, sSR, sCL, sCL, sCL, sCL, sLA, sLI, sCL }},
};

static struct tcp_states_t tcp_states_dos [] = {
/*	INPUT */
/*        sNO, sES, sSS, sSR, sFW, sTW, sCL, sCW, sLA, sLI, sSA	*/
/*syn*/ {{sSR, sES, sES, sSR, sSR, sSR, sSR, sSR, sSR, sSR, sSA }},
/*fin*/ {{sCL, sCW, sSS, sTW, sTW, sTW, sCL, sCW, sLA, sLI, sSA }},
/*ack*/ {{sES, sES, sSS, sSR, sFW, sTW, sCL, sCW, sCL, sLI, sSA }},
/*rst*/ {{sCL, sCL, sCL, sSR, sCL, sCL, sCL, sCL, sLA, sLI, sCL }},

/*	OUTPUT */
/*        sNO, sES, sSS, sSR, sFW, sTW, sCL, sCW, sLA, sLI, sSA	*/
/*syn*/ {{sSS, sES, sSS, sSA, sSS, sSS, sSS, sSS, sSS, sLI, sSA }},
/*fin*/ {{sTW, sFW, sSS, sTW, sFW, sTW, sCL, sTW, sLA, sLI, sTW }},
/*ack*/ {{sES, sES, sSS, sES, sFW, sTW, sCL, sCW, sLA, sES, sES }},
/*rst*/ {{sCL, sCL, sSS, sCL, sCL, sTW, sCL, sCL, sCL, sCL, sCL }},

/*	INPUT-ONLY */
/*        sNO, sES, sSS, sSR, sFW, sTW, sCL, sCW, sLA, sLI, sSA	*/
/*syn*/ {{sSA, sES, sES, sSR, sSA, sSA, sSA, sSA, sSA, sSA, sSA }},
/*fin*/ {{sCL, sFW, sSS, sTW, sFW, sTW, sCL, sCW, sLA, sLI, sTW }},
/*ack*/ {{sES, sES, sSS, sES, sFW, sTW, sCL, sCW, sCL, sLI, sES }},
/*rst*/ {{sCL, sCL, sCL, sSR, sCL, sCL, sCL, sCL, sLA, sLI, sCL }},
};

static void tcp_timeout_change(struct ip_vs_proto_data *pd, int flags)
{
	int on = (flags & 1);		/* secure_tcp */

	/*
	** FIXME: change secure_tcp to independent sysctl var
	** or make it per-service or per-app because it is valid
	** for most if not for all of the applications. Something
	** like "capabilities" (flags) for each object.
	*/
	pd->tcp_state_table = (on ? tcp_states_dos : tcp_states);
}

static inline int tcp_state_idx(struct tcphdr *th)
{
	if (th->rst)
		return 3;
	if (th->syn)
		return 0;
	if (th->fin)
		return 1;
	if (th->ack)
		return 2;
	return -1;
}

static inline void
set_tcp_state(struct ip_vs_proto_data *pd, struct ip_vs_conn *cp,
	      int direction, struct tcphdr *th)
{
	int state_idx;
	int new_state = IP_VS_TCP_S_CLOSE;
	int state_off = tcp_state_off[direction];

	/*
	 *    Update state offset to INPUT_ONLY if necessary
	 *    or delete NO_OUTPUT flag if output packet detected
	 */
	if (cp->flags & IP_VS_CONN_F_NOOUTPUT) {
		if (state_off == TCP_DIR_OUTPUT)
			cp->flags &= ~IP_VS_CONN_F_NOOUTPUT;
		else
			state_off = TCP_DIR_INPUT_ONLY;
	}

	if ((state_idx = tcp_state_idx(th)) < 0) {
		IP_VS_DBG(8, "tcp_state_idx=%d!!!\n", state_idx);
		goto tcp_state_out;
	}

	new_state =
		pd->tcp_state_table[state_off+state_idx].next_state[cp->state];

  tcp_state_out:
	if (new_state != cp->state) {
		struct ip_vs_dest *dest = cp->dest;

		IP_VS_DBG_BUF(8, "%s %s [%c%c%c%c] %s:%d->"
			      "%s:%d state: %s->%s conn->refcnt:%d\n",
			      pd->pp->name,
			      ((state_off == TCP_DIR_OUTPUT) ?
			       "output " : "input "),
			      th->syn ? 'S' : '.',
			      th->fin ? 'F' : '.',
			      th->ack ? 'A' : '.',
			      th->rst ? 'R' : '.',
			      IP_VS_DBG_ADDR(cp->daf, &cp->daddr),
			      ntohs(cp->dport),
			      IP_VS_DBG_ADDR(cp->af, &cp->caddr),
			      ntohs(cp->cport),
			      tcp_state_name(cp->state),
			      tcp_state_name(new_state),
			      atomic_read(&cp->refcnt));

		if (dest) {
			if (!(cp->flags & IP_VS_CONN_F_INACTIVE) &&
			    (new_state != IP_VS_TCP_S_ESTABLISHED)) {
				atomic_dec(&dest->activeconns);
				atomic_inc(&dest->inactconns);
				cp->flags |= IP_VS_CONN_F_INACTIVE;
			} else if ((cp->flags & IP_VS_CONN_F_INACTIVE) &&
				   (new_state == IP_VS_TCP_S_ESTABLISHED)) {
				atomic_inc(&dest->activeconns);
				atomic_dec(&dest->inactconns);
				cp->flags &= ~IP_VS_CONN_F_INACTIVE;
			}
		}
	}

	if (likely(pd))
		cp->timeout = pd->timeout_table[cp->state = new_state];
	else	/* What to do ? */
		cp->timeout = tcp_timeouts[cp->state = new_state];
}

/*
 *	Handle state transitions
 */
static void
tcp_state_transition(struct ip_vs_conn *cp, int direction,
		     const struct sk_buff *skb,
		     struct ip_vs_proto_data *pd)
{
	struct tcphdr _tcph, *th;

#ifdef CONFIG_IP_VS_IPV6
	int ihl = cp->af == AF_INET ? ip_hdrlen(skb) : sizeof(struct ipv6hdr);
#else
	int ihl = ip_hdrlen(skb);
#endif

	th = skb_header_pointer(skb, ihl, sizeof(_tcph), &_tcph);
	if (th == NULL)
		return;

	spin_lock_bh(&cp->lock);
	set_tcp_state(pd, cp, direction, th);
	spin_unlock_bh(&cp->lock);
}

static inline __u16 tcp_app_hashkey(__be16 port)
{
	return (((__force u16)port >> TCP_APP_TAB_BITS) ^ (__force u16)port)
		& TCP_APP_TAB_MASK;
}


static int tcp_register_app(struct net *net, struct ip_vs_app *inc)
{
	struct ip_vs_app *i;
	__u16 hash;
	__be16 port = inc->port;
	int ret = 0;
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ip_vs_proto_data *pd = ip_vs_proto_data_get(net, IPPROTO_TCP);

	hash = tcp_app_hashkey(port);

	list_for_each_entry(i, &ipvs->tcp_apps[hash], p_list) {
		if (i->port == port) {
			ret = -EEXIST;
			goto out;
		}
	}
	list_add_rcu(&inc->p_list, &ipvs->tcp_apps[hash]);
	atomic_inc(&pd->appcnt);

  out:
	return ret;
}


static void
tcp_unregister_app(struct net *net, struct ip_vs_app *inc)
{
	struct ip_vs_proto_data *pd = ip_vs_proto_data_get(net, IPPROTO_TCP);

	atomic_dec(&pd->appcnt);
	list_del_rcu(&inc->p_list);
}


static int
tcp_app_conn_bind(struct ip_vs_conn *cp)
{
	struct netns_ipvs *ipvs = net_ipvs(ip_vs_conn_net(cp));
	int hash;
	struct ip_vs_app *inc;
	int result = 0;

	/* Default binding: bind app only for NAT */
	if (IP_VS_FWD_METHOD(cp) != IP_VS_CONN_F_MASQ)
		return 0;

	/* Lookup application incarnations and bind the right one */
	hash = tcp_app_hashkey(cp->vport);

	rcu_read_lock();
	list_for_each_entry_rcu(inc, &ipvs->tcp_apps[hash], p_list) {
		if (inc->port == cp->vport) {
			if (unlikely(!ip_vs_app_inc_get(inc)))
				break;
			rcu_read_unlock();

			IP_VS_DBG_BUF(9, "%s(): Binding conn %s:%u->"
				      "%s:%u to app %s on port %u\n",
				      __func__,
				      IP_VS_DBG_ADDR(cp->af, &cp->caddr),
				      ntohs(cp->cport),
				      IP_VS_DBG_ADDR(cp->af, &cp->vaddr),
				      ntohs(cp->vport),
				      inc->name, ntohs(inc->port));

			cp->app = inc;
			if (inc->init_conn)
				result = inc->init_conn(inc, cp);
			goto out;
		}
	}
	rcu_read_unlock();

  out:
	return result;
}


/*
 *	Set LISTEN timeout. (ip_vs_conn_put will setup timer)
 */
void ip_vs_tcp_conn_listen(struct net *net, struct ip_vs_conn *cp)
{
	struct ip_vs_proto_data *pd = ip_vs_proto_data_get(net, IPPROTO_TCP);

	spin_lock_bh(&cp->lock);
	cp->state = IP_VS_TCP_S_LISTEN;
	cp->timeout = (pd ? pd->timeout_table[IP_VS_TCP_S_LISTEN]
			   : tcp_timeouts[IP_VS_TCP_S_LISTEN]);
	spin_unlock_bh(&cp->lock);
}

/* ---------------------------------------------
 *   timeouts is netns related now.
 * ---------------------------------------------
 */
static int __ip_vs_tcp_init(struct net *net, struct ip_vs_proto_data *pd)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	ip_vs_init_hash_table(ipvs->tcp_apps, TCP_APP_TAB_SIZE);
	pd->timeout_table = ip_vs_create_timeout_table((int *)tcp_timeouts,
							sizeof(tcp_timeouts));
	if (!pd->timeout_table)
		return -ENOMEM;
	pd->tcp_state_table =  tcp_states;
	return 0;
}

static void __ip_vs_tcp_exit(struct net *net, struct ip_vs_proto_data *pd)
{
	kfree(pd->timeout_table);
}


struct ip_vs_protocol ip_vs_protocol_tcp = {
	.name =			"TCP",
	.protocol =		IPPROTO_TCP,
	.num_states =		IP_VS_TCP_S_LAST,
	.dont_defrag =		0,
	.init =			NULL,
	.exit =			NULL,
	.init_netns =		__ip_vs_tcp_init,
	.exit_netns =		__ip_vs_tcp_exit,
	.register_app =		tcp_register_app,
	.unregister_app =	tcp_unregister_app,
	.conn_schedule =	tcp_conn_schedule,
	.conn_in_get =		ip_vs_conn_in_get_proto,
	.conn_out_get =		ip_vs_conn_out_get_proto,
	.snat_handler =		tcp_snat_handler,
	.dnat_handler =		tcp_dnat_handler,
	.csum_check =		tcp_csum_check,
	.state_name =		tcp_state_name,
	.state_transition =	tcp_state_transition,
	.app_conn_bind =	tcp_app_conn_bind,
	.debug_packet =		ip_vs_tcpudp_debug_packet,
	.timeout_change =	tcp_timeout_change,
};
