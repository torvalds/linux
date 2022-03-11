// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/slab.h>
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

/*
 * avs_ipc_init_instance - Initialize module instance
 *
 * @adev: Driver context
 * @module_id: Module-type id
 * @instance_id: Unique module instance id
 * @ppl_id: Parent pipeline id
 * @core_id: DSP core to allocate module on
 * @domain: Processing domain (low latency or data processing)
 * @param: Module-type specific configuration
 * @param_size: Size of @param in bytes
 *
 * Argument verification, as well as pipeline state checks are done by the
 * firmware.
 *
 * Note: @ppl_id and @core_id are independent of each other as single pipeline
 * can be composed of module instances located on different DSP cores.
 */
int avs_ipc_init_instance(struct avs_dev *adev, u16 module_id, u8 instance_id,
			  u8 ppl_id, u8 core_id, u8 domain,
			  void *param, u32 param_size)
{
	union avs_module_msg msg = AVS_MODULE_REQUEST(INIT_INSTANCE);
	struct avs_ipc_msg request;
	int ret;

	msg.module_id = module_id;
	msg.instance_id = instance_id;
	/* firmware expects size provided in dwords */
	msg.ext.init_instance.param_block_size = DIV_ROUND_UP(param_size, sizeof(u32));
	msg.ext.init_instance.ppl_instance_id = ppl_id;
	msg.ext.init_instance.core_id = core_id;
	msg.ext.init_instance.proc_domain = domain;

	request.header = msg.val;
	request.data = param;
	request.size = param_size;

	ret = avs_dsp_send_msg(adev, &request, NULL);
	if (ret)
		avs_ipc_err(adev, &request, "init instance", ret);

	return ret;
}

/*
 * avs_ipc_delete_instance - Delete module instance
 *
 * @adev: Driver context
 * @module_id: Module-type id
 * @instance_id: Unique module instance id
 *
 * Argument verification, as well as pipeline state checks are done by the
 * firmware.
 *
 * Note: only standalone modules i.e. without a parent pipeline shall be
 * deleted using this IPC message. In all other cases, pipeline owning the
 * modules performs cleanup automatically when it is deleted.
 */
int avs_ipc_delete_instance(struct avs_dev *adev, u16 module_id, u8 instance_id)
{
	union avs_module_msg msg = AVS_MODULE_REQUEST(DELETE_INSTANCE);
	struct avs_ipc_msg request = {{0}};
	int ret;

	msg.module_id = module_id;
	msg.instance_id = instance_id;
	request.header = msg.val;

	ret = avs_dsp_send_msg(adev, &request, NULL);
	if (ret)
		avs_ipc_err(adev, &request, "delete instance", ret);

	return ret;
}

/*
 * avs_ipc_bind - Bind two module instances
 *
 * @adev: Driver context
 * @module_id: Source module-type id
 * @instance_id: Source module instance id
 * @dst_module_id: Sink module-type id
 * @dst_instance_id: Sink module instance id
 * @dst_queue: Sink module pin to bind @src_queue with
 * @src_queue: Source module pin to bind @dst_queue with
 */
int avs_ipc_bind(struct avs_dev *adev, u16 module_id, u8 instance_id,
		 u16 dst_module_id, u8 dst_instance_id,
		 u8 dst_queue, u8 src_queue)
{
	union avs_module_msg msg = AVS_MODULE_REQUEST(BIND);
	struct avs_ipc_msg request = {{0}};
	int ret;

	msg.module_id = module_id;
	msg.instance_id = instance_id;
	msg.ext.bind_unbind.dst_module_id = dst_module_id;
	msg.ext.bind_unbind.dst_instance_id = dst_instance_id;
	msg.ext.bind_unbind.dst_queue = dst_queue;
	msg.ext.bind_unbind.src_queue = src_queue;
	request.header = msg.val;

	ret = avs_dsp_send_msg(adev, &request, NULL);
	if (ret)
		avs_ipc_err(adev, &request, "bind modules", ret);

	return ret;
}

/*
 * avs_ipc_unbind - Unbind two module instances
 *
 * @adev: Driver context
 * @module_id: Source module-type id
 * @instance_id: Source module instance id
 * @dst_module_id: Sink module-type id
 * @dst_instance_id: Sink module instance id
 * @dst_queue: Sink module pin to unbind @src_queue from
 * @src_queue: Source module pin to unbind @dst_queue from
 */
int avs_ipc_unbind(struct avs_dev *adev, u16 module_id, u8 instance_id,
		   u16 dst_module_id, u8 dst_instance_id,
		   u8 dst_queue, u8 src_queue)
{
	union avs_module_msg msg = AVS_MODULE_REQUEST(UNBIND);
	struct avs_ipc_msg request = {{0}};
	int ret;

	msg.module_id = module_id;
	msg.instance_id = instance_id;
	msg.ext.bind_unbind.dst_module_id = dst_module_id;
	msg.ext.bind_unbind.dst_instance_id = dst_instance_id;
	msg.ext.bind_unbind.dst_queue = dst_queue;
	msg.ext.bind_unbind.src_queue = src_queue;
	request.header = msg.val;

	ret = avs_dsp_send_msg(adev, &request, NULL);
	if (ret)
		avs_ipc_err(adev, &request, "unbind modules", ret);

	return ret;
}

static int __avs_ipc_set_large_config(struct avs_dev *adev, u16 module_id, u8 instance_id,
				      u8 param_id, bool init_block, bool final_block,
				      u8 *request_data, size_t request_size, size_t off_size)
{
	union avs_module_msg msg = AVS_MODULE_REQUEST(LARGE_CONFIG_SET);
	struct avs_ipc_msg request;
	int ret;

	msg.module_id = module_id;
	msg.instance_id = instance_id;
	msg.ext.large_config.data_off_size = off_size;
	msg.ext.large_config.large_param_id = param_id;
	msg.ext.large_config.final_block = final_block;
	msg.ext.large_config.init_block = init_block;

	request.header = msg.val;
	request.data = request_data;
	request.size = request_size;

	ret = avs_dsp_send_msg(adev, &request, NULL);
	if (ret)
		avs_ipc_err(adev, &request, "large config set", ret);

	return ret;
}

int avs_ipc_set_large_config(struct avs_dev *adev, u16 module_id,
			     u8 instance_id, u8 param_id,
			     u8 *request, size_t request_size)
{
	size_t remaining, tx_size;
	bool final;
	int ret;

	remaining = request_size;
	tx_size = min_t(size_t, AVS_MAILBOX_SIZE, remaining);
	final = (tx_size == remaining);

	/* Initial request states total payload size. */
	ret = __avs_ipc_set_large_config(adev, module_id, instance_id,
					 param_id, 1, final, request, tx_size,
					 request_size);
	if (ret)
		return ret;

	remaining -= tx_size;

	/* Loop the rest only when payload exceeds mailbox's size. */
	while (remaining) {
		size_t offset;

		offset = request_size - remaining;
		tx_size = min_t(size_t, AVS_MAILBOX_SIZE, remaining);
		final = (tx_size == remaining);

		ret = __avs_ipc_set_large_config(adev, module_id, instance_id,
						 param_id, 0, final,
						 request + offset, tx_size,
						 offset);
		if (ret)
			return ret;

		remaining -= tx_size;
	}

	return 0;
}

int avs_ipc_get_large_config(struct avs_dev *adev, u16 module_id, u8 instance_id,
			     u8 param_id, u8 *request_data, size_t request_size,
			     u8 **reply_data, size_t *reply_size)
{
	union avs_module_msg msg = AVS_MODULE_REQUEST(LARGE_CONFIG_GET);
	struct avs_ipc_msg request;
	struct avs_ipc_msg reply = {{0}};
	size_t size;
	void *buf;
	int ret;

	reply.data = kzalloc(AVS_MAILBOX_SIZE, GFP_KERNEL);
	if (!reply.data)
		return -ENOMEM;

	msg.module_id = module_id;
	msg.instance_id = instance_id;
	msg.ext.large_config.data_off_size = request_size;
	msg.ext.large_config.large_param_id = param_id;
	/* final_block is always 0 on request. Updated by fw on reply. */
	msg.ext.large_config.final_block = 0;
	msg.ext.large_config.init_block = 1;

	request.header = msg.val;
	request.data = request_data;
	request.size = request_size;
	reply.size = AVS_MAILBOX_SIZE;

	ret = avs_dsp_send_msg(adev, &request, &reply);
	if (ret) {
		avs_ipc_err(adev, &request, "large config get", ret);
		kfree(reply.data);
		return ret;
	}

	size = reply.rsp.ext.large_config.data_off_size;
	buf = krealloc(reply.data, size, GFP_KERNEL);
	if (!buf) {
		kfree(reply.data);
		return -ENOMEM;
	}

	*reply_data = buf;
	*reply_size = size;

	return 0;
}
