/* SPDX-License-Identifier: GPL-2.0
 *
 * HDA DSP ALSA Control Driver
 *
 * Copyright 2022 Cirrus Logic, Inc.
 *
 * Author: Stefan Binding <sbinding@opensource.cirrus.com>
 */

#ifndef __HDA_CS_DSP_CTL_H__
#define __HDA_CS_DSP_CTL_H__

#include <sound/soc.h>
#include <linux/firmware/cirrus/cs_dsp.h>

enum hda_cs_dsp_fw_id {
	HDA_CS_DSP_FW_SPK_PROT,
	HDA_CS_DSP_FW_SPK_CALI,
	HDA_CS_DSP_FW_SPK_DIAG,
	HDA_CS_DSP_FW_MISC,
	HDA_CS_DSP_NUM_FW
};

struct hda_cs_dsp_ctl_info {
	struct snd_card *card;
	enum hda_cs_dsp_fw_id fw_type;
	const char *device_name;
};

int hda_cs_dsp_control_add(struct cs_dsp_coeff_ctl *cs_ctl, struct hda_cs_dsp_ctl_info *info);
void hda_cs_dsp_control_remove(struct cs_dsp_coeff_ctl *cs_ctl);

#endif /*__HDA_CS_DSP_CTL_H__*/
