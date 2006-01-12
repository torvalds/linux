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

MODULE_AUTHOR("Henrik Nordstrom <hno@marasytems.com>");
MODULE_DESCRIPTION("IP tables connmark match module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_connmark");

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_connmark.h>
#include <net/netfilter/nf_conntrack_compat.h>

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_connmark_info *info = matchinfo;
	u_int32_t ctinfo;
	const u_int32_t *ctmark = nf_ct_get_mark(skb, &ctinfo);
	if (!ctmark)
		return 0;

	return (((*ctmark) & info->mask) == info->mark) ^ info->invert;
}

static int
checkentry(const char *tablename,
	   const void *ip,
	   void *matchinfo,
	   unsigned int matchsize,
	   unsigned int hook_mask)
{
	struct xt_connmark_info *cm = 
				(struct xt_connmark_info *)matchinfo;
	if (matchsize != XT_ALIGN(sizeof(struct xt_connmark_info)))
		return 0;

	if (cm->mark > 0xffffffff || cm->mask > 0xffffffff) {
		printk(KERN_WARNING "connmark: only support 32bit mark\n");
		return 0;
	}

	return 1;
}

static struct xt_match connmark_match = {
	.name = "connmark",
	.match = &match,
	.checkentry = &checkentry,
	.me = THIS_MODULE
};
static struct xt_match connmark6_match = {
	.name = "connmark",
	.match = &match,
	.checkentry = &checkentry,
	.me = THIS_MODULE
};


static int __init init(void)
{
	int ret;

	need_conntrack();

	ret = xt_register_match(AF_INET, &connmark_match);
	if (ret)
		return ret;

	ret = xt_register_match(AF_INET6, &connmark6_match);
	if (ret)
		xt_unregister_match(AF_INET, &connmark_match);
	return ret;
}

static void __exit fini(void)
{
	xt_unregister_match(AF_INET6, &connmark6_match);
	xt_unregister_match(AF_INET, &connmark_match);
}

module_init(init);
module_exit(fini);
