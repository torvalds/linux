// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2012-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2020, Stephan Gerhold

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/soc/qcom/apr.h>
#include "q6mvm.h"
#include "q6voice-common.h"

#define VSS_IMVM_CMD_CREATE_PASSIVE_CONTROL_SESSION	0x000110FF

struct vss_imvm_cmd_create_control_session_cmd {
	struct apr_hdr hdr;

	/* A variable-sized stream name. */
	char name[20];
} __packed;

#define VSS_IMVM_CMD_SET_POLICY_DUAL_CONTROL		0x00011327

/* This command is required to let MVM know who is in control of session. */
struct vss_imvm_cmd_set_policy_dual_control_cmd {
	struct apr_hdr hdr;

	/* Set to TRUE to enable modem state machine control */
	bool enable;
} __packed;

#define VSS_IMVM_CMD_ATTACH_VOCPROC			0x0001123E
#define VSS_IMVM_CMD_DETACH_VOCPROC			0x0001123F

/*
 * Attach/detach a vocproc to the MVM.
 * The MVM will symmetrically connect/disconnect this vocproc
 * to/from all the streams currently attached to it.
 */
struct vss_imvm_cmd_attach_vocproc_cmd {
	struct apr_hdr hdr;

	/* Handle of vocproc being attached. */
	u16 handle;
} __packed;

#define VSS_IMVM_CMD_START_VOICE			0x00011190
#define VSS_IMVM_CMD_STOP_VOICE				0x00011192

static inline const char *q6mvm_session_name(enum q6voice_path_type path)
{
	switch (path) {
	case Q6VOICE_PATH_VOICE:
		return "default modem voice";
	default:
		return NULL;
	}
}

static int q6mvm_set_dual_control(struct q6voice_session *mvm)
{
	struct vss_imvm_cmd_set_policy_dual_control_cmd cmd;

	cmd.hdr.pkt_size = sizeof(cmd);
	cmd.hdr.opcode = VSS_IMVM_CMD_SET_POLICY_DUAL_CONTROL;

	cmd.enable = true;

	return q6voice_common_send(mvm, &cmd.hdr);
}

struct q6voice_session *q6mvm_session_create(enum q6voice_path_type path)
{
	struct vss_imvm_cmd_create_control_session_cmd cmd;
	struct q6voice_session *mvm;
	const char *session_name;
	int ret;

	cmd.hdr.pkt_size = sizeof(cmd);
	cmd.hdr.opcode = VSS_IMVM_CMD_CREATE_PASSIVE_CONTROL_SESSION;

	session_name = q6mvm_session_name(path);
	if (session_name)
		strscpy(cmd.name, session_name, sizeof(cmd.name));

	mvm = q6voice_session_create(Q6VOICE_SERVICE_MVM, path, &cmd.hdr);
	if (IS_ERR(mvm))
		return mvm;

	ret = q6mvm_set_dual_control(mvm);
	if (ret) {
		dev_err(mvm->dev, "failed to set dual control: %d\n", ret);
		q6voice_session_release(mvm);
		return ERR_PTR(ret);
	}

	return mvm;
}
EXPORT_SYMBOL_GPL(q6mvm_session_create);

int q6mvm_attach(struct q6voice_session *mvm, struct q6voice_session *cvp,
		 bool state)
{
	struct vss_imvm_cmd_attach_vocproc_cmd cmd;

	cmd.hdr.pkt_size = sizeof(cmd);
	cmd.hdr.opcode = state ? VSS_IMVM_CMD_ATTACH_VOCPROC : VSS_IMVM_CMD_DETACH_VOCPROC;

	cmd.handle = cvp->handle;

	return q6voice_common_send(mvm, &cmd.hdr);
}
EXPORT_SYMBOL_GPL(q6mvm_attach);

int q6mvm_start(struct q6voice_session *mvm, bool state)
{
	struct apr_pkt cmd;

	cmd.hdr.pkt_size = APR_HDR_SIZE;
	cmd.hdr.opcode = state ? VSS_IMVM_CMD_START_VOICE : VSS_IMVM_CMD_STOP_VOICE;

	return q6voice_common_send(mvm, &cmd.hdr);
}
EXPORT_SYMBOL_GPL(q6mvm_start);

static int q6mvm_probe(struct apr_device *adev)
{
	int ret;

	ret = q6voice_common_probe(adev, Q6VOICE_SERVICE_MVM);
	if (ret)
		return ret;

	return of_platform_populate(adev->dev.of_node, NULL, NULL, &adev->dev);
}

static void q6mvm_remove(struct apr_device *adev)
{
	of_platform_depopulate(&adev->dev);
	q6voice_common_remove(adev);
}

static const struct of_device_id q6mvm_device_id[]  = {
	{ .compatible = "qcom,q6mvm" },
	{},
};
MODULE_DEVICE_TABLE(of, q6mvm_device_id);

static struct apr_driver qcom_q6mvm_driver = {
	.probe = q6mvm_probe,
	.remove = q6mvm_remove,
	.callback = q6voice_common_callback,
	.driver = {
		.name = "qcom-q6mvm",
		.of_match_table = of_match_ptr(q6mvm_device_id),
	},
};

module_apr_driver(qcom_q6mvm_driver);
MODULE_DESCRIPTION("Q6 Multimode Voice Manager");
MODULE_LICENSE("GPL v2");
