/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <net/ipv6.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/route.h>

#include <linux/netfilter.h>
#include <linux/netfilter/xt_LOG.h>
#include <net/netfilter/nf_log.h>

static struct nf_loginfo default_loginfo = {
	.type	= NF_LOG_TYPE_LOG,
	.u = {
		.log = {
			.level	  = LOGLEVEL_NOTICE,
			.logflags = NF_LOG_MASK,
		},
	},
};

/* One level of recursion won't kill us */
static void dump_ipv4_packet(struct nf_log_buf *m,
			     const struct nf_loginfo *info,
			     const struct sk_buff *skb, unsigned int iphoff)
{
	struct iphdr _iph;
	const struct iphdr *ih;
	unsigned int logflags;

	if (info->type == NF_LOG_TYPE_LOG)
		logflags = info->u.log.logflags;
	else
		logflags = NF_LOG_MASK;

	ih = skb_header_pointer(skb, iphoff, sizeof(_iph), &_iph);
	if (ih == NULL) {
		nf_log_buf_add(m, "TRUNCATED");
		return;
	}

	/* Important fields:
	 * TOS, len, DF/MF, fragment offset, TTL, src, dst, options. */
	/* Max length: 40 "SRC=255.255.255.255 DST=255.255.255.255 " */
	nf_log_buf_add(m, "SRC=%pI4 DST=%pI4 ", &ih->saddr, &ih->daddr);

	/* Max length: 46 "LEN=65535 TOS=0xFF PREC=0xFF TTL=255 ID=65535 " */
	nf_log_buf_add(m, "LEN=%u TOS=0x%02X PREC=0x%02X TTL=%u ID=%u ",
		       ntohs(ih->tot_len), ih->tos & IPTOS_TOS_MASK,
		       ih->tos & IPTOS_PREC_MASK, ih->ttl, ntohs(ih->id));

	/* Max length: 6 "CE DF MF " */
	if (ntohs(ih->frag_off) & IP_CE)
		nf_log_buf_add(m, "CE ");
	if (ntohs(ih->frag_off) & IP_DF)
		nf_log_buf_add(m, "DF ");
	if (ntohs(ih->frag_off) & IP_MF)
		nf_log_buf_add(m, "MF ");

	/* Max length: 11 "FRAG:65535 " */
	if (ntohs(ih->frag_off) & IP_OFFSET)
		nf_log_buf_add(m, "FRAG:%u ", ntohs(ih->frag_off) & IP_OFFSET);

	if ((logflags & XT_LOG_IPOPT) &&
	    ih->ihl * 4 > sizeof(struct iphdr)) {
		const unsigned char *op;
		unsigned char _opt[4 * 15 - sizeof(struct iphdr)];
		unsigned int i, optsize;

		optsize = ih->ihl * 4 - sizeof(struct iphdr);
		op = skb_header_pointer(skb, iphoff+sizeof(_iph),
					optsize, _opt);
		if (op == NULL) {
			nf_log_buf_add(m, "TRUNCATED");
			return;
		}

		/* Max length: 127 "OPT (" 15*4*2chars ") " */
		nf_log_buf_add(m, "OPT (");
		for (i = 0; i < optsize; i++)
			nf_log_buf_add(m, "%02X", op[i]);
		nf_log_buf_add(m, ") ");
	}

	switch (ih->protocol) {
	case IPPROTO_TCP:
		if (nf_log_dump_tcp_header(m, skb, ih->protocol,
					   ntohs(ih->frag_off) & IP_OFFSET,
					   iphoff+ih->ihl*4, logflags))
			return;
		break;
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
		if (nf_log_dump_udp_header(m, skb, ih->protocol,
					   ntohs(ih->frag_off) & IP_OFFSET,
					   iphoff+ih->ihl*4))
			return;
		break;
	case IPPROTO_ICMP: {
		struct icmphdr _icmph;
		const struct icmphdr *ich;
		static const size_t required_len[NR_ICMP_TYPES+1]
			= { [ICMP_ECHOREPLY] = 4,
			    [ICMP_DEST_UNREACH]
			    = 8 + sizeof(struct iphdr),
			    [ICMP_SOURCE_QUENCH]
			    = 8 + sizeof(struct iphdr),
			    [ICMP_REDIRECT]
			    = 8 + sizeof(struct iphdr),
			    [ICMP_ECHO] = 4,
			    [ICMP_TIME_EXCEEDED]
			    = 8 + sizeof(struct iphdr),
			    [ICMP_PARAMETERPROB]
			    = 8 + sizeof(struct iphdr),
			    [ICMP_TIMESTAMP] = 20,
			    [ICMP_TIMESTAMPREPLY] = 20,
			    [ICMP_ADDRESS] = 12,
			    [ICMP_ADDRESSREPLY] = 12 };

		/* Max length: 11 "PROTO=ICMP " */
		nf_log_buf_add(m, "PROTO=ICMP ");

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		ich = skb_header_pointer(skb, iphoff + ih->ihl * 4,
					 sizeof(_icmph), &_icmph);
		if (ich == NULL) {
			nf_log_buf_add(m, "INCOMPLETE [%u bytes] ",
				       skb->len - iphoff - ih->ihl*4);
			break;
		}

		/* Max length: 18 "TYPE=255 CODE=255 " */
		nf_log_buf_add(m, "TYPE=%u CODE=%u ", ich->type, ich->code);

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		if (ich->type <= NR_ICMP_TYPES &&
		    required_len[ich->type] &&
		    skb->len-iphoff-ih->ihl*4 < required_len[ich->type]) {
			nf_log_buf_add(m, "INCOMPLETE [%u bytes] ",
				       skb->len - iphoff - ih->ihl*4);
			break;
		}

		switch (ich->type) {
		case ICMP_ECHOREPLY:
		case ICMP_ECHO:
			/* Max length: 19 "ID=65535 SEQ=65535 " */
			nf_log_buf_add(m, "ID=%u SEQ=%u ",
				       ntohs(ich->un.echo.id),
				       ntohs(ich->un.echo.sequence));
			break;

		case ICMP_PARAMETERPROB:
			/* Max length: 14 "PARAMETER=255 " */
			nf_log_buf_add(m, "PARAMETER=%u ",
				       ntohl(ich->un.gateway) >> 24);
			break;
		case ICMP_REDIRECT:
			/* Max length: 24 "GATEWAY=255.255.255.255 " */
			nf_log_buf_add(m, "GATEWAY=%pI4 ", &ich->un.gateway);
			/* Fall through */
		case ICMP_DEST_UNREACH:
		case ICMP_SOURCE_QUENCH:
		case ICMP_TIME_EXCEEDED:
			/* Max length: 3+maxlen */
			if (!iphoff) { /* Only recurse once. */
				nf_log_buf_add(m, "[");
				dump_ipv4_packet(m, info, skb,
					    iphoff + ih->ihl*4+sizeof(_icmph));
				nf_log_buf_add(m, "] ");
			}

			/* Max length: 10 "MTU=65535 " */
			if (ich->type == ICMP_DEST_UNREACH &&
			    ich->code == ICMP_FRAG_NEEDED) {
				nf_log_buf_add(m, "MTU=%u ",
					       ntohs(ich->un.frag.mtu));
			}
		}
		break;
	}
	/* Max Length */
	case IPPROTO_AH: {
		struct ip_auth_hdr _ahdr;
		const struct ip_auth_hdr *ah;

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 9 "PROTO=AH " */
		nf_log_buf_add(m, "PROTO=AH ");

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		ah = skb_header_pointer(skb, iphoff+ih->ihl*4,
					sizeof(_ahdr), &_ahdr);
		if (ah == NULL) {
			nf_log_buf_add(m, "INCOMPLETE [%u bytes] ",
				       skb->len - iphoff - ih->ihl*4);
			break;
		}

		/* Length: 15 "SPI=0xF1234567 " */
		nf_log_buf_add(m, "SPI=0x%x ", ntohl(ah->spi));
		break;
	}
	case IPPROTO_ESP: {
		struct ip_esp_hdr _esph;
		const struct ip_esp_hdr *eh;

		/* Max length: 10 "PROTO=ESP " */
		nf_log_buf_add(m, "PROTO=ESP ");

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		eh = skb_header_pointer(skb, iphoff+ih->ihl*4,
					sizeof(_esph), &_esph);
		if (eh == NULL) {
			nf_log_buf_add(m, "INCOMPLETE [%u bytes] ",
				       skb->len - iphoff - ih->ihl*4);
			break;
		}

		/* Length: 15 "SPI=0xF1234567 " */
		nf_log_buf_add(m, "SPI=0x%x ", ntohl(eh->spi));
		break;
	}
	/* Max length: 10 "PROTO 255 " */
	default:
		nf_log_buf_add(m, "PROTO=%u ", ih->protocol);
	}

	/* Max length: 15 "UID=4294967295 " */
	if ((logflags & XT_LOG_UID) && !iphoff)
		nf_log_dump_sk_uid_gid(m, skb->sk);

	/* Max length: 16 "MARK=0xFFFFFFFF " */
	if (!iphoff && skb->mark)
		nf_log_buf_add(m, "MARK=0x%x ", skb->mark);

	/* Proto    Max log string length */
	/* IP:	    40+46+6+11+127 = 230 */
	/* TCP:     10+max(25,20+30+13+9+32+11+127) = 252 */
	/* UDP:     10+max(25,20) = 35 */
	/* UDPLITE: 14+max(25,20) = 39 */
	/* ICMP:    11+max(25, 18+25+max(19,14,24+3+n+10,3+n+10)) = 91+n */
	/* ESP:     10+max(25)+15 = 50 */
	/* AH:	    9+max(25)+15 = 49 */
	/* unknown: 10 */

	/* (ICMP allows recursion one level deep) */
	/* maxlen =  IP + ICMP +  IP + max(TCP,UDP,ICMP,unknown) */
	/* maxlen = 230+   91  + 230 + 252 = 803 */
}

static void dump_ipv4_mac_header(struct nf_log_buf *m,
			    const struct nf_loginfo *info,
			    const struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	unsigned int logflags = 0;

	if (info->type == NF_LOG_TYPE_LOG)
		logflags = info->u.log.logflags;

	if (!(logflags & XT_LOG_MACDECODE))
		goto fallback;

	switch (dev->type) {
	case ARPHRD_ETHER:
		nf_log_buf_add(m, "MACSRC=%pM MACDST=%pM MACPROTO=%04x ",
			       eth_hdr(skb)->h_source, eth_hdr(skb)->h_dest,
			       ntohs(eth_hdr(skb)->h_proto));
		return;
	default:
		break;
	}

fallback:
	nf_log_buf_add(m, "MAC=");
	if (dev->hard_header_len &&
	    skb->mac_header != skb->network_header) {
		const unsigned char *p = skb_mac_header(skb);
		unsigned int i;

		nf_log_buf_add(m, "%02x", *p++);
		for (i = 1; i < dev->hard_header_len; i++, p++)
			nf_log_buf_add(m, ":%02x", *p);
	}
	nf_log_buf_add(m, " ");
}

static void nf_log_ip_packet(struct net *net, u_int8_t pf,
			     unsigned int hooknum, const struct sk_buff *skb,
			     const struct net_device *in,
			     const struct net_device *out,
			     const struct nf_loginfo *loginfo,
			     const char *prefix)
{
	struct nf_log_buf *m;

	/* FIXME: Disabled from containers until syslog ns is supported */
	if (!net_eq(net, &init_net))
		return;

	m = nf_log_buf_open();

	if (!loginfo)
		loginfo = &default_loginfo;

	nf_log_dump_packet_common(m, pf, hooknum, skb, in,
				  out, loginfo, prefix);

	if (in != NULL)
		dump_ipv4_mac_header(m, loginfo, skb);

	dump_ipv4_packet(m, loginfo, skb, 0);

	nf_log_buf_close(m);
}

static struct nf_logger nf_ip_logger __read_mostly = {
	.name		= "nf_log_ipv4",
	.type		= NF_LOG_TYPE_LOG,
	.logfn		= nf_log_ip_packet,
	.me		= THIS_MODULE,
};

static int __net_init nf_log_ipv4_net_init(struct net *net)
{
	nf_log_set(net, NFPROTO_IPV4, &nf_ip_logger);
	return 0;
}

static void __net_exit nf_log_ipv4_net_exit(struct net *net)
{
	nf_log_unset(net, &nf_ip_logger);
}

static struct pernet_operations nf_log_ipv4_net_ops = {
	.init = nf_log_ipv4_net_init,
	.exit = nf_log_ipv4_net_exit,
};

static int __init nf_log_ipv4_init(void)
{
	int ret;

	ret = register_pernet_subsys(&nf_log_ipv4_net_ops);
	if (ret < 0)
		return ret;

	ret = nf_log_register(NFPROTO_IPV4, &nf_ip_logger);
	if (ret < 0) {
		pr_err("failed to register logger\n");
		goto err1;
	}

	return 0;

err1:
	unregister_pernet_subsys(&nf_log_ipv4_net_ops);
	return ret;
}

static void __exit nf_log_ipv4_exit(void)
{
	unregister_pernet_subsys(&nf_log_ipv4_net_ops);
	nf_log_unregister(&nf_ip_logger);
}

module_init(nf_log_ipv4_init);
module_exit(nf_log_ipv4_exit);

MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("Netfilter IPv4 packet logging");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NF_LOGGER(AF_INET, 0);
