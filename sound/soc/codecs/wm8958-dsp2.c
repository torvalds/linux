// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8958-dsp2.c  --  WM8958 DSP2 support
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
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

#include <asm/unaligned.h>

#include "wm8994.h"

#define WM_FW_BLOCK_INFO 0xff
#define WM_FW_BLOCK_PM   0x00
#define WM_FW_BLOCK_X    0x01
#define WM_FW_BLOCK_Y    0x02
#define WM_FW_BLOCK_Z    0x03
#define WM_FW_BLOCK_I    0x06
#define WM_FW_BLOCK_A    0x08
#define WM_FW_BLOCK_C    0x0c

static int wm8958_dsp2_fw(struct snd_soc_component *component, const char *name,
			  const struct firmware *fw, bool check)
{
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);
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
		dev_err(component->dev, "%s: firmware too short (%zd bytes)\n",
			name, fw->size);
		goto err;
	}

	if (memcmp(fw->data, "WMFW", 4) != 0) {
		data32 = get_unaligned_be32(fw->data);
		dev_err(component->dev, "%s: firmware has bad file magic %08x\n",
			name, data32);
		goto err;
	}

	len = get_unaligned_be32(fw->data + 4);
	data32 = get_unaligned_be32(fw->data + 8);

	if ((data32 >> 24) & 0xff) {
		dev_err(component->dev, "%s: unsupported firmware version %d\n",
			name, (data32 >> 24) & 0xff);
		goto err;
	}
	if ((data32 & 0xffff) != 8958) {
		dev_err(component->dev, "%s: unsupported target device %d\n",
			name, data32 & 0xffff);
		goto err;
	}
	if (((data32 >> 16) & 0xff) != 0xc) {
		dev_err(component->dev, "%s: unsupported target core %d\n",
			name, (data32 >> 16) & 0xff);
		goto err;
	}

	if (check) {
		data64 = get_unaligned_be64(fw->data + 24);
		dev_info(component->dev, "%s timestamp %llx\n",  name, data64);
	} else {
		snd_soc_component_write(component, 0x102, 0x2);
		snd_soc_component_write(component, 0x900, 0x2);
	}

	data = fw->data + len;
	len = fw->size - len;
	while (len) {
		if (len < 12) {
			dev_err(component->dev, "%s short data block of %zd\n",
				name, len);
			goto err;
		}

		block_len = get_unaligned_be32(data + 4);
		if (block_len + 8 > len) {
			dev_err(component->dev, "%zd byte block longer than file\n",
				block_len);
			goto err;
		}
		if (block_len == 0) {
			dev_err(component->dev, "Zero length block\n");
			goto err;
		}

		data32 = get_unaligned_be32(data);

		switch ((data32 >> 24) & 0xff) {
		case WM_FW_BLOCK_INFO:
			/* Informational text */
			if (!check)
				break;

			str = kzalloc(block_len + 1, GFP_KERNEL);
			if (str) {
				memcpy(str, data + 8, block_len);
				dev_info(component->dev, "%s: %s\n", name, str);
				kfree(str);
			} else {
				dev_err(component->dev, "Out of memory\n");
			}
			break;
		case WM_FW_BLOCK_PM:
		case WM_FW_BLOCK_X:
		case WM_FW_BLOCK_Y:
		case WM_FW_BLOCK_Z:
		case WM_FW_BLOCK_I:
		case WM_FW_BLOCK_A:
		case WM_FW_BLOCK_C:
			dev_dbg(component->dev, "%s: %zd bytes of %x@%x\n", name,
				block_len, (data32 >> 24) & 0xff,
				data32 & 0xffffff);

			if (check)
				break;

			data32 &= 0xffffff;

			wm8994_bulk_write(wm8994->wm8994,
					  data32 & 0xffffff,
					  block_len / 2,
					  (void *)(data + 8));

			break;
		default:
			dev_warn(component->dev, "%s: unknown block type %d\n",
				 name, (data32 >> 24) & 0xff);
			break;
		}

		/* Round up to the next 32 bit word */
		block_len += block_len % 4;

		data += block_len + 8;
		len -= block_len + 8;
	}

	if (!check) {
		dev_dbg(component->dev, "%s: download done\n", name);
		wm8994->cur_fw = fw;
	} else {
		dev_info(component->dev, "%s: got firmware\n", name);
	}

	goto ok;

err:
	ret = -EINVAL;
ok:
	if (!check) {
		snd_soc_component_write(component, 0x900, 0x0);
		snd_soc_component_write(component, 0x102, 0x0);
	}

	return ret;
}

static void wm8958_dsp_start_mbc(struct snd_soc_component *component, int path)
{
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);
	struct wm8994 *control = wm8994->wm8994;
	int i;

	/* If the DSP is already running then noop */
	if (snd_soc_component_read32(component, WM8958_DSP2_PROGRAM) & WM8958_DSP2_ENA)
		return;

	/* If we have MBC firmware download it */
	if (wm8994->mbc)
		wm8958_dsp2_fw(component, "MBC", wm8994->mbc, false);

	snd_soc_component_update_bits(component, WM8958_DSP2_PROGRAM,
			    WM8958_DSP2_ENA, WM8958_DSP2_ENA);

	/* If we've got user supplied MBC settings use them */
	if (control->pdata.num_mbc_cfgs) {
		struct wm8958_mbc_cfg *cfg
			= &control->pdata.mbc_cfgs[wm8994->mbc_cfg];

		for (i = 0; i < ARRAY_SIZE(cfg->coeff_regs); i++)
			snd_soc_component_write(component, i + WM8958_MBC_BAND_1_K_1,
				      cfg->coeff_regs[i]);

		for (i = 0; i < ARRAY_SIZE(cfg->cutoff_regs); i++)
			snd_soc_component_write(component,
				      i + WM8958_MBC_BAND_2_LOWER_CUTOFF_C1_1,
				      cfg->cutoff_regs[i]);
	}

	/* Run the DSP */
	snd_soc_component_write(component, WM8958_DSP2_EXECCONTROL,
		      WM8958_DSP2_RUNR);

	/* And we're off! */
	snd_soc_component_update_bits(component, WM8958_DSP2_CONFIG,
			    WM8958_MBC_ENA |
			    WM8958_MBC_SEL_MASK,
			    path << WM8958_MBC_SEL_SHIFT |
			    WM8958_MBC_ENA);
}

static void wm8958_dsp_start_vss(struct snd_soc_component *component, int path)
{
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);
	struct wm8994 *control = wm8994->wm8994;
	int i, ena;

	if (wm8994->mbc_vss)
		wm8958_dsp2_fw(component, "MBC+VSS", wm8994->mbc_vss, false);

	snd_soc_component_update_bits(component, WM8958_DSP2_PROGRAM,
			    WM8958_DSP2_ENA, WM8958_DSP2_ENA);

	/* If we've got user supplied settings use them */
	if (control->pdata.num_mbc_cfgs) {
		struct wm8958_mbc_cfg *cfg
			= &control->pdata.mbc_cfgs[wm8994->mbc_cfg];

		for (i = 0; i < ARRAY_SIZE(cfg->combined_regs); i++)
			snd_soc_component_write(component, i + 0x2800,
				      cfg->combined_regs[i]);
	}

	if (control->pdata.num_vss_cfgs) {
		struct wm8958_vss_cfg *cfg
			= &control->pdata.vss_cfgs[wm8994->vss_cfg];

		for (i = 0; i < ARRAY_SIZE(cfg->regs); i++)
			snd_soc_component_write(component, i + 0x2600, cfg->regs[i]);
	}

	if (control->pdata.num_vss_hpf_cfgs) {
		struct wm8958_vss_hpf_cfg *cfg
			= &control->pdata.vss_hpf_cfgs[wm8994->vss_hpf_cfg];

		for (i = 0; i < ARRAY_SIZE(cfg->regs); i++)
			snd_soc_component_write(component, i + 0x2400, cfg->regs[i]);
	}

	/* Run the DSP */
	snd_soc_component_write(component, WM8958_DSP2_EXECCONTROL,
		      WM8958_DSP2_RUNR);

	/* Enable the algorithms we've selected */
	ena = 0;
	if (wm8994->mbc_ena[path])
		ena |= 0x8;
	if (wm8994->hpf2_ena[path])
		ena |= 0x4;
	if (wm8994->hpf1_ena[path])
		ena |= 0x2;
	if (wm8994->vss_ena[path])
		ena |= 0x1;

	snd_soc_component_write(component, 0x2201, ena);

	/* Switch the DSP into the data path */
	snd_soc_component_update_bits(component, WM8958_DSP2_CONFIG,
			    WM8958_MBC_SEL_MASK | WM8958_MBC_ENA,
			    path << WM8958_MBC_SEL_SHIFT | WM8958_MBC_ENA);
}

static void wm8958_dsp_start_enh_eq(struct snd_soc_component *component, int path)
{
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);
	struct wm8994 *control = wm8994->wm8994;
	int i;

	wm8958_dsp2_fw(component, "ENH_EQ", wm8994->enh_eq, false);

	snd_soc_component_update_bits(component, WM8958_DSP2_PROGRAM,
			    WM8958_DSP2_ENA, WM8958_DSP2_ENA);

	/* If we've got user supplied settings use them */
	if (control->pdata.num_enh_eq_cfgs) {
		struct wm8958_enh_eq_cfg *cfg
			= &control->pdata.enh_eq_cfgs[wm8994->enh_eq_cfg];

		for (i = 0; i < ARRAY_SIZE(cfg->regs); i++)
			snd_soc_component_write(component, i + 0x2200,
				      cfg->regs[i]);
	}

	/* Run the DSP */
	snd_soc_component_write(component, WM8958_DSP2_EXECCONTROL,
		      WM8958_DSP2_RUNR);

	/* Switch the DSP into the data path */
	snd_soc_component_update_bits(component, WM8958_DSP2_CONFIG,
			    WM8958_MBC_SEL_MASK | WM8958_MBC_ENA,
			    path << WM8958_MBC_SEL_SHIFT | WM8958_MBC_ENA);
}

static void wm8958_dsp_apply(struct snd_soc_component *component, int path, int start)
{
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);
	int pwr_reg = snd_soc_component_read32(component, WM8994_POWER_MANAGEMENT_5);
	int ena, reg, aif;

	switch (path) {
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
		WARN(1, "Invalid path %d\n", path);
		return;
	}

	/* Do we have both an active AIF and an active algorithm? */
	ena = wm8994->mbc_ena[path] || wm8994->vss_ena[path] ||
		wm8994->hpf1_ena[path] || wm8994->hpf2_ena[path] ||
		wm8994->enh_eq_ena[path];
	if (!pwr_reg)
		ena = 0;

	reg = snd_soc_component_read32(component, WM8958_DSP2_PROGRAM);

	dev_dbg(component->dev, "DSP path %d %d startup: %d, power: %x, DSP: %x\n",
		path, wm8994->dsp_active, start, pwr_reg, reg);

	if (start && ena) {
		/* If the DSP is already running then noop */
		if (reg & WM8958_DSP2_ENA)
			return;

		/* If either AIFnCLK is not yet enabled postpone */
		if (!(snd_soc_component_read32(component, WM8994_AIF1_CLOCKING_1)
		      & WM8994_AIF1CLK_ENA_MASK) &&
		    !(snd_soc_component_read32(component, WM8994_AIF2_CLOCKING_1)
		      & WM8994_AIF2CLK_ENA_MASK))
			return;

		/* Switch the clock over to the appropriate AIF */
		snd_soc_component_update_bits(component, WM8994_CLOCKING_1,
				    WM8958_DSP2CLK_SRC | WM8958_DSP2CLK_ENA,
				    aif << WM8958_DSP2CLK_SRC_SHIFT |
				    WM8958_DSP2CLK_ENA);

		if (wm8994->enh_eq_ena[path])
			wm8958_dsp_start_enh_eq(component, path);
		else if (wm8994->vss_ena[path] || wm8994->hpf1_ena[path] ||
		    wm8994->hpf2_ena[path])
			wm8958_dsp_start_vss(component, path);
		else if (wm8994->mbc_ena[path])
			wm8958_dsp_start_mbc(component, path);

		wm8994->dsp_active = path;

		dev_dbg(component->dev, "DSP running in path %d\n", path);
	}

	if (!start && wm8994->dsp_active == path) {
		/* If the DSP is already stopped then noop */
		if (!(reg & WM8958_DSP2_ENA))
			return;

		snd_soc_component_update_bits(component, WM8958_DSP2_CONFIG,
				    WM8958_MBC_ENA, 0);	
		snd_soc_component_write(component, WM8958_DSP2_EXECCONTROL,
			      WM8958_DSP2_STOP);
		snd_soc_component_update_bits(component, WM8958_DSP2_PROGRAM,
				    WM8958_DSP2_ENA, 0);
		snd_soc_component_update_bits(component, WM8994_CLOCKING_1,
				    WM8958_DSP2CLK_ENA, 0);

		wm8994->dsp_active = -1;

		dev_dbg(component->dev, "DSP stopped\n");
	}
}

int wm8958_aif_ev(struct snd_soc_dapm_widget *w,
		  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int i;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
	case SND_SOC_DAPM_PRE_PMU:
		for (i = 0; i < 3; i++)
			wm8958_dsp_apply(component, i, 1);
		break;
	case SND_SOC_DAPM_POST_PMD:
	case SND_SOC_DAPM_PRE_PMD:
		for (i = 0; i < 3; i++)
			wm8958_dsp_apply(component, i, 0);
		break;
	}

	return 0;
}

/* Check if DSP2 is in use on another AIF */
static int wm8958_dsp2_busy(struct wm8994_priv *wm8994, int aif)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wm8994->mbc_ena); i++) {
		if (i == aif)
			continue;
		if (wm8994->mbc_ena[i] || wm8994->vss_ena[i] ||
		    wm8994->hpf1_ena[i] || wm8994->hpf2_ena[i])
			return 1;
	}

	return 0;
}

static int wm8958_put_mbc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);
	struct wm8994 *control = wm8994->wm8994;
	int value = ucontrol->value.enumerated.item[0];
	int reg;

	/* Don't allow on the fly reconfiguration */
	reg = snd_soc_component_read32(component, WM8994_CLOCKING_1);
	if (reg < 0 || reg & WM8958_DSP2CLK_ENA)
		return -EBUSY;

	if (value >= control->pdata.num_mbc_cfgs)
		return -EINVAL;

	wm8994->mbc_cfg = value;

	return 0;
}

static int wm8958_get_mbc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

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
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wm8994->mbc_ena[mbc];

	return 0;
}

static int wm8958_mbc_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int mbc = kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	if (wm8994->mbc_ena[mbc] == ucontrol->value.integer.value[0])
		return 0;

	if (ucontrol->value.integer.value[0] > 1)
		return -EINVAL;

	if (wm8958_dsp2_busy(wm8994, mbc)) {
		dev_dbg(component->dev, "DSP2 active on %d already\n", mbc);
		return -EBUSY;
	}

	if (wm8994->enh_eq_ena[mbc])
		return -EBUSY;

	wm8994->mbc_ena[mbc] = ucontrol->value.integer.value[0];

	wm8958_dsp_apply(component, mbc, wm8994->mbc_ena[mbc]);

	return 0;
}

#define WM8958_MBC_SWITCH(xname, xval) {\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.info = wm8958_mbc_info, \
	.get = wm8958_mbc_get, .put = wm8958_mbc_put, \
	.private_value = xval }

static int wm8958_put_vss_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);
	struct wm8994 *control = wm8994->wm8994;
	int value = ucontrol->value.enumerated.item[0];
	int reg;

	/* Don't allow on the fly reconfiguration */
	reg = snd_soc_component_read32(component, WM8994_CLOCKING_1);
	if (reg < 0 || reg & WM8958_DSP2CLK_ENA)
		return -EBUSY;

	if (value >= control->pdata.num_vss_cfgs)
		return -EINVAL;

	wm8994->vss_cfg = value;

	return 0;
}

static int wm8958_get_vss_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = wm8994->vss_cfg;

	return 0;
}

static int wm8958_put_vss_hpf_enum(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);
	struct wm8994 *control = wm8994->wm8994;
	int value = ucontrol->value.enumerated.item[0];
	int reg;

	/* Don't allow on the fly reconfiguration */
	reg = snd_soc_component_read32(component, WM8994_CLOCKING_1);
	if (reg < 0 || reg & WM8958_DSP2CLK_ENA)
		return -EBUSY;

	if (value >= control->pdata.num_vss_hpf_cfgs)
		return -EINVAL;

	wm8994->vss_hpf_cfg = value;

	return 0;
}

static int wm8958_get_vss_hpf_enum(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = wm8994->vss_hpf_cfg;

	return 0;
}

static int wm8958_vss_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int wm8958_vss_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int vss = kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wm8994->vss_ena[vss];

	return 0;
}

static int wm8958_vss_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int vss = kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	if (wm8994->vss_ena[vss] == ucontrol->value.integer.value[0])
		return 0;

	if (ucontrol->value.integer.value[0] > 1)
		return -EINVAL;

	if (!wm8994->mbc_vss)
		return -ENODEV;

	if (wm8958_dsp2_busy(wm8994, vss)) {
		dev_dbg(component->dev, "DSP2 active on %d already\n", vss);
		return -EBUSY;
	}

	if (wm8994->enh_eq_ena[vss])
		return -EBUSY;

	wm8994->vss_ena[vss] = ucontrol->value.integer.value[0];

	wm8958_dsp_apply(component, vss, wm8994->vss_ena[vss]);

	return 0;
}


#define WM8958_VSS_SWITCH(xname, xval) {\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.info = wm8958_vss_info, \
	.get = wm8958_vss_get, .put = wm8958_vss_put, \
	.private_value = xval }

static int wm8958_hpf_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int wm8958_hpf_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int hpf = kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	if (hpf < 3)
		ucontrol->value.integer.value[0] = wm8994->hpf1_ena[hpf % 3];
	else
		ucontrol->value.integer.value[0] = wm8994->hpf2_ena[hpf % 3];

	return 0;
}

static int wm8958_hpf_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int hpf = kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	if (hpf < 3) {
		if (wm8994->hpf1_ena[hpf % 3] ==
		    ucontrol->value.integer.value[0])
			return 0;
	} else {
		if (wm8994->hpf2_ena[hpf % 3] ==
		    ucontrol->value.integer.value[0])
			return 0;
	}

	if (ucontrol->value.integer.value[0] > 1)
		return -EINVAL;

	if (!wm8994->mbc_vss)
		return -ENODEV;

	if (wm8958_dsp2_busy(wm8994, hpf % 3)) {
		dev_dbg(component->dev, "DSP2 active on %d already\n", hpf);
		return -EBUSY;
	}

	if (wm8994->enh_eq_ena[hpf % 3])
		return -EBUSY;

	if (hpf < 3)
		wm8994->hpf1_ena[hpf % 3] = ucontrol->value.integer.value[0];
	else
		wm8994->hpf2_ena[hpf % 3] = ucontrol->value.integer.value[0];

	wm8958_dsp_apply(component, hpf % 3, ucontrol->value.integer.value[0]);

	return 0;
}

#define WM8958_HPF_SWITCH(xname, xval) {\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.info = wm8958_hpf_info, \
	.get = wm8958_hpf_get, .put = wm8958_hpf_put, \
	.private_value = xval }

static int wm8958_put_enh_eq_enum(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);
	struct wm8994 *control = wm8994->wm8994;
	int value = ucontrol->value.enumerated.item[0];
	int reg;

	/* Don't allow on the fly reconfiguration */
	reg = snd_soc_component_read32(component, WM8994_CLOCKING_1);
	if (reg < 0 || reg & WM8958_DSP2CLK_ENA)
		return -EBUSY;

	if (value >= control->pdata.num_enh_eq_cfgs)
		return -EINVAL;

	wm8994->enh_eq_cfg = value;

	return 0;
}

static int wm8958_get_enh_eq_enum(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = wm8994->enh_eq_cfg;

	return 0;
}

static int wm8958_enh_eq_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int wm8958_enh_eq_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int eq = kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wm8994->enh_eq_ena[eq];

	return 0;
}

static int wm8958_enh_eq_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int eq = kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	if (wm8994->enh_eq_ena[eq] == ucontrol->value.integer.value[0])
		return 0;

	if (ucontrol->value.integer.value[0] > 1)
		return -EINVAL;

	if (!wm8994->enh_eq)
		return -ENODEV;

	if (wm8958_dsp2_busy(wm8994, eq)) {
		dev_dbg(component->dev, "DSP2 active on %d already\n", eq);
		return -EBUSY;
	}

	if (wm8994->mbc_ena[eq] || wm8994->vss_ena[eq] ||
	    wm8994->hpf1_ena[eq] || wm8994->hpf2_ena[eq])
		return -EBUSY;

	wm8994->enh_eq_ena[eq] = ucontrol->value.integer.value[0];

	wm8958_dsp_apply(component, eq, ucontrol->value.integer.value[0]);

	return 0;
}

#define WM8958_ENH_EQ_SWITCH(xname, xval) {\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.info = wm8958_enh_eq_info, \
	.get = wm8958_enh_eq_get, .put = wm8958_enh_eq_put, \
	.private_value = xval }

static const struct snd_kcontrol_new wm8958_mbc_snd_controls[] = {
WM8958_MBC_SWITCH("AIF1DAC1 MBC Switch", 0),
WM8958_MBC_SWITCH("AIF1DAC2 MBC Switch", 1),
WM8958_MBC_SWITCH("AIF2DAC MBC Switch", 2),
};

static const struct snd_kcontrol_new wm8958_vss_snd_controls[] = {
WM8958_VSS_SWITCH("AIF1DAC1 VSS Switch", 0),
WM8958_VSS_SWITCH("AIF1DAC2 VSS Switch", 1),
WM8958_VSS_SWITCH("AIF2DAC VSS Switch", 2),
WM8958_HPF_SWITCH("AIF1DAC1 HPF1 Switch", 0),
WM8958_HPF_SWITCH("AIF1DAC2 HPF1 Switch", 1),
WM8958_HPF_SWITCH("AIF2DAC HPF1 Switch", 2),
WM8958_HPF_SWITCH("AIF1DAC1 HPF2 Switch", 3),
WM8958_HPF_SWITCH("AIF1DAC2 HPF2 Switch", 4),
WM8958_HPF_SWITCH("AIF2DAC HPF2 Switch", 5),
};

static const struct snd_kcontrol_new wm8958_enh_eq_snd_controls[] = {
WM8958_ENH_EQ_SWITCH("AIF1DAC1 Enhanced EQ Switch", 0),
WM8958_ENH_EQ_SWITCH("AIF1DAC2 Enhanced EQ Switch", 1),
WM8958_ENH_EQ_SWITCH("AIF2DAC Enhanced EQ Switch", 2),
};

static void wm8958_enh_eq_loaded(const struct firmware *fw, void *context)
{
	struct snd_soc_component *component = context;
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	if (fw && (wm8958_dsp2_fw(component, "ENH_EQ", fw, true) == 0)) {
		mutex_lock(&wm8994->fw_lock);
		wm8994->enh_eq = fw;
		mutex_unlock(&wm8994->fw_lock);
	}
}

static void wm8958_mbc_vss_loaded(const struct firmware *fw, void *context)
{
	struct snd_soc_component *component = context;
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	if (fw && (wm8958_dsp2_fw(component, "MBC+VSS", fw, true) == 0)) {
		mutex_lock(&wm8994->fw_lock);
		wm8994->mbc_vss = fw;
		mutex_unlock(&wm8994->fw_lock);
	}
}

static void wm8958_mbc_loaded(const struct firmware *fw, void *context)
{
	struct snd_soc_component *component = context;
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);

	if (fw && (wm8958_dsp2_fw(component, "MBC", fw, true) == 0)) {
		mutex_lock(&wm8994->fw_lock);
		wm8994->mbc = fw;
		mutex_unlock(&wm8994->fw_lock);
	}
}

void wm8958_dsp2_init(struct snd_soc_component *component)
{
	struct wm8994_priv *wm8994 = snd_soc_component_get_drvdata(component);
	struct wm8994 *control = wm8994->wm8994;
	struct wm8994_pdata *pdata = &control->pdata;
	int ret, i;

	wm8994->dsp_active = -1;

	snd_soc_add_component_controls(component, wm8958_mbc_snd_controls,
			     ARRAY_SIZE(wm8958_mbc_snd_controls));
	snd_soc_add_component_controls(component, wm8958_vss_snd_controls,
			     ARRAY_SIZE(wm8958_vss_snd_controls));
	snd_soc_add_component_controls(component, wm8958_enh_eq_snd_controls,
			     ARRAY_SIZE(wm8958_enh_eq_snd_controls));


	/* We don't *require* firmware and don't want to delay boot */
	request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				"wm8958_mbc.wfw", component->dev, GFP_KERNEL,
				component, wm8958_mbc_loaded);
	request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				"wm8958_mbc_vss.wfw", component->dev, GFP_KERNEL,
				component, wm8958_mbc_vss_loaded);
	request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				"wm8958_enh_eq.wfw", component->dev, GFP_KERNEL,
				component, wm8958_enh_eq_loaded);

	if (pdata->num_mbc_cfgs) {
		struct snd_kcontrol_new control[] = {
			SOC_ENUM_EXT("MBC Mode", wm8994->mbc_enum,
				     wm8958_get_mbc_enum, wm8958_put_mbc_enum),
		};

		/* We need an array of texts for the enum API */
		wm8994->mbc_texts = kmalloc_array(pdata->num_mbc_cfgs,
						  sizeof(char *),
						  GFP_KERNEL);
		if (!wm8994->mbc_texts)
			return;

		for (i = 0; i < pdata->num_mbc_cfgs; i++)
			wm8994->mbc_texts[i] = pdata->mbc_cfgs[i].name;

		wm8994->mbc_enum.items = pdata->num_mbc_cfgs;
		wm8994->mbc_enum.texts = wm8994->mbc_texts;

		ret = snd_soc_add_component_controls(wm8994->hubs.component,
						 control, 1);
		if (ret != 0)
			dev_err(wm8994->hubs.component->dev,
				"Failed to add MBC mode controls: %d\n", ret);
	}

	if (pdata->num_vss_cfgs) {
		struct snd_kcontrol_new control[] = {
			SOC_ENUM_EXT("VSS Mode", wm8994->vss_enum,
				     wm8958_get_vss_enum, wm8958_put_vss_enum),
		};

		/* We need an array of texts for the enum API */
		wm8994->vss_texts = kmalloc_array(pdata->num_vss_cfgs,
						  sizeof(char *),
						  GFP_KERNEL);
		if (!wm8994->vss_texts)
			return;

		for (i = 0; i < pdata->num_vss_cfgs; i++)
			wm8994->vss_texts[i] = pdata->vss_cfgs[i].name;

		wm8994->vss_enum.items = pdata->num_vss_cfgs;
		wm8994->vss_enum.texts = wm8994->vss_texts;

		ret = snd_soc_add_component_controls(wm8994->hubs.component,
						 control, 1);
		if (ret != 0)
			dev_err(wm8994->hubs.component->dev,
				"Failed to add VSS mode controls: %d\n", ret);
	}

	if (pdata->num_vss_hpf_cfgs) {
		struct snd_kcontrol_new control[] = {
			SOC_ENUM_EXT("VSS HPF Mode", wm8994->vss_hpf_enum,
				     wm8958_get_vss_hpf_enum,
				     wm8958_put_vss_hpf_enum),
		};

		/* We need an array of texts for the enum API */
		wm8994->vss_hpf_texts = kmalloc_array(pdata->num_vss_hpf_cfgs,
						      sizeof(char *),
						      GFP_KERNEL);
		if (!wm8994->vss_hpf_texts)
			return;

		for (i = 0; i < pdata->num_vss_hpf_cfgs; i++)
			wm8994->vss_hpf_texts[i] = pdata->vss_hpf_cfgs[i].name;

		wm8994->vss_hpf_enum.items = pdata->num_vss_hpf_cfgs;
		wm8994->vss_hpf_enum.texts = wm8994->vss_hpf_texts;

		ret = snd_soc_add_component_controls(wm8994->hubs.component,
						 control, 1);
		if (ret != 0)
			dev_err(wm8994->hubs.component->dev,
				"Failed to add VSS HPFmode controls: %d\n",
				ret);
	}

	if (pdata->num_enh_eq_cfgs) {
		struct snd_kcontrol_new control[] = {
			SOC_ENUM_EXT("Enhanced EQ Mode", wm8994->enh_eq_enum,
				     wm8958_get_enh_eq_enum,
				     wm8958_put_enh_eq_enum),
		};

		/* We need an array of texts for the enum API */
		wm8994->enh_eq_texts = kmalloc_array(pdata->num_enh_eq_cfgs,
						     sizeof(char *),
						     GFP_KERNEL);
		if (!wm8994->enh_eq_texts)
			return;

		for (i = 0; i < pdata->num_enh_eq_cfgs; i++)
			wm8994->enh_eq_texts[i] = pdata->enh_eq_cfgs[i].name;

		wm8994->enh_eq_enum.items = pdata->num_enh_eq_cfgs;
		wm8994->enh_eq_enum.texts = wm8994->enh_eq_texts;

		ret = snd_soc_add_component_controls(wm8994->hubs.component,
						 control, 1);
		if (ret != 0)
			dev_err(wm8994->hubs.component->dev,
				"Failed to add enhanced EQ controls: %d\n",
				ret);
	}
}
