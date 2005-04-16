/* This is a module which is used for setting the NFMARK field of an skb. */

/* (C) 1999-2001 Marc Boucher <marc@mbsi.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/checksum.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_MARK.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables MARK modification module");

static unsigned int
target_v0(struct sk_buff **pskb,
	  const struct net_device *in,
	  const struct net_device *out,
	  unsigned int hooknum,
	  const void *targinfo,
	  void *userinfo)
{
	const struct ipt_mark_target_info *markinfo = targinfo;

	if((*pskb)->nfmark != markinfo->mark) {
		(*pskb)->nfmark = markinfo->mark;
		(*pskb)->nfcache |= NFC_ALTERED;
	}
	return IPT_CONTINUE;
}

static unsigned int
target_v1(struct sk_buff **pskb,
	  const struct net_device *in,
	  const struct net_device *out,
	  unsigned int hooknum,
	  const void *targinfo,
	  void *userinfo)
{
	const struct ipt_mark_target_info_v1 *markinfo = targinfo;
	int mark = 0;

	switch (markinfo->mode) {
	case IPT_MARK_SET:
		mark = markinfo->mark;
		break;
		
	case IPT_MARK_AND:
		mark = (*pskb)->nfmark & markinfo->mark;
		break;
		
	case IPT_MARK_OR:
		mark = (*pskb)->nfmark | markinfo->mark;
		break;
	}

	if((*pskb)->nfmark != mark) {
		(*pskb)->nfmark = mark;
		(*pskb)->nfcache |= NFC_ALTERED;
	}
	return IPT_CONTINUE;
}


static int
checkentry_v0(const char *tablename,
	      const struct ipt_entry *e,
	      void *targinfo,
	      unsigned int targinfosize,
	      unsigned int hook_mask)
{
	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_mark_target_info))) {
		printk(KERN_WARNING "MARK: targinfosize %u != %Zu\n",
		       targinfosize,
		       IPT_ALIGN(sizeof(struct ipt_mark_target_info)));
		return 0;
	}

	if (strcmp(tablename, "mangle") != 0) {
		printk(KERN_WARNING "MARK: can only be called from \"mangle\" table, not \"%s\"\n", tablename);
		return 0;
	}

	return 1;
}

static int
checkentry_v1(const char *tablename,
	      const struct ipt_entry *e,
	      void *targinfo,
	      unsigned int targinfosize,
	      unsigned int hook_mask)
{
	struct ipt_mark_target_info_v1 *markinfo = targinfo;

	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_mark_target_info_v1))){
		printk(KERN_WARNING "MARK: targinfosize %u != %Zu\n",
		       targinfosize,
		       IPT_ALIGN(sizeof(struct ipt_mark_target_info_v1)));
		return 0;
	}

	if (strcmp(tablename, "mangle") != 0) {
		printk(KERN_WARNING "MARK: can only be called from \"mangle\" table, not \"%s\"\n", tablename);
		return 0;
	}

	if (markinfo->mode != IPT_MARK_SET
	    && markinfo->mode != IPT_MARK_AND
	    && markinfo->mode != IPT_MARK_OR) {
		printk(KERN_WARNING "MARK: unknown mode %u\n",
		       markinfo->mode);
		return 0;
	}

	return 1;
}

static struct ipt_target ipt_mark_reg_v0 = {
	.name		= "MARK",
	.target		= target_v0,
	.checkentry	= checkentry_v0,
	.me		= THIS_MODULE,
	.revision	= 0,
};

static struct ipt_target ipt_mark_reg_v1 = {
	.name		= "MARK",
	.target		= target_v1,
	.checkentry	= checkentry_v1,
	.me		= THIS_MODULE,
	.revision	= 1,
};

static int __init init(void)
{
	int err;

	err = ipt_register_target(&ipt_mark_reg_v0);
	if (!err) {
		err = ipt_register_target(&ipt_mark_reg_v1);
		if (err)
			ipt_unregister_target(&ipt_mark_reg_v0);
	}
	return err;
}

static void __exit fini(void)
{
	ipt_unregister_target(&ipt_mark_reg_v0);
	ipt_unregister_target(&ipt_mark_reg_v1);
}

module_init(init);
module_exit(fini);
