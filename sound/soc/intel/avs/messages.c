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

int avs_ipc_set_boot_config(struct avs_dev *adev, u32 dma_id, u32 purge)
{
	union avs_global_msg msg = AVS_GLOBAL_REQUEST(ROM_CONTROL);
	struct avs_ipc_msg request = {{0}};
	int ret;

	msg.boot_cfg.rom_ctrl_msg_type = AVS_ROM_SET_BOOT_CONFIG;
	msg.boot_cfg.dma_id = dma_id;
	msg.boot_cfg.purge_request = purge;
	request.header = msg.val;

	ret = avs_dsp_send_rom_msg(adev, &request);
	if (ret)
		avs_ipc_err(adev, &request, "set boot config", ret);

	return ret;
}

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

	buf = krealloc(reply.data, reply.size, GFP_KERNEL);
	if (!buf) {
		kfree(reply.data);
		return -ENOMEM;
	}

	*reply_data = buf;
	*reply_size = reply.size;

	return 0;
}

int avs_ipc_set_dx(struct avs_dev *adev, u32 core_mask, bool powerup)
{
	union avs_module_msg msg = AVS_MODULE_REQUEST(SET_DX);
	struct avs_ipc_msg request;
	struct avs_dxstate_info dx;
	int ret;

	dx.core_mask = core_mask;
	dx.dx_mask = powerup ? core_mask : 0;
	request.header = msg.val;
	request.data = &dx;
	request.size = sizeof(dx);

	ret = avs_dsp_send_pm_msg(adev, &request, NULL, true);
	if (ret)
		avs_ipc_err(adev, &request, "set dx", ret);

	return ret;
}

/*
 * avs_ipc_set_d0ix - Set power gating policy (entering D0IX substates)
 *
 * @enable_pg: Whether to enable or disable power gating
 * @streaming: Whether a stream is running when transitioning
 */
int avs_ipc_set_d0ix(struct avs_dev *adev, bool enable_pg, bool streaming)
{
	union avs_module_msg msg = AVS_MODULE_REQUEST(SET_D0IX);
	struct avs_ipc_msg request = {{0}};
	int ret;

	msg.ext.set_d0ix.wake = enable_pg;
	msg.ext.set_d0ix.streaming = streaming;

	request.header = msg.val;

	ret = avs_dsp_send_pm_msg(adev, &request, NULL, false);
	if (ret)
		avs_ipc_err(adev, &request, "set d0ix", ret);

	return ret;
}

int avs_ipc_get_fw_config(struct avs_dev *adev, struct avs_fw_cfg *cfg)
{
	struct avs_tlv *tlv;
	size_t payload_size;
	size_t offset = 0;
	u8 *payload;
	int ret;

	ret = avs_ipc_get_large_config(adev, AVS_BASEFW_MOD_ID, AVS_BASEFW_INST_ID,
				       AVS_BASEFW_FIRMWARE_CONFIG, NULL, 0,
				       &payload, &payload_size);
	if (ret)
		return ret;

	while (offset < payload_size) {
		tlv = (struct avs_tlv *)(payload + offset);

		switch (tlv->type) {
		case AVS_FW_CFG_FW_VERSION:
			memcpy(&cfg->fw_version, tlv->value, sizeof(cfg->fw_version));
			break;

		case AVS_FW_CFG_MEMORY_RECLAIMED:
			cfg->memory_reclaimed = *tlv->value;
			break;

		case AVS_FW_CFG_SLOW_CLOCK_FREQ_HZ:
			cfg->slow_clock_freq_hz = *tlv->value;
			break;

		case AVS_FW_CFG_FAST_CLOCK_FREQ_HZ:
			cfg->fast_clock_freq_hz = *tlv->value;
			break;

		case AVS_FW_CFG_ALH_SUPPORT_LEVEL:
			cfg->alh_support = *tlv->value;
			break;

		case AVS_FW_CFG_IPC_DL_MAILBOX_BYTES:
			cfg->ipc_dl_mailbox_bytes = *tlv->value;
			break;

		case AVS_FW_CFG_IPC_UL_MAILBOX_BYTES:
			cfg->ipc_ul_mailbox_bytes = *tlv->value;
			break;

		case AVS_FW_CFG_TRACE_LOG_BYTES:
			cfg->trace_log_bytes = *tlv->value;
			break;

		case AVS_FW_CFG_MAX_PPL_COUNT:
			cfg->max_ppl_count = *tlv->value;
			break;

		case AVS_FW_CFG_MAX_ASTATE_COUNT:
			cfg->max_astate_count = *tlv->value;
			break;

		case AVS_FW_CFG_MAX_MODULE_PIN_COUNT:
			cfg->max_module_pin_count = *tlv->value;
			break;

		case AVS_FW_CFG_MODULES_COUNT:
			cfg->modules_count = *tlv->value;
			break;

		case AVS_FW_CFG_MAX_MOD_INST_COUNT:
			cfg->max_mod_inst_count = *tlv->value;
			break;

		case AVS_FW_CFG_MAX_LL_TASKS_PER_PRI_COUNT:
			cfg->max_ll_tasks_per_pri_count = *tlv->value;
			break;

		case AVS_FW_CFG_LL_PRI_COUNT:
			cfg->ll_pri_count = *tlv->value;
			break;

		case AVS_FW_CFG_MAX_DP_TASKS_COUNT:
			cfg->max_dp_tasks_count = *tlv->value;
			break;

		case AVS_FW_CFG_MAX_LIBS_COUNT:
			cfg->max_libs_count = *tlv->value;
			break;

		case AVS_FW_CFG_XTAL_FREQ_HZ:
			cfg->xtal_freq_hz = *tlv->value;
			break;

		case AVS_FW_CFG_POWER_GATING_POLICY:
			cfg->power_gating_policy = *tlv->value;
			break;

		/* Known but not useful to us. */
		case AVS_FW_CFG_DMA_BUFFER_CONFIG:
		case AVS_FW_CFG_SCHEDULER_CONFIG:
		case AVS_FW_CFG_CLOCKS_CONFIG:
			break;

		default:
			dev_info(adev->dev, "Unrecognized fw param: %d\n", tlv->type);
			break;
		}

		offset += sizeof(*tlv) + tlv->length;
	}

	/* No longer needed, free it as it's owned by the get_large_config() caller. */
	kfree(payload);
	return ret;
}

int avs_ipc_get_hw_config(struct avs_dev *adev, struct avs_hw_cfg *cfg)
{
	struct avs_tlv *tlv;
	size_t payload_size;
	size_t size, offset = 0;
	u8 *payload;
	int ret;

	ret = avs_ipc_get_large_config(adev, AVS_BASEFW_MOD_ID, AVS_BASEFW_INST_ID,
				       AVS_BASEFW_HARDWARE_CONFIG, NULL, 0,
				       &payload, &payload_size);
	if (ret)
		return ret;

	while (offset < payload_size) {
		tlv = (struct avs_tlv *)(payload + offset);

		switch (tlv->type) {
		case AVS_HW_CFG_AVS_VER:
			cfg->avs_version = *tlv->value;
			break;

		case AVS_HW_CFG_DSP_CORES:
			cfg->dsp_cores = *tlv->value;
			break;

		case AVS_HW_CFG_MEM_PAGE_BYTES:
			cfg->mem_page_bytes = *tlv->value;
			break;

		case AVS_HW_CFG_TOTAL_PHYS_MEM_PAGES:
			cfg->total_phys_mem_pages = *tlv->value;
			break;

		case AVS_HW_CFG_I2S_CAPS:
			cfg->i2s_caps.i2s_version = tlv->value[0];
			size = tlv->value[1];
			cfg->i2s_caps.ctrl_count = size;
			if (!size)
				break;

			/* Multiply to get entire array size. */
			size *= sizeof(*cfg->i2s_caps.ctrl_base_addr);
			cfg->i2s_caps.ctrl_base_addr = devm_kmemdup(adev->dev,
								    &tlv->value[2],
								    size, GFP_KERNEL);
			if (!cfg->i2s_caps.ctrl_base_addr) {
				ret = -ENOMEM;
				goto exit;
			}
			break;

		case AVS_HW_CFG_GATEWAY_COUNT:
			cfg->gateway_count = *tlv->value;
			break;

		case AVS_HW_CFG_HP_EBB_COUNT:
			cfg->hp_ebb_count = *tlv->value;
			break;

		case AVS_HW_CFG_LP_EBB_COUNT:
			cfg->lp_ebb_count = *tlv->value;
			break;

		case AVS_HW_CFG_EBB_SIZE_BYTES:
			cfg->ebb_size_bytes = *tlv->value;
			break;

		case AVS_HW_CFG_GPDMA_CAPS:
			break;

		default:
			dev_info(adev->dev, "Unrecognized hw config: %d\n", tlv->type);
			break;
		}

		offset += sizeof(*tlv) + tlv->length;
	}

exit:
	/* No longer needed, free it as it's owned by the get_large_config() caller. */
	kfree(payload);
	return ret;
}

int avs_ipc_get_modules_info(struct avs_dev *adev, struct avs_mods_info **info)
{
	size_t payload_size;
	u8 *payload;
	int ret;

	ret = avs_ipc_get_large_config(adev, AVS_BASEFW_MOD_ID, AVS_BASEFW_INST_ID,
				       AVS_BASEFW_MODULES_INFO, NULL, 0,
				       &payload, &payload_size);
	if (ret)
		return ret;

	*info = (struct avs_mods_info *)payload;
	return 0;
}

int avs_ipc_set_enable_logs(struct avs_dev *adev, u8 *log_info, size_t size)
{
	int ret;

	ret = avs_ipc_set_large_config(adev, AVS_BASEFW_MOD_ID, AVS_BASEFW_INST_ID,
				       AVS_BASEFW_ENABLE_LOGS, log_info, size);
	if (ret)
		dev_err(adev->dev, "enable logs failed: %d\n", ret);

	return ret;
}

int avs_ipc_set_system_time(struct avs_dev *adev)
{
	struct avs_sys_time sys_time;
	int ret;
	u64 us;

	/* firmware expects UTC time in micro seconds */
	us = ktime_to_us(ktime_get());
	sys_time.val_l = us & UINT_MAX;
	sys_time.val_u = us >> 32;

	ret = avs_ipc_set_large_config(adev, AVS_BASEFW_MOD_ID, AVS_BASEFW_INST_ID,
				       AVS_BASEFW_SYSTEM_TIME, (u8 *)&sys_time, sizeof(sys_time));
	if (ret)
		dev_err(adev->dev, "set system time failed: %d\n", ret);

	return ret;
}

int avs_ipc_copier_set_sink_format(struct avs_dev *adev, u16 module_id,
				   u8 instance_id, u32 sink_id,
				   const struct avs_audio_format *src_fmt,
				   const struct avs_audio_format *sink_fmt)
{
	struct avs_copier_sink_format cpr_fmt;

	cpr_fmt.sink_id = sink_id;
	/* Firmware expects driver to resend copier's input format. */
	cpr_fmt.src_fmt = *src_fmt;
	cpr_fmt.sink_fmt = *sink_fmt;

	return avs_ipc_set_large_config(adev, module_id, instance_id,
					AVS_COPIER_SET_SINK_FORMAT,
					(u8 *)&cpr_fmt, sizeof(cpr_fmt));
}
