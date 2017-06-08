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

struct tcpudphdr {
	__be16 src;
	__be16 dst;
};

static bool
ebt_ip_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct ebt_ip_info *info = par->matchinfo;
	const struct iphdr *ih;
	struct iphdr _iph;
	const struct tcpudphdr *pptr;
	struct tcpudphdr _ports;

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
		if (!(info->bitmask & EBT_IP_DPORT) &&
		    !(info->bitmask & EBT_IP_SPORT))
			return true;
		if (ntohs(ih->frag_off) & IP_OFFSET)
			return false;
		pptr = skb_header_pointer(skb, ih->ihl*4,
					  sizeof(_ports), &_ports);
		if (pptr == NULL)
			return false;
		if (info->bitmask & EBT_IP_DPORT) {
			u32 dst = ntohs(pptr->dst);
			if (NF_INVF(info, EBT_IP_DPORT,
				    dst < info->dport[0] ||
				    dst > info->dport[1]))
			return false;
		}
		if (info->bitmask & EBT_IP_SPORT) {
			u32 src = ntohs(pptr->src);
			if (NF_INVF(info, EBT_IP_SPORT,
				    src < info->sport[0] ||
				    src > info->sport[1]))
			return false;
		}
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
