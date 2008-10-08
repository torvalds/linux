/* This is a module which is used to mark packets for tracing.
 */
#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter/x_tables.h>

MODULE_DESCRIPTION("Xtables: packet flow tracing");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_TRACE");
MODULE_ALIAS("ip6t_TRACE");

static unsigned int
trace_tg(struct sk_buff *skb, const struct net_device *in,
         const struct net_device *out, unsigned int hooknum,
         const struct xt_target *target, const void *targinfo)
{
	skb->nf_trace = 1;
	return XT_CONTINUE;
}

static struct xt_target trace_tg_reg[] __read_mostly = {
	{
		.name		= "TRACE",
		.family		= NFPROTO_IPV4,
		.target		= trace_tg,
		.table		= "raw",
		.me		= THIS_MODULE,
	},
	{
		.name		= "TRACE",
		.family		= NFPROTO_IPV6,
		.target		= trace_tg,
		.table		= "raw",
		.me		= THIS_MODULE,
	},
};

static int __init trace_tg_init(void)
{
	return xt_register_targets(trace_tg_reg, ARRAY_SIZE(trace_tg_reg));
}

static void __exit trace_tg_exit(void)
{
	xt_unregister_targets(trace_tg_reg, ARRAY_SIZE(trace_tg_reg));
}

module_init(trace_tg_init);
module_exit(trace_tg_exit);
