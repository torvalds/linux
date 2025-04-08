// SPDX-License-Identifier: GPL-2.0
#include <kunit/test.h>

#include "hsr_main.h"
#include "hsr_framereg.h"

struct prp_test_data {
	struct hsr_port port;
	struct hsr_port port_rcv;
	struct hsr_frame_info frame;
	struct hsr_node node;
};

static struct prp_test_data *build_prp_test_data(struct kunit *test)
{
	struct prp_test_data *data = kunit_kzalloc(test,
		sizeof(struct prp_test_data), GFP_USER);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, data);

	data->frame.node_src = &data->node;
	data->frame.port_rcv = &data->port_rcv;
	data->port_rcv.type = HSR_PT_SLAVE_A;
	data->node.seq_start[HSR_PT_SLAVE_A] = 1;
	data->node.seq_expected[HSR_PT_SLAVE_A] = 1;
	data->node.seq_start[HSR_PT_SLAVE_B] = 1;
	data->node.seq_expected[HSR_PT_SLAVE_B] = 1;
	data->node.seq_out[HSR_PT_MASTER] = 0;
	data->node.time_out[HSR_PT_MASTER] = jiffies;
	data->port.type = HSR_PT_MASTER;

	return data;
}

static void check_prp_counters(struct kunit *test,
			       struct prp_test_data *data,
			       u16 seq_start_a, u16 seq_expected_a,
			       u16 seq_start_b, u16 seq_expected_b)
{
	KUNIT_EXPECT_EQ(test, data->node.seq_start[HSR_PT_SLAVE_A],
			seq_start_a);
	KUNIT_EXPECT_EQ(test, data->node.seq_start[HSR_PT_SLAVE_B],
			seq_start_b);
	KUNIT_EXPECT_EQ(test, data->node.seq_expected[HSR_PT_SLAVE_A],
			seq_expected_a);
	KUNIT_EXPECT_EQ(test, data->node.seq_expected[HSR_PT_SLAVE_B],
			seq_expected_b);
}

static void prp_dup_discard_forward(struct kunit *test)
{
	/* Normal situation, both LANs in sync. Next frame is forwarded */
	struct prp_test_data *data = build_prp_test_data(test);

	data->frame.sequence_nr = 2;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	KUNIT_EXPECT_EQ(test, data->frame.sequence_nr,
			data->node.seq_out[HSR_PT_MASTER]);
	KUNIT_EXPECT_EQ(test, jiffies, data->node.time_out[HSR_PT_MASTER]);
	check_prp_counters(test, data, data->frame.sequence_nr,
			   data->frame.sequence_nr + 1, 1, 1);
}

static void prp_dup_discard_inside_dropwindow(struct kunit *test)
{
	/* Normal situation, other LAN ahead by one. Frame is dropped */
	struct prp_test_data *data = build_prp_test_data(test);
	unsigned long time = jiffies - 10;

	data->frame.sequence_nr = 1;
	data->node.seq_expected[HSR_PT_SLAVE_B] = 3;
	data->node.seq_out[HSR_PT_MASTER] = 2;
	data->node.time_out[HSR_PT_MASTER] = time;

	KUNIT_EXPECT_EQ(test, 1,
			prp_register_frame_out(&data->port, &data->frame));
	KUNIT_EXPECT_EQ(test, 2, data->node.seq_out[HSR_PT_MASTER]);
	KUNIT_EXPECT_EQ(test, time, data->node.time_out[HSR_PT_MASTER]);
	check_prp_counters(test, data, 2, 2, 2, 3);
}

static void prp_dup_discard_node_timeout(struct kunit *test)
{
	/* Timeout situation, node hasn't sent anything for a while */
	struct prp_test_data *data = build_prp_test_data(test);

	data->frame.sequence_nr = 7;
	data->node.seq_start[HSR_PT_SLAVE_A] = 1234;
	data->node.seq_expected[HSR_PT_SLAVE_A] = 1235;
	data->node.seq_start[HSR_PT_SLAVE_B] = 1234;
	data->node.seq_expected[HSR_PT_SLAVE_B] = 1234;
	data->node.seq_out[HSR_PT_MASTER] = 1234;
	data->node.time_out[HSR_PT_MASTER] =
		jiffies - msecs_to_jiffies(HSR_ENTRY_FORGET_TIME) - 1;

	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	KUNIT_EXPECT_EQ(test, data->frame.sequence_nr,
			data->node.seq_out[HSR_PT_MASTER]);
	KUNIT_EXPECT_EQ(test, jiffies, data->node.time_out[HSR_PT_MASTER]);
	check_prp_counters(test, data, data->frame.sequence_nr,
			   data->frame.sequence_nr + 1, 1234, 1234);
}

static void prp_dup_discard_out_of_sequence(struct kunit *test)
{
	/* One frame is received out of sequence on both LANs */
	struct prp_test_data *data = build_prp_test_data(test);

	data->node.seq_start[HSR_PT_SLAVE_A] = 10;
	data->node.seq_expected[HSR_PT_SLAVE_A] = 10;
	data->node.seq_start[HSR_PT_SLAVE_B] = 10;
	data->node.seq_expected[HSR_PT_SLAVE_B] = 10;
	data->node.seq_out[HSR_PT_MASTER] = 9;

	/* 1st old frame, should be accepted */
	data->frame.sequence_nr = 8;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	KUNIT_EXPECT_EQ(test, data->frame.sequence_nr,
			data->node.seq_out[HSR_PT_MASTER]);
	check_prp_counters(test, data, data->frame.sequence_nr,
			   data->frame.sequence_nr + 1, 10, 10);

	/* 2nd frame should be dropped */
	data->frame.sequence_nr = 8;
	data->port_rcv.type = HSR_PT_SLAVE_B;
	KUNIT_EXPECT_EQ(test, 1,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_counters(test, data, data->frame.sequence_nr + 1,
			   data->frame.sequence_nr + 1,
			   data->frame.sequence_nr + 1,
			   data->frame.sequence_nr + 1);

	/* Next frame, this is forwarded */
	data->frame.sequence_nr = 10;
	data->port_rcv.type = HSR_PT_SLAVE_A;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	KUNIT_EXPECT_EQ(test, data->frame.sequence_nr,
			data->node.seq_out[HSR_PT_MASTER]);
	check_prp_counters(test, data, data->frame.sequence_nr,
			   data->frame.sequence_nr + 1, 9, 9);

	/* and next one is dropped */
	data->frame.sequence_nr = 10;
	data->port_rcv.type = HSR_PT_SLAVE_B;
	KUNIT_EXPECT_EQ(test, 1,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_counters(test, data, data->frame.sequence_nr + 1,
			   data->frame.sequence_nr + 1,
			   data->frame.sequence_nr + 1,
			   data->frame.sequence_nr + 1);
}

static void prp_dup_discard_lan_b_late(struct kunit *test)
{
	/* LAN B is behind */
	struct prp_test_data *data = build_prp_test_data(test);

	data->node.seq_start[HSR_PT_SLAVE_A] = 9;
	data->node.seq_expected[HSR_PT_SLAVE_A] = 9;
	data->node.seq_start[HSR_PT_SLAVE_B] = 9;
	data->node.seq_expected[HSR_PT_SLAVE_B] = 9;
	data->node.seq_out[HSR_PT_MASTER] = 8;

	data->frame.sequence_nr = 9;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	KUNIT_EXPECT_EQ(test, data->frame.sequence_nr,
			data->node.seq_out[HSR_PT_MASTER]);
	check_prp_counters(test, data, 9, 10, 9, 9);

	data->frame.sequence_nr = 10;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	KUNIT_EXPECT_EQ(test, data->frame.sequence_nr,
			data->node.seq_out[HSR_PT_MASTER]);
	check_prp_counters(test, data, 9, 11, 9, 9);

	data->frame.sequence_nr = 9;
	data->port_rcv.type = HSR_PT_SLAVE_B;
	KUNIT_EXPECT_EQ(test, 1,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_counters(test, data, 10, 11, 10, 10);

	data->frame.sequence_nr = 10;
	data->port_rcv.type = HSR_PT_SLAVE_B;
	KUNIT_EXPECT_EQ(test, 1,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_counters(test, data,  11, 11, 11, 11);
}

static struct kunit_case prp_dup_discard_test_cases[] = {
	KUNIT_CASE(prp_dup_discard_forward),
	KUNIT_CASE(prp_dup_discard_inside_dropwindow),
	KUNIT_CASE(prp_dup_discard_node_timeout),
	KUNIT_CASE(prp_dup_discard_out_of_sequence),
	KUNIT_CASE(prp_dup_discard_lan_b_late),
	{}
};

static struct kunit_suite prp_dup_discard_suite = {
	.name = "prp_duplicate_discard",
	.test_cases = prp_dup_discard_test_cases,
};

kunit_test_suite(prp_dup_discard_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for PRP duplicate discard");
MODULE_AUTHOR("Jaakko Karrenpalo <jkarrenpalo@gmail.com>");
