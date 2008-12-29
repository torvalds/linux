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
 * Changes:
 *
 */

#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/tcp.h>                  /* for tcphdr */
#include <net/ip.h>
#include <net/tcp.h>                    /* for csum_tcpudp_magic */
#include <net/ip6_checksum.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <net/ip_vs.h>


static struct ip_vs_conn *
tcp_conn_in_get(int af, const struct sk_buff *skb, struct ip_vs_protocol *pp,
		const struct ip_vs_iphdr *iph, unsigned int proto_off,
		int inverse)
{
	__be16 _ports[2], *pptr;

	pptr = skb_header_pointer(skb, proto_off, sizeof(_ports), _ports);
	if (pptr == NULL)
		return NULL;

	if (likely(!inverse)) {
		return ip_vs_conn_in_get(af, iph->protocol,
					 &iph->saddr, pptr[0],
					 &iph->daddr, pptr[1]);
	} else {
		return ip_vs_conn_in_get(af, iph->protocol,
					 &iph->daddr, pptr[1],
					 &iph->saddr, pptr[0]);
	}
}

static struct ip_vs_conn *
tcp_conn_out_get(int af, const struct sk_buff *skb, struct ip_vs_protocol *pp,
		 const struct ip_vs_iphdr *iph, unsigned int proto_off,
		 int inverse)
{
	__be16 _ports[2], *pptr;

	pptr = skb_header_pointer(skb, proto_off, sizeof(_ports), _ports);
	if (pptr == NULL)
		return NULL;

	if (likely(!inverse)) {
		return ip_vs_conn_out_get(af, iph->protocol,
					  &iph->saddr, pptr[0],
					  &iph->daddr, pptr[1]);
	} else {
		return ip_vs_conn_out_get(af, iph->protocol,
					  &iph->daddr, pptr[1],
					  &iph->saddr, pptr[0]);
	}
}


static int
tcp_conn_schedule(int af, struct sk_buff *skb, struct ip_vs_protocol *pp,
		  int *verdict, struct ip_vs_conn **cpp)
{
	struct ip_vs_service *svc;
	struct tcphdr _tcph, *th;
	struct ip_vs_iphdr iph;

	ip_vs_fill_iphdr(af, skb_network_header(skb), &iph);

	th = skb_header_pointer(skb, iph.len, sizeof(_tcph), &_tcph);
	if (th == NULL) {
		*verdict = NF_DROP;
		return 0;
	}

	if (th->syn &&
	    (svc = ip_vs_service_get(af, skb->mark, iph.protocol, &iph.daddr,
				     th->dest))) {
		if (ip_vs_todrop()) {
			/*
			 * It seems that we are very loaded.
			 * We have to drop this packet :(
			 */
			ip_vs_service_put(svc);
			*verdict = NF_DROP;
			return 0;
		}

		/*
		 * Let the virtual server select a real server for the
		 * incoming connection, and create a connection entry.
		 */
		*cpp = ip_vs_schedule(svc, skb);
		if (!*cpp) {
			*verdict = ip_vs_leave(svc, skb, pp);
			return 0;
		}
		ip_vs_service_put(svc);
	}
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
			csum_fold(ip_vs_check_diff16(oldip->ip6, newip->ip6,
					 ip_vs_check_diff2(oldlen, newlen,
						~csum_unfold(tcph->check))));
	else
#endif
	tcph->check =
		csum_fold(ip_vs_check_diff4(oldip->ip, newip->ip,
				ip_vs_check_diff2(oldlen, newlen,
						~csum_unfold(tcph->check))));
}


static int
tcp_snat_handler(struct sk_buff *skb,
		 struct ip_vs_protocol *pp, struct ip_vs_conn *cp)
{
	struct tcphdr *tcph;
	unsigned int tcphoff;
	int oldlen;

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6)
		tcphoff = sizeof(struct ipv6hdr);
	else
#endif
		tcphoff = ip_hdrlen(skb);
	oldlen = skb->len - tcphoff;

	/* csum_check requires unshared skb */
	if (!skb_make_writable(skb, tcphoff+sizeof(*tcph)))
		return 0;

	if (unlikely(cp->app != NULL)) {
		/* Some checks before mangling */
		if (pp->csum_check && !pp->csum_check(cp->af, skb, pp))
			return 0;

		/* Call application helper if needed */
		if (!ip_vs_app_pkt_out(cp, skb))
			return 0;
	}

	tcph = (void *)skb_network_header(skb) + tcphoff;
	tcph->source = cp->vport;

	/* Adjust TCP checksums */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		tcp_partial_csum_update(cp->af, tcph, &cp->daddr, &cp->vaddr,
					htons(oldlen),
					htons(skb->len - tcphoff));
	} else if (!cp->app) {
		/* Only port and addr are changed, do fast csum update */
		tcp_fast_csum_update(cp->af, tcph, &cp->daddr, &cp->vaddr,
				     cp->dport, cp->vport);
		if (skb->ip_summed == CHECKSUM_COMPLETE)
			skb->ip_summed = CHECKSUM_NONE;
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

		IP_VS_DBG(11, "O-pkt: %s O-csum=%d (+%zd)\n",
			  pp->name, tcph->check,
			  (char*)&(tcph->check) - (char*)tcph);
	}
	return 1;
}


static int
tcp_dnat_handler(struct sk_buff *skb,
		 struct ip_vs_protocol *pp, struct ip_vs_conn *cp)
{
	struct tcphdr *tcph;
	unsigned int tcphoff;
	int oldlen;

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6)
		tcphoff = sizeof(struct ipv6hdr);
	else
#endif
		tcphoff = ip_hdrlen(skb);
	oldlen = skb->len - tcphoff;

	/* csum_check requires unshared skb */
	if (!skb_make_writable(skb, tcphoff+sizeof(*tcph)))
		return 0;

	if (unlikely(cp->app != NULL)) {
		/* Some checks before mangling */
		if (pp->csum_check && !pp->csum_check(cp->af, skb, pp))
			return 0;

		/*
		 *	Attempt ip_vs_app call.
		 *	It will fix ip_vs_conn and iph ack_seq stuff
		 */
		if (!ip_vs_app_pkt_in(cp, skb))
			return 0;
	}

	tcph = (void *)skb_network_header(skb) + tcphoff;
	tcph->dest = cp->dport;

	/*
	 *	Adjust TCP checksums
	 */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		tcp_partial_csum_update(cp->af, tcph, &cp->daddr, &cp->vaddr,
					htons(oldlen),
					htons(skb->len - tcphoff));
	} else if (!cp->app) {
		/* Only port and addr are changed, do fast csum update */
		tcp_fast_csum_update(cp->af, tcph, &cp->vaddr, &cp->daddr,
				     cp->vport, cp->dport);
		if (skb->ip_summed == CHECKSUM_COMPLETE)
			skb->ip_summed = CHECKSUM_NONE;
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
				IP_VS_DBG_RL_PKT(0, pp, skb, 0,
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
				IP_VS_DBG_RL_PKT(0, pp, skb, 0,
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
static int tcp_timeouts[IP_VS_TCP_S_LAST+1] = {
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

static char * tcp_state_name_table[IP_VS_TCP_S_LAST+1] = {
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
/*ack*/ {{sCL, sES, sSS, sES, sFW, sTW, sCL, sCW, sCL, sLI, sES }},
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
/*ack*/ {{sCL, sES, sSS, sES, sFW, sTW, sCL, sCW, sCL, sLI, sES }},
/*rst*/ {{sCL, sCL, sCL, sSR, sCL, sCL, sCL, sCL, sLA, sLI, sCL }},
};

static struct tcp_states_t tcp_states_dos [] = {
/*	INPUT */
/*        sNO, sES, sSS, sSR, sFW, sTW, sCL, sCW, sLA, sLI, sSA	*/
/*syn*/ {{sSR, sES, sES, sSR, sSR, sSR, sSR, sSR, sSR, sSR, sSA }},
/*fin*/ {{sCL, sCW, sSS, sTW, sTW, sTW, sCL, sCW, sLA, sLI, sSA }},
/*ack*/ {{sCL, sES, sSS, sSR, sFW, sTW, sCL, sCW, sCL, sLI, sSA }},
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
/*ack*/ {{sCL, sES, sSS, sES, sFW, sTW, sCL, sCW, sCL, sLI, sES }},
/*rst*/ {{sCL, sCL, sCL, sSR, sCL, sCL, sCL, sCL, sLA, sLI, sCL }},
};

static struct tcp_states_t *tcp_state_table = tcp_states;


static void tcp_timeout_change(struct ip_vs_protocol *pp, int flags)
{
	int on = (flags & 1);		/* secure_tcp */

	/*
	** FIXME: change secure_tcp to independent sysctl var
	** or make it per-service or per-app because it is valid
	** for most if not for all of the applications. Something
	** like "capabilities" (flags) for each object.
	*/
	tcp_state_table = (on? tcp_states_dos : tcp_states);
}

static int
tcp_set_state_timeout(struct ip_vs_protocol *pp, char *sname, int to)
{
	return ip_vs_set_state_timeout(pp->timeout_table, IP_VS_TCP_S_LAST,
				       tcp_state_name_table, sname, to);
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
set_tcp_state(struct ip_vs_protocol *pp, struct ip_vs_conn *cp,
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

	new_state = tcp_state_table[state_off+state_idx].next_state[cp->state];

  tcp_state_out:
	if (new_state != cp->state) {
		struct ip_vs_dest *dest = cp->dest;

		IP_VS_DBG_BUF(8, "%s %s [%c%c%c%c] %s:%d->"
			      "%s:%d state: %s->%s conn->refcnt:%d\n",
			      pp->name,
			      ((state_off == TCP_DIR_OUTPUT) ?
			       "output " : "input "),
			      th->syn ? 'S' : '.',
			      th->fin ? 'F' : '.',
			      th->ack ? 'A' : '.',
			      th->rst ? 'R' : '.',
			      IP_VS_DBG_ADDR(cp->af, &cp->daddr),
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

	cp->timeout = pp->timeout_table[cp->state = new_state];
}


/*
 *	Handle state transitions
 */
static int
tcp_state_transition(struct ip_vs_conn *cp, int direction,
		     const struct sk_buff *skb,
		     struct ip_vs_protocol *pp)
{
	struct tcphdr _tcph, *th;

#ifdef CONFIG_IP_VS_IPV6
	int ihl = cp->af == AF_INET ? ip_hdrlen(skb) : sizeof(struct ipv6hdr);
#else
	int ihl = ip_hdrlen(skb);
#endif

	th = skb_header_pointer(skb, ihl, sizeof(_tcph), &_tcph);
	if (th == NULL)
		return 0;

	spin_lock(&cp->lock);
	set_tcp_state(pp, cp, direction, th);
	spin_unlock(&cp->lock);

	return 1;
}


/*
 *	Hash table for TCP application incarnations
 */
#define	TCP_APP_TAB_BITS	4
#define	TCP_APP_TAB_SIZE	(1 << TCP_APP_TAB_BITS)
#define	TCP_APP_TAB_MASK	(TCP_APP_TAB_SIZE - 1)

static struct list_head tcp_apps[TCP_APP_TAB_SIZE];
static DEFINE_SPINLOCK(tcp_app_lock);

static inline __u16 tcp_app_hashkey(__be16 port)
{
	return (((__force u16)port >> TCP_APP_TAB_BITS) ^ (__force u16)port)
		& TCP_APP_TAB_MASK;
}


static int tcp_register_app(struct ip_vs_app *inc)
{
	struct ip_vs_app *i;
	__u16 hash;
	__be16 port = inc->port;
	int ret = 0;

	hash = tcp_app_hashkey(port);

	spin_lock_bh(&tcp_app_lock);
	list_for_each_entry(i, &tcp_apps[hash], p_list) {
		if (i->port == port) {
			ret = -EEXIST;
			goto out;
		}
	}
	list_add(&inc->p_list, &tcp_apps[hash]);
	atomic_inc(&ip_vs_protocol_tcp.appcnt);

  out:
	spin_unlock_bh(&tcp_app_lock);
	return ret;
}


static void
tcp_unregister_app(struct ip_vs_app *inc)
{
	spin_lock_bh(&tcp_app_lock);
	atomic_dec(&ip_vs_protocol_tcp.appcnt);
	list_del(&inc->p_list);
	spin_unlock_bh(&tcp_app_lock);
}


static int
tcp_app_conn_bind(struct ip_vs_conn *cp)
{
	int hash;
	struct ip_vs_app *inc;
	int result = 0;

	/* Default binding: bind app only for NAT */
	if (IP_VS_FWD_METHOD(cp) != IP_VS_CONN_F_MASQ)
		return 0;

	/* Lookup application incarnations and bind the right one */
	hash = tcp_app_hashkey(cp->vport);

	spin_lock(&tcp_app_lock);
	list_for_each_entry(inc, &tcp_apps[hash], p_list) {
		if (inc->port == cp->vport) {
			if (unlikely(!ip_vs_app_inc_get(inc)))
				break;
			spin_unlock(&tcp_app_lock);

			IP_VS_DBG_BUF(9, "%s: Binding conn %s:%u->"
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
	spin_unlock(&tcp_app_lock);

  out:
	return result;
}


/*
 *	Set LISTEN timeout. (ip_vs_conn_put will setup timer)
 */
void ip_vs_tcp_conn_listen(struct ip_vs_conn *cp)
{
	spin_lock(&cp->lock);
	cp->state = IP_VS_TCP_S_LISTEN;
	cp->timeout = ip_vs_protocol_tcp.timeout_table[IP_VS_TCP_S_LISTEN];
	spin_unlock(&cp->lock);
}


static void ip_vs_tcp_init(struct ip_vs_protocol *pp)
{
	IP_VS_INIT_HASH_TABLE(tcp_apps);
	pp->timeout_table = tcp_timeouts;
}


static void ip_vs_tcp_exit(struct ip_vs_protocol *pp)
{
}


struct ip_vs_protocol ip_vs_protocol_tcp = {
	.name =			"TCP",
	.protocol =		IPPROTO_TCP,
	.num_states =		IP_VS_TCP_S_LAST,
	.dont_defrag =		0,
	.appcnt =		ATOMIC_INIT(0),
	.init =			ip_vs_tcp_init,
	.exit =			ip_vs_tcp_exit,
	.register_app =		tcp_register_app,
	.unregister_app =	tcp_unregister_app,
	.conn_schedule =	tcp_conn_schedule,
	.conn_in_get =		tcp_conn_in_get,
	.conn_out_get =		tcp_conn_out_get,
	.snat_handler =		tcp_snat_handler,
	.dnat_handler =		tcp_dnat_handler,
	.csum_check =		tcp_csum_check,
	.state_name =		tcp_state_name,
	.state_transition =	tcp_state_transition,
	.app_conn_bind =	tcp_app_conn_bind,
	.debug_packet =		ip_vs_tcpudp_debug_packet,
	.timeout_change =	tcp_timeout_change,
	.set_state_timeout =	tcp_set_state_timeout,
};
