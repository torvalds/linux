/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/cryptouser.h>
#include <net/netlink.h>

struct crypto_alg *crypto_alg_match(struct crypto_user_alg *p, int exact);

#ifdef CONFIG_CRYPTO_STATS
int crypto_reportstat(struct sk_buff *in_skb, struct nlmsghdr *in_nlh, struct nlattr **attrs);
#else
static inline int crypto_reportstat(struct sk_buff *in_skb,
				    struct nlmsghdr *in_nlh,
				    struct nlattr **attrs)
{
	return -ENOTSUPP;
}
#endif
