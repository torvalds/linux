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
