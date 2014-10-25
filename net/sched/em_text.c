/*
 * net/sched/em_text.c	Textsearch ematch
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Thomas Graf <tgraf@suug.ch>
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/textsearch.h>
#include <linux/tc_ematch/tc_em_text.h>
#include <net/pkt_cls.h>

struct text_match {
	u16			from_offset;
	u16			to_offset;
	u8			from_layer;
	u8			to_layer;
	struct ts_config	*config;
};

#define EM_TEXT_PRIV(m) ((struct text_match *) (m)->data)

static int em_text_match(struct sk_buff *skb, struct tcf_ematch *m,
			 struct tcf_pkt_info *info)
{
	struct text_match *tm = EM_TEXT_PRIV(m);
	int from, to;
	struct ts_state state;

	from = tcf_get_base_ptr(skb, tm->from_layer) - skb->data;
	from += tm->from_offset;

	to = tcf_get_base_ptr(skb, tm->to_layer) - skb->data;
	to += tm->to_offset;

	return skb_find_text(skb, from, to, tm->config, &state) != UINT_MAX;
}

static int em_text_change(struct net *net, void *data, int len,
			  struct tcf_ematch *m)
{
	struct text_match *tm;
	struct tcf_em_text *conf = data;
	struct ts_config *ts_conf;
	int flags = 0;

	if (len < sizeof(*conf) || len < (sizeof(*conf) + conf->pattern_len))
		return -EINVAL;

	if (conf->from_layer > conf->to_layer)
		return -EINVAL;

	if (conf->from_layer == conf->to_layer &&
	    conf->from_offset > conf->to_offset)
		return -EINVAL;

retry:
	ts_conf = textsearch_prepare(conf->algo, (u8 *) conf + sizeof(*conf),
				     conf->pattern_len, GFP_KERNEL, flags);

	if (flags & TS_AUTOLOAD)
		rtnl_lock();

	if (IS_ERR(ts_conf)) {
		if (PTR_ERR(ts_conf) == -ENOENT && !(flags & TS_AUTOLOAD)) {
			rtnl_unlock();
			flags |= TS_AUTOLOAD;
			goto retry;
		} else
			return PTR_ERR(ts_conf);
	} else if (flags & TS_AUTOLOAD) {
		textsearch_destroy(ts_conf);
		return -EAGAIN;
	}

	tm = kmalloc(sizeof(*tm), GFP_KERNEL);
	if (tm == NULL) {
		textsearch_destroy(ts_conf);
		return -ENOBUFS;
	}

	tm->from_offset = conf->from_offset;
	tm->to_offset   = conf->to_offset;
	tm->from_layer  = conf->from_layer;
	tm->to_layer    = conf->to_layer;
	tm->config      = ts_conf;

	m->datalen = sizeof(*tm);
	m->data = (unsigned long) tm;

	return 0;
}

static void em_text_destroy(struct tcf_ematch *m)
{
	if (EM_TEXT_PRIV(m) && EM_TEXT_PRIV(m)->config)
		textsearch_destroy(EM_TEXT_PRIV(m)->config);
}

static int em_text_dump(struct sk_buff *skb, struct tcf_ematch *m)
{
	struct text_match *tm = EM_TEXT_PRIV(m);
	struct tcf_em_text conf;

	strncpy(conf.algo, tm->config->ops->name, sizeof(conf.algo) - 1);
	conf.from_offset = tm->from_offset;
	conf.to_offset = tm->to_offset;
	conf.from_layer = tm->from_layer;
	conf.to_layer = tm->to_layer;
	conf.pattern_len = textsearch_get_pattern_len(tm->config);
	conf.pad = 0;

	if (nla_put_nohdr(skb, sizeof(conf), &conf) < 0)
		goto nla_put_failure;
	if (nla_append(skb, conf.pattern_len,
		       textsearch_get_pattern(tm->config)) < 0)
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static struct tcf_ematch_ops em_text_ops = {
	.kind	  = TCF_EM_TEXT,
	.change	  = em_text_change,
	.match	  = em_text_match,
	.destroy  = em_text_destroy,
	.dump	  = em_text_dump,
	.owner	  = THIS_MODULE,
	.link	  = LIST_HEAD_INIT(em_text_ops.link)
};

static int __init init_em_text(void)
{
	return tcf_em_register(&em_text_ops);
}

static void __exit exit_em_text(void)
{
	tcf_em_unregister(&em_text_ops);
}

MODULE_LICENSE("GPL");

module_init(init_em_text);
module_exit(exit_em_text);

MODULE_ALIAS_TCF_EMATCH(TCF_EM_TEXT);
