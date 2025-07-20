// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2025 Intel Corporation.
//

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "sof-function-topology-lib.h"

enum tplg_device_id {
	TPLG_DEVICE_SDCA_JACK,
	TPLG_DEVICE_SDCA_AMP,
	TPLG_DEVICE_SDCA_MIC,
	TPLG_DEVICE_INTEL_PCH_DMIC,
	TPLG_DEVICE_HDMI,
	TPLG_DEVICE_MAX
};

#define SDCA_DEVICE_MASK (BIT(TPLG_DEVICE_SDCA_JACK) | BIT(TPLG_DEVICE_SDCA_AMP) | \
			  BIT(TPLG_DEVICE_SDCA_MIC))

#define SOF_INTEL_PLATFORM_NAME_MAX 4

int sof_sdw_get_tplg_files(struct snd_soc_card *card, const struct snd_soc_acpi_mach *mach,
			   const char *prefix, const char ***tplg_files)
{
	struct snd_soc_acpi_mach_params mach_params = mach->mach_params;
	struct snd_soc_dai_link *dai_link;
	const struct firmware *fw;
	char platform[SOF_INTEL_PLATFORM_NAME_MAX];
	unsigned long tplg_mask = 0;
	int tplg_num = 0;
	int tplg_dev;
	int ret;
	int i;

	ret = sscanf(mach->sof_tplg_filename, "sof-%3s-*.tplg", platform);
	if (ret != 1) {
		dev_err(card->dev, "Invalid platform name %s of tplg %s\n",
			platform, mach->sof_tplg_filename);
		return -EINVAL;
	}

	for_each_card_prelinks(card, i, dai_link) {
		char *tplg_dev_name;

		dev_dbg(card->dev, "dai_link %s id %d\n", dai_link->name, dai_link->id);
		if (strstr(dai_link->name, "SimpleJack")) {
			tplg_dev = TPLG_DEVICE_SDCA_JACK;
			tplg_dev_name = "sdca-jack";
		} else if (strstr(dai_link->name, "SmartAmp")) {
			tplg_dev = TPLG_DEVICE_SDCA_AMP;
			tplg_dev_name = devm_kasprintf(card->dev, GFP_KERNEL,
						       "sdca-%damp", dai_link->num_cpus);
			if (!tplg_dev_name)
				return -ENOMEM;
		} else if (strstr(dai_link->name, "SmartMic")) {
			tplg_dev = TPLG_DEVICE_SDCA_MIC;
			tplg_dev_name = "sdca-mic";
		} else if (strstr(dai_link->name, "dmic")) {
			switch (mach_params.dmic_num) {
			case 2:
				tplg_dev_name = "dmic-2ch";
				break;
			case 4:
				tplg_dev_name = "dmic-4ch";
				break;
			default:
				dev_warn(card->dev,
					 "unsupported number of dmics: %d\n",
					 mach_params.dmic_num);
				continue;
			}
			tplg_dev = TPLG_DEVICE_INTEL_PCH_DMIC;
		} else if (strstr(dai_link->name, "iDisp")) {
			tplg_dev = TPLG_DEVICE_HDMI;
			tplg_dev_name = "hdmi-pcm5";

		} else {
			/* The dai link is not supported by separated tplg yet */
			dev_dbg(card->dev,
				"dai_link %s is not supported by separated tplg yet\n",
				dai_link->name);
			return 0;
		}
		if (tplg_mask & BIT(tplg_dev))
			continue;

		tplg_mask |= BIT(tplg_dev);

		/*
		 * The tplg file naming rule is sof-<platform>-<function>-id<BE id number>.tplg
		 * where <platform> is only required for the DMIC function as the nhlt blob
		 * is platform dependent.
		 */
		switch (tplg_dev) {
		case TPLG_DEVICE_INTEL_PCH_DMIC:
			(*tplg_files)[tplg_num] = devm_kasprintf(card->dev, GFP_KERNEL,
								 "%s/sof-%s-%s-id%d.tplg",
								 prefix, platform,
								 tplg_dev_name, dai_link->id);
			break;
		default:
			(*tplg_files)[tplg_num] = devm_kasprintf(card->dev, GFP_KERNEL,
								 "%s/sof-%s-id%d.tplg",
								 prefix, tplg_dev_name,
								 dai_link->id);
			break;
		}
		if (!(*tplg_files)[tplg_num])
			return -ENOMEM;
		tplg_num++;
	}

	dev_dbg(card->dev, "tplg_mask %#lx tplg_num %d\n", tplg_mask, tplg_num);

	/* Check presence of sub-topologies */
	for (i = 0; i < tplg_num; i++) {
		ret = firmware_request_nowarn(&fw, (*tplg_files)[i], card->dev);
		if (!ret) {
			release_firmware(fw);
		} else {
			dev_dbg(card->dev, "Failed to open topology file: %s\n", (*tplg_files)[i]);
			return 0;
		}
	}

	return tplg_num;
}

