// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include <net/if.h>

#include <kselftest_harness.h>

#include "ethtool-user.h"

FIXTURE(ethtool)
{
	struct ynl_sock *ys;
};

FIXTURE_SETUP(ethtool)
{
	self->ys = ynl_sock_create(&ynl_ethtool_family, NULL);
	ASSERT_NE(NULL, self->ys)
		TH_LOG("failed to create ethtool socket");
}

FIXTURE_TEARDOWN(ethtool)
{
	ynl_sock_destroy(self->ys);
}

TEST_F(ethtool, channels)
{
	struct ethtool_channels_get_req_dump creq = {};
	struct ethtool_channels_get_list *channels;

	creq._present.header = 1; /* ethtool needs an empty nest */
	channels = ethtool_channels_get_dump(self->ys, &creq);
	ASSERT_NE(NULL, channels) {
		TH_LOG("channels dump failed: %s", self->ys->err.msg);
	}

	if (ynl_dump_empty(channels)) {
		ethtool_channels_get_list_free(channels);
		SKIP(return, "no entries in channels dump");
	}

	ynl_dump_foreach(channels, dev) {
		EXPECT_TRUE((bool)dev->header._len.dev_name);
		ksft_print_msg("%8s: ", dev->header.dev_name);
		EXPECT_TRUE(dev->_present.rx_count ||
			    dev->_present.tx_count ||
			    dev->_present.combined_count);
		if (dev->_present.rx_count)
			printf("rx %d ", dev->rx_count);
		if (dev->_present.tx_count)
			printf("tx %d ", dev->tx_count);
		if (dev->_present.combined_count)
			printf("combined %d ", dev->combined_count);
		printf("\n");
	}
	ethtool_channels_get_list_free(channels);
}

TEST_F(ethtool, rings)
{
	struct ethtool_rings_get_req_dump rreq = {};
	struct ethtool_rings_get_list *rings;

	rreq._present.header = 1; /* ethtool needs an empty nest */
	rings = ethtool_rings_get_dump(self->ys, &rreq);
	ASSERT_NE(NULL, rings) {
		TH_LOG("rings dump failed: %s", self->ys->err.msg);
	}

	if (ynl_dump_empty(rings)) {
		ethtool_rings_get_list_free(rings);
		SKIP(return, "no entries in rings dump");
	}

	ynl_dump_foreach(rings, dev) {
		EXPECT_TRUE((bool)dev->header._len.dev_name);
		ksft_print_msg("%8s: ", dev->header.dev_name);
		EXPECT_TRUE(dev->_present.rx || dev->_present.tx);
		if (dev->_present.rx)
			printf("rx %d ", dev->rx);
		if (dev->_present.tx)
			printf("tx %d ", dev->tx);
		printf("\n");
	}
	ethtool_rings_get_list_free(rings);
}

TEST_HARNESS_MAIN
