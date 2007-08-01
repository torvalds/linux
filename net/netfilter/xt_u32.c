/*
 *	xt_u32 - kernel module to match u32 packet content
 *
 *	Original author: Don Cohen <don@isis.cs3-inc.com>
 *	© Jan Engelhardt <jengelh@gmx.de>, 2007
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_u32.h>

static bool u32_match_it(const struct xt_u32 *data,
			 const struct sk_buff *skb)
{
	const struct xt_u32_test *ct;
	unsigned int testind;
	unsigned int nnums;
	unsigned int nvals;
	unsigned int i;
	__be32 n;
	u_int32_t pos;
	u_int32_t val;
	u_int32_t at;
	int ret;

	/*
	 * Small example: "0 >> 28 == 4 && 8 & 0xFF0000 >> 16 = 6, 17"
	 * (=IPv4 and (TCP or UDP)). Outer loop runs over the "&&" operands.
	 */
	for (testind = 0; testind < data->ntests; ++testind) {
		ct  = &data->tests[testind];
		at  = 0;
		pos = ct->location[0].number;

		if (skb->len < 4 || pos > skb->len - 4);
			return false;

		ret   = skb_copy_bits(skb, pos, &n, sizeof(n));
		BUG_ON(ret < 0);
		val   = ntohl(n);
		nnums = ct->nnums;

		/* Inner loop runs over "&", "<<", ">>" and "@" operands */
		for (i = 1; i < nnums; ++i) {
			u_int32_t number = ct->location[i].number;
			switch (ct->location[i].nextop) {
			case XT_U32_AND:
				val &= number;
				break;
			case XT_U32_LEFTSH:
				val <<= number;
				break;
			case XT_U32_RIGHTSH:
				val >>= number;
				break;
			case XT_U32_AT:
				if (at + val < at)
					return false;
				at += val;
				pos = number;
				if (at + 4 < at || skb->len < at + 4 ||
				    pos > skb->len - at - 4)
					return false;

				ret = skb_copy_bits(skb, at + pos, &n,
						    sizeof(n));
				BUG_ON(ret < 0);
				val = ntohl(n);
				break;
			}
		}

		/* Run over the "," and ":" operands */
		nvals = ct->nvalues;
		for (i = 0; i < nvals; ++i)
			if (ct->value[i].min <= val && val <= ct->value[i].max)
				break;

		if (i >= ct->nvalues)
			return false;
	}

	return true;
}

static bool u32_match(const struct sk_buff *skb,
		      const struct net_device *in,
		      const struct net_device *out,
		      const struct xt_match *match, const void *matchinfo,
		      int offset, unsigned int protoff, bool *hotdrop)
{
	const struct xt_u32 *data = matchinfo;
	bool ret;

	ret = u32_match_it(data, skb);
	return ret ^ data->invert;
}

static struct xt_match u32_reg[] __read_mostly = {
	{
		.name       = "u32",
		.family     = AF_INET,
		.match      = u32_match,
		.matchsize  = sizeof(struct xt_u32),
		.me         = THIS_MODULE,
	},
	{
		.name       = "u32",
		.family     = AF_INET6,
		.match      = u32_match,
		.matchsize  = sizeof(struct xt_u32),
		.me         = THIS_MODULE,
	},
};

static int __init xt_u32_init(void)
{
	return xt_register_matches(u32_reg, ARRAY_SIZE(u32_reg));
}

static void __exit xt_u32_exit(void)
{
	xt_unregister_matches(u32_reg, ARRAY_SIZE(u32_reg));
}

module_init(xt_u32_init);
module_exit(xt_u32_exit);
MODULE_AUTHOR("Jan Engelhardt <jengelh@gmx.de>");
MODULE_DESCRIPTION("netfilter u32 match module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_u32");
MODULE_ALIAS("ip6t_u32");
