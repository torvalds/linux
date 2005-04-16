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

static unsigned int nflog = 1;
module_param(nflog, int, 0400);
MODULE_PARM_DESC(nflog, "register as internal netfilter logging module");
 
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
static void dump_packet(const struct ip6t_log_info *info,
			const struct sk_buff *skb, unsigned int ip6hoff,
			int recurse)
{
	u_int8_t currenthdr;
	int fragment;
	struct ipv6hdr _ip6h, *ih;
	unsigned int ptr;
	unsigned int hdrlen = 0;

	ih = skb_header_pointer(skb, ip6hoff, sizeof(_ip6h), &_ip6h);
	if (ih == NULL) {
		printk("TRUNCATED");
		return;
	}

	/* Max length: 88 "SRC=0000.0000.0000.0000.0000.0000.0000.0000 DST=0000.0000.0000.0000.0000.0000.0000.0000" */
	printk("SRC=%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x ", NIP6(ih->saddr));
	printk("DST=%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x ", NIP6(ih->daddr));

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
		if (info->logflags & IP6T_LOG_IPOPT)
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
				if (info->logflags & IP6T_LOG_IPOPT)
					printk(")");
				return;
			}
			hdrlen = ipv6_optlen(hp);
			break;
		/* Max Length */
		case IPPROTO_AH:
			if (info->logflags & IP6T_LOG_IPOPT) {
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
			if (info->logflags & IP6T_LOG_IPOPT) {
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
		if (info->logflags & IP6T_LOG_IPOPT)
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
		if (info->logflags & IP6T_LOG_TCPSEQ)
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

		if ((info->logflags & IP6T_LOG_TCPOPT)
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
	case IPPROTO_UDP: {
		struct udphdr _udph, *uh;

		/* Max length: 10 "PROTO=UDP " */
		printk("PROTO=UDP ");

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
	if ((info->logflags & IP6T_LOG_UID) && recurse && skb->sk) {
		read_lock_bh(&skb->sk->sk_callback_lock);
		if (skb->sk->sk_socket && skb->sk->sk_socket->file)
			printk("UID=%u ", skb->sk->sk_socket->file->f_uid);
		read_unlock_bh(&skb->sk->sk_callback_lock);
	}
}

static void
ip6t_log_packet(unsigned int hooknum,
		const struct sk_buff *skb,
		const struct net_device *in,
		const struct net_device *out,
		const struct ip6t_log_info *loginfo,
		const char *level_string,
		const char *prefix)
{
	struct ipv6hdr *ipv6h = skb->nh.ipv6h;

	spin_lock_bh(&log_lock);
	printk(level_string);
	printk("%sIN=%s OUT=%s ",
		prefix == NULL ? loginfo->prefix : prefix,
		in ? in->name : "",
		out ? out->name : "");
	if (in && !out) {
		/* MAC logging for input chain only. */
		printk("MAC=");
		if (skb->dev && skb->dev->hard_header_len && skb->mac.raw != (void*)ipv6h) {
			if (skb->dev->type != ARPHRD_SIT){
			  int i;
			  unsigned char *p = skb->mac.raw;
			  for (i = 0; i < skb->dev->hard_header_len; i++,p++)
				printk("%02x%c", *p,
			       		i==skb->dev->hard_header_len - 1
			       		? ' ':':');
			} else {
			  int i;
			  unsigned char *p = skb->mac.raw;
			  if ( p - (ETH_ALEN*2+2) > skb->head ){
			    p -= (ETH_ALEN+2);
			    for (i = 0; i < (ETH_ALEN); i++,p++)
				printk("%02x%s", *p,
					i == ETH_ALEN-1 ? "->" : ":");
			    p -= (ETH_ALEN*2);
			    for (i = 0; i < (ETH_ALEN); i++,p++)
				printk("%02x%c", *p,
					i == ETH_ALEN-1 ? ' ' : ':');
			  }
			  
			  if ((skb->dev->addr_len == 4) &&
			      skb->dev->hard_header_len > 20){
			    printk("TUNNEL=");
			    p = skb->mac.raw + 12;
			    for (i = 0; i < 4; i++,p++)
				printk("%3d%s", *p,
					i == 3 ? "->" : ".");
			    for (i = 0; i < 4; i++,p++)
				printk("%3d%c", *p,
					i == 3 ? ' ' : '.');
			  }
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
		const void *targinfo,
		void *userinfo)
{
	const struct ip6t_log_info *loginfo = targinfo;
	char level_string[4] = "< >";

	level_string[1] = '0' + (loginfo->level % 8);
	ip6t_log_packet(hooknum, *pskb, in, out, loginfo, level_string, NULL);

	return IP6T_CONTINUE;
}

static void
ip6t_logfn(unsigned int hooknum,
	   const struct sk_buff *skb,
	   const struct net_device *in,
	   const struct net_device *out,
	   const char *prefix)
{
	struct ip6t_log_info loginfo = {
		.level = 0,
		.logflags = IP6T_LOG_MASK,
		.prefix = ""
	};

	ip6t_log_packet(hooknum, skb, in, out, &loginfo, KERN_WARNING, prefix);
}

static int ip6t_log_checkentry(const char *tablename,
			       const struct ip6t_entry *e,
			       void *targinfo,
			       unsigned int targinfosize,
			       unsigned int hook_mask)
{
	const struct ip6t_log_info *loginfo = targinfo;

	if (targinfosize != IP6T_ALIGN(sizeof(struct ip6t_log_info))) {
		DEBUGP("LOG: targinfosize %u != %u\n",
		       targinfosize, IP6T_ALIGN(sizeof(struct ip6t_log_info)));
		return 0;
	}

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
	.checkentry	= ip6t_log_checkentry, 
	.me 		= THIS_MODULE,
};

static int __init init(void)
{
	if (ip6t_register_target(&ip6t_log_reg))
		return -EINVAL;
	if (nflog)
		nf_log_register(PF_INET6, &ip6t_logfn);

	return 0;
}

static void __exit fini(void)
{
	if (nflog)
		nf_log_unregister(PF_INET6, &ip6t_logfn);
	ip6t_unregister_target(&ip6t_log_reg);
}

module_init(init);
module_exit(fini);
