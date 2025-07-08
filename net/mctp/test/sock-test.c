// SPDX-License-Identifier: GPL-2.0

#include <kunit/static_stub.h>
#include <kunit/test.h>

#include <linux/socket.h>
#include <linux/spinlock.h>

#include "utils.h"

static const u8 dev_default_lladdr[] = { 0x01, 0x02 };

/* helper for simple sock setup: single device, with dev_default_lladdr as its
 * hardware address, assigned with a local EID 8, and a route to EID 9
 */
static void __mctp_sock_test_init(struct kunit *test,
				  struct mctp_test_dev **devp,
				  struct mctp_test_route **rtp,
				  struct socket **sockp)
{
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct socket *sock;
	unsigned long flags;
	u8 *addrs;
	int rc;

	dev = mctp_test_create_dev_lladdr(sizeof(dev_default_lladdr),
					  dev_default_lladdr);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	addrs = kmalloc(1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, addrs);
	addrs[0] = 8;

	spin_lock_irqsave(&dev->mdev->addrs_lock, flags);
	dev->mdev->num_addrs = 1;
	swap(addrs, dev->mdev->addrs);
	spin_unlock_irqrestore(&dev->mdev->addrs_lock, flags);

	kfree(addrs);

	rt = mctp_test_create_route_direct(dev_net(dev->ndev), dev->mdev, 9, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rt);

	rc = sock_create_kern(&init_net, AF_MCTP, SOCK_DGRAM, 0, &sock);
	KUNIT_ASSERT_EQ(test, rc, 0);

	*devp = dev;
	*rtp = rt;
	*sockp = sock;
}

static void __mctp_sock_test_fini(struct kunit *test,
				  struct mctp_test_dev *dev,
				  struct mctp_test_route *rt,
				  struct socket *sock)
{
	sock_release(sock);
	mctp_test_route_destroy(test, rt);
	mctp_test_destroy_dev(dev);
}

struct mctp_test_sock_local_output_config {
	struct mctp_test_dev *dev;
	size_t halen;
	u8 haddr[MAX_ADDR_LEN];
	bool invoked;
	int rc;
};

static int mctp_test_sock_local_output(struct sock *sk,
				       struct mctp_dst *dst,
				       struct sk_buff *skb,
				       mctp_eid_t daddr, u8 req_tag)
{
	struct kunit *test = kunit_get_current_test();
	struct mctp_test_sock_local_output_config *cfg = test->priv;

	KUNIT_EXPECT_PTR_EQ(test, dst->dev, cfg->dev->mdev);
	KUNIT_EXPECT_EQ(test, dst->halen, cfg->halen);
	KUNIT_EXPECT_MEMEQ(test, dst->haddr, cfg->haddr, dst->halen);

	cfg->invoked = true;

	kfree_skb(skb);

	return cfg->rc;
}

static void mctp_test_sock_sendmsg_extaddr(struct kunit *test)
{
	struct sockaddr_mctp_ext addr = {
		.smctp_base = {
			.smctp_family = AF_MCTP,
			.smctp_tag = MCTP_TAG_OWNER,
			.smctp_network = MCTP_NET_ANY,
		},
	};
	struct mctp_test_sock_local_output_config cfg = { 0 };
	u8 haddr[] = { 0xaa, 0x01 };
	u8 buf[4] = { 0, 1, 2, 3 };
	struct mctp_test_route *rt;
	struct msghdr msg = { 0 };
	struct mctp_test_dev *dev;
	struct mctp_sock *msk;
	struct socket *sock;
	ssize_t send_len;
	struct kvec vec = {
		.iov_base = buf,
		.iov_len = sizeof(buf),
	};

	__mctp_sock_test_init(test, &dev, &rt, &sock);

	/* Expect to see the dst configured up with the addressing data we
	 * provide in the struct sockaddr_mctp_ext
	 */
	cfg.dev = dev;
	cfg.halen = sizeof(haddr);
	memcpy(cfg.haddr, haddr, sizeof(haddr));

	test->priv = &cfg;

	kunit_activate_static_stub(test, mctp_local_output,
				   mctp_test_sock_local_output);

	/* enable and configure direct addressing */
	msk = container_of(sock->sk, struct mctp_sock, sk);
	msk->addr_ext = true;

	addr.smctp_ifindex = dev->ndev->ifindex;
	addr.smctp_halen = sizeof(haddr);
	memcpy(addr.smctp_haddr, haddr, sizeof(haddr));

	msg.msg_name = &addr;
	msg.msg_namelen = sizeof(addr);

	iov_iter_kvec(&msg.msg_iter, ITER_SOURCE, &vec, 1, sizeof(buf));
	send_len = mctp_sendmsg(sock, &msg, sizeof(buf));
	KUNIT_EXPECT_EQ(test, send_len, sizeof(buf));
	KUNIT_EXPECT_TRUE(test, cfg.invoked);

	__mctp_sock_test_fini(test, dev, rt, sock);
}

static void mctp_test_sock_recvmsg_extaddr(struct kunit *test)
{
	struct sockaddr_mctp_ext recv_addr = { 0 };
	u8 rcv_buf[1], rcv_data[] = { 0, 1 };
	u8 haddr[] = { 0xaa, 0x02 };
	struct mctp_test_route *rt;
	struct mctp_test_dev *dev;
	struct mctp_skb_cb *cb;
	struct mctp_sock *msk;
	struct sk_buff *skb;
	struct mctp_hdr hdr;
	struct socket *sock;
	struct msghdr msg;
	ssize_t recv_len;
	int rc;
	struct kvec vec = {
		.iov_base = rcv_buf,
		.iov_len = sizeof(rcv_buf),
	};

	__mctp_sock_test_init(test, &dev, &rt, &sock);

	/* enable extended addressing on recv */
	msk = container_of(sock->sk, struct mctp_sock, sk);
	msk->addr_ext = true;

	/* base incoming header, using a nul-EID dest */
	hdr.ver = 1;
	hdr.dest = 0;
	hdr.src = 9;
	hdr.flags_seq_tag = MCTP_HDR_FLAG_SOM | MCTP_HDR_FLAG_EOM |
			    MCTP_HDR_FLAG_TO;

	skb = mctp_test_create_skb_data(&hdr, &rcv_data);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, skb);

	mctp_test_skb_set_dev(skb, dev);

	/* set incoming extended address data */
	cb = mctp_cb(skb);
	cb->halen = sizeof(haddr);
	cb->ifindex = dev->ndev->ifindex;
	memcpy(cb->haddr, haddr, sizeof(haddr));

	/* Deliver to socket. The route input path pulls the network header,
	 * leaving skb data at type byte onwards. recvmsg will consume the
	 * type for addr.smctp_type
	 */
	skb_pull(skb, sizeof(hdr));
	rc = sock_queue_rcv_skb(sock->sk, skb);
	KUNIT_ASSERT_EQ(test, rc, 0);

	msg.msg_name = &recv_addr;
	msg.msg_namelen = sizeof(recv_addr);
	iov_iter_kvec(&msg.msg_iter, ITER_DEST, &vec, 1, sizeof(rcv_buf));

	recv_len = mctp_recvmsg(sock, &msg, sizeof(rcv_buf),
				MSG_DONTWAIT | MSG_TRUNC);

	KUNIT_EXPECT_EQ(test, recv_len, sizeof(rcv_buf));

	/* expect our extended address to be populated from hdr and cb */
	KUNIT_EXPECT_EQ(test, msg.msg_namelen, sizeof(recv_addr));
	KUNIT_EXPECT_EQ(test, recv_addr.smctp_base.smctp_family, AF_MCTP);
	KUNIT_EXPECT_EQ(test, recv_addr.smctp_ifindex, dev->ndev->ifindex);
	KUNIT_EXPECT_EQ(test, recv_addr.smctp_halen, sizeof(haddr));
	KUNIT_EXPECT_MEMEQ(test, recv_addr.smctp_haddr, haddr, sizeof(haddr));

	__mctp_sock_test_fini(test, dev, rt, sock);
}

static struct kunit_case mctp_test_cases[] = {
	KUNIT_CASE(mctp_test_sock_sendmsg_extaddr),
	KUNIT_CASE(mctp_test_sock_recvmsg_extaddr),
	{}
};

static struct kunit_suite mctp_test_suite = {
	.name = "mctp-sock",
	.test_cases = mctp_test_cases,
};

kunit_test_suite(mctp_test_suite);
