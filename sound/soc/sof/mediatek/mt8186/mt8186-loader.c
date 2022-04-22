// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright (c) 2022 Mediatek Corporation. All rights reserved.
//
// Author: Allen-KH Cheng <allen-kh.cheng@mediatek.com>
//         Tinghan Shen <tinghan.shen@mediatek.com>
//
// Hardware interface for mt8186 DSP code loader

#include <sound/sof.h>
#include "mt8186.h"
#include "../../ops.h"

void sof_hifixdsp_boot_sequence(struct snd_sof_dev *sdev, u32 boot_addr)
{
	/* set RUNSTALL to stop core */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, ADSP_HIFI_IO_CONFIG,
				RUNSTALL, RUNSTALL);

	/* set core boot address */
	snd_sof_dsp_write(sdev, DSP_SECREG_BAR, ADSP_ALTVEC_C0, boot_addr);
	snd_sof_dsp_write(sdev, DSP_SECREG_BAR, ADSP_ALTVECSEL, ADSP_ALTVECSEL_C0);

	/* assert core reset */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, ADSP_CFGREG_SW_RSTN,
				SW_RSTN_C0 | SW_DBG_RSTN_C0,
				SW_RSTN_C0 | SW_DBG_RSTN_C0);

	/* hardware requirement */
	udelay(1);

	/* release core reset */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, ADSP_CFGREG_SW_RSTN,
				SW_RSTN_C0 | SW_DBG_RSTN_C0,
				0);

	/* clear RUNSTALL (bit31) to start core */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, ADSP_HIFI_IO_CONFIG,
				RUNSTALL, 0);
}

void sof_hifixdsp_shutdown(struct snd_sof_dev *sdev)
{
	/* set RUNSTALL to stop core */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, ADSP_HIFI_IO_CONFIG,
				RUNSTALL, RUNSTALL);

	/* assert core reset */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, ADSP_CFGREG_SW_RSTN,
				SW_RSTN_C0 | SW_DBG_RSTN_C0,
				SW_RSTN_C0 | SW_DBG_RSTN_C0);
}

