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
	switch (a->addr_type) {
	case IEEE802154_ADDR_LONG:
		return (__force u32)((((u32 *)a->hwaddr))[0] ^
				      ((u32 *)(a->hwaddr))[1]);
	case IEEE802154_ADDR_SHORT:
		return (__force u32)(a->short_addr);
	default:
		return 0;
	}
}

static inline bool ieee802154_addr_addr_equal(const struct ieee802154_addr *a1,
				   const struct ieee802154_addr *a2)
{
	if (a1->pan_id != a2->pan_id)
		return false;

	if (a1->addr_type != a2->addr_type)
		return false;

	switch (a1->addr_type) {
	case IEEE802154_ADDR_LONG:
		if (memcmp(a1->hwaddr, a2->hwaddr, IEEE802154_ADDR_LEN))
			return false;
		break;
	case IEEE802154_ADDR_SHORT:
		if (a1->short_addr != a2->short_addr)
			return false;
		break;
	default:
		return false;
	}

	return true;
}

int lowpan_frag_rcv(struct sk_buff *skb, const u8 frag_type);
void lowpan_net_frag_exit(void);
int lowpan_net_frag_init(void);

#endif /* __IEEE802154_6LOWPAN_REASSEMBLY_H__ */
