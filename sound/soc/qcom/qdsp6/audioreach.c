// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Linaro Limited

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/soc/qcom/apr.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <dt-bindings/soc/qcom,gpr.h>
#include "q6apm.h"
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

#define APM_MFC_CFG_PSIZE(p, n) ALIGN(struct_size(p, channel_mapping, n), 4)

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

struct apm_display_port_module_intf_cfg {
	struct apm_module_param_data param_data;
	struct param_id_display_port_intf_cfg cfg;
} __packed;
#define APM_DP_INTF_CFG_PSIZE ALIGN(sizeof(struct apm_display_port_module_intf_cfg), 8)

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

static void audioreach_set_channel_mapping(u8 *ch_map, int num_channels)
{
	if (num_channels == 1) {
		ch_map[0] =  PCM_CHANNEL_FL;
	} else if (num_channels == 2) {
		ch_map[0] =  PCM_CHANNEL_FL;
		ch_map[1] =  PCM_CHANNEL_FR;
	} else if (num_channels == 4) {
		ch_map[0] =  PCM_CHANNEL_FL;
		ch_map[1] =  PCM_CHANNEL_FR;
		ch_map[2] =  PCM_CHANNEL_LS;
		ch_map[3] =  PCM_CHANNEL_RS;
	}
}

static void apm_populate_container_config(struct apm_container_obj *cfg,
					  struct audioreach_container *cont)
{

	/* Container Config */
	cfg->container_cfg.container_id = cont->container_id;
	cfg->container_cfg.num_prop = 4;

	/* Capability list */
	cfg->cap_data.prop_id = APM_CONTAINER_PROP_ID_CAPABILITY_LIST;
	cfg->cap_data.prop_size = APM_CONTAINER_PROP_ID_CAPABILITY_SIZE;
	cfg->num_capability_id = 1;
	cfg->capability_id = cont->capability_id;

	/* Graph Position */
	cfg->pos_data.prop_id = APM_CONTAINER_PROP_ID_GRAPH_POS;
	cfg->pos_data.prop_size = sizeof(struct apm_cont_prop_id_graph_pos);
	cfg->pos.graph_pos = cont->graph_pos;

	/* Stack size */
	cfg->stack_data.prop_id = APM_CONTAINER_PROP_ID_STACK_SIZE;
	cfg->stack_data.prop_size = sizeof(struct apm_cont_prop_id_stack_size);
	cfg->stack.stack_size = cont->stack_size;

	/* Proc domain */
	cfg->domain_data.prop_id = APM_CONTAINER_PROP_ID_PROC_DOMAIN;
	cfg->domain_data.prop_size = sizeof(struct apm_cont_prop_id_domain);
	cfg->domain.proc_domain = cont->proc_domain;
}

static void apm_populate_sub_graph_config(struct apm_sub_graph_data *cfg,
					  struct audioreach_sub_graph *sg)
{
	cfg->sub_graph_cfg.sub_graph_id = sg->sub_graph_id;
	cfg->sub_graph_cfg.num_sub_graph_prop = APM_SUB_GRAPH_CFG_NPROP;

	/* Perf Mode */
	cfg->perf_data.prop_id = APM_SUB_GRAPH_PROP_ID_PERF_MODE;
	cfg->perf_data.prop_size = APM_SG_PROP_ID_PERF_MODE_SIZE;
	cfg->perf.perf_mode = sg->perf_mode;

	/* Direction */
	cfg->dir_data.prop_id = APM_SUB_GRAPH_PROP_ID_DIRECTION;
	cfg->dir_data.prop_size = APM_SG_PROP_ID_DIR_SIZE;
	cfg->dir.direction = sg->direction;

	/* Scenario ID */
	cfg->sid_data.prop_id = APM_SUB_GRAPH_PROP_ID_SCENARIO_ID;
	cfg->sid_data.prop_size = APM_SG_PROP_ID_SID_SIZE;
	cfg->sid.scenario_id = sg->scenario_id;
}

static void apm_populate_module_prop_obj(struct apm_mod_prop_obj *obj,
					 struct audioreach_module *module)
{

	obj->instance_id = module->instance_id;
	obj->num_props = 1;
	obj->prop_data_1.prop_id = APM_MODULE_PROP_ID_PORT_INFO;
	obj->prop_data_1.prop_size = APM_MODULE_PROP_ID_PORT_INFO_SZ;
	obj->prop_id_port.max_ip_port = module->max_ip_port;
	obj->prop_id_port.max_op_port = module->max_op_port;
}

static void apm_populate_module_list_obj(struct apm_mod_list_obj *obj,
					 struct audioreach_container *container,
					 int sub_graph_id)
{
	struct audioreach_module *module;
	int i;

	obj->sub_graph_id = sub_graph_id;
	obj->container_id = container->container_id;
	obj->num_modules = container->num_modules;
	i = 0;
	list_for_each_entry(module, &container->modules_list, node) {
		obj->mod_cfg[i].module_id = module->module_id;
		obj->mod_cfg[i].instance_id = module->instance_id;
		i++;
	}
}

static void audioreach_populate_graph(struct q6apm *apm, struct audioreach_graph_info *info,
				      struct apm_graph_open_params *open,
				      struct list_head *sg_list,
				      int num_sub_graphs)
{
	struct apm_mod_conn_list_params *mc_data = open->mod_conn_list_data;
	struct apm_module_list_params *ml_data = open->mod_list_data;
	struct apm_prop_list_params *mp_data = open->mod_prop_data;
	struct apm_container_params *c_data = open->cont_data;
	struct apm_sub_graph_params *sg_data = open->sg_data;
	int ncontainer = 0, nmodule = 0, nconn = 0;
	struct apm_mod_prop_obj *module_prop_obj;
	struct audioreach_container *container;
	struct apm_module_conn_obj *conn_obj;
	struct audioreach_module *module;
	struct audioreach_sub_graph *sg;
	struct apm_container_obj *cobj;
	struct apm_mod_list_obj *mlobj;
	int i = 0;

	mlobj = &ml_data->mod_list_obj[0];


	if (info->dst_mod_inst_id && info->src_mod_inst_id) {
		conn_obj = &mc_data->conn_obj[nconn];
		conn_obj->src_mod_inst_id = info->src_mod_inst_id;
		conn_obj->src_mod_op_port_id = info->src_mod_op_port_id;
		conn_obj->dst_mod_inst_id = info->dst_mod_inst_id;
		conn_obj->dst_mod_ip_port_id = info->dst_mod_ip_port_id;
		nconn++;
	}

	list_for_each_entry(sg, sg_list, node) {
		struct apm_sub_graph_data *sg_cfg = &sg_data->sg_cfg[i++];

		apm_populate_sub_graph_config(sg_cfg, sg);

		list_for_each_entry(container, &sg->container_list, node) {
			cobj = &c_data->cont_obj[ncontainer];

			apm_populate_container_config(cobj, container);
			apm_populate_module_list_obj(mlobj, container, sg->sub_graph_id);

			list_for_each_entry(module, &container->modules_list, node) {
				int pn;

				module_prop_obj = &mp_data->mod_prop_obj[nmodule++];
				apm_populate_module_prop_obj(module_prop_obj, module);

				if (!module->max_op_port)
					continue;

				for (pn = 0; pn < module->max_op_port; pn++) {
					if (module->dst_mod_inst_id[pn]) {
						conn_obj = &mc_data->conn_obj[nconn];
						conn_obj->src_mod_inst_id = module->instance_id;
						conn_obj->src_mod_op_port_id =
								module->src_mod_op_port_id[pn];
						conn_obj->dst_mod_inst_id =
								module->dst_mod_inst_id[pn];
						conn_obj->dst_mod_ip_port_id =
								module->dst_mod_ip_port_id[pn];
						nconn++;
					}
				}
			}
			mlobj = (void *) mlobj + APM_MOD_LIST_OBJ_PSIZE(mlobj,
									container->num_modules);

			ncontainer++;
		}
	}
}

void *audioreach_alloc_graph_pkt(struct q6apm *apm, struct audioreach_graph_info *info)
{
	int payload_size, sg_sz, cont_sz, ml_sz, mp_sz, mc_sz;
	struct apm_module_param_data  *param_data;
	struct apm_container_params *cont_params;
	struct audioreach_container *container;
	struct apm_sub_graph_params *sg_params;
	struct apm_mod_conn_list_params *mcon;
	struct apm_graph_open_params params;
	struct apm_prop_list_params *mprop;
	struct audioreach_module *module;
	struct audioreach_sub_graph *sgs;
	struct apm_mod_list_obj *mlobj;
	struct list_head *sg_list;
	int num_connections = 0;
	int num_containers = 0;
	int num_sub_graphs = 0;
	int num_modules = 0;
	int num_modules_list;
	struct gpr_pkt *pkt;
	void *p;

	sg_list = &info->sg_list;
	ml_sz = 0;

	/* add FE-BE connections */
	if (info->dst_mod_inst_id && info->src_mod_inst_id)
		num_connections++;

	list_for_each_entry(sgs, sg_list, node) {
		num_sub_graphs++;
		list_for_each_entry(container, &sgs->container_list, node) {
			num_containers++;
			num_modules += container->num_modules;
			ml_sz = ml_sz + sizeof(struct apm_module_list_params) +
				APM_MOD_LIST_OBJ_PSIZE(mlobj, container->num_modules);

			list_for_each_entry(module, &container->modules_list, node) {
				num_connections += module->num_connections;
			}
		}
	}

	num_modules_list = num_containers;
	sg_sz = APM_SUB_GRAPH_PSIZE(sg_params, num_sub_graphs);
	cont_sz = APM_CONTAINER_PSIZE(cont_params, num_containers);

	ml_sz = ALIGN(ml_sz, 8);

	mp_sz = APM_MOD_PROP_PSIZE(mprop, num_modules);
	mc_sz =	APM_MOD_CONN_PSIZE(mcon, num_connections);

	payload_size = sg_sz + cont_sz + ml_sz + mp_sz + mc_sz;
	pkt = audioreach_alloc_apm_cmd_pkt(payload_size, APM_CMD_GRAPH_OPEN, 0);
	if (IS_ERR(pkt))
		return pkt;

	p = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	/* SubGraph */
	params.sg_data = p;
	param_data = &params.sg_data->param_data;
	param_data->module_instance_id = APM_MODULE_INSTANCE_ID;
	param_data->param_id = APM_PARAM_ID_SUB_GRAPH_CONFIG;
	param_data->param_size = sg_sz - APM_MODULE_PARAM_DATA_SIZE;
	params.sg_data->num_sub_graphs = num_sub_graphs;
	p += sg_sz;

	/* Container */
	params.cont_data = p;
	param_data = &params.cont_data->param_data;
	param_data->module_instance_id = APM_MODULE_INSTANCE_ID;
	param_data->param_id = APM_PARAM_ID_CONTAINER_CONFIG;
	param_data->param_size = cont_sz - APM_MODULE_PARAM_DATA_SIZE;
	params.cont_data->num_containers = num_containers;
	p += cont_sz;

	/* Module List*/
	params.mod_list_data = p;
	param_data = &params.mod_list_data->param_data;
	param_data->module_instance_id = APM_MODULE_INSTANCE_ID;
	param_data->param_id = APM_PARAM_ID_MODULE_LIST;
	param_data->param_size = ml_sz - APM_MODULE_PARAM_DATA_SIZE;
	params.mod_list_data->num_modules_list = num_modules_list;
	p += ml_sz;

	/* Module Properties */
	params.mod_prop_data = p;
	param_data = &params.mod_prop_data->param_data;
	param_data->module_instance_id = APM_MODULE_INSTANCE_ID;
	param_data->param_id = APM_PARAM_ID_MODULE_PROP;
	param_data->param_size = mp_sz - APM_MODULE_PARAM_DATA_SIZE;
	params.mod_prop_data->num_modules_prop_cfg = num_modules;
	p += mp_sz;

	/* Module Connections */
	params.mod_conn_list_data = p;
	param_data = &params.mod_conn_list_data->param_data;
	param_data->module_instance_id = APM_MODULE_INSTANCE_ID;
	param_data->param_id = APM_PARAM_ID_MODULE_CONN;
	param_data->param_size = mc_sz - APM_MODULE_PARAM_DATA_SIZE;
	params.mod_conn_list_data->num_connections = num_connections;
	p += mc_sz;

	audioreach_populate_graph(apm, info, &params, sg_list, num_sub_graphs);

	return pkt;
}
EXPORT_SYMBOL_GPL(audioreach_alloc_graph_pkt);

int audioreach_send_cmd_sync(struct device *dev, gpr_device_t *gdev,
			     struct gpr_ibasic_rsp_result_t *result, struct mutex *cmd_lock,
			     gpr_port_t *port, wait_queue_head_t *cmd_wait,
			     struct gpr_pkt *pkt, uint32_t rsp_opcode)
{

	struct gpr_hdr *hdr = &pkt->hdr;
	int rc;

	mutex_lock(cmd_lock);
	result->opcode = 0;
	result->status = 0;

	if (port)
		rc = gpr_send_port_pkt(port, pkt);
	else if (gdev)
		rc = gpr_send_pkt(gdev, pkt);
	else
		rc = -EINVAL;

	if (rc < 0)
		goto err;

	if (rsp_opcode)
		rc = wait_event_timeout(*cmd_wait, (result->opcode == hdr->opcode) ||
					(result->opcode == rsp_opcode),	5 * HZ);
	else
		rc = wait_event_timeout(*cmd_wait, (result->opcode == hdr->opcode), 5 * HZ);

	if (!rc) {
		dev_err(dev, "CMD timeout for [%x] opcode\n", hdr->opcode);
		rc = -ETIMEDOUT;
	} else if (result->status > 0) {
		dev_err(dev, "DSP returned error[%x] %x\n", hdr->opcode, result->status);
		rc = -EINVAL;
	} else {
		/* DSP successfully finished the command */
		rc = 0;
	}

err:
	mutex_unlock(cmd_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(audioreach_send_cmd_sync);

int audioreach_graph_send_cmd_sync(struct q6apm_graph *graph, struct gpr_pkt *pkt,
				   uint32_t rsp_opcode)
{

	return audioreach_send_cmd_sync(graph->dev, NULL,  &graph->result, &graph->lock,
					graph->port, &graph->cmd_wait, pkt, rsp_opcode);
}
EXPORT_SYMBOL_GPL(audioreach_graph_send_cmd_sync);

static int audioreach_display_port_set_media_format(struct q6apm_graph *graph,
						    struct audioreach_module *module,
						    struct audioreach_module_config *cfg)
{
	struct apm_display_port_module_intf_cfg *intf_cfg;
	struct apm_module_frame_size_factor_cfg *fs_cfg;
	struct apm_module_param_data *param_data;
	struct apm_module_hw_ep_mf_cfg *hw_cfg;
	int ic_sz, ep_sz, fs_sz, dl_sz;
	int rc, payload_size;
	struct gpr_pkt *pkt;
	void *p;

	ic_sz = APM_DP_INTF_CFG_PSIZE;
	ep_sz = APM_HW_EP_CFG_PSIZE;
	fs_sz = APM_FS_CFG_PSIZE;
	dl_sz = 0;

	payload_size = ic_sz + ep_sz + fs_sz + dl_sz;

	pkt = audioreach_alloc_apm_cmd_pkt(payload_size, APM_CMD_SET_CFG, 0);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	p = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	hw_cfg = p;
	param_data = &hw_cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_HW_EP_MF_CFG;
	param_data->param_size = ep_sz - APM_MODULE_PARAM_DATA_SIZE;

	hw_cfg->mf.sample_rate = cfg->sample_rate;
	hw_cfg->mf.bit_width = cfg->bit_width;
	hw_cfg->mf.num_channels = cfg->num_channels;
	hw_cfg->mf.data_format = module->data_format;
	p += ep_sz;

	fs_cfg = p;
	param_data = &fs_cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_HW_EP_FRAME_SIZE_FACTOR;
	param_data->param_size = fs_sz - APM_MODULE_PARAM_DATA_SIZE;
	fs_cfg->frame_size_factor = 1;
	p += fs_sz;

	intf_cfg = p;
	param_data = &intf_cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_DISPLAY_PORT_INTF_CFG;
	param_data->param_size = ic_sz - APM_MODULE_PARAM_DATA_SIZE;

	intf_cfg->cfg.channel_allocation = cfg->channel_allocation;
	intf_cfg->cfg.mst_idx = 0;
	intf_cfg->cfg.dptx_idx = cfg->dp_idx;

	rc = q6apm_send_cmd_sync(graph->apm, pkt, 0);

	kfree(pkt);

	return rc;
}

/* LPASS Codec DMA port Module Media Format Setup */
static int audioreach_codec_dma_set_media_format(struct q6apm_graph *graph,
						 struct audioreach_module *module,
						 struct audioreach_module_config *cfg)
{
	struct apm_codec_dma_module_intf_cfg *intf_cfg;
	struct apm_module_frame_size_factor_cfg *fs_cfg;
	struct apm_module_hw_ep_power_mode_cfg *pm_cfg;
	struct apm_module_param_data *param_data;
	struct apm_module_hw_ep_mf_cfg *hw_cfg;
	int ic_sz, ep_sz, fs_sz, pm_sz, dl_sz;
	int rc, payload_size;
	struct gpr_pkt *pkt;
	void *p;

	ic_sz = APM_CDMA_INTF_CFG_PSIZE;
	ep_sz = APM_HW_EP_CFG_PSIZE;
	fs_sz = APM_FS_CFG_PSIZE;
	pm_sz = APM_HW_EP_PMODE_CFG_PSIZE;
	dl_sz = 0;

	payload_size = ic_sz + ep_sz + fs_sz + pm_sz + dl_sz;

	pkt = audioreach_alloc_apm_cmd_pkt(payload_size, APM_CMD_SET_CFG, 0);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	p = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	hw_cfg = p;
	param_data = &hw_cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_HW_EP_MF_CFG;
	param_data->param_size = ep_sz - APM_MODULE_PARAM_DATA_SIZE;

	hw_cfg->mf.sample_rate = cfg->sample_rate;
	hw_cfg->mf.bit_width = cfg->bit_width;
	hw_cfg->mf.num_channels = cfg->num_channels;
	hw_cfg->mf.data_format = module->data_format;
	p += ep_sz;

	fs_cfg = p;
	param_data = &fs_cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_HW_EP_FRAME_SIZE_FACTOR;
	param_data->param_size = fs_sz - APM_MODULE_PARAM_DATA_SIZE;
	fs_cfg->frame_size_factor = 1;
	p += fs_sz;

	intf_cfg = p;
	param_data = &intf_cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_CODEC_DMA_INTF_CFG;
	param_data->param_size = ic_sz - APM_MODULE_PARAM_DATA_SIZE;

	intf_cfg->cfg.lpaif_type = module->hw_interface_type;
	intf_cfg->cfg.intf_index = module->hw_interface_idx;
	intf_cfg->cfg.active_channels_mask = (1 << cfg->num_channels) - 1;
	p += ic_sz;

	pm_cfg = p;
	param_data = &pm_cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_HW_EP_POWER_MODE_CFG;
	param_data->param_size = pm_sz - APM_MODULE_PARAM_DATA_SIZE;
	pm_cfg->power_mode.power_mode = 0;

	rc = q6apm_send_cmd_sync(graph->apm, pkt, 0);

	kfree(pkt);

	return rc;
}

int audioreach_send_u32_param(struct q6apm_graph *graph, struct audioreach_module *module,
			      uint32_t param_id, uint32_t param_val)
{
	struct apm_module_param_data *param_data;
	struct gpr_pkt *pkt;
	uint32_t *param;
	int rc, payload_size;
	void *p;

	payload_size = sizeof(uint32_t) + APM_MODULE_PARAM_DATA_SIZE;
	p = audioreach_alloc_apm_cmd_pkt(payload_size, APM_CMD_SET_CFG, 0);
	if (IS_ERR(p))
		return -ENOMEM;

	pkt = p;
	p = p + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = p;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = param_id;
	param_data->param_size = sizeof(uint32_t);

	p = p + APM_MODULE_PARAM_DATA_SIZE;
	param = p;
	*param = param_val;

	rc = q6apm_send_cmd_sync(graph->apm, pkt, 0);

	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(audioreach_send_u32_param);

static int audioreach_sal_limiter_enable(struct q6apm_graph *graph,
					 struct audioreach_module *module, bool enable)
{
	return audioreach_send_u32_param(graph, module, PARAM_ID_SAL_LIMITER_ENABLE, enable);
}

static int audioreach_sal_set_media_format(struct q6apm_graph *graph,
					   struct audioreach_module *module,
					   struct audioreach_module_config *cfg)
{
	return audioreach_send_u32_param(graph, module, PARAM_ID_SAL_OUTPUT_CFG,  cfg->bit_width);
}

static int audioreach_module_enable(struct q6apm_graph *graph,
				    struct audioreach_module *module,
				    bool enable)
{
	return audioreach_send_u32_param(graph, module, PARAM_ID_MODULE_ENABLE, enable);
}

static int audioreach_gapless_set_media_format(struct q6apm_graph *graph,
					       struct audioreach_module *module,
					       struct audioreach_module_config *cfg)
{
	return audioreach_send_u32_param(graph, module, PARAM_ID_EARLY_EOS_DELAY,
					 EARLY_EOS_DELAY_MS);
}

static int audioreach_mfc_set_media_format(struct q6apm_graph *graph,
					   struct audioreach_module *module,
					   struct audioreach_module_config *cfg)
{
	struct apm_module_param_data *param_data;
	struct param_id_mfc_media_format *media_format;
	uint32_t num_channels = cfg->num_channels;
	int payload_size;
	struct gpr_pkt *pkt;
	int rc;
	void *p;

	payload_size = APM_MFC_CFG_PSIZE(media_format, num_channels) +
		APM_MODULE_PARAM_DATA_SIZE;

	pkt = audioreach_alloc_apm_cmd_pkt(payload_size, APM_CMD_SET_CFG, 0);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	p = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = p;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT;
	param_data->param_size = APM_MFC_CFG_PSIZE(media_format, num_channels);
	p = p + APM_MODULE_PARAM_DATA_SIZE;
	media_format = p;

	media_format->sample_rate = cfg->sample_rate;
	media_format->bit_width = cfg->bit_width;
	media_format->num_channels = cfg->num_channels;

	if (num_channels == 1) {
		media_format->channel_mapping[0] = PCM_CHANNEL_FL;
	} else if (num_channels == 2) {
		media_format->channel_mapping[0] = PCM_CHANNEL_FL;
		media_format->channel_mapping[1] = PCM_CHANNEL_FR;
	} else if (num_channels == 4) {
		media_format->channel_mapping[0] = PCM_CHANNEL_FL;
		media_format->channel_mapping[1] = PCM_CHANNEL_FR;
		media_format->channel_mapping[2] = PCM_CHANNEL_LS;
		media_format->channel_mapping[3] = PCM_CHANNEL_RS;
	}

	rc = q6apm_send_cmd_sync(graph->apm, pkt, 0);

	kfree(pkt);

	return rc;
}

static int audioreach_set_compr_media_format(struct media_format *media_fmt_hdr,
					     void *p, struct audioreach_module_config *mcfg)
{
	struct payload_media_fmt_aac_t *aac_cfg;
	struct payload_media_fmt_pcm *mp3_cfg;
	struct payload_media_fmt_flac_t *flac_cfg;

	switch (mcfg->fmt) {
	case SND_AUDIOCODEC_MP3:
		media_fmt_hdr->data_format = DATA_FORMAT_RAW_COMPRESSED;
		media_fmt_hdr->fmt_id = MEDIA_FMT_ID_MP3;
		media_fmt_hdr->payload_size = 0;
		p = p + sizeof(*media_fmt_hdr);
		mp3_cfg = p;
		mp3_cfg->sample_rate = mcfg->sample_rate;
		mp3_cfg->bit_width = mcfg->bit_width;
		mp3_cfg->alignment = PCM_LSB_ALIGNED;
		mp3_cfg->bits_per_sample = mcfg->bit_width;
		mp3_cfg->q_factor = mcfg->bit_width - 1;
		mp3_cfg->endianness = PCM_LITTLE_ENDIAN;
		mp3_cfg->num_channels = mcfg->num_channels;

		audioreach_set_channel_mapping(mp3_cfg->channel_mapping,
					       mcfg->num_channels);
		break;
	case SND_AUDIOCODEC_AAC:
		media_fmt_hdr->data_format = DATA_FORMAT_RAW_COMPRESSED;
		media_fmt_hdr->fmt_id = MEDIA_FMT_ID_AAC;
		media_fmt_hdr->payload_size = sizeof(struct payload_media_fmt_aac_t);
		p = p + sizeof(*media_fmt_hdr);
		aac_cfg = p;
		aac_cfg->aac_fmt_flag = 0;
		aac_cfg->audio_obj_type = 5;
		aac_cfg->num_channels = mcfg->num_channels;
		aac_cfg->total_size_of_PCE_bits = 0;
		aac_cfg->sample_rate = mcfg->sample_rate;
		break;
	case SND_AUDIOCODEC_FLAC:
		media_fmt_hdr->data_format = DATA_FORMAT_RAW_COMPRESSED;
		media_fmt_hdr->fmt_id = MEDIA_FMT_ID_FLAC;
		media_fmt_hdr->payload_size = sizeof(struct payload_media_fmt_flac_t);
		p = p + sizeof(*media_fmt_hdr);
		flac_cfg = p;
		flac_cfg->sample_size = mcfg->codec.options.flac_d.sample_size;
		flac_cfg->num_channels = mcfg->num_channels;
		flac_cfg->min_blk_size = mcfg->codec.options.flac_d.min_blk_size;
		flac_cfg->max_blk_size = mcfg->codec.options.flac_d.max_blk_size;
		flac_cfg->sample_rate = mcfg->sample_rate;
		flac_cfg->min_frame_size = mcfg->codec.options.flac_d.min_frame_size;
		flac_cfg->max_frame_size = mcfg->codec.options.flac_d.max_frame_size;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int audioreach_compr_set_param(struct q6apm_graph *graph, struct audioreach_module_config *mcfg)
{
	struct media_format *header;
	struct gpr_pkt *pkt;
	int iid, payload_size, rc;
	void *p;

	payload_size = sizeof(struct apm_sh_module_media_fmt_cmd);

	iid = q6apm_graph_get_rx_shmem_module_iid(graph);
	pkt = audioreach_alloc_cmd_pkt(payload_size, DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT,
			0, graph->port->id, iid);

	if (IS_ERR(pkt))
		return -ENOMEM;

	p = (void *)pkt + GPR_HDR_SIZE;
	header = p;
	rc = audioreach_set_compr_media_format(header, p, mcfg);
	if (rc) {
		kfree(pkt);
		return rc;
	}

	rc = gpr_send_port_pkt(graph->port, pkt);
	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(audioreach_compr_set_param);

static int audioreach_i2s_set_media_format(struct q6apm_graph *graph,
					   struct audioreach_module *module,
					   struct audioreach_module_config *cfg)
{
	struct apm_module_frame_size_factor_cfg *fs_cfg;
	struct apm_module_param_data *param_data;
	struct apm_i2s_module_intf_cfg *intf_cfg;
	struct apm_module_hw_ep_mf_cfg *hw_cfg;
	int ic_sz, ep_sz, fs_sz;
	int rc, payload_size;
	struct gpr_pkt *pkt;
	void *p;

	ic_sz = APM_I2S_INTF_CFG_PSIZE;
	ep_sz = APM_HW_EP_CFG_PSIZE;
	fs_sz = APM_FS_CFG_PSIZE;

	payload_size = ic_sz + ep_sz + fs_sz;

	pkt = audioreach_alloc_apm_cmd_pkt(payload_size, APM_CMD_SET_CFG, 0);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	p = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;
	intf_cfg = p;

	param_data = &intf_cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_I2S_INTF_CFG;
	param_data->param_size = ic_sz - APM_MODULE_PARAM_DATA_SIZE;

	intf_cfg->cfg.intf_idx = module->hw_interface_idx;
	intf_cfg->cfg.sd_line_idx = module->sd_line_idx;

	switch (cfg->fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		intf_cfg->cfg.ws_src = CONFIG_I2S_WS_SRC_INTERNAL;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		/* CPU is slave */
		intf_cfg->cfg.ws_src = CONFIG_I2S_WS_SRC_EXTERNAL;
		break;
	default:
		break;
	}

	p += ic_sz;
	hw_cfg = p;
	param_data = &hw_cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_HW_EP_MF_CFG;
	param_data->param_size = ep_sz - APM_MODULE_PARAM_DATA_SIZE;

	hw_cfg->mf.sample_rate = cfg->sample_rate;
	hw_cfg->mf.bit_width = cfg->bit_width;
	hw_cfg->mf.num_channels = cfg->num_channels;
	hw_cfg->mf.data_format = module->data_format;

	p += ep_sz;
	fs_cfg = p;
	param_data = &fs_cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_HW_EP_FRAME_SIZE_FACTOR;
	param_data->param_size = fs_sz - APM_MODULE_PARAM_DATA_SIZE;
	fs_cfg->frame_size_factor = 1;

	rc = q6apm_send_cmd_sync(graph->apm, pkt, 0);

	kfree(pkt);

	return rc;
}

static int audioreach_logging_set_media_format(struct q6apm_graph *graph,
					       struct audioreach_module *module)
{
	struct apm_module_param_data *param_data;
	struct data_logging_config *cfg;
	int rc, payload_size;
	struct gpr_pkt *pkt;
	void *p;

	payload_size = sizeof(*cfg) + APM_MODULE_PARAM_DATA_SIZE;
	pkt = audioreach_alloc_apm_cmd_pkt(payload_size, APM_CMD_SET_CFG, 0);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	p = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = p;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_DATA_LOGGING_CONFIG;
	param_data->param_size = payload_size - APM_MODULE_PARAM_DATA_SIZE;

	p = p + APM_MODULE_PARAM_DATA_SIZE;
	cfg = p;
	cfg->log_code = module->log_code;
	cfg->log_tap_point_id = module->log_tap_point_id;
	cfg->mode = module->log_mode;

	rc = q6apm_send_cmd_sync(graph->apm, pkt, 0);

	kfree(pkt);

	return rc;
}

static int audioreach_pcm_set_media_format(struct q6apm_graph *graph,
					   struct audioreach_module *module,
					   struct audioreach_module_config *mcfg)
{
	struct payload_pcm_output_format_cfg *media_cfg;
	uint32_t num_channels = mcfg->num_channels;
	struct apm_pcm_module_media_fmt_cmd *cfg;
	struct apm_module_param_data *param_data;
	int rc, payload_size;
	struct gpr_pkt *pkt;

	if (num_channels > 4) {
		dev_err(graph->dev, "Error: Invalid channels (%d)!\n", num_channels);
		return -EINVAL;
	}

	payload_size = APM_PCM_MODULE_FMT_CMD_PSIZE(num_channels);

	pkt = audioreach_alloc_apm_cmd_pkt(payload_size, APM_CMD_SET_CFG, 0);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	cfg = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = &cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_PCM_OUTPUT_FORMAT_CFG;
	param_data->param_size = payload_size - APM_MODULE_PARAM_DATA_SIZE;

	cfg->header.data_format = DATA_FORMAT_FIXED_POINT;
	cfg->header.fmt_id = MEDIA_FMT_ID_PCM;
	cfg->header.payload_size = APM_PCM_OUT_FMT_CFG_PSIZE(media_cfg, num_channels);

	media_cfg = &cfg->media_cfg;
	media_cfg->alignment = PCM_LSB_ALIGNED;
	media_cfg->bit_width = mcfg->bit_width;
	media_cfg->endianness = PCM_LITTLE_ENDIAN;
	media_cfg->interleaved = module->interleave_type;
	media_cfg->num_channels = mcfg->num_channels;
	media_cfg->q_factor = mcfg->bit_width - 1;
	media_cfg->bits_per_sample = mcfg->bit_width;

	audioreach_set_channel_mapping(media_cfg->channel_mapping,
				       num_channels);

	rc = q6apm_send_cmd_sync(graph->apm, pkt, 0);

	kfree(pkt);

	return rc;
}

static int audioreach_shmem_set_media_format(struct q6apm_graph *graph,
					     struct audioreach_module *module,
					     struct audioreach_module_config *mcfg)
{
	uint32_t num_channels = mcfg->num_channels;
	struct apm_module_param_data *param_data;
	struct payload_media_fmt_pcm *cfg;
	struct media_format *header;
	int rc, payload_size;
	struct gpr_pkt *pkt;
	void *p;

	if (num_channels > 4) {
		dev_err(graph->dev, "Error: Invalid channels (%d)!\n", num_channels);
		return -EINVAL;
	}

	payload_size = APM_SHMEM_FMT_CFG_PSIZE(num_channels) + APM_MODULE_PARAM_DATA_SIZE;

	pkt = audioreach_alloc_cmd_pkt(payload_size, APM_CMD_SET_CFG, 0,
				     graph->port->id, module->instance_id);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	p = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = p;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_MEDIA_FORMAT;
	param_data->param_size = payload_size - APM_MODULE_PARAM_DATA_SIZE;
	p = p + APM_MODULE_PARAM_DATA_SIZE;

	header = p;
	if (mcfg->fmt == SND_AUDIOCODEC_PCM) {
		header->data_format = DATA_FORMAT_FIXED_POINT;
		header->fmt_id =  MEDIA_FMT_ID_PCM;
		header->payload_size = payload_size - sizeof(*header);

		p = p + sizeof(*header);
		cfg = p;
		cfg->sample_rate = mcfg->sample_rate;
		cfg->bit_width = mcfg->bit_width;
		cfg->alignment = PCM_LSB_ALIGNED;
		cfg->bits_per_sample = mcfg->bit_width;
		cfg->q_factor = mcfg->bit_width - 1;
		cfg->endianness = PCM_LITTLE_ENDIAN;
		cfg->num_channels = mcfg->num_channels;

		audioreach_set_channel_mapping(cfg->channel_mapping,
					       num_channels);
	} else {
		rc = audioreach_set_compr_media_format(header, p, mcfg);
		if (rc) {
			kfree(pkt);
			return rc;
		}
	}

	rc = audioreach_graph_send_cmd_sync(graph, pkt, 0);

	kfree(pkt);

	return rc;
}

int audioreach_gain_set_vol_ctrl(struct q6apm *apm, struct audioreach_module *module, int vol)
{
	struct param_id_vol_ctrl_master_gain *cfg;
	struct apm_module_param_data *param_data;
	int rc, payload_size;
	struct gpr_pkt *pkt;
	void *p;

	payload_size = sizeof(*cfg) + APM_MODULE_PARAM_DATA_SIZE;
	pkt = audioreach_alloc_apm_cmd_pkt(payload_size, APM_CMD_SET_CFG, 0);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	p = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = p;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_VOL_CTRL_MASTER_GAIN;
	param_data->param_size = payload_size - APM_MODULE_PARAM_DATA_SIZE;

	p = p + APM_MODULE_PARAM_DATA_SIZE;
	cfg = p;
	cfg->master_gain =  vol;
	rc = q6apm_send_cmd_sync(apm, pkt, 0);

	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(audioreach_gain_set_vol_ctrl);

static int audioreach_gain_set(struct q6apm_graph *graph, struct audioreach_module *module)
{
	struct apm_module_param_data *param_data;
	struct apm_gain_module_cfg *cfg;
	int rc, payload_size;
	struct gpr_pkt *pkt;

	payload_size = APM_GAIN_CFG_PSIZE;
	pkt = audioreach_alloc_apm_cmd_pkt(payload_size, APM_CMD_SET_CFG, 0);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	cfg = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = &cfg->param_data;
	param_data->module_instance_id = module->instance_id;
	param_data->error_code = 0;
	param_data->param_id = APM_PARAM_ID_GAIN;
	param_data->param_size = payload_size - APM_MODULE_PARAM_DATA_SIZE;

	cfg->gain_cfg.gain = module->gain;

	rc = q6apm_send_cmd_sync(graph->apm, pkt, 0);

	kfree(pkt);

	return rc;
}

int audioreach_set_media_format(struct q6apm_graph *graph, struct audioreach_module *module,
				struct audioreach_module_config *cfg)
{
	int rc;

	switch (module->module_id) {
	case MODULE_ID_DATA_LOGGING:
		rc = audioreach_module_enable(graph, module, true);
		if (!rc)
			rc = audioreach_logging_set_media_format(graph, module);
		break;
	case MODULE_ID_PCM_DEC:
	case MODULE_ID_PCM_ENC:
	case MODULE_ID_PCM_CNV:
	case MODULE_ID_PLACEHOLDER_DECODER:
	case MODULE_ID_PLACEHOLDER_ENCODER:
		rc = audioreach_pcm_set_media_format(graph, module, cfg);
		break;
	case MODULE_ID_DISPLAY_PORT_SINK:
		rc = audioreach_display_port_set_media_format(graph, module, cfg);
		break;
	case MODULE_ID_I2S_SOURCE:
	case MODULE_ID_I2S_SINK:
		rc = audioreach_i2s_set_media_format(graph, module, cfg);
		break;
	case MODULE_ID_WR_SHARED_MEM_EP:
		rc = audioreach_shmem_set_media_format(graph, module, cfg);
		break;
	case MODULE_ID_GAIN:
		rc = audioreach_gain_set(graph, module);
		break;
	case MODULE_ID_CODEC_DMA_SINK:
	case MODULE_ID_CODEC_DMA_SOURCE:
		rc = audioreach_codec_dma_set_media_format(graph, module, cfg);
		break;
	case MODULE_ID_SAL:
		rc = audioreach_sal_set_media_format(graph, module, cfg);
		if (!rc)
			rc = audioreach_sal_limiter_enable(graph, module, true);
		break;
	case MODULE_ID_MFC:
		rc = audioreach_mfc_set_media_format(graph, module, cfg);
		break;
	case MODULE_ID_GAPLESS:
		rc = audioreach_gapless_set_media_format(graph, module, cfg);
		break;
	default:
		rc = 0;
	}

	return rc;
}
EXPORT_SYMBOL_GPL(audioreach_set_media_format);

void audioreach_graph_free_buf(struct q6apm_graph *graph)
{
	struct audioreach_graph_data *port;

	mutex_lock(&graph->lock);
	port = &graph->rx_data;
	port->num_periods = 0;
	kfree(port->buf);
	port->buf = NULL;

	port = &graph->tx_data;
	port->num_periods = 0;
	kfree(port->buf);
	port->buf = NULL;
	mutex_unlock(&graph->lock);
}
EXPORT_SYMBOL_GPL(audioreach_graph_free_buf);

int audioreach_map_memory_regions(struct q6apm_graph *graph, unsigned int dir, size_t period_sz,
				  unsigned int periods, bool is_contiguous)
{
	struct apm_shared_map_region_payload *mregions;
	struct apm_cmd_shared_mem_map_regions *cmd;
	uint32_t num_regions, buf_sz, payload_size;
	struct audioreach_graph_data *data;
	struct gpr_pkt *pkt;
	void *p;
	int rc, i;

	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		data = &graph->rx_data;
	else
		data = &graph->tx_data;

	if (is_contiguous) {
		num_regions = 1;
		buf_sz = period_sz * periods;
	} else {
		buf_sz = period_sz;
		num_regions = periods;
	}

	/* DSP expects size should be aligned to 4K */
	buf_sz = ALIGN(buf_sz, 4096);

	payload_size = sizeof(*cmd) + (sizeof(*mregions) * num_regions);

	pkt = audioreach_alloc_apm_pkt(payload_size, APM_CMD_SHARED_MEM_MAP_REGIONS, dir,
				     graph->port->id);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	p = (void *)pkt + GPR_HDR_SIZE;
	cmd = p;
	cmd->mem_pool_id = APM_MEMORY_MAP_SHMEM8_4K_POOL;
	cmd->num_regions = num_regions;

	cmd->property_flag = 0x0;

	mregions = p + sizeof(*cmd);

	mutex_lock(&graph->lock);

	for (i = 0; i < num_regions; i++) {
		struct audio_buffer *ab;

		ab = &data->buf[i];
		mregions->shm_addr_lsw = lower_32_bits(ab->phys);
		mregions->shm_addr_msw = upper_32_bits(ab->phys);
		mregions->mem_size_bytes = buf_sz;
		++mregions;
	}
	mutex_unlock(&graph->lock);

	rc = audioreach_graph_send_cmd_sync(graph, pkt, APM_CMD_RSP_SHARED_MEM_MAP_REGIONS);

	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(audioreach_map_memory_regions);

int audioreach_shared_memory_send_eos(struct q6apm_graph *graph)
{
	struct data_cmd_wr_sh_mem_ep_eos *eos;
	struct gpr_pkt *pkt;
	int rc = 0, iid;

	iid = q6apm_graph_get_rx_shmem_module_iid(graph);
	pkt = audioreach_alloc_cmd_pkt(sizeof(*eos), DATA_CMD_WR_SH_MEM_EP_EOS, 0,
				       graph->port->id, iid);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	eos = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	eos->policy = WR_SH_MEM_EP_EOS_POLICY_LAST;

	rc = gpr_send_port_pkt(graph->port, pkt);
	kfree(pkt);

	return rc;
}
EXPORT_SYMBOL_GPL(audioreach_shared_memory_send_eos);
