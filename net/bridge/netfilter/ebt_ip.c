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

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_ip.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/in.h>
#include <linux/module.h>

struct tcpudphdr {
	uint16_t src;
	uint16_t dst;
};

static int ebt_filter_ip(const struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, const void *data,
   unsigned int datalen)
{
	struct ebt_ip_info *info = (struct ebt_ip_info *)data;
	struct iphdr _iph, *ih;
	struct tcpudphdr _ports, *pptr;

	ih = skb_header_pointer(skb, 0, sizeof(_iph), &_iph);
	if (ih == NULL)
		return EBT_NOMATCH;
	if (info->bitmask & EBT_IP_TOS &&
	   FWINV(info->tos != ih->tos, EBT_IP_TOS))
		return EBT_NOMATCH;
	if (info->bitmask & EBT_IP_SOURCE &&
	   FWINV((ih->saddr & info->smsk) !=
	   info->saddr, EBT_IP_SOURCE))
		return EBT_NOMATCH;
	if ((info->bitmask & EBT_IP_DEST) &&
	   FWINV((ih->daddr & info->dmsk) !=
	   info->daddr, EBT_IP_DEST))
		return EBT_NOMATCH;
	if (info->bitmask & EBT_IP_PROTO) {
		if (FWINV(info->protocol != ih->protocol, EBT_IP_PROTO))
			return EBT_NOMATCH;
		if (!(info->bitmask & EBT_IP_DPORT) &&
		    !(info->bitmask & EBT_IP_SPORT))
			return EBT_MATCH;
		if (ntohs(ih->frag_off) & IP_OFFSET)
			return EBT_NOMATCH;
		pptr = skb_header_pointer(skb, ih->ihl*4,
					  sizeof(_ports), &_ports);
		if (pptr == NULL)
			return EBT_NOMATCH;
		if (info->bitmask & EBT_IP_DPORT) {
			u32 dst = ntohs(pptr->dst);
			if (FWINV(dst < info->dport[0] ||
			          dst > info->dport[1],
			          EBT_IP_DPORT))
			return EBT_NOMATCH;
		}
		if (info->bitmask & EBT_IP_SPORT) {
			u32 src = ntohs(pptr->src);
			if (FWINV(src < info->sport[0] ||
			          src > info->sport[1],
			          EBT_IP_SPORT))
			return EBT_NOMATCH;
		}
	}
	return EBT_MATCH;
}

static int ebt_ip_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_ip_info *info = (struct ebt_ip_info *)data;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_ip_info)))
		return -EINVAL;
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

static struct ebt_match filter_ip =
{
	.name		= EBT_IP_MATCH,
	.match		= ebt_filter_ip,
	.check		= ebt_ip_check,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ebt_register_match(&filter_ip);
}

static void __exit fini(void)
{
	ebt_unregister_match(&filter_ip);
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
