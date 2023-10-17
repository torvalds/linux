// SPDX-License-Identifier: GPL-2.0
//
// Driver for Microchip Pulse Density Microphone Controller (PDMC) interfaces
//
// Copyright (C) 2019-2022 Microchip Technology Inc. and its subsidiaries
//
// Author: Codrin Ciubotariu <codrin.ciubotariu@microchip.com>

#include <dt-bindings/sound/microchip,pdmc.h>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

/*
 * ---- PDMC Register map ----
 */
#define MCHP_PDMC_CR			0x00	/* Control Register */
#define MCHP_PDMC_MR			0x04	/* Mode Register */
#define MCHP_PDMC_CFGR			0x08	/* Configuration Register */
#define MCHP_PDMC_RHR			0x0C	/* Receive Holding Register */
#define MCHP_PDMC_IER			0x14	/* Interrupt Enable Register */
#define MCHP_PDMC_IDR			0x18	/* Interrupt Disable Register */
#define MCHP_PDMC_IMR			0x1C	/* Interrupt Mask Register */
#define MCHP_PDMC_ISR			0x20	/* Interrupt Status Register */
#define MCHP_PDMC_VER			0x50	/* Version Register */

/*
 * ---- Control Register (Write-only) ----
 */
#define MCHP_PDMC_CR_SWRST		BIT(0)	/* Software Reset */

/*
 * ---- Mode Register (Read/Write) ----
 */
#define MCHP_PDMC_MR_PDMCEN_MASK	GENMASK(3, 0)
#define MCHP_PDMC_MR_PDMCEN(ch)		(BIT(ch) & MCHP_PDMC_MR_PDMCEN_MASK)

#define MCHP_PDMC_MR_OSR_MASK		GENMASK(17, 16)
#define MCHP_PDMC_MR_OSR64		(1 << 16)
#define MCHP_PDMC_MR_OSR128		(2 << 16)
#define MCHP_PDMC_MR_OSR256		(3 << 16)

#define MCHP_PDMC_MR_SINCORDER_MASK	GENMASK(23, 20)

#define MCHP_PDMC_MR_SINC_OSR_MASK	GENMASK(27, 24)
#define MCHP_PDMC_MR_SINC_OSR_DIS	(0 << 24)
#define MCHP_PDMC_MR_SINC_OSR_8		(1 << 24)
#define MCHP_PDMC_MR_SINC_OSR_16	(2 << 24)
#define MCHP_PDMC_MR_SINC_OSR_32	(3 << 24)
#define MCHP_PDMC_MR_SINC_OSR_64	(4 << 24)
#define MCHP_PDMC_MR_SINC_OSR_128	(5 << 24)
#define MCHP_PDMC_MR_SINC_OSR_256	(6 << 24)

#define MCHP_PDMC_MR_CHUNK_MASK		GENMASK(31, 28)

/*
 * ---- Configuration Register (Read/Write) ----
 */
#define MCHP_PDMC_CFGR_BSSEL_MASK	(BIT(0) | BIT(2) | BIT(4) | BIT(6))
#define MCHP_PDMC_CFGR_BSSEL(ch)	BIT((ch) * 2)

#define MCHP_PDMC_CFGR_PDMSEL_MASK	(BIT(16) | BIT(18) | BIT(20) | BIT(22))
#define MCHP_PDMC_CFGR_PDMSEL(ch)	BIT((ch) * 2 + 16)

/*
 * ---- Interrupt Enable/Disable/Mask/Status Registers ----
 */
#define MCHP_PDMC_IR_RXRDY		BIT(0)
#define MCHP_PDMC_IR_RXEMPTY		BIT(1)
#define MCHP_PDMC_IR_RXFULL		BIT(2)
#define MCHP_PDMC_IR_RXCHUNK		BIT(3)
#define MCHP_PDMC_IR_RXUDR		BIT(4)
#define MCHP_PDMC_IR_RXOVR		BIT(5)

/*
 * ---- Version Register (Read-only) ----
 */
#define MCHP_PDMC_VER_VERSION		GENMASK(11, 0)

#define MCHP_PDMC_MAX_CHANNELS		4
#define MCHP_PDMC_DS_NO			2
#define MCHP_PDMC_EDGE_NO		2

struct mic_map {
	int ds_pos;
	int clk_edge;
};

struct mchp_pdmc_chmap {
	struct snd_pcm_chmap_elem *chmap;
	struct mchp_pdmc *dd;
	struct snd_pcm *pcm;
	struct snd_kcontrol *kctl;
};

struct mchp_pdmc {
	struct mic_map channel_mic_map[MCHP_PDMC_MAX_CHANNELS];
	struct device *dev;
	struct snd_dmaengine_dai_dma_data addr;
	struct regmap *regmap;
	struct clk *pclk;
	struct clk *gclk;
	u32 pdmcen;
	u32 suspend_irq;
	u32 startup_delay_us;
	int mic_no;
	int sinc_order;
	bool audio_filter_en;
};

static const char *const mchp_pdmc_sinc_filter_order_text[] = {
	"1", "2", "3", "4", "5"
};

static const unsigned int mchp_pdmc_sinc_filter_order_values[] = {
	1, 2, 3, 4, 5,
};

static const struct soc_enum mchp_pdmc_sinc_filter_order_enum = {
	.items = ARRAY_SIZE(mchp_pdmc_sinc_filter_order_text),
	.texts = mchp_pdmc_sinc_filter_order_text,
	.values = mchp_pdmc_sinc_filter_order_values,
};

static int mchp_pdmc_sinc_order_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct mchp_pdmc *dd = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int item;

	item = snd_soc_enum_val_to_item(e, dd->sinc_order);
	uvalue->value.enumerated.item[0] = item;

	return 0;
}

static int mchp_pdmc_sinc_order_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct mchp_pdmc *dd = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = uvalue->value.enumerated.item;
	unsigned int val;

	if (item[0] >= e->items)
		return -EINVAL;

	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;
	if (val == dd->sinc_order)
		return 0;

	dd->sinc_order = val;

	return 1;
}

static int mchp_pdmc_af_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct mchp_pdmc *dd = snd_soc_component_get_drvdata(component);

	uvalue->value.integer.value[0] = !!dd->audio_filter_en;

	return 0;
}

static int mchp_pdmc_af_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct mchp_pdmc *dd = snd_soc_component_get_drvdata(component);
	bool af = uvalue->value.integer.value[0] ? true : false;

	if (dd->audio_filter_en == af)
		return 0;

	dd->audio_filter_en = af;

	return 1;
}

static int mchp_pdmc_chmap_ctl_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct mchp_pdmc_chmap *info = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = info->dd->mic_no;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SNDRV_CHMAP_RR; /* maxmimum 4 channels */
	return 0;
}

static inline struct snd_pcm_substream *
mchp_pdmc_chmap_substream(struct mchp_pdmc_chmap *info, unsigned int idx)
{
	struct snd_pcm_substream *s;

	for (s = info->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream; s; s = s->next)
		if (s->number == idx)
			return s;
	return NULL;
}

static struct snd_pcm_chmap_elem *mchp_pdmc_chmap_get(struct snd_pcm_substream *substream,
						      struct mchp_pdmc_chmap *ch_info)
{
	struct snd_pcm_chmap_elem *map;

	for (map = ch_info->chmap; map->channels; map++) {
		if (map->channels == substream->runtime->channels)
			return map;
	}
	return NULL;
}

static int mchp_pdmc_chmap_ctl_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct mchp_pdmc_chmap *info = snd_kcontrol_chip(kcontrol);
	struct mchp_pdmc *dd = info->dd;
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	struct snd_pcm_substream *substream;
	const struct snd_pcm_chmap_elem *map;
	int i;
	u32 cfgr_val = 0;

	if (!info->chmap)
		return -EINVAL;
	substream = mchp_pdmc_chmap_substream(info, idx);
	if (!substream)
		return -ENODEV;
	memset(ucontrol->value.integer.value, 0, sizeof(long) * info->dd->mic_no);
	if (!substream->runtime)
		return 0; /* no channels set */

	map = mchp_pdmc_chmap_get(substream, info);
	if (!map)
		return -EINVAL;

	for (i = 0; i < map->channels; i++) {
		int map_idx = map->channels == 1 ? map->map[i] - SNDRV_CHMAP_MONO :
						   map->map[i] - SNDRV_CHMAP_FL;

		/* make sure the reported channel map is the real one, so write the map */
		if (dd->channel_mic_map[map_idx].ds_pos)
			cfgr_val |= MCHP_PDMC_CFGR_PDMSEL(i);
		if (dd->channel_mic_map[map_idx].clk_edge)
			cfgr_val |= MCHP_PDMC_CFGR_BSSEL(i);

		ucontrol->value.integer.value[i] = map->map[i];
	}

	regmap_write(dd->regmap, MCHP_PDMC_CFGR, cfgr_val);

	return 0;
}

static int mchp_pdmc_chmap_ctl_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct mchp_pdmc_chmap *info = snd_kcontrol_chip(kcontrol);
	struct mchp_pdmc *dd = info->dd;
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	struct snd_pcm_substream *substream;
	struct snd_pcm_chmap_elem *map;
	u32 cfgr_val = 0;
	int i;

	if (!info->chmap)
		return -EINVAL;
	substream = mchp_pdmc_chmap_substream(info, idx);
	if (!substream)
		return -ENODEV;

	map = mchp_pdmc_chmap_get(substream, info);
	if (!map)
		return -EINVAL;

	for (i = 0; i < map->channels; i++) {
		int map_idx;

		map->map[i] = ucontrol->value.integer.value[i];
		map_idx = map->channels == 1 ? map->map[i] - SNDRV_CHMAP_MONO :
					       map->map[i] - SNDRV_CHMAP_FL;

		/* configure IP for the desired channel map */
		if (dd->channel_mic_map[map_idx].ds_pos)
			cfgr_val |= MCHP_PDMC_CFGR_PDMSEL(i);
		if (dd->channel_mic_map[map_idx].clk_edge)
			cfgr_val |= MCHP_PDMC_CFGR_BSSEL(i);
	}

	regmap_write(dd->regmap, MCHP_PDMC_CFGR, cfgr_val);

	return 0;
}

static void mchp_pdmc_chmap_ctl_private_free(struct snd_kcontrol *kcontrol)
{
	struct mchp_pdmc_chmap *info = snd_kcontrol_chip(kcontrol);

	info->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].chmap_kctl = NULL;
	kfree(info);
}

static int mchp_pdmc_chmap_ctl_tlv(struct snd_kcontrol *kcontrol, int op_flag,
				   unsigned int size, unsigned int __user *tlv)
{
	struct mchp_pdmc_chmap *info = snd_kcontrol_chip(kcontrol);
	const struct snd_pcm_chmap_elem *map;
	unsigned int __user *dst;
	int c, count = 0;

	if (!info->chmap)
		return -EINVAL;
	if (size < 8)
		return -ENOMEM;
	if (put_user(SNDRV_CTL_TLVT_CONTAINER, tlv))
		return -EFAULT;
	size -= 8;
	dst = tlv + 2;
	for (map = info->chmap; map->channels; map++) {
		int chs_bytes = map->channels * 4;

		if (size < 8)
			return -ENOMEM;
		if (put_user(SNDRV_CTL_TLVT_CHMAP_VAR, dst) ||
		    put_user(chs_bytes, dst + 1))
			return -EFAULT;
		dst += 2;
		size -= 8;
		count += 8;
		if (size < chs_bytes)
			return -ENOMEM;
		size -= chs_bytes;
		count += chs_bytes;
		for (c = 0; c < map->channels; c++) {
			if (put_user(map->map[c], dst))
				return -EFAULT;
			dst++;
		}
	}
	if (put_user(count, tlv + 1))
		return -EFAULT;
	return 0;
}

static const struct snd_kcontrol_new mchp_pdmc_snd_controls[] = {
	SOC_SINGLE_BOOL_EXT("Audio Filter", 0, &mchp_pdmc_af_get, &mchp_pdmc_af_put),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "SINC Filter Order",
		.info = snd_soc_info_enum_double,
		.get = mchp_pdmc_sinc_order_get,
		.put = mchp_pdmc_sinc_order_put,
		.private_value = (unsigned long)&mchp_pdmc_sinc_filter_order_enum,
	},
};

static int mchp_pdmc_close(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	return snd_soc_add_component_controls(component, mchp_pdmc_snd_controls,
					      ARRAY_SIZE(mchp_pdmc_snd_controls));
}

static int mchp_pdmc_open(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	int i;

	/* remove controls that can't be changed at runtime */
	for (i = 0; i < ARRAY_SIZE(mchp_pdmc_snd_controls); i++) {
		const struct snd_kcontrol_new *control = &mchp_pdmc_snd_controls[i];
		struct snd_ctl_elem_id id;
		int err;

		if (component->name_prefix)
			snprintf(id.name, sizeof(id.name), "%s %s", component->name_prefix,
				 control->name);
		else
			strscpy(id.name, control->name, sizeof(id.name));

		id.numid = 0;
		id.iface = control->iface;
		id.device = control->device;
		id.subdevice = control->subdevice;
		id.index = control->index;
		err = snd_ctl_remove_id(component->card->snd_card, &id);
		if (err < 0)
			dev_err(component->dev, "%d: Failed to remove %s\n", err,
				control->name);
	}

	return 0;
}

static const struct snd_soc_component_driver mchp_pdmc_dai_component = {
	.name = "mchp-pdmc",
	.controls = mchp_pdmc_snd_controls,
	.num_controls = ARRAY_SIZE(mchp_pdmc_snd_controls),
	.open = &mchp_pdmc_open,
	.close = &mchp_pdmc_close,
	.legacy_dai_naming = 1,
	.trigger_start = SND_SOC_TRIGGER_ORDER_LDC,
};

static const unsigned int mchp_pdmc_1mic[] = {1};
static const unsigned int mchp_pdmc_2mic[] = {1, 2};
static const unsigned int mchp_pdmc_3mic[] = {1, 2, 3};
static const unsigned int mchp_pdmc_4mic[] = {1, 2, 3, 4};

static const struct snd_pcm_hw_constraint_list mchp_pdmc_chan_constr[] = {
	{
		.list = mchp_pdmc_1mic,
		.count = ARRAY_SIZE(mchp_pdmc_1mic),
	},
	{
		.list = mchp_pdmc_2mic,
		.count = ARRAY_SIZE(mchp_pdmc_2mic),
	},
	{
		.list = mchp_pdmc_3mic,
		.count = ARRAY_SIZE(mchp_pdmc_3mic),
	},
	{
		.list = mchp_pdmc_4mic,
		.count = ARRAY_SIZE(mchp_pdmc_4mic),
	},
};

static int mchp_pdmc_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct mchp_pdmc *dd = snd_soc_dai_get_drvdata(dai);

	regmap_write(dd->regmap, MCHP_PDMC_CR, MCHP_PDMC_CR_SWRST);

	snd_pcm_hw_constraint_list(substream->runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &mchp_pdmc_chan_constr[dd->mic_no - 1]);

	return 0;
}

static int mchp_pdmc_dai_probe(struct snd_soc_dai *dai)
{
	struct mchp_pdmc *dd = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, NULL, &dd->addr);

	return 0;
}

static int mchp_pdmc_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	unsigned int fmt_master = fmt & SND_SOC_DAIFMT_MASTER_MASK;
	unsigned int fmt_format = fmt & SND_SOC_DAIFMT_FORMAT_MASK;

	/* IP needs to be bitclock master */
	if (fmt_master != SND_SOC_DAIFMT_BP_FP &&
	    fmt_master != SND_SOC_DAIFMT_BP_FC)
		return -EINVAL;

	/* IP supports only PDM interface */
	if (fmt_format != SND_SOC_DAIFMT_PDM)
		return -EINVAL;

	return 0;
}

static u32 mchp_pdmc_mr_set_osr(int audio_filter_en, unsigned int osr)
{
	if (audio_filter_en) {
		switch (osr) {
		case 64:
			return MCHP_PDMC_MR_OSR64;
		case 128:
			return MCHP_PDMC_MR_OSR128;
		case 256:
			return MCHP_PDMC_MR_OSR256;
		}
	} else {
		switch (osr) {
		case 8:
			return MCHP_PDMC_MR_SINC_OSR_8;
		case 16:
			return MCHP_PDMC_MR_SINC_OSR_16;
		case 32:
			return MCHP_PDMC_MR_SINC_OSR_32;
		case 64:
			return MCHP_PDMC_MR_SINC_OSR_64;
		case 128:
			return MCHP_PDMC_MR_SINC_OSR_128;
		case 256:
			return MCHP_PDMC_MR_SINC_OSR_256;
		}
	}
	return 0;
}

static inline int mchp_pdmc_period_to_maxburst(int period_size)
{
	if (!(period_size % 8))
		return 8;
	if (!(period_size % 4))
		return 4;
	if (!(period_size % 2))
		return 2;
	return 1;
}

static struct snd_pcm_chmap_elem mchp_pdmc_std_chmaps[] = {
	{ .channels = 1,
	  .map = { SNDRV_CHMAP_MONO } },
	{ .channels = 2,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR } },
	{ .channels = 3,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
		   SNDRV_CHMAP_RL } },
	{ .channels = 4,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
		   SNDRV_CHMAP_RL, SNDRV_CHMAP_RR } },
	{ }
};

static int mchp_pdmc_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct mchp_pdmc *dd = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_component *comp = dai->component;
	unsigned long gclk_rate = 0;
	unsigned long best_diff_rate = ~0UL;
	unsigned int channels = params_channels(params);
	unsigned int osr = 0, osr_start;
	unsigned int fs = params_rate(params);
	u32 mr_val = 0;
	u32 cfgr_val = 0;
	int i;
	int ret;

	dev_dbg(comp->dev, "%s() rate=%u format=%#x width=%u channels=%u\n",
		__func__, params_rate(params), params_format(params),
		params_width(params), params_channels(params));

	if (channels > dd->mic_no) {
		dev_err(comp->dev, "more channels %u than microphones %d\n",
			channels, dd->mic_no);
		return -EINVAL;
	}

	dd->pdmcen = 0;
	for (i = 0; i < channels; i++) {
		dd->pdmcen |= MCHP_PDMC_MR_PDMCEN(i);
		if (dd->channel_mic_map[i].ds_pos)
			cfgr_val |= MCHP_PDMC_CFGR_PDMSEL(i);
		if (dd->channel_mic_map[i].clk_edge)
			cfgr_val |= MCHP_PDMC_CFGR_BSSEL(i);
	}

	for (osr_start = dd->audio_filter_en ? 64 : 8;
	     osr_start <= 256 && best_diff_rate; osr_start *= 2) {
		long round_rate;
		unsigned long diff_rate;

		round_rate = clk_round_rate(dd->gclk,
					    (unsigned long)fs * 16 * osr_start);
		if (round_rate < 0)
			continue;
		diff_rate = abs((fs * 16 * osr_start) - round_rate);
		if (diff_rate < best_diff_rate) {
			best_diff_rate = diff_rate;
			osr = osr_start;
			gclk_rate = fs * 16 * osr;
		}
	}
	if (!gclk_rate) {
		dev_err(comp->dev, "invalid sampling rate: %u\n", fs);
		return -EINVAL;
	}

	/* CLK is enabled by runtime PM. */
	clk_disable_unprepare(dd->gclk);

	/* set the rate */
	ret = clk_set_rate(dd->gclk, gclk_rate);
	clk_prepare_enable(dd->gclk);
	if (ret) {
		dev_err(comp->dev, "unable to set rate %lu to GCLK: %d\n",
			gclk_rate, ret);
		return ret;
	}

	mr_val |= mchp_pdmc_mr_set_osr(dd->audio_filter_en, osr);

	mr_val |= FIELD_PREP(MCHP_PDMC_MR_SINCORDER_MASK, dd->sinc_order);

	dd->addr.maxburst = mchp_pdmc_period_to_maxburst(snd_pcm_lib_period_bytes(substream));
	mr_val |= FIELD_PREP(MCHP_PDMC_MR_CHUNK_MASK, dd->addr.maxburst);
	dev_dbg(comp->dev, "maxburst set to %d\n", dd->addr.maxburst);

	snd_soc_component_update_bits(comp, MCHP_PDMC_MR,
				      MCHP_PDMC_MR_OSR_MASK |
				      MCHP_PDMC_MR_SINCORDER_MASK |
				      MCHP_PDMC_MR_SINC_OSR_MASK |
				      MCHP_PDMC_MR_CHUNK_MASK, mr_val);

	snd_soc_component_write(comp, MCHP_PDMC_CFGR, cfgr_val);

	return 0;
}

static void mchp_pdmc_noise_filter_workaround(struct mchp_pdmc *dd)
{
	u32 tmp, steps = 16;

	/*
	 * PDMC doesn't wait for microphones' startup time thus the acquisition
	 * may start before the microphones are ready leading to poc noises at
	 * the beginning of capture. To avoid this, we need to wait 50ms (in
	 * normal startup procedure) or 150 ms (worst case after resume from sleep
	 * states) after microphones are enabled and then clear the FIFOs (by
	 * reading the RHR 16 times) and possible interrupts before continuing.
	 * Also, for this to work the DMA needs to be started after interrupts
	 * are enabled.
	 */
	usleep_range(dd->startup_delay_us, dd->startup_delay_us + 5);

	while (steps--)
		regmap_read(dd->regmap, MCHP_PDMC_RHR, &tmp);

	/* Clear interrupts. */
	regmap_read(dd->regmap, MCHP_PDMC_ISR, &tmp);
}

static int mchp_pdmc_trigger(struct snd_pcm_substream *substream,
			     int cmd, struct snd_soc_dai *dai)
{
	struct mchp_pdmc *dd = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_component *cpu = dai->component;
#ifdef DEBUG
	u32 val;
#endif

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_soc_component_update_bits(cpu, MCHP_PDMC_MR,
					      MCHP_PDMC_MR_PDMCEN_MASK,
					      dd->pdmcen);

		mchp_pdmc_noise_filter_workaround(dd);

		/* Enable interrupts. */
		regmap_write(dd->regmap, MCHP_PDMC_IER, dd->suspend_irq |
			     MCHP_PDMC_IR_RXOVR | MCHP_PDMC_IR_RXUDR);
		dd->suspend_irq = 0;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		regmap_read(dd->regmap, MCHP_PDMC_IMR, &dd->suspend_irq);
		fallthrough;
	case SNDRV_PCM_TRIGGER_STOP:
		/* Disable overrun and underrun error interrupts */
		regmap_write(dd->regmap, MCHP_PDMC_IDR, dd->suspend_irq |
			     MCHP_PDMC_IR_RXOVR | MCHP_PDMC_IR_RXUDR);
		fallthrough;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_soc_component_update_bits(cpu, MCHP_PDMC_MR,
					      MCHP_PDMC_MR_PDMCEN_MASK, 0);
		break;
	default:
		return -EINVAL;
	}

#ifdef DEBUG
	regmap_read(dd->regmap, MCHP_PDMC_MR, &val);
	dev_dbg(dd->dev, "MR (0x%02x): 0x%08x\n", MCHP_PDMC_MR, val);
	regmap_read(dd->regmap, MCHP_PDMC_CFGR, &val);
	dev_dbg(dd->dev, "CFGR (0x%02x): 0x%08x\n", MCHP_PDMC_CFGR, val);
	regmap_read(dd->regmap, MCHP_PDMC_IMR, &val);
	dev_dbg(dd->dev, "IMR (0x%02x): 0x%08x\n", MCHP_PDMC_IMR, val);
#endif

	return 0;
}

static int mchp_pdmc_add_chmap_ctls(struct snd_pcm *pcm, struct mchp_pdmc *dd)
{
	struct mchp_pdmc_chmap *info;
	struct snd_kcontrol_new knew = {
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK,
		.info = mchp_pdmc_chmap_ctl_info,
		.get = mchp_pdmc_chmap_ctl_get,
		.put = mchp_pdmc_chmap_ctl_put,
		.tlv.c = mchp_pdmc_chmap_ctl_tlv,
	};
	int err;

	if (WARN_ON(pcm->streams[SNDRV_PCM_STREAM_CAPTURE].chmap_kctl))
		return -EBUSY;
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->pcm = pcm;
	info->dd = dd;
	info->chmap = mchp_pdmc_std_chmaps;
	knew.name = "Capture Channel Map";
	knew.device = pcm->device;
	knew.count = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream_count;
	info->kctl = snd_ctl_new1(&knew, info);
	if (!info->kctl) {
		kfree(info);
		return -ENOMEM;
	}
	info->kctl->private_free = mchp_pdmc_chmap_ctl_private_free;
	err = snd_ctl_add(pcm->card, info->kctl);
	if (err < 0)
		return err;
	pcm->streams[SNDRV_PCM_STREAM_CAPTURE].chmap_kctl = info->kctl;
	return 0;
}

static int mchp_pdmc_pcm_new(struct snd_soc_pcm_runtime *rtd,
			     struct snd_soc_dai *dai)
{
	struct mchp_pdmc *dd = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = mchp_pdmc_add_chmap_ctls(rtd->pcm, dd);
	if (ret < 0)
		dev_err(dd->dev, "failed to add channel map controls: %d\n", ret);

	return ret;
}

static const struct snd_soc_dai_ops mchp_pdmc_dai_ops = {
	.probe		= mchp_pdmc_dai_probe,
	.set_fmt	= mchp_pdmc_set_fmt,
	.startup	= mchp_pdmc_startup,
	.hw_params	= mchp_pdmc_hw_params,
	.trigger	= mchp_pdmc_trigger,
	.pcm_new	= &mchp_pdmc_pcm_new,
};

static struct snd_soc_dai_driver mchp_pdmc_dai = {
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= 4,
		.rate_min	= 8000,
		.rate_max	= 192000,
		.rates		= SNDRV_PCM_RATE_KNOT,
		.formats	= SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &mchp_pdmc_dai_ops,
};

/* PDMC interrupt handler */
static irqreturn_t mchp_pdmc_interrupt(int irq, void *dev_id)
{
	struct mchp_pdmc *dd = dev_id;
	u32 isr, msr, pending;
	irqreturn_t ret = IRQ_NONE;

	regmap_read(dd->regmap, MCHP_PDMC_ISR, &isr);
	regmap_read(dd->regmap, MCHP_PDMC_IMR, &msr);

	pending = isr & msr;
	dev_dbg(dd->dev, "ISR (0x%02x): 0x%08x, IMR (0x%02x): 0x%08x, pending: 0x%08x\n",
		MCHP_PDMC_ISR, isr, MCHP_PDMC_IMR, msr, pending);
	if (!pending)
		return IRQ_NONE;

	if (pending & MCHP_PDMC_IR_RXUDR) {
		dev_warn(dd->dev, "underrun detected\n");
		regmap_write(dd->regmap, MCHP_PDMC_IDR, MCHP_PDMC_IR_RXUDR);
		ret = IRQ_HANDLED;
	}
	if (pending & MCHP_PDMC_IR_RXOVR) {
		dev_warn(dd->dev, "overrun detected\n");
		regmap_write(dd->regmap, MCHP_PDMC_IDR, MCHP_PDMC_IR_RXOVR);
		ret = IRQ_HANDLED;
	}

	return ret;
}

/* regmap configuration */
static bool mchp_pdmc_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MCHP_PDMC_MR:
	case MCHP_PDMC_CFGR:
	case MCHP_PDMC_IMR:
	case MCHP_PDMC_ISR:
	case MCHP_PDMC_RHR:
	case MCHP_PDMC_VER:
		return true;
	default:
		return false;
	}
}

static bool mchp_pdmc_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MCHP_PDMC_CR:
	case MCHP_PDMC_MR:
	case MCHP_PDMC_CFGR:
	case MCHP_PDMC_IER:
	case MCHP_PDMC_IDR:
		return true;
	default:
		return false;
	}
}

static bool mchp_pdmc_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MCHP_PDMC_ISR:
	case MCHP_PDMC_RHR:
		return true;
	default:
		return false;
	}
}

static bool mchp_pdmc_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MCHP_PDMC_RHR:
	case MCHP_PDMC_ISR:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config mchp_pdmc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= MCHP_PDMC_VER,
	.readable_reg	= mchp_pdmc_readable_reg,
	.writeable_reg	= mchp_pdmc_writeable_reg,
	.precious_reg	= mchp_pdmc_precious_reg,
	.volatile_reg	= mchp_pdmc_volatile_reg,
	.cache_type	= REGCACHE_FLAT,
};

static int mchp_pdmc_dt_init(struct mchp_pdmc *dd)
{
	struct device_node *np = dd->dev->of_node;
	bool mic_ch[MCHP_PDMC_DS_NO][MCHP_PDMC_EDGE_NO] = {0};
	int i;
	int ret;

	if (!np) {
		dev_err(dd->dev, "device node not found\n");
		return -EINVAL;
	}

	dd->mic_no = of_property_count_u32_elems(np, "microchip,mic-pos");
	if (dd->mic_no < 0) {
		dev_err(dd->dev, "failed to get microchip,mic-pos: %d",
			dd->mic_no);
		return dd->mic_no;
	}
	if (!dd->mic_no || dd->mic_no % 2 ||
	    dd->mic_no / 2 > MCHP_PDMC_MAX_CHANNELS) {
		dev_err(dd->dev, "invalid array length for microchip,mic-pos: %d",
			dd->mic_no);
		return -EINVAL;
	}

	dd->mic_no /= 2;

	dev_info(dd->dev, "%d PDM microphones declared\n", dd->mic_no);

	/*
	 * by default, we consider the order of microphones in
	 * microchip,mic-pos to be the same with the channel mapping;
	 * 1st microphone channel 0, 2nd microphone channel 1, etc.
	 */
	for (i = 0; i < dd->mic_no; i++) {
		int ds;
		int edge;

		ret = of_property_read_u32_index(np, "microchip,mic-pos", i * 2,
						 &ds);
		if (ret) {
			dev_err(dd->dev,
				"failed to get value no %d value from microchip,mic-pos: %d",
				i * 2, ret);
			return ret;
		}
		if (ds >= MCHP_PDMC_DS_NO) {
			dev_err(dd->dev,
				"invalid DS index in microchip,mic-pos array: %d",
				ds);
			return -EINVAL;
		}

		ret = of_property_read_u32_index(np, "microchip,mic-pos", i * 2 + 1,
						 &edge);
		if (ret) {
			dev_err(dd->dev,
				"failed to get value no %d value from microchip,mic-pos: %d",
				i * 2 + 1, ret);
			return ret;
		}

		if (edge != MCHP_PDMC_CLK_POSITIVE &&
		    edge != MCHP_PDMC_CLK_NEGATIVE) {
			dev_err(dd->dev,
				"invalid edge in microchip,mic-pos array: %d", edge);
			return -EINVAL;
		}
		if (mic_ch[ds][edge]) {
			dev_err(dd->dev,
				"duplicated mic (DS %d, edge %d) in microchip,mic-pos array",
				ds, edge);
			return -EINVAL;
		}
		mic_ch[ds][edge] = true;
		dd->channel_mic_map[i].ds_pos = ds;
		dd->channel_mic_map[i].clk_edge = edge;
	}

	dd->startup_delay_us = 150000;
	of_property_read_u32(np, "microchip,startup-delay-us", &dd->startup_delay_us);

	return 0;
}

/* used to clean the channel index found on RHR's MSB */
static int mchp_pdmc_process(struct snd_pcm_substream *substream,
			     int channel, unsigned long hwoff,
			     unsigned long bytes)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	u8 *dma_ptr = runtime->dma_area + hwoff +
		      channel * (runtime->dma_bytes / runtime->channels);
	u8 *dma_ptr_end = dma_ptr + bytes;
	unsigned int sample_size = samples_to_bytes(runtime, 1);

	for (; dma_ptr < dma_ptr_end; dma_ptr += sample_size)
		*dma_ptr = 0;

	return 0;
}

static struct snd_dmaengine_pcm_config mchp_pdmc_config = {
	.process = mchp_pdmc_process,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
};

static int mchp_pdmc_runtime_suspend(struct device *dev)
{
	struct mchp_pdmc *dd = dev_get_drvdata(dev);

	regcache_cache_only(dd->regmap, true);

	clk_disable_unprepare(dd->gclk);
	clk_disable_unprepare(dd->pclk);

	return 0;
}

static int mchp_pdmc_runtime_resume(struct device *dev)
{
	struct mchp_pdmc *dd = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(dd->pclk);
	if (ret) {
		dev_err(dd->dev,
			"failed to enable the peripheral clock: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(dd->gclk);
	if (ret) {
		dev_err(dd->dev,
			"failed to enable generic clock: %d\n", ret);
		goto disable_pclk;
	}

	regcache_cache_only(dd->regmap, false);
	regcache_mark_dirty(dd->regmap);
	ret = regcache_sync(dd->regmap);
	if (ret) {
		regcache_cache_only(dd->regmap, true);
		clk_disable_unprepare(dd->gclk);
disable_pclk:
		clk_disable_unprepare(dd->pclk);
	}

	return ret;
}

static int mchp_pdmc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mchp_pdmc *dd;
	struct resource *res;
	void __iomem *io_base;
	u32 version;
	int irq;
	int ret;

	dd = devm_kzalloc(dev, sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;

	dd->dev = &pdev->dev;
	ret = mchp_pdmc_dt_init(dd);
	if (ret < 0)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	dd->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dd->pclk)) {
		ret = PTR_ERR(dd->pclk);
		dev_err(dev, "failed to get peripheral clock: %d\n", ret);
		return ret;
	}

	dd->gclk = devm_clk_get(dev, "gclk");
	if (IS_ERR(dd->gclk)) {
		ret = PTR_ERR(dd->gclk);
		dev_err(dev, "failed to get GCK: %d\n", ret);
		return ret;
	}

	io_base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(io_base)) {
		ret = PTR_ERR(io_base);
		dev_err(dev, "failed to remap register memory: %d\n", ret);
		return ret;
	}

	dd->regmap = devm_regmap_init_mmio(dev, io_base,
					   &mchp_pdmc_regmap_config);
	if (IS_ERR(dd->regmap)) {
		ret = PTR_ERR(dd->regmap);
		dev_err(dev, "failed to init register map: %d\n", ret);
		return ret;
	}

	ret = devm_request_irq(dev, irq, mchp_pdmc_interrupt, 0,
			       dev_name(&pdev->dev), dd);
	if (ret < 0) {
		dev_err(dev, "can't register ISR for IRQ %u (ret=%i)\n",
			irq, ret);
		return ret;
	}

	/* by default audio filter is enabled and the SINC Filter order
	 * will be set to the recommended value, 3
	 */
	dd->audio_filter_en = true;
	dd->sinc_order = 3;

	dd->addr.addr = (dma_addr_t)res->start + MCHP_PDMC_RHR;
	platform_set_drvdata(pdev, dd);

	pm_runtime_enable(dd->dev);
	if (!pm_runtime_enabled(dd->dev)) {
		ret = mchp_pdmc_runtime_resume(dd->dev);
		if (ret)
			return ret;
	}

	/* register platform */
	ret = devm_snd_dmaengine_pcm_register(dev, &mchp_pdmc_config, 0);
	if (ret) {
		dev_err(dev, "could not register platform: %d\n", ret);
		goto pm_runtime_suspend;
	}

	ret = devm_snd_soc_register_component(dev, &mchp_pdmc_dai_component,
					      &mchp_pdmc_dai, 1);
	if (ret) {
		dev_err(dev, "could not register CPU DAI: %d\n", ret);
		goto pm_runtime_suspend;
	}

	/* print IP version */
	regmap_read(dd->regmap, MCHP_PDMC_VER, &version);
	dev_info(dd->dev, "hw version: %#lx\n",
		 version & MCHP_PDMC_VER_VERSION);

	return 0;

pm_runtime_suspend:
	if (!pm_runtime_status_suspended(dd->dev))
		mchp_pdmc_runtime_suspend(dd->dev);
	pm_runtime_disable(dd->dev);

	return ret;
}

static void mchp_pdmc_remove(struct platform_device *pdev)
{
	struct mchp_pdmc *dd = platform_get_drvdata(pdev);

	if (!pm_runtime_status_suspended(dd->dev))
		mchp_pdmc_runtime_suspend(dd->dev);

	pm_runtime_disable(dd->dev);
}

static const struct of_device_id mchp_pdmc_of_match[] = {
	{
		.compatible = "microchip,sama7g5-pdmc",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, mchp_pdmc_of_match);

static const struct dev_pm_ops mchp_pdmc_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	RUNTIME_PM_OPS(mchp_pdmc_runtime_suspend, mchp_pdmc_runtime_resume,
		       NULL)
};

static struct platform_driver mchp_pdmc_driver = {
	.driver	= {
		.name		= "mchp-pdmc",
		.of_match_table	= of_match_ptr(mchp_pdmc_of_match),
		.pm		= pm_ptr(&mchp_pdmc_pm_ops),
	},
	.probe	= mchp_pdmc_probe,
	.remove_new = mchp_pdmc_remove,
};
module_platform_driver(mchp_pdmc_driver);

MODULE_DESCRIPTION("Microchip PDMC driver under ALSA SoC architecture");
MODULE_AUTHOR("Codrin Ciubotariu <codrin.ciubotariu@microchip.com>");
MODULE_LICENSE("GPL v2");
