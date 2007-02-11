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

#define REQ_CID(req, off)		(*(__be16 *)((char *)(req) + (off)))

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
	struct ip_nat_range range;

	ct_pptp_info = &master->help.ct_pptp_info;
	nat_pptp_info = &master->nat.help.nat_pptp_info;

	/* And here goes the grand finale of corrosion... */

	if (exp->dir == IP_CT_DIR_ORIGINAL) {
		DEBUGP("we are PNS->PAC\n");
		/* therefore, build tuple for PAC->PNS */
		t.src.ip = master->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip;
		t.src.u.gre.key = master->help.ct_pptp_info.pac_call_id;
		t.dst.ip = master->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip;
		t.dst.u.gre.key = master->help.ct_pptp_info.pns_call_id;
		t.dst.protonum = IPPROTO_GRE;
	} else {
		DEBUGP("we are PAC->PNS\n");
		/* build tuple for PNS->PAC */
		t.src.ip = master->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
		t.src.u.gre.key = master->nat.help.nat_pptp_info.pns_call_id;
		t.dst.ip = master->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip;
		t.dst.u.gre.key = master->nat.help.nat_pptp_info.pac_call_id;
		t.dst.protonum = IPPROTO_GRE;
	}

	DEBUGP("trying to unexpect other dir: ");
	DUMP_TUPLE(&t);
	other_exp = ip_conntrack_expect_find_get(&t);
	if (other_exp) {
		ip_conntrack_unexpect_related(other_exp);
		ip_conntrack_expect_put(other_exp);
		DEBUGP("success\n");
	} else {
		DEBUGP("not found!\n");
	}

	/* This must be a fresh one. */
	BUG_ON(ct->status & IPS_NAT_DONE_MASK);

	/* Change src to where master sends to */
	range.flags = IP_NAT_RANGE_MAP_IPS;
	range.min_ip = range.max_ip
		= ct->master->tuplehash[!exp->dir].tuple.dst.ip;
	if (exp->dir == IP_CT_DIR_ORIGINAL) {
		range.flags |= IP_NAT_RANGE_PROTO_SPECIFIED;
		range.min = range.max = exp->saved_proto;
	}
	/* hook doesn't matter, but it has to do source manip */
	ip_nat_setup_info(ct, &range, NF_IP_POST_ROUTING);

	/* For DST manip, map port here to where it's expected. */
	range.flags = IP_NAT_RANGE_MAP_IPS;
	range.min_ip = range.max_ip
		= ct->master->tuplehash[!exp->dir].tuple.src.ip;
	if (exp->dir == IP_CT_DIR_REPLY) {
		range.flags |= IP_NAT_RANGE_PROTO_SPECIFIED;
		range.min = range.max = exp->saved_proto;
	}
	/* hook doesn't matter, but it has to do destination manip */
	ip_nat_setup_info(ct, &range, NF_IP_PRE_ROUTING);
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
	u_int16_t msg;
	__be16 new_callid;
	unsigned int cid_off;

	new_callid = ct_pptp_info->pns_call_id;

	switch (msg = ntohs(ctlh->messageType)) {
	case PPTP_OUT_CALL_REQUEST:
		cid_off = offsetof(union pptp_ctrl_union, ocreq.callID);
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
		ct_pptp_info->pns_call_id = new_callid;
		break;
	case PPTP_IN_CALL_REPLY:
		cid_off = offsetof(union pptp_ctrl_union, icack.callID);
		break;
	case PPTP_CALL_CLEAR_REQUEST:
		cid_off = offsetof(union pptp_ctrl_union, clrreq.callID);
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
	DEBUGP("altering call id from 0x%04x to 0x%04x\n",
		ntohs(REQ_CID(pptpReq, cid_off)), ntohs(new_callid));

	/* mangle packet */
	if (ip_nat_mangle_tcp_packet(pskb, ct, ctinfo,
				     cid_off + sizeof(struct pptp_pkt_hdr) +
				     sizeof(struct PptpControlHeader),
				     sizeof(new_callid), (char *)&new_callid,
				     sizeof(new_callid)) == 0)
		return NF_DROP;

	return NF_ACCEPT;
}

static void
pptp_exp_gre(struct ip_conntrack_expect *expect_orig,
	     struct ip_conntrack_expect *expect_reply)
{
	struct ip_conntrack *ct = expect_orig->master;
	struct ip_ct_pptp_master *ct_pptp_info = &ct->help.ct_pptp_info;
	struct ip_nat_pptp *nat_pptp_info = &ct->nat.help.nat_pptp_info;

	/* save original PAC call ID in nat_info */
	nat_pptp_info->pac_call_id = ct_pptp_info->pac_call_id;

	/* alter expectation for PNS->PAC direction */
	expect_orig->saved_proto.gre.key = ct_pptp_info->pns_call_id;
	expect_orig->tuple.src.u.gre.key = nat_pptp_info->pns_call_id;
	expect_orig->tuple.dst.u.gre.key = ct_pptp_info->pac_call_id;
	expect_orig->dir = IP_CT_DIR_ORIGINAL;

	/* alter expectation for PAC->PNS direction */
	expect_reply->saved_proto.gre.key = nat_pptp_info->pns_call_id;
	expect_reply->tuple.src.u.gre.key = nat_pptp_info->pac_call_id;
	expect_reply->tuple.dst.u.gre.key = ct_pptp_info->pns_call_id;
	expect_reply->dir = IP_CT_DIR_REPLY;
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
	u_int16_t msg;
	__be16 new_pcid;
	unsigned int pcid_off;

	new_pcid = nat_pptp_info->pns_call_id;

	switch (msg = ntohs(ctlh->messageType)) {
	case PPTP_OUT_CALL_REPLY:
		pcid_off = offsetof(union pptp_ctrl_union, ocack.peersCallID);
		break;
	case PPTP_IN_CALL_CONNECT:
		pcid_off = offsetof(union pptp_ctrl_union, iccon.peersCallID);
		break;
	case PPTP_IN_CALL_REQUEST:
		/* only need to nat in case PAC is behind NAT box */
		return NF_ACCEPT;
	case PPTP_WAN_ERROR_NOTIFY:
		pcid_off = offsetof(union pptp_ctrl_union, wanerr.peersCallID);
		break;
	case PPTP_CALL_DISCONNECT_NOTIFY:
		pcid_off = offsetof(union pptp_ctrl_union, disc.callID);
		break;
	case PPTP_SET_LINK_INFO:
		pcid_off = offsetof(union pptp_ctrl_union, setlink.peersCallID);
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
	DEBUGP("altering peer call id from 0x%04x to 0x%04x\n",
		ntohs(REQ_CID(pptpReq, pcid_off)), ntohs(new_pcid));

	if (ip_nat_mangle_tcp_packet(pskb, ct, ctinfo,
				     pcid_off + sizeof(struct pptp_pkt_hdr) +
				     sizeof(struct PptpControlHeader),
				     sizeof(new_pcid), (char *)&new_pcid,
				     sizeof(new_pcid)) == 0)
		return NF_DROP;
	return NF_ACCEPT;
}


extern int __init ip_nat_proto_gre_init(void);
extern void __exit ip_nat_proto_gre_fini(void);

static int __init ip_nat_helper_pptp_init(void)
{
	int ret;

	DEBUGP("%s: registering NAT helper\n", __FILE__);

	ret = ip_nat_proto_gre_init();
	if (ret < 0)
		return ret;

	BUG_ON(rcu_dereference(ip_nat_pptp_hook_outbound));
	rcu_assign_pointer(ip_nat_pptp_hook_outbound, pptp_outbound_pkt);

	BUG_ON(rcu_dereference(ip_nat_pptp_hook_inbound));
	rcu_assign_pointer(ip_nat_pptp_hook_inbound, pptp_inbound_pkt);

	BUG_ON(rcu_dereference(ip_nat_pptp_hook_exp_gre));
	rcu_assign_pointer(ip_nat_pptp_hook_exp_gre, pptp_exp_gre);

	BUG_ON(rcu_dereference(ip_nat_pptp_hook_expectfn));
	rcu_assign_pointer(ip_nat_pptp_hook_expectfn, pptp_nat_expected);

	printk("ip_nat_pptp version %s loaded\n", IP_NAT_PPTP_VERSION);
	return 0;
}

static void __exit ip_nat_helper_pptp_fini(void)
{
	DEBUGP("cleanup_module\n" );

	rcu_assign_pointer(ip_nat_pptp_hook_expectfn, NULL);
	rcu_assign_pointer(ip_nat_pptp_hook_exp_gre, NULL);
	rcu_assign_pointer(ip_nat_pptp_hook_inbound, NULL);
	rcu_assign_pointer(ip_nat_pptp_hook_outbound, NULL);
	synchronize_rcu();

	ip_nat_proto_gre_fini();

	printk("ip_nat_pptp version %s unloaded\n", IP_NAT_PPTP_VERSION);
}

module_init(ip_nat_helper_pptp_init);
module_exit(ip_nat_helper_pptp_fini);
