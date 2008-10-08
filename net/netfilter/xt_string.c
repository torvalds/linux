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
	int invert;

	memset(&state, 0, sizeof(struct ts_state));

	invert = (match->revision == 0 ? conf->u.v0.invert :
				    conf->u.v1.flags & XT_STRING_FLAG_INVERT);

	return (skb_find_text((struct sk_buff *)skb, conf->from_offset,
			     conf->to_offset, conf->config, &state)
			     != UINT_MAX) ^ invert;
}

#define STRING_TEXT_PRIV(m) ((struct xt_string_info *)(m))

static bool
string_mt_check(const char *tablename, const void *ip,
                const struct xt_match *match, void *matchinfo,
                unsigned int hook_mask)
{
	struct xt_string_info *conf = matchinfo;
	struct ts_config *ts_conf;
	int flags = TS_AUTOLOAD;

	/* Damn, can't handle this case properly with iptables... */
	if (conf->from_offset > conf->to_offset)
		return false;
	if (conf->algo[XT_STRING_MAX_ALGO_NAME_SIZE - 1] != '\0')
		return false;
	if (conf->patlen > XT_STRING_MAX_PATTERN_SIZE)
		return false;
	if (match->revision == 1) {
		if (conf->u.v1.flags &
		    ~(XT_STRING_FLAG_IGNORECASE | XT_STRING_FLAG_INVERT))
			return false;
		if (conf->u.v1.flags & XT_STRING_FLAG_IGNORECASE)
			flags |= TS_IGNORECASE;
	}
	ts_conf = textsearch_prepare(conf->algo, conf->pattern, conf->patlen,
				     GFP_KERNEL, flags);
	if (IS_ERR(ts_conf))
		return false;

	conf->config = ts_conf;

	return true;
}

static void string_mt_destroy(const struct xt_match *match, void *matchinfo)
{
	textsearch_destroy(STRING_TEXT_PRIV(matchinfo)->config);
}

static struct xt_match xt_string_mt_reg[] __read_mostly = {
	{
		.name 		= "string",
		.revision	= 0,
		.family		= NFPROTO_UNSPEC,
		.checkentry	= string_mt_check,
		.match 		= string_mt,
		.destroy 	= string_mt_destroy,
		.matchsize	= sizeof(struct xt_string_info),
		.me 		= THIS_MODULE
	},
	{
		.name 		= "string",
		.revision	= 1,
		.family		= NFPROTO_UNSPEC,
		.checkentry	= string_mt_check,
		.match 		= string_mt,
		.destroy 	= string_mt_destroy,
		.matchsize	= sizeof(struct xt_string_info),
		.me 		= THIS_MODULE
	},
};

static int __init string_mt_init(void)
{
	return xt_register_matches(xt_string_mt_reg,
				   ARRAY_SIZE(xt_string_mt_reg));
}

static void __exit string_mt_exit(void)
{
	xt_unregister_matches(xt_string_mt_reg, ARRAY_SIZE(xt_string_mt_reg));
}

module_init(string_mt_init);
module_exit(string_mt_exit);
