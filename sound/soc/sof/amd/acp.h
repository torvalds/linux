/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license. When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
 */

#ifndef __SOF_AMD_ACP_H
#define __SOF_AMD_ACP_H

#define ACP_DSP_BAR	0

#define ACP_REG_POLL_INTERVAL                   500
#define ACP_REG_POLL_TIMEOUT_US                 2000

#define ACP_PGFSM_CNTL_POWER_ON_MASK		0x01
#define ACP_PGFSM_STATUS_MASK			0x03
#define ACP_POWERED_ON				0x00
#define ACP_ASSERT_RESET			0x01
#define ACP_RELEASE_RESET			0x00
#define ACP_SOFT_RESET_DONE_MASK		0x00010001

/* Common device data struct for ACP devices */
struct acp_dev_data {
	struct snd_sof_dev  *dev;
};

/* ACP device probe/remove */
int amd_sof_acp_probe(struct snd_sof_dev *sdev);
int amd_sof_acp_remove(struct snd_sof_dev *sdev);

extern const struct snd_sof_dsp_ops sof_renoir_ops;
#endif
