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

#include <linux/in.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_log.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/spinlock.h>

static DEFINE_SPINLOCK(ebt_log_lock);

static int ebt_log_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_log_info *info = (struct ebt_log_info *)data;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_log_info)))
		return -EINVAL;
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

static void print_MAC(unsigned char *p)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++, p++)
		printk("%02x%c", *p, i == ETH_ALEN - 1 ? ' ':':');
}

#define myNIPQUAD(a) a[0], a[1], a[2], a[3]
static void
ebt_log_packet(unsigned int pf, unsigned int hooknum,
   const struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, const struct nf_loginfo *loginfo,
   const char *prefix)
{
	unsigned int bitmask;

	spin_lock_bh(&ebt_log_lock);
	printk("<%c>%s IN=%s OUT=%s MAC source = ", '0' + loginfo->u.log.level,
	       prefix, in ? in->name : "", out ? out->name : "");

	print_MAC(eth_hdr(skb)->h_source);
	printk("MAC dest = ");
	print_MAC(eth_hdr(skb)->h_dest);

	printk("proto = 0x%04x", ntohs(eth_hdr(skb)->h_proto));

	if (loginfo->type == NF_LOG_TYPE_LOG)
		bitmask = loginfo->u.log.logflags;
	else
		bitmask = NF_LOG_MASK;

	if ((bitmask & EBT_LOG_IP) && eth_hdr(skb)->h_proto ==
	   htons(ETH_P_IP)){
		struct iphdr _iph, *ih;

		ih = skb_header_pointer(skb, 0, sizeof(_iph), &_iph);
		if (ih == NULL) {
			printk(" INCOMPLETE IP header");
			goto out;
		}
		printk(" IP SRC=%u.%u.%u.%u IP DST=%u.%u.%u.%u, IP "
		       "tos=0x%02X, IP proto=%d", NIPQUAD(ih->saddr),
		       NIPQUAD(ih->daddr), ih->tos, ih->protocol);
		if (ih->protocol == IPPROTO_TCP ||
		    ih->protocol == IPPROTO_UDP ||
		    ih->protocol == IPPROTO_UDPLITE ||
		    ih->protocol == IPPROTO_SCTP ||
		    ih->protocol == IPPROTO_DCCP) {
			struct tcpudphdr _ports, *pptr;

			pptr = skb_header_pointer(skb, ih->ihl*4,
						  sizeof(_ports), &_ports);
			if (pptr == NULL) {
				printk(" INCOMPLETE TCP/UDP header");
				goto out;
			}
			printk(" SPT=%u DPT=%u", ntohs(pptr->src),
			   ntohs(pptr->dst));
		}
		goto out;
	}

	if ((bitmask & EBT_LOG_ARP) &&
	    ((eth_hdr(skb)->h_proto == htons(ETH_P_ARP)) ||
	     (eth_hdr(skb)->h_proto == htons(ETH_P_RARP)))) {
		struct arphdr _arph, *ah;

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
			struct arppayload _arpp, *ap;

			ap = skb_header_pointer(skb, sizeof(_arph),
						sizeof(_arpp), &_arpp);
			if (ap == NULL) {
				printk(" INCOMPLETE ARP payload");
				goto out;
			}
			printk(" ARP MAC SRC=");
			print_MAC(ap->mac_src);
			printk(" ARP IP SRC=%u.%u.%u.%u",
			       myNIPQUAD(ap->ip_src));
			printk(" ARP MAC DST=");
			print_MAC(ap->mac_dst);
			printk(" ARP IP DST=%u.%u.%u.%u",
			       myNIPQUAD(ap->ip_dst));
		}
	}
out:
	printk("\n");
	spin_unlock_bh(&ebt_log_lock);

}

static void ebt_log(const struct sk_buff *skb, unsigned int hooknr,
   const struct net_device *in, const struct net_device *out,
   const void *data, unsigned int datalen)
{
	struct ebt_log_info *info = (struct ebt_log_info *)data;
	struct nf_loginfo li;

	li.type = NF_LOG_TYPE_LOG;
	li.u.log.level = info->loglevel;
	li.u.log.logflags = info->bitmask;

	if (info->bitmask & EBT_LOG_NFLOG)
		nf_log_packet(PF_BRIDGE, hooknr, skb, in, out, &li,
			      "%s", info->prefix);
	else
		ebt_log_packet(PF_BRIDGE, hooknr, skb, in, out, &li,
			       info->prefix);
}

static struct ebt_watcher log =
{
	.name		= EBT_LOG_WATCHER,
	.watcher	= ebt_log,
	.check		= ebt_log_check,
	.me		= THIS_MODULE,
};

static struct nf_logger ebt_log_logger = {
	.name 		= "ebt_log",
	.logfn		= &ebt_log_packet,
	.me		= THIS_MODULE,
};

static int __init ebt_log_init(void)
{
	int ret;

	ret = ebt_register_watcher(&log);
	if (ret < 0)
		return ret;
	ret = nf_log_register(PF_BRIDGE, &ebt_log_logger);
	if (ret < 0 && ret != -EEXIST)
		ebt_unregister_watcher(&log);
	return ret;
}

static void __exit ebt_log_fini(void)
{
	nf_log_unregister(&ebt_log_logger);
	ebt_unregister_watcher(&log);
}

module_init(ebt_log_init);
module_exit(ebt_log_fini);
MODULE_LICENSE("GPL");
