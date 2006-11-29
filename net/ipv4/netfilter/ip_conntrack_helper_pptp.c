/*
 * ip_conntrack_pptp.c	- Version 3.0
 *
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
 * Limitations:
 * 	 - We blindly assume that control connections are always
 * 	   established in PNS->PAC direction.  This is a violation
 * 	   of RFFC2673
 * 	 - We can only support one single call within each session
 *
 * TODO:
 *	 - testing of incoming PPTP calls
 *
 * Changes:
 * 	2002-02-05 - Version 1.3
 * 	  - Call ip_conntrack_unexpect_related() from
 * 	    pptp_destroy_siblings() to destroy expectations in case
 * 	    CALL_DISCONNECT_NOTIFY or tcp fin packet was seen
 * 	    (Philip Craig <philipc@snapgear.com>)
 * 	  - Add Version information at module loadtime
 * 	2002-02-10 - Version 1.6
 * 	  - move to C99 style initializers
 * 	  - remove second expectation if first arrives
 * 	2004-10-22 - Version 2.0
 * 	  - merge Mandrake's 2.6.x port with recent 2.6.x API changes
 * 	  - fix lots of linear skb assumptions from Mandrake's port
 * 	2005-06-10 - Version 2.1
 * 	  - use ip_conntrack_expect_free() instead of kfree() on the
 * 	    expect's (which are from the slab for quite some time)
 * 	2005-06-10 - Version 3.0
 * 	  - port helper to post-2.6.11 API changes,
 * 	    funded by Oxcoda NetBox Blue (http://www.netboxblue.com/)
 * 	2005-07-30 - Version 3.1
 * 	  - port helper to 2.6.13 API changes
 *
 */

#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <net/checksum.h>
#include <net/tcp.h>

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_proto_gre.h>
#include <linux/netfilter_ipv4/ip_conntrack_pptp.h>

#define IP_CT_PPTP_VERSION "3.1"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("Netfilter connection tracking helper module for PPTP");

static DEFINE_SPINLOCK(ip_pptp_lock);

int
(*ip_nat_pptp_hook_outbound)(struct sk_buff **pskb,
			  struct ip_conntrack *ct,
			  enum ip_conntrack_info ctinfo,
			  struct PptpControlHeader *ctlh,
			  union pptp_ctrl_union *pptpReq);

int
(*ip_nat_pptp_hook_inbound)(struct sk_buff **pskb,
			  struct ip_conntrack *ct,
			  enum ip_conntrack_info ctinfo,
			  struct PptpControlHeader *ctlh,
			  union pptp_ctrl_union *pptpReq);

void
(*ip_nat_pptp_hook_exp_gre)(struct ip_conntrack_expect *expect_orig,
			    struct ip_conntrack_expect *expect_reply);

void
(*ip_nat_pptp_hook_expectfn)(struct ip_conntrack *ct,
			     struct ip_conntrack_expect *exp);

#if 0
/* PptpControlMessageType names */
const char *pptp_msg_name[] = {
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
#define DEBUGP(format, args...)	printk(KERN_DEBUG "%s:%s: " format, __FILE__, __FUNCTION__, ## args)
#else
#define DEBUGP(format, args...)
#endif

#define SECS *HZ
#define MINS * 60 SECS
#define HOURS * 60 MINS

#define PPTP_GRE_TIMEOUT 		(10 MINS)
#define PPTP_GRE_STREAM_TIMEOUT 	(5 HOURS)

static void pptp_expectfn(struct ip_conntrack *ct,
			 struct ip_conntrack_expect *exp)
{
	DEBUGP("increasing timeouts\n");

	/* increase timeout of GRE data channel conntrack entry */
	ct->proto.gre.timeout = PPTP_GRE_TIMEOUT;
	ct->proto.gre.stream_timeout = PPTP_GRE_STREAM_TIMEOUT;

	/* Can you see how rusty this code is, compared with the pre-2.6.11
	 * one? That's what happened to my shiny newnat of 2002 ;( -HW */

	if (!ip_nat_pptp_hook_expectfn) {
		struct ip_conntrack_tuple inv_t;
		struct ip_conntrack_expect *exp_other;

		/* obviously this tuple inversion only works until you do NAT */
		invert_tuplepr(&inv_t, &exp->tuple);
		DEBUGP("trying to unexpect other dir: ");
		DUMP_TUPLE(&inv_t);

		exp_other = ip_conntrack_expect_find_get(&inv_t);
		if (exp_other) {
			/* delete other expectation.  */
			DEBUGP("found\n");
			ip_conntrack_unexpect_related(exp_other);
			ip_conntrack_expect_put(exp_other);
		} else {
			DEBUGP("not found\n");
		}
	} else {
		/* we need more than simple inversion */
		ip_nat_pptp_hook_expectfn(ct, exp);
	}
}

static int destroy_sibling_or_exp(const struct ip_conntrack_tuple *t)
{
	struct ip_conntrack_tuple_hash *h;
	struct ip_conntrack_expect *exp;

	DEBUGP("trying to timeout ct or exp for tuple ");
	DUMP_TUPLE(t);

	h = ip_conntrack_find_get(t, NULL);
	if (h)  {
		struct ip_conntrack *sibling = tuplehash_to_ctrack(h);
		DEBUGP("setting timeout of conntrack %p to 0\n", sibling);
		sibling->proto.gre.timeout = 0;
		sibling->proto.gre.stream_timeout = 0;
		if (del_timer(&sibling->timeout))
			sibling->timeout.function((unsigned long)sibling);
		ip_conntrack_put(sibling);
		return 1;
	} else {
		exp = ip_conntrack_expect_find_get(t);
		if (exp) {
			DEBUGP("unexpect_related of expect %p\n", exp);
			ip_conntrack_unexpect_related(exp);
			ip_conntrack_expect_put(exp);
			return 1;
		}
	}

	return 0;
}


/* timeout GRE data connections */
static void pptp_destroy_siblings(struct ip_conntrack *ct)
{
	struct ip_conntrack_tuple t;

	ip_ct_gre_keymap_destroy(ct);
	/* Since ct->sibling_list has literally rusted away in 2.6.11,
	 * we now need another way to find out about our sibling
	 * contrack and expects... -HW */

	/* try original (pns->pac) tuple */
	memcpy(&t, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple, sizeof(t));
	t.dst.protonum = IPPROTO_GRE;
	t.src.u.gre.key = ct->help.ct_pptp_info.pns_call_id;
	t.dst.u.gre.key = ct->help.ct_pptp_info.pac_call_id;

	if (!destroy_sibling_or_exp(&t))
		DEBUGP("failed to timeout original pns->pac ct/exp\n");

	/* try reply (pac->pns) tuple */
	memcpy(&t, &ct->tuplehash[IP_CT_DIR_REPLY].tuple, sizeof(t));
	t.dst.protonum = IPPROTO_GRE;
	t.src.u.gre.key = ct->help.ct_pptp_info.pac_call_id;
	t.dst.u.gre.key = ct->help.ct_pptp_info.pns_call_id;

	if (!destroy_sibling_or_exp(&t))
		DEBUGP("failed to timeout reply pac->pns ct/exp\n");
}

/* expect GRE connections (PNS->PAC and PAC->PNS direction) */
static inline int
exp_gre(struct ip_conntrack *ct,
	__be16 callid,
	__be16 peer_callid)
{
	struct ip_conntrack_expect *exp_orig, *exp_reply;
	int ret = 1;

	exp_orig = ip_conntrack_expect_alloc(ct);
	if (exp_orig == NULL)
		goto out;

	exp_reply = ip_conntrack_expect_alloc(ct);
	if (exp_reply == NULL)
		goto out_put_orig;

	/* original direction, PNS->PAC */
	exp_orig->tuple.src.ip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
	exp_orig->tuple.src.u.gre.key = peer_callid;
	exp_orig->tuple.dst.ip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip;
	exp_orig->tuple.dst.u.gre.key = callid;
	exp_orig->tuple.dst.protonum = IPPROTO_GRE;

	exp_orig->mask.src.ip = htonl(0xffffffff);
	exp_orig->mask.src.u.all = 0;
	exp_orig->mask.dst.u.gre.key = htons(0xffff);
	exp_orig->mask.dst.ip = htonl(0xffffffff);
	exp_orig->mask.dst.protonum = 0xff;

	exp_orig->master = ct;
	exp_orig->expectfn = pptp_expectfn;
	exp_orig->flags = 0;

	/* both expectations are identical apart from tuple */
	memcpy(exp_reply, exp_orig, sizeof(*exp_reply));

	/* reply direction, PAC->PNS */
	exp_reply->tuple.src.ip = ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip;
	exp_reply->tuple.src.u.gre.key = callid;
	exp_reply->tuple.dst.ip = ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip;
	exp_reply->tuple.dst.u.gre.key = peer_callid;
	exp_reply->tuple.dst.protonum = IPPROTO_GRE;

	if (ip_nat_pptp_hook_exp_gre)
		ip_nat_pptp_hook_exp_gre(exp_orig, exp_reply);
	if (ip_conntrack_expect_related(exp_orig) != 0)
		goto out_put_both;
	if (ip_conntrack_expect_related(exp_reply) != 0)
		goto out_unexpect_orig;

	/* Add GRE keymap entries */
	if (ip_ct_gre_keymap_add(ct, &exp_orig->tuple, 0) != 0)
		goto out_unexpect_both;
	if (ip_ct_gre_keymap_add(ct, &exp_reply->tuple, 1) != 0) {
		ip_ct_gre_keymap_destroy(ct);
		goto out_unexpect_both;
	}
	ret = 0;

out_put_both:
	ip_conntrack_expect_put(exp_reply);
out_put_orig:
	ip_conntrack_expect_put(exp_orig);
out:
	return ret;

out_unexpect_both:
	ip_conntrack_unexpect_related(exp_reply);
out_unexpect_orig:
	ip_conntrack_unexpect_related(exp_orig);
	goto out_put_both;
}

static inline int
pptp_inbound_pkt(struct sk_buff **pskb,
		 struct PptpControlHeader *ctlh,
		 union pptp_ctrl_union *pptpReq,
		 unsigned int reqlen,
		 struct ip_conntrack *ct,
		 enum ip_conntrack_info ctinfo)
{
	struct ip_ct_pptp_master *info = &ct->help.ct_pptp_info;
	u_int16_t msg;
	__be16 cid = 0, pcid = 0;

	msg = ntohs(ctlh->messageType);
	DEBUGP("inbound control message %s\n", pptp_msg_name[msg]);

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
		DEBUGP("%s, CID=%X, PCID=%X\n", pptp_msg_name[msg],
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
		DEBUGP("%s, CID=%X\n", pptp_msg_name[msg], ntohs(cid));
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

		DEBUGP("%s, PCID=%X\n", pptp_msg_name[msg], ntohs(pcid));
		info->cstate = PPTP_CALL_IN_CONF;

		/* we expect a GRE connection from PAC to PNS */
		exp_gre(ct, cid, pcid);
		break;

	case PPTP_CALL_DISCONNECT_NOTIFY:
		/* server confirms disconnect */
		cid = pptpReq->disc.callID;
		DEBUGP("%s, CID=%X\n", pptp_msg_name[msg], ntohs(cid));
		info->cstate = PPTP_CALL_NONE;

		/* untrack this call id, unexpect GRE packets */
		pptp_destroy_siblings(ct);
		break;

	case PPTP_WAN_ERROR_NOTIFY:
	case PPTP_ECHO_REQUEST:
	case PPTP_ECHO_REPLY:
		/* I don't have to explain these ;) */
		break;
	default:
		goto invalid;
	}

	if (ip_nat_pptp_hook_inbound)
		return ip_nat_pptp_hook_inbound(pskb, ct, ctinfo, ctlh,
						pptpReq);
	return NF_ACCEPT;

invalid:
	DEBUGP("invalid %s: type=%d cid=%u pcid=%u "
	       "cstate=%d sstate=%d pns_cid=%u pac_cid=%u\n",
	       msg <= PPTP_MSG_MAX ? pptp_msg_name[msg] : pptp_msg_name[0],
	       msg, ntohs(cid), ntohs(pcid),  info->cstate, info->sstate,
	       ntohs(info->pns_call_id), ntohs(info->pac_call_id));
	return NF_ACCEPT;
}

static inline int
pptp_outbound_pkt(struct sk_buff **pskb,
		  struct PptpControlHeader *ctlh,
		  union pptp_ctrl_union *pptpReq,
		  unsigned int reqlen,
		  struct ip_conntrack *ct,
		  enum ip_conntrack_info ctinfo)
{
	struct ip_ct_pptp_master *info = &ct->help.ct_pptp_info;
	u_int16_t msg;
	__be16 cid = 0, pcid = 0;

	msg = ntohs(ctlh->messageType);
	DEBUGP("outbound control message %s\n", pptp_msg_name[msg]);

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
		DEBUGP("%s, CID=%X\n", pptp_msg_name[msg], ntohs(cid));
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
		DEBUGP("%s, CID=%X PCID=%X\n", pptp_msg_name[msg],
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

	if (ip_nat_pptp_hook_outbound)
		return ip_nat_pptp_hook_outbound(pskb, ct, ctinfo, ctlh,
						 pptpReq);
	return NF_ACCEPT;

invalid:
	DEBUGP("invalid %s: type=%d cid=%u pcid=%u "
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
conntrack_pptp_help(struct sk_buff **pskb,
		    struct ip_conntrack *ct, enum ip_conntrack_info ctinfo)

{
	int dir = CTINFO2DIR(ctinfo);
	struct ip_ct_pptp_master *info = &ct->help.ct_pptp_info;
	struct tcphdr _tcph, *tcph;
	struct pptp_pkt_hdr _pptph, *pptph;
	struct PptpControlHeader _ctlh, *ctlh;
	union pptp_ctrl_union _pptpReq, *pptpReq;
	unsigned int tcplen = (*pskb)->len - (*pskb)->nh.iph->ihl * 4;
	unsigned int datalen, reqlen, nexthdr_off;
	int oldsstate, oldcstate;
	int ret;
	u_int16_t msg;

	/* don't do any tracking before tcp handshake complete */
	if (ctinfo != IP_CT_ESTABLISHED
	    && ctinfo != IP_CT_ESTABLISHED+IP_CT_IS_REPLY) {
		DEBUGP("ctinfo = %u, skipping\n", ctinfo);
		return NF_ACCEPT;
	}

	nexthdr_off = (*pskb)->nh.iph->ihl*4;
	tcph = skb_header_pointer(*pskb, nexthdr_off, sizeof(_tcph), &_tcph);
	BUG_ON(!tcph);
	nexthdr_off += tcph->doff * 4;
 	datalen = tcplen - tcph->doff * 4;

	pptph = skb_header_pointer(*pskb, nexthdr_off, sizeof(_pptph), &_pptph);
	if (!pptph) {
		DEBUGP("no full PPTP header, can't track\n");
		return NF_ACCEPT;
	}
	nexthdr_off += sizeof(_pptph);
	datalen -= sizeof(_pptph);

	/* if it's not a control message we can't do anything with it */
	if (ntohs(pptph->packetType) != PPTP_PACKET_CONTROL ||
	    ntohl(pptph->magicCookie) != PPTP_MAGIC_COOKIE) {
		DEBUGP("not a control packet\n");
		return NF_ACCEPT;
	}

	ctlh = skb_header_pointer(*pskb, nexthdr_off, sizeof(_ctlh), &_ctlh);
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

	pptpReq = skb_header_pointer(*pskb, nexthdr_off, reqlen, &_pptpReq);
	if (!pptpReq)
		return NF_ACCEPT;

	oldsstate = info->sstate;
	oldcstate = info->cstate;

	spin_lock_bh(&ip_pptp_lock);

	/* FIXME: We just blindly assume that the control connection is always
	 * established from PNS->PAC.  However, RFC makes no guarantee */
	if (dir == IP_CT_DIR_ORIGINAL)
		/* client -> server (PNS -> PAC) */
		ret = pptp_outbound_pkt(pskb, ctlh, pptpReq, reqlen, ct,
					ctinfo);
	else
		/* server -> client (PAC -> PNS) */
		ret = pptp_inbound_pkt(pskb, ctlh, pptpReq, reqlen, ct,
				       ctinfo);
	DEBUGP("sstate: %d->%d, cstate: %d->%d\n",
		oldsstate, info->sstate, oldcstate, info->cstate);
	spin_unlock_bh(&ip_pptp_lock);

	return ret;
}

/* control protocol helper */
static struct ip_conntrack_helper pptp = {
	.list = { NULL, NULL },
	.name = "pptp",
	.me = THIS_MODULE,
	.max_expected = 2,
	.timeout = 5 * 60,
	.tuple = { .src = { .ip = 0,
		 	    .u = { .tcp = { .port =
				    __constant_htons(PPTP_CONTROL_PORT) } }
			  },
		   .dst = { .ip = 0,
			    .u = { .all = 0 },
			    .protonum = IPPROTO_TCP
			  }
		 },
	.mask = { .src = { .ip = 0,
			   .u = { .tcp = { .port = __constant_htons(0xffff) } }
			 },
		  .dst = { .ip = 0,
			   .u = { .all = 0 },
			   .protonum = 0xff
		 	 }
		},
	.help = conntrack_pptp_help,
	.destroy = pptp_destroy_siblings,
};

extern void ip_ct_proto_gre_fini(void);
extern int __init ip_ct_proto_gre_init(void);

/* ip_conntrack_pptp initialization */
static int __init ip_conntrack_helper_pptp_init(void)
{
	int retcode;

	retcode = ip_ct_proto_gre_init();
	if (retcode < 0)
		return retcode;

	DEBUGP(" registering helper\n");
	if ((retcode = ip_conntrack_helper_register(&pptp))) {
		printk(KERN_ERR "Unable to register conntrack application "
				"helper for pptp: %d\n", retcode);
		ip_ct_proto_gre_fini();
		return retcode;
	}

	printk("ip_conntrack_pptp version %s loaded\n", IP_CT_PPTP_VERSION);
	return 0;
}

static void __exit ip_conntrack_helper_pptp_fini(void)
{
	ip_conntrack_helper_unregister(&pptp);
	ip_ct_proto_gre_fini();
	printk("ip_conntrack_pptp version %s unloaded\n", IP_CT_PPTP_VERSION);
}

module_init(ip_conntrack_helper_pptp_init);
module_exit(ip_conntrack_helper_pptp_fini);

EXPORT_SYMBOL(ip_nat_pptp_hook_outbound);
EXPORT_SYMBOL(ip_nat_pptp_hook_inbound);
EXPORT_SYMBOL(ip_nat_pptp_hook_exp_gre);
EXPORT_SYMBOL(ip_nat_pptp_hook_expectfn);
