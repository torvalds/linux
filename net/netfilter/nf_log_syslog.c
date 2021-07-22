// SPDX-License-Identifier: GPL-2.0-only
/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
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
#include <linux/netfilter_bridge.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/xt_LOG.h>
#include <net/netfilter/nf_log.h>

static const struct nf_loginfo default_loginfo = {
	.type	= NF_LOG_TYPE_LOG,
	.u = {
		.log = {
			.level	  = LOGLEVEL_NOTICE,
			.logflags = NF_LOG_DEFAULT_MASK,
		},
	},
};

struct arppayload {
	unsigned char mac_src[ETH_ALEN];
	unsigned char ip_src[4];
	unsigned char mac_dst[ETH_ALEN];
	unsigned char ip_dst[4];
};

static void nf_log_dump_vlan(struct nf_log_buf *m, const struct sk_buff *skb)
{
	u16 vid;

	if (!skb_vlan_tag_present(skb))
		return;

	vid = skb_vlan_tag_get(skb);
	nf_log_buf_add(m, "VPROTO=%04x VID=%u ", ntohs(skb->vlan_proto), vid);
}
static void noinline_for_stack
dump_arp_packet(struct nf_log_buf *m,
		const struct nf_loginfo *info,
		const struct sk_buff *skb, unsigned int nhoff)
{
	const struct arppayload *ap;
	struct arppayload _arpp;
	const struct arphdr *ah;
	unsigned int logflags;
	struct arphdr _arph;

	ah = skb_header_pointer(skb, 0, sizeof(_arph), &_arph);
	if (!ah) {
		nf_log_buf_add(m, "TRUNCATED");
		return;
	}

	if (info->type == NF_LOG_TYPE_LOG)
		logflags = info->u.log.logflags;
	else
		logflags = NF_LOG_DEFAULT_MASK;

	if (logflags & NF_LOG_MACDECODE) {
		nf_log_buf_add(m, "MACSRC=%pM MACDST=%pM ",
			       eth_hdr(skb)->h_source, eth_hdr(skb)->h_dest);
		nf_log_dump_vlan(m, skb);
		nf_log_buf_add(m, "MACPROTO=%04x ",
			       ntohs(eth_hdr(skb)->h_proto));
	}

	nf_log_buf_add(m, "ARP HTYPE=%d PTYPE=0x%04x OPCODE=%d",
		       ntohs(ah->ar_hrd), ntohs(ah->ar_pro), ntohs(ah->ar_op));
	/* If it's for Ethernet and the lengths are OK, then log the ARP
	 * payload.
	 */
	if (ah->ar_hrd != htons(ARPHRD_ETHER) ||
	    ah->ar_hln != ETH_ALEN ||
	    ah->ar_pln != sizeof(__be32))
		return;

	ap = skb_header_pointer(skb, sizeof(_arph), sizeof(_arpp), &_arpp);
	if (!ap) {
		nf_log_buf_add(m, " INCOMPLETE [%zu bytes]",
			       skb->len - sizeof(_arph));
		return;
	}
	nf_log_buf_add(m, " MACSRC=%pM IPSRC=%pI4 MACDST=%pM IPDST=%pI4",
		       ap->mac_src, ap->ip_src, ap->mac_dst, ap->ip_dst);
}

static void
nf_log_dump_packet_common(struct nf_log_buf *m, u8 pf,
			  unsigned int hooknum, const struct sk_buff *skb,
			  const struct net_device *in,
			  const struct net_device *out,
			  const struct nf_loginfo *loginfo, const char *prefix)
{
	const struct net_device *physoutdev __maybe_unused;
	const struct net_device *physindev __maybe_unused;

	nf_log_buf_add(m, KERN_SOH "%c%sIN=%s OUT=%s ",
		       '0' + loginfo->u.log.level, prefix,
			in ? in->name : "",
			out ? out->name : "");
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	physindev = nf_bridge_get_physindev(skb);
	if (physindev && in != physindev)
		nf_log_buf_add(m, "PHYSIN=%s ", physindev->name);
	physoutdev = nf_bridge_get_physoutdev(skb);
	if (physoutdev && out != physoutdev)
		nf_log_buf_add(m, "PHYSOUT=%s ", physoutdev->name);
#endif
}

static void nf_log_arp_packet(struct net *net, u_int8_t pf,
			      unsigned int hooknum, const struct sk_buff *skb,
			      const struct net_device *in,
			      const struct net_device *out,
			      const struct nf_loginfo *loginfo,
			      const char *prefix)
{
	struct nf_log_buf *m;

	/* FIXME: Disabled from containers until syslog ns is supported */
	if (!net_eq(net, &init_net) && !sysctl_nf_log_all_netns)
		return;

	m = nf_log_buf_open();

	if (!loginfo)
		loginfo = &default_loginfo;

	nf_log_dump_packet_common(m, pf, hooknum, skb, in, out, loginfo,
				  prefix);
	dump_arp_packet(m, loginfo, skb, 0);

	nf_log_buf_close(m);
}

static struct nf_logger nf_arp_logger __read_mostly = {
	.name		= "nf_log_arp",
	.type		= NF_LOG_TYPE_LOG,
	.logfn		= nf_log_arp_packet,
	.me		= THIS_MODULE,
};

static void nf_log_dump_sk_uid_gid(struct net *net, struct nf_log_buf *m,
				   struct sock *sk)
{
	if (!sk || !sk_fullsock(sk) || !net_eq(net, sock_net(sk)))
		return;

	read_lock_bh(&sk->sk_callback_lock);
	if (sk->sk_socket && sk->sk_socket->file) {
		const struct cred *cred = sk->sk_socket->file->f_cred;

		nf_log_buf_add(m, "UID=%u GID=%u ",
			       from_kuid_munged(&init_user_ns, cred->fsuid),
			       from_kgid_munged(&init_user_ns, cred->fsgid));
	}
	read_unlock_bh(&sk->sk_callback_lock);
}

static noinline_for_stack int
nf_log_dump_tcp_header(struct nf_log_buf *m,
		       const struct sk_buff *skb,
		       u8 proto, int fragment,
		       unsigned int offset,
		       unsigned int logflags)
{
	struct tcphdr _tcph;
	const struct tcphdr *th;

	/* Max length: 10 "PROTO=TCP " */
	nf_log_buf_add(m, "PROTO=TCP ");

	if (fragment)
		return 0;

	/* Max length: 25 "INCOMPLETE [65535 bytes] " */
	th = skb_header_pointer(skb, offset, sizeof(_tcph), &_tcph);
	if (!th) {
		nf_log_buf_add(m, "INCOMPLETE [%u bytes] ", skb->len - offset);
		return 1;
	}

	/* Max length: 20 "SPT=65535 DPT=65535 " */
	nf_log_buf_add(m, "SPT=%u DPT=%u ",
		       ntohs(th->source), ntohs(th->dest));
	/* Max length: 30 "SEQ=4294967295 ACK=4294967295 " */
	if (logflags & NF_LOG_TCPSEQ) {
		nf_log_buf_add(m, "SEQ=%u ACK=%u ",
			       ntohl(th->seq), ntohl(th->ack_seq));
	}

	/* Max length: 13 "WINDOW=65535 " */
	nf_log_buf_add(m, "WINDOW=%u ", ntohs(th->window));
	/* Max length: 9 "RES=0x3C " */
	nf_log_buf_add(m, "RES=0x%02x ", (u_int8_t)(ntohl(tcp_flag_word(th) &
					    TCP_RESERVED_BITS) >> 22));
	/* Max length: 32 "CWR ECE URG ACK PSH RST SYN FIN " */
	if (th->cwr)
		nf_log_buf_add(m, "CWR ");
	if (th->ece)
		nf_log_buf_add(m, "ECE ");
	if (th->urg)
		nf_log_buf_add(m, "URG ");
	if (th->ack)
		nf_log_buf_add(m, "ACK ");
	if (th->psh)
		nf_log_buf_add(m, "PSH ");
	if (th->rst)
		nf_log_buf_add(m, "RST ");
	if (th->syn)
		nf_log_buf_add(m, "SYN ");
	if (th->fin)
		nf_log_buf_add(m, "FIN ");
	/* Max length: 11 "URGP=65535 " */
	nf_log_buf_add(m, "URGP=%u ", ntohs(th->urg_ptr));

	if ((logflags & NF_LOG_TCPOPT) && th->doff * 4 > sizeof(struct tcphdr)) {
		unsigned int optsize = th->doff * 4 - sizeof(struct tcphdr);
		u8 _opt[60 - sizeof(struct tcphdr)];
		unsigned int i;
		const u8 *op;

		op = skb_header_pointer(skb, offset + sizeof(struct tcphdr),
					optsize, _opt);
		if (!op) {
			nf_log_buf_add(m, "OPT (TRUNCATED)");
			return 1;
		}

		/* Max length: 127 "OPT (" 15*4*2chars ") " */
		nf_log_buf_add(m, "OPT (");
		for (i = 0; i < optsize; i++)
			nf_log_buf_add(m, "%02X", op[i]);

		nf_log_buf_add(m, ") ");
	}

	return 0;
}

static noinline_for_stack int
nf_log_dump_udp_header(struct nf_log_buf *m,
		       const struct sk_buff *skb,
		       u8 proto, int fragment,
		       unsigned int offset)
{
	struct udphdr _udph;
	const struct udphdr *uh;

	if (proto == IPPROTO_UDP)
		/* Max length: 10 "PROTO=UDP "     */
		nf_log_buf_add(m, "PROTO=UDP ");
	else	/* Max length: 14 "PROTO=UDPLITE " */
		nf_log_buf_add(m, "PROTO=UDPLITE ");

	if (fragment)
		goto out;

	/* Max length: 25 "INCOMPLETE [65535 bytes] " */
	uh = skb_header_pointer(skb, offset, sizeof(_udph), &_udph);
	if (!uh) {
		nf_log_buf_add(m, "INCOMPLETE [%u bytes] ", skb->len - offset);

		return 1;
	}

	/* Max length: 20 "SPT=65535 DPT=65535 " */
	nf_log_buf_add(m, "SPT=%u DPT=%u LEN=%u ",
		       ntohs(uh->source), ntohs(uh->dest), ntohs(uh->len));

out:
	return 0;
}

/* One level of recursion won't kill us */
static noinline_for_stack void
dump_ipv4_packet(struct net *net, struct nf_log_buf *m,
		 const struct nf_loginfo *info,
		 const struct sk_buff *skb, unsigned int iphoff)
{
	const struct iphdr *ih;
	unsigned int logflags;
	struct iphdr _iph;

	if (info->type == NF_LOG_TYPE_LOG)
		logflags = info->u.log.logflags;
	else
		logflags = NF_LOG_DEFAULT_MASK;

	ih = skb_header_pointer(skb, iphoff, sizeof(_iph), &_iph);
	if (!ih) {
		nf_log_buf_add(m, "TRUNCATED");
		return;
	}

	/* Important fields:
	 * TOS, len, DF/MF, fragment offset, TTL, src, dst, options.
	 * Max length: 40 "SRC=255.255.255.255 DST=255.255.255.255 "
	 */
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

	if ((logflags & NF_LOG_IPOPT) &&
	    ih->ihl * 4 > sizeof(struct iphdr)) {
		unsigned char _opt[4 * 15 - sizeof(struct iphdr)];
		const unsigned char *op;
		unsigned int i, optsize;

		optsize = ih->ihl * 4 - sizeof(struct iphdr);
		op = skb_header_pointer(skb, iphoff + sizeof(_iph),
					optsize, _opt);
		if (!op) {
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
					   iphoff + ih->ihl * 4, logflags))
			return;
		break;
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
		if (nf_log_dump_udp_header(m, skb, ih->protocol,
					   ntohs(ih->frag_off) & IP_OFFSET,
					   iphoff + ih->ihl * 4))
			return;
		break;
	case IPPROTO_ICMP: {
		static const size_t required_len[NR_ICMP_TYPES + 1] = {
			[ICMP_ECHOREPLY] = 4,
			[ICMP_DEST_UNREACH] = 8 + sizeof(struct iphdr),
			[ICMP_SOURCE_QUENCH] = 8 + sizeof(struct iphdr),
			[ICMP_REDIRECT] = 8 + sizeof(struct iphdr),
			[ICMP_ECHO] = 4,
			[ICMP_TIME_EXCEEDED] = 8 + sizeof(struct iphdr),
			[ICMP_PARAMETERPROB] = 8 + sizeof(struct iphdr),
			[ICMP_TIMESTAMP] = 20,
			[ICMP_TIMESTAMPREPLY] = 20,
			[ICMP_ADDRESS] = 12,
			[ICMP_ADDRESSREPLY] = 12 };
		const struct icmphdr *ich;
		struct icmphdr _icmph;

		/* Max length: 11 "PROTO=ICMP " */
		nf_log_buf_add(m, "PROTO=ICMP ");

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		ich = skb_header_pointer(skb, iphoff + ih->ihl * 4,
					 sizeof(_icmph), &_icmph);
		if (!ich) {
			nf_log_buf_add(m, "INCOMPLETE [%u bytes] ",
				       skb->len - iphoff - ih->ihl * 4);
			break;
		}

		/* Max length: 18 "TYPE=255 CODE=255 " */
		nf_log_buf_add(m, "TYPE=%u CODE=%u ", ich->type, ich->code);

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		if (ich->type <= NR_ICMP_TYPES &&
		    required_len[ich->type] &&
		    skb->len - iphoff - ih->ihl * 4 < required_len[ich->type]) {
			nf_log_buf_add(m, "INCOMPLETE [%u bytes] ",
				       skb->len - iphoff - ih->ihl * 4);
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
			fallthrough;
		case ICMP_DEST_UNREACH:
		case ICMP_SOURCE_QUENCH:
		case ICMP_TIME_EXCEEDED:
			/* Max length: 3+maxlen */
			if (!iphoff) { /* Only recurse once. */
				nf_log_buf_add(m, "[");
				dump_ipv4_packet(net, m, info, skb,
						 iphoff + ih->ihl * 4 + sizeof(_icmph));
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
		const struct ip_auth_hdr *ah;
		struct ip_auth_hdr _ahdr;

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 9 "PROTO=AH " */
		nf_log_buf_add(m, "PROTO=AH ");

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		ah = skb_header_pointer(skb, iphoff + ih->ihl * 4,
					sizeof(_ahdr), &_ahdr);
		if (!ah) {
			nf_log_buf_add(m, "INCOMPLETE [%u bytes] ",
				       skb->len - iphoff - ih->ihl * 4);
			break;
		}

		/* Length: 15 "SPI=0xF1234567 " */
		nf_log_buf_add(m, "SPI=0x%x ", ntohl(ah->spi));
		break;
	}
	case IPPROTO_ESP: {
		const struct ip_esp_hdr *eh;
		struct ip_esp_hdr _esph;

		/* Max length: 10 "PROTO=ESP " */
		nf_log_buf_add(m, "PROTO=ESP ");

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		eh = skb_header_pointer(skb, iphoff + ih->ihl * 4,
					sizeof(_esph), &_esph);
		if (!eh) {
			nf_log_buf_add(m, "INCOMPLETE [%u bytes] ",
				       skb->len - iphoff - ih->ihl * 4);
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
	if ((logflags & NF_LOG_UID) && !iphoff)
		nf_log_dump_sk_uid_gid(net, m, skb->sk);

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

static noinline_for_stack void
dump_ipv6_packet(struct net *net, struct nf_log_buf *m,
		 const struct nf_loginfo *info,
		 const struct sk_buff *skb, unsigned int ip6hoff,
		 int recurse)
{
	const struct ipv6hdr *ih;
	unsigned int hdrlen = 0;
	unsigned int logflags;
	struct ipv6hdr _ip6h;
	unsigned int ptr;
	u8 currenthdr;
	int fragment;

	if (info->type == NF_LOG_TYPE_LOG)
		logflags = info->u.log.logflags;
	else
		logflags = NF_LOG_DEFAULT_MASK;

	ih = skb_header_pointer(skb, ip6hoff, sizeof(_ip6h), &_ip6h);
	if (!ih) {
		nf_log_buf_add(m, "TRUNCATED");
		return;
	}

	/* Max length: 88 "SRC=0000.0000.0000.0000.0000.0000.0000.0000 DST=0000.0000.0000.0000.0000.0000.0000.0000 " */
	nf_log_buf_add(m, "SRC=%pI6 DST=%pI6 ", &ih->saddr, &ih->daddr);

	/* Max length: 44 "LEN=65535 TC=255 HOPLIMIT=255 FLOWLBL=FFFFF " */
	nf_log_buf_add(m, "LEN=%zu TC=%u HOPLIMIT=%u FLOWLBL=%u ",
		       ntohs(ih->payload_len) + sizeof(struct ipv6hdr),
		       (ntohl(*(__be32 *)ih) & 0x0ff00000) >> 20,
		       ih->hop_limit,
		       (ntohl(*(__be32 *)ih) & 0x000fffff));

	fragment = 0;
	ptr = ip6hoff + sizeof(struct ipv6hdr);
	currenthdr = ih->nexthdr;
	while (currenthdr != NEXTHDR_NONE && nf_ip6_ext_hdr(currenthdr)) {
		struct ipv6_opt_hdr _hdr;
		const struct ipv6_opt_hdr *hp;

		hp = skb_header_pointer(skb, ptr, sizeof(_hdr), &_hdr);
		if (!hp) {
			nf_log_buf_add(m, "TRUNCATED");
			return;
		}

		/* Max length: 48 "OPT (...) " */
		if (logflags & NF_LOG_IPOPT)
			nf_log_buf_add(m, "OPT ( ");

		switch (currenthdr) {
		case IPPROTO_FRAGMENT: {
			struct frag_hdr _fhdr;
			const struct frag_hdr *fh;

			nf_log_buf_add(m, "FRAG:");
			fh = skb_header_pointer(skb, ptr, sizeof(_fhdr),
						&_fhdr);
			if (!fh) {
				nf_log_buf_add(m, "TRUNCATED ");
				return;
			}

			/* Max length: 6 "65535 " */
			nf_log_buf_add(m, "%u ", ntohs(fh->frag_off) & 0xFFF8);

			/* Max length: 11 "INCOMPLETE " */
			if (fh->frag_off & htons(0x0001))
				nf_log_buf_add(m, "INCOMPLETE ");

			nf_log_buf_add(m, "ID:%08x ",
				       ntohl(fh->identification));

			if (ntohs(fh->frag_off) & 0xFFF8)
				fragment = 1;

			hdrlen = 8;
			break;
		}
		case IPPROTO_DSTOPTS:
		case IPPROTO_ROUTING:
		case IPPROTO_HOPOPTS:
			if (fragment) {
				if (logflags & NF_LOG_IPOPT)
					nf_log_buf_add(m, ")");
				return;
			}
			hdrlen = ipv6_optlen(hp);
			break;
		/* Max Length */
		case IPPROTO_AH:
			if (logflags & NF_LOG_IPOPT) {
				struct ip_auth_hdr _ahdr;
				const struct ip_auth_hdr *ah;

				/* Max length: 3 "AH " */
				nf_log_buf_add(m, "AH ");

				if (fragment) {
					nf_log_buf_add(m, ")");
					return;
				}

				ah = skb_header_pointer(skb, ptr, sizeof(_ahdr),
							&_ahdr);
				if (!ah) {
					/* Max length: 26 "INCOMPLETE [65535 bytes] )" */
					nf_log_buf_add(m, "INCOMPLETE [%u bytes] )",
						       skb->len - ptr);
					return;
				}

				/* Length: 15 "SPI=0xF1234567 */
				nf_log_buf_add(m, "SPI=0x%x ", ntohl(ah->spi));
			}

			hdrlen = ipv6_authlen(hp);
			break;
		case IPPROTO_ESP:
			if (logflags & NF_LOG_IPOPT) {
				struct ip_esp_hdr _esph;
				const struct ip_esp_hdr *eh;

				/* Max length: 4 "ESP " */
				nf_log_buf_add(m, "ESP ");

				if (fragment) {
					nf_log_buf_add(m, ")");
					return;
				}

				/* Max length: 26 "INCOMPLETE [65535 bytes] )" */
				eh = skb_header_pointer(skb, ptr, sizeof(_esph),
							&_esph);
				if (!eh) {
					nf_log_buf_add(m, "INCOMPLETE [%u bytes] )",
						       skb->len - ptr);
					return;
				}

				/* Length: 16 "SPI=0xF1234567 )" */
				nf_log_buf_add(m, "SPI=0x%x )",
					       ntohl(eh->spi));
			}
			return;
		default:
			/* Max length: 20 "Unknown Ext Hdr 255" */
			nf_log_buf_add(m, "Unknown Ext Hdr %u", currenthdr);
			return;
		}
		if (logflags & NF_LOG_IPOPT)
			nf_log_buf_add(m, ") ");

		currenthdr = hp->nexthdr;
		ptr += hdrlen;
	}

	switch (currenthdr) {
	case IPPROTO_TCP:
		if (nf_log_dump_tcp_header(m, skb, currenthdr, fragment,
					   ptr, logflags))
			return;
		break;
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
		if (nf_log_dump_udp_header(m, skb, currenthdr, fragment, ptr))
			return;
		break;
	case IPPROTO_ICMPV6: {
		struct icmp6hdr _icmp6h;
		const struct icmp6hdr *ic;

		/* Max length: 13 "PROTO=ICMPv6 " */
		nf_log_buf_add(m, "PROTO=ICMPv6 ");

		if (fragment)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		ic = skb_header_pointer(skb, ptr, sizeof(_icmp6h), &_icmp6h);
		if (!ic) {
			nf_log_buf_add(m, "INCOMPLETE [%u bytes] ",
				       skb->len - ptr);
			return;
		}

		/* Max length: 18 "TYPE=255 CODE=255 " */
		nf_log_buf_add(m, "TYPE=%u CODE=%u ",
			       ic->icmp6_type, ic->icmp6_code);

		switch (ic->icmp6_type) {
		case ICMPV6_ECHO_REQUEST:
		case ICMPV6_ECHO_REPLY:
			/* Max length: 19 "ID=65535 SEQ=65535 " */
			nf_log_buf_add(m, "ID=%u SEQ=%u ",
				       ntohs(ic->icmp6_identifier),
				       ntohs(ic->icmp6_sequence));
			break;
		case ICMPV6_MGM_QUERY:
		case ICMPV6_MGM_REPORT:
		case ICMPV6_MGM_REDUCTION:
			break;

		case ICMPV6_PARAMPROB:
			/* Max length: 17 "POINTER=ffffffff " */
			nf_log_buf_add(m, "POINTER=%08x ",
				       ntohl(ic->icmp6_pointer));
			fallthrough;
		case ICMPV6_DEST_UNREACH:
		case ICMPV6_PKT_TOOBIG:
		case ICMPV6_TIME_EXCEED:
			/* Max length: 3+maxlen */
			if (recurse) {
				nf_log_buf_add(m, "[");
				dump_ipv6_packet(net, m, info, skb,
						 ptr + sizeof(_icmp6h), 0);
				nf_log_buf_add(m, "] ");
			}

			/* Max length: 10 "MTU=65535 " */
			if (ic->icmp6_type == ICMPV6_PKT_TOOBIG) {
				nf_log_buf_add(m, "MTU=%u ",
					       ntohl(ic->icmp6_mtu));
			}
		}
		break;
	}
	/* Max length: 10 "PROTO=255 " */
	default:
		nf_log_buf_add(m, "PROTO=%u ", currenthdr);
	}

	/* Max length: 15 "UID=4294967295 " */
	if ((logflags & NF_LOG_UID) && recurse)
		nf_log_dump_sk_uid_gid(net, m, skb->sk);

	/* Max length: 16 "MARK=0xFFFFFFFF " */
	if (recurse && skb->mark)
		nf_log_buf_add(m, "MARK=0x%x ", skb->mark);
}

static void dump_ipv4_mac_header(struct nf_log_buf *m,
				 const struct nf_loginfo *info,
				 const struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	unsigned int logflags = 0;

	if (info->type == NF_LOG_TYPE_LOG)
		logflags = info->u.log.logflags;

	if (!(logflags & NF_LOG_MACDECODE))
		goto fallback;

	switch (dev->type) {
	case ARPHRD_ETHER:
		nf_log_buf_add(m, "MACSRC=%pM MACDST=%pM ",
			       eth_hdr(skb)->h_source, eth_hdr(skb)->h_dest);
		nf_log_dump_vlan(m, skb);
		nf_log_buf_add(m, "MACPROTO=%04x ",
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
	if (!net_eq(net, &init_net) && !sysctl_nf_log_all_netns)
		return;

	m = nf_log_buf_open();

	if (!loginfo)
		loginfo = &default_loginfo;

	nf_log_dump_packet_common(m, pf, hooknum, skb, in,
				  out, loginfo, prefix);

	if (in)
		dump_ipv4_mac_header(m, loginfo, skb);

	dump_ipv4_packet(net, m, loginfo, skb, 0);

	nf_log_buf_close(m);
}

static struct nf_logger nf_ip_logger __read_mostly = {
	.name		= "nf_log_ipv4",
	.type		= NF_LOG_TYPE_LOG,
	.logfn		= nf_log_ip_packet,
	.me		= THIS_MODULE,
};

static void dump_ipv6_mac_header(struct nf_log_buf *m,
				 const struct nf_loginfo *info,
				 const struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	unsigned int logflags = 0;

	if (info->type == NF_LOG_TYPE_LOG)
		logflags = info->u.log.logflags;

	if (!(logflags & NF_LOG_MACDECODE))
		goto fallback;

	switch (dev->type) {
	case ARPHRD_ETHER:
		nf_log_buf_add(m, "MACSRC=%pM MACDST=%pM ",
			       eth_hdr(skb)->h_source, eth_hdr(skb)->h_dest);
		nf_log_dump_vlan(m, skb);
		nf_log_buf_add(m, "MACPROTO=%04x ",
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
		unsigned int len = dev->hard_header_len;
		unsigned int i;

		if (dev->type == ARPHRD_SIT) {
			p -= ETH_HLEN;

			if (p < skb->head)
				p = NULL;
		}

		if (p) {
			nf_log_buf_add(m, "%02x", *p++);
			for (i = 1; i < len; i++)
				nf_log_buf_add(m, ":%02x", *p++);
		}
		nf_log_buf_add(m, " ");

		if (dev->type == ARPHRD_SIT) {
			const struct iphdr *iph =
				(struct iphdr *)skb_mac_header(skb);
			nf_log_buf_add(m, "TUNNEL=%pI4->%pI4 ", &iph->saddr,
				       &iph->daddr);
		}
	} else {
		nf_log_buf_add(m, " ");
	}
}

static void nf_log_ip6_packet(struct net *net, u_int8_t pf,
			      unsigned int hooknum, const struct sk_buff *skb,
			      const struct net_device *in,
			      const struct net_device *out,
			      const struct nf_loginfo *loginfo,
			      const char *prefix)
{
	struct nf_log_buf *m;

	/* FIXME: Disabled from containers until syslog ns is supported */
	if (!net_eq(net, &init_net) && !sysctl_nf_log_all_netns)
		return;

	m = nf_log_buf_open();

	if (!loginfo)
		loginfo = &default_loginfo;

	nf_log_dump_packet_common(m, pf, hooknum, skb, in, out,
				  loginfo, prefix);

	if (in)
		dump_ipv6_mac_header(m, loginfo, skb);

	dump_ipv6_packet(net, m, loginfo, skb, skb_network_offset(skb), 1);

	nf_log_buf_close(m);
}

static struct nf_logger nf_ip6_logger __read_mostly = {
	.name		= "nf_log_ipv6",
	.type		= NF_LOG_TYPE_LOG,
	.logfn		= nf_log_ip6_packet,
	.me		= THIS_MODULE,
};

static void nf_log_netdev_packet(struct net *net, u_int8_t pf,
				 unsigned int hooknum,
				 const struct sk_buff *skb,
				 const struct net_device *in,
				 const struct net_device *out,
				 const struct nf_loginfo *loginfo,
				 const char *prefix)
{
	switch (skb->protocol) {
	case htons(ETH_P_IP):
		nf_log_ip_packet(net, pf, hooknum, skb, in, out, loginfo, prefix);
		break;
	case htons(ETH_P_IPV6):
		nf_log_ip6_packet(net, pf, hooknum, skb, in, out, loginfo, prefix);
		break;
	case htons(ETH_P_ARP):
	case htons(ETH_P_RARP):
		nf_log_arp_packet(net, pf, hooknum, skb, in, out, loginfo, prefix);
		break;
	}
}

static struct nf_logger nf_netdev_logger __read_mostly = {
	.name		= "nf_log_netdev",
	.type		= NF_LOG_TYPE_LOG,
	.logfn		= nf_log_netdev_packet,
	.me		= THIS_MODULE,
};

static struct nf_logger nf_bridge_logger __read_mostly = {
	.name		= "nf_log_bridge",
	.type		= NF_LOG_TYPE_LOG,
	.logfn		= nf_log_netdev_packet,
	.me		= THIS_MODULE,
};

static int __net_init nf_log_syslog_net_init(struct net *net)
{
	int ret = nf_log_set(net, NFPROTO_IPV4, &nf_ip_logger);

	if (ret)
		return ret;

	ret = nf_log_set(net, NFPROTO_ARP, &nf_arp_logger);
	if (ret)
		goto err1;

	ret = nf_log_set(net, NFPROTO_IPV6, &nf_ip6_logger);
	if (ret)
		goto err2;

	ret = nf_log_set(net, NFPROTO_NETDEV, &nf_netdev_logger);
	if (ret)
		goto err3;

	ret = nf_log_set(net, NFPROTO_BRIDGE, &nf_bridge_logger);
	if (ret)
		goto err4;
	return 0;
err4:
	nf_log_unset(net, &nf_netdev_logger);
err3:
	nf_log_unset(net, &nf_ip6_logger);
err2:
	nf_log_unset(net, &nf_arp_logger);
err1:
	nf_log_unset(net, &nf_ip_logger);
	return ret;
}

static void __net_exit nf_log_syslog_net_exit(struct net *net)
{
	nf_log_unset(net, &nf_ip_logger);
	nf_log_unset(net, &nf_arp_logger);
	nf_log_unset(net, &nf_ip6_logger);
	nf_log_unset(net, &nf_netdev_logger);
	nf_log_unset(net, &nf_bridge_logger);
}

static struct pernet_operations nf_log_syslog_net_ops = {
	.init = nf_log_syslog_net_init,
	.exit = nf_log_syslog_net_exit,
};

static int __init nf_log_syslog_init(void)
{
	int ret;

	ret = register_pernet_subsys(&nf_log_syslog_net_ops);
	if (ret < 0)
		return ret;

	ret = nf_log_register(NFPROTO_IPV4, &nf_ip_logger);
	if (ret < 0)
		goto err1;

	ret = nf_log_register(NFPROTO_ARP, &nf_arp_logger);
	if (ret < 0)
		goto err2;

	ret = nf_log_register(NFPROTO_IPV6, &nf_ip6_logger);
	if (ret < 0)
		goto err3;

	ret = nf_log_register(NFPROTO_NETDEV, &nf_netdev_logger);
	if (ret < 0)
		goto err4;

	ret = nf_log_register(NFPROTO_BRIDGE, &nf_bridge_logger);
	if (ret < 0)
		goto err5;

	return 0;
err5:
	nf_log_unregister(&nf_netdev_logger);
err4:
	nf_log_unregister(&nf_ip6_logger);
err3:
	nf_log_unregister(&nf_arp_logger);
err2:
	nf_log_unregister(&nf_ip_logger);
err1:
	pr_err("failed to register logger\n");
	unregister_pernet_subsys(&nf_log_syslog_net_ops);
	return ret;
}

static void __exit nf_log_syslog_exit(void)
{
	unregister_pernet_subsys(&nf_log_syslog_net_ops);
	nf_log_unregister(&nf_ip_logger);
	nf_log_unregister(&nf_arp_logger);
	nf_log_unregister(&nf_ip6_logger);
	nf_log_unregister(&nf_netdev_logger);
	nf_log_unregister(&nf_bridge_logger);
}

module_init(nf_log_syslog_init);
module_exit(nf_log_syslog_exit);

MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("Netfilter syslog packet logging");
MODULE_LICENSE("GPL");
MODULE_ALIAS("nf_log_arp");
MODULE_ALIAS("nf_log_bridge");
MODULE_ALIAS("nf_log_ipv4");
MODULE_ALIAS("nf_log_ipv6");
MODULE_ALIAS("nf_log_netdev");
MODULE_ALIAS_NF_LOGGER(AF_BRIDGE, 0);
MODULE_ALIAS_NF_LOGGER(AF_INET, 0);
MODULE_ALIAS_NF_LOGGER(3, 0);
MODULE_ALIAS_NF_LOGGER(5, 0); /* NFPROTO_NETDEV */
MODULE_ALIAS_NF_LOGGER(AF_INET6, 0);
