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
       const void *targinfo,
       void *userinfo)
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

static struct xt_target notrack_reg = {
	.name		= "NOTRACK",
	.target		= target,
	.targetsize	= 0,
	.table		= "raw",
	.family		= AF_INET,
	.me		= THIS_MODULE,
};

static struct xt_target notrack6_reg = {
	.name		= "NOTRACK",
	.target		= target,
	.targetsize	= 0,
	.table		= "raw",
	.family		= AF_INET6,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	int ret;

	ret = xt_register_target(&notrack_reg);
	if (ret)
		return ret;

	ret = xt_register_target(&notrack6_reg);
	if (ret)
		xt_unregister_target(&notrack_reg);

	return ret;
}

static void __exit fini(void)
{
	xt_unregister_target(&notrack6_reg);
	xt_unregister_target(&notrack_reg);
}

module_init(init);
module_exit(fini);
