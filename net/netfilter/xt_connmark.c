/* This kernel module matches connection mark values set by the
 * CONNMARK target
 *
 * Copyright (C) 2002,2004 MARA Systems AB <http://www.marasystems.com>
 * by Henrik Nordstrom <hno@marasystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/netfilter/nf_conntrack.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_connmark.h>

MODULE_AUTHOR("Henrik Nordstrom <hno@marasystems.com>");
MODULE_DESCRIPTION("IP tables connmark match module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_connmark");
MODULE_ALIAS("ip6t_connmark");

static bool
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      bool *hotdrop)
{
	const struct xt_connmark_info *info = matchinfo;
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return false;

	return ((ct->mark & info->mask) == info->mark) ^ info->invert;
}

static bool
checkentry(const char *tablename,
	   const void *ip,
	   const struct xt_match *match,
	   void *matchinfo,
	   unsigned int hook_mask)
{
	const struct xt_connmark_info *cm = matchinfo;

	if (cm->mark > 0xffffffff || cm->mask > 0xffffffff) {
		printk(KERN_WARNING "connmark: only support 32bit mark\n");
		return false;
	}
	if (nf_ct_l3proto_try_module_get(match->family) < 0) {
		printk(KERN_WARNING "can't load conntrack support for "
				    "proto=%d\n", match->family);
		return false;
	}
	return true;
}

static void
destroy(const struct xt_match *match, void *matchinfo)
{
	nf_ct_l3proto_module_put(match->family);
}

#ifdef CONFIG_COMPAT
struct compat_xt_connmark_info {
	compat_ulong_t	mark, mask;
	u_int8_t	invert;
	u_int8_t	__pad1;
	u_int16_t	__pad2;
};

static void compat_from_user(void *dst, void *src)
{
	const struct compat_xt_connmark_info *cm = src;
	struct xt_connmark_info m = {
		.mark	= cm->mark,
		.mask	= cm->mask,
		.invert	= cm->invert,
	};
	memcpy(dst, &m, sizeof(m));
}

static int compat_to_user(void __user *dst, void *src)
{
	const struct xt_connmark_info *m = src;
	struct compat_xt_connmark_info cm = {
		.mark	= m->mark,
		.mask	= m->mask,
		.invert	= m->invert,
	};
	return copy_to_user(dst, &cm, sizeof(cm)) ? -EFAULT : 0;
}
#endif /* CONFIG_COMPAT */

static struct xt_match xt_connmark_match[] __read_mostly = {
	{
		.name		= "connmark",
		.family		= AF_INET,
		.checkentry	= checkentry,
		.match		= match,
		.destroy	= destroy,
		.matchsize	= sizeof(struct xt_connmark_info),
#ifdef CONFIG_COMPAT
		.compatsize	= sizeof(struct compat_xt_connmark_info),
		.compat_from_user = compat_from_user,
		.compat_to_user	= compat_to_user,
#endif
		.me		= THIS_MODULE
	},
	{
		.name		= "connmark",
		.family		= AF_INET6,
		.checkentry	= checkentry,
		.match		= match,
		.destroy	= destroy,
		.matchsize	= sizeof(struct xt_connmark_info),
		.me		= THIS_MODULE
	},
};

static int __init xt_connmark_init(void)
{
	return xt_register_matches(xt_connmark_match,
				   ARRAY_SIZE(xt_connmark_match));
}

static void __exit xt_connmark_fini(void)
{
	xt_unregister_matches(xt_connmark_match, ARRAY_SIZE(xt_connmark_match));
}

module_init(xt_connmark_init);
module_exit(xt_connmark_fini);
