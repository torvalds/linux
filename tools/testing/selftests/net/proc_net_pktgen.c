// SPDX-License-Identifier: GPL-2.0
/*
 * proc_net_pktgen: kselftest for /proc/net/pktgen interface
 *
 * Copyright (c) 2025 Peter Seiderer <ps.report@gmx.net>
 *
 */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "../kselftest_harness.h"

static const char ctrl_cmd_stop[] = "stop";
static const char ctrl_cmd_start[] = "start";
static const char ctrl_cmd_reset[] = "reset";

static const char wrong_ctrl_cmd[] = "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789";

static const char thr_cmd_add_loopback_0[] = "add_device lo@0";
static const char thr_cmd_rm_loopback_0[] = "rem_device_all";

static const char wrong_thr_cmd[] = "forsureawrongcommand";
static const char legacy_thr_cmd[] = "max_before_softirq";

static const char wrong_dev_cmd[] = "forsurewrongcommand";
static const char dev_cmd_min_pkt_size_0[] = "min_pkt_size";
static const char dev_cmd_min_pkt_size_1[] = "min_pkt_size ";
static const char dev_cmd_min_pkt_size_2[] = "min_pkt_size 0";
static const char dev_cmd_min_pkt_size_3[] = "min_pkt_size 1";
static const char dev_cmd_min_pkt_size_4[] = "min_pkt_size 100";
static const char dev_cmd_min_pkt_size_5[] = "min_pkt_size=1001";
static const char dev_cmd_min_pkt_size_6[] = "min_pkt_size =2002";
static const char dev_cmd_min_pkt_size_7[] = "min_pkt_size= 3003";
static const char dev_cmd_min_pkt_size_8[] = "min_pkt_size = 4004";
static const char dev_cmd_max_pkt_size_0[] = "max_pkt_size 200";
static const char dev_cmd_pkt_size_0[] = "pkt_size 300";
static const char dev_cmd_imix_weights_0[] = "imix_weights 0,7 576,4 1500,1";
static const char dev_cmd_imix_weights_1[] = "imix_weights 101,1 102,2 103,3 104,4 105,5 106,6 107,7 108,8 109,9 110,10 111,11 112,12 113,13 114,14 115,15 116,16 117,17 118,18 119,19 120,20";
static const char dev_cmd_imix_weights_2[] = "imix_weights 100,1 102,2 103,3 104,4 105,5 106,6 107,7 108,8 109,9 110,10 111,11 112,12 113,13 114,14 115,15 116,16 117,17 118,18 119,19 120,20 121,21";
static const char dev_cmd_imix_weights_3[] = "imix_weights";
static const char dev_cmd_imix_weights_4[] = "imix_weights ";
static const char dev_cmd_imix_weights_5[] = "imix_weights 0";
static const char dev_cmd_imix_weights_6[] = "imix_weights 0,";
static const char dev_cmd_debug_0[] = "debug 1";
static const char dev_cmd_debug_1[] = "debug 0";
static const char dev_cmd_frags_0[] = "frags 100";
static const char dev_cmd_delay_0[] = "delay 100";
static const char dev_cmd_delay_1[] = "delay 2147483647";
static const char dev_cmd_rate_0[] = "rate 0";
static const char dev_cmd_rate_1[] = "rate 100";
static const char dev_cmd_ratep_0[] = "ratep 0";
static const char dev_cmd_ratep_1[] = "ratep 200";
static const char dev_cmd_udp_src_min_0[] = "udp_src_min 1";
static const char dev_cmd_udp_dst_min_0[] = "udp_dst_min 2";
static const char dev_cmd_udp_src_max_0[] = "udp_src_max 3";
static const char dev_cmd_udp_dst_max_0[] = "udp_dst_max 4";
static const char dev_cmd_clone_skb_0[] = "clone_skb 1";
static const char dev_cmd_clone_skb_1[] = "clone_skb 0";
static const char dev_cmd_count_0[] = "count 100";
static const char dev_cmd_src_mac_count_0[] = "src_mac_count 100";
static const char dev_cmd_dst_mac_count_0[] = "dst_mac_count 100";
static const char dev_cmd_burst_0[] = "burst 0";
static const char dev_cmd_node_0[] = "node 100";
static const char dev_cmd_xmit_mode_0[] = "xmit_mode start_xmit";
static const char dev_cmd_xmit_mode_1[] = "xmit_mode netif_receive";
static const char dev_cmd_xmit_mode_2[] = "xmit_mode queue_xmit";
static const char dev_cmd_xmit_mode_3[] = "xmit_mode nonsense";
static const char dev_cmd_flag_0[] = "flag UDPCSUM";
static const char dev_cmd_flag_1[] = "flag !UDPCSUM";
static const char dev_cmd_flag_2[] = "flag nonsense";
static const char dev_cmd_dst_min_0[] = "dst_min 101.102.103.104";
static const char dev_cmd_dst_0[] = "dst 101.102.103.104";
static const char dev_cmd_dst_max_0[] = "dst_max 201.202.203.204";
static const char dev_cmd_dst6_0[] = "dst6 2001:db38:1234:0000:0000:0000:0000:0000";
static const char dev_cmd_dst6_min_0[] = "dst6_min 2001:db8:1234:0000:0000:0000:0000:0000";
static const char dev_cmd_dst6_max_0[] = "dst6_max 2001:db8:1234:0000:0000:0000:0000:0000";
static const char dev_cmd_src6_0[] = "src6 2001:db38:1234:0000:0000:0000:0000:0000";
static const char dev_cmd_src_min_0[] = "src_min 101.102.103.104";
static const char dev_cmd_src_max_0[] = "src_max 201.202.203.204";
static const char dev_cmd_dst_mac_0[] = "dst_mac 01:02:03:04:05:06";
static const char dev_cmd_src_mac_0[] = "src_mac 11:12:13:14:15:16";
static const char dev_cmd_clear_counters_0[] = "clear_counters";
static const char dev_cmd_flows_0[] = "flows 100";
static const char dev_cmd_spi_0[] = "spi 100";
static const char dev_cmd_flowlen_0[] = "flowlen 100";
static const char dev_cmd_queue_map_min_0[] = "queue_map_min 1";
static const char dev_cmd_queue_map_max_0[] = "queue_map_max 2";
static const char dev_cmd_mpls_0[] = "mpls 00000001";
static const char dev_cmd_mpls_1[] = "mpls 00000001,000000f2";
static const char dev_cmd_mpls_2[] = "mpls 00000f00,00000f01,00000f02,00000f03,00000f04,00000f05,00000f06,00000f07,00000f08,00000f09,00000f0a,00000f0b,00000f0c,00000f0d,00000f0e,00000f0f";
static const char dev_cmd_mpls_3[] = "mpls 00000f00,00000f01,00000f02,00000f03,00000f04,00000f05,00000f06,00000f07,00000f08,00000f09,00000f0a,00000f0b,00000f0c,00000f0d,00000f0e,00000f0f,00000f10";
static const char dev_cmd_vlan_id_0[] = "vlan_id 1";
static const char dev_cmd_vlan_p_0[] = "vlan_p 1";
static const char dev_cmd_vlan_cfi_0[] = "vlan_cfi 1";
static const char dev_cmd_vlan_id_1[] = "vlan_id 4096";
static const char dev_cmd_svlan_id_0[] = "svlan_id 1";
static const char dev_cmd_svlan_p_0[] = "svlan_p 1";
static const char dev_cmd_svlan_cfi_0[] = "svlan_cfi 1";
static const char dev_cmd_svlan_id_1[] = "svlan_id 4096";
static const char dev_cmd_tos_0[] = "tos 0";
static const char dev_cmd_tos_1[] = "tos 0f";
static const char dev_cmd_tos_2[] = "tos 0ff";
static const char dev_cmd_traffic_class_0[] = "traffic_class f0";
static const char dev_cmd_skb_priority_0[] = "skb_priority 999";

FIXTURE(proc_net_pktgen) {
	int ctrl_fd;
	int thr_fd;
	int dev_fd;
};

FIXTURE_SETUP(proc_net_pktgen) {
	int r;
	ssize_t len;

	r = system("modprobe pktgen");
	ASSERT_EQ(r, 0) TH_LOG("CONFIG_NET_PKTGEN not enabled, module pktgen not loaded?");

	self->ctrl_fd = open("/proc/net/pktgen/pgctrl", O_RDWR);
	ASSERT_GE(self->ctrl_fd, 0) TH_LOG("CONFIG_NET_PKTGEN not enabled, module pktgen not loaded?");

	self->thr_fd = open("/proc/net/pktgen/kpktgend_0", O_RDWR);
	ASSERT_GE(self->thr_fd, 0) TH_LOG("CONFIG_NET_PKTGEN not enabled, module pktgen not loaded?");

	len = write(self->thr_fd, thr_cmd_add_loopback_0, sizeof(thr_cmd_add_loopback_0));
	ASSERT_EQ(len, sizeof(thr_cmd_add_loopback_0)) TH_LOG("device lo@0 already registered?");

	self->dev_fd = open("/proc/net/pktgen/lo@0", O_RDWR);
	ASSERT_GE(self->dev_fd, 0) TH_LOG("device entry for lo@0 missing?");
}

FIXTURE_TEARDOWN(proc_net_pktgen) {
	int ret;
	ssize_t len;

	ret = close(self->dev_fd);
	EXPECT_EQ(ret, 0);

	len = write(self->thr_fd, thr_cmd_rm_loopback_0, sizeof(thr_cmd_rm_loopback_0));
	EXPECT_EQ(len, sizeof(thr_cmd_rm_loopback_0));

	ret = close(self->thr_fd);
	EXPECT_EQ(ret, 0);

	ret = close(self->ctrl_fd);
	EXPECT_EQ(ret, 0);
}

TEST_F(proc_net_pktgen, wrong_ctrl_cmd) {
	for (int i = 0; i <= sizeof(wrong_ctrl_cmd); i++) {
		ssize_t len;

		len = write(self->ctrl_fd, wrong_ctrl_cmd, i);
		EXPECT_EQ(len, -1);
		EXPECT_EQ(errno, EINVAL);
	}
}

TEST_F(proc_net_pktgen, ctrl_cmd) {
	ssize_t len;

	len = write(self->ctrl_fd, ctrl_cmd_stop, sizeof(ctrl_cmd_stop));
	EXPECT_EQ(len,	sizeof(ctrl_cmd_stop));

	len = write(self->ctrl_fd, ctrl_cmd_stop, sizeof(ctrl_cmd_stop) - 1);
	EXPECT_EQ(len,	sizeof(ctrl_cmd_stop) - 1);

	len = write(self->ctrl_fd, ctrl_cmd_start, sizeof(ctrl_cmd_start));
	EXPECT_EQ(len,	sizeof(ctrl_cmd_start));

	len = write(self->ctrl_fd, ctrl_cmd_start, sizeof(ctrl_cmd_start) - 1);
	EXPECT_EQ(len,	sizeof(ctrl_cmd_start) - 1);

	len = write(self->ctrl_fd, ctrl_cmd_reset, sizeof(ctrl_cmd_reset));
	EXPECT_EQ(len,	sizeof(ctrl_cmd_reset));

	len = write(self->ctrl_fd, ctrl_cmd_reset, sizeof(ctrl_cmd_reset) - 1);
	EXPECT_EQ(len,	sizeof(ctrl_cmd_reset) - 1);
}

TEST_F(proc_net_pktgen, wrong_thr_cmd) {
	for (int i = 0; i <= sizeof(wrong_thr_cmd); i++) {
		ssize_t len;

		len = write(self->thr_fd, wrong_thr_cmd, i);
		EXPECT_EQ(len, -1);
		EXPECT_EQ(errno, EINVAL);
	}
}

TEST_F(proc_net_pktgen, legacy_thr_cmd) {
	for (int i = 0; i <= sizeof(legacy_thr_cmd); i++) {
		ssize_t len;

		len = write(self->thr_fd, legacy_thr_cmd, i);
		if (i < (sizeof(legacy_thr_cmd) - 1)) {
			/* incomplete command string */
			EXPECT_EQ(len, -1);
			EXPECT_EQ(errno, EINVAL);
		} else {
			/* complete command string without/with trailing '\0' */
			EXPECT_EQ(len, i);
		}
	}
}

TEST_F(proc_net_pktgen, wrong_dev_cmd) {
	for (int i = 0; i <= sizeof(wrong_dev_cmd); i++) {
		ssize_t len;

		len = write(self->dev_fd, wrong_dev_cmd, i);
		EXPECT_EQ(len, -1);
		EXPECT_EQ(errno, EINVAL);
	}
}

TEST_F(proc_net_pktgen, dev_cmd_min_pkt_size) {
	ssize_t len;

	/* with trailing '\0' */
	len = write(self->dev_fd, dev_cmd_min_pkt_size_0, sizeof(dev_cmd_min_pkt_size_0));
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_0));

	/* without trailing '\0' */
	len = write(self->dev_fd, dev_cmd_min_pkt_size_0, sizeof(dev_cmd_min_pkt_size_0) - 1);
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_0) - 1);

	/* with trailing '\0' */
	len = write(self->dev_fd, dev_cmd_min_pkt_size_1, sizeof(dev_cmd_min_pkt_size_1));
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_1));

	/* without trailing '\0' */
	len = write(self->dev_fd, dev_cmd_min_pkt_size_1, sizeof(dev_cmd_min_pkt_size_1) - 1);
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_1) - 1);

	/* with trailing '\0' */
	len = write(self->dev_fd, dev_cmd_min_pkt_size_2, sizeof(dev_cmd_min_pkt_size_2));
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_2));

	/* without trailing '\0' */
	len = write(self->dev_fd, dev_cmd_min_pkt_size_2, sizeof(dev_cmd_min_pkt_size_2) - 1);
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_2) - 1);

	len = write(self->dev_fd, dev_cmd_min_pkt_size_3, sizeof(dev_cmd_min_pkt_size_3));
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_3));

	len = write(self->dev_fd, dev_cmd_min_pkt_size_4, sizeof(dev_cmd_min_pkt_size_4));
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_4));

	len = write(self->dev_fd, dev_cmd_min_pkt_size_5, sizeof(dev_cmd_min_pkt_size_5));
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_5));

	len = write(self->dev_fd, dev_cmd_min_pkt_size_6, sizeof(dev_cmd_min_pkt_size_6));
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_6));

	len = write(self->dev_fd, dev_cmd_min_pkt_size_7, sizeof(dev_cmd_min_pkt_size_7));
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_7));

	len = write(self->dev_fd, dev_cmd_min_pkt_size_8, sizeof(dev_cmd_min_pkt_size_8));
	EXPECT_EQ(len, sizeof(dev_cmd_min_pkt_size_8));
}

TEST_F(proc_net_pktgen, dev_cmd_max_pkt_size) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_max_pkt_size_0, sizeof(dev_cmd_max_pkt_size_0));
	EXPECT_EQ(len, sizeof(dev_cmd_max_pkt_size_0));
}

TEST_F(proc_net_pktgen, dev_cmd_pkt_size) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_pkt_size_0, sizeof(dev_cmd_pkt_size_0));
	EXPECT_EQ(len, sizeof(dev_cmd_pkt_size_0));
}

TEST_F(proc_net_pktgen, dev_cmd_imix_weights) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_imix_weights_0, sizeof(dev_cmd_imix_weights_0));
	EXPECT_EQ(len, sizeof(dev_cmd_imix_weights_0));

	len = write(self->dev_fd, dev_cmd_imix_weights_1, sizeof(dev_cmd_imix_weights_1));
	EXPECT_EQ(len, sizeof(dev_cmd_imix_weights_1));

	len = write(self->dev_fd, dev_cmd_imix_weights_2, sizeof(dev_cmd_imix_weights_2));
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, E2BIG);

	/* with trailing '\0' */
	len = write(self->dev_fd, dev_cmd_imix_weights_3, sizeof(dev_cmd_imix_weights_3));
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, EINVAL);

	/* without trailing '\0' */
	len = write(self->dev_fd, dev_cmd_imix_weights_3, sizeof(dev_cmd_imix_weights_3) - 1);
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, EINVAL);

	/* with trailing '\0' */
	len = write(self->dev_fd, dev_cmd_imix_weights_4, sizeof(dev_cmd_imix_weights_4));
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, EINVAL);

	/* without trailing '\0' */
	len = write(self->dev_fd, dev_cmd_imix_weights_4, sizeof(dev_cmd_imix_weights_4) - 1);
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, EINVAL);

	/* with trailing '\0' */
	len = write(self->dev_fd, dev_cmd_imix_weights_5, sizeof(dev_cmd_imix_weights_5));
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, EINVAL);

	/* without trailing '\0' */
	len = write(self->dev_fd, dev_cmd_imix_weights_5, sizeof(dev_cmd_imix_weights_5) - 1);
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, EINVAL);

	/* with trailing '\0' */
	len = write(self->dev_fd, dev_cmd_imix_weights_6, sizeof(dev_cmd_imix_weights_6));
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, EINVAL);

	/* without trailing '\0' */
	len = write(self->dev_fd, dev_cmd_imix_weights_6, sizeof(dev_cmd_imix_weights_6) - 1);
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, EINVAL);
}

TEST_F(proc_net_pktgen, dev_cmd_debug) {
	ssize_t len;

	/* debug on */
	len = write(self->dev_fd, dev_cmd_debug_0, sizeof(dev_cmd_debug_0));
	EXPECT_EQ(len, sizeof(dev_cmd_debug_0));

	/* debug off */
	len = write(self->dev_fd, dev_cmd_debug_1, sizeof(dev_cmd_debug_1));
	EXPECT_EQ(len, sizeof(dev_cmd_debug_1));
}

TEST_F(proc_net_pktgen, dev_cmd_frags) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_frags_0, sizeof(dev_cmd_frags_0));
	EXPECT_EQ(len, sizeof(dev_cmd_frags_0));
}

TEST_F(proc_net_pktgen, dev_cmd_delay) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_delay_0, sizeof(dev_cmd_delay_0));
	EXPECT_EQ(len, sizeof(dev_cmd_delay_0));

	len = write(self->dev_fd, dev_cmd_delay_1, sizeof(dev_cmd_delay_1));
	EXPECT_EQ(len, sizeof(dev_cmd_delay_1));
}

TEST_F(proc_net_pktgen, dev_cmd_rate) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_rate_0, sizeof(dev_cmd_rate_0));
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, EINVAL);

	len = write(self->dev_fd, dev_cmd_rate_1, sizeof(dev_cmd_rate_1));
	EXPECT_EQ(len, sizeof(dev_cmd_rate_1));
}

TEST_F(proc_net_pktgen, dev_cmd_ratep) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_ratep_0, sizeof(dev_cmd_ratep_0));
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, EINVAL);

	len = write(self->dev_fd, dev_cmd_ratep_1, sizeof(dev_cmd_ratep_1));
	EXPECT_EQ(len, sizeof(dev_cmd_ratep_1));
}

TEST_F(proc_net_pktgen, dev_cmd_udp_src_min) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_udp_src_min_0, sizeof(dev_cmd_udp_src_min_0));
	EXPECT_EQ(len, sizeof(dev_cmd_udp_src_min_0));
}

TEST_F(proc_net_pktgen, dev_cmd_udp_dst_min) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_udp_dst_min_0, sizeof(dev_cmd_udp_dst_min_0));
	EXPECT_EQ(len, sizeof(dev_cmd_udp_dst_min_0));
}

TEST_F(proc_net_pktgen, dev_cmd_udp_src_max) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_udp_src_max_0, sizeof(dev_cmd_udp_src_max_0));
	EXPECT_EQ(len, sizeof(dev_cmd_udp_src_max_0));
}

TEST_F(proc_net_pktgen, dev_cmd_udp_dst_max) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_udp_dst_max_0, sizeof(dev_cmd_udp_dst_max_0));
	EXPECT_EQ(len, sizeof(dev_cmd_udp_dst_max_0));
}

TEST_F(proc_net_pktgen, dev_cmd_clone_skb) {
	ssize_t len;

	/* clone_skb on (gives EOPNOTSUPP on lo device) */
	len = write(self->dev_fd, dev_cmd_clone_skb_0, sizeof(dev_cmd_clone_skb_0));
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, EOPNOTSUPP);

	/* clone_skb off */
	len = write(self->dev_fd, dev_cmd_clone_skb_1, sizeof(dev_cmd_clone_skb_1));
	EXPECT_EQ(len, sizeof(dev_cmd_clone_skb_1));
}

TEST_F(proc_net_pktgen, dev_cmd_count) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_count_0, sizeof(dev_cmd_count_0));
	EXPECT_EQ(len, sizeof(dev_cmd_count_0));
}

TEST_F(proc_net_pktgen, dev_cmd_src_mac_count) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_src_mac_count_0, sizeof(dev_cmd_src_mac_count_0));
	EXPECT_EQ(len, sizeof(dev_cmd_src_mac_count_0));
}

TEST_F(proc_net_pktgen, dev_cmd_dst_mac_count) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_dst_mac_count_0, sizeof(dev_cmd_dst_mac_count_0));
	EXPECT_EQ(len, sizeof(dev_cmd_dst_mac_count_0));
}

TEST_F(proc_net_pktgen, dev_cmd_burst) {
	ssize_t len;

	/* burst off */
	len = write(self->dev_fd, dev_cmd_burst_0, sizeof(dev_cmd_burst_0));
	EXPECT_EQ(len, sizeof(dev_cmd_burst_0));
}

TEST_F(proc_net_pktgen, dev_cmd_node) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_node_0, sizeof(dev_cmd_node_0));
	EXPECT_EQ(len, sizeof(dev_cmd_node_0));
}

TEST_F(proc_net_pktgen, dev_cmd_xmit_mode) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_xmit_mode_0, sizeof(dev_cmd_xmit_mode_0));
	EXPECT_EQ(len, sizeof(dev_cmd_xmit_mode_0));

	len = write(self->dev_fd, dev_cmd_xmit_mode_1, sizeof(dev_cmd_xmit_mode_1));
	EXPECT_EQ(len, sizeof(dev_cmd_xmit_mode_1));

	len = write(self->dev_fd, dev_cmd_xmit_mode_2, sizeof(dev_cmd_xmit_mode_2));
	EXPECT_EQ(len, sizeof(dev_cmd_xmit_mode_2));

	len = write(self->dev_fd, dev_cmd_xmit_mode_3, sizeof(dev_cmd_xmit_mode_3));
	EXPECT_EQ(len, sizeof(dev_cmd_xmit_mode_3));
}

TEST_F(proc_net_pktgen, dev_cmd_flag) {
	ssize_t len;

	/* flag UDPCSUM on */
	len = write(self->dev_fd, dev_cmd_flag_0, sizeof(dev_cmd_flag_0));
	EXPECT_EQ(len, sizeof(dev_cmd_flag_0));

	/* flag UDPCSUM off */
	len = write(self->dev_fd, dev_cmd_flag_1, sizeof(dev_cmd_flag_1));
	EXPECT_EQ(len, sizeof(dev_cmd_flag_1));

	/* flag invalid */
	len = write(self->dev_fd, dev_cmd_flag_2, sizeof(dev_cmd_flag_2));
	EXPECT_EQ(len, sizeof(dev_cmd_flag_2));
}

TEST_F(proc_net_pktgen, dev_cmd_dst_min) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_dst_min_0, sizeof(dev_cmd_dst_min_0));
	EXPECT_EQ(len, sizeof(dev_cmd_dst_min_0));
}

TEST_F(proc_net_pktgen, dev_cmd_dst) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_dst_0, sizeof(dev_cmd_dst_0));
	EXPECT_EQ(len, sizeof(dev_cmd_dst_0));
}

TEST_F(proc_net_pktgen, dev_cmd_dst_max) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_dst_max_0, sizeof(dev_cmd_dst_max_0));
	EXPECT_EQ(len, sizeof(dev_cmd_dst_max_0));
}

TEST_F(proc_net_pktgen, dev_cmd_dst6) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_dst6_0, sizeof(dev_cmd_dst6_0));
	EXPECT_EQ(len, sizeof(dev_cmd_dst6_0));
}

TEST_F(proc_net_pktgen, dev_cmd_dst6_min) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_dst6_min_0, sizeof(dev_cmd_dst6_min_0));
	EXPECT_EQ(len, sizeof(dev_cmd_dst6_min_0));
}

TEST_F(proc_net_pktgen, dev_cmd_dst6_max) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_dst6_max_0, sizeof(dev_cmd_dst6_max_0));
	EXPECT_EQ(len, sizeof(dev_cmd_dst6_max_0));
}

TEST_F(proc_net_pktgen, dev_cmd_src6) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_src6_0, sizeof(dev_cmd_src6_0));
	EXPECT_EQ(len, sizeof(dev_cmd_src6_0));
}

TEST_F(proc_net_pktgen, dev_cmd_src_min) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_src_min_0, sizeof(dev_cmd_src_min_0));
	EXPECT_EQ(len, sizeof(dev_cmd_src_min_0));
}

TEST_F(proc_net_pktgen, dev_cmd_src_max) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_src_max_0, sizeof(dev_cmd_src_max_0));
	EXPECT_EQ(len, sizeof(dev_cmd_src_max_0));
}

TEST_F(proc_net_pktgen, dev_cmd_dst_mac) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_dst_mac_0, sizeof(dev_cmd_dst_mac_0));
	EXPECT_EQ(len, sizeof(dev_cmd_dst_mac_0));
}

TEST_F(proc_net_pktgen, dev_cmd_src_mac) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_src_mac_0, sizeof(dev_cmd_src_mac_0));
	EXPECT_EQ(len, sizeof(dev_cmd_src_mac_0));
}

TEST_F(proc_net_pktgen, dev_cmd_clear_counters) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_clear_counters_0, sizeof(dev_cmd_clear_counters_0));
	EXPECT_EQ(len, sizeof(dev_cmd_clear_counters_0));
}

TEST_F(proc_net_pktgen, dev_cmd_flows) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_flows_0, sizeof(dev_cmd_flows_0));
	EXPECT_EQ(len, sizeof(dev_cmd_flows_0));
}

TEST_F(proc_net_pktgen, dev_cmd_spi) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_spi_0, sizeof(dev_cmd_spi_0));
	EXPECT_EQ(len, sizeof(dev_cmd_spi_0)) TH_LOG("CONFIG_XFRM not enabled?");
}

TEST_F(proc_net_pktgen, dev_cmd_flowlen) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_flowlen_0, sizeof(dev_cmd_flowlen_0));
	EXPECT_EQ(len, sizeof(dev_cmd_flowlen_0));
}

TEST_F(proc_net_pktgen, dev_cmd_queue_map_min) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_queue_map_min_0, sizeof(dev_cmd_queue_map_min_0));
	EXPECT_EQ(len, sizeof(dev_cmd_queue_map_min_0));
}

TEST_F(proc_net_pktgen, dev_cmd_queue_map_max) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_queue_map_max_0, sizeof(dev_cmd_queue_map_max_0));
	EXPECT_EQ(len, sizeof(dev_cmd_queue_map_max_0));
}

TEST_F(proc_net_pktgen, dev_cmd_mpls) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_mpls_0, sizeof(dev_cmd_mpls_0));
	EXPECT_EQ(len, sizeof(dev_cmd_mpls_0));

	len = write(self->dev_fd, dev_cmd_mpls_1, sizeof(dev_cmd_mpls_1));
	EXPECT_EQ(len, sizeof(dev_cmd_mpls_1));

	len = write(self->dev_fd, dev_cmd_mpls_2, sizeof(dev_cmd_mpls_2));
	EXPECT_EQ(len, sizeof(dev_cmd_mpls_2));

	len = write(self->dev_fd, dev_cmd_mpls_3, sizeof(dev_cmd_mpls_3));
	EXPECT_EQ(len, -1);
	EXPECT_EQ(errno, E2BIG);
}

TEST_F(proc_net_pktgen, dev_cmd_vlan_id) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_vlan_id_0, sizeof(dev_cmd_vlan_id_0));
	EXPECT_EQ(len, sizeof(dev_cmd_vlan_id_0));

	len = write(self->dev_fd, dev_cmd_vlan_p_0, sizeof(dev_cmd_vlan_p_0));
	EXPECT_EQ(len, sizeof(dev_cmd_vlan_p_0));

	len = write(self->dev_fd, dev_cmd_vlan_cfi_0, sizeof(dev_cmd_vlan_cfi_0));
	EXPECT_EQ(len, sizeof(dev_cmd_vlan_cfi_0));

	len = write(self->dev_fd, dev_cmd_vlan_id_1, sizeof(dev_cmd_vlan_id_1));
	EXPECT_EQ(len, sizeof(dev_cmd_vlan_id_1));
}

TEST_F(proc_net_pktgen, dev_cmd_svlan_id) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_svlan_id_0, sizeof(dev_cmd_svlan_id_0));
	EXPECT_EQ(len, sizeof(dev_cmd_svlan_id_0));

	len = write(self->dev_fd, dev_cmd_svlan_p_0, sizeof(dev_cmd_svlan_p_0));
	EXPECT_EQ(len, sizeof(dev_cmd_svlan_p_0));

	len = write(self->dev_fd, dev_cmd_svlan_cfi_0, sizeof(dev_cmd_svlan_cfi_0));
	EXPECT_EQ(len, sizeof(dev_cmd_svlan_cfi_0));

	len = write(self->dev_fd, dev_cmd_svlan_id_1, sizeof(dev_cmd_svlan_id_1));
	EXPECT_EQ(len, sizeof(dev_cmd_svlan_id_1));
}


TEST_F(proc_net_pktgen, dev_cmd_tos) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_tos_0, sizeof(dev_cmd_tos_0));
	EXPECT_EQ(len, sizeof(dev_cmd_tos_0));

	len = write(self->dev_fd, dev_cmd_tos_1, sizeof(dev_cmd_tos_1));
	EXPECT_EQ(len, sizeof(dev_cmd_tos_1));

	len = write(self->dev_fd, dev_cmd_tos_2, sizeof(dev_cmd_tos_2));
	EXPECT_EQ(len, sizeof(dev_cmd_tos_2));
}


TEST_F(proc_net_pktgen, dev_cmd_traffic_class) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_traffic_class_0, sizeof(dev_cmd_traffic_class_0));
	EXPECT_EQ(len, sizeof(dev_cmd_traffic_class_0));
}

TEST_F(proc_net_pktgen, dev_cmd_skb_priority) {
	ssize_t len;

	len = write(self->dev_fd, dev_cmd_skb_priority_0, sizeof(dev_cmd_skb_priority_0));
	EXPECT_EQ(len, sizeof(dev_cmd_skb_priority_0));
}

TEST_HARNESS_MAIN
