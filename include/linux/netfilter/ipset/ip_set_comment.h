#ifndef _IP_SET_COMMENT_H
#define _IP_SET_COMMENT_H

/* Copyright (C) 2013 Oliver Smith <oliver@8.c.9.b.0.7.4.0.1.0.0.2.ip6.arpa>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef __KERNEL__

static inline char*
ip_set_comment_uget(struct nlattr *tb)
{
	return nla_data(tb);
}

static inline void
ip_set_init_comment(struct ip_set_comment *comment,
		    const struct ip_set_ext *ext)
{
	size_t len = ext->comment ? strlen(ext->comment) : 0;

	if (unlikely(comment->str)) {
		kfree(comment->str);
		comment->str = NULL;
	}
	if (!len)
		return;
	if (unlikely(len > IPSET_MAX_COMMENT_SIZE))
		len = IPSET_MAX_COMMENT_SIZE;
	comment->str = kzalloc(len + 1, GFP_ATOMIC);
	if (unlikely(!comment->str))
		return;
	strlcpy(comment->str, ext->comment, len + 1);
}

static inline int
ip_set_put_comment(struct sk_buff *skb, struct ip_set_comment *comment)
{
	if (!comment->str)
		return 0;
	return nla_put_string(skb, IPSET_ATTR_COMMENT, comment->str);
}

static inline void
ip_set_comment_free(struct ip_set_comment *comment)
{
	if (unlikely(!comment->str))
		return;
	kfree(comment->str);
	comment->str = NULL;
}

#endif
#endif
