/*
 * ip_nat_pptp.c	- Version 3.0
 *
 * NAT support for PPTP (Point to Point Tunneling Protocol).
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
 * TODO: - NAT to a unique tuple, not to TCP source port
 * 	   (needs netfilter tuple reservation)
 *
 * Changes:
 *     2002-02-10 - Version 1.3
 *       - Use ip_nat_mangle_tcp_packet() because of cloned skb's
 *	   in local connections (Philip Craig <philipc@snapgear.com>)
 *       - add checks for magicCookie and pptp version
 *       - make argument list of pptp_{out,in}bound_packet() shorter
 *       - move to C99 style initializers
 *       - print version number at module loadtime
 *     2003-09-22 - Version 1.5
 *       - use SNATed tcp sourceport as callid, since we get called before
 *	   TCP header is mangled (Philip Craig <philipc@snapgear.com>)
 *     2004-10-22 - Version 2.0
 *       - kernel 2.6.x version
 *     2005-06-10 - Version 3.0
 *       - kernel >= 2.6.11 version,
 *	   funded by Oxcoda NetBox Blue (http://www.netboxblue.com/)
 * 
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/tcp.h>

#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_pptp.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_proto_gre.h>
#include <linux/netfilter_ipv4/ip_conntrack_pptp.h>

#define IP_NAT_PPTP_VERSION "3.0"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("Netfilter NAT helper module for PPTP");


#if 0
extern const char *pptp_msg_name[];
#define DEBUGP(format, args...) printk(KERN_DEBUG "%s:%s: " format, __FILE__, \
				       __FUNCTION__, ## args)
#else
#define DEBUGP(format, args...)
#endif

static void pptp_nat_expected(struct ip_conntrack *ct,
			      struct ip_conntrack_expect *exp)
{
	struct ip_conntrack *master = ct->master;
	struct ip_conntrack_expect *other_exp;
	struct ip_conntrack_tuple t;
	struct ip_ct_pptp_master *ct_pptp_info;
	struct ip_nat_pptp *nat_pptp_info;

	ct_pptp_info = &master->help.ct_pptp_info;
	nat_pptp_info = &master->nat.help.nat_pptp_info;

	/* And here goes the grand finale of corrosion... */

	if (exp->dir == IP_CT_DIR_ORIGINAL) {
		DEBUGP("we are PNS->PAC\n");
		/* therefore, build tuple for PAC->PNS */
		t.src.ip = master->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip;
		t.src.u.gre.key = htons(master->help.ct_pptp_info.pac_call_id);
		t.dst.ip = master->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip;
		t.dst.u.gre.key = htons(master->help.ct_pptp_info.pns_call_id);
		t.dst.protonum = IPPROTO_GRE;
	} else {
		DEBUGP("we are PAC->PNS\n");
		/* build tuple for PNS->PAC */
		t.src.ip = master->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
		t.src.u.gre.key = 
			htons(master->nat.help.nat_pptp_info.pns_call_id);
		t.dst.ip = master->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip;
		t.dst.u.gre.key = 
			htons(master->nat.help.nat_pptp_info.pac_call_id);
		t.dst.protonum = IPPROTO_GRE;
	}

	DEBUGP("trying to unexpect other dir: ");
	DUMP_TUPLE(&t);
	other_exp = ip_conntrack_expect_find(&t);
	if (other_exp) {
		ip_conntrack_unexpect_related(other_exp);
		ip_conntrack_expect_put(other_exp);
		DEBUGP("success\n");
	} else {
		DEBUGP("not found!\n");
	}

	ip_nat_follow_master(ct, exp);
}

/* outbound packets == from PNS to PAC */
static int
pptp_outbound_pkt(struct sk_buff **pskb,
		  struct ip_conntrack *ct,
		  enum ip_conntrack_info ctinfo,
		  struct PptpControlHeader *ctlh,
		  union pptp_ctrl_union *pptpReq)

{
	struct ip_ct_pptp_master *ct_pptp_info = &ct->help.ct_pptp_info;
	struct ip_nat_pptp *nat_pptp_info = &ct->nat.help.nat_pptp_info;

	u_int16_t msg, *cid = NULL, new_callid;

	new_callid = htons(ct_pptp_info->pns_call_id);
	
	switch (msg = ntohs(ctlh->messageType)) {
		case PPTP_OUT_CALL_REQUEST:
			cid = &pptpReq->ocreq.callID;
			/* FIXME: ideally we would want to reserve a call ID
			 * here.  current netfilter NAT core is not able to do
			 * this :( For now we use TCP source port. This breaks
			 * multiple calls within one control session */

			/* save original call ID in nat_info */
			nat_pptp_info->pns_call_id = ct_pptp_info->pns_call_id;

			/* don't use tcph->source since we are at a DSTmanip
			 * hook (e.g. PREROUTING) and pkt is not mangled yet */
			new_callid = ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u.tcp.port;

			/* save new call ID in ct info */
			ct_pptp_info->pns_call_id = ntohs(new_callid);
			break;
		case PPTP_IN_CALL_REPLY:
			cid = &pptpReq->icreq.callID;
			break;
		case PPTP_CALL_CLEAR_REQUEST:
			cid = &pptpReq->clrreq.callID;
			break;
		default:
			DEBUGP("unknown outbound packet 0x%04x:%s\n", msg,
			      (msg <= PPTP_MSG_MAX)? 
			      pptp_msg_name[msg]:pptp_msg_name[0]);
			/* fall through */

		case PPTP_SET_LINK_INFO:
			/* only need to NAT in case PAC is behind NAT box */
		case PPTP_START_SESSION_REQUEST:
		case PPTP_START_SESSION_REPLY:
		case PPTP_STOP_SESSION_REQUEST:
		case PPTP_STOP_SESSION_REPLY:
		case PPTP_ECHO_REQUEST:
		case PPTP_ECHO_REPLY:
			/* no need to alter packet */
			return NF_ACCEPT;
	}

	/* only OUT_CALL_REQUEST, IN_CALL_REPLY, CALL_CLEAR_REQUEST pass
	 * down to here */

	IP_NF_ASSERT(cid);

	DEBUGP("altering call id from 0x%04x to 0x%04x\n",
		ntohs(*cid), ntohs(new_callid));

	/* mangle packet */
	if (ip_nat_mangle_tcp_packet(pskb, ct, ctinfo,
		(void *)cid - ((void *)ctlh - sizeof(struct pptp_pkt_hdr)),
				 	sizeof(new_callid), 
					(char *)&new_callid,
				 	sizeof(new_callid)) == 0)
		return NF_DROP;

	return NF_ACCEPT;
}

static int
pptp_exp_gre(struct ip_conntrack_expect *expect_orig,
	     struct ip_conntrack_expect *expect_reply)
{
	struct ip_ct_pptp_master *ct_pptp_info = 
				&expect_orig->master->help.ct_pptp_info;
	struct ip_nat_pptp *nat_pptp_info = 
				&expect_orig->master->nat.help.nat_pptp_info;

	struct ip_conntrack *ct = expect_orig->master;

	struct ip_conntrack_tuple inv_t;
	struct ip_conntrack_tuple *orig_t, *reply_t;

	/* save original PAC call ID in nat_info */
	nat_pptp_info->pac_call_id = ct_pptp_info->pac_call_id;

	/* alter expectation */
	orig_t = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	reply_t = &ct->tuplehash[IP_CT_DIR_REPLY].tuple;

	/* alter expectation for PNS->PAC direction */
	invert_tuplepr(&inv_t, &expect_orig->tuple);
	expect_orig->saved_proto.gre.key = htons(nat_pptp_info->pac_call_id);
	expect_orig->tuple.src.u.gre.key = htons(nat_pptp_info->pns_call_id);
	expect_orig->tuple.dst.u.gre.key = htons(ct_pptp_info->pac_call_id);
	inv_t.src.ip = reply_t->src.ip;
	inv_t.dst.ip = reply_t->dst.ip;
	inv_t.src.u.gre.key = htons(nat_pptp_info->pac_call_id);
	inv_t.dst.u.gre.key = htons(ct_pptp_info->pns_call_id);

	if (!ip_conntrack_expect_related(expect_orig)) {
		DEBUGP("successfully registered expect\n");
	} else {
		DEBUGP("can't expect_related(expect_orig)\n");
		return 1;
	}

	/* alter expectation for PAC->PNS direction */
	invert_tuplepr(&inv_t, &expect_reply->tuple);
	expect_reply->saved_proto.gre.key = htons(nat_pptp_info->pns_call_id);
	expect_reply->tuple.src.u.gre.key = htons(nat_pptp_info->pac_call_id);
	expect_reply->tuple.dst.u.gre.key = htons(ct_pptp_info->pns_call_id);
	inv_t.src.ip = orig_t->src.ip;
	inv_t.dst.ip = orig_t->dst.ip;
	inv_t.src.u.gre.key = htons(nat_pptp_info->pns_call_id);
	inv_t.dst.u.gre.key = htons(ct_pptp_info->pac_call_id);

	if (!ip_conntrack_expect_related(expect_reply)) {
		DEBUGP("successfully registered expect\n");
	} else {
		DEBUGP("can't expect_related(expect_reply)\n");
		ip_conntrack_unexpect_related(expect_orig);
		return 1;
	}

	if (ip_ct_gre_keymap_add(ct, &expect_reply->tuple, 0) < 0) {
		DEBUGP("can't register original keymap\n");
		ip_conntrack_unexpect_related(expect_orig);
		ip_conntrack_unexpect_related(expect_reply);
		return 1;
	}

	if (ip_ct_gre_keymap_add(ct, &inv_t, 1) < 0) {
		DEBUGP("can't register reply keymap\n");
		ip_conntrack_unexpect_related(expect_orig);
		ip_conntrack_unexpect_related(expect_reply);
		ip_ct_gre_keymap_destroy(ct);
		return 1;
	}

	return 0;
}

/* inbound packets == from PAC to PNS */
static int
pptp_inbound_pkt(struct sk_buff **pskb,
		 struct ip_conntrack *ct,
		 enum ip_conntrack_info ctinfo,
		 struct PptpControlHeader *ctlh,
		 union pptp_ctrl_union *pptpReq)
{
	struct ip_nat_pptp *nat_pptp_info = &ct->nat.help.nat_pptp_info;
	u_int16_t msg, new_cid = 0, new_pcid, *pcid = NULL, *cid = NULL;

	int ret = NF_ACCEPT, rv;

	new_pcid = htons(nat_pptp_info->pns_call_id);

	switch (msg = ntohs(ctlh->messageType)) {
	case PPTP_OUT_CALL_REPLY:
		pcid = &pptpReq->ocack.peersCallID;	
		cid = &pptpReq->ocack.callID;
		break;
	case PPTP_IN_CALL_CONNECT:
		pcid = &pptpReq->iccon.peersCallID;
		break;
	case PPTP_IN_CALL_REQUEST:
		/* only need to nat in case PAC is behind NAT box */
		break;
	case PPTP_WAN_ERROR_NOTIFY:
		pcid = &pptpReq->wanerr.peersCallID;
		break;
	case PPTP_CALL_DISCONNECT_NOTIFY:
		pcid = &pptpReq->disc.callID;
		break;
	case PPTP_SET_LINK_INFO:
		pcid = &pptpReq->setlink.peersCallID;
		break;

	default:
		DEBUGP("unknown inbound packet %s\n", (msg <= PPTP_MSG_MAX)? 
			pptp_msg_name[msg]:pptp_msg_name[0]);
		/* fall through */

	case PPTP_START_SESSION_REQUEST:
	case PPTP_START_SESSION_REPLY:
	case PPTP_STOP_SESSION_REQUEST:
	case PPTP_STOP_SESSION_REPLY:
	case PPTP_ECHO_REQUEST:
	case PPTP_ECHO_REPLY:
		/* no need to alter packet */
		return NF_ACCEPT;
	}

	/* only OUT_CALL_REPLY, IN_CALL_CONNECT, IN_CALL_REQUEST,
	 * WAN_ERROR_NOTIFY, CALL_DISCONNECT_NOTIFY pass down here */

	/* mangle packet */
	IP_NF_ASSERT(pcid);
	DEBUGP("altering peer call id from 0x%04x to 0x%04x\n",
		ntohs(*pcid), ntohs(new_pcid));
	
	rv = ip_nat_mangle_tcp_packet(pskb, ct, ctinfo, 
				      (void *)pcid - ((void *)ctlh - sizeof(struct pptp_pkt_hdr)),
				      sizeof(new_pcid), (char *)&new_pcid, 
				      sizeof(new_pcid));
	if (rv != NF_ACCEPT) 
		return rv;

	if (new_cid) {
		IP_NF_ASSERT(cid);
		DEBUGP("altering call id from 0x%04x to 0x%04x\n",
			ntohs(*cid), ntohs(new_cid));
		rv = ip_nat_mangle_tcp_packet(pskb, ct, ctinfo, 
					      (void *)cid - ((void *)ctlh - sizeof(struct pptp_pkt_hdr)), 
					      sizeof(new_cid),
					      (char *)&new_cid, 
					      sizeof(new_cid));
		if (rv != NF_ACCEPT)
			return rv;
	}

	/* check for earlier return value of 'switch' above */
	if (ret != NF_ACCEPT)
		return ret;

	/* great, at least we don't need to resize packets */
	return NF_ACCEPT;
}


extern int __init ip_nat_proto_gre_init(void);
extern void __exit ip_nat_proto_gre_fini(void);

static int __init init(void)
{
	int ret;

	DEBUGP("%s: registering NAT helper\n", __FILE__);

	ret = ip_nat_proto_gre_init();
	if (ret < 0)
		return ret;

	BUG_ON(ip_nat_pptp_hook_outbound);
	ip_nat_pptp_hook_outbound = &pptp_outbound_pkt;

	BUG_ON(ip_nat_pptp_hook_inbound);
	ip_nat_pptp_hook_inbound = &pptp_inbound_pkt;

	BUG_ON(ip_nat_pptp_hook_exp_gre);
	ip_nat_pptp_hook_exp_gre = &pptp_exp_gre;

	BUG_ON(ip_nat_pptp_hook_expectfn);
	ip_nat_pptp_hook_expectfn = &pptp_nat_expected;

	printk("ip_nat_pptp version %s loaded\n", IP_NAT_PPTP_VERSION);
	return 0;
}

static void __exit fini(void)
{
	DEBUGP("cleanup_module\n" );

	ip_nat_pptp_hook_expectfn = NULL;
	ip_nat_pptp_hook_exp_gre = NULL;
	ip_nat_pptp_hook_inbound = NULL;
	ip_nat_pptp_hook_outbound = NULL;

	ip_nat_proto_gre_fini();
	/* Make sure noone calls it, meanwhile */
	synchronize_net();

	printk("ip_nat_pptp version %s unloaded\n", IP_NAT_PPTP_VERSION);
}

module_init(init);
module_exit(fini);
