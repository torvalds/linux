// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2012-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2020, Stephan Gerhold

#include <linux/module.h>
#include <linux/of.h>
#include <linux/soc/qcom/apr.h>
#include "q6cvp.h"
#include "q6voice-common.h"

#define VSS_IVOCPROC_DIRECTION_RX	0
#define VSS_IVOCPROC_DIRECTION_TX	1
#define VSS_IVOCPROC_DIRECTION_RX_TX	2

#define VSS_IVOCPROC_PORT_ID_NONE	0xFFFF

#define VSS_IVOCPROC_TOPOLOGY_ID_NONE			0x00010F70
#define VSS_IVOCPROC_TOPOLOGY_ID_TX_SM_ECNS		0x00010F71
#define VSS_IVOCPROC_TOPOLOGY_ID_TX_DM_FLUENCE		0x00010F72

#define VSS_IVOCPROC_TOPOLOGY_ID_RX_DEFAULT		0x00010F77

#define VSS_IVOCPROC_VOCPROC_MODE_EC_INT_MIXING		0x00010F7C
#define VSS_IVOCPROC_VOCPROC_MODE_EC_EXT_MIXING		0x00010F7D

#define VSS_ICOMMON_CAL_NETWORK_ID_NONE			0x0001135E

#define VSS_IVOCPROC_CMD_ENABLE				0x000100C6
#define VSS_IVOCPROC_CMD_DISABLE			0x000110E1

#define VSS_IVOCPROC_CMD_CREATE_FULL_CONTROL_SESSION_V2	0x000112BF

struct vss_ivocproc_cmd_create_full_control_session_v2_cmd {
	struct apr_hdr hdr;

	/*
	 * Vocproc direction. The supported values:
	 * VSS_IVOCPROC_DIRECTION_RX
	 * VSS_IVOCPROC_DIRECTION_TX
	 * VSS_IVOCPROC_DIRECTION_RX_TX
	 */
	u16 direction;

	/*
	 * Tx device port ID to which the vocproc connects. If a port ID is
	 * not being supplied, set this to #VSS_IVOCPROC_PORT_ID_NONE.
	 */
	u16 tx_port_id;

	/*
	 * Tx path topology ID. If a topology ID is not being supplied, set
	 * this to #VSS_IVOCPROC_TOPOLOGY_ID_NONE.
	 */
	u32 tx_topology_id;

	/*
	 * Rx device port ID to which the vocproc connects. If a port ID is
	 * not being supplied, set this to #VSS_IVOCPROC_PORT_ID_NONE.
	 */
	u16 rx_port_id;

	/*
	 * Rx path topology ID. If a topology ID is not being supplied, set
	 * this to #VSS_IVOCPROC_TOPOLOGY_ID_NONE.
	 */
	u32 rx_topology_id;

	/* Voice calibration profile ID. */
	u32 profile_id;

	/*
	 * Vocproc mode. The supported values:
	 * VSS_IVOCPROC_VOCPROC_MODE_EC_INT_MIXING
	 * VSS_IVOCPROC_VOCPROC_MODE_EC_EXT_MIXING
	 */
	u32 vocproc_mode;

	/*
	 * Port ID to which the vocproc connects for receiving echo
	 * cancellation reference signal. If a port ID is not being supplied,
	 * set this to #VSS_IVOCPROC_PORT_ID_NONE. This parameter value is
	 * ignored when the vocproc_mode parameter is set to
	 * VSS_IVOCPROC_VOCPROC_MODE_EC_INT_MIXING.
	 */
	u16 ec_ref_port_id;

	/*
	 * Session name string used to identify a session that can be shared
	 * with passive controllers (optional).
	 */
	char name[20];
} __packed;

struct q6voice_session *q6cvp_session_create(enum q6voice_path_type path,
					     u16 tx_port, u16 rx_port)
{
	struct vss_ivocproc_cmd_create_full_control_session_v2_cmd cmd;

	cmd.hdr.pkt_size = sizeof(cmd);
	cmd.hdr.opcode = VSS_IVOCPROC_CMD_CREATE_FULL_CONTROL_SESSION_V2;

	/* TODO: Implement calibration */
	cmd.tx_topology_id = VSS_IVOCPROC_TOPOLOGY_ID_TX_SM_ECNS;
	cmd.rx_topology_id = VSS_IVOCPROC_TOPOLOGY_ID_RX_DEFAULT;

	cmd.direction = VSS_IVOCPROC_DIRECTION_RX_TX;
	cmd.tx_port_id = tx_port;
	cmd.rx_port_id = rx_port;
	cmd.profile_id = VSS_ICOMMON_CAL_NETWORK_ID_NONE;
	cmd.vocproc_mode = VSS_IVOCPROC_VOCPROC_MODE_EC_INT_MIXING;
	cmd.ec_ref_port_id = VSS_IVOCPROC_PORT_ID_NONE;

	return q6voice_session_create(Q6VOICE_SERVICE_CVP, path, &cmd.hdr);
}
EXPORT_SYMBOL_GPL(q6cvp_session_create);

int q6cvp_enable(struct q6voice_session *cvp, bool state)
{
	struct apr_pkt cmd;

	cmd.hdr.pkt_size = APR_HDR_SIZE;
	cmd.hdr.opcode = state ? VSS_IVOCPROC_CMD_ENABLE : VSS_IVOCPROC_CMD_DISABLE;

	return q6voice_common_send(cvp, &cmd.hdr);
}
EXPORT_SYMBOL_GPL(q6cvp_enable);

static int q6cvp_probe(struct apr_device *adev)
{
	return q6voice_common_probe(adev, Q6VOICE_SERVICE_CVP);
}

static const struct of_device_id q6cvp_device_id[]  = {
	{ .compatible = "qcom,q6cvp" },
	{},
};
MODULE_DEVICE_TABLE(of, q6cvp_device_id);

static struct apr_driver qcom_q6cvp_driver = {
	.probe = q6cvp_probe,
	.remove = q6voice_common_remove,
	.callback = q6voice_common_callback,
	.driver = {
		.name = "qcom-q6cvp",
		.of_match_table = of_match_ptr(q6cvp_device_id),
	},
};

module_apr_driver(qcom_q6cvp_driver);

MODULE_AUTHOR("Stephan Gerhold <stephan@gerhold.net>");
MODULE_DESCRIPTION("Q6 Core Voice Processor");
MODULE_LICENSE("GPL v2");
