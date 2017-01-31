/*
 *   intel_hdmi_audio.c - Intel HDMI audio driver
 *
 *  Copyright (C) 2016 Intel Corp
 *  Authors:	Sailaja Bandarupalli <sailaja.bandarupalli@intel.com>
 *		Ramesh Babu K V	<ramesh.babu@intel.com>
 *		Vaibhav Agarwal <vaibhav.agarwal@intel.com>
 *		Jerome Anand <jerome.anand@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * ALSA driver for Intel HDMI audio
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/acpi.h>
#include <asm/cacheflush.h>
#include <sound/pcm.h>
#include <sound/core.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <drm/intel_lpe_audio.h>
#include "intel_hdmi_audio.h"

/*standard module options for ALSA. This module supports only one card*/
static int hdmi_card_index = SNDRV_DEFAULT_IDX1;
static char *hdmi_card_id = SNDRV_DEFAULT_STR1;

module_param_named(index, hdmi_card_index, int, 0444);
MODULE_PARM_DESC(index,
		"Index value for INTEL Intel HDMI Audio controller.");
module_param_named(id, hdmi_card_id, charp, 0444);
MODULE_PARM_DESC(id,
		"ID string for INTEL Intel HDMI Audio controller.");

/*
 * ELD SA bits in the CEA Speaker Allocation data block
 */
static int eld_speaker_allocation_bits[] = {
	[0] = FL | FR,
	[1] = LFE,
	[2] = FC,
	[3] = RL | RR,
	[4] = RC,
	[5] = FLC | FRC,
	[6] = RLC | RRC,
	/* the following are not defined in ELD yet */
	[7] = 0,
};

/*
 * This is an ordered list!
 *
 * The preceding ones have better chances to be selected by
 * hdmi_channel_allocation().
 */
static struct cea_channel_speaker_allocation channel_allocations[] = {
/*                        channel:   7     6    5    4    3     2    1    0  */
{ .ca_index = 0x00,  .speakers = {   0,    0,   0,   0,   0,    0,  FR,  FL } },
				/* 2.1 */
{ .ca_index = 0x01,  .speakers = {   0,    0,   0,   0,   0,  LFE,  FR,  FL } },
				/* Dolby Surround */
{ .ca_index = 0x02,  .speakers = {   0,    0,   0,   0,  FC,    0,  FR,  FL } },
				/* surround40 */
{ .ca_index = 0x08,  .speakers = {   0,    0,  RR,  RL,   0,    0,  FR,  FL } },
				/* surround41 */
{ .ca_index = 0x09,  .speakers = {   0,    0,  RR,  RL,   0,  LFE,  FR,  FL } },
				/* surround50 */
{ .ca_index = 0x0a,  .speakers = {   0,    0,  RR,  RL,  FC,    0,  FR,  FL } },
				/* surround51 */
{ .ca_index = 0x0b,  .speakers = {   0,    0,  RR,  RL,  FC,  LFE,  FR,  FL } },
				/* 6.1 */
{ .ca_index = 0x0f,  .speakers = {   0,   RC,  RR,  RL,  FC,  LFE,  FR,  FL } },
				/* surround71 */
{ .ca_index = 0x13,  .speakers = { RRC,  RLC,  RR,  RL,  FC,  LFE,  FR,  FL } },

{ .ca_index = 0x03,  .speakers = {   0,    0,   0,   0,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x04,  .speakers = {   0,    0,   0,  RC,   0,    0,  FR,  FL } },
{ .ca_index = 0x05,  .speakers = {   0,    0,   0,  RC,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x06,  .speakers = {   0,    0,   0,  RC,  FC,    0,  FR,  FL } },
{ .ca_index = 0x07,  .speakers = {   0,    0,   0,  RC,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x0c,  .speakers = {   0,   RC,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x0d,  .speakers = {   0,   RC,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x0e,  .speakers = {   0,   RC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x10,  .speakers = { RRC,  RLC,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x11,  .speakers = { RRC,  RLC,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x12,  .speakers = { RRC,  RLC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x14,  .speakers = { FRC,  FLC,   0,   0,   0,    0,  FR,  FL } },
{ .ca_index = 0x15,  .speakers = { FRC,  FLC,   0,   0,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x16,  .speakers = { FRC,  FLC,   0,   0,  FC,    0,  FR,  FL } },
{ .ca_index = 0x17,  .speakers = { FRC,  FLC,   0,   0,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x18,  .speakers = { FRC,  FLC,   0,  RC,   0,    0,  FR,  FL } },
{ .ca_index = 0x19,  .speakers = { FRC,  FLC,   0,  RC,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x1a,  .speakers = { FRC,  FLC,   0,  RC,  FC,    0,  FR,  FL } },
{ .ca_index = 0x1b,  .speakers = { FRC,  FLC,   0,  RC,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x1c,  .speakers = { FRC,  FLC,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x1d,  .speakers = { FRC,  FLC,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x1e,  .speakers = { FRC,  FLC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x1f,  .speakers = { FRC,  FLC,  RR,  RL,  FC,  LFE,  FR,  FL } },
};

static struct channel_map_table map_tables[] = {
	{ SNDRV_CHMAP_FL,       0x00,   FL },
	{ SNDRV_CHMAP_FR,       0x01,   FR },
	{ SNDRV_CHMAP_RL,       0x04,   RL },
	{ SNDRV_CHMAP_RR,       0x05,   RR },
	{ SNDRV_CHMAP_LFE,      0x02,   LFE },
	{ SNDRV_CHMAP_FC,       0x03,   FC },
	{ SNDRV_CHMAP_RLC,      0x06,   RLC },
	{ SNDRV_CHMAP_RRC,      0x07,   RRC },
	{} /* terminator */
};

/* hardware capability structure */
static const struct snd_pcm_hardware snd_intel_hadstream = {
	.info =	(SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_DOUBLE |
		SNDRV_PCM_INFO_MMAP|
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH),
	.formats = (SNDRV_PCM_FMTBIT_S24 |
		SNDRV_PCM_FMTBIT_U24),
	.rates = SNDRV_PCM_RATE_32000 |
		SNDRV_PCM_RATE_44100 |
		SNDRV_PCM_RATE_48000 |
		SNDRV_PCM_RATE_88200 |
		SNDRV_PCM_RATE_96000 |
		SNDRV_PCM_RATE_176400 |
		SNDRV_PCM_RATE_192000,
	.rate_min = HAD_MIN_RATE,
	.rate_max = HAD_MAX_RATE,
	.channels_min = HAD_MIN_CHANNEL,
	.channels_max = HAD_MAX_CHANNEL,
	.buffer_bytes_max = HAD_MAX_BUFFER,
	.period_bytes_min = HAD_MIN_PERIOD_BYTES,
	.period_bytes_max = HAD_MAX_PERIOD_BYTES,
	.periods_min = HAD_MIN_PERIODS,
	.periods_max = HAD_MAX_PERIODS,
	.fifo_size = HAD_FIFO_SIZE,
};

/* Register access functions */
static int had_get_hwstate(struct snd_intelhad *intelhaddata)
{
	/* Check for device presence -SW state */
	if (intelhaddata->drv_status == HAD_DRV_DISCONNECTED)
		return -ENODEV;

	return 0;
}

static inline void
mid_hdmi_audio_read(struct snd_intelhad *ctx, u32 reg, u32 *val)
{
	*val = ioread32(ctx->mmio_start + ctx->had_config_offset + reg);
}

static inline void
mid_hdmi_audio_write(struct snd_intelhad *ctx, u32 reg, u32 val)
{
	iowrite32(val, ctx->mmio_start + ctx->had_config_offset + reg);
}

static int had_read_register(struct snd_intelhad *intelhaddata,
			     u32 offset, u32 *data)
{
	int retval;

	retval = had_get_hwstate(intelhaddata);
	if (retval)
		return retval;

	mid_hdmi_audio_read(intelhaddata, offset, data);
	return 0;
}

static void fixup_dp_config(struct snd_intelhad *intelhaddata,
			    u32 offset, u32 *data)
{
	if (intelhaddata->dp_output) {
		if (offset == AUD_CONFIG && (*data & AUD_CONFIG_VALID_BIT))
			*data |= AUD_CONFIG_DP_MODE | AUD_CONFIG_BLOCK_BIT;
	}
}

static int had_write_register(struct snd_intelhad *intelhaddata,
			      u32 offset, u32 data)
{
	int retval;

	retval = had_get_hwstate(intelhaddata);
	if (retval)
		return retval;

	fixup_dp_config(intelhaddata, offset, &data);
	mid_hdmi_audio_write(intelhaddata, offset, data);
	return 0;
}

static int had_read_modify(struct snd_intelhad *intelhaddata, u32 offset,
			   u32 data, u32 mask)
{
	u32 val_tmp;
	int retval;

	retval = had_get_hwstate(intelhaddata);
	if (retval)
		return retval;

	mid_hdmi_audio_read(intelhaddata, offset, &val_tmp);
	val_tmp &= ~mask;
	val_tmp |= (data & mask);

	fixup_dp_config(intelhaddata, offset, &val_tmp);
	mid_hdmi_audio_write(intelhaddata, offset, val_tmp);
	return 0;
}

/*
 * function to read-modify AUD_CONFIG register on VLV2.
 * The had_read_modify() function should not directly be used on VLV2 for
 * updating AUD_CONFIG register.
 * This is because:
 * Bit6 of AUD_CONFIG register is writeonly due to a silicon bug on VLV2
 * HDMI IP. As a result a read-modify of AUD_CONFIG regiter will always
 * clear bit6. AUD_CONFIG[6:4] represents the "channels" field of the
 * register. This field should be 1xy binary for configuration with 6 or
 * more channels. Read-modify of AUD_CONFIG (Eg. for enabling audio)
 * causes the "channels" field to be updated as 0xy binary resulting in
 * bad audio. The fix is to always write the AUD_CONFIG[6:4] with
 * appropriate value when doing read-modify of AUD_CONFIG register.
 *
 * @substream: the current substream or NULL if no active substream
 * @data : data to be written
 * @mask : mask
 *
 */
static int had_read_modify_aud_config_v2(struct snd_intelhad *intelhaddata,
					u32 data, u32 mask)
{
	struct snd_pcm_substream *substream;
	union aud_cfg cfg_val = {.cfg_regval = 0};
	u8 channels;

	/*
	 * If substream is NULL, there is no active stream.
	 * In this case just set channels to 2
	 */
	substream = intelhaddata->stream_info.had_substream;
	if (substream && substream->runtime)
		channels = substream->runtime->channels;
	else
		channels = 2;
	cfg_val.cfg_regx_v2.num_ch = channels - 2;

	data = data | cfg_val.cfg_regval;
	mask = mask | AUD_CONFIG_CH_MASK_V2;

	dev_dbg(intelhaddata->dev, "%s : data = %x, mask =%x\n",
		__func__, data, mask);

	return had_read_modify(intelhaddata, AUD_CONFIG, data, mask);
}

static void snd_intelhad_enable_audio_int(struct snd_intelhad *ctx, bool enable)
{
	u32 status_reg;

	if (enable) {
		mid_hdmi_audio_read(ctx, AUD_HDMI_STATUS_v2, &status_reg);
		status_reg |= HDMI_AUDIO_BUFFER_DONE | HDMI_AUDIO_UNDERRUN;
		mid_hdmi_audio_write(ctx, AUD_HDMI_STATUS_v2, status_reg);
		mid_hdmi_audio_read(ctx, AUD_HDMI_STATUS_v2, &status_reg);
	}
}

static void snd_intelhad_enable_audio(struct snd_intelhad *intelhaddata,
				      bool enable)
{
	had_read_modify_aud_config_v2(intelhaddata, enable ? BIT(0) : 0,
				      BIT(0));
}

static void snd_intelhad_reset_audio(struct snd_intelhad *intelhaddata,
				     u8 reset)
{
	had_write_register(intelhaddata, AUD_HDMI_STATUS_v2, reset);
}

/**
 * initialize audio channel status registers
 * This function is called in the prepare callback
 */
static int had_prog_status_reg(struct snd_pcm_substream *substream,
			struct snd_intelhad *intelhaddata)
{
	union aud_cfg cfg_val = {.cfg_regval = 0};
	union aud_ch_status_0 ch_stat0 = {.status_0_regval = 0};
	union aud_ch_status_1 ch_stat1 = {.status_1_regval = 0};
	int format;

	ch_stat0.status_0_regx.lpcm_id = (intelhaddata->aes_bits &
						IEC958_AES0_NONAUDIO)>>1;
	ch_stat0.status_0_regx.clk_acc = (intelhaddata->aes_bits &
						IEC958_AES3_CON_CLOCK)>>4;
	cfg_val.cfg_regx_v2.val_bit = ch_stat0.status_0_regx.lpcm_id;

	switch (substream->runtime->rate) {
	case AUD_SAMPLE_RATE_32:
		ch_stat0.status_0_regx.samp_freq = CH_STATUS_MAP_32KHZ;
		break;

	case AUD_SAMPLE_RATE_44_1:
		ch_stat0.status_0_regx.samp_freq = CH_STATUS_MAP_44KHZ;
		break;
	case AUD_SAMPLE_RATE_48:
		ch_stat0.status_0_regx.samp_freq = CH_STATUS_MAP_48KHZ;
		break;
	case AUD_SAMPLE_RATE_88_2:
		ch_stat0.status_0_regx.samp_freq = CH_STATUS_MAP_88KHZ;
		break;
	case AUD_SAMPLE_RATE_96:
		ch_stat0.status_0_regx.samp_freq = CH_STATUS_MAP_96KHZ;
		break;
	case AUD_SAMPLE_RATE_176_4:
		ch_stat0.status_0_regx.samp_freq = CH_STATUS_MAP_176KHZ;
		break;
	case AUD_SAMPLE_RATE_192:
		ch_stat0.status_0_regx.samp_freq = CH_STATUS_MAP_192KHZ;
		break;

	default:
		/* control should never come here */
		return -EINVAL;
	break;

	}
	had_write_register(intelhaddata,
			   AUD_CH_STATUS_0, ch_stat0.status_0_regval);

	format = substream->runtime->format;

	if (format == SNDRV_PCM_FORMAT_S16_LE) {
		ch_stat1.status_1_regx.max_wrd_len = MAX_SMPL_WIDTH_20;
		ch_stat1.status_1_regx.wrd_len = SMPL_WIDTH_16BITS;
	} else if (format == SNDRV_PCM_FORMAT_S24_LE) {
		ch_stat1.status_1_regx.max_wrd_len = MAX_SMPL_WIDTH_24;
		ch_stat1.status_1_regx.wrd_len = SMPL_WIDTH_24BITS;
	} else {
		ch_stat1.status_1_regx.max_wrd_len = 0;
		ch_stat1.status_1_regx.wrd_len = 0;
	}
	had_write_register(intelhaddata,
			   AUD_CH_STATUS_1, ch_stat1.status_1_regval);
	return 0;
}

/*
 * function to initialize audio
 * registers and buffer confgiuration registers
 * This function is called in the prepare callback
 */
static int snd_intelhad_audio_ctrl(struct snd_pcm_substream *substream,
				   struct snd_intelhad *intelhaddata)
{
	union aud_cfg cfg_val = {.cfg_regval = 0};
	union aud_buf_config buf_cfg = {.buf_cfgval = 0};
	u8 channels;

	had_prog_status_reg(substream, intelhaddata);

	buf_cfg.buf_cfg_regx_v2.audio_fifo_watermark = FIFO_THRESHOLD;
	buf_cfg.buf_cfg_regx_v2.dma_fifo_watermark = DMA_FIFO_THRESHOLD;
	buf_cfg.buf_cfg_regx_v2.aud_delay = 0;
	had_write_register(intelhaddata, AUD_BUF_CONFIG, buf_cfg.buf_cfgval);

	channels = substream->runtime->channels;
	cfg_val.cfg_regx_v2.num_ch = channels - 2;
	if (channels <= 2)
		cfg_val.cfg_regx_v2.layout = LAYOUT0;
	else
		cfg_val.cfg_regx_v2.layout = LAYOUT1;

	cfg_val.cfg_regx_v2.val_bit = 1;
	had_write_register(intelhaddata, AUD_CONFIG, cfg_val.cfg_regval);
	return 0;
}

/*
 * Compute derived values in channel_allocations[].
 */
static void init_channel_allocations(void)
{
	int i, j;
	struct cea_channel_speaker_allocation *p;

	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		p = channel_allocations + i;
		p->channels = 0;
		p->spk_mask = 0;
		for (j = 0; j < ARRAY_SIZE(p->speakers); j++)
			if (p->speakers[j]) {
				p->channels++;
				p->spk_mask |= p->speakers[j];
			}
	}
}

/*
 * The transformation takes two steps:
 *
 *      eld->spk_alloc => (eld_speaker_allocation_bits[]) => spk_mask
 *            spk_mask => (channel_allocations[])         => ai->CA
 *
 * TODO: it could select the wrong CA from multiple candidates.
 */
static int snd_intelhad_channel_allocation(struct snd_intelhad *intelhaddata,
					int channels)
{
	int i;
	int ca = 0;
	int spk_mask = 0;

	/*
	 * CA defaults to 0 for basic stereo audio
	 */
	if (channels <= 2)
		return 0;

	/*
	 * expand ELD's speaker allocation mask
	 *
	 * ELD tells the speaker mask in a compact(paired) form,
	 * expand ELD's notions to match the ones used by Audio InfoFrame.
	 */

	for (i = 0; i < ARRAY_SIZE(eld_speaker_allocation_bits); i++) {
		if (intelhaddata->eld.speaker_allocation_block & (1 << i))
			spk_mask |= eld_speaker_allocation_bits[i];
	}

	/* search for the first working match in the CA table */
	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		if (channels == channel_allocations[i].channels &&
		(spk_mask & channel_allocations[i].spk_mask) ==
				channel_allocations[i].spk_mask) {
			ca = channel_allocations[i].ca_index;
			break;
		}
	}

	dev_dbg(intelhaddata->dev, "select CA 0x%x for %d\n", ca, channels);

	return ca;
}

/* from speaker bit mask to ALSA API channel position */
static int spk_to_chmap(int spk)
{
	struct channel_map_table *t = map_tables;

	for (; t->map; t++) {
		if (t->spk_mask == spk)
			return t->map;
	}
	return 0;
}

static void had_build_channel_allocation_map(struct snd_intelhad *intelhaddata)
{
	int i = 0, c = 0;
	int spk_mask = 0;
	struct snd_pcm_chmap_elem *chmap;
	u8 eld_high, eld_high_mask = 0xF0;
	u8 high_msb;

	chmap = kzalloc(sizeof(*chmap), GFP_KERNEL);
	if (chmap == NULL) {
		intelhaddata->chmap->chmap = NULL;
		return;
	}

	dev_dbg(intelhaddata->dev, "eld.speaker_allocation_block = %x\n",
			intelhaddata->eld.speaker_allocation_block);

	/* WA: Fix the max channel supported to 8 */

	/*
	 * Sink may support more than 8 channels, if eld_high has more than
	 * one bit set. SOC supports max 8 channels.
	 * Refer eld_speaker_allocation_bits, for sink speaker allocation
	 */

	/* if 0x2F < eld < 0x4F fall back to 0x2f, else fall back to 0x4F */
	eld_high = intelhaddata->eld.speaker_allocation_block & eld_high_mask;
	if ((eld_high & (eld_high-1)) && (eld_high > 0x1F)) {
		/* eld_high & (eld_high-1): if more than 1 bit set */
		/* 0x1F: 7 channels */
		for (i = 1; i < 4; i++) {
			high_msb = eld_high & (0x80 >> i);
			if (high_msb) {
				intelhaddata->eld.speaker_allocation_block &=
					high_msb | 0xF;
				break;
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(eld_speaker_allocation_bits); i++) {
		if (intelhaddata->eld.speaker_allocation_block & (1 << i))
			spk_mask |= eld_speaker_allocation_bits[i];
	}

	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		if (spk_mask == channel_allocations[i].spk_mask) {
			for (c = 0; c < channel_allocations[i].channels; c++) {
				chmap->map[c] = spk_to_chmap(
					channel_allocations[i].speakers[
						(MAX_SPEAKERS - 1)-c]);
			}
			chmap->channels = channel_allocations[i].channels;
			intelhaddata->chmap->chmap = chmap;
			break;
		}
	}
	if (i >= ARRAY_SIZE(channel_allocations)) {
		intelhaddata->chmap->chmap = NULL;
		kfree(chmap);
	}
}

/*
 * ALSA API channel-map control callbacks
 */
static int had_chmap_ctl_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct snd_intelhad *intelhaddata = info->private_data;

	if (intelhaddata->drv_status == HAD_DRV_DISCONNECTED)
		return -ENODEV;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = HAD_MAX_CHANNEL;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SNDRV_CHMAP_LAST;
	return 0;
}

static int had_chmap_ctl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct snd_intelhad *intelhaddata = info->private_data;
	int i = 0;
	const struct snd_pcm_chmap_elem *chmap;

	if (intelhaddata->drv_status == HAD_DRV_DISCONNECTED)
		return -ENODEV;
	if (intelhaddata->chmap->chmap ==  NULL)
		return -ENODATA;
	chmap = intelhaddata->chmap->chmap;
	for (i = 0; i < chmap->channels; i++)
		ucontrol->value.integer.value[i] = chmap->map[i];

	return 0;
}

static int had_register_chmap_ctls(struct snd_intelhad *intelhaddata,
						struct snd_pcm *pcm)
{
	int err = 0;

	err = snd_pcm_add_chmap_ctls(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			NULL, 0, (unsigned long)intelhaddata,
			&intelhaddata->chmap);
	if (err < 0)
		return err;

	intelhaddata->chmap->private_data = intelhaddata;
	intelhaddata->chmap->kctl->info = had_chmap_ctl_info;
	intelhaddata->chmap->kctl->get = had_chmap_ctl_get;
	intelhaddata->chmap->chmap = NULL;
	return 0;
}

/*
 * snd_intelhad_prog_dip - to initialize Data Island Packets registers
 *
 * @substream:substream for which the prepare function is called
 * @intelhaddata:substream private data
 *
 * This function is called in the prepare callback
 */
static void snd_intelhad_prog_dip(struct snd_pcm_substream *substream,
				  struct snd_intelhad *intelhaddata)
{
	int i;
	union aud_ctrl_st ctrl_state = {.ctrl_val = 0};
	union aud_info_frame2 frame2 = {.fr2_val = 0};
	union aud_info_frame3 frame3 = {.fr3_val = 0};
	u8 checksum = 0;
	u32 info_frame;
	int channels;

	channels = substream->runtime->channels;

	had_write_register(intelhaddata, AUD_CNTL_ST, ctrl_state.ctrl_val);

	if (intelhaddata->dp_output) {
		info_frame = DP_INFO_FRAME_WORD1;
		frame2.fr2_val = 1;
	} else {
		info_frame = HDMI_INFO_FRAME_WORD1;
		frame2.fr2_regx.chnl_cnt = substream->runtime->channels - 1;

		frame3.fr3_regx.chnl_alloc = snd_intelhad_channel_allocation(
			intelhaddata, channels);

		/*Calculte the byte wide checksum for all valid DIP words*/
		for (i = 0; i < BYTES_PER_WORD; i++)
			checksum += (info_frame >> i*BITS_PER_BYTE) & MASK_BYTE0;
		for (i = 0; i < BYTES_PER_WORD; i++)
			checksum += (frame2.fr2_val >> i*BITS_PER_BYTE) & MASK_BYTE0;
		for (i = 0; i < BYTES_PER_WORD; i++)
			checksum += (frame3.fr3_val >> i*BITS_PER_BYTE) & MASK_BYTE0;

		frame2.fr2_regx.chksum = -(checksum);
	}

	had_write_register(intelhaddata, AUD_HDMIW_INFOFR_v2, info_frame);
	had_write_register(intelhaddata, AUD_HDMIW_INFOFR_v2, frame2.fr2_val);
	had_write_register(intelhaddata, AUD_HDMIW_INFOFR_v2, frame3.fr3_val);

	/* program remaining DIP words with zero */
	for (i = 0; i < HAD_MAX_DIP_WORDS-VALID_DIP_WORDS; i++)
		had_write_register(intelhaddata, AUD_HDMIW_INFOFR_v2, 0x0);

	ctrl_state.ctrl_regx.dip_freq = 1;
	ctrl_state.ctrl_regx.dip_en_sta = 1;
	had_write_register(intelhaddata, AUD_CNTL_ST, ctrl_state.ctrl_val);
}

/**
 * snd_intelhad_prog_buffer - programs buffer
 * address and length registers
 *
 * @substream:substream for which the prepare function is called
 * @intelhaddata:substream private data
 *
 * This function programs ring buffer address and length into registers.
 */
static int snd_intelhad_prog_buffer(struct snd_intelhad *intelhaddata,
					int start, int end)
{
	u32 ring_buf_addr, ring_buf_size, period_bytes;
	u8 i, num_periods;
	struct snd_pcm_substream *substream;

	substream = intelhaddata->stream_info.had_substream;
	if (WARN_ON(!substream))
		return 0;

	ring_buf_addr = substream->runtime->dma_addr;
	ring_buf_size = snd_pcm_lib_buffer_bytes(substream);
	intelhaddata->stream_info.ring_buf_size = ring_buf_size;
	period_bytes = frames_to_bytes(substream->runtime,
				substream->runtime->period_size);
	num_periods = substream->runtime->periods;

	/*
	 * buffer addr should  be 64 byte aligned, period bytes
	 * will be used to calculate addr offset
	 */
	period_bytes &= ~0x3F;

	/* Hardware supports MAX_PERIODS buffers */
	if (end >= HAD_MAX_PERIODS)
		return -EINVAL;

	for (i = start; i <= end; i++) {
		/* Program the buf registers with addr and len */
		intelhaddata->buf_info[i].buf_addr = ring_buf_addr +
							 (i * period_bytes);
		if (i < num_periods-1)
			intelhaddata->buf_info[i].buf_size = period_bytes;
		else
			intelhaddata->buf_info[i].buf_size = ring_buf_size -
							(period_bytes*i);

		had_write_register(intelhaddata,
				   AUD_BUF_A_ADDR + (i * HAD_REG_WIDTH),
					intelhaddata->buf_info[i].buf_addr |
					BIT(0) | BIT(1));
		had_write_register(intelhaddata,
				   AUD_BUF_A_LENGTH + (i * HAD_REG_WIDTH),
					period_bytes);
		intelhaddata->buf_info[i].is_valid = true;
	}
	dev_dbg(intelhaddata->dev, "%s:buf[%d-%d] addr=%#x  and size=%d\n",
		__func__, start, end,
		intelhaddata->buf_info[start].buf_addr,
		intelhaddata->buf_info[start].buf_size);
	intelhaddata->valid_buf_cnt = num_periods;
	return 0;
}

static int snd_intelhad_read_len(struct snd_intelhad *intelhaddata)
{
	int i, retval = 0;
	u32 len[4];

	for (i = 0; i < 4 ; i++) {
		had_read_register(intelhaddata,
				  AUD_BUF_A_LENGTH + (i * HAD_REG_WIDTH),
				  &len[i]);
		if (!len[i])
			retval++;
	}
	if (retval != 1) {
		for (i = 0; i < 4 ; i++)
			dev_dbg(intelhaddata->dev, "buf[%d] size=%d\n",
				i, len[i]);
	}

	return retval;
}

static int had_calculate_maud_value(u32 aud_samp_freq, u32 link_rate)
{
	u32 maud_val;

	/* Select maud according to DP 1.2 spec*/
	if (link_rate == DP_2_7_GHZ) {
		switch (aud_samp_freq) {
		case AUD_SAMPLE_RATE_32:
			maud_val = AUD_SAMPLE_RATE_32_DP_2_7_MAUD_VAL;
			break;

		case AUD_SAMPLE_RATE_44_1:
			maud_val = AUD_SAMPLE_RATE_44_1_DP_2_7_MAUD_VAL;
			break;

		case AUD_SAMPLE_RATE_48:
			maud_val = AUD_SAMPLE_RATE_48_DP_2_7_MAUD_VAL;
			break;

		case AUD_SAMPLE_RATE_88_2:
			maud_val = AUD_SAMPLE_RATE_88_2_DP_2_7_MAUD_VAL;
			break;

		case AUD_SAMPLE_RATE_96:
			maud_val = AUD_SAMPLE_RATE_96_DP_2_7_MAUD_VAL;
			break;

		case AUD_SAMPLE_RATE_176_4:
			maud_val = AUD_SAMPLE_RATE_176_4_DP_2_7_MAUD_VAL;
			break;

		case HAD_MAX_RATE:
			maud_val = HAD_MAX_RATE_DP_2_7_MAUD_VAL;
			break;

		default:
			maud_val = -EINVAL;
			break;
		}
	} else if (link_rate == DP_1_62_GHZ) {
		switch (aud_samp_freq) {
		case AUD_SAMPLE_RATE_32:
			maud_val = AUD_SAMPLE_RATE_32_DP_1_62_MAUD_VAL;
			break;

		case AUD_SAMPLE_RATE_44_1:
			maud_val = AUD_SAMPLE_RATE_44_1_DP_1_62_MAUD_VAL;
			break;

		case AUD_SAMPLE_RATE_48:
			maud_val = AUD_SAMPLE_RATE_48_DP_1_62_MAUD_VAL;
			break;

		case AUD_SAMPLE_RATE_88_2:
			maud_val = AUD_SAMPLE_RATE_88_2_DP_1_62_MAUD_VAL;
			break;

		case AUD_SAMPLE_RATE_96:
			maud_val = AUD_SAMPLE_RATE_96_DP_1_62_MAUD_VAL;
			break;

		case AUD_SAMPLE_RATE_176_4:
			maud_val = AUD_SAMPLE_RATE_176_4_DP_1_62_MAUD_VAL;
			break;

		case HAD_MAX_RATE:
			maud_val = HAD_MAX_RATE_DP_1_62_MAUD_VAL;
			break;

		default:
			maud_val = -EINVAL;
			break;
		}
	} else
		maud_val = -EINVAL;

	return maud_val;
}

/*
 * snd_intelhad_prog_cts - Program HDMI audio CTS value
 *
 * @aud_samp_freq: sampling frequency of audio data
 * @tmds: sampling frequency of the display data
 * @n_param: N value, depends on aud_samp_freq
 * @intelhaddata:substream private data
 *
 * Program CTS register based on the audio and display sampling frequency
 */
static void snd_intelhad_prog_cts(u32 aud_samp_freq, u32 tmds,
				  u32 link_rate, u32 n_param,
				  struct snd_intelhad *intelhaddata)
{
	u32 cts_val;
	u64 dividend, divisor;

	if (intelhaddata->dp_output) {
		/* Substitute cts_val with Maud according to DP 1.2 spec*/
		cts_val = had_calculate_maud_value(aud_samp_freq, link_rate);
	} else {
		/* Calculate CTS according to HDMI 1.3a spec*/
		dividend = (u64)tmds * n_param*1000;
		divisor = 128 * aud_samp_freq;
		cts_val = div64_u64(dividend, divisor);
	}
	dev_dbg(intelhaddata->dev, "TMDS value=%d, N value=%d, CTS Value=%d\n",
		 tmds, n_param, cts_val);
	had_write_register(intelhaddata, AUD_HDMI_CTS, (BIT(24) | cts_val));
}

static int had_calculate_n_value(u32 aud_samp_freq)
{
	s32 n_val;

	/* Select N according to HDMI 1.3a spec*/
	switch (aud_samp_freq) {
	case AUD_SAMPLE_RATE_32:
		n_val = 4096;
	break;

	case AUD_SAMPLE_RATE_44_1:
		n_val = 6272;
	break;

	case AUD_SAMPLE_RATE_48:
		n_val = 6144;
	break;

	case AUD_SAMPLE_RATE_88_2:
		n_val = 12544;
	break;

	case AUD_SAMPLE_RATE_96:
		n_val = 12288;
	break;

	case AUD_SAMPLE_RATE_176_4:
		n_val = 25088;
	break;

	case HAD_MAX_RATE:
		n_val = 24576;
	break;

	default:
		n_val = -EINVAL;
	break;
	}
	return n_val;
}

/*
 * snd_intelhad_prog_n - Program HDMI audio N value
 *
 * @aud_samp_freq: sampling frequency of audio data
 * @n_param: N value, depends on aud_samp_freq
 * @intelhaddata:substream private data
 *
 * This function is called in the prepare callback.
 * It programs based on the audio and display sampling frequency
 */
static int snd_intelhad_prog_n(u32 aud_samp_freq, u32 *n_param,
			       struct snd_intelhad *intelhaddata)
{
	s32 n_val;

	if (intelhaddata->dp_output) {
		/*
		 * According to DP specs, Maud and Naud values hold
		 * a relationship, which is stated as:
		 * Maud/Naud = 512 * fs / f_LS_Clk
		 * where, fs is the sampling frequency of the audio stream
		 * and Naud is 32768 for Async clock.
		 */

		n_val = DP_NAUD_VAL;
	} else
		n_val =	had_calculate_n_value(aud_samp_freq);

	if (n_val < 0)
		return n_val;

	had_write_register(intelhaddata, AUD_N_ENABLE, (BIT(24) | n_val));
	*n_param = n_val;
	return 0;
}

static void snd_intelhad_handle_underrun(struct snd_intelhad *intelhaddata)
{
	u32 hdmi_status, i = 0;

	/* Handle Underrun interrupt within Audio Unit */
	had_write_register(intelhaddata, AUD_CONFIG, 0);
	/* Reset buffer pointers */
	had_write_register(intelhaddata, AUD_HDMI_STATUS_v2, 1);
	had_write_register(intelhaddata, AUD_HDMI_STATUS_v2, 0);
	/**
	 * The interrupt status 'sticky' bits might not be cleared by
	 * setting '1' to that bit once...
	 */
	do { /* clear bit30, 31 AUD_HDMI_STATUS */
		had_read_register(intelhaddata, AUD_HDMI_STATUS_v2,
				  &hdmi_status);
		dev_dbg(intelhaddata->dev, "HDMI status =0x%x\n", hdmi_status);
		if (hdmi_status & AUD_CONFIG_MASK_UNDERRUN) {
			i++;
			had_write_register(intelhaddata,
					   AUD_HDMI_STATUS_v2, hdmi_status);
		} else
			break;
	} while (i < MAX_CNT);
	if (i >= MAX_CNT)
		dev_err(intelhaddata->dev, "Unable to clear UNDERRUN bits\n");
}

/**
 * snd_intelhad_open - stream initializations are done here
 * @substream:substream for which the stream function is called
 *
 * This function is called whenever a PCM stream is opened
 */
static int snd_intelhad_open(struct snd_pcm_substream *substream)
{
	struct snd_intelhad *intelhaddata;
	struct snd_pcm_runtime *runtime;
	struct had_stream_pvt *stream;
	struct had_stream_data *had_stream;
	int retval;

	intelhaddata = snd_pcm_substream_chip(substream);
	had_stream = &intelhaddata->stream_data;
	runtime = substream->runtime;
	intelhaddata->underrun_count = 0;

	pm_runtime_get(intelhaddata->dev);

	if (had_get_hwstate(intelhaddata)) {
		dev_dbg(intelhaddata->dev, "%s: HDMI cable plugged-out\n",
			__func__);
		retval = -ENODEV;
		goto exit_put_handle;
	}

	/* Check, if device already in use */
	if (runtime->private_data) {
		dev_dbg(intelhaddata->dev, "Device already in use\n");
		retval = -EBUSY;
		goto exit_put_handle;
	}

	/* set the runtime hw parameter with local snd_pcm_hardware struct */
	runtime->hw = snd_intel_hadstream;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream) {
		retval = -ENOMEM;
		goto exit_put_handle;
	}
	stream->stream_status = STREAM_INIT;
	runtime->private_data = stream;

	retval = snd_pcm_hw_constraint_integer(runtime,
			 SNDRV_PCM_HW_PARAM_PERIODS);
	if (retval < 0)
		goto exit_err;

	/* Make sure, that the period size is always aligned
	 * 64byte boundary
	 */
	retval = snd_pcm_hw_constraint_step(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 64);
	if (retval < 0) {
		dev_dbg(intelhaddata->dev, "%s:step_size=64 failed,err=%d\n",
			__func__, retval);
		goto exit_err;
	}

	return retval;
exit_err:
	kfree(stream);
exit_put_handle:
	pm_runtime_put(intelhaddata->dev);
	runtime->private_data = NULL;
	return retval;
}

/*
 * had_period_elapsed - updates the hardware pointer status
 * @had_substream: substream for which the stream function is called
 */
static void had_period_elapsed(struct snd_pcm_substream *substream)
{
	struct had_stream_pvt *stream;

	if (!substream || !substream->runtime)
		return;
	stream = substream->runtime->private_data;
	if (!stream)
		return;

	if (stream->stream_status != STREAM_RUNNING)
		return;
	snd_pcm_period_elapsed(substream);
}

/**
 * snd_intelhad_init_stream - internal function to initialize stream info
 * @substream:substream for which the stream function is called
 *
 */
static int snd_intelhad_init_stream(struct snd_pcm_substream *substream)
{
	struct snd_intelhad *intelhaddata = snd_pcm_substream_chip(substream);

	intelhaddata->stream_info.had_substream = substream;
	intelhaddata->stream_info.buffer_ptr = 0;
	intelhaddata->stream_info.buffer_rendered = 0;
	intelhaddata->stream_info.sfreq = substream->runtime->rate;
	return 0;
}

/**
 * snd_intelhad_close- to free parameteres when stream is stopped
 *
 * @substream:  substream for which the function is called
 *
 * This function is called by ALSA framework when stream is stopped
 */
static int snd_intelhad_close(struct snd_pcm_substream *substream)
{
	struct snd_intelhad *intelhaddata;
	struct snd_pcm_runtime *runtime;

	intelhaddata = snd_pcm_substream_chip(substream);
	runtime = substream->runtime;

	if (WARN_ON(!runtime->private_data))
		return 0;

	intelhaddata->stream_info.buffer_rendered = 0;
	intelhaddata->stream_info.buffer_ptr = 0;
	intelhaddata->stream_info.str_id = 0;
	intelhaddata->stream_info.had_substream = NULL;

	/* Check if following drv_status modification is required - VA */
	if (intelhaddata->drv_status != HAD_DRV_DISCONNECTED) {
		intelhaddata->drv_status = HAD_DRV_CONNECTED;
		dev_dbg(intelhaddata->dev,
			"%s @ %d:DEBUG PLUG/UNPLUG : HAD_DRV_CONNECTED\n",
			__func__, __LINE__);
	}
	kfree(runtime->private_data);
	runtime->private_data = NULL;
	pm_runtime_put(intelhaddata->dev);
	return 0;
}

/**
 * snd_intelhad_hw_params- to setup the hardware parameters
 * like allocating the buffers
 *
 * @substream:  substream for which the function is called
 * @hw_params: hardware parameters
 *
 * This function is called by ALSA framework when hardware params are set
 */
static int snd_intelhad_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)
{
	struct snd_intelhad *intelhaddata;
	unsigned long addr;
	int pages, buf_size, retval;

	if (!hw_params)
		return -EINVAL;

	intelhaddata = snd_pcm_substream_chip(substream);
	buf_size = params_buffer_bytes(hw_params);
	retval = snd_pcm_lib_malloc_pages(substream, buf_size);
	if (retval < 0)
		return retval;
	dev_dbg(intelhaddata->dev, "%s:allocated memory = %d\n",
		__func__, buf_size);
	/* mark the pages as uncached region */
	addr = (unsigned long) substream->runtime->dma_area;
	pages = (substream->runtime->dma_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
	retval = set_memory_uc(addr, pages);
	if (retval) {
		dev_err(intelhaddata->dev, "set_memory_uc failed.Error:%d\n",
			retval);
		return retval;
	}
	memset(substream->runtime->dma_area, 0, buf_size);

	return retval;
}

/**
 * snd_intelhad_hw_free- to release the resources allocated during
 * hardware params setup
 *
 * @substream:  substream for which the function is called
 *
 * This function is called by ALSA framework before close callback.
 *
 */
static int snd_intelhad_hw_free(struct snd_pcm_substream *substream)
{
	unsigned long addr;
	u32 pages;

	/* mark back the pages as cached/writeback region before the free */
	if (substream->runtime->dma_area != NULL) {
		addr = (unsigned long) substream->runtime->dma_area;
		pages = (substream->runtime->dma_bytes + PAGE_SIZE - 1) /
								PAGE_SIZE;
		set_memory_wb(addr, pages);
		return snd_pcm_lib_free_pages(substream);
	}
	return 0;
}

/**
 * snd_intelhad_pcm_trigger - stream activities are handled here
 * @substream:substream for which the stream function is called
 * @cmd:the stream commamd thats requested from upper layer
 * This function is called whenever an a stream activity is invoked
 */
static int snd_intelhad_pcm_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	int retval = 0;
	unsigned long flag_irq;
	struct snd_intelhad *intelhaddata;
	struct had_stream_pvt *stream;
	struct had_stream_data *had_stream;

	intelhaddata = snd_pcm_substream_chip(substream);
	stream = substream->runtime->private_data;
	had_stream = &intelhaddata->stream_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* Disable local INTRs till register prgmng is done */
		if (had_get_hwstate(intelhaddata)) {
			dev_dbg(intelhaddata->dev,
				"_START: HDMI cable plugged-out\n");
			retval = -ENODEV;
			break;
		}
		stream->stream_status = STREAM_RUNNING;

		had_stream->stream_type = HAD_RUNNING_STREAM;

		/* Enable Audio */
		snd_intelhad_enable_audio_int(intelhaddata, true);
		snd_intelhad_enable_audio(intelhaddata, true);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irq);
		intelhaddata->stream_info.str_id = 0;
		intelhaddata->curr_buf = 0;

		/* Stop reporting BUFFER_DONE/UNDERRUN to above layers */

		had_stream->stream_type = HAD_INIT;
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irq);
		/* Disable Audio */
		snd_intelhad_enable_audio_int(intelhaddata, false);
		snd_intelhad_enable_audio(intelhaddata, false);
		/* Reset buffer pointers */
		snd_intelhad_reset_audio(intelhaddata, 1);
		snd_intelhad_reset_audio(intelhaddata, 0);
		stream->stream_status = STREAM_DROPPED;
		snd_intelhad_enable_audio_int(intelhaddata, false);
		break;

	default:
		retval = -EINVAL;
	}
	return retval;
}

/**
 * snd_intelhad_pcm_prepare- internal preparation before starting a stream
 *
 * @substream:  substream for which the function is called
 *
 * This function is called when a stream is started for internal preparation.
 */
static int snd_intelhad_pcm_prepare(struct snd_pcm_substream *substream)
{
	int retval;
	u32 disp_samp_freq, n_param;
	u32 link_rate = 0;
	struct snd_intelhad *intelhaddata;
	struct snd_pcm_runtime *runtime;
	struct had_stream_data *had_stream;

	intelhaddata = snd_pcm_substream_chip(substream);
	runtime = substream->runtime;
	had_stream = &intelhaddata->stream_data;

	if (had_get_hwstate(intelhaddata)) {
		dev_dbg(intelhaddata->dev, "%s: HDMI cable plugged-out\n",
			__func__);
		retval = -ENODEV;
		goto prep_end;
	}

	dev_dbg(intelhaddata->dev, "period_size=%d\n",
		(int)frames_to_bytes(runtime, runtime->period_size));
	dev_dbg(intelhaddata->dev, "periods=%d\n", runtime->periods);
	dev_dbg(intelhaddata->dev, "buffer_size=%d\n",
		(int)snd_pcm_lib_buffer_bytes(substream));
	dev_dbg(intelhaddata->dev, "rate=%d\n", runtime->rate);
	dev_dbg(intelhaddata->dev, "channels=%d\n", runtime->channels);

	if (intelhaddata->stream_info.str_id) {
		dev_dbg(intelhaddata->dev,
			"_prepare is called for existing str_id#%d\n",
					intelhaddata->stream_info.str_id);
		retval = snd_intelhad_pcm_trigger(substream,
						SNDRV_PCM_TRIGGER_STOP);
		return retval;
	}

	retval = snd_intelhad_init_stream(substream);
	if (retval)
		goto prep_end;


	/* Get N value in KHz */
	disp_samp_freq = intelhaddata->tmds_clock_speed;

	retval = snd_intelhad_prog_n(substream->runtime->rate, &n_param,
				     intelhaddata);
	if (retval) {
		dev_err(intelhaddata->dev,
			"programming N value failed %#x\n", retval);
		goto prep_end;
	}

	if (intelhaddata->dp_output)
		link_rate = intelhaddata->link_rate;

	snd_intelhad_prog_cts(substream->runtime->rate,
			      disp_samp_freq, link_rate,
			      n_param, intelhaddata);

	snd_intelhad_prog_dip(substream, intelhaddata);

	retval = snd_intelhad_audio_ctrl(substream, intelhaddata);

	/* Prog buffer address */
	retval = snd_intelhad_prog_buffer(intelhaddata,
			HAD_BUF_TYPE_A, HAD_BUF_TYPE_D);

	/*
	 * Program channel mapping in following order:
	 * FL, FR, C, LFE, RL, RR
	 */

	had_write_register(intelhaddata, AUD_BUF_CH_SWAP, SWAP_LFE_CENTER);

prep_end:
	return retval;
}

/**
 * snd_intelhad_pcm_pointer- to send the current buffer pointerprocessed by hw
 *
 * @substream:  substream for which the function is called
 *
 * This function is called by ALSA framework to get the current hw buffer ptr
 * when a period is elapsed
 */
static snd_pcm_uframes_t snd_intelhad_pcm_pointer(
					struct snd_pcm_substream *substream)
{
	struct snd_intelhad *intelhaddata;
	u32 bytes_rendered = 0;
	u32 t;
	int buf_id;

	intelhaddata = snd_pcm_substream_chip(substream);

	if (intelhaddata->flag_underrun) {
		intelhaddata->flag_underrun = false;
		return SNDRV_PCM_POS_XRUN;
	}

	/* Use a hw register to calculate sub-period position reports.
	 * This makes PulseAudio happier.
	 */

	buf_id = intelhaddata->curr_buf % 4;
	had_read_register(intelhaddata,
			  AUD_BUF_A_LENGTH + (buf_id * HAD_REG_WIDTH), &t);

	if ((t == 0) || (t == ((u32)-1L))) {
		intelhaddata->underrun_count++;
		dev_dbg(intelhaddata->dev,
			"discovered buffer done for buf %d, count = %d\n",
			 buf_id, intelhaddata->underrun_count);

		if (intelhaddata->underrun_count > (HAD_MIN_PERIODS/2)) {
			dev_dbg(intelhaddata->dev,
				"assume audio_codec_reset, underrun = %d - do xrun\n",
				 intelhaddata->underrun_count);
			intelhaddata->underrun_count = 0;
			return SNDRV_PCM_POS_XRUN;
		}
	} else {
		/* Reset Counter */
		intelhaddata->underrun_count = 0;
	}

	t = intelhaddata->buf_info[buf_id].buf_size - t;

	if (intelhaddata->stream_info.buffer_rendered)
		div_u64_rem(intelhaddata->stream_info.buffer_rendered,
			intelhaddata->stream_info.ring_buf_size,
			&(bytes_rendered));

	intelhaddata->stream_info.buffer_ptr = bytes_to_frames(
						substream->runtime,
						bytes_rendered + t);
	return intelhaddata->stream_info.buffer_ptr;
}

/**
 * snd_intelhad_pcm_mmap- mmaps a kernel buffer to user space for copying data
 *
 * @substream:  substream for which the function is called
 * @vma:		struct instance of memory VMM memory area
 *
 * This function is called by OS when a user space component
 * tries to get mmap memory from driver
 */
static int snd_intelhad_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start,
			substream->dma_buffer.addr >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static int hdmi_audio_mode_change(struct snd_intelhad *intelhaddata)
{
	struct snd_pcm_substream *substream;
	int retval = 0;
	u32 disp_samp_freq, n_param;
	u32 link_rate = 0;

	substream = intelhaddata->stream_info.had_substream;
	if (!substream || !substream->runtime)
		return 0;

	/* Disable Audio */
	snd_intelhad_enable_audio(intelhaddata, false);

	/* Update CTS value */
	disp_samp_freq = intelhaddata->tmds_clock_speed;

	retval = snd_intelhad_prog_n(substream->runtime->rate, &n_param,
				     intelhaddata);
	if (retval) {
		dev_err(intelhaddata->dev,
			"programming N value failed %#x\n", retval);
		goto out;
	}

	if (intelhaddata->dp_output)
		link_rate = intelhaddata->link_rate;

	snd_intelhad_prog_cts(substream->runtime->rate,
			      disp_samp_freq, link_rate,
			      n_param, intelhaddata);

	/* Enable Audio */
	snd_intelhad_enable_audio(intelhaddata, true);

out:
	return retval;
}

/*
 * hdmi_lpe_audio_suspend - power management suspend function
 *
 * @pdev: platform device
 *
 * This function is called by client driver to suspend the
 * hdmi audio.
 */
static int hdmi_lpe_audio_suspend(struct platform_device *pdev,
				  pm_message_t state)
{
	struct had_stream_data *had_stream;
	unsigned long flag_irqs;
	struct snd_pcm_substream *substream;
	struct snd_intelhad *intelhaddata = platform_get_drvdata(pdev);

	had_stream = &intelhaddata->stream_data;
	substream = intelhaddata->stream_info.had_substream;

	if (!pm_runtime_status_suspended(intelhaddata->dev)) {
		dev_err(intelhaddata->dev, "audio stream is active\n");
		return -EAGAIN;
	}


	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	if (intelhaddata->drv_status == HAD_DRV_DISCONNECTED) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		dev_dbg(intelhaddata->dev, "had not connected\n");
		return 0;
	}

	if (intelhaddata->drv_status == HAD_DRV_SUSPENDED) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		dev_dbg(intelhaddata->dev, "had already suspended\n");
		return 0;
	}

	intelhaddata->drv_status = HAD_DRV_SUSPENDED;
	dev_dbg(intelhaddata->dev,
		"%s @ %d:DEBUG PLUG/UNPLUG : HAD_DRV_SUSPENDED\n",
			__func__, __LINE__);

	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
	snd_intelhad_enable_audio_int(intelhaddata, false);
	return 0;
}

/*
 * hdmi_lpe_audio_resume - power management resume function
 *
 *@pdev: platform device
 *
 * This function is called by client driver to resume the
 * hdmi audio.
 */
static int hdmi_lpe_audio_resume(struct platform_device *pdev)
{
	struct snd_intelhad *intelhaddata = platform_get_drvdata(pdev);
	unsigned long flag_irqs;

	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	if (intelhaddata->drv_status == HAD_DRV_DISCONNECTED) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		dev_dbg(intelhaddata->dev, "had not connected\n");
		return 0;
	}

	if (intelhaddata->drv_status != HAD_DRV_SUSPENDED) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		dev_err(intelhaddata->dev, "had is not in suspended state\n");
		return 0;
	}

	if (had_get_hwstate(intelhaddata)) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		dev_err(intelhaddata->dev,
			"Failed to resume. Device not accessible\n");
		return -ENODEV;
	}

	intelhaddata->drv_status = HAD_DRV_CONNECTED;
	dev_dbg(intelhaddata->dev,
		"%s @ %d:DEBUG PLUG/UNPLUG : HAD_DRV_DISCONNECTED\n",
			__func__, __LINE__);
	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
	snd_intelhad_enable_audio_int(intelhaddata, true);
	return 0;
}

static inline int had_chk_intrmiss(struct snd_intelhad *intelhaddata,
		enum intel_had_aud_buf_type buf_id)
{
	int i, intr_count = 0;
	enum intel_had_aud_buf_type buff_done;
	u32 buf_size, buf_addr;
	struct had_stream_data *had_stream;
	unsigned long flag_irqs;

	had_stream = &intelhaddata->stream_data;

	buff_done = buf_id;

	intr_count = snd_intelhad_read_len(intelhaddata);
	if (intr_count > 1) {
		/* In case of active playback */
		dev_err(intelhaddata->dev,
			"Driver detected %d missed buffer done interrupt(s)\n",
			(intr_count - 1));
		if (intr_count > 3)
			return intr_count;

		buf_id += (intr_count - 1);
		/* Reprogram registers*/
		for (i = buff_done; i < buf_id; i++) {
			int j = i % 4;

			buf_size = intelhaddata->buf_info[j].buf_size;
			buf_addr = intelhaddata->buf_info[j].buf_addr;
			had_write_register(intelhaddata,
					   AUD_BUF_A_LENGTH +
					   (j * HAD_REG_WIDTH), buf_size);
			had_write_register(intelhaddata,
					   AUD_BUF_A_ADDR+(j * HAD_REG_WIDTH),
					   (buf_addr | BIT(0) | BIT(1)));
		}
		buf_id = buf_id % 4;
		spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
		intelhaddata->buff_done = buf_id;
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
	}

	return intr_count;
}

static int had_process_buffer_done(struct snd_intelhad *intelhaddata)
{
	u32 len = 1;
	enum intel_had_aud_buf_type buf_id;
	enum intel_had_aud_buf_type buff_done;
	struct pcm_stream_info *stream;
	u32 buf_size;
	struct had_stream_data *had_stream;
	int intr_count;
	enum had_status_stream		stream_type;
	unsigned long flag_irqs;

	had_stream = &intelhaddata->stream_data;
	stream = &intelhaddata->stream_info;
	intr_count = 1;

	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	if (intelhaddata->drv_status == HAD_DRV_DISCONNECTED) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		dev_dbg(intelhaddata->dev,
			"%s:Device already disconnected\n", __func__);
		return 0;
	}
	buf_id = intelhaddata->curr_buf;
	intelhaddata->buff_done = buf_id;
	buff_done = intelhaddata->buff_done;
	buf_size = intelhaddata->buf_info[buf_id].buf_size;
	stream_type = had_stream->stream_type;

	/* Every debug statement has an implication
	 * of ~5msec. Thus, avoid having >3 debug statements
	 * for each buffer_done handling.
	 */

	/* Check for any intr_miss in case of active playback */
	if (had_stream->stream_type == HAD_RUNNING_STREAM) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		intr_count = had_chk_intrmiss(intelhaddata, buf_id);
		if (!intr_count || (intr_count > 3)) {
			dev_err(intelhaddata->dev,
				"HAD SW state in non-recoverable mode\n");
			return 0;
		}
		buf_id += (intr_count - 1);
		buf_id = buf_id % 4;
		spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	}

	intelhaddata->buf_info[buf_id].is_valid = true;
	if (intelhaddata->valid_buf_cnt-1 == buf_id) {
		if (had_stream->stream_type >= HAD_RUNNING_STREAM)
			intelhaddata->curr_buf = HAD_BUF_TYPE_A;
	} else
		intelhaddata->curr_buf = buf_id + 1;

	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);

	if (had_get_hwstate(intelhaddata)) {
		dev_dbg(intelhaddata->dev, "HDMI cable plugged-out\n");
		return 0;
	}

	/*Reprogram the registers with addr and length*/
	had_write_register(intelhaddata,
			   AUD_BUF_A_LENGTH + (buf_id * HAD_REG_WIDTH),
			   buf_size);
	had_write_register(intelhaddata,
			   AUD_BUF_A_ADDR + (buf_id * HAD_REG_WIDTH),
			   intelhaddata->buf_info[buf_id].buf_addr |
			   BIT(0) | BIT(1));

	had_read_register(intelhaddata,
			  AUD_BUF_A_LENGTH + (buf_id * HAD_REG_WIDTH),
			  &len);
	dev_dbg(intelhaddata->dev, "%s:Enabled buf[%d]\n", __func__, buf_id);

	/* In case of actual data,
	 * report buffer_done to above ALSA layer
	 */
	buf_size = intelhaddata->buf_info[buf_id].buf_size;
	if (stream_type >= HAD_RUNNING_STREAM) {
		intelhaddata->stream_info.buffer_rendered +=
			(intr_count * buf_size);
		had_period_elapsed(stream->had_substream);
	}

	return 0;
}

static int had_process_buffer_underrun(struct snd_intelhad *intelhaddata)
{
	enum intel_had_aud_buf_type buf_id;
	struct pcm_stream_info *stream;
	struct had_stream_data *had_stream;
	enum had_status_stream stream_type;
	unsigned long flag_irqs;
	int drv_status;

	had_stream = &intelhaddata->stream_data;
	stream = &intelhaddata->stream_info;

	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	buf_id = intelhaddata->curr_buf;
	stream_type = had_stream->stream_type;
	intelhaddata->buff_done = buf_id;
	drv_status = intelhaddata->drv_status;
	if (stream_type == HAD_RUNNING_STREAM)
		intelhaddata->curr_buf = HAD_BUF_TYPE_A;

	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);

	dev_dbg(intelhaddata->dev, "Enter:%s buf_id=%d, stream_type=%d\n",
			__func__, buf_id, stream_type);

	snd_intelhad_handle_underrun(intelhaddata);

	if (drv_status == HAD_DRV_DISCONNECTED) {
		dev_dbg(intelhaddata->dev,
			"%s:Device already disconnected\n", __func__);
		return 0;
	}

	if (stream_type == HAD_RUNNING_STREAM) {
		/* Report UNDERRUN error to above layers */
		intelhaddata->flag_underrun = true;
		had_period_elapsed(stream->had_substream);
	}

	return 0;
}

static int had_process_hot_plug(struct snd_intelhad *intelhaddata)
{
	enum intel_had_aud_buf_type buf_id;
	struct snd_pcm_substream *substream;
	struct had_stream_data *had_stream;
	unsigned long flag_irqs;

	substream = intelhaddata->stream_info.had_substream;
	had_stream = &intelhaddata->stream_data;

	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	if (intelhaddata->drv_status == HAD_DRV_CONNECTED) {
		dev_dbg(intelhaddata->dev, "Device already connected\n");
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		return 0;
	}
	buf_id = intelhaddata->curr_buf;
	intelhaddata->buff_done = buf_id;
	intelhaddata->drv_status = HAD_DRV_CONNECTED;
	dev_dbg(intelhaddata->dev,
		"%s @ %d:DEBUG PLUG/UNPLUG : HAD_DRV_CONNECTED\n",
			__func__, __LINE__);
	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);

	dev_dbg(intelhaddata->dev, "Processing HOT_PLUG, buf_id = %d\n",
		buf_id);

	/* Safety check */
	if (substream) {
		dev_dbg(intelhaddata->dev,
			"Force to stop the active stream by disconnection\n");
		/* Set runtime->state to hw_params done */
		snd_pcm_stop(substream, SNDRV_PCM_STATE_SETUP);
	}

	had_build_channel_allocation_map(intelhaddata);

	return 0;
}

static int had_process_hot_unplug(struct snd_intelhad *intelhaddata)
{
	enum intel_had_aud_buf_type buf_id;
	struct had_stream_data *had_stream;
	unsigned long flag_irqs;

	had_stream = &intelhaddata->stream_data;
	buf_id = intelhaddata->curr_buf;

	spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);

	if (intelhaddata->drv_status == HAD_DRV_DISCONNECTED) {
		dev_dbg(intelhaddata->dev, "Device already disconnected\n");
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		return 0;

	} else {
		/* Disable Audio */
		snd_intelhad_enable_audio_int(intelhaddata, false);
		snd_intelhad_enable_audio(intelhaddata, false);
	}

	intelhaddata->drv_status = HAD_DRV_DISCONNECTED;
	dev_dbg(intelhaddata->dev,
		"%s @ %d:DEBUG PLUG/UNPLUG : HAD_DRV_DISCONNECTED\n",
			__func__, __LINE__);

	/* Report to above ALSA layer */
	if (intelhaddata->stream_info.had_substream != NULL) {
		spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
		snd_pcm_stop(intelhaddata->stream_info.had_substream,
				SNDRV_PCM_STATE_SETUP);
		spin_lock_irqsave(&intelhaddata->had_spinlock, flag_irqs);
	}

	had_stream->stream_type = HAD_INIT;
	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flag_irqs);
	kfree(intelhaddata->chmap->chmap);
	intelhaddata->chmap->chmap = NULL;

	return 0;
}

/* PCM operations structure and the calls back for the same */
static struct snd_pcm_ops snd_intelhad_playback_ops = {
	.open =		snd_intelhad_open,
	.close =	snd_intelhad_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intelhad_hw_params,
	.hw_free =	snd_intelhad_hw_free,
	.prepare =	snd_intelhad_pcm_prepare,
	.trigger =	snd_intelhad_pcm_trigger,
	.pointer =	snd_intelhad_pcm_pointer,
	.mmap =	snd_intelhad_pcm_mmap,
};

/*
 * snd_intelhad_pcm_free - to free the memory allocated
 *
 * @pcm: pointer to pcm instance
 * This function is called when the device is removed
 */
static void snd_intelhad_pcm_free(struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int had_iec958_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int had_iec958_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_intelhad *intelhaddata = snd_kcontrol_chip(kcontrol);

	ucontrol->value.iec958.status[0] = (intelhaddata->aes_bits >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (intelhaddata->aes_bits >> 8) & 0xff;
	ucontrol->value.iec958.status[2] =
					(intelhaddata->aes_bits >> 16) & 0xff;
	ucontrol->value.iec958.status[3] =
					(intelhaddata->aes_bits >> 24) & 0xff;
	return 0;
}

static int had_iec958_mask_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
	return 0;
}

static int had_iec958_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	unsigned int val;
	struct snd_intelhad *intelhaddata = snd_kcontrol_chip(kcontrol);

	val = (ucontrol->value.iec958.status[0] << 0) |
		(ucontrol->value.iec958.status[1] << 8) |
		(ucontrol->value.iec958.status[2] << 16) |
		(ucontrol->value.iec958.status[3] << 24);
	if (intelhaddata->aes_bits != val) {
		intelhaddata->aes_bits = val;
		return 1;
	}
	return 1;
}

static struct snd_kcontrol_new had_control_iec958_mask = {
	.access =   SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =    SNDRV_CTL_ELEM_IFACE_PCM,
	.name =     SNDRV_CTL_NAME_IEC958("", PLAYBACK, MASK),
	.info =     had_iec958_info, /* shared */
	.get =      had_iec958_mask_get,
};

static struct snd_kcontrol_new had_control_iec958 = {
	.iface =    SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
	.info =         had_iec958_info,
	.get =          had_iec958_get,
	.put =          had_iec958_put
};

static void _had_wq(struct work_struct *work)
{
	struct snd_intelhad *ctx =
		container_of(work, struct snd_intelhad, hdmi_audio_wq);

	had_process_hot_plug(ctx);
}

static irqreturn_t display_pipe_interrupt_handler(int irq, void *dev_id)
{
	struct snd_intelhad *ctx = dev_id;
	u32 audio_stat, audio_reg;

	audio_reg = AUD_HDMI_STATUS_v2;
	mid_hdmi_audio_read(ctx, audio_reg, &audio_stat);

	if (audio_stat & HDMI_AUDIO_UNDERRUN) {
		mid_hdmi_audio_write(ctx, audio_reg, HDMI_AUDIO_UNDERRUN);
		had_process_buffer_underrun(ctx);
	}

	if (audio_stat & HDMI_AUDIO_BUFFER_DONE) {
		mid_hdmi_audio_write(ctx, audio_reg, HDMI_AUDIO_BUFFER_DONE);
		had_process_buffer_done(ctx);
	}

	return IRQ_HANDLED;
}

static void notify_audio_lpe(struct platform_device *pdev)
{
	struct snd_intelhad *ctx = platform_get_drvdata(pdev);
	struct intel_hdmi_lpe_audio_pdata *pdata = pdev->dev.platform_data;

	if (pdata->hdmi_connected != true) {

		dev_dbg(&pdev->dev, "%s: Event: HAD_NOTIFY_HOT_UNPLUG\n",
			__func__);

		if (ctx->state == hdmi_connector_status_connected) {

			ctx->state = hdmi_connector_status_disconnected;

			had_process_hot_unplug(ctx);
		} else
			dev_dbg(&pdev->dev, "%s: Already Unplugged!\n",
				__func__);

	} else {
		struct intel_hdmi_lpe_audio_eld *eld = &pdata->eld;

		switch (eld->pipe_id) {
		case 0:
			ctx->had_config_offset = AUDIO_HDMI_CONFIG_A;
			break;
		case 1:
			ctx->had_config_offset = AUDIO_HDMI_CONFIG_B;
			break;
		case 2:
			ctx->had_config_offset = AUDIO_HDMI_CONFIG_C;
			break;
		default:
			dev_dbg(&pdev->dev, "Invalid pipe %d\n",
				eld->pipe_id);
			break;
		}

		memcpy(&ctx->eld, eld->eld_data, sizeof(ctx->eld));

		had_process_hot_plug(ctx);

		ctx->state = hdmi_connector_status_connected;

		dev_dbg(&pdev->dev, "%s: HAD_NOTIFY_ELD : port = %d, tmds = %d\n",
			__func__, eld->port_id,	pdata->tmds_clock_speed);

		if (pdata->tmds_clock_speed) {
			ctx->tmds_clock_speed = pdata->tmds_clock_speed;
			ctx->dp_output = pdata->dp_output;
			ctx->link_rate = pdata->link_rate;

			/* Process mode change if stream is active */
			if (ctx->stream_data.stream_type == HAD_RUNNING_STREAM)
				hdmi_audio_mode_change(ctx);
		}
	}
}

/* release resources */
static void hdmi_lpe_audio_free(struct snd_card *card)
{
	struct snd_intelhad *ctx = card->private_data;

	if (ctx->mmio_start)
		iounmap(ctx->mmio_start);
	if (ctx->irq >= 0)
		free_irq(ctx->irq, ctx);
}

/*
 * hdmi_lpe_audio_probe - start bridge with i915
 *
 * This function is called when the i915 driver creates the
 * hdmi-lpe-audio platform device. Card creation is deferred until a
 * hot plug event is received
 */
static int hdmi_lpe_audio_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	struct snd_intelhad *ctx;
	struct snd_pcm *pcm;
	struct intel_hdmi_lpe_audio_pdata *pdata;
	int irq;
	struct resource *res_mmio;
	int ret;
	unsigned long flags;

	dev_dbg(&pdev->dev, "dma_mask: %p\n", pdev->dev.dma_mask);

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "%s: quit: pdata not allocated by i915!!\n", __func__);
		return -EINVAL;
	}

	/* get resources */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Could not get irq resource\n");
		return -ENODEV;
	}

	res_mmio = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mmio) {
		dev_err(&pdev->dev, "Could not get IO_MEM resources\n");
		return -ENXIO;
	}

	/* create a card instance with ALSA framework */
	ret = snd_card_new(&pdev->dev, hdmi_card_index, hdmi_card_id,
			   THIS_MODULE, sizeof(*ctx), &card);
	if (ret)
		return ret;

	ctx = card->private_data;
	spin_lock_init(&ctx->had_spinlock);
	ctx->drv_status = HAD_DRV_DISCONNECTED;
	ctx->dev = &pdev->dev;
	ctx->card = card;
	ctx->flag_underrun = false;
	ctx->aes_bits = SNDRV_PCM_DEFAULT_CON_SPDIF;
	strcpy(card->driver, INTEL_HAD);
	strcpy(card->shortname, INTEL_HAD);

	ctx->irq = -1;
	ctx->tmds_clock_speed = DIS_SAMPLE_RATE_148_5;
	INIT_WORK(&ctx->hdmi_audio_wq, _had_wq);
	ctx->state = hdmi_connector_status_disconnected;

	card->private_free = hdmi_lpe_audio_free;

	/* assume pipe A as default */
	ctx->had_config_offset = AUDIO_HDMI_CONFIG_A;

	platform_set_drvdata(pdev, ctx);

	dev_dbg(&pdev->dev, "%s: mmio_start = 0x%x, mmio_end = 0x%x\n",
		__func__, (unsigned int)res_mmio->start,
		(unsigned int)res_mmio->end);

	ctx->mmio_start = ioremap_nocache(res_mmio->start,
					  (size_t)(resource_size(res_mmio)));
	if (!ctx->mmio_start) {
		dev_err(&pdev->dev, "Could not get ioremap\n");
		ret = -EACCES;
		goto err;
	}

	/* setup interrupt handler */
	ret = request_irq(irq, display_pipe_interrupt_handler, 0,
			  pdev->name, ctx);
	if (ret < 0) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto err;
	}

	ctx->irq = irq;

	ret = snd_pcm_new(card, INTEL_HAD, PCM_INDEX, MAX_PB_STREAMS,
			  MAX_CAP_STREAMS, &pcm);
	if (ret)
		goto err;

	/* setup private data which can be retrieved when required */
	pcm->private_data = ctx;
	pcm->private_free = snd_intelhad_pcm_free;
	pcm->info_flags = 0;
	strncpy(pcm->name, card->shortname, strlen(card->shortname));
	/* setup the ops for playabck */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			    &snd_intelhad_playback_ops);
	/* allocate dma pages for ALSA stream operations
	 * memory allocated is based on size, not max value
	 * thus using same argument for max & size
	 */
	snd_pcm_lib_preallocate_pages_for_all(pcm,
			SNDRV_DMA_TYPE_DEV, NULL,
			HAD_MAX_BUFFER, HAD_MAX_BUFFER);

	/* IEC958 controls */
	ret = snd_ctl_add(card, snd_ctl_new1(&had_control_iec958_mask, ctx));
	if (ret < 0)
		goto err;
	ret = snd_ctl_add(card, snd_ctl_new1(&had_control_iec958, ctx));
	if (ret < 0)
		goto err;

	init_channel_allocations();

	/* Register channel map controls */
	ret = had_register_chmap_ctls(ctx, pcm);
	if (ret < 0)
		goto err;

	ret = snd_card_register(card);
	if (ret)
		goto err;

	spin_lock_irqsave(&pdata->lpe_audio_slock, flags);
	pdata->notify_audio_lpe = notify_audio_lpe;
	if (pdata->notify_pending) {

		dev_dbg(&pdev->dev, "%s: handle pending notification\n", __func__);
		notify_audio_lpe(pdev);
		pdata->notify_pending = false;
	}
	spin_unlock_irqrestore(&pdata->lpe_audio_slock, flags);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	schedule_work(&ctx->hdmi_audio_wq);

	return 0;

err:
	snd_card_free(card);
	return ret;
}

/*
 * hdmi_lpe_audio_remove - stop bridge with i915
 *
 * This function is called when the platform device is destroyed. The sound
 * card should have been removed on hot plug event.
 */
static int hdmi_lpe_audio_remove(struct platform_device *pdev)
{
	struct snd_intelhad *ctx = platform_get_drvdata(pdev);

	if (ctx->drv_status != HAD_DRV_DISCONNECTED)
		snd_intelhad_enable_audio_int(ctx, false);
	snd_card_free(ctx->card);
	return 0;
}

static struct platform_driver hdmi_lpe_audio_driver = {
	.driver		= {
		.name  = "hdmi-lpe-audio",
	},
	.probe          = hdmi_lpe_audio_probe,
	.remove		= hdmi_lpe_audio_remove,
	.suspend	= hdmi_lpe_audio_suspend,
	.resume		= hdmi_lpe_audio_resume
};

module_platform_driver(hdmi_lpe_audio_driver);
MODULE_ALIAS("platform:hdmi_lpe_audio");

MODULE_AUTHOR("Sailaja Bandarupalli <sailaja.bandarupalli@intel.com>");
MODULE_AUTHOR("Ramesh Babu K V <ramesh.babu@intel.com>");
MODULE_AUTHOR("Vaibhav Agarwal <vaibhav.agarwal@intel.com>");
MODULE_AUTHOR("Jerome Anand <jerome.anand@intel.com>");
MODULE_DESCRIPTION("Intel HDMI Audio driver");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{Intel,Intel_HAD}");
