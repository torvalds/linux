// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Linaro Limited

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/soc/qcom/apr.h>
#include <dt-bindings/soc/qcom,gpr.h>
#include "audioreach.h"

/* SubGraph Config */
struct apm_sub_graph_data {
	struct apm_sub_graph_cfg sub_graph_cfg;
	struct apm_prop_data perf_data;
	struct apm_sg_prop_id_perf_mode perf;
	struct apm_prop_data dir_data;
	struct apm_sg_prop_id_direction dir;
	struct apm_prop_data sid_data;
	struct apm_sg_prop_id_scenario_id sid;

} __packed;

#define APM_SUB_GRAPH_CFG_NPROP	3

struct apm_sub_graph_params  {
	struct apm_module_param_data param_data;
	uint32_t num_sub_graphs;
	struct apm_sub_graph_data sg_cfg[];
} __packed;

#define APM_SUB_GRAPH_PSIZE(p, n) ALIGN(struct_size(p, sg_cfg, n), 8)

/* container config */
struct apm_container_obj  {
	struct apm_container_cfg container_cfg;
	/* Capability ID list */
	struct apm_prop_data cap_data;
	uint32_t num_capability_id;
	uint32_t capability_id;

	/* Container graph Position */
	struct apm_prop_data pos_data;
	struct apm_cont_prop_id_graph_pos pos;

	/* Container Stack size */
	struct apm_prop_data stack_data;
	struct apm_cont_prop_id_stack_size stack;

	/* Container proc domain id */
	struct apm_prop_data domain_data;
	struct apm_cont_prop_id_domain domain;
} __packed;

struct apm_container_params  {
	struct apm_module_param_data param_data;
	uint32_t num_containers;
	struct apm_container_obj cont_obj[];
} __packed;

#define APM_CONTAINER_PSIZE(p, n) ALIGN(struct_size(p, cont_obj, n), 8)

/* Module List config */
struct apm_mod_list_obj {
	/* Modules list cfg */
	uint32_t sub_graph_id;
	uint32_t container_id;
	uint32_t num_modules;
	struct apm_module_obj mod_cfg[];
} __packed;

#define APM_MOD_LIST_OBJ_PSIZE(p, n) struct_size(p, mod_cfg, n)

struct apm_module_list_params {
	struct apm_module_param_data param_data;
	uint32_t num_modules_list;
	/* Module list config array */
	struct apm_mod_list_obj mod_list_obj[];
} __packed;


/* Module Properties */
struct apm_mod_prop_obj {
	u32 instance_id;
	u32 num_props;
	struct apm_prop_data prop_data_1;
	struct apm_module_prop_id_port_info prop_id_port;
} __packed;

struct apm_prop_list_params {
	struct apm_module_param_data param_data;
	u32 num_modules_prop_cfg;
	struct apm_mod_prop_obj mod_prop_obj[];

} __packed;

#define APM_MOD_PROP_PSIZE(p, n) ALIGN(struct_size(p, mod_prop_obj, n), 8)

/* Module Connections */
struct apm_mod_conn_list_params {
	struct apm_module_param_data param_data;
	u32 num_connections;
	struct apm_module_conn_obj conn_obj[];

} __packed;

#define APM_MOD_CONN_PSIZE(p, n) ALIGN(struct_size(p, conn_obj, n), 8)

struct apm_graph_open_params {
	struct apm_cmd_header *cmd_header;
	struct apm_sub_graph_params *sg_data;
	struct apm_container_params *cont_data;
	struct apm_module_list_params *mod_list_data;
	struct apm_prop_list_params *mod_prop_data;
	struct apm_mod_conn_list_params *mod_conn_list_data;
} __packed;

struct apm_pcm_module_media_fmt_cmd {
	struct apm_module_param_data param_data;
	struct param_id_pcm_output_format_cfg header;
	struct payload_pcm_output_format_cfg media_cfg;
} __packed;

struct apm_rd_shmem_module_config_cmd {
	struct apm_module_param_data param_data;
	struct param_id_rd_sh_mem_cfg cfg;
} __packed;

struct apm_sh_module_media_fmt_cmd {
	struct media_format header;
	struct payload_media_fmt_pcm cfg;
} __packed;

#define APM_SHMEM_FMT_CFG_PSIZE(ch) ALIGN( \
				sizeof(struct apm_sh_module_media_fmt_cmd) + \
				ch * sizeof(uint8_t), 8)

/* num of channels as argument */
#define APM_PCM_MODULE_FMT_CMD_PSIZE(ch) ALIGN( \
				sizeof(struct apm_pcm_module_media_fmt_cmd) + \
				ch * sizeof(uint8_t), 8)

#define APM_PCM_OUT_FMT_CFG_PSIZE(p, n) ALIGN(struct_size(p, channel_mapping, n), 4)

struct apm_i2s_module_intf_cfg {
	struct apm_module_param_data param_data;
	struct param_id_i2s_intf_cfg cfg;
} __packed;

#define APM_I2S_INTF_CFG_PSIZE ALIGN(sizeof(struct apm_i2s_module_intf_cfg), 8)

struct apm_module_hw_ep_mf_cfg {
	struct apm_module_param_data param_data;
	struct param_id_hw_ep_mf mf;
} __packed;

#define APM_HW_EP_CFG_PSIZE ALIGN(sizeof(struct apm_module_hw_ep_mf_cfg), 8)

struct apm_module_frame_size_factor_cfg {
	struct apm_module_param_data param_data;
	uint32_t frame_size_factor;
} __packed;

#define APM_FS_CFG_PSIZE ALIGN(sizeof(struct apm_module_frame_size_factor_cfg), 8)

struct apm_module_hw_ep_power_mode_cfg {
	struct apm_module_param_data param_data;
	struct param_id_hw_ep_power_mode_cfg power_mode;
} __packed;

#define APM_HW_EP_PMODE_CFG_PSIZE ALIGN(sizeof(struct apm_module_hw_ep_power_mode_cfg),	8)

struct apm_module_hw_ep_dma_data_align_cfg {
	struct apm_module_param_data param_data;
	struct param_id_hw_ep_dma_data_align align;
} __packed;

#define APM_HW_EP_DALIGN_CFG_PSIZE ALIGN(sizeof(struct apm_module_hw_ep_dma_data_align_cfg), 8)

struct apm_gain_module_cfg {
	struct apm_module_param_data param_data;
	struct param_id_gain_cfg gain_cfg;
} __packed;

#define APM_GAIN_CFG_PSIZE ALIGN(sizeof(struct apm_gain_module_cfg), 8)

struct apm_codec_dma_module_intf_cfg {
	struct apm_module_param_data param_data;
	struct param_id_codec_dma_intf_cfg cfg;
} __packed;

#define APM_CDMA_INTF_CFG_PSIZE ALIGN(sizeof(struct apm_codec_dma_module_intf_cfg), 8)

static void *__audioreach_alloc_pkt(int payload_size, uint32_t opcode, uint32_t token,
				    uint32_t src_port, uint32_t dest_port, bool has_cmd_hdr)
{
	struct gpr_pkt *pkt;
	void *p;
	int pkt_size = GPR_HDR_SIZE + payload_size;

	if (has_cmd_hdr)
		pkt_size += APM_CMD_HDR_SIZE;

	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	pkt = p;
	pkt->hdr.version = GPR_PKT_VER;
	pkt->hdr.hdr_size = GPR_PKT_HEADER_WORD_SIZE;
	pkt->hdr.pkt_size = pkt_size;
	pkt->hdr.dest_port = dest_port;
	pkt->hdr.src_port = src_port;

	pkt->hdr.dest_domain = GPR_DOMAIN_ID_ADSP;
	pkt->hdr.src_domain = GPR_DOMAIN_ID_APPS;
	pkt->hdr.token = token;
	pkt->hdr.opcode = opcode;

	if (has_cmd_hdr) {
		struct apm_cmd_header *cmd_header;

		p = p + GPR_HDR_SIZE;
		cmd_header = p;
		cmd_header->payload_size = payload_size;
	}

	return pkt;
}

void *audioreach_alloc_pkt(int payload_size, uint32_t opcode, uint32_t token,
			   uint32_t src_port, uint32_t dest_port)
{
	return __audioreach_alloc_pkt(payload_size, opcode, token, src_port, dest_port, false);
}
EXPORT_SYMBOL_GPL(audioreach_alloc_pkt);

void *audioreach_alloc_apm_pkt(int pkt_size, uint32_t opcode, uint32_t token, uint32_t src_port)
{
	return __audioreach_alloc_pkt(pkt_size, opcode, token, src_port, APM_MODULE_INSTANCE_ID,
				      false);
}
EXPORT_SYMBOL_GPL(audioreach_alloc_apm_pkt);

void *audioreach_alloc_cmd_pkt(int payload_size, uint32_t opcode, uint32_t token,
			       uint32_t src_port, uint32_t dest_port)
{
	return __audioreach_alloc_pkt(payload_size, opcode, token, src_port, dest_port, true);
}
EXPORT_SYMBOL_GPL(audioreach_alloc_cmd_pkt);

void *audioreach_alloc_apm_cmd_pkt(int pkt_size, uint32_t opcode, uint32_t token)
{
	return __audioreach_alloc_pkt(pkt_size, opcode, token, GPR_APM_MODULE_IID,
				       APM_MODULE_INSTANCE_ID, true);
}
EXPORT_SYMBOL_GPL(audioreach_alloc_apm_cmd_pkt);
