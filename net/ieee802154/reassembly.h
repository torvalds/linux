#ifndef __IEEE802154_6LOWPAN_REASSEMBLY_H__
#define __IEEE802154_6LOWPAN_REASSEMBLY_H__

#include <net/inet_frag.h>

struct lowpan_create_arg {
	__be16 tag;
	u16 d_size;
	const struct ieee802154_addr *src;
	const struct ieee802154_addr *dst;
};

/* Equivalent of ipv4 struct ip
 */
struct lowpan_frag_queue {
	struct inet_frag_queue	q;

	__be16			tag;
	u16			d_size;
	struct ieee802154_addr	saddr;
	struct ieee802154_addr	daddr;
};

static inline u32 ieee802154_addr_hash(const struct ieee802154_addr *a)
{
	switch (a->mode) {
	case IEEE802154_ADDR_LONG:
		return (((__force u64)a->extended_addr) >> 32) ^
			(((__force u64)a->extended_addr) & 0xffffffff);
	case IEEE802154_ADDR_SHORT:
		return (__force u32)(a->short_addr);
	default:
		return 0;
	}
}

int lowpan_frag_rcv(struct sk_buff *skb, const u8 frag_type);
void lowpan_net_frag_exit(void);
int lowpan_net_frag_init(void);

#endif /* __IEEE802154_6LOWPAN_REASSEMBLY_H__ */
