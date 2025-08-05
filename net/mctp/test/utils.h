/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __NET_MCTP_TEST_UTILS_H
#define __NET_MCTP_TEST_UTILS_H

#include <uapi/linux/netdevice.h>

#include <net/mctp.h>
#include <net/mctpdevice.h>

#include <kunit/test.h>

#define MCTP_DEV_TEST_MTU	68

struct mctp_test_dev {
	struct net_device *ndev;
	struct mctp_dev *mdev;

	unsigned short lladdr_len;
	unsigned char lladdr[MAX_ADDR_LEN];
};

struct mctp_test_dev;

struct mctp_test_route {
	struct mctp_route	rt;
};

struct mctp_test_pktqueue {
	unsigned int magic;
	struct sk_buff_head pkts;
};

struct mctp_test_bind_setup {
	mctp_eid_t bind_addr;
	int bind_net;
	u8 bind_type;

	bool have_peer;
	mctp_eid_t peer_addr;
	int peer_net;

	/* optional name. Used for comparison in "lookup" tests */
	const char *name;
};

struct mctp_test_dev *mctp_test_create_dev(void);
struct mctp_test_dev *mctp_test_create_dev_lladdr(unsigned short lladdr_len,
						  const unsigned char *lladdr);
void mctp_test_destroy_dev(struct mctp_test_dev *dev);

struct mctp_test_route *mctp_test_create_route_direct(struct net *net,
						      struct mctp_dev *dev,
						      mctp_eid_t eid,
						      unsigned int mtu);
struct mctp_test_route *mctp_test_create_route_gw(struct net *net,
						  unsigned int netid,
						  mctp_eid_t eid,
						  mctp_eid_t gw,
						  unsigned int mtu);
void mctp_test_dst_setup(struct kunit *test, struct mctp_dst *dst,
			 struct mctp_test_dev *dev,
			 struct mctp_test_pktqueue *tpq, unsigned int mtu);
void mctp_test_dst_release(struct mctp_dst *dst,
			   struct mctp_test_pktqueue *tpq);
void mctp_test_pktqueue_init(struct mctp_test_pktqueue *tpq);
void mctp_test_route_destroy(struct kunit *test, struct mctp_test_route *rt);
void mctp_test_skb_set_dev(struct sk_buff *skb, struct mctp_test_dev *dev);
struct sk_buff *mctp_test_create_skb(const struct mctp_hdr *hdr,
				     unsigned int data_len);
struct sk_buff *__mctp_test_create_skb_data(const struct mctp_hdr *hdr,
					    const void *data, size_t data_len);

#define mctp_test_create_skb_data(h, d) \
	__mctp_test_create_skb_data(h, d, sizeof(*d))

void mctp_test_bind_run(struct kunit *test,
			const struct mctp_test_bind_setup *setup,
			int *ret_bind_errno, struct socket **sock);

#endif /* __NET_MCTP_TEST_UTILS_H */
