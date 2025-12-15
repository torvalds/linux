// SPDX-License-Identifier: GPL-2.0
#include <net/if.h>
#include <stdarg.h>

#include "network_helpers.h"
#include "test_progs.h"
#include "test_xsk.h"
#include "xsk_xdp_progs.skel.h"

#define VETH_RX "veth0"
#define VETH_TX "veth1"
#define MTU	1500

int setup_veth(bool busy_poll)
{
	SYS(fail,
	"ip link add %s numtxqueues 4 numrxqueues 4 type veth peer name %s numtxqueues 4 numrxqueues 4",
	VETH_RX, VETH_TX);
	SYS(fail, "sysctl -wq net.ipv6.conf.%s.disable_ipv6=1", VETH_RX);
	SYS(fail, "sysctl -wq net.ipv6.conf.%s.disable_ipv6=1", VETH_TX);

	if (busy_poll) {
		SYS(fail, "echo 2 > /sys/class/net/%s/napi_defer_hard_irqs", VETH_RX);
		SYS(fail, "echo 200000 > /sys/class/net/%s/gro_flush_timeout", VETH_RX);
		SYS(fail, "echo 2 > /sys/class/net/%s/napi_defer_hard_irqs", VETH_TX);
		SYS(fail, "echo 200000 > /sys/class/net/%s/gro_flush_timeout", VETH_TX);
	}

	SYS(fail, "ip link set %s mtu %d", VETH_RX, MTU);
	SYS(fail, "ip link set %s mtu %d", VETH_TX, MTU);
	SYS(fail, "ip link set %s up", VETH_RX);
	SYS(fail, "ip link set %s up", VETH_TX);

	return 0;

fail:
	return -1;
}

void delete_veth(void)
{
	SYS_NOFAIL("ip link del %s", VETH_RX);
	SYS_NOFAIL("ip link del %s", VETH_TX);
}

int configure_ifobj(struct ifobject *tx, struct ifobject *rx)
{
	rx->ifindex = if_nametoindex(VETH_RX);
	if (!ASSERT_OK_FD(rx->ifindex, "get RX ifindex"))
		return -1;

	tx->ifindex = if_nametoindex(VETH_TX);
	if (!ASSERT_OK_FD(tx->ifindex, "get TX ifindex"))
		return -1;

	tx->shared_umem = false;
	rx->shared_umem = false;


	return 0;
}

static void test_xsk(const struct test_spec *test_to_run, enum test_mode mode)
{
	struct ifobject *ifobj_tx, *ifobj_rx;
	struct test_spec test;
	int ret;

	ifobj_tx = ifobject_create();
	if (!ASSERT_OK_PTR(ifobj_tx, "create ifobj_tx"))
		return;

	ifobj_rx = ifobject_create();
	if (!ASSERT_OK_PTR(ifobj_rx, "create ifobj_rx"))
		goto delete_tx;

	if (!ASSERT_OK(configure_ifobj(ifobj_tx, ifobj_rx), "conigure ifobj"))
		goto delete_rx;

	ret = get_hw_ring_size(ifobj_tx->ifname, &ifobj_tx->ring);
	if (!ret) {
		ifobj_tx->hw_ring_size_supp = true;
		ifobj_tx->set_ring.default_tx = ifobj_tx->ring.tx_pending;
		ifobj_tx->set_ring.default_rx = ifobj_tx->ring.rx_pending;
	}

	if (!ASSERT_OK(init_iface(ifobj_rx, worker_testapp_validate_rx), "init RX"))
		goto delete_rx;
	if (!ASSERT_OK(init_iface(ifobj_tx, worker_testapp_validate_tx), "init TX"))
		goto delete_rx;

	test_init(&test, ifobj_tx, ifobj_rx, 0, &tests[0]);

	test.tx_pkt_stream_default = pkt_stream_generate(DEFAULT_PKT_CNT, MIN_PKT_SIZE);
	if (!ASSERT_OK_PTR(test.tx_pkt_stream_default, "TX pkt generation"))
		goto delete_rx;
	test.rx_pkt_stream_default = pkt_stream_generate(DEFAULT_PKT_CNT, MIN_PKT_SIZE);
	if (!ASSERT_OK_PTR(test.rx_pkt_stream_default, "RX pkt generation"))
		goto delete_rx;


	test_init(&test, ifobj_tx, ifobj_rx, mode, test_to_run);
	ret = test.test_func(&test);
	if (ret != TEST_SKIP)
		ASSERT_OK(ret, "Run test");
	pkt_stream_restore_default(&test);

	if (ifobj_tx->hw_ring_size_supp)
		hw_ring_size_reset(ifobj_tx);

	pkt_stream_delete(test.tx_pkt_stream_default);
	pkt_stream_delete(test.rx_pkt_stream_default);
	xsk_xdp_progs__destroy(ifobj_tx->xdp_progs);
	xsk_xdp_progs__destroy(ifobj_rx->xdp_progs);

delete_rx:
	ifobject_delete(ifobj_rx);
delete_tx:
	ifobject_delete(ifobj_tx);
}

void test_ns_xsk_skb(void)
{
	int i;

	if (!ASSERT_OK(setup_veth(false), "setup veth"))
		return;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (test__start_subtest(tests[i].name))
			test_xsk(&tests[i], TEST_MODE_SKB);
	}

	delete_veth();
}

void test_ns_xsk_drv(void)
{
	int i;

	if (!ASSERT_OK(setup_veth(false), "setup veth"))
		return;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (test__start_subtest(tests[i].name))
			test_xsk(&tests[i], TEST_MODE_DRV);
	}

	delete_veth();
}

