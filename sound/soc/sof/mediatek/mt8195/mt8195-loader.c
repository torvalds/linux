// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright (c) 2021 Mediatek Corporation. All rights reserved.
//
// Author: YC Hung <yc.hung@mediatek.com>
//
// Hardware interface for mt8195 DSP code loader

#include <sound/sof.h>
#include "mt8195.h"
#include "../../ops.h"

void sof_hifixdsp_boot_sequence(struct snd_sof_dev *sdev, u32 boot_addr)
{
	/* ADSP bootup base */
	snd_sof_dsp_write(sdev, DSP_REG_BAR, DSP_ALTRESETVEC, boot_addr);

	/* pull high RunStall (set bit3 to 1) */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, DSP_RESET_SW,
				ADSP_RUNSTALL, ADSP_RUNSTALL);

	/* pull high StatVectorSel to use AltResetVec (set bit4 to 1) */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, DSP_RESET_SW,
				DSP_RESET_SW, DSP_RESET_SW);

	/* toggle  DReset & BReset */
	/* pull high DReset & BReset */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, DSP_RESET_SW,
				ADSP_BRESET_SW | ADSP_DRESET_SW,
				ADSP_BRESET_SW | ADSP_DRESET_SW);

	/* pull low DReset & BReset */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, DSP_RESET_SW,
				ADSP_BRESET_SW | ADSP_DRESET_SW,
				0);

	/* Enable PDebug */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, DSP_PDEBUGBUS0,
				PDEBUG_ENABLE,
				PDEBUG_ENABLE);

	/* release RunStall (set bit3 to 0) */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, DSP_RESET_SW,
				ADSP_RUNSTALL, 0);
}

void sof_hifixdsp_shutdown(struct snd_sof_dev *sdev)
{
	/* Clear to 0 firstly */
	snd_sof_dsp_write(sdev, DSP_REG_BAR, DSP_RESET_SW, 0x0);

	/* RUN_STALL pull high again to reset */
	snd_sof_dsp_update_bits(sdev, DSP_REG_BAR, DSP_RESET_SW,
				ADSP_RUNSTALL, ADSP_RUNSTALL);
}

