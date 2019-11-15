// SPDX-License-Identifier: GPL-2.0-only
/*
 * Connection tracking support for PPTP (Point to Point Tunneling Protocol).
 * PPTP is a a protocol for creating virtual private networks.
 * It is a specification defined by Microsoft and some vendors
 * working with Microsoft.  PPTP is built on top of a modified
 * version of the Internet Generic Routing Encapsulation Protocol.
 * GRE is defined in RFC 1701 and RFC 1702.  Documentation of
 * PPTP can be found in RFC 2637
 *
 * (C) 2000-2005 by Harald Welte <laforge@gnumonks.org>
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 *
 * (C) 2006-2012 Patrick McHardy <kaber@trash.net>
 *
 * Limitations:
 * 	 - We blindly assume that control connections are always
 * 	   established in PNS->PAC direction.  This is a violation
 *	   of RFC 2637
 * 	 - We can only support one single call within each session
 * TODO:
 *	 - testing of incoming PPTP calls
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/tcp.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <linux/netfilter/nf_conntrack_proto_gre.h>
#include <linux/netfilter/nf_conntrack_pptp.h>

#define NF_CT_PPTP_VERSION "3.1"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("Netfilter connection tracking helper module for PPTP");
MODULE_ALIAS("ip_conntrack_pptp");
MODULE_ALIAS_NFCT_HELPER("pptp");

static DEFINE_SPINLOCK(nf_pptp_lock);

int
(*nf_nat_pptp_hook_outbound)(struct sk_buff *skb,
			     struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			     unsigned int protoff, struct PptpControlHeader *ctlh,
			     union pptp_ctrl_union *pptpReq) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_pptp_hook_outbound);

int
(*nf_nat_pptp_hook_inbound)(struct sk_buff *skb,
			    struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			    unsigned int protoff, struct PptpControlHeader *ctlh,
			    union pptp_ctrl_union *pptpReq) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_pptp_hook_inbound);

void
(*nf_nat_pptp_hook_exp_gre)(struct nf_conntrack_expect *expect_orig,
			    struct nf_conntrack_expect *expect_reply)
			    __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_pptp_hook_exp_gre);

void
(*nf_nat_pptp_hook_expectfn)(struct nf_conn *ct,
			     struct nf_conntrack_expect *exp) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_pptp_hook_expectfn);

#if defined(DEBUG) || defined(CONFIG_DYNAMIC_DEBUG)
/* PptpControlMessageType names */
const char *const pptp_msg_name[] = {
	"UNKNOWN_MESSAGE",
	"START_SESSION_REQUEST",
	"START_SESSION_REPLY",
	"STOP_SESSION_REQUEST",
	"STOP_SESSION_REPLY",
	"ECHO_REQUEST",
	"ECHO_REPLY",
	"OUT_CALL_REQUEST",
	"OUT_CALL_REPLY",
	"IN_CALL_REQUEST",
	"IN_CALL_REPLY",
	"IN_CALL_CONNECT",
	"CALL_CLEAR_REQUEST",
	"CALL_DISCONNECT_NOTIFY",
	"WAN_ERROR_NOTIFY",
	"SET_LINK_INFO"
};
EXPORT_SYMBOL(pptp_msg_name);
#endif

#define SECS *HZ
#define MINS * 60 SECS
#define HOURS * 60 MINS

#define PPTP_GRE_TIMEOUT 		(10 MINS)
#define PPTP_GRE_STREAM_TIMEOUT 	(5 HOURS)

static void pptp_expectfn(struct nf_conn *ct,
			 struct nf_conntrack_expect *exp)
{
	struct net *net = nf_ct_net(ct);
	typeof(nf_nat_pptp_hook_expectfn) nf_nat_pptp_expectfn;
	pr_debug("increasing timeouts\n");

	/* increase timeout of GRE data channel conntrack entry */
	ct->proto.gre.timeout	     = PPTP_GRE_TIMEOUT;
	ct->proto.gre.stream_timeout = PPTP_GRE_STREAM_TIMEOUT;

	/* Can you see how rusty this code is, compared with the pre-2.6.11
	 * one? That's what happened to my shiny newnat of 2002 ;( -HW */

	nf_nat_pptp_expectfn = rcu_dereference(nf_nat_pptp_hook_expectfn);
	if (nf_nat_pptp_expectfn && ct->master->status & IPS_NAT_MASK)
		nf_nat_pptp_expectfn(ct, exp);
	else {
		struct nf_conntrack_tuple inv_t;
		struct nf_conntrack_expect *exp_other;

		/* obviously this tuple inversion only works until you do NAT */
		nf_ct_invert_tuple(&inv_t, &exp->tuple);
		pr_debug("trying to unexpect other dir: ");
		nf_ct_dump_tuple(&inv_t);

		exp_other = nf_ct_expect_find_get(net, nf_ct_zone(ct), &inv_t);
		if (exp_other) {
			/* delete other expectation.  */
			pr_debug("found\n");
			nf_ct_unexpect_related(exp_other);
			nf_ct_expect_put(exp_other);
		} else {
			pr_debug("not found\n");
		}
	}
}

static int destroy_sibling_or_exp(struct net *net, struct nf_conn *ct,
				  const struct nf_conntrack_tuple *t)
{
	const struct nf_conntrack_tuple_hash *h;
	const struct nf_conntrack_zone *zone;
	struct nf_conntrack_expect *exp;
	struct nf_conn *sibling;

	pr_debug("trying to timeout ct or exp for tuple ");
	nf_ct_dump_tuple(t);

	zone = nf_ct_zone(ct);
	h = nf_conntrack_find_get(net, zone, t);
	if (h)  {
		sibling = nf_ct_tuplehash_to_ctrack(h);
		pr_debug("setting timeout of conntrack %p to 0\n", sibling);
		sibling->proto.gre.timeout	  = 0;
		sibling->proto.gre.stream_timeout = 0;
		nf_ct_kill(sibling);
		nf_ct_put(sibling);
		return 1;
	} else {
		exp = nf_ct_expect_find_get(net, zone, t);
		if (exp) {
			pr_debug("unexpect_related of expect %p\n", exp);
			nf_ct_unexpect_related(exp);
			nf_ct_expect_put(exp);
			return 1;
		}
	}
	return 0;
}

/* timeout GRE data connections */
static void pptp_destroy_siblings(struct nf_conn *ct)
{
	struct net *net = nf_ct_net(ct);
	const struct nf_ct_pptp_master *ct_pptp_info = nfct_help_data(ct);
	struct nf_conntrack_tuple t;

	nf_ct_gre_keymap_destroy(ct);

	/* try original (pns->pac) tuple */
	memcpy(&t, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple, sizeof(t));
	t.dst.protonum = IPPROTO_GRE;
	t.src.u.gre.key = ct_pptp_info->pns_call_id;
	t.dst.u.gre.key = ct_pptp_info->pac_call_id;
	if (!destroy_sibling_or_exp(net, ct, &t))
		pr_debug("failed to timeout original pns->pac ct/exp\n");

	/* try reply (pac->pns) tuple */
	memcpy(&t, &ct->tuplehash[IP_CT_DIR_REPLY].tuple, sizeof(t));
	t.dst.protonum = IPPROTO_GRE;
	t.src.u.gre.key = ct_pptp_info->pac_call_id;
	t.dst.u.gre.key = ct_pptp_info->pns_call_id;
	if (!destroy_sibling_or_exp(net, ct, &t))
		pr_debug("failed to timeout reply pac->pns ct/exp\n");
}

/* expect GRE connections (PNS->PAC and PAC->PNS direction) */
static int exp_gre(struct nf_conn *ct, __be16 callid, __be16 peer_callid)
{
	struct nf_conntrack_expect *exp_orig, *exp_reply;
	enum ip_conntrack_dir dir;
	int ret = 1;
	typeof(nf_nat_pptp_hook_exp_gre) nf_nat_pptp_exp_gre;

	exp_orig = nf_ct_expect_alloc(ct);
	if (exp_orig == NULL)
		goto out;

	exp_reply = nf_ct_expect_alloc(ct);
	if (exp_reply == NULL)
		goto out_put_orig;

	/* original direction, PNS->PAC */
	dir = IP_CT_DIR_ORIGINAL;
	nf_ct_expect_init(exp_orig, NF_CT_EXPECT_CLASS_DEFAULT,
			  nf_ct_l3num(ct),
			  &ct->tuplehash[dir].tuple.src.u3,
			  &ct->tuplehash[dir].tuple.dst.u3,
			  IPPROTO_GRE, &peer_callid, &callid);
	exp_orig->expectfn = pptp_expectfn;

	/* reply direction, PAC->PNS */
	dir = IP_CT_DIR_REPLY;
	nf_ct_expect_init(exp_reply, NF_CT_EXPECT_CLASS_DEFAULT,
			  nf_ct_l3num(ct),
			  &ct->tuplehash[dir].tuple.src.u3,
			  &ct->tuplehash[dir].tuple.dst.u3,
			  IPPROTO_GRE, &callid, &peer_callid);
	exp_reply->expectfn = pptp_expectfn;

	nf_nat_pptp_exp_gre = rcu_dereference(nf_nat_pptp_hook_exp_gre);
	if (nf_nat_pptp_exp_gre && ct->status & IPS_NAT_MASK)
		nf_nat_pptp_exp_gre(exp_orig, exp_reply);
	if (nf_ct_expect_related(exp_orig, 0) != 0)
		goto out_put_both;
	if (nf_ct_expect_related(exp_reply, 0) != 0)
		goto out_unexpect_orig;

	/* Add GRE keymap entries */
	if (nf_ct_gre_keymap_add(ct, IP_CT_DIR_ORIGINAL, &exp_orig->tuple) != 0)
		goto out_unexpect_both;
	if (nf_ct_gre_keymap_add(ct, IP_CT_DIR_REPLY, &exp_reply->tuple) != 0) {
		nf_ct_gre_keymap_destroy(ct);
		goto out_unexpect_both;
	}
	ret = 0;

out_put_both:
	nf_ct_expect_put(exp_reply);
out_put_orig:
	nf_ct_expect_put(exp_orig);
out:
	return ret;

out_unexpect_both:
	nf_ct_unexpect_related(exp_reply);
out_unexpect_orig:
	nf_ct_unexpect_related(exp_orig);
	goto out_put_both;
}

static int
pptp_inbound_pkt(struct sk_buff *skb, unsigned int protoff,
		 struct PptpControlHeader *ctlh,
		 union pptp_ctrl_union *pptpReq,
		 unsigned int reqlen,
		 struct nf_conn *ct,
		 enum ip_conntrack_info ctinfo)
{
	struct nf_ct_pptp_master *info = nfct_help_data(ct);
	u_int16_t msg;
	__be16 cid = 0, pcid = 0;
	typeof(nf_nat_pptp_hook_inbound) nf_nat_pptp_inbound;

	msg = ntohs(ctlh->messageType);
	pr_debug("inbound control message %s\n", pptp_msg_name[msg]);

	switch (msg) {
	case PPTP_START_SESSION_REPLY:
		/* server confirms new control session */
		if (info->sstate < PPTP_SESSION_REQUESTED)
			goto invalid;
		if (pptpReq->srep.resultCode == PPTP_START_OK)
			info->sstate = PPTP_SESSION_CONFIRMED;
		else
			info->sstate = PPTP_SESSION_ERROR;
		break;

	case PPTP_STOP_SESSION_REPLY:
		/* server confirms end of control session */
		if (info->sstate > PPTP_SESSION_STOPREQ)
			goto invalid;
		if (pptpReq->strep.resultCode == PPTP_STOP_OK)
			info->sstate = PPTP_SESSION_NONE;
		else
			info->sstate = PPTP_SESSION_ERROR;
		break;

	case PPTP_OUT_CALL_REPLY:
		/* server accepted call, we now expect GRE frames */
		if (info->sstate != PPTP_SESSION_CONFIRMED)
			goto invalid;
		if (info->cstate != PPTP_CALL_OUT_REQ &&
		    info->cstate != PPTP_CALL_OUT_CONF)
			goto invalid;

		cid = pptpReq->ocack.callID;
		pcid = pptpReq->ocack.peersCallID;
		if (info->pns_call_id != pcid)
			goto invalid;
		pr_debug("%s, CID=%X, PCID=%X\n", pptp_msg_name[msg],
			 ntohs(cid), ntohs(pcid));

		if (pptpReq->ocack.resultCode == PPTP_OUTCALL_CONNECT) {
			info->cstate = PPTP_CALL_OUT_CONF;
			info->pac_call_id = cid;
			exp_gre(ct, cid, pcid);
		} else
			info->cstate = PPTP_CALL_NONE;
		break;

	case PPTP_IN_CALL_REQUEST:
		/* server tells us about incoming call request */
		if (info->sstate != PPTP_SESSION_CONFIRMED)
			goto invalid;

		cid = pptpReq->icreq.callID;
		pr_debug("%s, CID=%X\n", pptp_msg_name[msg], ntohs(cid));
		info->cstate = PPTP_CALL_IN_REQ;
		info->pac_call_id = cid;
		break;

	case PPTP_IN_CALL_CONNECT:
		/* server tells us about incoming call established */
		if (info->sstate != PPTP_SESSION_CONFIRMED)
			goto invalid;
		if (info->cstate != PPTP_CALL_IN_REP &&
		    info->cstate != PPTP_CALL_IN_CONF)
			goto invalid;

		pcid = pptpReq->iccon.peersCallID;
		cid = info->pac_call_id;

		if (info->pns_call_id != pcid)
			goto invalid;

		pr_debug("%s, PCID=%X\n", pptp_msg_name[msg], ntohs(pcid));
		info->cstate = PPTP_CALL_IN_CONF;

		/* we expect a GRE connection from PAC to PNS */
		exp_gre(ct, cid, pcid);
		break;

	case PPTP_CALL_DISCONNECT_NOTIFY:
		/* server confirms disconnect */
		cid = pptpReq->disc.callID;
		pr_debug("%s, CID=%X\n", pptp_msg_name[msg], ntohs(cid));
		info->cstate = PPTP_CALL_NONE;

		/* untrack this call id, unexpect GRE packets */
		pptp_destroy_siblings(ct);
		break;

	case PPTP_WAN_ERROR_NOTIFY:
	case PPTP_SET_LINK_INFO:
	case PPTP_ECHO_REQUEST:
	case PPTP_ECHO_REPLY:
		/* I don't have to explain these ;) */
		break;

	default:
		goto invalid;
	}

	nf_nat_pptp_inbound = rcu_dereference(nf_nat_pptp_hook_inbound);
	if (nf_nat_pptp_inbound && ct->status & IPS_NAT_MASK)
		return nf_nat_pptp_inbound(skb, ct, ctinfo,
					   protoff, ctlh, pptpReq);
	return NF_ACCEPT;

invalid:
	pr_debug("invalid %s: type=%d cid=%u pcid=%u "
		 "cstate=%d sstate=%d pns_cid=%u pac_cid=%u\n",
		 msg <= PPTP_MSG_MAX ? pptp_msg_name[msg] : pptp_msg_name[0],
		 msg, ntohs(cid), ntohs(pcid),  info->cstate, info->sstate,
		 ntohs(info->pns_call_id), ntohs(info->pac_call_id));
	return NF_ACCEPT;
}

static int
pptp_outbound_pkt(struct sk_buff *skb, unsigned int protoff,
		  struct PptpControlHeader *ctlh,
		  union pptp_ctrl_union *pptpReq,
		  unsigned int reqlen,
		  struct nf_conn *ct,
		  enum ip_conntrack_info ctinfo)
{
	struct nf_ct_pptp_master *info = nfct_help_data(ct);
	u_int16_t msg;
	__be16 cid = 0, pcid = 0;
	typeof(nf_nat_pptp_hook_outbound) nf_nat_pptp_outbound;

	msg = ntohs(ctlh->messageType);
	pr_debug("outbound control message %s\n", pptp_msg_name[msg]);

	switch (msg) {
	case PPTP_START_SESSION_REQUEST:
		/* client requests for new control session */
		if (info->sstate != PPTP_SESSION_NONE)
			goto invalid;
		info->sstate = PPTP_SESSION_REQUESTED;
		break;

	case PPTP_STOP_SESSION_REQUEST:
		/* client requests end of control session */
		info->sstate = PPTP_SESSION_STOPREQ;
		break;

	case PPTP_OUT_CALL_REQUEST:
		/* client initiating connection to server */
		if (info->sstate != PPTP_SESSION_CONFIRMED)
			goto invalid;
		info->cstate = PPTP_CALL_OUT_REQ;
		/* track PNS call id */
		cid = pptpReq->ocreq.callID;
		pr_debug("%s, CID=%X\n", pptp_msg_name[msg], ntohs(cid));
		info->pns_call_id = cid;
		break;

	case PPTP_IN_CALL_REPLY:
		/* client answers incoming call */
		if (info->cstate != PPTP_CALL_IN_REQ &&
		    info->cstate != PPTP_CALL_IN_REP)
			goto invalid;

		cid = pptpReq->icack.callID;
		pcid = pptpReq->icack.peersCallID;
		if (info->pac_call_id != pcid)
			goto invalid;
		pr_debug("%s, CID=%X PCID=%X\n", pptp_msg_name[msg],
			 ntohs(cid), ntohs(pcid));

		if (pptpReq->icack.resultCode == PPTP_INCALL_ACCEPT) {
			/* part two of the three-way handshake */
			info->cstate = PPTP_CALL_IN_REP;
			info->pns_call_id = cid;
		} else
			info->cstate = PPTP_CALL_NONE;
		break;

	case PPTP_CALL_CLEAR_REQUEST:
		/* client requests hangup of call */
		if (info->sstate != PPTP_SESSION_CONFIRMED)
			goto invalid;
		/* FUTURE: iterate over all calls and check if
		 * call ID is valid.  We don't do this without newnat,
		 * because we only know about last call */
		info->cstate = PPTP_CALL_CLEAR_REQ;
		break;

	case PPTP_SET_LINK_INFO:
	case PPTP_ECHO_REQUEST:
	case PPTP_ECHO_REPLY:
		/* I don't have to explain these ;) */
		break;

	default:
		goto invalid;
	}

	nf_nat_pptp_outbound = rcu_dereference(nf_nat_pptp_hook_outbound);
	if (nf_nat_pptp_outbound && ct->status & IPS_NAT_MASK)
		return nf_nat_pptp_outbound(skb, ct, ctinfo,
					    protoff, ctlh, pptpReq);
	return NF_ACCEPT;

invalid:
	pr_debug("invalid %s: type=%d cid=%u pcid=%u "
		 "cstate=%d sstate=%d pns_cid=%u pac_cid=%u\n",
		 msg <= PPTP_MSG_MAX ? pptp_msg_name[msg] : pptp_msg_name[0],
		 msg, ntohs(cid), ntohs(pcid),  info->cstate, info->sstate,
		 ntohs(info->pns_call_id), ntohs(info->pac_call_id));
	return NF_ACCEPT;
}

static const unsigned int pptp_msg_size[] = {
	[PPTP_START_SESSION_REQUEST]  = sizeof(struct PptpStartSessionRequest),
	[PPTP_START_SESSION_REPLY]    = sizeof(struct PptpStartSessionReply),
	[PPTP_STOP_SESSION_REQUEST]   = sizeof(struct PptpStopSessionRequest),
	[PPTP_STOP_SESSION_REPLY]     = sizeof(struct PptpStopSessionReply),
	[PPTP_OUT_CALL_REQUEST]       = sizeof(struct PptpOutCallRequest),
	[PPTP_OUT_CALL_REPLY]	      = sizeof(struct PptpOutCallReply),
	[PPTP_IN_CALL_REQUEST]	      = sizeof(struct PptpInCallRequest),
	[PPTP_IN_CALL_REPLY]	      = sizeof(struct PptpInCallReply),
	[PPTP_IN_CALL_CONNECT]	      = sizeof(struct PptpInCallConnected),
	[PPTP_CALL_CLEAR_REQUEST]     = sizeof(struct PptpClearCallRequest),
	[PPTP_CALL_DISCONNECT_NOTIFY] = sizeof(struct PptpCallDisconnectNotify),
	[PPTP_WAN_ERROR_NOTIFY]	      = sizeof(struct PptpWanErrorNotify),
	[PPTP_SET_LINK_INFO]	      = sizeof(struct PptpSetLinkInfo),
};

/* track caller id inside control connection, call expect_related */
static int
conntrack_pptp_help(struct sk_buff *skb, unsigned int protoff,
		    struct nf_conn *ct, enum ip_conntrack_info ctinfo)

{
	int dir = CTINFO2DIR(ctinfo);
	const struct nf_ct_pptp_master *info = nfct_help_data(ct);
	const struct tcphdr *tcph;
	struct tcphdr _tcph;
	const struct pptp_pkt_hdr *pptph;
	struct pptp_pkt_hdr _pptph;
	struct PptpControlHeader _ctlh, *ctlh;
	union pptp_ctrl_union _pptpReq, *pptpReq;
	unsigned int tcplen = skb->len - protoff;
	unsigned int datalen, reqlen, nexthdr_off;
	int oldsstate, oldcstate;
	int ret;
	u_int16_t msg;

#if IS_ENABLED(CONFIG_NF_NAT)
	if (!nf_ct_is_confirmed(ct) && (ct->status & IPS_NAT_MASK)) {
		struct nf_conn_nat *nat = nf_ct_ext_find(ct, NF_CT_EXT_NAT);

		if (!nat && !nf_ct_ext_add(ct, NF_CT_EXT_NAT, GFP_ATOMIC))
			return NF_DROP;
	}
#endif
	/* don't do any tracking before tcp handshake complete */
	if (ctinfo != IP_CT_ESTABLISHED && ctinfo != IP_CT_ESTABLISHED_REPLY)
		return NF_ACCEPT;

	nexthdr_off = protoff;
	tcph = skb_header_pointer(skb, nexthdr_off, sizeof(_tcph), &_tcph);
	BUG_ON(!tcph);
	nexthdr_off += tcph->doff * 4;
	datalen = tcplen - tcph->doff * 4;

	pptph = skb_header_pointer(skb, nexthdr_off, sizeof(_pptph), &_pptph);
	if (!pptph) {
		pr_debug("no full PPTP header, can't track\n");
		return NF_ACCEPT;
	}
	nexthdr_off += sizeof(_pptph);
	datalen -= sizeof(_pptph);

	/* if it's not a control message we can't do anything with it */
	if (ntohs(pptph->packetType) != PPTP_PACKET_CONTROL ||
	    ntohl(pptph->magicCookie) != PPTP_MAGIC_COOKIE) {
		pr_debug("not a control packet\n");
		return NF_ACCEPT;
	}

	ctlh = skb_header_pointer(skb, nexthdr_off, sizeof(_ctlh), &_ctlh);
	if (!ctlh)
		return NF_ACCEPT;
	nexthdr_off += sizeof(_ctlh);
	datalen -= sizeof(_ctlh);

	reqlen = datalen;
	msg = ntohs(ctlh->messageType);
	if (msg > 0 && msg <= PPTP_MSG_MAX && reqlen < pptp_msg_size[msg])
		return NF_ACCEPT;
	if (reqlen > sizeof(*pptpReq))
		reqlen = sizeof(*pptpReq);

	pptpReq = skb_header_pointer(skb, nexthdr_off, reqlen, &_pptpReq);
	if (!pptpReq)
		return NF_ACCEPT;

	oldsstate = info->sstate;
	oldcstate = info->cstate;

	spin_lock_bh(&nf_pptp_lock);

	/* FIXME: We just blindly assume that the control connection is always
	 * established from PNS->PAC.  However, RFC makes no guarantee */
	if (dir == IP_CT_DIR_ORIGINAL)
		/* client -> server (PNS -> PAC) */
		ret = pptp_outbound_pkt(skb, protoff, ctlh, pptpReq, reqlen, ct,
					ctinfo);
	else
		/* server -> client (PAC -> PNS) */
		ret = pptp_inbound_pkt(skb, protoff, ctlh, pptpReq, reqlen, ct,
				       ctinfo);
	pr_debug("sstate: %d->%d, cstate: %d->%d\n",
		 oldsstate, info->sstate, oldcstate, info->cstate);
	spin_unlock_bh(&nf_pptp_lock);

	return ret;
}

static const struct nf_conntrack_expect_policy pptp_exp_policy = {
	.max_expected	= 2,
	.timeout	= 5 * 60,
};

/* control protocol helper */
static struct nf_conntrack_helper pptp __read_mostly = {
	.name			= "pptp",
	.me			= THIS_MODULE,
	.tuple.src.l3num	= AF_INET,
	.tuple.src.u.tcp.port	= cpu_to_be16(PPTP_CONTROL_PORT),
	.tuple.dst.protonum	= IPPROTO_TCP,
	.help			= conntrack_pptp_help,
	.destroy		= pptp_destroy_siblings,
	.expect_policy		= &pptp_exp_policy,
};

static int __init nf_conntrack_pptp_init(void)
{
	NF_CT_HELPER_BUILD_BUG_ON(sizeof(struct nf_ct_pptp_master));

	return nf_conntrack_helper_register(&pptp);
}

static void __exit nf_conntrack_pptp_fini(void)
{
	nf_conntrack_helper_unregister(&pptp);
}

module_init(nf_conntrack_pptp_init);
module_exit(nf_conntrack_pptp_fini);
