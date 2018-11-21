/*
 *  ebt_ip
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  April, 2002
 *
 *  Changes:
 *    added ip-sport and ip-dport
 *    Innominate Security Technologies AG <mhopf@innominate.com>
 *    September, 2002
 */
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/in.h>
#include <linux/module.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_ip.h>

union pkthdr {
	struct {
		__be16 src;
		__be16 dst;
	} tcpudphdr;
	struct {
		u8 type;
		u8 code;
	} icmphdr;
	struct {
		u8 type;
	} igmphdr;
};

static bool
ebt_ip_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct ebt_ip_info *info = par->matchinfo;
	const struct iphdr *ih;
	struct iphdr _iph;
	const union pkthdr *pptr;
	union pkthdr _pkthdr;

	ih = skb_header_pointer(skb, 0, sizeof(_iph), &_iph);
	if (ih == NULL)
		return false;
	if ((info->bitmask & EBT_IP_TOS) &&
	    NF_INVF(info, EBT_IP_TOS, info->tos != ih->tos))
		return false;
	if ((info->bitmask & EBT_IP_SOURCE) &&
	    NF_INVF(info, EBT_IP_SOURCE,
		    (ih->saddr & info->smsk) != info->saddr))
		return false;
	if ((info->bitmask & EBT_IP_DEST) &&
	    NF_INVF(info, EBT_IP_DEST,
		    (ih->daddr & info->dmsk) != info->daddr))
		return false;
	if (info->bitmask & EBT_IP_PROTO) {
		if (NF_INVF(info, EBT_IP_PROTO, info->protocol != ih->protocol))
			return false;
		if (!(info->bitmask & (EBT_IP_DPORT | EBT_IP_SPORT |
				       EBT_IP_ICMP | EBT_IP_IGMP)))
			return true;
		if (ntohs(ih->frag_off) & IP_OFFSET)
			return false;

		/* min icmp/igmp headersize is 4, so sizeof(_pkthdr) is ok. */
		pptr = skb_header_pointer(skb, ih->ihl*4,
					  sizeof(_pkthdr), &_pkthdr);
		if (pptr == NULL)
			return false;
		if (info->bitmask & EBT_IP_DPORT) {
			u32 dst = ntohs(pptr->tcpudphdr.dst);
			if (NF_INVF(info, EBT_IP_DPORT,
				    dst < info->dport[0] ||
				    dst > info->dport[1]))
				return false;
		}
		if (info->bitmask & EBT_IP_SPORT) {
			u32 src = ntohs(pptr->tcpudphdr.src);
			if (NF_INVF(info, EBT_IP_SPORT,
				    src < info->sport[0] ||
				    src > info->sport[1]))
				return false;
		}
		if ((info->bitmask & EBT_IP_ICMP) &&
		    NF_INVF(info, EBT_IP_ICMP,
			    pptr->icmphdr.type < info->icmp_type[0] ||
			    pptr->icmphdr.type > info->icmp_type[1] ||
			    pptr->icmphdr.code < info->icmp_code[0] ||
			    pptr->icmphdr.code > info->icmp_code[1]))
			return false;
		if ((info->bitmask & EBT_IP_IGMP) &&
		    NF_INVF(info, EBT_IP_IGMP,
			    pptr->igmphdr.type < info->igmp_type[0] ||
			    pptr->igmphdr.type > info->igmp_type[1]))
			return false;
	}
	return true;
}

static int ebt_ip_mt_check(const struct xt_mtchk_param *par)
{
	const struct ebt_ip_info *info = par->matchinfo;
	const struct ebt_entry *e = par->entryinfo;

	if (e->ethproto != htons(ETH_P_IP) ||
	   e->invflags & EBT_IPROTO)
		return -EINVAL;
	if (info->bitmask & ~EBT_IP_MASK || info->invflags & ~EBT_IP_MASK)
		return -EINVAL;
	if (info->bitmask & (EBT_IP_DPORT | EBT_IP_SPORT)) {
		if (info->invflags & EBT_IP_PROTO)
			return -EINVAL;
		if (info->protocol != IPPROTO_TCP &&
		    info->protocol != IPPROTO_UDP &&
		    info->protocol != IPPROTO_UDPLITE &&
		    info->protocol != IPPROTO_SCTP &&
		    info->protocol != IPPROTO_DCCP)
			 return -EINVAL;
	}
	if (info->bitmask & EBT_IP_DPORT && info->dport[0] > info->dport[1])
		return -EINVAL;
	if (info->bitmask & EBT_IP_SPORT && info->sport[0] > info->sport[1])
		return -EINVAL;
	if (info->bitmask & EBT_IP_ICMP) {
		if ((info->invflags & EBT_IP_PROTO) ||
		    info->protocol != IPPROTO_ICMP)
			return -EINVAL;
		if (info->icmp_type[0] > info->icmp_type[1] ||
		    info->icmp_code[0] > info->icmp_code[1])
			return -EINVAL;
	}
	if (info->bitmask & EBT_IP_IGMP) {
		if ((info->invflags & EBT_IP_PROTO) ||
		    info->protocol != IPPROTO_IGMP)
			return -EINVAL;
		if (info->igmp_type[0] > info->igmp_type[1])
			return -EINVAL;
	}
	return 0;
}

static struct xt_match ebt_ip_mt_reg __read_mostly = {
	.name		= "ip",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.match		= ebt_ip_mt,
	.checkentry	= ebt_ip_mt_check,
	.matchsize	= sizeof(struct ebt_ip_info),
	.me		= THIS_MODULE,
};

static int __init ebt_ip_init(void)
{
	return xt_register_match(&ebt_ip_mt_reg);
}

static void __exit ebt_ip_fini(void)
{
	xt_unregister_match(&ebt_ip_mt_reg);
}

module_init(ebt_ip_init);
module_exit(ebt_ip_fini);
MODULE_DESCRIPTION("Ebtables: IPv4 protocol packet match");
MODULE_LICENSE("GPL");
