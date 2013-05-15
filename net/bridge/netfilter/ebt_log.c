/*
 *  ebt_log
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *	Harald Welte <laforge@netfilter.org>
 *
 *  April, 2002
 *
 */
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/spinlock.h>
#include <net/netfilter/nf_log.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <linux/in6.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_log.h>
#include <linux/netfilter.h>

static DEFINE_SPINLOCK(ebt_log_lock);

static int ebt_log_tg_check(const struct xt_tgchk_param *par)
{
	struct ebt_log_info *info = par->targinfo;

	if (info->bitmask & ~EBT_LOG_MASK)
		return -EINVAL;
	if (info->loglevel >= 8)
		return -EINVAL;
	info->prefix[EBT_LOG_PREFIX_SIZE - 1] = '\0';
	return 0;
}

struct tcpudphdr
{
	__be16 src;
	__be16 dst;
};

struct arppayload
{
	unsigned char mac_src[ETH_ALEN];
	unsigned char ip_src[4];
	unsigned char mac_dst[ETH_ALEN];
	unsigned char ip_dst[4];
};

static void
print_ports(const struct sk_buff *skb, uint8_t protocol, int offset)
{
	if (protocol == IPPROTO_TCP ||
	    protocol == IPPROTO_UDP ||
	    protocol == IPPROTO_UDPLITE ||
	    protocol == IPPROTO_SCTP ||
	    protocol == IPPROTO_DCCP) {
		const struct tcpudphdr *pptr;
		struct tcpudphdr _ports;

		pptr = skb_header_pointer(skb, offset,
					  sizeof(_ports), &_ports);
		if (pptr == NULL) {
			printk(" INCOMPLETE TCP/UDP header");
			return;
		}
		printk(" SPT=%u DPT=%u", ntohs(pptr->src), ntohs(pptr->dst));
	}
}

static void
ebt_log_packet(struct net *net, u_int8_t pf, unsigned int hooknum,
	       const struct sk_buff *skb, const struct net_device *in,
	       const struct net_device *out, const struct nf_loginfo *loginfo,
	       const char *prefix)
{
	unsigned int bitmask;

	/* FIXME: Disabled from containers until syslog ns is supported */
	if (!net_eq(net, &init_net))
		return;

	spin_lock_bh(&ebt_log_lock);
	printk(KERN_SOH "%c%s IN=%s OUT=%s MAC source = %pM MAC dest = %pM proto = 0x%04x",
	       '0' + loginfo->u.log.level, prefix,
	       in ? in->name : "", out ? out->name : "",
	       eth_hdr(skb)->h_source, eth_hdr(skb)->h_dest,
	       ntohs(eth_hdr(skb)->h_proto));

	if (loginfo->type == NF_LOG_TYPE_LOG)
		bitmask = loginfo->u.log.logflags;
	else
		bitmask = NF_LOG_MASK;

	if ((bitmask & EBT_LOG_IP) && eth_hdr(skb)->h_proto ==
	   htons(ETH_P_IP)){
		const struct iphdr *ih;
		struct iphdr _iph;

		ih = skb_header_pointer(skb, 0, sizeof(_iph), &_iph);
		if (ih == NULL) {
			printk(" INCOMPLETE IP header");
			goto out;
		}
		printk(" IP SRC=%pI4 IP DST=%pI4, IP tos=0x%02X, IP proto=%d",
		       &ih->saddr, &ih->daddr, ih->tos, ih->protocol);
		print_ports(skb, ih->protocol, ih->ihl*4);
		goto out;
	}

#if IS_ENABLED(CONFIG_BRIDGE_EBT_IP6)
	if ((bitmask & EBT_LOG_IP6) && eth_hdr(skb)->h_proto ==
	   htons(ETH_P_IPV6)) {
		const struct ipv6hdr *ih;
		struct ipv6hdr _iph;
		uint8_t nexthdr;
		__be16 frag_off;
		int offset_ph;

		ih = skb_header_pointer(skb, 0, sizeof(_iph), &_iph);
		if (ih == NULL) {
			printk(" INCOMPLETE IPv6 header");
			goto out;
		}
		printk(" IPv6 SRC=%pI6 IPv6 DST=%pI6, IPv6 priority=0x%01X, Next Header=%d",
		       &ih->saddr, &ih->daddr, ih->priority, ih->nexthdr);
		nexthdr = ih->nexthdr;
		offset_ph = ipv6_skip_exthdr(skb, sizeof(_iph), &nexthdr, &frag_off);
		if (offset_ph == -1)
			goto out;
		print_ports(skb, nexthdr, offset_ph);
		goto out;
	}
#endif

	if ((bitmask & EBT_LOG_ARP) &&
	    ((eth_hdr(skb)->h_proto == htons(ETH_P_ARP)) ||
	     (eth_hdr(skb)->h_proto == htons(ETH_P_RARP)))) {
		const struct arphdr *ah;
		struct arphdr _arph;

		ah = skb_header_pointer(skb, 0, sizeof(_arph), &_arph);
		if (ah == NULL) {
			printk(" INCOMPLETE ARP header");
			goto out;
		}
		printk(" ARP HTYPE=%d, PTYPE=0x%04x, OPCODE=%d",
		       ntohs(ah->ar_hrd), ntohs(ah->ar_pro),
		       ntohs(ah->ar_op));

		/* If it's for Ethernet and the lengths are OK,
		 * then log the ARP payload */
		if (ah->ar_hrd == htons(1) &&
		    ah->ar_hln == ETH_ALEN &&
		    ah->ar_pln == sizeof(__be32)) {
			const struct arppayload *ap;
			struct arppayload _arpp;

			ap = skb_header_pointer(skb, sizeof(_arph),
						sizeof(_arpp), &_arpp);
			if (ap == NULL) {
				printk(" INCOMPLETE ARP payload");
				goto out;
			}
			printk(" ARP MAC SRC=%pM ARP IP SRC=%pI4 ARP MAC DST=%pM ARP IP DST=%pI4",
					ap->mac_src, ap->ip_src, ap->mac_dst, ap->ip_dst);
		}
	}
out:
	printk("\n");
	spin_unlock_bh(&ebt_log_lock);

}

static unsigned int
ebt_log_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct ebt_log_info *info = par->targinfo;
	struct nf_loginfo li;
	struct net *net = dev_net(par->in ? par->in : par->out);

	li.type = NF_LOG_TYPE_LOG;
	li.u.log.level = info->loglevel;
	li.u.log.logflags = info->bitmask;

	if (info->bitmask & EBT_LOG_NFLOG)
		nf_log_packet(net, NFPROTO_BRIDGE, par->hooknum, skb,
			      par->in, par->out, &li, "%s", info->prefix);
	else
		ebt_log_packet(net, NFPROTO_BRIDGE, par->hooknum, skb, par->in,
			       par->out, &li, info->prefix);
	return EBT_CONTINUE;
}

static struct xt_target ebt_log_tg_reg __read_mostly = {
	.name		= "log",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.target		= ebt_log_tg,
	.checkentry	= ebt_log_tg_check,
	.targetsize	= sizeof(struct ebt_log_info),
	.me		= THIS_MODULE,
};

static struct nf_logger ebt_log_logger __read_mostly = {
	.name 		= "ebt_log",
	.logfn		= &ebt_log_packet,
	.me		= THIS_MODULE,
};

static int __net_init ebt_log_net_init(struct net *net)
{
	nf_log_set(net, NFPROTO_BRIDGE, &ebt_log_logger);
	return 0;
}

static void __net_exit ebt_log_net_fini(struct net *net)
{
	nf_log_unset(net, &ebt_log_logger);
}

static struct pernet_operations ebt_log_net_ops = {
	.init = ebt_log_net_init,
	.exit = ebt_log_net_fini,
};

static int __init ebt_log_init(void)
{
	int ret;

	ret = register_pernet_subsys(&ebt_log_net_ops);
	if (ret < 0)
		goto err_pernet;

	ret = xt_register_target(&ebt_log_tg_reg);
	if (ret < 0)
		goto err_target;

	nf_log_register(NFPROTO_BRIDGE, &ebt_log_logger);

	return ret;

err_target:
	unregister_pernet_subsys(&ebt_log_net_ops);
err_pernet:
	return ret;
}

static void __exit ebt_log_fini(void)
{
	unregister_pernet_subsys(&ebt_log_net_ops);
	nf_log_unregister(&ebt_log_logger);
	xt_unregister_target(&ebt_log_tg_reg);
}

module_init(ebt_log_init);
module_exit(ebt_log_fini);
MODULE_DESCRIPTION("Ebtables: Packet logging to syslog");
MODULE_LICENSE("GPL");
