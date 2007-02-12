/* This is a module which is used for setting up fake conntracks
 * on packets so that they are not seen by the conntrack/NAT code.
 */
#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_conntrack_compat.h>

MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_NOTRACK");

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const struct xt_target *target,
       const void *targinfo)
{
	/* Previously seen (loopback)? Ignore. */
	if ((*pskb)->nfct != NULL)
		return XT_CONTINUE;

	/* Attach fake conntrack entry.
	   If there is a real ct entry correspondig to this packet,
	   it'll hang aroun till timing out. We don't deal with it
	   for performance reasons. JK */
	nf_ct_untrack(*pskb);
	(*pskb)->nfctinfo = IP_CT_NEW;
	nf_conntrack_get((*pskb)->nfct);

	return XT_CONTINUE;
}

static struct xt_target xt_notrack_target[] = {
	{
		.name		= "NOTRACK",
		.family		= AF_INET,
		.target		= target,
		.table		= "raw",
		.me		= THIS_MODULE,
	},
	{
		.name		= "NOTRACK",
		.family		= AF_INET6,
		.target		= target,
		.table		= "raw",
		.me		= THIS_MODULE,
	},
};

static int __init xt_notrack_init(void)
{
	return xt_register_targets(xt_notrack_target,
				   ARRAY_SIZE(xt_notrack_target));
}

static void __exit xt_notrack_fini(void)
{
	xt_unregister_targets(xt_notrack_target, ARRAY_SIZE(xt_notrack_target));
}

module_init(xt_notrack_init);
module_exit(xt_notrack_fini);
