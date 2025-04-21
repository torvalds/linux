// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2025 Intel Corporation

/*
 * Hardware interface for audio DSP on PantherLake.
 */

#include <sound/hda_register.h>
#include <sound/hda-mlink.h>
#include <sound/sof/ipc4/header.h>
#include "../ipc4-priv.h"
#include "../ops.h"
#include "hda.h"
#include "hda-ipc.h"
#include "../sof-audio.h"
#include "mtl.h"
#include "lnl.h"
#include "ptl.h"

static bool sof_ptl_check_mic_privacy_irq(struct snd_sof_dev *sdev, bool alt,
					  int elid)
{
	if (!alt || elid != AZX_REG_ML_LEPTR_ID_SDW)
		return false;

	return hdac_bus_eml_is_mic_privacy_changed(sof_to_bus(sdev), alt, elid);
}

static void sof_ptl_mic_privacy_work(struct work_struct *work)
{
	struct sof_intel_hda_dev *hdev = container_of(work,
						      struct sof_intel_hda_dev,
						      mic_privacy.work);
	struct hdac_bus *bus = &hdev->hbus.core;
	struct snd_sof_dev *sdev = dev_get_drvdata(bus->dev);
	bool state;

	/*
	 * The microphone privacy state is only available via Soundwire shim
	 * in PTL
	 * The work is only scheduled on change.
	 */
	state = hdac_bus_eml_get_mic_privacy_state(bus, 1,
						   AZX_REG_ML_LEPTR_ID_SDW);
	sof_ipc4_mic_privacy_state_change(sdev, state);
}

static void sof_ptl_process_mic_privacy(struct snd_sof_dev *sdev, bool alt,
					int elid)
{
	struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;

	if (!alt || elid != AZX_REG_ML_LEPTR_ID_SDW)
		return;

	/*
	 * Schedule the work to read the microphone privacy state and send IPC
	 * message about the new state to the firmware
	 */
	schedule_work(&hdev->mic_privacy.work);
}

static void sof_ptl_set_mic_privacy(struct snd_sof_dev *sdev,
				    struct sof_ipc4_intel_mic_privacy_cap *caps)
{
	struct sof_intel_hda_dev *hdev = sdev->pdata->hw_pdata;
	u32 micpvcp;

	if (!caps || !caps->capabilities_length)
		return;

	micpvcp = caps->capabilities[0];

	/* No need to set the mic privacy if it is not enabled or forced */
	if (!(micpvcp & PTL_MICPVCP_DDZE_ENABLED) ||
	    micpvcp & PTL_MICPVCP_DDZE_FORCED)
		return;

	hdac_bus_eml_set_mic_privacy_mask(sof_to_bus(sdev), true,
					  AZX_REG_ML_LEPTR_ID_SDW,
					  PTL_MICPVCP_GET_SDW_MASK(micpvcp));

	INIT_WORK(&hdev->mic_privacy.work, sof_ptl_mic_privacy_work);
	hdev->mic_privacy.active = true;
}

int sof_ptl_set_ops(struct snd_sof_dev *sdev, struct snd_sof_dsp_ops *dsp_ops)
{
	struct sof_ipc4_fw_data *ipc4_data;
	int ret;

	ret = sof_lnl_set_ops(sdev, dsp_ops);
	if (ret)
		return ret;

	ipc4_data = sdev->private;
	ipc4_data->intel_configure_mic_privacy = sof_ptl_set_mic_privacy;

	return 0;
};
EXPORT_SYMBOL_NS(sof_ptl_set_ops, "SND_SOC_SOF_INTEL_PTL");

const struct sof_intel_dsp_desc ptl_chip_info = {
	.cores_num = 5,
	.init_core_mask = BIT(0),
	.host_managed_cores_mask = BIT(0),
	.ipc_req = MTL_DSP_REG_HFIPCXIDR,
	.ipc_req_mask = MTL_DSP_REG_HFIPCXIDR_BUSY,
	.ipc_ack = MTL_DSP_REG_HFIPCXIDA,
	.ipc_ack_mask = MTL_DSP_REG_HFIPCXIDA_DONE,
	.ipc_ctl = MTL_DSP_REG_HFIPCXCTL,
	.rom_status_reg = LNL_DSP_REG_HFDSC,
	.rom_init_timeout = 300,
	.ssp_count = MTL_SSP_COUNT,
	.d0i3_offset = MTL_HDA_VS_D0I3C,
	.read_sdw_lcount =  hda_sdw_check_lcount_ext,
	.check_sdw_irq = lnl_dsp_check_sdw_irq,
	.check_sdw_wakeen_irq = lnl_sdw_check_wakeen_irq,
	.check_ipc_irq = mtl_dsp_check_ipc_irq,
	.check_mic_privacy_irq = sof_ptl_check_mic_privacy_irq,
	.process_mic_privacy = sof_ptl_process_mic_privacy,
	.cl_init = mtl_dsp_cl_init,
	.power_down_dsp = mtl_power_down_dsp,
	.disable_interrupts = lnl_dsp_disable_interrupts,
	.hw_ip_version = SOF_INTEL_ACE_3_0,
};

MODULE_IMPORT_NS("SND_SOC_SOF_INTEL_MTL");
MODULE_IMPORT_NS("SND_SOC_SOF_INTEL_LNL");
MODULE_IMPORT_NS("SND_SOC_SOF_HDA_MLINK");
