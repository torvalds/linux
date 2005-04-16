/*
 * Implements a dummy match to allow attaching comments to rules
 *
 * 2003-05-13 Brad Fisher (brad@info-link.net)
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_comment.h>

MODULE_AUTHOR("Brad Fisher <brad@info-link.net>");
MODULE_DESCRIPTION("iptables comment match module");
MODULE_LICENSE("GPL");

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      int *hotdrop)
{
	/* We always match */
	return 1;
}

static int
checkentry(const char *tablename,
           const struct ipt_ip *ip,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	/* Check the size */
	if (matchsize != IPT_ALIGN(sizeof(struct ipt_comment_info)))
		return 0;
	return 1;
}

static struct ipt_match comment_match = {
	.name		= "comment",
	.match		= match,
	.checkentry	= checkentry,
	.me		= THIS_MODULE
};

static int __init init(void)
{
	return ipt_register_match(&comment_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&comment_match);
}

module_init(init);
module_exit(fini);
