/* This is a module which is used for setting up fake conntracks
 * on packets so that they are not seen by the conntrack/NAT code.
 */
#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_conntrack.h>

MODULE_DESCRIPTION("Xtables: Disabling connection tracking for packets");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_NOTRACK");
MODULE_ALIAS("ip6t_NOTRACK");

static unsigned int
notrack_tg(struct sk_buff *skb, const struct net_device *in,
           const struct net_device *out, unsigned int hooknum,
           const struct xt_target *target, const void *targinfo)
{
	/* Previously seen (loopback)? Ignore. */
	if (skb->nfct != NULL)
		return XT_CONTINUE;

	/* Attach fake conntrack entry.
	   If there is a real ct entry correspondig to this packet,
	   it'll hang aroun till timing out. We don't deal with it
	   for performance reasons. JK */
	skb->nfct = &nf_conntrack_untracked.ct_general;
	skb->nfctinfo = IP_CT_NEW;
	nf_conntrack_get(skb->nfct);

	return XT_CONTINUE;
}

static struct xt_target notrack_tg_reg[] __read_mostly = {
	{
		.name		= "NOTRACK",
		.family		= NFPROTO_IPV4,
		.target		= notrack_tg,
		.table		= "raw",
		.me		= THIS_MODULE,
	},
	{
		.name		= "NOTRACK",
		.family		= NFPROTO_IPV6,
		.target		= notrack_tg,
		.table		= "raw",
		.me		= THIS_MODULE,
	},
};

static int __init notrack_tg_init(void)
{
	return xt_register_targets(notrack_tg_reg, ARRAY_SIZE(notrack_tg_reg));
}

static void __exit notrack_tg_exit(void)
{
	xt_unregister_targets(notrack_tg_reg, ARRAY_SIZE(notrack_tg_reg));
}

module_init(notrack_tg_init);
module_exit(notrack_tg_exit);
