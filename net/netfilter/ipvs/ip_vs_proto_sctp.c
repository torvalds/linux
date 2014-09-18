#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/sctp.h>
#include <net/ip.h>
#include <net/ip6_checksum.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <net/sctp/checksum.h>
#include <net/ip_vs.h>

static int
sctp_conn_schedule(int af, struct sk_buff *skb, struct ip_vs_proto_data *pd,
		   int *verdict, struct ip_vs_conn **cpp,
		   struct ip_vs_iphdr *iph)
{
	struct net *net;
	struct ip_vs_service *svc;
	struct netns_ipvs *ipvs;
	sctp_chunkhdr_t _schunkh, *sch;
	sctp_sctphdr_t *sh, _sctph;

	sh = skb_header_pointer(skb, iph->len, sizeof(_sctph), &_sctph);
	if (sh == NULL) {
		*verdict = NF_DROP;
		return 0;
	}

	sch = skb_header_pointer(skb, iph->len + sizeof(sctp_sctphdr_t),
				 sizeof(_schunkh), &_schunkh);
	if (sch == NULL) {
		*verdict = NF_DROP;
		return 0;
	}

	net = skb_net(skb);
	ipvs = net_ipvs(net);
	rcu_read_lock();
	if ((sch->type == SCTP_CID_INIT || sysctl_sloppy_sctp(ipvs)) &&
	    (svc = ip_vs_service_find(net, af, skb->mark, iph->protocol,
				      &iph->daddr, sh->dest))) {
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

static void sctp_nat_csum(struct sk_buff *skb, sctp_sctphdr_t *sctph,
			  unsigned int sctphoff)
{
	sctph->checksum = sctp_compute_cksum(skb, sctphoff);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
}

static int
sctp_snat_handler(struct sk_buff *skb, struct ip_vs_protocol *pp,
		  struct ip_vs_conn *cp, struct ip_vs_iphdr *iph)
{
	sctp_sctphdr_t *sctph;
	unsigned int sctphoff = iph->len;
	bool payload_csum = false;

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6 && iph->fragoffs)
		return 1;
#endif

	/* csum_check requires unshared skb */
	if (!skb_make_writable(skb, sctphoff + sizeof(*sctph)))
		return 0;

	if (unlikely(cp->app != NULL)) {
		int ret;

		/* Some checks before mangling */
		if (pp->csum_check && !pp->csum_check(cp->af, skb, pp))
			return 0;

		/* Call application helper if needed */
		ret = ip_vs_app_pkt_out(cp, skb);
		if (ret == 0)
			return 0;
		/* ret=2: csum update is needed after payload mangling */
		if (ret == 2)
			payload_csum = true;
	}

	sctph = (void *) skb_network_header(skb) + sctphoff;

	/* Only update csum if we really have to */
	if (sctph->source != cp->vport || payload_csum ||
	    skb->ip_summed == CHECKSUM_PARTIAL) {
		sctph->source = cp->vport;
		sctp_nat_csum(skb, sctph, sctphoff);
	} else {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	return 1;
}

static int
sctp_dnat_handler(struct sk_buff *skb, struct ip_vs_protocol *pp,
		  struct ip_vs_conn *cp, struct ip_vs_iphdr *iph)
{
	sctp_sctphdr_t *sctph;
	unsigned int sctphoff = iph->len;
	bool payload_csum = false;

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6 && iph->fragoffs)
		return 1;
#endif

	/* csum_check requires unshared skb */
	if (!skb_make_writable(skb, sctphoff + sizeof(*sctph)))
		return 0;

	if (unlikely(cp->app != NULL)) {
		int ret;

		/* Some checks before mangling */
		if (pp->csum_check && !pp->csum_check(cp->af, skb, pp))
			return 0;

		/* Call application helper if needed */
		ret = ip_vs_app_pkt_in(cp, skb);
		if (ret == 0)
			return 0;
		/* ret=2: csum update is needed after payload mangling */
		if (ret == 2)
			payload_csum = true;
	}

	sctph = (void *) skb_network_header(skb) + sctphoff;

	/* Only update csum if we really have to */
	if (sctph->dest != cp->dport || payload_csum ||
	    (skb->ip_summed == CHECKSUM_PARTIAL &&
	     !(skb_dst(skb)->dev->features & NETIF_F_SCTP_CSUM))) {
		sctph->dest = cp->dport;
		sctp_nat_csum(skb, sctph, sctphoff);
	} else if (skb->ip_summed != CHECKSUM_PARTIAL) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	return 1;
}

static int
sctp_csum_check(int af, struct sk_buff *skb, struct ip_vs_protocol *pp)
{
	unsigned int sctphoff;
	struct sctphdr *sh, _sctph;
	__le32 cmp, val;

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		sctphoff = sizeof(struct ipv6hdr);
	else
#endif
		sctphoff = ip_hdrlen(skb);

	sh = skb_header_pointer(skb, sctphoff, sizeof(_sctph), &_sctph);
	if (sh == NULL)
		return 0;

	cmp = sh->checksum;
	val = sctp_compute_cksum(skb, sctphoff);

	if (val != cmp) {
		/* CRC failure, dump it. */
		IP_VS_DBG_RL_PKT(0, af, pp, skb, 0,
				"Failed checksum for");
		return 0;
	}
	return 1;
}

enum ipvs_sctp_event_t {
	IP_VS_SCTP_DATA = 0,		/* DATA, SACK, HEARTBEATs */
	IP_VS_SCTP_INIT,
	IP_VS_SCTP_INIT_ACK,
	IP_VS_SCTP_COOKIE_ECHO,
	IP_VS_SCTP_COOKIE_ACK,
	IP_VS_SCTP_SHUTDOWN,
	IP_VS_SCTP_SHUTDOWN_ACK,
	IP_VS_SCTP_SHUTDOWN_COMPLETE,
	IP_VS_SCTP_ERROR,
	IP_VS_SCTP_ABORT,
	IP_VS_SCTP_EVENT_LAST
};

/* RFC 2960, 3.2 Chunk Field Descriptions */
static __u8 sctp_events[] = {
	[SCTP_CID_DATA]			= IP_VS_SCTP_DATA,
	[SCTP_CID_INIT]			= IP_VS_SCTP_INIT,
	[SCTP_CID_INIT_ACK]		= IP_VS_SCTP_INIT_ACK,
	[SCTP_CID_SACK]			= IP_VS_SCTP_DATA,
	[SCTP_CID_HEARTBEAT]		= IP_VS_SCTP_DATA,
	[SCTP_CID_HEARTBEAT_ACK]	= IP_VS_SCTP_DATA,
	[SCTP_CID_ABORT]		= IP_VS_SCTP_ABORT,
	[SCTP_CID_SHUTDOWN]		= IP_VS_SCTP_SHUTDOWN,
	[SCTP_CID_SHUTDOWN_ACK]		= IP_VS_SCTP_SHUTDOWN_ACK,
	[SCTP_CID_ERROR]		= IP_VS_SCTP_ERROR,
	[SCTP_CID_COOKIE_ECHO]		= IP_VS_SCTP_COOKIE_ECHO,
	[SCTP_CID_COOKIE_ACK]		= IP_VS_SCTP_COOKIE_ACK,
	[SCTP_CID_ECN_ECNE]		= IP_VS_SCTP_DATA,
	[SCTP_CID_ECN_CWR]		= IP_VS_SCTP_DATA,
	[SCTP_CID_SHUTDOWN_COMPLETE]	= IP_VS_SCTP_SHUTDOWN_COMPLETE,
};

/* SCTP States:
 * See RFC 2960, 4. SCTP Association State Diagram
 *
 * New states (not in diagram):
 * - INIT1 state: use shorter timeout for dropped INIT packets
 * - REJECTED state: use shorter timeout if INIT is rejected with ABORT
 * - INIT, COOKIE_SENT, COOKIE_REPLIED, COOKIE states: for better debugging
 *
 * The states are as seen in real server. In the diagram, INIT1, INIT,
 * COOKIE_SENT and COOKIE_REPLIED processing happens in CLOSED state.
 *
 * States as per packets from client (C) and server (S):
 *
 * Setup of client connection:
 * IP_VS_SCTP_S_INIT1: First C:INIT sent, wait for S:INIT-ACK
 * IP_VS_SCTP_S_INIT: Next C:INIT sent, wait for S:INIT-ACK
 * IP_VS_SCTP_S_COOKIE_SENT: S:INIT-ACK sent, wait for C:COOKIE-ECHO
 * IP_VS_SCTP_S_COOKIE_REPLIED: C:COOKIE-ECHO sent, wait for S:COOKIE-ACK
 *
 * Setup of server connection:
 * IP_VS_SCTP_S_COOKIE_WAIT: S:INIT sent, wait for C:INIT-ACK
 * IP_VS_SCTP_S_COOKIE: C:INIT-ACK sent, wait for S:COOKIE-ECHO
 * IP_VS_SCTP_S_COOKIE_ECHOED: S:COOKIE-ECHO sent, wait for C:COOKIE-ACK
 */

#define sNO IP_VS_SCTP_S_NONE
#define sI1 IP_VS_SCTP_S_INIT1
#define sIN IP_VS_SCTP_S_INIT
#define sCS IP_VS_SCTP_S_COOKIE_SENT
#define sCR IP_VS_SCTP_S_COOKIE_REPLIED
#define sCW IP_VS_SCTP_S_COOKIE_WAIT
#define sCO IP_VS_SCTP_S_COOKIE
#define sCE IP_VS_SCTP_S_COOKIE_ECHOED
#define sES IP_VS_SCTP_S_ESTABLISHED
#define sSS IP_VS_SCTP_S_SHUTDOWN_SENT
#define sSR IP_VS_SCTP_S_SHUTDOWN_RECEIVED
#define sSA IP_VS_SCTP_S_SHUTDOWN_ACK_SENT
#define sRJ IP_VS_SCTP_S_REJECTED
#define sCL IP_VS_SCTP_S_CLOSED

static const __u8 sctp_states
	[IP_VS_DIR_LAST][IP_VS_SCTP_EVENT_LAST][IP_VS_SCTP_S_LAST] = {
	{ /* INPUT */
/*        sNO, sI1, sIN, sCS, sCR, sCW, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL*/
/* d   */{sES, sI1, sIN, sCS, sCR, sCW, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* i   */{sI1, sIN, sIN, sCS, sCR, sCW, sCO, sCE, sES, sSS, sSR, sSA, sIN, sIN},
/* i_a */{sCW, sCW, sCW, sCS, sCR, sCO, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* c_e */{sCR, sIN, sIN, sCR, sCR, sCW, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* c_a */{sES, sI1, sIN, sCS, sCR, sCW, sCO, sES, sES, sSS, sSR, sSA, sRJ, sCL},
/* s   */{sSR, sI1, sIN, sCS, sCR, sCW, sCO, sCE, sSR, sSS, sSR, sSA, sRJ, sCL},
/* s_a */{sCL, sIN, sIN, sCS, sCR, sCW, sCO, sCE, sES, sCL, sSR, sCL, sRJ, sCL},
/* s_c */{sCL, sCL, sCL, sCS, sCR, sCW, sCO, sCE, sES, sSS, sSR, sCL, sRJ, sCL},
/* err */{sCL, sI1, sIN, sCS, sCR, sCW, sCO, sCL, sES, sSS, sSR, sSA, sRJ, sCL},
/* ab  */{sCL, sCL, sCL, sCL, sCL, sRJ, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL},
	},
	{ /* OUTPUT */
/*        sNO, sI1, sIN, sCS, sCR, sCW, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL*/
/* d   */{sES, sI1, sIN, sCS, sCR, sCW, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* i   */{sCW, sCW, sCW, sCW, sCW, sCW, sCW, sCW, sES, sCW, sCW, sCW, sCW, sCW},
/* i_a */{sCS, sCS, sCS, sCS, sCR, sCW, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* c_e */{sCE, sCE, sCE, sCE, sCE, sCE, sCE, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* c_a */{sES, sES, sES, sES, sES, sES, sES, sES, sES, sSS, sSR, sSA, sRJ, sCL},
/* s   */{sSS, sSS, sSS, sSS, sSS, sSS, sSS, sSS, sSS, sSS, sSR, sSA, sRJ, sCL},
/* s_a */{sSA, sSA, sSA, sSA, sSA, sCW, sCO, sCE, sES, sSA, sSA, sSA, sRJ, sCL},
/* s_c */{sCL, sI1, sIN, sCS, sCR, sCW, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* err */{sCL, sCL, sCL, sCL, sCL, sCW, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* ab  */{sCL, sRJ, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL},
	},
	{ /* INPUT-ONLY */
/*        sNO, sI1, sIN, sCS, sCR, sCW, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL*/
/* d   */{sES, sI1, sIN, sCS, sCR, sES, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* i   */{sI1, sIN, sIN, sIN, sIN, sIN, sCO, sCE, sES, sSS, sSR, sSA, sIN, sIN},
/* i_a */{sCE, sCE, sCE, sCE, sCE, sCE, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* c_e */{sES, sES, sES, sES, sES, sES, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* c_a */{sES, sI1, sIN, sES, sES, sCW, sES, sES, sES, sSS, sSR, sSA, sRJ, sCL},
/* s   */{sSR, sI1, sIN, sCS, sCR, sCW, sCO, sCE, sSR, sSS, sSR, sSA, sRJ, sCL},
/* s_a */{sCL, sIN, sIN, sCS, sCR, sCW, sCO, sCE, sCL, sCL, sSR, sCL, sRJ, sCL},
/* s_c */{sCL, sCL, sCL, sCL, sCL, sCW, sCO, sCE, sES, sSS, sCL, sCL, sRJ, sCL},
/* err */{sCL, sI1, sIN, sCS, sCR, sCW, sCO, sCE, sES, sSS, sSR, sSA, sRJ, sCL},
/* ab  */{sCL, sCL, sCL, sCL, sCL, sRJ, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL},
	},
};

#define IP_VS_SCTP_MAX_RTO	((60 + 1) * HZ)

/* Timeout table[state] */
static const int sctp_timeouts[IP_VS_SCTP_S_LAST + 1] = {
	[IP_VS_SCTP_S_NONE]			= 2 * HZ,
	[IP_VS_SCTP_S_INIT1]			= (0 + 3 + 1) * HZ,
	[IP_VS_SCTP_S_INIT]			= IP_VS_SCTP_MAX_RTO,
	[IP_VS_SCTP_S_COOKIE_SENT]		= IP_VS_SCTP_MAX_RTO,
	[IP_VS_SCTP_S_COOKIE_REPLIED]		= IP_VS_SCTP_MAX_RTO,
	[IP_VS_SCTP_S_COOKIE_WAIT]		= IP_VS_SCTP_MAX_RTO,
	[IP_VS_SCTP_S_COOKIE]			= IP_VS_SCTP_MAX_RTO,
	[IP_VS_SCTP_S_COOKIE_ECHOED]		= IP_VS_SCTP_MAX_RTO,
	[IP_VS_SCTP_S_ESTABLISHED]		= 15 * 60 * HZ,
	[IP_VS_SCTP_S_SHUTDOWN_SENT]		= IP_VS_SCTP_MAX_RTO,
	[IP_VS_SCTP_S_SHUTDOWN_RECEIVED]	= IP_VS_SCTP_MAX_RTO,
	[IP_VS_SCTP_S_SHUTDOWN_ACK_SENT]	= IP_VS_SCTP_MAX_RTO,
	[IP_VS_SCTP_S_REJECTED]			= (0 + 3 + 1) * HZ,
	[IP_VS_SCTP_S_CLOSED]			= IP_VS_SCTP_MAX_RTO,
	[IP_VS_SCTP_S_LAST]			= 2 * HZ,
};

static const char *sctp_state_name_table[IP_VS_SCTP_S_LAST + 1] = {
	[IP_VS_SCTP_S_NONE]			= "NONE",
	[IP_VS_SCTP_S_INIT1]			= "INIT1",
	[IP_VS_SCTP_S_INIT]			= "INIT",
	[IP_VS_SCTP_S_COOKIE_SENT]		= "C-SENT",
	[IP_VS_SCTP_S_COOKIE_REPLIED]		= "C-REPLIED",
	[IP_VS_SCTP_S_COOKIE_WAIT]		= "C-WAIT",
	[IP_VS_SCTP_S_COOKIE]			= "COOKIE",
	[IP_VS_SCTP_S_COOKIE_ECHOED]		= "C-ECHOED",
	[IP_VS_SCTP_S_ESTABLISHED]		= "ESTABLISHED",
	[IP_VS_SCTP_S_SHUTDOWN_SENT]		= "S-SENT",
	[IP_VS_SCTP_S_SHUTDOWN_RECEIVED]	= "S-RECEIVED",
	[IP_VS_SCTP_S_SHUTDOWN_ACK_SENT]	= "S-ACK-SENT",
	[IP_VS_SCTP_S_REJECTED]			= "REJECTED",
	[IP_VS_SCTP_S_CLOSED]			= "CLOSED",
	[IP_VS_SCTP_S_LAST]			= "BUG!",
};


static const char *sctp_state_name(int state)
{
	if (state >= IP_VS_SCTP_S_LAST)
		return "ERR!";
	if (sctp_state_name_table[state])
		return sctp_state_name_table[state];
	return "?";
}

static inline void
set_sctp_state(struct ip_vs_proto_data *pd, struct ip_vs_conn *cp,
		int direction, const struct sk_buff *skb)
{
	sctp_chunkhdr_t _sctpch, *sch;
	unsigned char chunk_type;
	int event, next_state;
	int ihl, cofs;

#ifdef CONFIG_IP_VS_IPV6
	ihl = cp->af == AF_INET ? ip_hdrlen(skb) : sizeof(struct ipv6hdr);
#else
	ihl = ip_hdrlen(skb);
#endif

	cofs = ihl + sizeof(sctp_sctphdr_t);
	sch = skb_header_pointer(skb, cofs, sizeof(_sctpch), &_sctpch);
	if (sch == NULL)
		return;

	chunk_type = sch->type;
	/*
	 * Section 3: Multiple chunks can be bundled into one SCTP packet
	 * up to the MTU size, except for the INIT, INIT ACK, and
	 * SHUTDOWN COMPLETE chunks. These chunks MUST NOT be bundled with
	 * any other chunk in a packet.
	 *
	 * Section 3.3.7: DATA chunks MUST NOT be bundled with ABORT. Control
	 * chunks (except for INIT, INIT ACK, and SHUTDOWN COMPLETE) MAY be
	 * bundled with an ABORT, but they MUST be placed before the ABORT
	 * in the SCTP packet or they will be ignored by the receiver.
	 */
	if ((sch->type == SCTP_CID_COOKIE_ECHO) ||
	    (sch->type == SCTP_CID_COOKIE_ACK)) {
		int clen = ntohs(sch->length);

		if (clen >= sizeof(sctp_chunkhdr_t)) {
			sch = skb_header_pointer(skb, cofs + ALIGN(clen, 4),
						 sizeof(_sctpch), &_sctpch);
			if (sch && sch->type == SCTP_CID_ABORT)
				chunk_type = sch->type;
		}
	}

	event = (chunk_type < sizeof(sctp_events)) ?
		sctp_events[chunk_type] : IP_VS_SCTP_DATA;

	/* Update direction to INPUT_ONLY if necessary
	 * or delete NO_OUTPUT flag if output packet detected
	 */
	if (cp->flags & IP_VS_CONN_F_NOOUTPUT) {
		if (direction == IP_VS_DIR_OUTPUT)
			cp->flags &= ~IP_VS_CONN_F_NOOUTPUT;
		else
			direction = IP_VS_DIR_INPUT_ONLY;
	}

	next_state = sctp_states[direction][event][cp->state];

	if (next_state != cp->state) {
		struct ip_vs_dest *dest = cp->dest;

		IP_VS_DBG_BUF(8, "%s %s  %s:%d->"
				"%s:%d state: %s->%s conn->refcnt:%d\n",
				pd->pp->name,
				((direction == IP_VS_DIR_OUTPUT) ?
				 "output " : "input "),
				IP_VS_DBG_ADDR(cp->af, &cp->daddr),
				ntohs(cp->dport),
				IP_VS_DBG_ADDR(cp->af, &cp->caddr),
				ntohs(cp->cport),
				sctp_state_name(cp->state),
				sctp_state_name(next_state),
				atomic_read(&cp->refcnt));
		if (dest) {
			if (!(cp->flags & IP_VS_CONN_F_INACTIVE) &&
				(next_state != IP_VS_SCTP_S_ESTABLISHED)) {
				atomic_dec(&dest->activeconns);
				atomic_inc(&dest->inactconns);
				cp->flags |= IP_VS_CONN_F_INACTIVE;
			} else if ((cp->flags & IP_VS_CONN_F_INACTIVE) &&
				   (next_state == IP_VS_SCTP_S_ESTABLISHED)) {
				atomic_inc(&dest->activeconns);
				atomic_dec(&dest->inactconns);
				cp->flags &= ~IP_VS_CONN_F_INACTIVE;
			}
		}
	}
	if (likely(pd))
		cp->timeout = pd->timeout_table[cp->state = next_state];
	else	/* What to do ? */
		cp->timeout = sctp_timeouts[cp->state = next_state];
}

static void
sctp_state_transition(struct ip_vs_conn *cp, int direction,
		const struct sk_buff *skb, struct ip_vs_proto_data *pd)
{
	spin_lock_bh(&cp->lock);
	set_sctp_state(pd, cp, direction, skb);
	spin_unlock_bh(&cp->lock);
}

static inline __u16 sctp_app_hashkey(__be16 port)
{
	return (((__force u16)port >> SCTP_APP_TAB_BITS) ^ (__force u16)port)
		& SCTP_APP_TAB_MASK;
}

static int sctp_register_app(struct net *net, struct ip_vs_app *inc)
{
	struct ip_vs_app *i;
	__u16 hash;
	__be16 port = inc->port;
	int ret = 0;
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ip_vs_proto_data *pd = ip_vs_proto_data_get(net, IPPROTO_SCTP);

	hash = sctp_app_hashkey(port);

	list_for_each_entry(i, &ipvs->sctp_apps[hash], p_list) {
		if (i->port == port) {
			ret = -EEXIST;
			goto out;
		}
	}
	list_add_rcu(&inc->p_list, &ipvs->sctp_apps[hash]);
	atomic_inc(&pd->appcnt);
out:

	return ret;
}

static void sctp_unregister_app(struct net *net, struct ip_vs_app *inc)
{
	struct ip_vs_proto_data *pd = ip_vs_proto_data_get(net, IPPROTO_SCTP);

	atomic_dec(&pd->appcnt);
	list_del_rcu(&inc->p_list);
}

static int sctp_app_conn_bind(struct ip_vs_conn *cp)
{
	struct netns_ipvs *ipvs = net_ipvs(ip_vs_conn_net(cp));
	int hash;
	struct ip_vs_app *inc;
	int result = 0;

	/* Default binding: bind app only for NAT */
	if (IP_VS_FWD_METHOD(cp) != IP_VS_CONN_F_MASQ)
		return 0;
	/* Lookup application incarnations and bind the right one */
	hash = sctp_app_hashkey(cp->vport);

	rcu_read_lock();
	list_for_each_entry_rcu(inc, &ipvs->sctp_apps[hash], p_list) {
		if (inc->port == cp->vport) {
			if (unlikely(!ip_vs_app_inc_get(inc)))
				break;
			rcu_read_unlock();

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
	rcu_read_unlock();
out:
	return result;
}

/* ---------------------------------------------
 *   timeouts is netns related now.
 * ---------------------------------------------
 */
static int __ip_vs_sctp_init(struct net *net, struct ip_vs_proto_data *pd)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	ip_vs_init_hash_table(ipvs->sctp_apps, SCTP_APP_TAB_SIZE);
	pd->timeout_table = ip_vs_create_timeout_table((int *)sctp_timeouts,
							sizeof(sctp_timeouts));
	if (!pd->timeout_table)
		return -ENOMEM;
	return 0;
}

static void __ip_vs_sctp_exit(struct net *net, struct ip_vs_proto_data *pd)
{
	kfree(pd->timeout_table);
}

struct ip_vs_protocol ip_vs_protocol_sctp = {
	.name		= "SCTP",
	.protocol	= IPPROTO_SCTP,
	.num_states	= IP_VS_SCTP_S_LAST,
	.dont_defrag	= 0,
	.init		= NULL,
	.exit		= NULL,
	.init_netns	= __ip_vs_sctp_init,
	.exit_netns	= __ip_vs_sctp_exit,
	.register_app	= sctp_register_app,
	.unregister_app = sctp_unregister_app,
	.conn_schedule	= sctp_conn_schedule,
	.conn_in_get	= ip_vs_conn_in_get_proto,
	.conn_out_get	= ip_vs_conn_out_get_proto,
	.snat_handler	= sctp_snat_handler,
	.dnat_handler	= sctp_dnat_handler,
	.csum_check	= sctp_csum_check,
	.state_name	= sctp_state_name,
	.state_transition = sctp_state_transition,
	.app_conn_bind	= sctp_app_conn_bind,
	.debug_packet	= ip_vs_tcpudp_debug_packet,
	.timeout_change	= NULL,
};
