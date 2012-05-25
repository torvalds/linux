/*
 * This is a module which is used for logging packets.
 */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
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
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_LOG.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <net/netfilter/nf_log.h>
#include <net/netfilter/xt_log.h>

static struct nf_loginfo default_loginfo = {
	.type	= NF_LOG_TYPE_LOG,
	.u = {
		.log = {
			.level    = 5,
			.logflags = NF_LOG_MASK,
		},
	},
};

static int dump_udp_header(struct sbuff *m, const struct sk_buff *skb,
			   u8 proto, int fragment, unsigned int offset)
{
	struct udphdr _udph;
	const struct udphdr *uh;

	if (proto == IPPROTO_UDP)
		/* Max length: 10 "PROTO=UDP "     */
		sb_add(m, "PROTO=UDP ");
	else	/* Max length: 14 "PROTO=UDPLITE " */
		sb_add(m, "PROTO=UDPLITE ");

	if (fragment)
		goto out;

	/* Max length: 25 "INCOMPLETE [65535 bytes] " */
	uh = skb_header_pointer(skb, offset, sizeof(_udph), &_udph);
	if (uh == NULL) {
		sb_add(m, "INCOMPLETE [%u bytes] ", skb->len - offset);

		return 1;
	}

	/* Max length: 20 "SPT=65535 DPT=65535 " */
	sb_add(m, "SPT=%u DPT=%u LEN=%u ", ntohs(uh->source), ntohs(uh->dest),
		ntohs(uh->len));

out:
	return 0;
}

static int dump_tcp_header(struct sbuff *m, const struct sk_buff *skb,
			   u8 proto, int fragment, unsigned int offset,
			   unsigned int logflags)
{
	struct tcphdr _tcph;
	const struct tcphdr *th;

	/* Max length: 10 "PROTO=TCP " */
	sb_add(m, "PROTO=TCP ");

	if (fragment)
		return 0;

	/* Max length: 25 "INCOMPLETE [65535 bytes] " */
	th = skb_header_pointer(skb, offset, sizeof(_tcph), &_tcph);
	if (th == NULL) {
		sb_add(m, "INCOMPLETE [%u bytes] ", skb->len - offset);
		return 1;
	}

	/* Max length: 20 "SPT=65535 DPT=65535 " */
	sb_add(m, "SPT=%u DPT=%u ", ntohs(th->source), ntohs(th->dest));
	/* Max length: 30 "SEQ=4294967295 ACK=4294967295 " */
	if (logflags & XT_LOG_TCPSEQ)
		sb_add(m, "SEQ=%u ACK=%u ", ntohl(th->seq), ntohl(th->ack_seq));

	/* Max length: 13 "WINDOW=65535 " */
	sb_add(m, "WINDOW=%u ", ntohs(th->window));
	/* Max length: 9 "RES=0x3C " */
	sb_add(m, "RES=0x%02x ", (u_int8_t)(ntohl(tcp_flag_word(th) &
					    TCP_RESERVED_BITS) >> 22));
	/* Max length: 32 "CWR ECE URG ACK PSH RST SYN FIN " */
	if (th->cwr)
		sb_add(m, "CWR ");
	if (th->ece)
		sb_add(m, "ECE ");
	if (th->urg)
		sb_add(m, "URG ");
	if (th->ack)
		sb_add(m, "ACK ");
	if (th->psh)
		sb_add(m, "PSH ");
	if (th->rst)
		sb_add(m, "RST ");
	if (th->syn)
		sb_add(m, "SYN ");
	if (th->fin)
		sb_add(m, "FIN ");
	/* Max length: 11 "URGP=65535 " */
	sb_add(m, "URGP=%u ", ntohs(th->urg_ptr));

	if ((logflags & XT_LOG_TCPOPT) && th->doff*4 > sizeof(struct tcphdr)) {
		u_int8_t _opt[60 - sizeof(struct tcphdr)];
		const u_int8_t *op;
		unsigned int i;
		unsigned int optsize = th->doff*4 - sizeof(struct tcphdr);

		op = skb_header_pointer(skb, offset + sizeof(struct tcphdr),
					optsize, _opt);
		if (op == NULL) {
			sb_add(m, "OPT (TRUNCATED)");
			return 1;
		}

		/* Max length: 127 "OPT (" 15*4*2chars ") " */
		sb_add(m, "OPT (");
		for (i = 0; i < optsize; i++)
			sb_add(m, "%02X", op[i]);

		sb_add(m, ") ");
	}

	return 0;
}

/* One level of recursion won't kill us */
static void dump_ipv4_packet(struct sbuff *m,
			const struct nf_loginfo *info,
			const struct sk_buff *skb,
			unsigned int iphoff)
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
		sb_add(m, "TRUNCATED");
		return;
	}

	/* Important fields:
	 * TOS, len, DF/MF, fragment offset, TTL, src, dst, options. */
	/* Max length: 40 "SRC=255.255.255.255 DST=255.255.255.255 " */
	sb_add(m, "SRC=%pI4 DST=%pI4 ",
	       &ih->saddr, &ih->daddr);

	/* Max length: 46 "LEN=65535 TOS=0xFF PREC=0xFF TTL=255 ID=65535 " */
	sb_add(m, "LEN=%u TOS=0x%02X PREC=0x%02X TTL=%u ID=%u ",
	       ntohs(ih->tot_len), ih->tos & IPTOS_TOS_MASK,
	       ih->tos & IPTOS_PREC_MASK, ih->ttl, ntohs(ih->id));

	/* Max length: 6 "CE DF MF " */
	if (ntohs(ih->frag_off) & IP_CE)
		sb_add(m, "CE ");
	if (ntohs(ih->frag_off) & IP_DF)
		sb_add(m, "DF ");
	if (ntohs(ih->frag_off) & IP_MF)
		sb_add(m, "MF ");

	/* Max length: 11 "FRAG:65535 " */
	if (ntohs(ih->frag_off) & IP_OFFSET)
		sb_add(m, "FRAG:%u ", ntohs(ih->frag_off) & IP_OFFSET);

	if ((logflags & XT_LOG_IPOPT) &&
	    ih->ihl * 4 > sizeof(struct iphdr)) {
		const unsigned char *op;
		unsigned char _opt[4 * 15 - sizeof(struct iphdr)];
		unsigned int i, optsize;

		optsize = ih->ihl * 4 - sizeof(struct iphdr);
		op = skb_header_pointer(skb, iphoff+sizeof(_iph),
					optsize, _opt);
		if (op == NULL) {
			sb_add(m, "TRUNCATED");
			return;
		}

		/* Max length: 127 "OPT (" 15*4*2chars ") " */
		sb_add(m, "OPT (");
		for (i = 0; i < optsize; i++)
			sb_add(m, "%02X", op[i]);
		sb_add(m, ") ");
	}

	switch (ih->protocol) {
	case IPPROTO_TCP:
		if (dump_tcp_header(m, skb, ih->protocol,
				    ntohs(ih->frag_off) & IP_OFFSET,
				    iphoff+ih->ihl*4, logflags))
			return;
		break;
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
		if (dump_udp_header(m, skb, ih->protocol,
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
		sb_add(m, "PROTO=ICMP ");

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		ich = skb_header_pointer(skb, iphoff + ih->ihl * 4,
					 sizeof(_icmph), &_icmph);
		if (ich == NULL) {
			sb_add(m, "INCOMPLETE [%u bytes] ",
			       skb->len - iphoff - ih->ihl*4);
			break;
		}

		/* Max length: 18 "TYPE=255 CODE=255 " */
		sb_add(m, "TYPE=%u CODE=%u ", ich->type, ich->code);

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		if (ich->type <= NR_ICMP_TYPES &&
		    required_len[ich->type] &&
		    skb->len-iphoff-ih->ihl*4 < required_len[ich->type]) {
			sb_add(m, "INCOMPLETE [%u bytes] ",
			       skb->len - iphoff - ih->ihl*4);
			break;
		}

		switch (ich->type) {
		case ICMP_ECHOREPLY:
		case ICMP_ECHO:
			/* Max length: 19 "ID=65535 SEQ=65535 " */
			sb_add(m, "ID=%u SEQ=%u ",
			       ntohs(ich->un.echo.id),
			       ntohs(ich->un.echo.sequence));
			break;

		case ICMP_PARAMETERPROB:
			/* Max length: 14 "PARAMETER=255 " */
			sb_add(m, "PARAMETER=%u ",
			       ntohl(ich->un.gateway) >> 24);
			break;
		case ICMP_REDIRECT:
			/* Max length: 24 "GATEWAY=255.255.255.255 " */
			sb_add(m, "GATEWAY=%pI4 ", &ich->un.gateway);
			/* Fall through */
		case ICMP_DEST_UNREACH:
		case ICMP_SOURCE_QUENCH:
		case ICMP_TIME_EXCEEDED:
			/* Max length: 3+maxlen */
			if (!iphoff) { /* Only recurse once. */
				sb_add(m, "[");
				dump_ipv4_packet(m, info, skb,
					    iphoff + ih->ihl*4+sizeof(_icmph));
				sb_add(m, "] ");
			}

			/* Max length: 10 "MTU=65535 " */
			if (ich->type == ICMP_DEST_UNREACH &&
			    ich->code == ICMP_FRAG_NEEDED)
				sb_add(m, "MTU=%u ", ntohs(ich->un.frag.mtu));
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
		sb_add(m, "PROTO=AH ");

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		ah = skb_header_pointer(skb, iphoff+ih->ihl*4,
					sizeof(_ahdr), &_ahdr);
		if (ah == NULL) {
			sb_add(m, "INCOMPLETE [%u bytes] ",
			       skb->len - iphoff - ih->ihl*4);
			break;
		}

		/* Length: 15 "SPI=0xF1234567 " */
		sb_add(m, "SPI=0x%x ", ntohl(ah->spi));
		break;
	}
	case IPPROTO_ESP: {
		struct ip_esp_hdr _esph;
		const struct ip_esp_hdr *eh;

		/* Max length: 10 "PROTO=ESP " */
		sb_add(m, "PROTO=ESP ");

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		eh = skb_header_pointer(skb, iphoff+ih->ihl*4,
					sizeof(_esph), &_esph);
		if (eh == NULL) {
			sb_add(m, "INCOMPLETE [%u bytes] ",
			       skb->len - iphoff - ih->ihl*4);
			break;
		}

		/* Length: 15 "SPI=0xF1234567 " */
		sb_add(m, "SPI=0x%x ", ntohl(eh->spi));
		break;
	}
	/* Max length: 10 "PROTO 255 " */
	default:
		sb_add(m, "PROTO=%u ", ih->protocol);
	}

	/* Max length: 15 "UID=4294967295 " */
	if ((logflags & XT_LOG_UID) && !iphoff && skb->sk) {
		read_lock_bh(&skb->sk->sk_callback_lock);
		if (skb->sk->sk_socket && skb->sk->sk_socket->file) {
			const struct cred *cred = skb->sk->sk_socket->file->f_cred;
			sb_add(m, "UID=%u GID=%u ",
				from_kuid_munged(&init_user_ns, cred->fsuid),
				from_kgid_munged(&init_user_ns, cred->fsgid));
		}
		read_unlock_bh(&skb->sk->sk_callback_lock);
	}

	/* Max length: 16 "MARK=0xFFFFFFFF " */
	if (!iphoff && skb->mark)
		sb_add(m, "MARK=0x%x ", skb->mark);

	/* Proto    Max log string length */
	/* IP:      40+46+6+11+127 = 230 */
	/* TCP:     10+max(25,20+30+13+9+32+11+127) = 252 */
	/* UDP:     10+max(25,20) = 35 */
	/* UDPLITE: 14+max(25,20) = 39 */
	/* ICMP:    11+max(25, 18+25+max(19,14,24+3+n+10,3+n+10)) = 91+n */
	/* ESP:     10+max(25)+15 = 50 */
	/* AH:      9+max(25)+15 = 49 */
	/* unknown: 10 */

	/* (ICMP allows recursion one level deep) */
	/* maxlen =  IP + ICMP +  IP + max(TCP,UDP,ICMP,unknown) */
	/* maxlen = 230+   91  + 230 + 252 = 803 */
}

static void dump_ipv4_mac_header(struct sbuff *m,
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
		sb_add(m, "MACSRC=%pM MACDST=%pM MACPROTO=%04x ",
		       eth_hdr(skb)->h_source, eth_hdr(skb)->h_dest,
		       ntohs(eth_hdr(skb)->h_proto));
		return;
	default:
		break;
	}

fallback:
	sb_add(m, "MAC=");
	if (dev->hard_header_len &&
	    skb->mac_header != skb->network_header) {
		const unsigned char *p = skb_mac_header(skb);
		unsigned int i;

		sb_add(m, "%02x", *p++);
		for (i = 1; i < dev->hard_header_len; i++, p++)
			sb_add(m, ":%02x", *p);
	}
	sb_add(m, " ");
}

static void
log_packet_common(struct sbuff *m,
		  u_int8_t pf,
		  unsigned int hooknum,
		  const struct sk_buff *skb,
		  const struct net_device *in,
		  const struct net_device *out,
		  const struct nf_loginfo *loginfo,
		  const char *prefix)
{
	sb_add(m, "<%d>%sIN=%s OUT=%s ", loginfo->u.log.level,
	       prefix,
	       in ? in->name : "",
	       out ? out->name : "");
#ifdef CONFIG_BRIDGE_NETFILTER
	if (skb->nf_bridge) {
		const struct net_device *physindev;
		const struct net_device *physoutdev;

		physindev = skb->nf_bridge->physindev;
		if (physindev && in != physindev)
			sb_add(m, "PHYSIN=%s ", physindev->name);
		physoutdev = skb->nf_bridge->physoutdev;
		if (physoutdev && out != physoutdev)
			sb_add(m, "PHYSOUT=%s ", physoutdev->name);
	}
#endif
}


static void
ipt_log_packet(u_int8_t pf,
	       unsigned int hooknum,
	       const struct sk_buff *skb,
	       const struct net_device *in,
	       const struct net_device *out,
	       const struct nf_loginfo *loginfo,
	       const char *prefix)
{
	struct sbuff *m = sb_open();

	if (!loginfo)
		loginfo = &default_loginfo;

	log_packet_common(m, pf, hooknum, skb, in, out, loginfo, prefix);

	if (in != NULL)
		dump_ipv4_mac_header(m, loginfo, skb);

	dump_ipv4_packet(m, loginfo, skb, 0);

	sb_close(m);
}

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
/* One level of recursion won't kill us */
static void dump_ipv6_packet(struct sbuff *m,
			const struct nf_loginfo *info,
			const struct sk_buff *skb, unsigned int ip6hoff,
			int recurse)
{
	u_int8_t currenthdr;
	int fragment;
	struct ipv6hdr _ip6h;
	const struct ipv6hdr *ih;
	unsigned int ptr;
	unsigned int hdrlen = 0;
	unsigned int logflags;

	if (info->type == NF_LOG_TYPE_LOG)
		logflags = info->u.log.logflags;
	else
		logflags = NF_LOG_MASK;

	ih = skb_header_pointer(skb, ip6hoff, sizeof(_ip6h), &_ip6h);
	if (ih == NULL) {
		sb_add(m, "TRUNCATED");
		return;
	}

	/* Max length: 88 "SRC=0000.0000.0000.0000.0000.0000.0000.0000 DST=0000.0000.0000.0000.0000.0000.0000.0000 " */
	sb_add(m, "SRC=%pI6 DST=%pI6 ", &ih->saddr, &ih->daddr);

	/* Max length: 44 "LEN=65535 TC=255 HOPLIMIT=255 FLOWLBL=FFFFF " */
	sb_add(m, "LEN=%Zu TC=%u HOPLIMIT=%u FLOWLBL=%u ",
	       ntohs(ih->payload_len) + sizeof(struct ipv6hdr),
	       (ntohl(*(__be32 *)ih) & 0x0ff00000) >> 20,
	       ih->hop_limit,
	       (ntohl(*(__be32 *)ih) & 0x000fffff));

	fragment = 0;
	ptr = ip6hoff + sizeof(struct ipv6hdr);
	currenthdr = ih->nexthdr;
	while (currenthdr != NEXTHDR_NONE && ip6t_ext_hdr(currenthdr)) {
		struct ipv6_opt_hdr _hdr;
		const struct ipv6_opt_hdr *hp;

		hp = skb_header_pointer(skb, ptr, sizeof(_hdr), &_hdr);
		if (hp == NULL) {
			sb_add(m, "TRUNCATED");
			return;
		}

		/* Max length: 48 "OPT (...) " */
		if (logflags & XT_LOG_IPOPT)
			sb_add(m, "OPT ( ");

		switch (currenthdr) {
		case IPPROTO_FRAGMENT: {
			struct frag_hdr _fhdr;
			const struct frag_hdr *fh;

			sb_add(m, "FRAG:");
			fh = skb_header_pointer(skb, ptr, sizeof(_fhdr),
						&_fhdr);
			if (fh == NULL) {
				sb_add(m, "TRUNCATED ");
				return;
			}

			/* Max length: 6 "65535 " */
			sb_add(m, "%u ", ntohs(fh->frag_off) & 0xFFF8);

			/* Max length: 11 "INCOMPLETE " */
			if (fh->frag_off & htons(0x0001))
				sb_add(m, "INCOMPLETE ");

			sb_add(m, "ID:%08x ", ntohl(fh->identification));

			if (ntohs(fh->frag_off) & 0xFFF8)
				fragment = 1;

			hdrlen = 8;

			break;
		}
		case IPPROTO_DSTOPTS:
		case IPPROTO_ROUTING:
		case IPPROTO_HOPOPTS:
			if (fragment) {
				if (logflags & XT_LOG_IPOPT)
					sb_add(m, ")");
				return;
			}
			hdrlen = ipv6_optlen(hp);
			break;
		/* Max Length */
		case IPPROTO_AH:
			if (logflags & XT_LOG_IPOPT) {
				struct ip_auth_hdr _ahdr;
				const struct ip_auth_hdr *ah;

				/* Max length: 3 "AH " */
				sb_add(m, "AH ");

				if (fragment) {
					sb_add(m, ")");
					return;
				}

				ah = skb_header_pointer(skb, ptr, sizeof(_ahdr),
							&_ahdr);
				if (ah == NULL) {
					/*
					 * Max length: 26 "INCOMPLETE [65535
					 *  bytes] )"
					 */
					sb_add(m, "INCOMPLETE [%u bytes] )",
					       skb->len - ptr);
					return;
				}

				/* Length: 15 "SPI=0xF1234567 */
				sb_add(m, "SPI=0x%x ", ntohl(ah->spi));

			}

			hdrlen = (hp->hdrlen+2)<<2;
			break;
		case IPPROTO_ESP:
			if (logflags & XT_LOG_IPOPT) {
				struct ip_esp_hdr _esph;
				const struct ip_esp_hdr *eh;

				/* Max length: 4 "ESP " */
				sb_add(m, "ESP ");

				if (fragment) {
					sb_add(m, ")");
					return;
				}

				/*
				 * Max length: 26 "INCOMPLETE [65535 bytes] )"
				 */
				eh = skb_header_pointer(skb, ptr, sizeof(_esph),
							&_esph);
				if (eh == NULL) {
					sb_add(m, "INCOMPLETE [%u bytes] )",
					       skb->len - ptr);
					return;
				}

				/* Length: 16 "SPI=0xF1234567 )" */
				sb_add(m, "SPI=0x%x )", ntohl(eh->spi));

			}
			return;
		default:
			/* Max length: 20 "Unknown Ext Hdr 255" */
			sb_add(m, "Unknown Ext Hdr %u", currenthdr);
			return;
		}
		if (logflags & XT_LOG_IPOPT)
			sb_add(m, ") ");

		currenthdr = hp->nexthdr;
		ptr += hdrlen;
	}

	switch (currenthdr) {
	case IPPROTO_TCP:
		if (dump_tcp_header(m, skb, currenthdr, fragment, ptr,
		    logflags))
			return;
		break;
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
		if (dump_udp_header(m, skb, currenthdr, fragment, ptr))
			return;
		break;
	case IPPROTO_ICMPV6: {
		struct icmp6hdr _icmp6h;
		const struct icmp6hdr *ic;

		/* Max length: 13 "PROTO=ICMPv6 " */
		sb_add(m, "PROTO=ICMPv6 ");

		if (fragment)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		ic = skb_header_pointer(skb, ptr, sizeof(_icmp6h), &_icmp6h);
		if (ic == NULL) {
			sb_add(m, "INCOMPLETE [%u bytes] ", skb->len - ptr);
			return;
		}

		/* Max length: 18 "TYPE=255 CODE=255 " */
		sb_add(m, "TYPE=%u CODE=%u ", ic->icmp6_type, ic->icmp6_code);

		switch (ic->icmp6_type) {
		case ICMPV6_ECHO_REQUEST:
		case ICMPV6_ECHO_REPLY:
			/* Max length: 19 "ID=65535 SEQ=65535 " */
			sb_add(m, "ID=%u SEQ=%u ",
				ntohs(ic->icmp6_identifier),
				ntohs(ic->icmp6_sequence));
			break;
		case ICMPV6_MGM_QUERY:
		case ICMPV6_MGM_REPORT:
		case ICMPV6_MGM_REDUCTION:
			break;

		case ICMPV6_PARAMPROB:
			/* Max length: 17 "POINTER=ffffffff " */
			sb_add(m, "POINTER=%08x ", ntohl(ic->icmp6_pointer));
			/* Fall through */
		case ICMPV6_DEST_UNREACH:
		case ICMPV6_PKT_TOOBIG:
		case ICMPV6_TIME_EXCEED:
			/* Max length: 3+maxlen */
			if (recurse) {
				sb_add(m, "[");
				dump_ipv6_packet(m, info, skb,
					    ptr + sizeof(_icmp6h), 0);
				sb_add(m, "] ");
			}

			/* Max length: 10 "MTU=65535 " */
			if (ic->icmp6_type == ICMPV6_PKT_TOOBIG)
				sb_add(m, "MTU=%u ", ntohl(ic->icmp6_mtu));
		}
		break;
	}
	/* Max length: 10 "PROTO=255 " */
	default:
		sb_add(m, "PROTO=%u ", currenthdr);
	}

	/* Max length: 15 "UID=4294967295 " */
	if ((logflags & XT_LOG_UID) && recurse && skb->sk) {
		read_lock_bh(&skb->sk->sk_callback_lock);
		if (skb->sk->sk_socket && skb->sk->sk_socket->file) {
			const struct cred *cred = skb->sk->sk_socket->file->f_cred;
			sb_add(m, "UID=%u GID=%u ",
				from_kuid_munged(&init_user_ns, cred->fsuid),
				from_kgid_munged(&init_user_ns, cred->fsgid));
		}
		read_unlock_bh(&skb->sk->sk_callback_lock);
	}

	/* Max length: 16 "MARK=0xFFFFFFFF " */
	if (!recurse && skb->mark)
		sb_add(m, "MARK=0x%x ", skb->mark);
}

static void dump_ipv6_mac_header(struct sbuff *m,
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
		sb_add(m, "MACSRC=%pM MACDST=%pM MACPROTO=%04x ",
		       eth_hdr(skb)->h_source, eth_hdr(skb)->h_dest,
		       ntohs(eth_hdr(skb)->h_proto));
		return;
	default:
		break;
	}

fallback:
	sb_add(m, "MAC=");
	if (dev->hard_header_len &&
	    skb->mac_header != skb->network_header) {
		const unsigned char *p = skb_mac_header(skb);
		unsigned int len = dev->hard_header_len;
		unsigned int i;

		if (dev->type == ARPHRD_SIT) {
			p -= ETH_HLEN;

			if (p < skb->head)
				p = NULL;
		}

		if (p != NULL) {
			sb_add(m, "%02x", *p++);
			for (i = 1; i < len; i++)
				sb_add(m, ":%02x", *p++);
		}
		sb_add(m, " ");

		if (dev->type == ARPHRD_SIT) {
			const struct iphdr *iph =
				(struct iphdr *)skb_mac_header(skb);
			sb_add(m, "TUNNEL=%pI4->%pI4 ", &iph->saddr,
			       &iph->daddr);
		}
	} else
		sb_add(m, " ");
}

static void
ip6t_log_packet(u_int8_t pf,
		unsigned int hooknum,
		const struct sk_buff *skb,
		const struct net_device *in,
		const struct net_device *out,
		const struct nf_loginfo *loginfo,
		const char *prefix)
{
	struct sbuff *m = sb_open();

	if (!loginfo)
		loginfo = &default_loginfo;

	log_packet_common(m, pf, hooknum, skb, in, out, loginfo, prefix);

	if (in != NULL)
		dump_ipv6_mac_header(m, loginfo, skb);

	dump_ipv6_packet(m, loginfo, skb, skb_network_offset(skb), 1);

	sb_close(m);
}
#endif

static unsigned int
log_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_log_info *loginfo = par->targinfo;
	struct nf_loginfo li;

	li.type = NF_LOG_TYPE_LOG;
	li.u.log.level = loginfo->level;
	li.u.log.logflags = loginfo->logflags;

	if (par->family == NFPROTO_IPV4)
		ipt_log_packet(NFPROTO_IPV4, par->hooknum, skb, par->in,
			       par->out, &li, loginfo->prefix);
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	else if (par->family == NFPROTO_IPV6)
		ip6t_log_packet(NFPROTO_IPV6, par->hooknum, skb, par->in,
				par->out, &li, loginfo->prefix);
#endif
	else
		WARN_ON_ONCE(1);

	return XT_CONTINUE;
}

static int log_tg_check(const struct xt_tgchk_param *par)
{
	const struct xt_log_info *loginfo = par->targinfo;

	if (par->family != NFPROTO_IPV4 && par->family != NFPROTO_IPV6)
		return -EINVAL;

	if (loginfo->level >= 8) {
		pr_debug("level %u >= 8\n", loginfo->level);
		return -EINVAL;
	}

	if (loginfo->prefix[sizeof(loginfo->prefix)-1] != '\0') {
		pr_debug("prefix is not null-terminated\n");
		return -EINVAL;
	}

	return 0;
}

static struct xt_target log_tg_regs[] __read_mostly = {
	{
		.name		= "LOG",
		.family		= NFPROTO_IPV4,
		.target		= log_tg,
		.targetsize	= sizeof(struct xt_log_info),
		.checkentry	= log_tg_check,
		.me		= THIS_MODULE,
	},
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	{
		.name		= "LOG",
		.family		= NFPROTO_IPV6,
		.target		= log_tg,
		.targetsize	= sizeof(struct xt_log_info),
		.checkentry	= log_tg_check,
		.me		= THIS_MODULE,
	},
#endif
};

static struct nf_logger ipt_log_logger __read_mostly = {
	.name		= "ipt_LOG",
	.logfn		= &ipt_log_packet,
	.me		= THIS_MODULE,
};

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
static struct nf_logger ip6t_log_logger __read_mostly = {
	.name		= "ip6t_LOG",
	.logfn		= &ip6t_log_packet,
	.me		= THIS_MODULE,
};
#endif

static int __init log_tg_init(void)
{
	int ret;

	ret = xt_register_targets(log_tg_regs, ARRAY_SIZE(log_tg_regs));
	if (ret < 0)
		return ret;

	nf_log_register(NFPROTO_IPV4, &ipt_log_logger);
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	nf_log_register(NFPROTO_IPV6, &ip6t_log_logger);
#endif
	return 0;
}

static void __exit log_tg_exit(void)
{
	nf_log_unregister(&ipt_log_logger);
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	nf_log_unregister(&ip6t_log_logger);
#endif
	xt_unregister_targets(log_tg_regs, ARRAY_SIZE(log_tg_regs));
}

module_init(log_tg_init);
module_exit(log_tg_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_AUTHOR("Jan Rekorajski <baggins@pld.org.pl>");
MODULE_DESCRIPTION("Xtables: IPv4/IPv6 packet logging");
MODULE_ALIAS("ipt_LOG");
MODULE_ALIAS("ip6t_LOG");
