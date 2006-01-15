/* Kernel module to match one of a list of TCP/UDP ports: ports are in
   the same place so we can treat them as equal. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/udp.h>
#include <linux/skbuff.h>

#include <linux/netfilter_ipv4/ipt_multiport.h>
#include <linux/netfilter_ipv4/ip_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("iptables multiple port match module");

#if 0
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

/* Returns 1 if the port is matched by the test, 0 otherwise. */
static inline int
ports_match(const u_int16_t *portlist, enum ipt_multiport_flags flags,
	    u_int8_t count, u_int16_t src, u_int16_t dst)
{
	unsigned int i;
	for (i=0; i<count; i++) {
		if (flags != IPT_MULTIPORT_DESTINATION
		    && portlist[i] == src)
			return 1;

		if (flags != IPT_MULTIPORT_SOURCE
		    && portlist[i] == dst)
			return 1;
	}

	return 0;
}

/* Returns 1 if the port is matched by the test, 0 otherwise. */
static inline int
ports_match_v1(const struct ipt_multiport_v1 *minfo,
	       u_int16_t src, u_int16_t dst)
{
	unsigned int i;
	u_int16_t s, e;

	for (i=0; i < minfo->count; i++) {
		s = minfo->ports[i];

		if (minfo->pflags[i]) {
			/* range port matching */
			e = minfo->ports[++i];
			duprintf("src or dst matches with %d-%d?\n", s, e);

			if (minfo->flags == IPT_MULTIPORT_SOURCE
			    && src >= s && src <= e)
				return 1 ^ minfo->invert;
			if (minfo->flags == IPT_MULTIPORT_DESTINATION
			    && dst >= s && dst <= e)
				return 1 ^ minfo->invert;
			if (minfo->flags == IPT_MULTIPORT_EITHER
			    && ((dst >= s && dst <= e)
				|| (src >= s && src <= e)))
				return 1 ^ minfo->invert;
		} else {
			/* exact port matching */
			duprintf("src or dst matches with %d?\n", s);

			if (minfo->flags == IPT_MULTIPORT_SOURCE
			    && src == s)
				return 1 ^ minfo->invert;
			if (minfo->flags == IPT_MULTIPORT_DESTINATION
			    && dst == s)
				return 1 ^ minfo->invert;
			if (minfo->flags == IPT_MULTIPORT_EITHER
			    && (src == s || dst == s))
				return 1 ^ minfo->invert;
		}
	}
 
 	return minfo->invert;
}

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	u16 _ports[2], *pptr;
	const struct ipt_multiport *multiinfo = matchinfo;

	if (offset)
		return 0;

	pptr = skb_header_pointer(skb, protoff,
				  sizeof(_ports), _ports);
	if (pptr == NULL) {
		/* We've been asked to examine this packet, and we
		 * can't.  Hence, no choice but to drop.
		 */
		duprintf("ipt_multiport:"
			 " Dropping evil offset=0 tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	return ports_match(multiinfo->ports,
			   multiinfo->flags, multiinfo->count,
			   ntohs(pptr[0]), ntohs(pptr[1]));
}

static int
match_v1(const struct sk_buff *skb,
	 const struct net_device *in,
	 const struct net_device *out,
	 const void *matchinfo,
	 int offset,
	 unsigned int protoff,
	 int *hotdrop)
{
	u16 _ports[2], *pptr;
	const struct ipt_multiport_v1 *multiinfo = matchinfo;

	if (offset)
		return 0;

	pptr = skb_header_pointer(skb, protoff,
				  sizeof(_ports), _ports);
	if (pptr == NULL) {
		/* We've been asked to examine this packet, and we
		 * can't.  Hence, no choice but to drop.
		 */
		duprintf("ipt_multiport:"
			 " Dropping evil offset=0 tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	return ports_match_v1(multiinfo, ntohs(pptr[0]), ntohs(pptr[1]));
}

/* Called when user tries to insert an entry of this type. */
static int
checkentry(const char *tablename,
	   const void *ip,
	   void *matchinfo,
	   unsigned int matchsize,
	   unsigned int hook_mask)
{
	return (matchsize == IPT_ALIGN(sizeof(struct ipt_multiport)));
}

static int
checkentry_v1(const char *tablename,
	      const void *ip,
	      void *matchinfo,
	      unsigned int matchsize,
	      unsigned int hook_mask)
{
	return (matchsize == IPT_ALIGN(sizeof(struct ipt_multiport_v1)));
}

static struct ipt_match multiport_match = {
	.name		= "multiport",
	.revision	= 0,
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static struct ipt_match multiport_match_v1 = {
	.name		= "multiport",
	.revision	= 1,
	.match		= &match_v1,
	.checkentry	= &checkentry_v1,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	int err;

	err = ipt_register_match(&multiport_match);
	if (!err) {
		err = ipt_register_match(&multiport_match_v1);
		if (err)
			ipt_unregister_match(&multiport_match);
	}

	return err;
}

static void __exit fini(void)
{
	ipt_unregister_match(&multiport_match);
	ipt_unregister_match(&multiport_match_v1);
}

module_init(init);
module_exit(fini);
