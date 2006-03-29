/* String matching match for iptables
 * 
 * (C) 2005 Pablo Neira Ayuso <pablo@eurodev.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_string.h>
#include <linux/textsearch.h>

MODULE_AUTHOR("Pablo Neira Ayuso <pablo@eurodev.net>");
MODULE_DESCRIPTION("IP tables string match module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_string");
MODULE_ALIAS("ip6t_string");

static int match(const struct sk_buff *skb,
		 const struct net_device *in,
		 const struct net_device *out,
		 const struct xt_match *match,
		 const void *matchinfo,
		 int offset,
		 unsigned int protoff,
		 int *hotdrop)
{
	struct ts_state state;
	struct xt_string_info *conf = (struct xt_string_info *) matchinfo;

	memset(&state, 0, sizeof(struct ts_state));

	return (skb_find_text((struct sk_buff *)skb, conf->from_offset, 
			     conf->to_offset, conf->config, &state) 
			     != UINT_MAX) && !conf->invert;
}

#define STRING_TEXT_PRIV(m) ((struct xt_string_info *) m)

static int checkentry(const char *tablename,
		      const void *ip,
		      const struct xt_match *match,
		      void *matchinfo,
		      unsigned int matchsize,
		      unsigned int hook_mask)
{
	struct xt_string_info *conf = matchinfo;
	struct ts_config *ts_conf;

	/* Damn, can't handle this case properly with iptables... */
	if (conf->from_offset > conf->to_offset)
		return 0;

	ts_conf = textsearch_prepare(conf->algo, conf->pattern, conf->patlen,
				     GFP_KERNEL, TS_AUTOLOAD);
	if (IS_ERR(ts_conf))
		return 0;

	conf->config = ts_conf;

	return 1;
}

static void destroy(const struct xt_match *match, void *matchinfo,
		    unsigned int matchsize)
{
	textsearch_destroy(STRING_TEXT_PRIV(matchinfo)->config);
}

static struct xt_match string_match = {
	.name 		= "string",
	.match 		= match,
	.matchsize	= sizeof(struct xt_string_info),
	.checkentry	= checkentry,
	.destroy 	= destroy,
	.family		= AF_INET,
	.me 		= THIS_MODULE
};
static struct xt_match string6_match = {
	.name 		= "string",
	.match 		= match,
	.matchsize	= sizeof(struct xt_string_info),
	.checkentry	= checkentry,
	.destroy 	= destroy,
	.family		= AF_INET6,
	.me 		= THIS_MODULE
};

static int __init xt_string_init(void)
{
	int ret;

	ret = xt_register_match(&string_match);
	if (ret)
		return ret;
	ret = xt_register_match(&string6_match);
	if (ret)
		xt_unregister_match(&string_match);

	return ret;
}

static void __exit xt_string_fini(void)
{
	xt_unregister_match(&string_match);
	xt_unregister_match(&string6_match);
}

module_init(xt_string_init);
module_exit(xt_string_fini);
