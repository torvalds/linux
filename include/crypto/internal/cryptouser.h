/* SPDX-License-Identifier: GPL-2.0 */
#include <net/netlink.h>

struct crypto_alg *crypto_alg_match(struct crypto_user_alg *p, int exact);

int crypto_dump_reportstat(struct sk_buff *skb, struct netlink_callback *cb);
int crypto_reportstat(struct sk_buff *in_skb, struct nlmsghdr *in_nlh, struct nlattr **attrs);
int crypto_dump_reportstat_done(struct netlink_callback *cb);
