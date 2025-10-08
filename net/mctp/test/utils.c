// SPDX-License-Identifier: GPL-2.0

#include <linux/netdevice.h>
#include <linux/mctp.h>
#include <linux/if_arp.h>

#include <net/mctp.h>
#include <net/mctpdevice.h>
#include <net/pkt_sched.h>

#include "utils.h"

static netdev_tx_t mctp_test_dev_tx(struct sk_buff *skb,
				    struct net_device *ndev)
{
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops mctp_test_netdev_ops = {
	.ndo_start_xmit = mctp_test_dev_tx,
};

static void mctp_test_dev_setup(struct net_device *ndev)
{
	ndev->type = ARPHRD_MCTP;
	ndev->mtu = MCTP_DEV_TEST_MTU;
	ndev->hard_header_len = 0;
	ndev->tx_queue_len = DEFAULT_TX_QUEUE_LEN;
	ndev->flags = IFF_NOARP;
	ndev->netdev_ops = &mctp_test_netdev_ops;
	ndev->needs_free_netdev = true;
}

static struct mctp_test_dev *__mctp_test_create_dev(unsigned short lladdr_len,
						    const unsigned char *lladdr)
{
	struct mctp_test_dev *dev;
	struct net_device *ndev;
	int rc;

	if (WARN_ON(lladdr_len > MAX_ADDR_LEN))
		return NULL;

	ndev = alloc_netdev(sizeof(*dev), "mctptest%d", NET_NAME_ENUM,
			    mctp_test_dev_setup);
	if (!ndev)
		return NULL;

	dev = netdev_priv(ndev);
	dev->ndev = ndev;
	ndev->addr_len = lladdr_len;
	dev_addr_set(ndev, lladdr);

	rc = register_netdev(ndev);
	if (rc) {
		free_netdev(ndev);
		return NULL;
	}

	rcu_read_lock();
	dev->mdev = __mctp_dev_get(ndev);
	dev->mdev->net = mctp_default_net(dev_net(ndev));
	rcu_read_unlock();

	return dev;
}

struct mctp_test_dev *mctp_test_create_dev(void)
{
	return __mctp_test_create_dev(0, NULL);
}

struct mctp_test_dev *mctp_test_create_dev_lladdr(unsigned short lladdr_len,
						  const unsigned char *lladdr)
{
	return __mctp_test_create_dev(lladdr_len, lladdr);
}

void mctp_test_destroy_dev(struct mctp_test_dev *dev)
{
	mctp_dev_put(dev->mdev);
	unregister_netdev(dev->ndev);
}

static const unsigned int test_pktqueue_magic = 0x5f713aef;

void mctp_test_pktqueue_init(struct mctp_test_pktqueue *tpq)
{
	tpq->magic = test_pktqueue_magic;
	skb_queue_head_init(&tpq->pkts);
}

static int mctp_test_dst_output(struct mctp_dst *dst, struct sk_buff *skb)
{
	struct kunit *test = current->kunit_test;
	struct mctp_test_pktqueue *tpq = test->priv;

	KUNIT_ASSERT_EQ(test, tpq->magic, test_pktqueue_magic);

	skb_queue_tail(&tpq->pkts, skb);

	return 0;
}

/* local version of mctp_route_alloc() */
static struct mctp_test_route *mctp_route_test_alloc(void)
{
	struct mctp_test_route *rt;

	rt = kzalloc(sizeof(*rt), GFP_KERNEL);
	if (!rt)
		return NULL;

	INIT_LIST_HEAD(&rt->rt.list);
	refcount_set(&rt->rt.refs, 1);
	rt->rt.output = mctp_test_dst_output;

	return rt;
}

struct mctp_test_route *mctp_test_create_route_direct(struct net *net,
						      struct mctp_dev *dev,
						      mctp_eid_t eid,
						      unsigned int mtu)
{
	struct mctp_test_route *rt;

	rt = mctp_route_test_alloc();
	if (!rt)
		return NULL;

	rt->rt.min = eid;
	rt->rt.max = eid;
	rt->rt.mtu = mtu;
	rt->rt.type = RTN_UNSPEC;
	rt->rt.dst_type = MCTP_ROUTE_DIRECT;
	if (dev)
		mctp_dev_hold(dev);
	rt->rt.dev = dev;

	list_add_rcu(&rt->rt.list, &net->mctp.routes);

	return rt;
}

struct mctp_test_route *mctp_test_create_route_gw(struct net *net,
						  unsigned int netid,
						  mctp_eid_t eid,
						  mctp_eid_t gw,
						  unsigned int mtu)
{
	struct mctp_test_route *rt;

	rt = mctp_route_test_alloc();
	if (!rt)
		return NULL;

	rt->rt.min = eid;
	rt->rt.max = eid;
	rt->rt.mtu = mtu;
	rt->rt.type = RTN_UNSPEC;
	rt->rt.dst_type = MCTP_ROUTE_GATEWAY;
	rt->rt.gateway.eid = gw;
	rt->rt.gateway.net = netid;

	list_add_rcu(&rt->rt.list, &net->mctp.routes);

	return rt;
}

/* Convenience function for our test dst; release with mctp_test_dst_release()
 */
void mctp_test_dst_setup(struct kunit *test, struct mctp_dst *dst,
			 struct mctp_test_dev *dev,
			 struct mctp_test_pktqueue *tpq, unsigned int mtu)
{
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, dev);

	memset(dst, 0, sizeof(*dst));

	dst->dev = dev->mdev;
	__mctp_dev_get(dst->dev->dev);
	dst->mtu = mtu;
	dst->output = mctp_test_dst_output;
	mctp_test_pktqueue_init(tpq);
	test->priv = tpq;
}

void mctp_test_dst_release(struct mctp_dst *dst,
			   struct mctp_test_pktqueue *tpq)
{
	mctp_dst_release(dst);
	skb_queue_purge(&tpq->pkts);
}

void mctp_test_route_destroy(struct kunit *test, struct mctp_test_route *rt)
{
	unsigned int refs;

	rtnl_lock();
	list_del_rcu(&rt->rt.list);
	rtnl_unlock();

	if (rt->rt.dst_type == MCTP_ROUTE_DIRECT && rt->rt.dev)
		mctp_dev_put(rt->rt.dev);

	refs = refcount_read(&rt->rt.refs);
	KUNIT_ASSERT_EQ_MSG(test, refs, 1, "route ref imbalance");

	kfree_rcu(&rt->rt, rcu);
}

void mctp_test_skb_set_dev(struct sk_buff *skb, struct mctp_test_dev *dev)
{
	struct mctp_skb_cb *cb;

	cb = mctp_cb(skb);
	cb->net = READ_ONCE(dev->mdev->net);
	skb->dev = dev->ndev;
}

struct sk_buff *mctp_test_create_skb(const struct mctp_hdr *hdr,
				     unsigned int data_len)
{
	size_t hdr_len = sizeof(*hdr);
	struct sk_buff *skb;
	unsigned int i;
	u8 *buf;

	skb = alloc_skb(hdr_len + data_len, GFP_KERNEL);
	if (!skb)
		return NULL;

	__mctp_cb(skb);
	memcpy(skb_put(skb, hdr_len), hdr, hdr_len);

	buf = skb_put(skb, data_len);
	for (i = 0; i < data_len; i++)
		buf[i] = i & 0xff;

	return skb;
}

struct sk_buff *__mctp_test_create_skb_data(const struct mctp_hdr *hdr,
					    const void *data, size_t data_len)
{
	size_t hdr_len = sizeof(*hdr);
	struct sk_buff *skb;

	skb = alloc_skb(hdr_len + data_len, GFP_KERNEL);
	if (!skb)
		return NULL;

	__mctp_cb(skb);
	memcpy(skb_put(skb, hdr_len), hdr, hdr_len);
	memcpy(skb_put(skb, data_len), data, data_len);

	return skb;
}

void mctp_test_bind_run(struct kunit *test,
			const struct mctp_test_bind_setup *setup,
			int *ret_bind_errno, struct socket **sock)
{
	struct sockaddr_mctp addr;
	int rc;

	*ret_bind_errno = -EIO;

	rc = sock_create_kern(&init_net, AF_MCTP, SOCK_DGRAM, 0, sock);
	KUNIT_ASSERT_EQ(test, rc, 0);

	/* connect() if requested */
	if (setup->have_peer) {
		memset(&addr, 0x0, sizeof(addr));
		addr.smctp_family = AF_MCTP;
		addr.smctp_network = setup->peer_net;
		addr.smctp_addr.s_addr = setup->peer_addr;
		/* connect() type must match bind() type */
		addr.smctp_type = setup->bind_type;
		rc = kernel_connect(*sock, (struct sockaddr *)&addr,
				    sizeof(addr), 0);
		KUNIT_EXPECT_EQ(test, rc, 0);
	}

	/* bind() */
	memset(&addr, 0x0, sizeof(addr));
	addr.smctp_family = AF_MCTP;
	addr.smctp_network = setup->bind_net;
	addr.smctp_addr.s_addr = setup->bind_addr;
	addr.smctp_type = setup->bind_type;

	*ret_bind_errno =
		kernel_bind(*sock, (struct sockaddr *)&addr, sizeof(addr));
}
