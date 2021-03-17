// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Digigram VX soundcards
 *
 * DSP firmware management
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 */

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/hwdep.h>
#include <sound/vx_core.h>

MODULE_FIRMWARE("vx/bx_1_vxp.b56");
MODULE_FIRMWARE("vx/bx_1_vp4.b56");
MODULE_FIRMWARE("vx/x1_1_vx2.xlx");
MODULE_FIRMWARE("vx/x1_2_v22.xlx");
MODULE_FIRMWARE("vx/x1_1_vxp.xlx");
MODULE_FIRMWARE("vx/x1_1_vp4.xlx");
MODULE_FIRMWARE("vx/bd56002.boot");
MODULE_FIRMWARE("vx/bd563v2.boot");
MODULE_FIRMWARE("vx/bd563s3.boot");
MODULE_FIRMWARE("vx/l_1_vx2.d56");
MODULE_FIRMWARE("vx/l_1_v22.d56");
MODULE_FIRMWARE("vx/l_1_vxp.d56");
MODULE_FIRMWARE("vx/l_1_vp4.d56");

int snd_vx_setup_firmware(struct vx_core *chip)
{
	static const char * const fw_files[VX_TYPE_NUMS][4] = {
		[VX_TYPE_BOARD] = {
			NULL, "x1_1_vx2.xlx", "bd56002.boot", "l_1_vx2.d56",
		},
		[VX_TYPE_V2] = {
			NULL, "x1_2_v22.xlx", "bd563v2.boot", "l_1_v22.d56",
		},
		[VX_TYPE_MIC] = {
			NULL, "x1_2_v22.xlx", "bd563v2.boot", "l_1_v22.d56",
		},
		[VX_TYPE_VXPOCKET] = {
			"bx_1_vxp.b56", "x1_1_vxp.xlx", "bd563s3.boot", "l_1_vxp.d56"
		},
		[VX_TYPE_VXP440] = {
			"bx_1_vp4.b56", "x1_1_vp4.xlx", "bd563s3.boot", "l_1_vp4.d56"
		},
	};

	int i, err;

	for (i = 0; i < 4; i++) {
		char path[32];
		const struct firmware *fw;
		if (! fw_files[chip->type][i])
			continue;
		sprintf(path, "vx/%s", fw_files[chip->type][i]);
		if (request_firmware(&fw, path, chip->dev)) {
			snd_printk(KERN_ERR "vx: can't load firmware %s\n", path);
			return -ENOENT;
		}
		err = chip->ops->load_dsp(chip, i, fw);
		if (err < 0) {
			release_firmware(fw);
			return err;
		}
		if (i == 1)
			chip->chip_status |= VX_STAT_XILINX_LOADED;
#ifdef CONFIG_PM
		chip->firmware[i] = fw;
#else
		release_firmware(fw);
#endif
	}

	/* ok, we reached to the last one */
	/* create the devices if not built yet */
	if ((err = snd_vx_pcm_new(chip)) < 0)
		return err;

	if ((err = snd_vx_mixer_new(chip)) < 0)
		return err;

	if (chip->ops->add_controls)
		if ((err = chip->ops->add_controls(chip)) < 0)
			return err;

	chip->chip_status |= VX_STAT_DEVICE_INIT;
	chip->chip_status |= VX_STAT_CHIP_INIT;

	return snd_card_register(chip->card);
}

/* exported */
void snd_vx_free_firmware(struct vx_core *chip)
{
#ifdef CONFIG_PM
	int i;
	for (i = 0; i < 4; i++)
		release_firmware(chip->firmware[i]);
#endif
}

EXPORT_SYMBOL(snd_vx_setup_firmware);
EXPORT_SYMBOL(snd_vx_free_firmware);
