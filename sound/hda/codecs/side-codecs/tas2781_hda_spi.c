// SPDX-License-Identifier: GPL-2.0
//
// TAS2781 HDA SPI driver
//
// Copyright 2024 - 2025 Texas Instruments, Inc.
//
// Author: Baojun Xu <baojun.xu@ti.com>

#include <linux/acpi.h>
#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/crc8.h>
#include <linux/crc32.h>
#include <linux/efi.h>
#include <linux/firmware.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/units.h>

#include <sound/hda_codec.h>
#include <sound/soc.h>
#include <sound/tas2781.h>
#include <sound/tlv.h>
#include <sound/tas2781-tlv.h>

#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_component.h"
#include "hda_jack.h"
#include "../generic.h"
#include "tas2781_hda.h"

#define TASDEVICE_RANGE_MAX_SIZE	(256 * 128)
#define TASDEVICE_WIN_LEN		128
#define TAS2781_SPI_MAX_FREQ		(4 * HZ_PER_MHZ)
/* Flag of calibration registers address. */
#define TASDEVICE_CALIBRATION_REG_ADDRESS	BIT(7)
#define TASDEV_UEFI_CALI_REG_ADDR_FLG	BIT(7)

/* System Reset Check Register */
#define TAS2781_REG_CLK_CONFIG		TASDEVICE_REG(0x0, 0x0, 0x5c)
#define TAS2781_REG_CLK_CONFIG_RESET	0x19

struct tas2781_hda_spi_priv {
	struct snd_kcontrol *snd_ctls[3];
};

static const struct regmap_range_cfg tasdevice_ranges[] = {
	{
		.range_min = 0,
		.range_max = TASDEVICE_RANGE_MAX_SIZE,
		.selector_reg = TASDEVICE_PAGE_SELECT,
		.selector_mask = GENMASK(7, 0),
		.selector_shift = 0,
		.window_start = 0,
		.window_len = TASDEVICE_WIN_LEN,
	},
};

static const struct regmap_config tasdevice_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.zero_flag_mask = true,
	.read_flag_mask = 0x01,
	.reg_shift = -1,
	.cache_type = REGCACHE_NONE,
	.ranges = tasdevice_ranges,
	.num_ranges = ARRAY_SIZE(tasdevice_ranges),
	.max_register = TASDEVICE_RANGE_MAX_SIZE,
};

static int tasdevice_spi_dev_read(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned int *val)
{
	int ret;

	/*
	 * In our TAS2781 SPI mode, if read from other book (not book 0),
	 * or read from page number larger than 1 in book 0, one more byte
	 * read is needed, and first byte is a dummy byte, need to be ignored.
	 */
	if ((TASDEVICE_BOOK_ID(reg) > 0) || (TASDEVICE_PAGE_ID(reg) > 1)) {
		unsigned char data[2];

		ret = tasdevice_dev_bulk_read(tas_priv, chn, reg,
			data, sizeof(data));
		*val = data[1];
	} else {
		ret = tasdevice_dev_read(tas_priv, chn, reg, val);
	}
	if (ret < 0)
		dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);

	return ret;
}

static int tasdevice_spi_dev_bulk_read(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned char *data,
	unsigned int len)
{
	int ret;

	/*
	 * In our TAS2781 SPI mode, if read from other book (not book 0),
	 * or read from page number larger than 1 in book 0, one more byte
	 * read is needed, and first byte is a dummy byte, need to be ignored.
	 */
	if ((TASDEVICE_BOOK_ID(reg) > 0) || (TASDEVICE_PAGE_ID(reg) > 1)) {
		unsigned char buf[TASDEVICE_WIN_LEN + 1];

		ret = tasdevice_dev_bulk_read(tas_priv, chn, reg,
			buf, len + 1);
		memcpy(data, buf + 1, len);
	} else {
		ret = tasdevice_dev_bulk_read(tas_priv, chn, reg, data, len);
	}
	if (ret < 0)
		dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);

	return ret;
}

static int tasdevice_spi_dev_update_bits(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned int mask,
	unsigned int value)
{
	int ret, val;

	/*
	 * In our TAS2781 SPI mode, read/write was masked in last bit of
	 * address, it cause regmap_update_bits() not work as expected.
	 */
	ret = tasdevice_dev_read(tas_priv, chn, reg, &val);
	if (ret < 0) {
		dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);
		return ret;
	}

	ret = tasdevice_dev_write(tas_priv, chn, TASDEVICE_PAGE_REG(reg),
		(val & ~mask) | (mask & value));
	if (ret < 0)
		dev_err(tas_priv->dev, "%s, E=%d\n", __func__, ret);

	return ret;
}

static int tasdevice_spi_change_chn_book(struct tasdevice_priv *p,
	unsigned short chn, int book)
{
	int ret = 0;

	if (chn == p->index) {
		struct tasdevice *tasdev = &p->tasdevice[chn];
		struct regmap *map = p->regmap;

		if (tasdev->cur_book != book) {
			ret = regmap_write(map, TASDEVICE_BOOKCTL_REG, book);
			if (ret < 0)
				dev_err(p->dev, "%s, E=%d\n", __func__, ret);
			else
				tasdev->cur_book = book;
		}
	} else {
		ret = -EXDEV;
		dev_dbg(p->dev, "Not error, %s ignore channel(%d)\n",
			__func__, chn);
	}

	return ret;
}

static void tas2781_spi_reset(struct tasdevice_priv *tas_dev)
{
	int ret;

	if (tas_dev->reset) {
		gpiod_set_value_cansleep(tas_dev->reset, 0);
		fsleep(800);
		gpiod_set_value_cansleep(tas_dev->reset, 1);
	} else {
		ret = tasdevice_dev_write(tas_dev, tas_dev->index,
			TASDEVICE_REG_SWRESET, TASDEVICE_REG_SWRESET_RESET);
		if (ret < 0) {
			dev_err(tas_dev->dev, "dev sw-reset fail, %d\n", ret);
			return;
		}
		fsleep(1000);
	}
}

static int tascodec_spi_init(struct tasdevice_priv *tas_priv,
	void *codec, struct module *module,
	void (*cont)(const struct firmware *fw, void *context))
{
	int ret;

	/*
	 * Codec Lock Hold to ensure that codec_probe and firmware parsing and
	 * loading do not simultaneously execute.
	 */
	guard(mutex)(&tas_priv->codec_lock);

	scnprintf(tas_priv->rca_binaryname,
		sizeof(tas_priv->rca_binaryname), "%sRCA%d.bin",
		tas_priv->dev_name, tas_priv->ndev);
	crc8_populate_msb(tas_priv->crc8_lkp_tbl, TASDEVICE_CRC8_POLYNOMIAL);
	tas_priv->codec = codec;
	ret = request_firmware_nowait(module, FW_ACTION_UEVENT,
		tas_priv->rca_binaryname, tas_priv->dev, GFP_KERNEL, tas_priv,
		cont);
	if (ret)
		dev_err(tas_priv->dev, "request_firmware_nowait err:0x%08x\n",
			ret);

	return ret;
}

static void tasdevice_spi_init(struct tasdevice_priv *tas_priv)
{
	tas_priv->tasdevice[tas_priv->index].cur_book = -1;
	tas_priv->tasdevice[tas_priv->index].cur_conf = -1;
	tas_priv->tasdevice[tas_priv->index].cur_prog = -1;

	tas_priv->isspi = true;

	tas_priv->update_bits = tasdevice_spi_dev_update_bits;
	tas_priv->change_chn_book = tasdevice_spi_change_chn_book;
	tas_priv->dev_read = tasdevice_spi_dev_read;
	tas_priv->dev_bulk_read = tasdevice_spi_dev_bulk_read;

	mutex_init(&tas_priv->codec_lock);
}

static int tasdevice_spi_amp_putvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc)
{
	unsigned int invert = mc->invert;
	unsigned char mask;
	int max = mc->max;
	int val, ret;

	mask = rounddown_pow_of_two(max);
	mask <<= mc->shift;
	val =  clamp(invert ? max - ucontrol->value.integer.value[0] :
		ucontrol->value.integer.value[0], 0, max);

	ret = tasdevice_spi_dev_update_bits(tas_priv, tas_priv->index,
		mc->reg, mask, (unsigned int)(val << mc->shift));
	if (ret)
		dev_err(tas_priv->dev, "set AMP vol error in dev %d\n",
			tas_priv->index);

	return ret;
}

static int tasdevice_spi_amp_getvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc)
{
	unsigned int invert = mc->invert;
	unsigned char mask = 0;
	int max = mc->max;
	int ret, val;

	ret = tasdevice_spi_dev_read(tas_priv, tas_priv->index, mc->reg, &val);
	if (ret) {
		dev_err(tas_priv->dev, "%s, get AMP vol error\n", __func__);
		return ret;
	}

	mask = rounddown_pow_of_two(max);
	mask <<= mc->shift;
	val = (val & mask) >> mc->shift;
	val = clamp(invert ? max - val : val, 0, max);
	ucontrol->value.integer.value[0] = val;

	return ret;
}

static int tasdevice_spi_digital_putvol(struct tasdevice_priv *p,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc)
{
	unsigned int invert = mc->invert;
	int max = mc->max;
	int val, ret;

	val = clamp(invert ? max - ucontrol->value.integer.value[0] :
		ucontrol->value.integer.value[0], 0, max);
	ret = tasdevice_dev_write(p, p->index, mc->reg, (unsigned int)val);
	if (ret)
		dev_err(p->dev, "set digital vol err in dev %d\n", p->index);

	return ret;
}

static int tasdevice_spi_digital_getvol(struct tasdevice_priv *p,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc)
{
	unsigned int invert = mc->invert;
	int max = mc->max;
	int ret, val;

	ret = tasdevice_spi_dev_read(p, p->index, mc->reg, &val);
	if (ret) {
		dev_err(p->dev, "%s, get digital vol err\n", __func__);
		return ret;
	}

	val = clamp(invert ? max - val : val, 0, max);
	ucontrol->value.integer.value[0] = val;

	return ret;
}

static int tas2781_read_acpi(struct tas2781_hda *tas_hda,
	const char *hid, int id)
{
	struct tasdevice_priv *p = tas_hda->priv;
	struct acpi_device *adev;
	struct device *physdev;
	u32 values[HDA_MAX_COMPONENTS];
	const char *property;
	size_t nval;
	int ret, i;

	adev = acpi_dev_get_first_match_dev(hid, NULL, -1);
	if (!adev) {
		dev_err(p->dev, "Failed to find ACPI device: %s\n", hid);
		return -ENODEV;
	}

	strscpy(p->dev_name, hid, sizeof(p->dev_name));
	physdev = get_device(acpi_get_first_physical_node(adev));
	acpi_dev_put(adev);

	property = "ti,dev-index";
	ret = device_property_count_u32(physdev, property);
	if (ret <= 0 || ret > ARRAY_SIZE(values)) {
		ret = -EINVAL;
		goto err;
	}
	p->ndev = nval = ret;

	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err;

	p->index = U8_MAX;
	for (i = 0; i < nval; i++) {
		if (values[i] == id) {
			p->index = i;
			break;
		}
	}
	if (p->index == U8_MAX) {
		dev_dbg(p->dev, "No index found in %s\n", property);
		ret = -ENODEV;
		goto err;
	}

	if (p->index == 0) {
		/* All of amps share same RESET pin. */
		p->reset = devm_gpiod_get_index_optional(physdev, "reset",
			p->index, GPIOD_OUT_LOW);
		if (IS_ERR(p->reset)) {
			ret = PTR_ERR(p->reset);
			dev_err_probe(p->dev, ret, "Failed on reset GPIO\n");
			goto err;
		}
	}
	put_device(physdev);

	return 0;
err:
	dev_err(p->dev, "read acpi error, ret: %d\n", ret);
	put_device(physdev);
	acpi_dev_put(adev);

	return ret;
}

static void tas2781_hda_playback_hook(struct device *dev, int action)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);
	struct tasdevice_priv *tas_priv = tas_hda->priv;

	if (action == HDA_GEN_PCM_ACT_OPEN) {
		pm_runtime_get_sync(dev);
		guard(mutex)(&tas_priv->codec_lock);
		if (tas_priv->fw_state == TASDEVICE_DSP_FW_ALL_OK)
			tasdevice_tuning_switch(tas_hda->priv, 0);
	} else if (action == HDA_GEN_PCM_ACT_CLOSE) {
		guard(mutex)(&tas_priv->codec_lock);
		if (tas_priv->fw_state == TASDEVICE_DSP_FW_ALL_OK)
			tasdevice_tuning_switch(tas_priv, 1);
		pm_runtime_put_autosuspend(dev);
	}
}

/*
 * tas2781_digital_getvol - get the volum control
 * @kcontrol: control pointer
 * @ucontrol: User data
 *
 * Customer Kcontrol for tas2781 is primarily for regmap booking, paging
 * depends on internal regmap mechanism.
 * tas2781 contains book and page two-level register map, especially
 * book switching will set the register BXXP00R7F, after switching to the
 * correct book, then leverage the mechanism for paging to access the
 * register.
 *
 * Return 0 if succeeded.
 */
static int tas2781_digital_getvol(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	guard(mutex)(&tas_priv->codec_lock);
	return tasdevice_spi_digital_getvol(tas_priv, ucontrol, mc);
}

static int tas2781_amp_getvol(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	guard(mutex)(&tas_priv->codec_lock);
	return tasdevice_spi_amp_getvol(tas_priv, ucontrol, mc);
}

static int tas2781_digital_putvol(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	guard(mutex)(&tas_priv->codec_lock);
	return tasdevice_spi_digital_putvol(tas_priv, ucontrol, mc);
}

static int tas2781_amp_putvol(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	guard(mutex)(&tas_priv->codec_lock);
	return tasdevice_spi_amp_putvol(tas_priv, ucontrol, mc);
}

static int tas2781_force_fwload_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = (int)tas_priv->force_fwload_status;
	dev_dbg(tas_priv->dev, "%s : Force FWload %s\n", __func__,
		str_on_off(tas_priv->force_fwload_status));

	return 0;
}

static int tas2781_force_fwload_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	bool change, val = (bool)ucontrol->value.integer.value[0];

	if (tas_priv->force_fwload_status == val) {
		change = false;
	} else {
		change = true;
		tas_priv->force_fwload_status = val;
	}
	dev_dbg(tas_priv->dev, "%s : Force FWload %s\n", __func__,
		str_on_off(tas_priv->force_fwload_status));

	return change;
}

static struct snd_kcontrol_new tas2781_snd_ctls[] = {
	ACARD_SINGLE_RANGE_EXT_TLV(NULL, TAS2781_AMP_LEVEL, 1, 0, 20, 0,
		tas2781_amp_getvol, tas2781_amp_putvol,
		tas2781_amp_tlv),
	ACARD_SINGLE_RANGE_EXT_TLV(NULL, TAS2781_DVC_LVL, 0, 0, 200, 1,
		tas2781_digital_getvol, tas2781_digital_putvol,
		tas2781_dvc_tlv),
	ACARD_SINGLE_BOOL_EXT(NULL, 0, tas2781_force_fwload_get,
		tas2781_force_fwload_put),
};

static struct snd_kcontrol_new tas2781_prof_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.info = tasdevice_info_profile,
	.get = tasdevice_get_profile_id,
	.put = tasdevice_set_profile_id,
};

static struct snd_kcontrol_new tas2781_dsp_ctls[] = {
	/* Speaker Program */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_CARD,
		.info = tasdevice_info_programs,
		.get = tasdevice_program_get,
		.put = tasdevice_program_put,
	},
	/* Speaker Config */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_CARD,
		.info = tasdevice_info_config,
		.get = tasdevice_config_get,
		.put = tasdevice_config_put,
	},
};

static void tas2781_hda_remove_controls(struct tas2781_hda *tas_hda)
{
	struct hda_codec *codec = tas_hda->priv->codec;
	struct tas2781_hda_spi_priv *h_priv = tas_hda->hda_priv;

	snd_ctl_remove(codec->card, tas_hda->dsp_prog_ctl);

	snd_ctl_remove(codec->card, tas_hda->dsp_conf_ctl);

	for (int i = ARRAY_SIZE(h_priv->snd_ctls) - 1; i >= 0; i--)
		snd_ctl_remove(codec->card, h_priv->snd_ctls[i]);

	snd_ctl_remove(codec->card, tas_hda->prof_ctl);
}

static int tas2781_hda_spi_prf_ctl(struct tas2781_hda *h)
{
	struct tasdevice_priv *p = h->priv;
	struct hda_codec *c = p->codec;
	char name[64];
	int rc;

	snprintf(name, sizeof(name), "Speaker-%d Profile Id", p->index);
	tas2781_prof_ctl.name = name;
	h->prof_ctl = snd_ctl_new1(&tas2781_prof_ctl, p);
	rc = snd_ctl_add(c->card, h->prof_ctl);
	if (rc)
		dev_err(p->dev, "Failed to add KControl: %s, rc = %d\n",
			tas2781_prof_ctl.name, rc);
	return rc;
}

static int tas2781_hda_spi_snd_ctls(struct tas2781_hda *h)
{
	struct tas2781_hda_spi_priv *h_priv = h->hda_priv;
	struct tasdevice_priv *p = h->priv;
	struct hda_codec *c = p->codec;
	char name[64];
	int i = 0;
	int rc;

	snprintf(name, sizeof(name), "Speaker-%d Analog Volume", p->index);
	tas2781_snd_ctls[i].name = name;
	h_priv->snd_ctls[i] = snd_ctl_new1(&tas2781_snd_ctls[i], p);
	rc = snd_ctl_add(c->card, h_priv->snd_ctls[i]);
	if (rc) {
		dev_err(p->dev, "Failed to add KControl: %s, rc = %d\n",
			tas2781_snd_ctls[i].name, rc);
		return rc;
	}
	i++;
	snprintf(name, sizeof(name), "Speaker-%d Digital Volume", p->index);
	tas2781_snd_ctls[i].name = name;
	h_priv->snd_ctls[i] = snd_ctl_new1(&tas2781_snd_ctls[i], p);
	rc = snd_ctl_add(c->card, h_priv->snd_ctls[i]);
	if (rc) {
		dev_err(p->dev, "Failed to add KControl: %s, rc = %d\n",
			tas2781_snd_ctls[i].name, rc);
		return rc;
	}
	i++;
	snprintf(name, sizeof(name), "Froce Speaker-%d FW Load", p->index);
	tas2781_snd_ctls[i].name = name;
	h_priv->snd_ctls[i] = snd_ctl_new1(&tas2781_snd_ctls[i], p);
	rc = snd_ctl_add(c->card, h_priv->snd_ctls[i]);
	if (rc) {
		dev_err(p->dev, "Failed to add KControl: %s, rc = %d\n",
			tas2781_snd_ctls[i].name, rc);
	}
	return rc;
}

static int tas2781_hda_spi_dsp_ctls(struct tas2781_hda *h)
{
	struct tasdevice_priv *p = h->priv;
	struct hda_codec *c = p->codec;
	char name[64];
	int i = 0;
	int rc;

	snprintf(name, sizeof(name), "Speaker-%d Program Id", p->index);
	tas2781_dsp_ctls[i].name = name;
	h->dsp_prog_ctl = snd_ctl_new1(&tas2781_dsp_ctls[i], p);
	rc = snd_ctl_add(c->card, h->dsp_prog_ctl);
	if (rc) {
		dev_err(p->dev, "Failed to add KControl: %s, rc = %d\n",
			tas2781_dsp_ctls[i].name, rc);
		return rc;
	}
	i++;
	snprintf(name, sizeof(name), "Speaker-%d Config Id", p->index);
	tas2781_dsp_ctls[i].name = name;
	h->dsp_conf_ctl = snd_ctl_new1(&tas2781_dsp_ctls[i], p);
	rc = snd_ctl_add(c->card, h->dsp_conf_ctl);
	if (rc) {
		dev_err(p->dev, "Failed to add KControl: %s, rc = %d\n",
			tas2781_dsp_ctls[i].name, rc);
	}

	return rc;
}

static void tasdev_fw_ready(const struct firmware *fmw, void *context)
{
	struct tasdevice_priv *tas_priv = context;
	struct tas2781_hda *tas_hda = dev_get_drvdata(tas_priv->dev);
	struct hda_codec *codec = tas_priv->codec;
	int ret, val;

	pm_runtime_get_sync(tas_priv->dev);
	guard(mutex)(&tas_priv->codec_lock);

	ret = tasdevice_rca_parser(tas_priv, fmw);
	if (ret)
		goto out;

	/* Add control one time only. */
	ret = tas2781_hda_spi_prf_ctl(tas_hda);
	if (ret)
		goto out;

	ret = tas2781_hda_spi_snd_ctls(tas_hda);
	if (ret)
		goto out;

	tasdevice_dsp_remove(tas_priv);

	tas_priv->fw_state = TASDEVICE_DSP_FW_PENDING;
	scnprintf(tas_priv->coef_binaryname, 64, "TAS2XXX%04X-%01d.bin",
		lower_16_bits(codec->core.subsystem_id), tas_priv->index);
	ret = tasdevice_dsp_parser(tas_priv);
	if (ret) {
		dev_err(tas_priv->dev, "dspfw load %s error\n",
			tas_priv->coef_binaryname);
		tas_priv->fw_state = TASDEVICE_DSP_FW_FAIL;
		goto out;
	}

	ret = tas2781_hda_spi_dsp_ctls(tas_hda);
	if (ret)
		goto out;
	/* Perform AMP reset before firmware download. */
	tas2781_spi_reset(tas_priv);
	tas_priv->rcabin.profile_cfg_id = 0;

	tas_priv->fw_state = TASDEVICE_DSP_FW_ALL_OK;
	ret = tasdevice_spi_dev_read(tas_priv, tas_priv->index,
		TAS2781_REG_CLK_CONFIG, &val);
	if (ret < 0)
		goto out;

	if (val == TAS2781_REG_CLK_CONFIG_RESET) {
		ret = tasdevice_prmg_load(tas_priv, 0);
		if (ret < 0) {
			dev_err(tas_priv->dev, "FW download failed = %d\n",
				ret);
			goto out;
		}
		tas_priv->fw_state = TASDEVICE_DSP_FW_ALL_OK;
	}
	if (tas_priv->fmw->nr_programs > 0)
		tas_priv->tasdevice[tas_priv->index].cur_prog = 0;
	if (tas_priv->fmw->nr_configurations > 0)
		tas_priv->tasdevice[tas_priv->index].cur_conf = 0;

	/*
	 * If calibrated data occurs error, dsp will still works with default
	 * calibrated data inside algo.
	 */
	tas2781_save_calibration(tas_hda);
out:
	release_firmware(fmw);
	pm_runtime_put_autosuspend(tas_hda->priv->dev);
}

static int tas2781_hda_bind(struct device *dev, struct device *master,
	void *master_data)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);
	struct hda_component_parent *parent = master_data;
	struct hda_component *comp;
	struct hda_codec *codec;
	int ret;

	comp = hda_component_from_index(parent, tas_hda->priv->index);
	if (!comp)
		return -EINVAL;

	if (comp->dev)
		return -EBUSY;

	codec = parent->codec;

	pm_runtime_get_sync(dev);

	comp->dev = dev;

	strscpy(comp->name, dev_name(dev), sizeof(comp->name));

	ret = tascodec_spi_init(tas_hda->priv, codec, THIS_MODULE,
		tasdev_fw_ready);
	if (!ret)
		comp->playback_hook = tas2781_hda_playback_hook;

	pm_runtime_put_autosuspend(dev);

	return ret;
}

static void tas2781_hda_unbind(struct device *dev, struct device *master,
			       void *master_data)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);
	struct hda_component_parent *parent = master_data;
	struct tasdevice_priv *tas_priv = tas_hda->priv;
	struct hda_component *comp;

	comp = hda_component_from_index(parent, tas_priv->index);
	if (comp && (comp->dev == dev)) {
		comp->dev = NULL;
		memset(comp->name, 0, sizeof(comp->name));
		comp->playback_hook = NULL;
	}

	tas2781_hda_remove_controls(tas_hda);

	tasdevice_config_info_remove(tas_priv);
	tasdevice_dsp_remove(tas_priv);

	tas_hda->priv->fw_state = TASDEVICE_DSP_FW_PENDING;
}

static const struct component_ops tas2781_hda_comp_ops = {
	.bind = tas2781_hda_bind,
	.unbind = tas2781_hda_unbind,
};

static int tas2781_hda_spi_probe(struct spi_device *spi)
{
	struct tas2781_hda_spi_priv *hda_priv;
	struct tasdevice_priv *tas_priv;
	struct tas2781_hda *tas_hda;
	const char *device_name;
	int ret = 0;

	tas_hda = devm_kzalloc(&spi->dev, sizeof(*tas_hda), GFP_KERNEL);
	if (!tas_hda)
		return -ENOMEM;

	hda_priv = devm_kzalloc(&spi->dev, sizeof(*hda_priv), GFP_KERNEL);
	if (!hda_priv)
		return -ENOMEM;

	tas_hda->hda_priv = hda_priv;
	spi->max_speed_hz = TAS2781_SPI_MAX_FREQ;

	tas_priv = devm_kzalloc(&spi->dev, sizeof(*tas_priv), GFP_KERNEL);
	if (!tas_priv)
		return -ENOMEM;
	tas_priv->dev = &spi->dev;
	tas_hda->priv = tas_priv;
	tas_priv->regmap = devm_regmap_init_spi(spi, &tasdevice_regmap);
	if (IS_ERR(tas_priv->regmap)) {
		ret = PTR_ERR(tas_priv->regmap);
		dev_err(tas_priv->dev, "Failed to allocate regmap: %d\n",
			ret);
		return ret;
	}
	if (strstr(dev_name(&spi->dev), "TXNW2781")) {
		device_name = "TXNW2781";
	} else {
		dev_err(tas_priv->dev, "Unmatched spi dev %s\n",
			dev_name(&spi->dev));
		return -ENODEV;
	}

	tas_priv->irq = spi->irq;
	dev_set_drvdata(&spi->dev, tas_hda);
	ret = tas2781_read_acpi(tas_hda, device_name,
				spi_get_chipselect(spi, 0));
	if (ret)
		return dev_err_probe(tas_priv->dev, ret,
				"Platform not supported\n");

	tasdevice_spi_init(tas_priv);

	pm_runtime_set_autosuspend_delay(tas_priv->dev, 3000);
	pm_runtime_use_autosuspend(tas_priv->dev);
	pm_runtime_set_active(tas_priv->dev);
	pm_runtime_get_noresume(tas_priv->dev);
	pm_runtime_enable(tas_priv->dev);

	pm_runtime_put_autosuspend(tas_priv->dev);

	ret = component_add(tas_priv->dev, &tas2781_hda_comp_ops);
	if (ret) {
		dev_err(tas_priv->dev, "Register component fail: %d\n", ret);
		pm_runtime_disable(tas_priv->dev);
		tas2781_hda_remove(&spi->dev, &tas2781_hda_comp_ops);
	}

	return ret;
}

static void tas2781_hda_spi_remove(struct spi_device *spi)
{
	tas2781_hda_remove(&spi->dev, &tas2781_hda_comp_ops);
}

static int tas2781_runtime_suspend(struct device *dev)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);
	struct tasdevice_priv *tas_priv = tas_hda->priv;

	guard(mutex)(&tas_priv->codec_lock);

	if (tas_priv->fw_state == TASDEVICE_DSP_FW_ALL_OK
		&& tas_priv->playback_started)
		tasdevice_tuning_switch(tas_priv, 1);

	tas_priv->tasdevice[tas_priv->index].cur_book = -1;
	tas_priv->tasdevice[tas_priv->index].cur_conf = -1;

	return 0;
}

static int tas2781_runtime_resume(struct device *dev)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);
	struct tasdevice_priv *tas_priv = tas_hda->priv;

	guard(mutex)(&tas_priv->codec_lock);

	if (tas_priv->fw_state == TASDEVICE_DSP_FW_ALL_OK
		&& tas_priv->playback_started)
		tasdevice_tuning_switch(tas_priv, 0);

	return 0;
}

static int tas2781_system_suspend(struct device *dev)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);
	struct tasdevice_priv *tas_priv = tas_hda->priv;
	int ret;

	ret = pm_runtime_force_suspend(dev);
	if (ret)
		return ret;

	/* Shutdown chip before system suspend */
	if (tas_priv->fw_state == TASDEVICE_DSP_FW_ALL_OK
		&& tas_priv->playback_started)
		tasdevice_tuning_switch(tas_priv, 1);

	return 0;
}

static int tas2781_system_resume(struct device *dev)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);
	struct tasdevice_priv *tas_priv = tas_hda->priv;
	int ret, val;

	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	guard(mutex)(&tas_priv->codec_lock);
	ret = tas_priv->dev_read(tas_priv, tas_priv->index,
		TAS2781_REG_CLK_CONFIG, &val);
	if (ret < 0)
		return ret;

	if (val == TAS2781_REG_CLK_CONFIG_RESET) {
		tas_priv->tasdevice[tas_priv->index].cur_book = -1;
		tas_priv->tasdevice[tas_priv->index].cur_conf = -1;
		tas_priv->tasdevice[tas_priv->index].cur_prog = -1;

		ret = tasdevice_prmg_load(tas_priv, 0);
		if (ret < 0) {
			dev_err(tas_priv->dev,
				"FW download failed = %d\n", ret);
			return ret;
		}
		tas_priv->fw_state = TASDEVICE_DSP_FW_ALL_OK;

		if (tas_priv->playback_started)
			tasdevice_tuning_switch(tas_priv, 0);
	}

	return ret;
}

static const struct dev_pm_ops tas2781_hda_pm_ops = {
	RUNTIME_PM_OPS(tas2781_runtime_suspend, tas2781_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(tas2781_system_suspend, tas2781_system_resume)
};

static const struct spi_device_id tas2781_hda_spi_id[] = {
	{ "tas2781-hda", },
	{}
};

static const struct acpi_device_id tas2781_acpi_hda_match[] = {
	{"TXNW2781", },
	{}
};
MODULE_DEVICE_TABLE(acpi, tas2781_acpi_hda_match);

static struct spi_driver tas2781_hda_spi_driver = {
	.driver = {
		.name		= "tas2781-hda",
		.acpi_match_table = tas2781_acpi_hda_match,
		.pm		= &tas2781_hda_pm_ops,
	},
	.id_table	= tas2781_hda_spi_id,
	.probe		= tas2781_hda_spi_probe,
	.remove		= tas2781_hda_spi_remove,
};
module_spi_driver(tas2781_hda_spi_driver);

MODULE_DESCRIPTION("TAS2781 HDA SPI Driver");
MODULE_AUTHOR("Baojun, Xu, <baojun.xug@ti.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("SND_SOC_TAS2781_FMWLIB");
MODULE_IMPORT_NS("SND_HDA_SCODEC_TAS2781");
