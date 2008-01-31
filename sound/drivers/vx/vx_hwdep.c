/*
 * Driver for Digigram VX soundcards
 *
 * DSP firmware management
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>
#include <sound/core.h>
#include <sound/hwdep.h>
#include <sound/vx_core.h>

#ifdef SND_VX_FW_LOADER

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
	static char *fw_files[VX_TYPE_NUMS][4] = {
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

#else /* old style firmware loading */

static int vx_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
	return 0;
}

static int vx_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	return 0;
}

static int vx_hwdep_dsp_status(struct snd_hwdep *hw,
			       struct snd_hwdep_dsp_status *info)
{
	static char *type_ids[VX_TYPE_NUMS] = {
		[VX_TYPE_BOARD] = "vxboard",
		[VX_TYPE_V2] = "vx222",
		[VX_TYPE_MIC] = "vx222",
		[VX_TYPE_VXPOCKET] = "vxpocket",
		[VX_TYPE_VXP440] = "vxp440",
	};
	struct vx_core *vx = hw->private_data;

	snd_assert(type_ids[vx->type], return -EINVAL);
	strcpy(info->id, type_ids[vx->type]);
	if (vx_is_pcmcia(vx))
		info->num_dsps = 4;
	else
		info->num_dsps = 3;
	if (vx->chip_status & VX_STAT_CHIP_INIT)
		info->chip_ready = 1;
	info->version = VX_DRIVER_VERSION;
	return 0;
}

static void free_fw(const struct firmware *fw)
{
	if (fw) {
		vfree(fw->data);
		kfree(fw);
	}
}

static int vx_hwdep_dsp_load(struct snd_hwdep *hw,
			     struct snd_hwdep_dsp_image *dsp)
{
	struct vx_core *vx = hw->private_data;
	int index, err;
	struct firmware *fw;

	snd_assert(vx->ops->load_dsp, return -ENXIO);

	fw = kmalloc(sizeof(*fw), GFP_KERNEL);
	if (! fw) {
		snd_printk(KERN_ERR "cannot allocate firmware\n");
		return -ENOMEM;
	}
	fw->size = dsp->length;
	fw->data = vmalloc(fw->size);
	if (! fw->data) {
		snd_printk(KERN_ERR "cannot allocate firmware image (length=%d)\n",
			   (int)fw->size);
		kfree(fw);
		return -ENOMEM;
	}
	if (copy_from_user(fw->data, dsp->image, dsp->length)) {
		free_fw(fw);
		return -EFAULT;
	}

	index = dsp->index;
	if (! vx_is_pcmcia(vx))
		index++;
	err = vx->ops->load_dsp(vx, index, fw);
	if (err < 0) {
		free_fw(fw);
		return err;
	}
#ifdef CONFIG_PM
	vx->firmware[index] = fw;
#else
	free_fw(fw);
#endif

	if (index == 1)
		vx->chip_status |= VX_STAT_XILINX_LOADED;
	if (index < 3)
		return 0;

	/* ok, we reached to the last one */
	/* create the devices if not built yet */
	if (! (vx->chip_status & VX_STAT_DEVICE_INIT)) {
		if ((err = snd_vx_pcm_new(vx)) < 0)
			return err;

		if ((err = snd_vx_mixer_new(vx)) < 0)
			return err;

		if (vx->ops->add_controls)
			if ((err = vx->ops->add_controls(vx)) < 0)
				return err;

		if ((err = snd_card_register(vx->card)) < 0)
			return err;

		vx->chip_status |= VX_STAT_DEVICE_INIT;
	}
	vx->chip_status |= VX_STAT_CHIP_INIT;
	return 0;
}


/* exported */
int snd_vx_setup_firmware(struct vx_core *chip)
{
	int err;
	struct snd_hwdep *hw;

	if ((err = snd_hwdep_new(chip->card, SND_VX_HWDEP_ID, 0, &hw)) < 0)
		return err;

	hw->iface = SNDRV_HWDEP_IFACE_VX;
	hw->private_data = chip;
	hw->ops.open = vx_hwdep_open;
	hw->ops.release = vx_hwdep_release;
	hw->ops.dsp_status = vx_hwdep_dsp_status;
	hw->ops.dsp_load = vx_hwdep_dsp_load;
	hw->exclusive = 1;
	sprintf(hw->name, "VX Loader (%s)", chip->card->driver);
	chip->hwdep = hw;

	return snd_card_register(chip->card);
}

/* exported */
void snd_vx_free_firmware(struct vx_core *chip)
{
#ifdef CONFIG_PM
	int i;
	for (i = 0; i < 4; i++)
		free_fw(chip->firmware[i]);
#endif
}

#endif /* SND_VX_FW_LOADER */

EXPORT_SYMBOL(snd_vx_setup_firmware);
EXPORT_SYMBOL(snd_vx_free_firmware);
