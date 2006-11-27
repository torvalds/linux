/*
 * This is a module which is used for logging packets.
 */

/* (C) 2001 Jan Rekorajski <baggins@pld.org.pl>
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/spinlock.h>
#include <linux/icmpv6.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_AUTHOR("Jan Rekorajski <baggins@pld.org.pl>");
MODULE_DESCRIPTION("IP6 tables LOG target module");
MODULE_LICENSE("GPL");

struct in_device;
#include <net/route.h>
#include <linux/netfilter_ipv6/ip6t_LOG.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* Use lock to serialize, so printks don't overlap */
static DEFINE_SPINLOCK(log_lock);

/* One level of recursion won't kill us */
static void dump_packet(const struct nf_loginfo *info,
			const struct sk_buff *skb, unsigned int ip6hoff,
			int recurse)
{
	u_int8_t currenthdr;
	int fragment;
	struct ipv6hdr _ip6h, *ih;
	unsigned int ptr;
	unsigned int hdrlen = 0;
	unsigned int logflags;

	if (info->type == NF_LOG_TYPE_LOG)
		logflags = info->u.log.logflags;
	else
		logflags = NF_LOG_MASK;

	ih = skb_header_pointer(skb, ip6hoff, sizeof(_ip6h), &_ip6h);
	if (ih == NULL) {
		printk("TRUNCATED");
		return;
	}

	/* Max length: 88 "SRC=0000.0000.0000.0000.0000.0000.0000.0000 DST=0000.0000.0000.0000.0000.0000.0000.0000 " */
	printk("SRC=" NIP6_FMT " DST=" NIP6_FMT " ", NIP6(ih->saddr), NIP6(ih->daddr));

	/* Max length: 44 "LEN=65535 TC=255 HOPLIMIT=255 FLOWLBL=FFFFF " */
	printk("LEN=%Zu TC=%u HOPLIMIT=%u FLOWLBL=%u ",
	       ntohs(ih->payload_len) + sizeof(struct ipv6hdr),
	       (ntohl(*(u_int32_t *)ih) & 0x0ff00000) >> 20,
	       ih->hop_limit,
	       (ntohl(*(u_int32_t *)ih) & 0x000fffff));

	fragment = 0;
	ptr = ip6hoff + sizeof(struct ipv6hdr);
	currenthdr = ih->nexthdr;
	while (currenthdr != NEXTHDR_NONE && ip6t_ext_hdr(currenthdr)) {
		struct ipv6_opt_hdr _hdr, *hp;

		hp = skb_header_pointer(skb, ptr, sizeof(_hdr), &_hdr);
		if (hp == NULL) {
			printk("TRUNCATED");
			return;
		}

		/* Max length: 48 "OPT (...) " */
		if (logflags & IP6T_LOG_IPOPT)
			printk("OPT ( ");

		switch (currenthdr) {
		case IPPROTO_FRAGMENT: {
			struct frag_hdr _fhdr, *fh;

			printk("FRAG:");
			fh = skb_header_pointer(skb, ptr, sizeof(_fhdr),
						&_fhdr);
			if (fh == NULL) {
				printk("TRUNCATED ");
				return;
			}

			/* Max length: 6 "65535 " */
			printk("%u ", ntohs(fh->frag_off) & 0xFFF8);

			/* Max length: 11 "INCOMPLETE " */
			if (fh->frag_off & htons(0x0001))
				printk("INCOMPLETE ");

			printk("ID:%08x ", ntohl(fh->identification));

			if (ntohs(fh->frag_off) & 0xFFF8)
				fragment = 1;

			hdrlen = 8;

			break;
		}
		case IPPROTO_DSTOPTS:
		case IPPROTO_ROUTING:
		case IPPROTO_HOPOPTS:
			if (fragment) {
				if (logflags & IP6T_LOG_IPOPT)
					printk(")");
				return;
			}
			hdrlen = ipv6_optlen(hp);
			break;
		/* Max Length */
		case IPPROTO_AH:
			if (logflags & IP6T_LOG_IPOPT) {
				struct ip_auth_hdr _ahdr, *ah;

				/* Max length: 3 "AH " */
				printk("AH ");

				if (fragment) {
					printk(")");
					return;
				}

				ah = skb_header_pointer(skb, ptr, sizeof(_ahdr),
							&_ahdr);
				if (ah == NULL) {
					/*
					 * Max length: 26 "INCOMPLETE [65535 	
					 *  bytes] )"
					 */
					printk("INCOMPLETE [%u bytes] )",
					       skb->len - ptr);
					return;
				}

				/* Length: 15 "SPI=0xF1234567 */
				printk("SPI=0x%x ", ntohl(ah->spi));

			}

			hdrlen = (hp->hdrlen+2)<<2;
			break;
		case IPPROTO_ESP:
			if (logflags & IP6T_LOG_IPOPT) {
				struct ip_esp_hdr _esph, *eh;

				/* Max length: 4 "ESP " */
				printk("ESP ");

				if (fragment) {
					printk(")");
					return;
				}

				/*
				 * Max length: 26 "INCOMPLETE [65535 bytes] )"
				 */
				eh = skb_header_pointer(skb, ptr, sizeof(_esph),
							&_esph);
				if (eh == NULL) {
					printk("INCOMPLETE [%u bytes] )",
					       skb->len - ptr);
					return;
				}

				/* Length: 16 "SPI=0xF1234567 )" */
				printk("SPI=0x%x )", ntohl(eh->spi) );

			}
			return;
		default:
			/* Max length: 20 "Unknown Ext Hdr 255" */
			printk("Unknown Ext Hdr %u", currenthdr);
			return;
		}
		if (logflags & IP6T_LOG_IPOPT)
			printk(") ");

		currenthdr = hp->nexthdr;
		ptr += hdrlen;
	}

	switch (currenthdr) {
	case IPPROTO_TCP: {
		struct tcphdr _tcph, *th;

		/* Max length: 10 "PROTO=TCP " */
		printk("PROTO=TCP ");

		if (fragment)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		th = skb_header_pointer(skb, ptr, sizeof(_tcph), &_tcph);
		if (th == NULL) {
			printk("INCOMPLETE [%u bytes] ", skb->len - ptr);
			return;
		}

		/* Max length: 20 "SPT=65535 DPT=65535 " */
		printk("SPT=%u DPT=%u ",
		       ntohs(th->source), ntohs(th->dest));
		/* Max length: 30 "SEQ=4294967295 ACK=4294967295 " */
		if (logflags & IP6T_LOG_TCPSEQ)
			printk("SEQ=%u ACK=%u ",
			       ntohl(th->seq), ntohl(th->ack_seq));
		/* Max length: 13 "WINDOW=65535 " */
		printk("WINDOW=%u ", ntohs(th->window));
		/* Max length: 9 "RES=0x3C " */
		printk("RES=0x%02x ", (u_int8_t)(ntohl(tcp_flag_word(th) & TCP_RESERVED_BITS) >> 22));
		/* Max length: 32 "CWR ECE URG ACK PSH RST SYN FIN " */
		if (th->cwr)
			printk("CWR ");
		if (th->ece)
			printk("ECE ");
		if (th->urg)
			printk("URG ");
		if (th->ack)
			printk("ACK ");
		if (th->psh)
			printk("PSH ");
		if (th->rst)
			printk("RST ");
		if (th->syn)
			printk("SYN ");
		if (th->fin)
			printk("FIN ");
		/* Max length: 11 "URGP=65535 " */
		printk("URGP=%u ", ntohs(th->urg_ptr));

		if ((logflags & IP6T_LOG_TCPOPT)
		    && th->doff * 4 > sizeof(struct tcphdr)) {
			u_int8_t _opt[60 - sizeof(struct tcphdr)], *op;
			unsigned int i;
			unsigned int optsize = th->doff * 4
					       - sizeof(struct tcphdr);

			op = skb_header_pointer(skb,
						ptr + sizeof(struct tcphdr),
						optsize, _opt);
			if (op == NULL) {
				printk("OPT (TRUNCATED)");
				return;
			}

			/* Max length: 127 "OPT (" 15*4*2chars ") " */
			printk("OPT (");
			for (i =0; i < optsize; i++)
				printk("%02X", op[i]);
			printk(") ");
		}
		break;
	}
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE: {
		struct udphdr _udph, *uh;

		if (currenthdr == IPPROTO_UDP)
			/* Max length: 10 "PROTO=UDP "     */
			printk("PROTO=UDP " );
		else	/* Max length: 14 "PROTO=UDPLITE " */
			printk("PROTO=UDPLITE ");

		if (fragment)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		uh = skb_header_pointer(skb, ptr, sizeof(_udph), &_udph);
		if (uh == NULL) {
			printk("INCOMPLETE [%u bytes] ", skb->len - ptr);
			return;
		}

		/* Max length: 20 "SPT=65535 DPT=65535 " */
		printk("SPT=%u DPT=%u LEN=%u ",
		       ntohs(uh->source), ntohs(uh->dest),
		       ntohs(uh->len));
		break;
	}
	case IPPROTO_ICMPV6: {
		struct icmp6hdr _icmp6h, *ic;

		/* Max length: 13 "PROTO=ICMPv6 " */
		printk("PROTO=ICMPv6 ");

		if (fragment)
			break;

		/* Max length: 25 "INCOMPLETE [65535 bytes] " */
		ic = skb_header_pointer(skb, ptr, sizeof(_icmp6h), &_icmp6h);
		if (ic == NULL) {
			printk("INCOMPLETE [%u bytes] ", skb->len - ptr);
			return;
		}

		/* Max length: 18 "TYPE=255 CODE=255 " */
		printk("TYPE=%u CODE=%u ", ic->icmp6_type, ic->icmp6_code);

		switch (ic->icmp6_type) {
		case ICMPV6_ECHO_REQUEST:
		case ICMPV6_ECHO_REPLY:
			/* Max length: 19 "ID=65535 SEQ=65535 " */
			printk("ID=%u SEQ=%u ",
				ntohs(ic->icmp6_identifier),
				ntohs(ic->icmp6_sequence));
			break;
		case ICMPV6_MGM_QUERY:
		case ICMPV6_MGM_REPORT:
		case ICMPV6_MGM_REDUCTION:
			break;

		case ICMPV6_PARAMPROB:
			/* Max length: 17 "POINTER=ffffffff " */
			printk("POINTER=%08x ", ntohl(ic->icmp6_pointer));
			/* Fall through */
		case ICMPV6_DEST_UNREACH:
		case ICMPV6_PKT_TOOBIG:
		case ICMPV6_TIME_EXCEED:
			/* Max length: 3+maxlen */
			if (recurse) {
				printk("[");
				dump_packet(info, skb, ptr + sizeof(_icmp6h),
					    0);
				printk("] ");
			}

			/* Max length: 10 "MTU=65535 " */
			if (ic->icmp6_type == ICMPV6_PKT_TOOBIG)
				printk("MTU=%u ", ntohl(ic->icmp6_mtu));
		}
		break;
	}
	/* Max length: 10 "PROTO=255 " */
	default:
		printk("PROTO=%u ", currenthdr);
	}

	/* Max length: 15 "UID=4294967295 " */
	if ((logflags & IP6T_LOG_UID) && recurse && skb->sk) {
		read_lock_bh(&skb->sk->sk_callback_lock);
		if (skb->sk->sk_socket && skb->sk->sk_socket->file)
			printk("UID=%u ", skb->sk->sk_socket->file->f_uid);
		read_unlock_bh(&skb->sk->sk_callback_lock);
	}
}

static struct nf_loginfo default_loginfo = {
	.type	= NF_LOG_TYPE_LOG,
	.u = {
		.log = {
			.level	  = 0,
			.logflags = NF_LOG_MASK,
		},
	},
};

static void
ip6t_log_packet(unsigned int pf,
		unsigned int hooknum,
		const struct sk_buff *skb,
		const struct net_device *in,
		const struct net_device *out,
		const struct nf_loginfo *loginfo,
		const char *prefix)
{
	if (!loginfo)
		loginfo = &default_loginfo;

	spin_lock_bh(&log_lock);
	printk("<%d>%sIN=%s OUT=%s ", loginfo->u.log.level, 
		prefix,
		in ? in->name : "",
		out ? out->name : "");
	if (in && !out) {
		unsigned int len;
		/* MAC logging for input chain only. */
		printk("MAC=");
		if (skb->dev && (len = skb->dev->hard_header_len) &&
		    skb->mac.raw != skb->nh.raw) {
			unsigned char *p = skb->mac.raw;
			int i;

			if (skb->dev->type == ARPHRD_SIT &&
			    (p -= ETH_HLEN) < skb->head)
				p = NULL;

			if (p != NULL) {
				for (i = 0; i < len; i++)
					printk("%02x%s", p[i],
					       i == len - 1 ? "" : ":");
			}
			printk(" ");

			if (skb->dev->type == ARPHRD_SIT) {
				struct iphdr *iph = (struct iphdr *)skb->mac.raw;
				printk("TUNNEL=%u.%u.%u.%u->%u.%u.%u.%u ",
				       NIPQUAD(iph->saddr),
				       NIPQUAD(iph->daddr));
			}
		} else
			printk(" ");
	}

	dump_packet(loginfo, skb, (u8*)skb->nh.ipv6h - skb->data, 1);
	printk("\n");
	spin_unlock_bh(&log_lock);
}

static unsigned int
ip6t_log_target(struct sk_buff **pskb,
		const struct net_device *in,
		const struct net_device *out,
		unsigned int hooknum,
		const struct xt_target *target,
		const void *targinfo)
{
	const struct ip6t_log_info *loginfo = targinfo;
	struct nf_loginfo li;

	li.type = NF_LOG_TYPE_LOG;
	li.u.log.level = loginfo->level;
	li.u.log.logflags = loginfo->logflags;

	if (loginfo->logflags & IP6T_LOG_NFLOG)
		nf_log_packet(PF_INET6, hooknum, *pskb, in, out, &li,
		              "%s", loginfo->prefix);
	else
		ip6t_log_packet(PF_INET6, hooknum, *pskb, in, out, &li,
		                loginfo->prefix);

	return IP6T_CONTINUE;
}


static int ip6t_log_checkentry(const char *tablename,
			       const void *entry,
			       const struct xt_target *target,
			       void *targinfo,
			       unsigned int hook_mask)
{
	const struct ip6t_log_info *loginfo = targinfo;

	if (loginfo->level >= 8) {
		DEBUGP("LOG: level %u >= 8\n", loginfo->level);
		return 0;
	}
	if (loginfo->prefix[sizeof(loginfo->prefix)-1] != '\0') {
		DEBUGP("LOG: prefix term %i\n",
		       loginfo->prefix[sizeof(loginfo->prefix)-1]);
		return 0;
	}
	return 1;
}

static struct ip6t_target ip6t_log_reg = {
	.name 		= "LOG",
	.target 	= ip6t_log_target, 
	.targetsize	= sizeof(struct ip6t_log_info),
	.checkentry	= ip6t_log_checkentry, 
	.me 		= THIS_MODULE,
};

static struct nf_logger ip6t_logger = {
	.name		= "ip6t_LOG",
	.logfn		= &ip6t_log_packet,
	.me		= THIS_MODULE,
};

static int __init ip6t_log_init(void)
{
	if (ip6t_register_target(&ip6t_log_reg))
		return -EINVAL;
	if (nf_log_register(PF_INET6, &ip6t_logger) < 0) {
		printk(KERN_WARNING "ip6t_LOG: not logging via system console "
		       "since somebody else already registered for PF_INET6\n");
		/* we cannot make module load fail here, since otherwise
		 * ip6tables userspace would abort */
	}

	return 0;
}

static void __exit ip6t_log_fini(void)
{
	nf_log_unregister_logger(&ip6t_logger);
	ip6t_unregister_target(&ip6t_log_reg);
}

module_init(ip6t_log_init);
module_exit(ip6t_log_fini);
