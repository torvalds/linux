// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/*
 * Copyright (c) 2011 Volkswagen Group Electronic Research
 * All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <linux/if.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "../../kselftest_harness.h"

#define ID 0x123

char CANIF[IFNAMSIZ];

static int send_can_frames(int sock, int testcase)
{
	struct can_frame frame;

	frame.can_dlc = 1;
	frame.data[0] = testcase;

	frame.can_id = ID;
	if (write(sock, &frame, sizeof(frame)) < 0)
		goto write_err;

	frame.can_id = (ID | CAN_RTR_FLAG);
	if (write(sock, &frame, sizeof(frame)) < 0)
		goto write_err;

	frame.can_id = (ID | CAN_EFF_FLAG);
	if (write(sock, &frame, sizeof(frame)) < 0)
		goto write_err;

	frame.can_id = (ID | CAN_EFF_FLAG | CAN_RTR_FLAG);
	if (write(sock, &frame, sizeof(frame)) < 0)
		goto write_err;

	return 0;

write_err:
	perror("write");
	return 1;
}

FIXTURE(can_filters) {
	int sock;
};

FIXTURE_SETUP(can_filters)
{
	struct sockaddr_can addr;
	struct ifreq ifr;
	int recv_own_msgs = 1;
	int s, ret;

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	ASSERT_GE(s, 0)
		TH_LOG("failed to create CAN_RAW socket: %d", errno);

	strncpy(ifr.ifr_name, CANIF, sizeof(ifr.ifr_name));
	ret = ioctl(s, SIOCGIFINDEX, &ifr);
	ASSERT_GE(ret, 0)
		TH_LOG("failed SIOCGIFINDEX: %d", errno);

	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;

	setsockopt(s, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
		   &recv_own_msgs, sizeof(recv_own_msgs));

	ret = bind(s, (struct sockaddr *)&addr, sizeof(addr));
	ASSERT_EQ(ret, 0)
		TH_LOG("failed bind socket: %d", errno);

	self->sock = s;
}

FIXTURE_TEARDOWN(can_filters)
{
	close(self->sock);
}

FIXTURE_VARIANT(can_filters) {
	int testcase;
	canid_t id;
	canid_t mask;
	int exp_num_rx;
	canid_t exp_flags[];
};

/* Receive all frames when filtering for the ID in standard frame format */
FIXTURE_VARIANT_ADD(can_filters, base) {
	.testcase = 1,
	.id = ID,
	.mask = CAN_SFF_MASK,
	.exp_num_rx = 4,
	.exp_flags = {
		0,
		CAN_RTR_FLAG,
		CAN_EFF_FLAG,
		CAN_EFF_FLAG | CAN_RTR_FLAG,
	},
};

/* Ignore EFF flag in filter ID if not covered by filter mask */
FIXTURE_VARIANT_ADD(can_filters, base_eff) {
	.testcase = 2,
	.id = ID | CAN_EFF_FLAG,
	.mask = CAN_SFF_MASK,
	.exp_num_rx = 4,
	.exp_flags = {
		0,
		CAN_RTR_FLAG,
		CAN_EFF_FLAG,
		CAN_EFF_FLAG | CAN_RTR_FLAG,
	},
};

/* Ignore RTR flag in filter ID if not covered by filter mask */
FIXTURE_VARIANT_ADD(can_filters, base_rtr) {
	.testcase = 3,
	.id = ID | CAN_RTR_FLAG,
	.mask = CAN_SFF_MASK,
	.exp_num_rx = 4,
	.exp_flags = {
		0,
		CAN_RTR_FLAG,
		CAN_EFF_FLAG,
		CAN_EFF_FLAG | CAN_RTR_FLAG,
	},
};

/* Ignore EFF and RTR flags in filter ID if not covered by filter mask */
FIXTURE_VARIANT_ADD(can_filters, base_effrtr) {
	.testcase = 4,
	.id = ID | CAN_EFF_FLAG | CAN_RTR_FLAG,
	.mask = CAN_SFF_MASK,
	.exp_num_rx = 4,
	.exp_flags = {
		0,
		CAN_RTR_FLAG,
		CAN_EFF_FLAG,
		CAN_EFF_FLAG | CAN_RTR_FLAG,
	},
};

/* Receive only SFF frames when expecting no EFF flag */
FIXTURE_VARIANT_ADD(can_filters, filter_eff) {
	.testcase = 5,
	.id = ID,
	.mask = CAN_SFF_MASK | CAN_EFF_FLAG,
	.exp_num_rx = 2,
	.exp_flags = {
		0,
		CAN_RTR_FLAG,
	},
};

/* Receive only EFF frames when filter id and filter mask include EFF flag */
FIXTURE_VARIANT_ADD(can_filters, filter_eff_eff) {
	.testcase = 6,
	.id = ID | CAN_EFF_FLAG,
	.mask = CAN_SFF_MASK | CAN_EFF_FLAG,
	.exp_num_rx = 2,
	.exp_flags = {
		CAN_EFF_FLAG,
		CAN_EFF_FLAG | CAN_RTR_FLAG,
	},
};

/* Receive only SFF frames when expecting no EFF flag, ignoring RTR flag */
FIXTURE_VARIANT_ADD(can_filters, filter_eff_rtr) {
	.testcase = 7,
	.id = ID | CAN_RTR_FLAG,
	.mask = CAN_SFF_MASK | CAN_EFF_FLAG,
	.exp_num_rx = 2,
	.exp_flags = {
		0,
		CAN_RTR_FLAG,
	},
};

/* Receive only EFF frames when filter id and filter mask include EFF flag,
 * ignoring RTR flag
 */
FIXTURE_VARIANT_ADD(can_filters, filter_eff_effrtr) {
	.testcase = 8,
	.id = ID | CAN_EFF_FLAG | CAN_RTR_FLAG,
	.mask = CAN_SFF_MASK | CAN_EFF_FLAG,
	.exp_num_rx = 2,
	.exp_flags = {
		CAN_EFF_FLAG,
		CAN_EFF_FLAG | CAN_RTR_FLAG,
	},
};

/* Receive no remote frames when filtering for no RTR flag */
FIXTURE_VARIANT_ADD(can_filters, filter_rtr) {
	.testcase = 9,
	.id = ID,
	.mask = CAN_SFF_MASK | CAN_RTR_FLAG,
	.exp_num_rx = 2,
	.exp_flags = {
		0,
		CAN_EFF_FLAG,
	},
};

/* Receive no remote frames when filtering for no RTR flag, ignoring EFF flag */
FIXTURE_VARIANT_ADD(can_filters, filter_rtr_eff) {
	.testcase = 10,
	.id = ID | CAN_EFF_FLAG,
	.mask = CAN_SFF_MASK | CAN_RTR_FLAG,
	.exp_num_rx = 2,
	.exp_flags = {
		0,
		CAN_EFF_FLAG,
	},
};

/* Receive only remote frames when filter includes RTR flag */
FIXTURE_VARIANT_ADD(can_filters, filter_rtr_rtr) {
	.testcase = 11,
	.id = ID | CAN_RTR_FLAG,
	.mask = CAN_SFF_MASK | CAN_RTR_FLAG,
	.exp_num_rx = 2,
	.exp_flags = {
		CAN_RTR_FLAG,
		CAN_EFF_FLAG | CAN_RTR_FLAG,
	},
};

/* Receive only remote frames when filter includes RTR flag, ignoring EFF
 * flag
 */
FIXTURE_VARIANT_ADD(can_filters, filter_rtr_effrtr) {
	.testcase = 12,
	.id = ID | CAN_EFF_FLAG | CAN_RTR_FLAG,
	.mask = CAN_SFF_MASK | CAN_RTR_FLAG,
	.exp_num_rx = 2,
	.exp_flags = {
		CAN_RTR_FLAG,
		CAN_EFF_FLAG | CAN_RTR_FLAG,
	},
};

/* Receive only SFF data frame when filtering for no flags */
FIXTURE_VARIANT_ADD(can_filters, filter_effrtr) {
	.testcase = 13,
	.id = ID,
	.mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG,
	.exp_num_rx = 1,
	.exp_flags = {
		0,
	},
};

/* Receive only EFF data frame when filtering for EFF but no RTR flag */
FIXTURE_VARIANT_ADD(can_filters, filter_effrtr_eff) {
	.testcase = 14,
	.id = ID | CAN_EFF_FLAG,
	.mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG,
	.exp_num_rx = 1,
	.exp_flags = {
		CAN_EFF_FLAG,
	},
};

/* Receive only SFF remote frame when filtering for RTR but no EFF flag */
FIXTURE_VARIANT_ADD(can_filters, filter_effrtr_rtr) {
	.testcase = 15,
	.id = ID | CAN_RTR_FLAG,
	.mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG,
	.exp_num_rx = 1,
	.exp_flags = {
		CAN_RTR_FLAG,
	},
};

/* Receive only EFF remote frame when filtering for EFF and RTR flag */
FIXTURE_VARIANT_ADD(can_filters, filter_effrtr_effrtr) {
	.testcase = 16,
	.id = ID | CAN_EFF_FLAG | CAN_RTR_FLAG,
	.mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG,
	.exp_num_rx = 1,
	.exp_flags = {
		CAN_EFF_FLAG | CAN_RTR_FLAG,
	},
};

/* Receive only SFF data frame when filtering for no EFF flag and no RTR flag
 * but based on EFF mask
 */
FIXTURE_VARIANT_ADD(can_filters, eff) {
	.testcase = 17,
	.id = ID,
	.mask = CAN_EFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG,
	.exp_num_rx = 1,
	.exp_flags = {
		0,
	},
};

/* Receive only EFF data frame when filtering for EFF flag and no RTR flag but
 * based on EFF mask
 */
FIXTURE_VARIANT_ADD(can_filters, eff_eff) {
	.testcase = 18,
	.id = ID | CAN_EFF_FLAG,
	.mask = CAN_EFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG,
	.exp_num_rx = 1,
	.exp_flags = {
		CAN_EFF_FLAG,
	},
};

/* This test verifies that the raw CAN filters work, by checking if only frames
 * with the expected set of flags are received. For each test case, the given
 * filter (id and mask) is added and four CAN frames are sent with every
 * combination of set/unset EFF/RTR flags.
 */
TEST_F(can_filters, test_filter)
{
	struct can_filter rfilter;
	int ret;

	rfilter.can_id = variant->id;
	rfilter.can_mask = variant->mask;
	setsockopt(self->sock, SOL_CAN_RAW, CAN_RAW_FILTER,
		   &rfilter, sizeof(rfilter));

	TH_LOG("filters: can_id = 0x%08X can_mask = 0x%08X",
		rfilter.can_id, rfilter.can_mask);

	ret = send_can_frames(self->sock, variant->testcase);
	ASSERT_EQ(ret, 0)
		TH_LOG("failed to send CAN frames");

	for (int i = 0; i <= variant->exp_num_rx; i++) {
		struct can_frame frame;
		struct timeval tv = {
			.tv_sec = 0,
			.tv_usec = 50000, /* 50ms timeout */
		};
		fd_set rdfs;

		FD_ZERO(&rdfs);
		FD_SET(self->sock, &rdfs);

		ret = select(self->sock + 1, &rdfs, NULL, NULL, &tv);
		ASSERT_GE(ret, 0)
			TH_LOG("failed select for frame %d, err: %d)", i, errno);

		ret = FD_ISSET(self->sock, &rdfs);
		if (i == variant->exp_num_rx) {
			ASSERT_EQ(ret, 0)
				TH_LOG("too many frames received");
		} else {
			ASSERT_NE(ret, 0)
				TH_LOG("too few frames received");

			ret = read(self->sock, &frame, sizeof(frame));
			ASSERT_GE(ret, 0)
				TH_LOG("failed to read frame %d, err: %d", i, errno);

			TH_LOG("rx: can_id = 0x%08X rx = %d", frame.can_id, i);

			ASSERT_EQ(ID, frame.can_id & CAN_SFF_MASK)
				TH_LOG("received wrong can_id");
			ASSERT_EQ(variant->testcase, frame.data[0])
				TH_LOG("received wrong test case");

			ASSERT_EQ(frame.can_id & ~CAN_ERR_MASK,
				  variant->exp_flags[i])
				TH_LOG("received unexpected flags");
		}
	}
}

int main(int argc, char **argv)
{
	char *ifname = getenv("CANIF");

	if (!ifname) {
		printf("CANIF environment variable must contain the test interface\n");
		return KSFT_FAIL;
	}

	strncpy(CANIF, ifname, sizeof(CANIF) - 1);

	return test_harness_run(argc, argv);
}
