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
MODULE_DESCRIPTION("Xtables: string-based matching");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_string");
MODULE_ALIAS("ip6t_string");

static bool
string_mt(const struct sk_buff *skb, const struct net_device *in,
          const struct net_device *out, const struct xt_match *match,
          const void *matchinfo, int offset, unsigned int protoff,
          bool *hotdrop)
{
	const struct xt_string_info *conf = matchinfo;
	struct ts_state state;

	memset(&state, 0, sizeof(struct ts_state));

	return (skb_find_text((struct sk_buff *)skb, conf->from_offset,
			     conf->to_offset, conf->config, &state)
			     != UINT_MAX) ^ conf->invert;
}

#define STRING_TEXT_PRIV(m) ((struct xt_string_info *)(m))

static bool
string_mt_check(const char *tablename, const void *ip,
                const struct xt_match *match, void *matchinfo,
                unsigned int hook_mask)
{
	struct xt_string_info *conf = matchinfo;
	struct ts_config *ts_conf;

	/* Damn, can't handle this case properly with iptables... */
	if (conf->from_offset > conf->to_offset)
		return false;
	if (conf->algo[XT_STRING_MAX_ALGO_NAME_SIZE - 1] != '\0')
		return false;
	if (conf->patlen > XT_STRING_MAX_PATTERN_SIZE)
		return false;
	ts_conf = textsearch_prepare(conf->algo, conf->pattern, conf->patlen,
				     GFP_KERNEL, TS_AUTOLOAD);
	if (IS_ERR(ts_conf))
		return false;

	conf->config = ts_conf;

	return true;
}

static void string_mt_destroy(const struct xt_match *match, void *matchinfo)
{
	textsearch_destroy(STRING_TEXT_PRIV(matchinfo)->config);
}

static struct xt_match string_mt_reg[] __read_mostly = {
	{
		.name 		= "string",
		.family		= AF_INET,
		.checkentry	= string_mt_check,
		.match 		= string_mt,
		.destroy 	= string_mt_destroy,
		.matchsize	= sizeof(struct xt_string_info),
		.me 		= THIS_MODULE
	},
	{
		.name 		= "string",
		.family		= AF_INET6,
		.checkentry	= string_mt_check,
		.match 		= string_mt,
		.destroy 	= string_mt_destroy,
		.matchsize	= sizeof(struct xt_string_info),
		.me 		= THIS_MODULE
	},
};

static int __init string_mt_init(void)
{
	return xt_register_matches(string_mt_reg, ARRAY_SIZE(string_mt_reg));
}

static void __exit string_mt_exit(void)
{
	xt_unregister_matches(string_mt_reg, ARRAY_SIZE(string_mt_reg));
}

module_init(string_mt_init);
module_exit(string_mt_exit);
