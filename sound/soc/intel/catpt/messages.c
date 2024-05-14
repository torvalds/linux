// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/slab.h>
#include "core.h"
#include "messages.h"
#include "registers.h"

int catpt_ipc_get_fw_version(struct catpt_dev *cdev,
			     struct catpt_fw_version *version)
{
	union catpt_global_msg msg = CATPT_GLOBAL_MSG(GET_FW_VERSION);
	struct catpt_ipc_msg request = {{0}}, reply;
	int ret;

	request.header = msg.val;
	reply.size = sizeof(*version);
	reply.data = version;

	ret = catpt_dsp_send_msg(cdev, request, &reply);
	if (ret)
		dev_err(cdev->dev, "get fw version failed: %d\n", ret);

	return ret;
}

struct catpt_alloc_stream_input {
	enum catpt_path_id path_id:8;
	enum catpt_stream_type stream_type:8;
	enum catpt_format_id format_id:8;
	u8 reserved;
	struct catpt_audio_format input_format;
	struct catpt_ring_info ring_info;
	u8 num_entries;
	/* flex array with entries here */
	struct catpt_memory_info persistent_mem;
	struct catpt_memory_info scratch_mem;
	u32 num_notifications; /* obsolete */
} __packed;

int catpt_ipc_alloc_stream(struct catpt_dev *cdev,
			   enum catpt_path_id path_id,
			   enum catpt_stream_type type,
			   struct catpt_audio_format *afmt,
			   struct catpt_ring_info *rinfo,
			   u8 num_modules,
			   struct catpt_module_entry *modules,
			   struct resource *persistent,
			   struct resource *scratch,
			   struct catpt_stream_info *sinfo)
{
	union catpt_global_msg msg = CATPT_GLOBAL_MSG(ALLOCATE_STREAM);
	struct catpt_alloc_stream_input input;
	struct catpt_ipc_msg request, reply;
	size_t size, arrsz;
	u8 *payload;
	off_t off;
	int ret;

	off = offsetof(struct catpt_alloc_stream_input, persistent_mem);
	arrsz = sizeof(*modules) * num_modules;
	size = sizeof(input) + arrsz;

	payload = kzalloc(size, GFP_KERNEL);
	if (!payload)
		return -ENOMEM;

	memset(&input, 0, sizeof(input));
	input.path_id = path_id;
	input.stream_type = type;
	input.format_id = CATPT_FORMAT_PCM;
	input.input_format = *afmt;
	input.ring_info = *rinfo;
	input.num_entries = num_modules;
	input.persistent_mem.offset = catpt_to_dsp_offset(persistent->start);
	input.persistent_mem.size = resource_size(persistent);
	if (scratch) {
		input.scratch_mem.offset = catpt_to_dsp_offset(scratch->start);
		input.scratch_mem.size = resource_size(scratch);
	}

	/* re-arrange the input: account for flex array 'entries' */
	memcpy(payload, &input, sizeof(input));
	memmove(payload + off + arrsz, payload + off, sizeof(input) - off);
	memcpy(payload + off, modules, arrsz);

	request.header = msg.val;
	request.size = size;
	request.data = payload;
	reply.size = sizeof(*sinfo);
	reply.data = sinfo;

	ret = catpt_dsp_send_msg(cdev, request, &reply);
	if (ret)
		dev_err(cdev->dev, "alloc stream type %d failed: %d\n",
			type, ret);

	kfree(payload);
	return ret;
}

int catpt_ipc_free_stream(struct catpt_dev *cdev, u8 stream_hw_id)
{
	union catpt_global_msg msg = CATPT_GLOBAL_MSG(FREE_STREAM);
	struct catpt_ipc_msg request;
	int ret;

	request.header = msg.val;
	request.size = sizeof(stream_hw_id);
	request.data = &stream_hw_id;

	ret = catpt_dsp_send_msg(cdev, request, NULL);
	if (ret)
		dev_err(cdev->dev, "free stream %d failed: %d\n",
			stream_hw_id, ret);

	return ret;
}

int catpt_ipc_set_device_format(struct catpt_dev *cdev,
				struct catpt_ssp_device_format *devfmt)
{
	union catpt_global_msg msg = CATPT_GLOBAL_MSG(SET_DEVICE_FORMATS);
	struct catpt_ipc_msg request;
	int ret;

	request.header = msg.val;
	request.size = sizeof(*devfmt);
	request.data = devfmt;

	ret = catpt_dsp_send_msg(cdev, request, NULL);
	if (ret)
		dev_err(cdev->dev, "set device format failed: %d\n", ret);

	return ret;
}

int catpt_ipc_enter_dxstate(struct catpt_dev *cdev, enum catpt_dx_state state,
			    struct catpt_dx_context *context)
{
	union catpt_global_msg msg = CATPT_GLOBAL_MSG(ENTER_DX_STATE);
	struct catpt_ipc_msg request, reply;
	int ret;

	request.header = msg.val;
	request.size = sizeof(state);
	request.data = &state;
	reply.size = sizeof(*context);
	reply.data = context;

	ret = catpt_dsp_send_msg(cdev, request, &reply);
	if (ret)
		dev_err(cdev->dev, "enter dx state failed: %d\n", ret);

	return ret;
}

int catpt_ipc_get_mixer_stream_info(struct catpt_dev *cdev,
				    struct catpt_mixer_stream_info *info)
{
	union catpt_global_msg msg = CATPT_GLOBAL_MSG(GET_MIXER_STREAM_INFO);
	struct catpt_ipc_msg request = {{0}}, reply;
	int ret;

	request.header = msg.val;
	reply.size = sizeof(*info);
	reply.data = info;

	ret = catpt_dsp_send_msg(cdev, request, &reply);
	if (ret)
		dev_err(cdev->dev, "get mixer info failed: %d\n", ret);

	return ret;
}

int catpt_ipc_reset_stream(struct catpt_dev *cdev, u8 stream_hw_id)
{
	union catpt_stream_msg msg = CATPT_STREAM_MSG(RESET_STREAM);
	struct catpt_ipc_msg request = {{0}};
	int ret;

	msg.stream_hw_id = stream_hw_id;
	request.header = msg.val;

	ret = catpt_dsp_send_msg(cdev, request, NULL);
	if (ret)
		dev_err(cdev->dev, "reset stream %d failed: %d\n",
			stream_hw_id, ret);

	return ret;
}

int catpt_ipc_pause_stream(struct catpt_dev *cdev, u8 stream_hw_id)
{
	union catpt_stream_msg msg = CATPT_STREAM_MSG(PAUSE_STREAM);
	struct catpt_ipc_msg request = {{0}};
	int ret;

	msg.stream_hw_id = stream_hw_id;
	request.header = msg.val;

	ret = catpt_dsp_send_msg(cdev, request, NULL);
	if (ret)
		dev_err(cdev->dev, "pause stream %d failed: %d\n",
			stream_hw_id, ret);

	return ret;
}

int catpt_ipc_resume_stream(struct catpt_dev *cdev, u8 stream_hw_id)
{
	union catpt_stream_msg msg = CATPT_STREAM_MSG(RESUME_STREAM);
	struct catpt_ipc_msg request = {{0}};
	int ret;

	msg.stream_hw_id = stream_hw_id;
	request.header = msg.val;

	ret = catpt_dsp_send_msg(cdev, request, NULL);
	if (ret)
		dev_err(cdev->dev, "resume stream %d failed: %d\n",
			stream_hw_id, ret);

	return ret;
}

struct catpt_set_volume_input {
	u32 channel;
	u32 target_volume;
	u64 curve_duration;
	u32 curve_type;
} __packed;

int catpt_ipc_set_volume(struct catpt_dev *cdev, u8 stream_hw_id,
			 u32 channel, u32 volume,
			 u32 curve_duration,
			 enum catpt_audio_curve_type curve_type)
{
	union catpt_stream_msg msg = CATPT_STAGE_MSG(SET_VOLUME);
	struct catpt_ipc_msg request;
	struct catpt_set_volume_input input;
	int ret;

	msg.stream_hw_id = stream_hw_id;
	input.channel = channel;
	input.target_volume = volume;
	input.curve_duration = curve_duration;
	input.curve_type = curve_type;

	request.header = msg.val;
	request.size = sizeof(input);
	request.data = &input;

	ret = catpt_dsp_send_msg(cdev, request, NULL);
	if (ret)
		dev_err(cdev->dev, "set stream %d volume failed: %d\n",
			stream_hw_id, ret);

	return ret;
}

struct catpt_set_write_pos_input {
	u32 new_write_pos;
	bool end_of_buffer;
	bool low_latency;
} __packed;

int catpt_ipc_set_write_pos(struct catpt_dev *cdev, u8 stream_hw_id,
			    u32 pos, bool eob, bool ll)
{
	union catpt_stream_msg msg = CATPT_STAGE_MSG(SET_WRITE_POSITION);
	struct catpt_ipc_msg request;
	struct catpt_set_write_pos_input input;
	int ret;

	msg.stream_hw_id = stream_hw_id;
	input.new_write_pos = pos;
	input.end_of_buffer = eob;
	input.low_latency = ll;

	request.header = msg.val;
	request.size = sizeof(input);
	request.data = &input;

	ret = catpt_dsp_send_msg(cdev, request, NULL);
	if (ret)
		dev_err(cdev->dev, "set stream %d write pos failed: %d\n",
			stream_hw_id, ret);

	return ret;
}

int catpt_ipc_mute_loopback(struct catpt_dev *cdev, u8 stream_hw_id, bool mute)
{
	union catpt_stream_msg msg = CATPT_STAGE_MSG(MUTE_LOOPBACK);
	struct catpt_ipc_msg request;
	int ret;

	msg.stream_hw_id = stream_hw_id;
	request.header = msg.val;
	request.size = sizeof(mute);
	request.data = &mute;

	ret = catpt_dsp_send_msg(cdev, request, NULL);
	if (ret)
		dev_err(cdev->dev, "mute loopback failed: %d\n", ret);

	return ret;
}
