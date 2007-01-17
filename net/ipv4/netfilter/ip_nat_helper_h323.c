/*
 * H.323 extension for NAT alteration.
 *
 * Copyright (c) 2006 Jing Min Zhao <zhaojingmin@users.sourceforge.net>
 *
 * This source code is licensed under General Public License version 2.
 *
 * Based on the 'brute force' H.323 NAT module by
 * Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 */

#include <linux/module.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/moduleparam.h>
#include <net/tcp.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_conntrack_tuple.h>
#include <linux/netfilter_ipv4/ip_conntrack_h323.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/****************************************************************************/
static int set_addr(struct sk_buff **pskb,
		    unsigned char **data, int dataoff,
		    unsigned int addroff, __be32 ip, u_int16_t port)
{
	enum ip_conntrack_info ctinfo;
	struct ip_conntrack *ct = ip_conntrack_get(*pskb, &ctinfo);
	struct {
		__be32 ip;
		__be16 port;
	} __attribute__ ((__packed__)) buf;
	struct tcphdr _tcph, *th;

	buf.ip = ip;
	buf.port = htons(port);
	addroff += dataoff;

	if ((*pskb)->nh.iph->protocol == IPPROTO_TCP) {
		if (!ip_nat_mangle_tcp_packet(pskb, ct, ctinfo,
					      addroff, sizeof(buf),
					      (char *) &buf, sizeof(buf))) {
			if (net_ratelimit())
				printk("ip_nat_h323: ip_nat_mangle_tcp_packet"
				       " error\n");
			return -1;
		}

		/* Relocate data pointer */
		th = skb_header_pointer(*pskb, (*pskb)->nh.iph->ihl * 4,
					sizeof(_tcph), &_tcph);
		if (th == NULL)
			return -1;
		*data = (*pskb)->data + (*pskb)->nh.iph->ihl * 4 +
		    th->doff * 4 + dataoff;
	} else {
		if (!ip_nat_mangle_udp_packet(pskb, ct, ctinfo,
					      addroff, sizeof(buf),
					      (char *) &buf, sizeof(buf))) {
			if (net_ratelimit())
				printk("ip_nat_h323: ip_nat_mangle_udp_packet"
				       " error\n");
			return -1;
		}
		/* ip_nat_mangle_udp_packet uses skb_make_writable() to copy
		 * or pull everything in a linear buffer, so we can safely
		 * use the skb pointers now */
		*data = (*pskb)->data + (*pskb)->nh.iph->ihl * 4 +
		    sizeof(struct udphdr);
	}

	return 0;
}

/****************************************************************************/
static int set_h225_addr(struct sk_buff **pskb,
			 unsigned char **data, int dataoff,
			 TransportAddress * addr,
			 __be32 ip, u_int16_t port)
{
	return set_addr(pskb, data, dataoff, addr->ipAddress.ip, ip, port);
}

/****************************************************************************/
static int set_h245_addr(struct sk_buff **pskb,
			 unsigned char **data, int dataoff,
			 H245_TransportAddress * addr,
			 __be32 ip, u_int16_t port)
{
	return set_addr(pskb, data, dataoff,
			addr->unicastAddress.iPAddress.network, ip, port);
}

/****************************************************************************/
static int set_sig_addr(struct sk_buff **pskb, struct ip_conntrack *ct,
			enum ip_conntrack_info ctinfo,
			unsigned char **data,
			TransportAddress * addr, int count)
{
	struct ip_ct_h323_master *info = &ct->help.ct_h323_info;
	int dir = CTINFO2DIR(ctinfo);
	int i;
	__be32 ip;
	u_int16_t port;

	for (i = 0; i < count; i++) {
		if (get_h225_addr(*data, &addr[i], &ip, &port)) {
			if (ip == ct->tuplehash[dir].tuple.src.ip &&
			    port == info->sig_port[dir]) {
				/* GW->GK */

				/* Fix for Gnomemeeting */
				if (i > 0 &&
				    get_h225_addr(*data, &addr[0],
						  &ip, &port) &&
				    (ntohl(ip) & 0xff000000) == 0x7f000000)
					i = 0;

				DEBUGP
				    ("ip_nat_ras: set signal address "
				     "%u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
				     NIPQUAD(ip), port,
				     NIPQUAD(ct->tuplehash[!dir].tuple.dst.
					     ip), info->sig_port[!dir]);
				return set_h225_addr(pskb, data, 0, &addr[i],
						     ct->tuplehash[!dir].
						     tuple.dst.ip,
						     info->sig_port[!dir]);
			} else if (ip == ct->tuplehash[dir].tuple.dst.ip &&
				   port == info->sig_port[dir]) {
				/* GK->GW */
				DEBUGP
				    ("ip_nat_ras: set signal address "
				     "%u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
				     NIPQUAD(ip), port,
				     NIPQUAD(ct->tuplehash[!dir].tuple.src.
					     ip), info->sig_port[!dir]);
				return set_h225_addr(pskb, data, 0, &addr[i],
						     ct->tuplehash[!dir].
						     tuple.src.ip,
						     info->sig_port[!dir]);
			}
		}
	}

	return 0;
}

/****************************************************************************/
static int set_ras_addr(struct sk_buff **pskb, struct ip_conntrack *ct,
			enum ip_conntrack_info ctinfo,
			unsigned char **data,
			TransportAddress * addr, int count)
{
	int dir = CTINFO2DIR(ctinfo);
	int i;
	__be32 ip;
	u_int16_t port;

	for (i = 0; i < count; i++) {
		if (get_h225_addr(*data, &addr[i], &ip, &port) &&
		    ip == ct->tuplehash[dir].tuple.src.ip &&
		    port == ntohs(ct->tuplehash[dir].tuple.src.u.udp.port)) {
			DEBUGP("ip_nat_ras: set rasAddress "
			       "%u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
			       NIPQUAD(ip), port,
			       NIPQUAD(ct->tuplehash[!dir].tuple.dst.ip),
			       ntohs(ct->tuplehash[!dir].tuple.dst.u.udp.
				     port));
			return set_h225_addr(pskb, data, 0, &addr[i],
					     ct->tuplehash[!dir].tuple.dst.ip,
					     ntohs(ct->tuplehash[!dir].tuple.
						   dst.u.udp.port));
		}
	}

	return 0;
}

/****************************************************************************/
static int nat_rtp_rtcp(struct sk_buff **pskb, struct ip_conntrack *ct,
			enum ip_conntrack_info ctinfo,
			unsigned char **data, int dataoff,
			H245_TransportAddress * addr,
			u_int16_t port, u_int16_t rtp_port,
			struct ip_conntrack_expect *rtp_exp,
			struct ip_conntrack_expect *rtcp_exp)
{
	struct ip_ct_h323_master *info = &ct->help.ct_h323_info;
	int dir = CTINFO2DIR(ctinfo);
	int i;
	u_int16_t nated_port;

	/* Set expectations for NAT */
	rtp_exp->saved_proto.udp.port = rtp_exp->tuple.dst.u.udp.port;
	rtp_exp->expectfn = ip_nat_follow_master;
	rtp_exp->dir = !dir;
	rtcp_exp->saved_proto.udp.port = rtcp_exp->tuple.dst.u.udp.port;
	rtcp_exp->expectfn = ip_nat_follow_master;
	rtcp_exp->dir = !dir;

	/* Lookup existing expects */
	for (i = 0; i < H323_RTP_CHANNEL_MAX; i++) {
		if (info->rtp_port[i][dir] == rtp_port) {
			/* Expected */

			/* Use allocated ports first. This will refresh
			 * the expects */
			rtp_exp->tuple.dst.u.udp.port =
			    htons(info->rtp_port[i][dir]);
			rtcp_exp->tuple.dst.u.udp.port =
			    htons(info->rtp_port[i][dir] + 1);
			break;
		} else if (info->rtp_port[i][dir] == 0) {
			/* Not expected */
			break;
		}
	}

	/* Run out of expectations */
	if (i >= H323_RTP_CHANNEL_MAX) {
		if (net_ratelimit())
			printk("ip_nat_h323: out of expectations\n");
		return 0;
	}

	/* Try to get a pair of ports. */
	for (nated_port = ntohs(rtp_exp->tuple.dst.u.udp.port);
	     nated_port != 0; nated_port += 2) {
		rtp_exp->tuple.dst.u.udp.port = htons(nated_port);
		if (ip_conntrack_expect_related(rtp_exp) == 0) {
			rtcp_exp->tuple.dst.u.udp.port =
			    htons(nated_port + 1);
			if (ip_conntrack_expect_related(rtcp_exp) == 0)
				break;
			ip_conntrack_unexpect_related(rtp_exp);
		}
	}

	if (nated_port == 0) {	/* No port available */
		if (net_ratelimit())
			printk("ip_nat_h323: out of RTP ports\n");
		return 0;
	}

	/* Modify signal */
	if (set_h245_addr(pskb, data, dataoff, addr,
			  ct->tuplehash[!dir].tuple.dst.ip,
			  (port & 1) ? nated_port + 1 : nated_port) == 0) {
		/* Save ports */
		info->rtp_port[i][dir] = rtp_port;
		info->rtp_port[i][!dir] = nated_port;
	} else {
		ip_conntrack_unexpect_related(rtp_exp);
		ip_conntrack_unexpect_related(rtcp_exp);
		return -1;
	}

	/* Success */
	DEBUGP("ip_nat_h323: expect RTP %u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
	       NIPQUAD(rtp_exp->tuple.src.ip),
	       ntohs(rtp_exp->tuple.src.u.udp.port),
	       NIPQUAD(rtp_exp->tuple.dst.ip),
	       ntohs(rtp_exp->tuple.dst.u.udp.port));
	DEBUGP("ip_nat_h323: expect RTCP %u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
	       NIPQUAD(rtcp_exp->tuple.src.ip),
	       ntohs(rtcp_exp->tuple.src.u.udp.port),
	       NIPQUAD(rtcp_exp->tuple.dst.ip),
	       ntohs(rtcp_exp->tuple.dst.u.udp.port));

	return 0;
}

/****************************************************************************/
static int nat_t120(struct sk_buff **pskb, struct ip_conntrack *ct,
		    enum ip_conntrack_info ctinfo,
		    unsigned char **data, int dataoff,
		    H245_TransportAddress * addr, u_int16_t port,
		    struct ip_conntrack_expect *exp)
{
	int dir = CTINFO2DIR(ctinfo);
	u_int16_t nated_port = port;

	/* Set expectations for NAT */
	exp->saved_proto.tcp.port = exp->tuple.dst.u.tcp.port;
	exp->expectfn = ip_nat_follow_master;
	exp->dir = !dir;

	/* Try to get same port: if not, try to change it. */
	for (; nated_port != 0; nated_port++) {
		exp->tuple.dst.u.tcp.port = htons(nated_port);
		if (ip_conntrack_expect_related(exp) == 0)
			break;
	}

	if (nated_port == 0) {	/* No port available */
		if (net_ratelimit())
			printk("ip_nat_h323: out of TCP ports\n");
		return 0;
	}

	/* Modify signal */
	if (set_h245_addr(pskb, data, dataoff, addr,
			  ct->tuplehash[!dir].tuple.dst.ip, nated_port) < 0) {
		ip_conntrack_unexpect_related(exp);
		return -1;
	}

	DEBUGP("ip_nat_h323: expect T.120 %u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
	       NIPQUAD(exp->tuple.src.ip), ntohs(exp->tuple.src.u.tcp.port),
	       NIPQUAD(exp->tuple.dst.ip), ntohs(exp->tuple.dst.u.tcp.port));

	return 0;
}

/****************************************************************************
 * This conntrack expect function replaces ip_conntrack_h245_expect()
 * which was set by ip_conntrack_helper_h323.c. It calls both
 * ip_nat_follow_master() and ip_conntrack_h245_expect()
 ****************************************************************************/
static void ip_nat_h245_expect(struct ip_conntrack *new,
			       struct ip_conntrack_expect *this)
{
	ip_nat_follow_master(new, this);
	ip_conntrack_h245_expect(new, this);
}

/****************************************************************************/
static int nat_h245(struct sk_buff **pskb, struct ip_conntrack *ct,
		    enum ip_conntrack_info ctinfo,
		    unsigned char **data, int dataoff,
		    TransportAddress * addr, u_int16_t port,
		    struct ip_conntrack_expect *exp)
{
	struct ip_ct_h323_master *info = &ct->help.ct_h323_info;
	int dir = CTINFO2DIR(ctinfo);
	u_int16_t nated_port = port;

	/* Set expectations for NAT */
	exp->saved_proto.tcp.port = exp->tuple.dst.u.tcp.port;
	exp->expectfn = ip_nat_h245_expect;
	exp->dir = !dir;

	/* Check existing expects */
	if (info->sig_port[dir] == port)
		nated_port = info->sig_port[!dir];

	/* Try to get same port: if not, try to change it. */
	for (; nated_port != 0; nated_port++) {
		exp->tuple.dst.u.tcp.port = htons(nated_port);
		if (ip_conntrack_expect_related(exp) == 0)
			break;
	}

	if (nated_port == 0) {	/* No port available */
		if (net_ratelimit())
			printk("ip_nat_q931: out of TCP ports\n");
		return 0;
	}

	/* Modify signal */
	if (set_h225_addr(pskb, data, dataoff, addr,
			  ct->tuplehash[!dir].tuple.dst.ip,
			  nated_port) == 0) {
		/* Save ports */
		info->sig_port[dir] = port;
		info->sig_port[!dir] = nated_port;
	} else {
		ip_conntrack_unexpect_related(exp);
		return -1;
	}

	DEBUGP("ip_nat_q931: expect H.245 %u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
	       NIPQUAD(exp->tuple.src.ip), ntohs(exp->tuple.src.u.tcp.port),
	       NIPQUAD(exp->tuple.dst.ip), ntohs(exp->tuple.dst.u.tcp.port));

	return 0;
}

/****************************************************************************
 * This conntrack expect function replaces ip_conntrack_q931_expect()
 * which was set by ip_conntrack_helper_h323.c.
 ****************************************************************************/
static void ip_nat_q931_expect(struct ip_conntrack *new,
			       struct ip_conntrack_expect *this)
{
	struct ip_nat_range range;

	if (this->tuple.src.ip != 0) {	/* Only accept calls from GK */
		ip_nat_follow_master(new, this);
		goto out;
	}

	/* This must be a fresh one. */
	BUG_ON(new->status & IPS_NAT_DONE_MASK);

	/* Change src to where master sends to */
	range.flags = IP_NAT_RANGE_MAP_IPS;
	range.min_ip = range.max_ip = new->tuplehash[!this->dir].tuple.src.ip;

	/* hook doesn't matter, but it has to do source manip */
	ip_nat_setup_info(new, &range, NF_IP_POST_ROUTING);

	/* For DST manip, map port here to where it's expected. */
	range.flags = (IP_NAT_RANGE_MAP_IPS | IP_NAT_RANGE_PROTO_SPECIFIED);
	range.min = range.max = this->saved_proto;
	range.min_ip = range.max_ip =
	    new->master->tuplehash[!this->dir].tuple.src.ip;

	/* hook doesn't matter, but it has to do destination manip */
	ip_nat_setup_info(new, &range, NF_IP_PRE_ROUTING);

      out:
	ip_conntrack_q931_expect(new, this);
}

/****************************************************************************/
static int nat_q931(struct sk_buff **pskb, struct ip_conntrack *ct,
		    enum ip_conntrack_info ctinfo,
		    unsigned char **data, TransportAddress * addr, int idx,
		    u_int16_t port, struct ip_conntrack_expect *exp)
{
	struct ip_ct_h323_master *info = &ct->help.ct_h323_info;
	int dir = CTINFO2DIR(ctinfo);
	u_int16_t nated_port = port;
	__be32 ip;

	/* Set expectations for NAT */
	exp->saved_proto.tcp.port = exp->tuple.dst.u.tcp.port;
	exp->expectfn = ip_nat_q931_expect;
	exp->dir = !dir;

	/* Check existing expects */
	if (info->sig_port[dir] == port)
		nated_port = info->sig_port[!dir];

	/* Try to get same port: if not, try to change it. */
	for (; nated_port != 0; nated_port++) {
		exp->tuple.dst.u.tcp.port = htons(nated_port);
		if (ip_conntrack_expect_related(exp) == 0)
			break;
	}

	if (nated_port == 0) {	/* No port available */
		if (net_ratelimit())
			printk("ip_nat_ras: out of TCP ports\n");
		return 0;
	}

	/* Modify signal */
	if (set_h225_addr(pskb, data, 0, &addr[idx],
			  ct->tuplehash[!dir].tuple.dst.ip,
			  nated_port) == 0) {
		/* Save ports */
		info->sig_port[dir] = port;
		info->sig_port[!dir] = nated_port;

		/* Fix for Gnomemeeting */
		if (idx > 0 &&
		    get_h225_addr(*data, &addr[0], &ip, &port) &&
		    (ntohl(ip) & 0xff000000) == 0x7f000000) {
			set_h225_addr_hook(pskb, data, 0, &addr[0],
					   ct->tuplehash[!dir].tuple.dst.ip,
					   info->sig_port[!dir]);
		}
	} else {
		ip_conntrack_unexpect_related(exp);
		return -1;
	}

	/* Success */
	DEBUGP("ip_nat_ras: expect Q.931 %u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
	       NIPQUAD(exp->tuple.src.ip), ntohs(exp->tuple.src.u.tcp.port),
	       NIPQUAD(exp->tuple.dst.ip), ntohs(exp->tuple.dst.u.tcp.port));

	return 0;
}

/****************************************************************************/
static void ip_nat_callforwarding_expect(struct ip_conntrack *new,
					 struct ip_conntrack_expect *this)
{
	struct ip_nat_range range;

	/* This must be a fresh one. */
	BUG_ON(new->status & IPS_NAT_DONE_MASK);

	/* Change src to where master sends to */
	range.flags = IP_NAT_RANGE_MAP_IPS;
	range.min_ip = range.max_ip = new->tuplehash[!this->dir].tuple.src.ip;

	/* hook doesn't matter, but it has to do source manip */
	ip_nat_setup_info(new, &range, NF_IP_POST_ROUTING);

	/* For DST manip, map port here to where it's expected. */
	range.flags = (IP_NAT_RANGE_MAP_IPS | IP_NAT_RANGE_PROTO_SPECIFIED);
	range.min = range.max = this->saved_proto;
	range.min_ip = range.max_ip = this->saved_ip;

	/* hook doesn't matter, but it has to do destination manip */
	ip_nat_setup_info(new, &range, NF_IP_PRE_ROUTING);

	ip_conntrack_q931_expect(new, this);
}

/****************************************************************************/
static int nat_callforwarding(struct sk_buff **pskb, struct ip_conntrack *ct,
			      enum ip_conntrack_info ctinfo,
			      unsigned char **data, int dataoff,
			      TransportAddress * addr, u_int16_t port,
			      struct ip_conntrack_expect *exp)
{
	int dir = CTINFO2DIR(ctinfo);
	u_int16_t nated_port;

	/* Set expectations for NAT */
	exp->saved_ip = exp->tuple.dst.ip;
	exp->tuple.dst.ip = ct->tuplehash[!dir].tuple.dst.ip;
	exp->saved_proto.tcp.port = exp->tuple.dst.u.tcp.port;
	exp->expectfn = ip_nat_callforwarding_expect;
	exp->dir = !dir;

	/* Try to get same port: if not, try to change it. */
	for (nated_port = port; nated_port != 0; nated_port++) {
		exp->tuple.dst.u.tcp.port = htons(nated_port);
		if (ip_conntrack_expect_related(exp) == 0)
			break;
	}

	if (nated_port == 0) {	/* No port available */
		if (net_ratelimit())
			printk("ip_nat_q931: out of TCP ports\n");
		return 0;
	}

	/* Modify signal */
	if (!set_h225_addr(pskb, data, dataoff, addr,
			   ct->tuplehash[!dir].tuple.dst.ip,
			   nated_port) == 0) {
		ip_conntrack_unexpect_related(exp);
		return -1;
	}

	/* Success */
	DEBUGP("ip_nat_q931: expect Call Forwarding "
	       "%u.%u.%u.%u:%hu->%u.%u.%u.%u:%hu\n",
	       NIPQUAD(exp->tuple.src.ip), ntohs(exp->tuple.src.u.tcp.port),
	       NIPQUAD(exp->tuple.dst.ip), ntohs(exp->tuple.dst.u.tcp.port));

	return 0;
}

/****************************************************************************/
static int __init init(void)
{
	BUG_ON(rcu_dereference(set_h245_addr_hook) != NULL);
	BUG_ON(rcu_dereference(set_h225_addr_hook) != NULL);
	BUG_ON(rcu_dereference(set_sig_addr_hook) != NULL);
	BUG_ON(rcu_dereference(set_ras_addr_hook) != NULL);
	BUG_ON(rcu_dereference(nat_rtp_rtcp_hook) != NULL);
	BUG_ON(rcu_dereference(nat_t120_hook) != NULL);
	BUG_ON(rcu_dereference(nat_h245_hook) != NULL);
	BUG_ON(rcu_dereference(nat_callforwarding_hook) != NULL);
	BUG_ON(rcu_dereference(nat_q931_hook) != NULL);

	rcu_assign_pointer(set_h245_addr_hook, set_h245_addr);
	rcu_assign_pointer(set_h225_addr_hook, set_h225_addr);
	rcu_assign_pointer(set_sig_addr_hook, set_sig_addr);
	rcu_assign_pointer(set_ras_addr_hook, set_ras_addr);
	rcu_assign_pointer(nat_rtp_rtcp_hook, nat_rtp_rtcp);
	rcu_assign_pointer(nat_t120_hook, nat_t120);
	rcu_assign_pointer(nat_h245_hook, nat_h245);
	rcu_assign_pointer(nat_callforwarding_hook, nat_callforwarding);
	rcu_assign_pointer(nat_q931_hook, nat_q931);

	DEBUGP("ip_nat_h323: init success\n");
	return 0;
}

/****************************************************************************/
static void __exit fini(void)
{
	rcu_assign_pointer(set_h245_addr_hook, NULL);
	rcu_assign_pointer(set_h225_addr_hook, NULL);
	rcu_assign_pointer(set_sig_addr_hook, NULL);
	rcu_assign_pointer(set_ras_addr_hook, NULL);
	rcu_assign_pointer(nat_rtp_rtcp_hook, NULL);
	rcu_assign_pointer(nat_t120_hook, NULL);
	rcu_assign_pointer(nat_h245_hook, NULL);
	rcu_assign_pointer(nat_callforwarding_hook, NULL);
	rcu_assign_pointer(nat_q931_hook, NULL);
	synchronize_rcu();
}

/****************************************************************************/
module_init(init);
module_exit(fini);

MODULE_AUTHOR("Jing Min Zhao <zhaojingmin@users.sourceforge.net>");
MODULE_DESCRIPTION("H.323 NAT helper");
MODULE_LICENSE("GPL");
