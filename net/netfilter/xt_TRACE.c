/* This is a module which is used to mark packets for tracing.
 */
#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_TRACE");
MODULE_ALIAS("ip6t_TRACE");

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const struct xt_target *target,
       const void *targinfo)
{
	(*pskb)->nf_trace = 1;
	return XT_CONTINUE;
}

static struct xt_target xt_trace_target[] __read_mostly = {
	{
		.name		= "TRACE",
		.family		= AF_INET,
		.target		= target,
		.table		= "raw",
		.me		= THIS_MODULE,
	},
	{
		.name		= "TRACE",
		.family		= AF_INET6,
		.target		= target,
		.table		= "raw",
		.me		= THIS_MODULE,
	},
};

static int __init xt_trace_init(void)
{
	return xt_register_targets(xt_trace_target,
				   ARRAY_SIZE(xt_trace_target));
}

static void __exit xt_trace_fini(void)
{
	xt_unregister_targets(xt_trace_target, ARRAY_SIZE(xt_trace_target));
}

module_init(xt_trace_init);
module_exit(xt_trace_fini);
