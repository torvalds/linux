/*
 * ip_vs_nfct.c:	Netfilter connection tracking support for IPVS
 *
 * Portions Copyright (C) 2001-2002
 * Antefacto Ltd, 181 Parnell St, Dublin 1, Ireland.
 *
 * Portions Copyright (C) 2003-2010
 * Julian Anastasov
 *
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * Authors:
 * Ben North <ben@redfrontdoor.org>
 * Julian Anastasov <ja@ssi.bg>		Reorganize and sync with latest kernels
 * Hannes Eder <heder@google.com>	Extend NFCT support for FTP, ipvs match
 *
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
 *
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
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_zones.h>


#define FMT_TUPLE	"%pI4:%u->%pI4:%u/%u"
#define ARG_TUPLE(T)	&(T)->src.u3.ip, ntohs((T)->src.u.all), \
			&(T)->dst.u3.ip, ntohs((T)->dst.u.all), \
			(T)->dst.protonum

#define FMT_CONN	"%pI4:%u->%pI4:%u->%pI4:%u/%u:%u"
#define ARG_CONN(C)	&((C)->caddr.ip), ntohs((C)->cport), \
			&((C)->vaddr.ip), ntohs((C)->vport), \
			&((C)->daddr.ip), ntohs((C)->dport), \
			(C)->protocol, (C)->state

void
ip_vs_update_conntrack(struct sk_buff *skb, struct ip_vs_conn *cp, int outin)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = ct = nf_ct_get(skb, &ctinfo);
	struct nf_conntrack_tuple new_tuple;

	if (ct == NULL || nf_ct_is_confirmed(ct) || nf_ct_is_untracked(ct) ||
	    nf_ct_is_dying(ct))
		return;

	/* Never alter conntrack for non-NAT conns */
	if (IP_VS_FWD_METHOD(cp) != IP_VS_CONN_F_MASQ)
		return;

	/* Alter reply only in original direction */
	if (CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL)
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
	IP_VS_DBG(7, "%s: Updating conntrack ct=%p, status=0x%lX, "
		  "ctinfo=%d, old reply=" FMT_TUPLE
		  ", new reply=" FMT_TUPLE ", cp=" FMT_CONN "\n",
		  __func__, ct, ct->status, ctinfo,
		  ARG_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple),
		  ARG_TUPLE(&new_tuple), ARG_CONN(cp));
	nf_conntrack_alter_reply(ct, &new_tuple);
}

int ip_vs_confirm_conntrack(struct sk_buff *skb, struct ip_vs_conn *cp)
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

	if (exp->tuple.src.l3num != PF_INET)
		return;

	/*
	 * We assume that no NF locks are held before this callback.
	 * ip_vs_conn_out_get and ip_vs_conn_in_get should match their
	 * expectations even if they use wildcard values, now we provide the
	 * actual values from the newly created original conntrack direction.
	 * The conntrack is confirmed when packet reaches IPVS hooks.
	 */

	/* RS->CLIENT */
	orig = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	ip_vs_conn_fill_param(exp->tuple.src.l3num, orig->dst.protonum,
			      &orig->src.u3, orig->src.u.tcp.port,
			      &orig->dst.u3, orig->dst.u.tcp.port, &p);
	cp = ip_vs_conn_out_get(&p);
	if (cp) {
		/* Change reply CLIENT->RS to CLIENT->VS */
		new_reply = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
		IP_VS_DBG(7, "%s: ct=%p, status=0x%lX, tuples=" FMT_TUPLE ", "
			  FMT_TUPLE ", found inout cp=" FMT_CONN "\n",
			  __func__, ct, ct->status,
			  ARG_TUPLE(orig), ARG_TUPLE(&new_reply),
			  ARG_CONN(cp));
		new_reply.dst.u3 = cp->vaddr;
		new_reply.dst.u.tcp.port = cp->vport;
		IP_VS_DBG(7, "%s: ct=%p, new tuples=" FMT_TUPLE ", " FMT_TUPLE
			  ", inout cp=" FMT_CONN "\n",
			  __func__, ct,
			  ARG_TUPLE(orig), ARG_TUPLE(&new_reply),
			  ARG_CONN(cp));
		goto alter;
	}

	/* CLIENT->VS */
	cp = ip_vs_conn_in_get(&p);
	if (cp) {
		/* Change reply VS->CLIENT to RS->CLIENT */
		new_reply = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
		IP_VS_DBG(7, "%s: ct=%p, status=0x%lX, tuples=" FMT_TUPLE ", "
			  FMT_TUPLE ", found outin cp=" FMT_CONN "\n",
			  __func__, ct, ct->status,
			  ARG_TUPLE(orig), ARG_TUPLE(&new_reply),
			  ARG_CONN(cp));
		new_reply.src.u3 = cp->daddr;
		new_reply.src.u.tcp.port = cp->dport;
		IP_VS_DBG(7, "%s: ct=%p, new tuples=" FMT_TUPLE ", "
			  FMT_TUPLE ", outin cp=" FMT_CONN "\n",
			  __func__, ct,
			  ARG_TUPLE(orig), ARG_TUPLE(&new_reply),
			  ARG_CONN(cp));
		goto alter;
	}

	IP_VS_DBG(7, "%s: ct=%p, status=0x%lX, tuple=" FMT_TUPLE
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

	if (ct == NULL || nf_ct_is_untracked(ct))
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

	IP_VS_DBG(7, "%s: ct=%p, expect tuple=" FMT_TUPLE "\n",
		__func__, ct, ARG_TUPLE(&exp->tuple));
	nf_ct_expect_related(exp);
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

	IP_VS_DBG(7, "%s: dropping conntrack with tuple=" FMT_TUPLE
		" for conn " FMT_CONN "\n",
		__func__, ARG_TUPLE(&tuple), ARG_CONN(cp));

	h = nf_conntrack_find_get(&init_net, NF_CT_DEFAULT_ZONE, &tuple);
	if (h) {
		ct = nf_ct_tuplehash_to_ctrack(h);
		/* Show what happens instead of calling nf_ct_kill() */
		if (del_timer(&ct->timeout)) {
			IP_VS_DBG(7, "%s: ct=%p, deleted conntrack timer for tuple="
				FMT_TUPLE "\n",
				__func__, ct, ARG_TUPLE(&tuple));
			if (ct->timeout.function)
				ct->timeout.function(ct->timeout.data);
		} else {
			IP_VS_DBG(7, "%s: ct=%p, no conntrack timer for tuple="
				FMT_TUPLE "\n",
				__func__, ct, ARG_TUPLE(&tuple));
		}
		nf_ct_put(ct);
	} else {
		IP_VS_DBG(7, "%s: no conntrack for tuple=" FMT_TUPLE "\n",
			__func__, ARG_TUPLE(&tuple));
	}
}

