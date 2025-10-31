/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_SELFTESTS
#define _NET_SELFTESTS

#include <linux/ethtool.h>
#include <linux/netdevice.h>

struct net_packet_attrs {
	const unsigned char *src;
	const unsigned char *dst;
	u32 ip_src;
	u32 ip_dst;
	bool tcp;
	u16 sport;
	u16 dport;
	int timeout;
	int size;
	int max_size;
	u8 id;
	u16 queue_mapping;
	bool bad_csum;
};

struct net_test_priv {
	struct net_packet_attrs *packet;
	struct packet_type pt;
	struct completion comp;
	int double_vlan;
	int vlan_id;
	int ok;
};

struct netsfhdr {
	__be32 version;
	__be64 magic;
	u8 id;
} __packed;

#define NET_TEST_PKT_SIZE (sizeof(struct ethhdr) + sizeof(struct iphdr) + \
			   sizeof(struct netsfhdr))
#define NET_TEST_PKT_MAGIC	0xdeadcafecafedeadULL
#define NET_LB_TIMEOUT		msecs_to_jiffies(200)

#if IS_ENABLED(CONFIG_NET_SELFTESTS)

struct sk_buff *net_test_get_skb(struct net_device *ndev, u8 id,
				 struct net_packet_attrs *attr);
void net_selftest(struct net_device *ndev, struct ethtool_test *etest,
		  u64 *buf);
int net_selftest_get_count(void);
void net_selftest_get_strings(u8 *data);

#else

static inline struct sk_buff *net_test_get_skb(struct net_device *ndev, u8 id,
					       struct net_packet_attrs *attr)
{
	return NULL;
}

static inline void net_selftest(struct net_device *ndev, struct ethtool_test *etest,
				u64 *buf)
{
}

static inline int net_selftest_get_count(void)
{
	return 0;
}

static inline void net_selftest_get_strings(u8 *data)
{
}

#endif
#endif /* _NET_SELFTESTS */
