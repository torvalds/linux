// SPDX-License-Identifier: GPL-2.0
#include <kunit/test.h>

#include "hsr_main.h"
#include "hsr_framereg.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

struct prp_test_data {
	struct hsr_port port;
	struct hsr_port port_rcv;
	struct hsr_frame_info frame;
	struct hsr_node node;
};

static struct prp_test_data *build_prp_test_data(struct kunit *test)
{
	size_t block_sz;

	struct prp_test_data *data = kunit_kzalloc(test,
		sizeof(struct prp_test_data), GFP_USER);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, data);

	data->node.seq_port_cnt = 1;
	block_sz = hsr_seq_block_size(&data->node);
	data->node.block_buf = kunit_kcalloc(test, HSR_MAX_SEQ_BLOCKS, block_sz,
					     GFP_ATOMIC);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, data->node.block_buf);

	xa_init(&data->node.seq_blocks);
	spin_lock_init(&data->node.seq_out_lock);

	data->frame.node_src = &data->node;
	data->frame.port_rcv = &data->port_rcv;
	data->port_rcv.type = HSR_PT_SLAVE_A;
	data->port.type = HSR_PT_MASTER;

	return data;
}

static void check_prp_frame_seen(struct kunit *test, struct prp_test_data *data,
				 u16 sequence_nr)
{
	u16 block_idx, seq_bit;
	struct hsr_seq_block *block;

	block_idx = sequence_nr >> HSR_SEQ_BLOCK_SHIFT;
	block = xa_load(&data->node.seq_blocks, block_idx);
	KUNIT_EXPECT_NOT_NULL(test, block);

	seq_bit = sequence_nr & HSR_SEQ_BLOCK_MASK;
	KUNIT_EXPECT_TRUE(test, test_bit(seq_bit, block->seq_nrs[0]));
}

static void check_prp_frame_unseen(struct kunit *test,
				   struct prp_test_data *data, u16 sequence_nr)
{
	u16 block_idx, seq_bit;
	struct hsr_seq_block *block;

	block_idx = sequence_nr >> HSR_SEQ_BLOCK_SHIFT;
	block = hsr_get_seq_block(&data->node, block_idx);
	KUNIT_EXPECT_NOT_NULL(test, block);

	seq_bit = sequence_nr & HSR_SEQ_BLOCK_MASK;
	KUNIT_EXPECT_FALSE(test, test_bit(seq_bit, block->seq_nrs[0]));
}

static void prp_dup_discard_forward(struct kunit *test)
{
	/* Normal situation, both LANs in sync. Next frame is forwarded */
	struct prp_test_data *data = build_prp_test_data(test);

	data->frame.sequence_nr = 2;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_frame_seen(test, data, data->frame.sequence_nr);
}

static void prp_dup_discard_drop_duplicate(struct kunit *test)
{
	struct prp_test_data *data = build_prp_test_data(test);

	data->frame.sequence_nr = 2;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_frame_seen(test, data, data->frame.sequence_nr);

	KUNIT_EXPECT_EQ(test, 1,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_frame_seen(test, data, data->frame.sequence_nr);
}

static void prp_dup_discard_entry_timeout(struct kunit *test)
{
	/* Timeout situation, node hasn't sent anything for a while */
	struct prp_test_data *data = build_prp_test_data(test);
	struct hsr_seq_block *block;
	u16 block_idx;

	data->frame.sequence_nr = 7;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_frame_seen(test, data, data->frame.sequence_nr);

	data->frame.sequence_nr = 11;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_frame_seen(test, data, data->frame.sequence_nr);

	block_idx = data->frame.sequence_nr >> HSR_SEQ_BLOCK_SHIFT;
	block = hsr_get_seq_block(&data->node, block_idx);
	block->time = jiffies - msecs_to_jiffies(HSR_ENTRY_FORGET_TIME) - 1;

	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_frame_seen(test, data, data->frame.sequence_nr);
	check_prp_frame_unseen(test, data, 7);
}

static void prp_dup_discard_out_of_sequence(struct kunit *test)
{
	/* One frame is received out of sequence on both LANs */
	struct prp_test_data *data = build_prp_test_data(test);

	/* initial frame, should be accepted */
	data->frame.sequence_nr = 9;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_frame_seen(test, data, data->frame.sequence_nr);

	/* 1st old frame, should be accepted */
	data->frame.sequence_nr = 8;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_frame_seen(test, data, data->frame.sequence_nr);

	/* 2nd frame should be dropped */
	data->frame.sequence_nr = 8;
	data->port_rcv.type = HSR_PT_SLAVE_B;
	KUNIT_EXPECT_EQ(test, 1,
			prp_register_frame_out(&data->port, &data->frame));

	/* Next frame, this is forwarded */
	data->frame.sequence_nr = 10;
	data->port_rcv.type = HSR_PT_SLAVE_A;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_frame_seen(test, data, data->frame.sequence_nr);

	/* and next one is dropped */
	data->frame.sequence_nr = 10;
	data->port_rcv.type = HSR_PT_SLAVE_B;
	KUNIT_EXPECT_EQ(test, 1,
			prp_register_frame_out(&data->port, &data->frame));
}

static void prp_dup_discard_lan_b_late(struct kunit *test)
{
	/* LAN B is behind */
	struct prp_test_data *data = build_prp_test_data(test);

	data->frame.sequence_nr = 9;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_frame_seen(test, data, data->frame.sequence_nr);

	data->frame.sequence_nr = 10;
	KUNIT_EXPECT_EQ(test, 0,
			prp_register_frame_out(&data->port, &data->frame));
	check_prp_frame_seen(test, data, data->frame.sequence_nr);

	data->frame.sequence_nr = 9;
	data->port_rcv.type = HSR_PT_SLAVE_B;
	KUNIT_EXPECT_EQ(test, 1,
			prp_register_frame_out(&data->port, &data->frame));

	data->frame.sequence_nr = 10;
	data->port_rcv.type = HSR_PT_SLAVE_B;
	KUNIT_EXPECT_EQ(test, 1,
			prp_register_frame_out(&data->port, &data->frame));
}

static struct kunit_case prp_dup_discard_test_cases[] = {
	KUNIT_CASE(prp_dup_discard_forward),
	KUNIT_CASE(prp_dup_discard_drop_duplicate),
	KUNIT_CASE(prp_dup_discard_entry_timeout),
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
