// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Authors: Vijendar Mukunda <Vijendar.Mukunda@amd.com>
//	    Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>

/*
 * Hardware interface for generic AMD ACP processor
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "../ops.h"
#include "acp.h"
#include "acp-dsp-offset.h"

static int acp_power_on(struct snd_sof_dev *sdev)
{
	unsigned int val;
	int ret;

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_PGFSM_STATUS);

	if (val == ACP_POWERED_ON)
		return 0;

	if (val & ACP_PGFSM_STATUS_MASK)
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_PGFSM_CONTROL,
				  ACP_PGFSM_CNTL_POWER_ON_MASK);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_PGFSM_STATUS, val, !val,
					    ACP_REG_POLL_INTERVAL, ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0)
		dev_err(sdev->dev, "timeout in ACP_PGFSM_STATUS read\n");

	return ret;
}

static int acp_reset(struct snd_sof_dev *sdev)
{
	unsigned int val;
	int ret;

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, ACP_ASSERT_RESET);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, val,
					    val & ACP_SOFT_RESET_DONE_MASK,
					    ACP_REG_POLL_INTERVAL, ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev, "timeout asserting reset\n");
		return ret;
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, ACP_RELEASE_RESET);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, val, !val,
					    ACP_REG_POLL_INTERVAL, ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0)
		dev_err(sdev->dev, "timeout in releasing reset\n");

	return ret;
}

static int acp_init(struct snd_sof_dev *sdev)
{
	int ret;

	/* power on */
	ret = acp_power_on(sdev);
	if (ret) {
		dev_err(sdev->dev, "ACP power on failed\n");
		return ret;
	}
	/* Reset */
	return acp_reset(sdev);
}

int amd_sof_acp_probe(struct snd_sof_dev *sdev)
{
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	struct acp_dev_data *adata;
	unsigned int addr;

	adata = devm_kzalloc(sdev->dev, sizeof(struct acp_dev_data),
			     GFP_KERNEL);
	if (!adata)
		return -ENOMEM;

	adata->dev = sdev;
	addr = pci_resource_start(pci, ACP_DSP_BAR);
	sdev->bar[ACP_DSP_BAR] = devm_ioremap(sdev->dev, addr, pci_resource_len(pci, ACP_DSP_BAR));
	if (!sdev->bar[ACP_DSP_BAR]) {
		dev_err(sdev->dev, "ioremap error\n");
		return -ENXIO;
	}

	pci_set_master(pci);

	sdev->pdata->hw_pdata = adata;

	return acp_init(sdev);
}
EXPORT_SYMBOL_NS(amd_sof_acp_probe, SND_SOC_SOF_AMD_COMMON);

int amd_sof_acp_remove(struct snd_sof_dev *sdev)
{
	return acp_reset(sdev);
}
EXPORT_SYMBOL_NS(amd_sof_acp_remove, SND_SOC_SOF_AMD_COMMON);

MODULE_DESCRIPTION("AMD ACP sof driver");
MODULE_LICENSE("Dual BSD/GPL");
