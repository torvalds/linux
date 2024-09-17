// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ip_vs_nfct.c:	Netfilter connection tracking support for IPVS
 *
 * Portions Copyright (C) 2001-2002
 * Antefacto Ltd, 181 Parnell St, Dublin 1, Ireland.
 *
 * Portions Copyright (C) 2003-2010
 * Julian Anastasov
 *
 * Authors:
 * Ben North <ben@redfrontdoor.org>
 * Julian Anastasov <ja@ssi.bg>		Reorganize and sync with latest kernels
 * Hannes Eder <heder@google.com>	Extend NFCT support for FTP, ipvs match
 *
 * Current status:
 *
 * - provide conntrack confirmation for new and related connections, by
 * this way we can see their proper conntrack state in all hooks
 * - support for all forwarding methods, not only NAT
 * - FTP support (NAT), ability to support other NAT apps with expectations
 * - to correctly create expectations for related NAT connections the proper
 * NF conntrack support must be already installed, eg. ip_vs_ftp requires
 * nf_conntrack_ftp ... iptables_nat for the same ports (but no iptables
 * NAT rules are needed)
 * - alter reply for NAT when forwarding packet in original direction:
 * conntrack from client in NEW or RELATED (Passive FTP DATA) state or
 * when RELATED conntrack is created from real server (Active FTP DATA)
 * - if iptables_nat is not loaded the Passive FTP will not work (the
 * PASV response can not be NAT-ed) but Active FTP should work
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/vmalloc.h>
#include <linux/skbuff.h>
#include <net/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <net/ip_vs.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_zones.h>


#define FMT_TUPLE	"%s:%u->%s:%u/%u"
#define ARG_TUPLE(T)	IP_VS_DBG_ADDR((T)->src.l3num, &(T)->src.u3),	\
			ntohs((T)->src.u.all),				\
			IP_VS_DBG_ADDR((T)->src.l3num, &(T)->dst.u3),	\
			ntohs((T)->dst.u.all),				\
			(T)->dst.protonum

#define FMT_CONN	"%s:%u->%s:%u->%s:%u/%u:%u"
#define ARG_CONN(C)	IP_VS_DBG_ADDR((C)->af, &((C)->caddr)),		\
			ntohs((C)->cport),				\
			IP_VS_DBG_ADDR((C)->af, &((C)->vaddr)),		\
			ntohs((C)->vport),				\
			IP_VS_DBG_ADDR((C)->daf, &((C)->daddr)),	\
			ntohs((C)->dport),				\
			(C)->protocol, (C)->state

void
ip_vs_update_conntrack(struct sk_buff *skb, struct ip_vs_conn *cp, int outin)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct nf_conntrack_tuple new_tuple;

	if (ct == NULL || nf_ct_is_confirmed(ct) ||
	    nf_ct_is_dying(ct))
		return;

	/* Never alter conntrack for non-NAT conns */
	if (IP_VS_FWD_METHOD(cp) != IP_VS_CONN_F_MASQ)
		return;

	/* Never alter conntrack for OPS conns (no reply is expected) */
	if (cp->flags & IP_VS_CONN_F_ONE_PACKET)
		return;

	/* Alter reply only in original direction */
	if (CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL)
		return;

	/* Applications may adjust TCP seqs */
	if (cp->app && nf_ct_protonum(ct) == IPPROTO_TCP &&
	    !nfct_seqadj(ct) && !nfct_seqadj_ext_add(ct))
		return;

	/*
	 * The connection is not yet in the hashtable, so we update it.
	 * CIP->VIP will remain the same, so leave the tuple in
	 * IP_CT_DIR_ORIGINAL untouched.  When the reply comes back from the
	 * real-server we will see RIP->DIP.
	 */
	new_tuple = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
	/*
	 * This will also take care of UDP and other protocols.
	 */
	if (outin) {
		new_tuple.src.u3 = cp->daddr;
		if (new_tuple.dst.protonum != IPPROTO_ICMP &&
		    new_tuple.dst.protonum != IPPROTO_ICMPV6)
			new_tuple.src.u.tcp.port = cp->dport;
	} else {
		new_tuple.dst.u3 = cp->vaddr;
		if (new_tuple.dst.protonum != IPPROTO_ICMP &&
		    new_tuple.dst.protonum != IPPROTO_ICMPV6)
			new_tuple.dst.u.tcp.port = cp->vport;
	}
	IP_VS_DBG_BUF(7, "%s: Updating conntrack ct=%p, status=0x%lX, "
		      "ctinfo=%d, old reply=" FMT_TUPLE "\n",
		      __func__, ct, ct->status, ctinfo,
		      ARG_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple));
	IP_VS_DBG_BUF(7, "%s: Updating conntrack ct=%p, status=0x%lX, "
		      "ctinfo=%d, new reply=" FMT_TUPLE "\n",
		      __func__, ct, ct->status, ctinfo,
		      ARG_TUPLE(&new_tuple));
	nf_conntrack_alter_reply(ct, &new_tuple);
	IP_VS_DBG_BUF(7, "%s: Updated conntrack ct=%p for cp=" FMT_CONN "\n",
		      __func__, ct, ARG_CONN(cp));
}

int ip_vs_confirm_conntrack(struct sk_buff *skb)
{
	return nf_conntrack_confirm(skb);
}

/*
 * Called from init_conntrack() as expectfn handler.
 */
static void ip_vs_nfct_expect_callback(struct nf_conn *ct,
	struct nf_conntrack_expect *exp)
{
	struct nf_conntrack_tuple *orig, new_reply;
	struct ip_vs_conn *cp;
	struct ip_vs_conn_param p;
	struct net *net = nf_ct_net(ct);

	/*
	 * We assume that no NF locks are held before this callback.
	 * ip_vs_conn_out_get and ip_vs_conn_in_get should match their
	 * expectations even if they use wildcard values, now we provide the
	 * actual values from the newly created original conntrack direction.
	 * The conntrack is confirmed when packet reaches IPVS hooks.
	 */

	/* RS->CLIENT */
	orig = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	ip_vs_conn_fill_param(net_ipvs(net), exp->tuple.src.l3num, orig->dst.protonum,
			      &orig->src.u3, orig->src.u.tcp.port,
			      &orig->dst.u3, orig->dst.u.tcp.port, &p);
	cp = ip_vs_conn_out_get(&p);
	if (cp) {
		/* Change reply CLIENT->RS to CLIENT->VS */
		IP_VS_DBG_BUF(7, "%s: for ct=%p, status=0x%lX found inout cp="
			      FMT_CONN "\n",
			      __func__, ct, ct->status, ARG_CONN(cp));
		new_reply = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
		IP_VS_DBG_BUF(7, "%s: ct=%p before alter: reply tuple="
			      FMT_TUPLE "\n",
			      __func__, ct, ARG_TUPLE(&new_reply));
		new_reply.dst.u3 = cp->vaddr;
		new_reply.dst.u.tcp.port = cp->vport;
		goto alter;
	}

	/* CLIENT->VS */
	cp = ip_vs_conn_in_get(&p);
	if (cp) {
		/* Change reply VS->CLIENT to RS->CLIENT */
		IP_VS_DBG_BUF(7, "%s: for ct=%p, status=0x%lX found outin cp="
			      FMT_CONN "\n",
			      __func__, ct, ct->status, ARG_CONN(cp));
		new_reply = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
		IP_VS_DBG_BUF(7, "%s: ct=%p before alter: reply tuple="
			      FMT_TUPLE "\n",
			      __func__, ct, ARG_TUPLE(&new_reply));
		new_reply.src.u3 = cp->daddr;
		new_reply.src.u.tcp.port = cp->dport;
		goto alter;
	}

	IP_VS_DBG_BUF(7, "%s: ct=%p, status=0x%lX, tuple=" FMT_TUPLE
		      " - unknown expect\n",
		      __func__, ct, ct->status, ARG_TUPLE(orig));
	return;

alter:
	/* Never alter conntrack for non-NAT conns */
	if (IP_VS_FWD_METHOD(cp) == IP_VS_CONN_F_MASQ)
		nf_conntrack_alter_reply(ct, &new_reply);
	ip_vs_conn_put(cp);
	return;
}

/*
 * Create NF conntrack expectation with wildcard (optional) source port.
 * Then the default callback function will alter the reply and will confirm
 * the conntrack entry when the first packet comes.
 * Use port 0 to expect connection from any port.
 */
void ip_vs_nfct_expect_related(struct sk_buff *skb, struct nf_conn *ct,
			       struct ip_vs_conn *cp, u_int8_t proto,
			       const __be16 port, int from_rs)
{
	struct nf_conntrack_expect *exp;

	if (ct == NULL)
		return;

	exp = nf_ct_expect_alloc(ct);
	if (!exp)
		return;

	nf_ct_expect_init(exp, NF_CT_EXPECT_CLASS_DEFAULT, nf_ct_l3num(ct),
			from_rs ? &cp->daddr : &cp->caddr,
			from_rs ? &cp->caddr : &cp->vaddr,
			proto, port ? &port : NULL,
			from_rs ? &cp->cport : &cp->vport);

	exp->expectfn = ip_vs_nfct_expect_callback;

	IP_VS_DBG_BUF(7, "%s: ct=%p, expect tuple=" FMT_TUPLE "\n",
		      __func__, ct, ARG_TUPLE(&exp->tuple));
	nf_ct_expect_related(exp, 0);
	nf_ct_expect_put(exp);
}
EXPORT_SYMBOL(ip_vs_nfct_expect_related);

/*
 * Our connection was terminated, try to drop the conntrack immediately
 */
void ip_vs_conn_drop_conntrack(struct ip_vs_conn *cp)
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;
	struct nf_conntrack_tuple tuple;

	if (!cp->cport)
		return;

	tuple = (struct nf_conntrack_tuple) {
		.dst = { .protonum = cp->protocol, .dir = IP_CT_DIR_ORIGINAL } };
	tuple.src.u3 = cp->caddr;
	tuple.src.u.all = cp->cport;
	tuple.src.l3num = cp->af;
	tuple.dst.u3 = cp->vaddr;
	tuple.dst.u.all = cp->vport;

	IP_VS_DBG_BUF(7, "%s: dropping conntrack for conn " FMT_CONN "\n",
		      __func__, ARG_CONN(cp));

	h = nf_conntrack_find_get(cp->ipvs->net, &nf_ct_zone_dflt, &tuple);
	if (h) {
		ct = nf_ct_tuplehash_to_ctrack(h);
		if (nf_ct_kill(ct)) {
			IP_VS_DBG_BUF(7, "%s: ct=%p deleted for tuple="
				      FMT_TUPLE "\n",
				      __func__, ct, ARG_TUPLE(&tuple));
		} else {
			IP_VS_DBG_BUF(7, "%s: ct=%p, no conntrack for tuple="
				      FMT_TUPLE "\n",
				      __func__, ct, ARG_TUPLE(&tuple));
		}
		nf_ct_put(ct);
	} else {
		IP_VS_DBG_BUF(7, "%s: no conntrack for tuple=" FMT_TUPLE "\n",
			      __func__, ARG_TUPLE(&tuple));
	}
}

