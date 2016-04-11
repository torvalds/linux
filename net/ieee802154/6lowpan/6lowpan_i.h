#ifndef __IEEE802154_6LOWPAN_I_H__
#define __IEEE802154_6LOWPAN_I_H__

#include <linux/list.h>

#include <net/ieee802154_netdev.h>
#include <net/inet_frag.h>
#include <net/6lowpan.h>

typedef unsigned __bitwise__ lowpan_rx_result;
#define RX_CONTINUE		((__force lowpan_rx_result) 0u)
#define RX_DROP_UNUSABLE	((__force lowpan_rx_result) 1u)
#define RX_DROP			((__force lowpan_rx_result) 2u)
#define RX_QUEUED		((__force lowpan_rx_result) 3u)

#define LOWPAN_DISPATCH_FRAG1           0xc0
#define LOWPAN_DISPATCH_FRAGN           0xe0

struct lowpan_create_arg {
	u16 tag;
	u16 d_size;
	const struct ieee802154_addr *src;
	const struct ieee802154_addr *dst;
};

/* Equivalent of ipv4 struct ip
 */
struct lowpan_frag_queue {
	struct inet_frag_queue	q;

	u16			tag;
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
		return (__force u32)(a->short_addr + (a->pan_id << 16));
	default:
		return 0;
	}
}

int lowpan_frag_rcv(struct sk_buff *skb, const u8 frag_type);
void lowpan_net_frag_exit(void);
int lowpan_net_frag_init(void);

void lowpan_rx_init(void);
void lowpan_rx_exit(void);

int lowpan_header_create(struct sk_buff *skb, struct net_device *dev,
			 unsigned short type, const void *_daddr,
			 const void *_saddr, unsigned int len);
netdev_tx_t lowpan_xmit(struct sk_buff *skb, struct net_device *dev);

int lowpan_iphc_decompress(struct sk_buff *skb);
lowpan_rx_result lowpan_rx_h_ipv6(struct sk_buff *skb);

#endif /* __IEEE802154_6LOWPAN_I_H__ */
