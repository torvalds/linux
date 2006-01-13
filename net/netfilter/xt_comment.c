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
MODULE_DESCRIPTION("iptables comment match module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_comment");
MODULE_ALIAS("ip6t_comment");

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protooff,
      int *hotdrop)
{
	/* We always match */
	return 1;
}

static int
checkentry(const char *tablename,
           const void *ip,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	/* Check the size */
	if (matchsize != XT_ALIGN(sizeof(struct xt_comment_info)))
		return 0;
	return 1;
}

static struct xt_match comment_match = {
	.name		= "comment",
	.match		= match,
	.checkentry	= checkentry,
	.me		= THIS_MODULE
};

static struct xt_match comment6_match = {
	.name		= "comment",
	.match		= match,
	.checkentry	= checkentry,
	.me		= THIS_MODULE
};

static int __init init(void)
{
	int ret;

	ret = xt_register_match(AF_INET, &comment_match);
	if (ret)
		return ret;

	ret = xt_register_match(AF_INET6, &comment6_match);
	if (ret)
		xt_unregister_match(AF_INET, &comment_match);

	return ret;
}

static void __exit fini(void)
{
	xt_unregister_match(AF_INET, &comment_match);
	xt_unregister_match(AF_INET6, &comment6_match);
}

module_init(init);
module_exit(fini);
