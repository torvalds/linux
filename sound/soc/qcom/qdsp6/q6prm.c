// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, Linaro Limited

#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/of_platform.h>
#include <linux/jiffies.h>
#include <linux/soc/qcom/apr.h>
#include <dt-bindings/soc/qcom,gpr.h>
#include <dt-bindings/sound/qcom,q6dsp-lpass-ports.h>
#include "q6apm.h"
#include "q6prm.h"
#include "audioreach.h"

struct q6prm {
	struct device *dev;
	gpr_device_t *gdev;
	wait_queue_head_t wait;
	struct gpr_ibasic_rsp_result_t result;
	struct mutex lock;
};

#define PRM_CMD_REQUEST_HW_RSC		0x0100100F
#define PRM_CMD_RSP_REQUEST_HW_RSC	0x02001002
#define PRM_CMD_RELEASE_HW_RSC		0x01001010
#define PRM_CMD_RSP_RELEASE_HW_RSC	0x02001003
#define PARAM_ID_RSC_HW_CORE		0x08001032
#define PARAM_ID_RSC_LPASS_CORE		0x0800102B
#define PARAM_ID_RSC_AUDIO_HW_CLK	0x0800102C

struct prm_cmd_request_hw_core {
	struct apm_module_param_data param_data;
	uint32_t hw_clk_id;
} __packed;

struct prm_cmd_request_rsc {
	struct apm_module_param_data param_data;
	uint32_t num_clk_id;
	struct audio_hw_clk_cfg clock_id;
} __packed;

struct prm_cmd_release_rsc {
	struct apm_module_param_data param_data;
	uint32_t num_clk_id;
	struct audio_hw_clk_rel_cfg clock_id;
} __packed;

static int q6prm_send_cmd_sync(struct q6prm *prm, struct gpr_pkt *pkt, uint32_t rsp_opcode)
{
	return audioreach_send_cmd_sync(prm->dev, prm->gdev, &prm->result, &prm->lock,
					NULL, &prm->wait, pkt, rsp_opcode);
}

static int q6prm_set_hw_core_req(struct device *dev, uint32_t hw_block_id, bool enable)
{
	struct q6prm *prm = dev_get_drvdata(dev->parent);
	struct apm_module_param_data *param_data;
	struct prm_cmd_request_hw_core *req;
	gpr_device_t *gdev = prm->gdev;
	uint32_t opcode, rsp_opcode;
	struct gpr_pkt *pkt;
	int rc;

	if (enable) {
		opcode = PRM_CMD_REQUEST_HW_RSC;
		rsp_opcode = PRM_CMD_RSP_REQUEST_HW_RSC;
	} else {
		opcode = PRM_CMD_RELEASE_HW_RSC;
		rsp_opcode = PRM_CMD_RSP_RELEASE_HW_RSC;
	}

	pkt = audioreach_alloc_cmd_pkt(sizeof(*req), opcode, 0, gdev->svc.id, GPR_PRM_MODULE_IID);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	req = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = &req->param_data;

	param_data->module_instance_id = GPR_PRM_MODULE_IID;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_RSC_HW_CORE;
	param_data->param_size = sizeof(*req) - APM_MODULE_PARAM_DATA_SIZE;

	req->hw_clk_id = hw_block_id;

	rc = q6prm_send_cmd_sync(prm, pkt, rsp_opcode);

	kfree(pkt);

	return rc;
}

int q6prm_vote_lpass_core_hw(struct device *dev, uint32_t hw_block_id,
			     const char *client_name, uint32_t *client_handle)
{
	return q6prm_set_hw_core_req(dev, hw_block_id, true);

}
EXPORT_SYMBOL_GPL(q6prm_vote_lpass_core_hw);

int q6prm_unvote_lpass_core_hw(struct device *dev, uint32_t hw_block_id, uint32_t client_handle)
{
	return q6prm_set_hw_core_req(dev, hw_block_id, false);
}
EXPORT_SYMBOL_GPL(q6prm_unvote_lpass_core_hw);

static int q6prm_request_lpass_clock(struct device *dev, int clk_id, int clk_attr, int clk_root,
				     unsigned int freq)
{
	struct q6prm *prm = dev_get_drvdata(dev->parent);
	struct apm_module_param_data *param_data;
	struct prm_cmd_request_rsc *req;
	gpr_device_t *gdev = prm->gdev;
	struct gpr_pkt *pkt;
	int rc;

	pkt = audioreach_alloc_cmd_pkt(sizeof(*req), PRM_CMD_REQUEST_HW_RSC, 0, gdev->svc.id,
				       GPR_PRM_MODULE_IID);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	req = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = &req->param_data;

	param_data->module_instance_id = GPR_PRM_MODULE_IID;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_RSC_AUDIO_HW_CLK;
	param_data->param_size = sizeof(*req) - APM_MODULE_PARAM_DATA_SIZE;

	req->num_clk_id = 1;
	req->clock_id.clock_id = clk_id;
	req->clock_id.clock_freq = freq;
	req->clock_id.clock_attri = clk_attr;
	req->clock_id.clock_root = clk_root;

	rc = q6prm_send_cmd_sync(prm, pkt, PRM_CMD_RSP_REQUEST_HW_RSC);

	kfree(pkt);

	return rc;
}

static int q6prm_release_lpass_clock(struct device *dev, int clk_id, int clk_attr, int clk_root,
			  unsigned int freq)
{
	struct q6prm *prm = dev_get_drvdata(dev->parent);
	struct apm_module_param_data *param_data;
	struct prm_cmd_release_rsc *rel;
	gpr_device_t *gdev = prm->gdev;
	struct gpr_pkt *pkt;
	int rc;

	pkt = audioreach_alloc_cmd_pkt(sizeof(*rel), PRM_CMD_RELEASE_HW_RSC, 0, gdev->svc.id,
				       GPR_PRM_MODULE_IID);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);

	rel = (void *)pkt + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = &rel->param_data;

	param_data->module_instance_id = GPR_PRM_MODULE_IID;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_RSC_AUDIO_HW_CLK;
	param_data->param_size = sizeof(*rel) - APM_MODULE_PARAM_DATA_SIZE;

	rel->num_clk_id = 1;
	rel->clock_id.clock_id = clk_id;

	rc = q6prm_send_cmd_sync(prm, pkt, PRM_CMD_RSP_RELEASE_HW_RSC);

	kfree(pkt);

	return rc;
}

int q6prm_set_lpass_clock(struct device *dev, int clk_id, int clk_attr, int clk_root,
			  unsigned int freq)
{
	if (freq)
		return q6prm_request_lpass_clock(dev, clk_id, clk_attr, clk_root, freq);

	return q6prm_release_lpass_clock(dev, clk_id, clk_attr, clk_root, freq);
}
EXPORT_SYMBOL_GPL(q6prm_set_lpass_clock);

static int prm_callback(struct gpr_resp_pkt *data, void *priv, int op)
{
	gpr_device_t *gdev = priv;
	struct q6prm *prm = dev_get_drvdata(&gdev->dev);
	struct gpr_ibasic_rsp_result_t *result;
	struct gpr_hdr *hdr = &data->hdr;

	switch (hdr->opcode) {
	case PRM_CMD_RSP_REQUEST_HW_RSC:
	case PRM_CMD_RSP_RELEASE_HW_RSC:
		result = data->payload;
		prm->result.opcode = hdr->opcode;
		prm->result.status = result->status;
		wake_up(&prm->wait);
		break;
	default:
		break;
	}

	return 0;
}

static int prm_probe(gpr_device_t *gdev)
{
	struct device *dev = &gdev->dev;
	struct q6prm *cc;

	cc = devm_kzalloc(dev, sizeof(*cc), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;

	cc->dev = dev;
	cc->gdev = gdev;
	mutex_init(&cc->lock);
	init_waitqueue_head(&cc->wait);
	dev_set_drvdata(dev, cc);

	if (!q6apm_is_adsp_ready())
		return -EPROBE_DEFER;

	return devm_of_platform_populate(dev);
}

#ifdef CONFIG_OF
static const struct of_device_id prm_device_id[]  = {
	{ .compatible = "qcom,q6prm" },
	{},
};
MODULE_DEVICE_TABLE(of, prm_device_id);
#endif

static gpr_driver_t prm_driver = {
	.probe = prm_probe,
	.gpr_callback = prm_callback,
	.driver = {
		.name = "qcom-prm",
		.of_match_table = of_match_ptr(prm_device_id),
	},
};

module_gpr_driver(prm_driver);
MODULE_DESCRIPTION("Q6 Proxy Resource Manager");
MODULE_LICENSE("GPL");
