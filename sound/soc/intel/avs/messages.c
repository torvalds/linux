// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include "avs.h"
#include "messages.h"

#define AVS_CL_TIMEOUT_MS	5000

int avs_ipc_load_modules(struct avs_dev *adev, u16 *mod_ids, u32 num_mod_ids)
{
	union avs_global_msg msg = AVS_GLOBAL_REQUEST(LOAD_MULTIPLE_MODULES);
	struct avs_ipc_msg request;
	int ret;

	msg.load_multi_mods.mod_cnt = num_mod_ids;
	request.header = msg.val;
	request.data = mod_ids;
	request.size = sizeof(*mod_ids) * num_mod_ids;

	ret = avs_dsp_send_msg_timeout(adev, &request, NULL, AVS_CL_TIMEOUT_MS);
	if (ret)
		avs_ipc_err(adev, &request, "load multiple modules", ret);

	return ret;
}

int avs_ipc_unload_modules(struct avs_dev *adev, u16 *mod_ids, u32 num_mod_ids)
{
	union avs_global_msg msg = AVS_GLOBAL_REQUEST(UNLOAD_MULTIPLE_MODULES);
	struct avs_ipc_msg request;
	int ret;

	msg.load_multi_mods.mod_cnt = num_mod_ids;
	request.header = msg.val;
	request.data = mod_ids;
	request.size = sizeof(*mod_ids) * num_mod_ids;

	ret = avs_dsp_send_msg_timeout(adev, &request, NULL, AVS_CL_TIMEOUT_MS);
	if (ret)
		avs_ipc_err(adev, &request, "unload multiple modules", ret);

	return ret;
}

int avs_ipc_load_library(struct avs_dev *adev, u32 dma_id, u32 lib_id)
{
	union avs_global_msg msg = AVS_GLOBAL_REQUEST(LOAD_LIBRARY);
	struct avs_ipc_msg request = {{0}};
	int ret;

	msg.load_lib.dma_id = dma_id;
	msg.load_lib.lib_id = lib_id;
	request.header = msg.val;

	ret = avs_dsp_send_msg_timeout(adev, &request, NULL, AVS_CL_TIMEOUT_MS);
	if (ret)
		avs_ipc_err(adev, &request, "load library", ret);

	return ret;
}

int avs_ipc_create_pipeline(struct avs_dev *adev, u16 req_size, u8 priority,
			    u8 instance_id, bool lp, u16 attributes)
{
	union avs_global_msg msg = AVS_GLOBAL_REQUEST(CREATE_PIPELINE);
	struct avs_ipc_msg request = {{0}};
	int ret;

	msg.create_ppl.ppl_mem_size = req_size;
	msg.create_ppl.ppl_priority = priority;
	msg.create_ppl.instance_id = instance_id;
	msg.ext.create_ppl.lp = lp;
	msg.ext.create_ppl.attributes = attributes;
	request.header = msg.val;

	ret = avs_dsp_send_msg(adev, &request, NULL);
	if (ret)
		avs_ipc_err(adev, &request, "create pipeline", ret);

	return ret;
}

int avs_ipc_delete_pipeline(struct avs_dev *adev, u8 instance_id)
{
	union avs_global_msg msg = AVS_GLOBAL_REQUEST(DELETE_PIPELINE);
	struct avs_ipc_msg request = {{0}};
	int ret;

	msg.ppl.instance_id = instance_id;
	request.header = msg.val;

	ret = avs_dsp_send_msg(adev, &request, NULL);
	if (ret)
		avs_ipc_err(adev, &request, "delete pipeline", ret);

	return ret;
}

int avs_ipc_set_pipeline_state(struct avs_dev *adev, u8 instance_id,
			       enum avs_pipeline_state state)
{
	union avs_global_msg msg = AVS_GLOBAL_REQUEST(SET_PIPELINE_STATE);
	struct avs_ipc_msg request = {{0}};
	int ret;

	msg.set_ppl_state.ppl_id = instance_id;
	msg.set_ppl_state.state = state;
	request.header = msg.val;

	ret = avs_dsp_send_msg(adev, &request, NULL);
	if (ret)
		avs_ipc_err(adev, &request, "set pipeline state", ret);

	return ret;
}

int avs_ipc_get_pipeline_state(struct avs_dev *adev, u8 instance_id,
			       enum avs_pipeline_state *state)
{
	union avs_global_msg msg = AVS_GLOBAL_REQUEST(GET_PIPELINE_STATE);
	struct avs_ipc_msg request = {{0}};
	struct avs_ipc_msg reply = {{0}};
	int ret;

	msg.get_ppl_state.ppl_id = instance_id;
	request.header = msg.val;

	ret = avs_dsp_send_msg(adev, &request, &reply);
	if (ret) {
		avs_ipc_err(adev, &request, "get pipeline state", ret);
		return ret;
	}

	*state = reply.rsp.ext.get_ppl_state.state;
	return ret;
}
