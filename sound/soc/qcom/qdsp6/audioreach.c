// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Linaro Limited

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/soc/qcom/apr.h>
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

static void apm_populate_connection_obj(struct apm_module_conn_obj *obj,
					struct audioreach_module *module)
{
	obj->src_mod_inst_id = module->src_mod_inst_id;
	obj->src_mod_op_port_id = module->src_mod_op_port_id;
	obj->dst_mod_inst_id = module->instance_id;
	obj->dst_mod_ip_port_id = module->in_port;
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

struct audioreach_module *audioreach_get_container_last_module(
							struct audioreach_container *container)
{
	struct audioreach_module *module;

	list_for_each_entry(module, &container->modules_list, node) {
		if (module->dst_mod_inst_id == 0)
			return module;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(audioreach_get_container_last_module);

static bool is_module_in_container(struct audioreach_container *container, int module_iid)
{
	struct audioreach_module *module;

	list_for_each_entry(module, &container->modules_list, node) {
		if (module->instance_id == module_iid)
			return true;
	}

	return false;
}

struct audioreach_module *audioreach_get_container_first_module(
							struct audioreach_container *container)
{
	struct audioreach_module *module;

	/* get the first module from both connected or un-connected containers */
	list_for_each_entry(module, &container->modules_list, node) {
		if (module->src_mod_inst_id == 0 ||
		    !is_module_in_container(container, module->src_mod_inst_id))
			return module;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(audioreach_get_container_first_module);

struct audioreach_module *audioreach_get_container_next_module(
						struct audioreach_container *container,
						struct audioreach_module *module)
{
	int nmodule_iid = module->dst_mod_inst_id;
	struct audioreach_module *nmodule;

	list_for_each_entry(nmodule, &container->modules_list, node) {
		if (nmodule->instance_id == nmodule_iid)
			return nmodule;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(audioreach_get_container_next_module);

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
	list_for_each_container_module(module, container) {
		obj->mod_cfg[i].module_id = module->module_id;
		obj->mod_cfg[i].instance_id = module->instance_id;
		i++;
	}
}

static void audioreach_populate_graph(struct apm_graph_open_params *open,
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

	list_for_each_entry(sg, sg_list, node) {
		struct apm_sub_graph_data *sg_cfg = &sg_data->sg_cfg[i++];

		apm_populate_sub_graph_config(sg_cfg, sg);

		list_for_each_entry(container, &sg->container_list, node) {
			cobj = &c_data->cont_obj[ncontainer];

			apm_populate_container_config(cobj, container);
			apm_populate_module_list_obj(mlobj, container, sg->sub_graph_id);

			list_for_each_container_module(module, container) {
				uint32_t src_mod_inst_id;

				src_mod_inst_id = module->src_mod_inst_id;

				module_prop_obj = &mp_data->mod_prop_obj[nmodule];
				apm_populate_module_prop_obj(module_prop_obj, module);

				if (src_mod_inst_id) {
					conn_obj = &mc_data->conn_obj[nconn];
					apm_populate_connection_obj(conn_obj, module);
					nconn++;
				}

				nmodule++;
			}
			mlobj = (void *) mlobj + APM_MOD_LIST_OBJ_PSIZE(mlobj, container->num_modules);

			ncontainer++;
		}
	}
}

void *audioreach_alloc_graph_pkt(struct q6apm *apm, struct list_head *sg_list, int graph_id)
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
	int num_modules_per_list;
	int num_connections = 0;
	int num_containers = 0;
	int num_sub_graphs = 0;
	int num_modules = 0;
	int num_modules_list;
	struct gpr_pkt *pkt;
	void *p;

	list_for_each_entry(sgs, sg_list, node) {
		num_sub_graphs++;
		list_for_each_entry(container, &sgs->container_list, node) {
			num_containers++;
			num_modules += container->num_modules;
			list_for_each_container_module(module, container) {
				if (module->src_mod_inst_id)
					num_connections++;
			}
		}
	}

	num_modules_list = num_containers;
	num_modules_per_list = num_modules/num_containers;
	sg_sz = APM_SUB_GRAPH_PSIZE(sg_params, num_sub_graphs);
	cont_sz = APM_CONTAINER_PSIZE(cont_params, num_containers);
	ml_sz =	ALIGN(sizeof(struct apm_module_list_params) +
		num_modules_list * APM_MOD_LIST_OBJ_PSIZE(mlobj,  num_modules_per_list), 8);
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
	params.mod_list_data->num_modules_list = num_sub_graphs;
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

	audioreach_populate_graph(&params, sg_list, num_sub_graphs);

	return pkt;
}
EXPORT_SYMBOL_GPL(audioreach_alloc_graph_pkt);
