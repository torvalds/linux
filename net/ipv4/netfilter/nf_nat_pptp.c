/*
 * nf_nat_pptp.c
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
 */

#include <linux/module.h>
#include <linux/tcp.h>

#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/nf_nat_rule.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <linux/netfilter/nf_conntrack_proto_gre.h>
#include <linux/netfilter/nf_conntrack_pptp.h>

#define NF_NAT_PPTP_VERSION "3.0"

#define REQ_CID(req, off)		(*(__be16 *)((char *)(req) + (off)))

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("Netfilter NAT helper module for PPTP");
MODULE_ALIAS("ip_nat_pptp");

static void pptp_nat_expected(struct nf_conn *ct,
			      struct nf_conntrack_expect *exp)
{
	struct net *net = nf_ct_net(ct);
	const struct nf_conn *master = ct->master;
	struct nf_conntrack_expect *other_exp;
	struct nf_conntrack_tuple t;
	const struct nf_ct_pptp_master *ct_pptp_info;
	const struct nf_nat_pptp *nat_pptp_info;
	struct nf_nat_ipv4_range range;

	ct_pptp_info = nfct_help_data(master);
	nat_pptp_info = &nfct_nat(master)->help.nat_pptp_info;

	/* And here goes the grand finale of corrosion... */
	if (exp->dir == IP_CT_DIR_ORIGINAL) {
		pr_debug("we are PNS->PAC\n");
		/* therefore, build tuple for PAC->PNS */
		t.src.l3num = AF_INET;
		t.src.u3.ip = master->tuplehash[!exp->dir].tuple.src.u3.ip;
		t.src.u.gre.key = ct_pptp_info->pac_call_id;
		t.dst.u3.ip = master->tuplehash[!exp->dir].tuple.dst.u3.ip;
		t.dst.u.gre.key = ct_pptp_info->pns_call_id;
		t.dst.protonum = IPPROTO_GRE;
	} else {
		pr_debug("we are PAC->PNS\n");
		/* build tuple for PNS->PAC */
		t.src.l3num = AF_INET;
		t.src.u3.ip = master->tuplehash[!exp->dir].tuple.src.u3.ip;
		t.src.u.gre.key = nat_pptp_info->pns_call_id;
		t.dst.u3.ip = master->tuplehash[!exp->dir].tuple.dst.u3.ip;
		t.dst.u.gre.key = nat_pptp_info->pac_call_id;
		t.dst.protonum = IPPROTO_GRE;
	}

	pr_debug("trying to unexpect other dir: ");
	nf_ct_dump_tuple_ip(&t);
	other_exp = nf_ct_expect_find_get(net, nf_ct_zone(ct), &t);
	if (other_exp) {
		nf_ct_unexpect_related(other_exp);
		nf_ct_expect_put(other_exp);
		pr_debug("success\n");
	} else {
		pr_debug("not found!\n");
	}

	/* This must be a fresh one. */
	BUG_ON(ct->status & IPS_NAT_DONE_MASK);

	/* Change src to where master sends to */
	range.flags = NF_NAT_RANGE_MAP_IPS;
	range.min_ip = range.max_ip
		= ct->master->tuplehash[!exp->dir].tuple.dst.u3.ip;
	if (exp->dir == IP_CT_DIR_ORIGINAL) {
		range.flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
		range.min = range.max = exp->saved_proto;
	}
	nf_nat_setup_info(ct, &range, NF_NAT_MANIP_SRC);

	/* For DST manip, map port here to where it's expected. */
	range.flags = NF_NAT_RANGE_MAP_IPS;
	range.min_ip = range.max_ip
		= ct->master->tuplehash[!exp->dir].tuple.src.u3.ip;
	if (exp->dir == IP_CT_DIR_REPLY) {
		range.flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
		range.min = range.max = exp->saved_proto;
	}
	nf_nat_setup_info(ct, &range, NF_NAT_MANIP_DST);
}

/* outbound packets == from PNS to PAC */
static int
pptp_outbound_pkt(struct sk_buff *skb,
		  struct nf_conn *ct,
		  enum ip_conntrack_info ctinfo,
		  struct PptpControlHeader *ctlh,
		  union pptp_ctrl_union *pptpReq)

{
	struct nf_ct_pptp_master *ct_pptp_info;
	struct nf_nat_pptp *nat_pptp_info;
	u_int16_t msg;
	__be16 new_callid;
	unsigned int cid_off;

	ct_pptp_info = nfct_help_data(ct);
	nat_pptp_info = &nfct_nat(ct)->help.nat_pptp_info;

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
		pr_debug("unknown outbound packet 0x%04x:%s\n", msg,
			 msg <= PPTP_MSG_MAX ? pptp_msg_name[msg] :
					       pptp_msg_name[0]);
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
	pr_debug("altering call id from 0x%04x to 0x%04x\n",
		 ntohs(REQ_CID(pptpReq, cid_off)), ntohs(new_callid));

	/* mangle packet */
	if (nf_nat_mangle_tcp_packet(skb, ct, ctinfo,
				     cid_off + sizeof(struct pptp_pkt_hdr) +
				     sizeof(struct PptpControlHeader),
				     sizeof(new_callid), (char *)&new_callid,
				     sizeof(new_callid)) == 0)
		return NF_DROP;
	return NF_ACCEPT;
}

static void
pptp_exp_gre(struct nf_conntrack_expect *expect_orig,
	     struct nf_conntrack_expect *expect_reply)
{
	const struct nf_conn *ct = expect_orig->master;
	struct nf_ct_pptp_master *ct_pptp_info;
	struct nf_nat_pptp *nat_pptp_info;

	ct_pptp_info = nfct_help_data(ct);
	nat_pptp_info = &nfct_nat(ct)->help.nat_pptp_info;

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
pptp_inbound_pkt(struct sk_buff *skb,
		 struct nf_conn *ct,
		 enum ip_conntrack_info ctinfo,
		 struct PptpControlHeader *ctlh,
		 union pptp_ctrl_union *pptpReq)
{
	const struct nf_nat_pptp *nat_pptp_info;
	u_int16_t msg;
	__be16 new_pcid;
	unsigned int pcid_off;

	nat_pptp_info = &nfct_nat(ct)->help.nat_pptp_info;
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
		pr_debug("unknown inbound packet %s\n",
			 msg <= PPTP_MSG_MAX ? pptp_msg_name[msg] :
					       pptp_msg_name[0]);
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
	pr_debug("altering peer call id from 0x%04x to 0x%04x\n",
		 ntohs(REQ_CID(pptpReq, pcid_off)), ntohs(new_pcid));

	if (nf_nat_mangle_tcp_packet(skb, ct, ctinfo,
				     pcid_off + sizeof(struct pptp_pkt_hdr) +
				     sizeof(struct PptpControlHeader),
				     sizeof(new_pcid), (char *)&new_pcid,
				     sizeof(new_pcid)) == 0)
		return NF_DROP;
	return NF_ACCEPT;
}

static int __init nf_nat_helper_pptp_init(void)
{
	nf_nat_need_gre();

	BUG_ON(nf_nat_pptp_hook_outbound != NULL);
	RCU_INIT_POINTER(nf_nat_pptp_hook_outbound, pptp_outbound_pkt);

	BUG_ON(nf_nat_pptp_hook_inbound != NULL);
	RCU_INIT_POINTER(nf_nat_pptp_hook_inbound, pptp_inbound_pkt);

	BUG_ON(nf_nat_pptp_hook_exp_gre != NULL);
	RCU_INIT_POINTER(nf_nat_pptp_hook_exp_gre, pptp_exp_gre);

	BUG_ON(nf_nat_pptp_hook_expectfn != NULL);
	RCU_INIT_POINTER(nf_nat_pptp_hook_expectfn, pptp_nat_expected);
	return 0;
}

static void __exit nf_nat_helper_pptp_fini(void)
{
	RCU_INIT_POINTER(nf_nat_pptp_hook_expectfn, NULL);
	RCU_INIT_POINTER(nf_nat_pptp_hook_exp_gre, NULL);
	RCU_INIT_POINTER(nf_nat_pptp_hook_inbound, NULL);
	RCU_INIT_POINTER(nf_nat_pptp_hook_outbound, NULL);
	synchronize_rcu();
}

module_init(nf_nat_helper_pptp_init);
module_exit(nf_nat_helper_pptp_fini);
