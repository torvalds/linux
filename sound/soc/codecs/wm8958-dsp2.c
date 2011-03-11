/*
 * wm8958-dsp2.c  --  WM8958 DSP2 support
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <trace/events/asoc.h>

#include <linux/mfd/wm8994/core.h>
#include <linux/mfd/wm8994/registers.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/mfd/wm8994/gpio.h>

#include "wm8994.h"

#define WM_FW_BLOCK_INFO 0xff
#define WM_FW_BLOCK_PM   0x00
#define WM_FW_BLOCK_X    0x01
#define WM_FW_BLOCK_Y    0x02
#define WM_FW_BLOCK_Z    0x03
#define WM_FW_BLOCK_I    0x06
#define WM_FW_BLOCK_A    0x08
#define WM_FW_BLOCK_C    0x0c

static int wm8958_dsp2_fw(struct snd_soc_codec *codec, const char *name,
			  const struct firmware *fw, bool check)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	u64 data64;
	u32 data32;
	const u8 *data;
	char *str;
	size_t block_len, len;
	int ret = 0;

	/* Suppress unneeded downloads */
	if (wm8994->cur_fw == fw)
		return 0;

	if (fw->size < 32) {
		dev_err(codec->dev, "%s: firmware too short\n", name);
		goto err;
	}

	if (memcmp(fw->data, "WMFW", 4) != 0) {
		dev_err(codec->dev, "%s: firmware has bad file magic %08x\n",
			name, data32);
		goto err;
	}

	memcpy(&data32, fw->data + 4, sizeof(data32));
	len = be32_to_cpu(data32);

	memcpy(&data32, fw->data + 8, sizeof(data32));
	data32 = be32_to_cpu(data32);
	if ((data32 >> 24) & 0xff) {
		dev_err(codec->dev, "%s: unsupported firmware version %d\n",
			name, (data32 >> 24) & 0xff);
		goto err;
	}
	if ((data32 & 0xffff) != 8958) {
		dev_err(codec->dev, "%s: unsupported target device %d\n",
			name, data32 & 0xffff);
		goto err;
	}
	if (((data32 >> 16) & 0xff) != 0xc) {
		dev_err(codec->dev, "%s: unsupported target core %d\n",
			name, (data32 >> 16) & 0xff);
		goto err;
	}

	if (check) {
		memcpy(&data64, fw->data + 24, sizeof(u64));
		dev_info(codec->dev, "%s timestamp %llx\n",
			 name, be64_to_cpu(data64));
	} else {
		snd_soc_write(codec, 0x102, 0x2);
		snd_soc_write(codec, 0x900, 0x2);
	}

	data = fw->data + len;
	len = fw->size - len;
	while (len) {
		if (len < 12) {
			dev_err(codec->dev, "%s short data block of %d\n",
				name, len);
			goto err;
		}

		memcpy(&data32, data + 4, sizeof(data32));
		block_len = be32_to_cpu(data32);
		if (block_len + 8 > len) {
			dev_err(codec->dev, "%d byte block longer than file\n",
				block_len);
			goto err;
		}
		if (block_len == 0) {
			dev_err(codec->dev, "Zero length block\n");
			goto err;
		}

		memcpy(&data32, data, sizeof(data32));
		data32 = be32_to_cpu(data32);

		switch ((data32 >> 24) & 0xff) {
		case WM_FW_BLOCK_INFO:
			/* Informational text */
			if (!check)
				break;

			str = kzalloc(block_len + 1, GFP_KERNEL);
			if (str) {
				memcpy(str, data + 8, block_len);
				dev_info(codec->dev, "%s: %s\n", name, str);
				kfree(str);
			} else {
				dev_err(codec->dev, "Out of memory\n");
			}
			break;
		case WM_FW_BLOCK_PM:
		case WM_FW_BLOCK_X:
		case WM_FW_BLOCK_Y:
		case WM_FW_BLOCK_Z:
		case WM_FW_BLOCK_I:
		case WM_FW_BLOCK_A:
		case WM_FW_BLOCK_C:
			dev_dbg(codec->dev, "%s: %d bytes of %x@%x\n", name,
				block_len, (data32 >> 24) & 0xff,
				data32 & 0xffffff);

			if (check)
				break;

			data32 &= 0xffffff;

			wm8994_bulk_write(codec->control_data,
					  data32 & 0xffffff,
					  block_len / 2,
					  (void *)(data + 8));

			break;
		default:
			dev_warn(codec->dev, "%s: unknown block type %d\n",
				 name, (data32 >> 24) & 0xff);
			break;
		}

		/* Round up to the next 32 bit word */
		block_len += block_len % 4;

		data += block_len + 8;
		len -= block_len + 8;
	}

	if (!check) {
		dev_dbg(codec->dev, "%s: download done\n", name);
		wm8994->cur_fw = fw;
	} else {
		dev_info(codec->dev, "%s: got firmware\n", name);
	}

	goto ok;

err:
	ret = -EINVAL;
ok:
	if (!check) {
		snd_soc_write(codec, 0x900, 0x0);
		snd_soc_write(codec, 0x102, 0x0);
	}

	return ret;
}

static void wm8958_mbc_apply(struct snd_soc_codec *codec, int mbc, int start)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994_pdata *pdata = wm8994->pdata;
	int i;

	/* If the DSP is already running then noop */
	if (snd_soc_read(codec, WM8958_DSP2_PROGRAM) & WM8958_DSP2_ENA)
		return;

	/* If we have MBC firmware download it */
	if (wm8994->mbc)
		wm8958_dsp2_fw(codec, "MBC", wm8994->mbc, false);

	snd_soc_update_bits(codec, WM8958_DSP2_PROGRAM,
			    WM8958_DSP2_ENA, WM8958_DSP2_ENA);

	/* If we've got user supplied MBC settings use them */
	if (pdata && pdata->num_mbc_cfgs) {
		struct wm8958_mbc_cfg *cfg
			= &pdata->mbc_cfgs[wm8994->mbc_cfg];

		for (i = 0; i < ARRAY_SIZE(cfg->coeff_regs); i++)
			snd_soc_write(codec, i + WM8958_MBC_BAND_1_K_1,
				      cfg->coeff_regs[i]);

		for (i = 0; i < ARRAY_SIZE(cfg->cutoff_regs); i++)
			snd_soc_write(codec,
				      i + WM8958_MBC_BAND_2_LOWER_CUTOFF_C1_1,
				      cfg->cutoff_regs[i]);
	}

	/* Run the DSP */
	snd_soc_write(codec, WM8958_DSP2_EXECCONTROL,
		      WM8958_DSP2_RUNR);

	/* And we're off! */
	snd_soc_update_bits(codec, WM8958_DSP2_CONFIG,
			    WM8958_MBC_ENA |
			    WM8958_MBC_SEL_MASK,
			    path << WM8958_MBC_SEL_SHIFT |
			    WM8958_MBC_ENA);
}

static void wm8958_dsp_apply(struct snd_soc_codec *codec, int path, int start)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int pwr_reg = snd_soc_read(codec, WM8994_POWER_MANAGEMENT_5);
	int ena, reg, aif, i;

	switch (mbc) {
	case 0:
		pwr_reg &= (WM8994_AIF1DAC1L_ENA | WM8994_AIF1DAC1R_ENA);
		aif = 0;
		break;
	case 1:
		pwr_reg &= (WM8994_AIF1DAC2L_ENA | WM8994_AIF1DAC2R_ENA);
		aif = 0;
		break;
	case 2:
		pwr_reg &= (WM8994_AIF2DACL_ENA | WM8994_AIF2DACR_ENA);
		aif = 1;
		break;
	default:
		BUG();
		return;
	}

	/* We can only enable the MBC if the AIF is enabled and we
	 * want it to be enabled. */
	ena = pwr_reg && wm8994->mbc_ena[mbc];

	reg = snd_soc_read(codec, WM8958_DSP2_PROGRAM);

	dev_dbg(codec->dev, "MBC %d startup: %d, power: %x, DSP: %x\n",
		mbc, start, pwr_reg, reg);

	if (start && ena) {
		/* If the DSP is already running then noop */
		if (reg & WM8958_DSP2_ENA)
			return;

		/* If neither AIFnCLK is not yet enabled postpone */
		if (!(snd_soc_read(codec, WM8994_AIF1_CLOCKING_1)
		      & WM8994_AIF1CLK_ENA_MASK) &&
		    !(snd_soc_read(codec, WM8994_AIF2_CLOCKING_1)
		      & WM8994_AIF2CLK_ENA_MASK))
			return;

		/* If we have MBC firmware download it */
		if (wm8994->mbc && wm8994->mbc_ena[mbc])
			wm8958_dsp2_fw(codec, "MBC", wm8994->mbc, false);

		/* Switch the clock over to the appropriate AIF */
		snd_soc_update_bits(codec, WM8994_CLOCKING_1,
				    WM8958_DSP2CLK_SRC | WM8958_DSP2CLK_ENA,
				    aif << WM8958_DSP2CLK_SRC_SHIFT |
				    WM8958_DSP2CLK_ENA);

		snd_soc_update_bits(codec, WM8958_DSP2_PROGRAM,
				    WM8958_DSP2_ENA, WM8958_DSP2_ENA);

		/* If we've got user supplied MBC settings use them */
		if (pdata && pdata->num_mbc_cfgs) {
			struct wm8958_mbc_cfg *cfg
				= &pdata->mbc_cfgs[wm8994->mbc_cfg];

			for (i = 0; i < ARRAY_SIZE(cfg->coeff_regs); i++)
				snd_soc_write(codec, i + WM8958_MBC_BAND_1_K_1,
					      cfg->coeff_regs[i]);

			for (i = 0; i < ARRAY_SIZE(cfg->cutoff_regs); i++)
				snd_soc_write(codec,
					      i + WM8958_MBC_BAND_2_LOWER_CUTOFF_C1_1,
					      cfg->cutoff_regs[i]);
		}

		/* Run the DSP */
		snd_soc_write(codec, WM8958_DSP2_EXECCONTROL,
			      WM8958_DSP2_RUNR);

		/* And we're off! */
		snd_soc_update_bits(codec, WM8958_DSP2_CONFIG,
				    WM8958_MBC_ENA | WM8958_MBC_SEL_MASK,
				    mbc << WM8958_MBC_SEL_SHIFT |
				    WM8958_MBC_ENA);
	} else {
		/* If the DSP is already stopped then noop */
		if (!(reg & WM8958_DSP2_ENA))
			return;

		snd_soc_update_bits(codec, WM8958_DSP2_CONFIG,
				    WM8958_MBC_ENA, 0);	
		snd_soc_update_bits(codec, WM8958_DSP2_PROGRAM,
				    WM8958_DSP2_ENA, 0);
		snd_soc_update_bits(codec, WM8994_CLOCKING_1,
				    WM8958_DSP2CLK_ENA, 0);
	}
}

int wm8958_aif_ev(struct snd_soc_dapm_widget *w,
		  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	int i;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
	case SND_SOC_DAPM_PRE_PMU:
		for (i = 0; i < 3; i++)
			wm8958_mbc_apply(codec, i, 1);
		break;
	case SND_SOC_DAPM_POST_PMD:
	case SND_SOC_DAPM_PRE_PMD:
		for (i = 0; i < 3; i++)
			wm8958_mbc_apply(codec, i, 0);
		break;
	}

	return 0;
}

static int wm8958_put_mbc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994_pdata *pdata = wm8994->pdata;
	int value = ucontrol->value.integer.value[0];
	int reg;

	/* Don't allow on the fly reconfiguration */
	reg = snd_soc_read(codec, WM8994_CLOCKING_1);
	if (reg < 0 || reg & WM8958_DSP2CLK_ENA)
		return -EBUSY;

	if (value >= pdata->num_mbc_cfgs)
		return -EINVAL;

	wm8994->mbc_cfg = value;

	return 0;
}

static int wm8958_get_mbc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = wm8994->mbc_cfg;

	return 0;
}

static int wm8958_mbc_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int wm8958_mbc_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int mbc = kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = wm8994->mbc_ena[mbc];

	return 0;
}

static int wm8958_mbc_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int mbc = kcontrol->private_value;
	int i;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] > 1)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(wm8994->mbc_ena); i++) {
		if (mbc != i && wm8994->mbc_ena[i]) {
			dev_dbg(codec->dev, "MBC %d active already\n", mbc);
			return -EBUSY;
		}
	}

	wm8994->mbc_ena[mbc] = ucontrol->value.integer.value[0];

	wm8958_mbc_apply(codec, mbc, wm8994->mbc_ena[mbc]);

	return 0;
}

#define WM8958_MBC_SWITCH(xname, xval) {\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.info = wm8958_mbc_info, \
	.get = wm8958_mbc_get, .put = wm8958_mbc_put, \
	.private_value = xval }

static const struct snd_kcontrol_new wm8958_mbc_snd_controls[] = {
WM8958_MBC_SWITCH("AIF1DAC1 MBC Switch", 0),
WM8958_MBC_SWITCH("AIF1DAC2 MBC Switch", 1),
WM8958_MBC_SWITCH("AIF2DAC MBC Switch", 2),
};

static void wm8958_mbc_loaded(const struct firmware *fw, void *context)
{
	struct snd_soc_codec *codec = context;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	if (fw && wm8958_dsp2_fw(codec, "MBC", fw, true) != 0) {
		mutex_lock(&codec->mutex);
		wm8994->mbc = fw;
		mutex_unlock(&codec->mutex);
	}
}

void wm8958_dsp2_init(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994_pdata *pdata = wm8994->pdata;
	int ret, i;

	snd_soc_add_controls(codec, wm8958_mbc_snd_controls,
			     ARRAY_SIZE(wm8958_mbc_snd_controls));

	/* We don't require firmware and don't want to delay boot */
	request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				"wm8958_mbc.wfw", codec->dev, GFP_KERNEL,
				codec, wm8958_mbc_loaded);

	if (!pdata)
		return;

	if (pdata->num_mbc_cfgs) {
		struct snd_kcontrol_new control[] = {
			SOC_ENUM_EXT("MBC Mode", wm8994->mbc_enum,
				     wm8958_get_mbc_enum, wm8958_put_mbc_enum),
		};

		/* We need an array of texts for the enum API */
		wm8994->mbc_texts = kmalloc(sizeof(char *)
					    * pdata->num_mbc_cfgs, GFP_KERNEL);
		if (!wm8994->mbc_texts) {
			dev_err(wm8994->codec->dev,
				"Failed to allocate %d MBC config texts\n",
				pdata->num_mbc_cfgs);
			return;
		}

		for (i = 0; i < pdata->num_mbc_cfgs; i++)
			wm8994->mbc_texts[i] = pdata->mbc_cfgs[i].name;

		wm8994->mbc_enum.max = pdata->num_mbc_cfgs;
		wm8994->mbc_enum.texts = wm8994->mbc_texts;

		ret = snd_soc_add_controls(wm8994->codec, control, 1);
		if (ret != 0)
			dev_err(wm8994->codec->dev,
				"Failed to add MBC mode controls: %d\n", ret);
	}


}
