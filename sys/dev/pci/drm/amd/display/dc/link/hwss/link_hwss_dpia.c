/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */
#include "link_hwss_dpia.h"
#include "core_types.h"
#include "link_hwss_dio.h"
#include "link_enc_cfg.h"

#define DC_LOGGER \
	link->ctx->logger
#define DC_LOGGER_INIT(logger)

static void update_dpia_stream_allocation_table(struct dc_link *link,
		const struct link_resource *link_res,
		const struct link_mst_stream_allocation_table *table)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);
	static enum dc_status status;
	uint8_t mst_alloc_slots = 0, prev_mst_slots_in_use = 0xFF;
	int i;
	DC_LOGGER_INIT(link->ctx->logger);

	for (i = 0; i < table->stream_count; i++)
		mst_alloc_slots += table->stream_allocations[i].slot_count;

	status = dc_process_dmub_set_mst_slots(link->dc, link->link_index,
			mst_alloc_slots, &prev_mst_slots_in_use);
	ASSERT(status == DC_OK);
	DC_LOG_MST("dpia : status[%d]: alloc_slots[%d]: used_slots[%d]\n",
			status, mst_alloc_slots, prev_mst_slots_in_use);

	if (link_enc)
		link_enc->funcs->update_mst_stream_allocation_table(link_enc, table);
}

static void set_dio_dpia_link_test_pattern(struct dc_link *link,
		const struct link_resource *link_res,
		struct encoder_set_dp_phy_pattern_param *tp_params)
{
	if (tp_params->dp_phy_pattern != DP_TEST_PATTERN_VIDEO_MODE)
		return;

	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	if (!link_enc)
		return;

	link_enc->funcs->dp_set_phy_pattern(link_enc, tp_params);
	link->dc->link_srv->dp_trace_source_sequence(link, DPCD_SOURCE_SEQ_AFTER_SET_SOURCE_PATTERN);
}

static void set_dio_dpia_lane_settings(struct dc_link *link,
		const struct link_resource *link_res,
		const struct dc_link_settings *link_settings,
		const struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX])
{
}

static const struct link_hwss dpia_link_hwss = {
	.setup_stream_encoder = setup_dio_stream_encoder,
	.reset_stream_encoder = reset_dio_stream_encoder,
	.setup_stream_attribute = setup_dio_stream_attribute,
	.disable_link_output = disable_dio_link_output,
	.setup_audio_output = setup_dio_audio_output,
	.enable_audio_packet = enable_dio_audio_packet,
	.disable_audio_packet = disable_dio_audio_packet,
	.ext = {
		.set_throttled_vcp_size = set_dio_throttled_vcp_size,
		.enable_dp_link_output = enable_dio_dp_link_output,
		.set_dp_link_test_pattern = set_dio_dpia_link_test_pattern,
		.set_dp_lane_settings = set_dio_dpia_lane_settings,
		.update_stream_allocation_table = update_dpia_stream_allocation_table,
	},
};

bool can_use_dpia_link_hwss(const struct dc_link *link,
		const struct link_resource *link_res)
{
	return link->is_dig_mapping_flexible &&
			link->dc->res_pool->funcs->link_encs_assign;
}

const struct link_hwss *get_dpia_link_hwss(void)
{
	return &dpia_link_hwss;
}
