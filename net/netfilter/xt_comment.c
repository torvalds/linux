/*
 * Implements a dummy match to allow attaching comments to rules
 *
 * 2003-05-13 Brad Fisher (brad@info-link.net)
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_comment.h>

MODULE_AUTHOR("Brad Fisher <brad@info-link.net>");
MODULE_DESCRIPTION("Xtables: No-op match which can be tagged with a comment");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_comment");
MODULE_ALIAS("ip6t_comment");

static bool
comment_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	/* We always match */
	return true;
}

static struct xt_match comment_mt_reg[] __read_mostly = {
	{
		.name		= "comment",
		.family		= NFPROTO_IPV4,
		.match		= comment_mt,
		.matchsize	= sizeof(struct xt_comment_info),
		.me		= THIS_MODULE
	},
	{
		.name		= "comment",
		.family		= NFPROTO_IPV6,
		.match		= comment_mt,
		.matchsize	= sizeof(struct xt_comment_info),
		.me		= THIS_MODULE
	},
};

static int __init comment_mt_init(void)
{
	return xt_register_matches(comment_mt_reg, ARRAY_SIZE(comment_mt_reg));
}

static void __exit comment_mt_exit(void)
{
	xt_unregister_matches(comment_mt_reg, ARRAY_SIZE(comment_mt_reg));
}

module_init(comment_mt_init);
module_exit(comment_mt_exit);
