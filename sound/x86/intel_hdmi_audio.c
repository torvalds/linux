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

#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <asm/set_memory.h>
#include <sound/core.h>
#include <sound/asoundef.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <sound/jack.h>
#include <drm/drm_edid.h>
#include <drm/intel_lpe_audio.h>
#include "intel_hdmi_audio.h"

#define for_each_pipe(card_ctx, pipe) \
	for ((pipe) = 0; (pipe) < (card_ctx)->num_pipes; (pipe)++)
#define for_each_port(card_ctx, port) \
	for ((port) = 0; (port) < (card_ctx)->num_ports; (port)++)

/*standard module options for ALSA. This module supports only one card*/
static int hdmi_card_index = SNDRV_DEFAULT_IDX1;
static char *hdmi_card_id = SNDRV_DEFAULT_STR1;
static bool single_port;

module_param_named(index, hdmi_card_index, int, 0444);
MODULE_PARM_DESC(index,
		"Index value for INTEL Intel HDMI Audio controller.");
module_param_named(id, hdmi_card_id, charp, 0444);
MODULE_PARM_DESC(id,
		"ID string for INTEL Intel HDMI Audio controller.");
module_param(single_port, bool, 0444);
MODULE_PARM_DESC(single_port,
		"Single-port mode (for compatibility)");

/*
 * ELD SA bits in the CEA Speaker Allocation data block
 */
static const int eld_speaker_allocation_bits[] = {
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

static const struct channel_map_table map_tables[] = {
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
static const struct snd_pcm_hardware had_pcm_hardware = {
	.info =	(SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_NO_PERIOD_WAKEUP),
	.formats = (SNDRV_PCM_FMTBIT_S16_LE |
		    SNDRV_PCM_FMTBIT_S24_LE |
		    SNDRV_PCM_FMTBIT_S32_LE),
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

/* Get the active PCM substream;
 * Call had_substream_put() for unreferecing.
 * Don't call this inside had_spinlock, as it takes by itself
 */
static struct snd_pcm_substream *
had_substream_get(struct snd_intelhad *intelhaddata)
{
	struct snd_pcm_substream *substream;
	unsigned long flags;

	spin_lock_irqsave(&intelhaddata->had_spinlock, flags);
	substream = intelhaddata->stream_info.substream;
	if (substream)
		intelhaddata->stream_info.substream_refcount++;
	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flags);
	return substream;
}

/* Unref the active PCM substream;
 * Don't call this inside had_spinlock, as it takes by itself
 */
static void had_substream_put(struct snd_intelhad *intelhaddata)
{
	unsigned long flags;

	spin_lock_irqsave(&intelhaddata->had_spinlock, flags);
	intelhaddata->stream_info.substream_refcount--;
	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flags);
}

static u32 had_config_offset(int pipe)
{
	switch (pipe) {
	default:
	case 0:
		return AUDIO_HDMI_CONFIG_A;
	case 1:
		return AUDIO_HDMI_CONFIG_B;
	case 2:
		return AUDIO_HDMI_CONFIG_C;
	}
}

/* Register access functions */
static u32 had_read_register_raw(struct snd_intelhad_card *card_ctx,
				 int pipe, u32 reg)
{
	return ioread32(card_ctx->mmio_start + had_config_offset(pipe) + reg);
}

static void had_write_register_raw(struct snd_intelhad_card *card_ctx,
				   int pipe, u32 reg, u32 val)
{
	iowrite32(val, card_ctx->mmio_start + had_config_offset(pipe) + reg);
}

static void had_read_register(struct snd_intelhad *ctx, u32 reg, u32 *val)
{
	if (!ctx->connected)
		*val = 0;
	else
		*val = had_read_register_raw(ctx->card_ctx, ctx->pipe, reg);
}

static void had_write_register(struct snd_intelhad *ctx, u32 reg, u32 val)
{
	if (ctx->connected)
		had_write_register_raw(ctx->card_ctx, ctx->pipe, reg, val);
}

/*
 * enable / disable audio configuration
 *
 * The normal read/modify should not directly be used on VLV2 for
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
 */
static void had_enable_audio(struct snd_intelhad *intelhaddata,
			     bool enable)
{
	/* update the cached value */
	intelhaddata->aud_config.regx.aud_en = enable;
	had_write_register(intelhaddata, AUD_CONFIG,
			   intelhaddata->aud_config.regval);
}

/* forcibly ACKs to both BUFFER_DONE and BUFFER_UNDERRUN interrupts */
static void had_ack_irqs(struct snd_intelhad *ctx)
{
	u32 status_reg;

	if (!ctx->connected)
		return;
	had_read_register(ctx, AUD_HDMI_STATUS, &status_reg);
	status_reg |= HDMI_AUDIO_BUFFER_DONE | HDMI_AUDIO_UNDERRUN;
	had_write_register(ctx, AUD_HDMI_STATUS, status_reg);
	had_read_register(ctx, AUD_HDMI_STATUS, &status_reg);
}

/* Reset buffer pointers */
static void had_reset_audio(struct snd_intelhad *intelhaddata)
{
	had_write_register(intelhaddata, AUD_HDMI_STATUS,
			   AUD_HDMI_STATUSG_MASK_FUNCRST);
	had_write_register(intelhaddata, AUD_HDMI_STATUS, 0);
}

/*
 * initialize audio channel status registers
 * This function is called in the prepare callback
 */
static int had_prog_status_reg(struct snd_pcm_substream *substream,
			struct snd_intelhad *intelhaddata)
{
	union aud_cfg cfg_val = {.regval = 0};
	union aud_ch_status_0 ch_stat0 = {.regval = 0};
	union aud_ch_status_1 ch_stat1 = {.regval = 0};

	ch_stat0.regx.lpcm_id = (intelhaddata->aes_bits &
					  IEC958_AES0_NONAUDIO) >> 1;
	ch_stat0.regx.clk_acc = (intelhaddata->aes_bits &
					  IEC958_AES3_CON_CLOCK) >> 4;
	cfg_val.regx.val_bit = ch_stat0.regx.lpcm_id;

	switch (substream->runtime->rate) {
	case AUD_SAMPLE_RATE_32:
		ch_stat0.regx.samp_freq = CH_STATUS_MAP_32KHZ;
		break;

	case AUD_SAMPLE_RATE_44_1:
		ch_stat0.regx.samp_freq = CH_STATUS_MAP_44KHZ;
		break;
	case AUD_SAMPLE_RATE_48:
		ch_stat0.regx.samp_freq = CH_STATUS_MAP_48KHZ;
		break;
	case AUD_SAMPLE_RATE_88_2:
		ch_stat0.regx.samp_freq = CH_STATUS_MAP_88KHZ;
		break;
	case AUD_SAMPLE_RATE_96:
		ch_stat0.regx.samp_freq = CH_STATUS_MAP_96KHZ;
		break;
	case AUD_SAMPLE_RATE_176_4:
		ch_stat0.regx.samp_freq = CH_STATUS_MAP_176KHZ;
		break;
	case AUD_SAMPLE_RATE_192:
		ch_stat0.regx.samp_freq = CH_STATUS_MAP_192KHZ;
		break;

	default:
		/* control should never come here */
		return -EINVAL;
	}

	had_write_register(intelhaddata,
			   AUD_CH_STATUS_0, ch_stat0.regval);

	switch (substream->runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		ch_stat1.regx.max_wrd_len = MAX_SMPL_WIDTH_20;
		ch_stat1.regx.wrd_len = SMPL_WIDTH_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		ch_stat1.regx.max_wrd_len = MAX_SMPL_WIDTH_24;
		ch_stat1.regx.wrd_len = SMPL_WIDTH_24BITS;
		break;
	default:
		return -EINVAL;
	}

	had_write_register(intelhaddata,
			   AUD_CH_STATUS_1, ch_stat1.regval);
	return 0;
}

/*
 * function to initialize audio
 * registers and buffer confgiuration registers
 * This function is called in the prepare callback
 */
static int had_init_audio_ctrl(struct snd_pcm_substream *substream,
			       struct snd_intelhad *intelhaddata)
{
	union aud_cfg cfg_val = {.regval = 0};
	union aud_buf_config buf_cfg = {.regval = 0};
	u8 channels;

	had_prog_status_reg(substream, intelhaddata);

	buf_cfg.regx.audio_fifo_watermark = FIFO_THRESHOLD;
	buf_cfg.regx.dma_fifo_watermark = DMA_FIFO_THRESHOLD;
	buf_cfg.regx.aud_delay = 0;
	had_write_register(intelhaddata, AUD_BUF_CONFIG, buf_cfg.regval);

	channels = substream->runtime->channels;
	cfg_val.regx.num_ch = channels - 2;
	if (channels <= 2)
		cfg_val.regx.layout = LAYOUT0;
	else
		cfg_val.regx.layout = LAYOUT1;

	if (substream->runtime->format == SNDRV_PCM_FORMAT_S16_LE)
		cfg_val.regx.packet_mode = 1;

	if (substream->runtime->format == SNDRV_PCM_FORMAT_S32_LE)
		cfg_val.regx.left_align = 1;

	cfg_val.regx.val_bit = 1;

	/* fix up the DP bits */
	if (intelhaddata->dp_output) {
		cfg_val.regx.dp_modei = 1;
		cfg_val.regx.set = 1;
	}

	had_write_register(intelhaddata, AUD_CONFIG, cfg_val.regval);
	intelhaddata->aud_config = cfg_val;
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
static int had_channel_allocation(struct snd_intelhad *intelhaddata,
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
		if (intelhaddata->eld[DRM_ELD_SPEAKER] & (1 << i))
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
	const struct channel_map_table *t = map_tables;

	for (; t->map; t++) {
		if (t->spk_mask == spk)
			return t->map;
	}
	return 0;
}

static void had_build_channel_allocation_map(struct snd_intelhad *intelhaddata)
{
	int i, c;
	int spk_mask = 0;
	struct snd_pcm_chmap_elem *chmap;
	u8 eld_high, eld_high_mask = 0xF0;
	u8 high_msb;

	kfree(intelhaddata->chmap->chmap);
	intelhaddata->chmap->chmap = NULL;

	chmap = kzalloc(sizeof(*chmap), GFP_KERNEL);
	if (!chmap)
		return;

	dev_dbg(intelhaddata->dev, "eld speaker = %x\n",
		intelhaddata->eld[DRM_ELD_SPEAKER]);

	/* WA: Fix the max channel supported to 8 */

	/*
	 * Sink may support more than 8 channels, if eld_high has more than
	 * one bit set. SOC supports max 8 channels.
	 * Refer eld_speaker_allocation_bits, for sink speaker allocation
	 */

	/* if 0x2F < eld < 0x4F fall back to 0x2f, else fall back to 0x4F */
	eld_high = intelhaddata->eld[DRM_ELD_SPEAKER] & eld_high_mask;
	if ((eld_high & (eld_high-1)) && (eld_high > 0x1F)) {
		/* eld_high & (eld_high-1): if more than 1 bit set */
		/* 0x1F: 7 channels */
		for (i = 1; i < 4; i++) {
			high_msb = eld_high & (0x80 >> i);
			if (high_msb) {
				intelhaddata->eld[DRM_ELD_SPEAKER] &=
					high_msb | 0xF;
				break;
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(eld_speaker_allocation_bits); i++) {
		if (intelhaddata->eld[DRM_ELD_SPEAKER] & (1 << i))
			spk_mask |= eld_speaker_allocation_bits[i];
	}

	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		if (spk_mask == channel_allocations[i].spk_mask) {
			for (c = 0; c < channel_allocations[i].channels; c++) {
				chmap->map[c] = spk_to_chmap(
					channel_allocations[i].speakers[
						(MAX_SPEAKERS - 1) - c]);
			}
			chmap->channels = channel_allocations[i].channels;
			intelhaddata->chmap->chmap = chmap;
			break;
		}
	}
	if (i >= ARRAY_SIZE(channel_allocations))
		kfree(chmap);
}

/*
 * ALSA API channel-map control callbacks
 */
static int had_chmap_ctl_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
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
	int i;
	const struct snd_pcm_chmap_elem *chmap;

	memset(ucontrol->value.integer.value, 0,
	       sizeof(long) * HAD_MAX_CHANNEL);
	mutex_lock(&intelhaddata->mutex);
	if (!intelhaddata->chmap->chmap) {
		mutex_unlock(&intelhaddata->mutex);
		return 0;
	}

	chmap = intelhaddata->chmap->chmap;
	for (i = 0; i < chmap->channels; i++)
		ucontrol->value.integer.value[i] = chmap->map[i];
	mutex_unlock(&intelhaddata->mutex);

	return 0;
}

static int had_register_chmap_ctls(struct snd_intelhad *intelhaddata,
						struct snd_pcm *pcm)
{
	int err;

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
 * Initialize Data Island Packets registers
 * This function is called in the prepare callback
 */
static void had_prog_dip(struct snd_pcm_substream *substream,
			 struct snd_intelhad *intelhaddata)
{
	int i;
	union aud_ctrl_st ctrl_state = {.regval = 0};
	union aud_info_frame2 frame2 = {.regval = 0};
	union aud_info_frame3 frame3 = {.regval = 0};
	u8 checksum = 0;
	u32 info_frame;
	int channels;
	int ca;

	channels = substream->runtime->channels;

	had_write_register(intelhaddata, AUD_CNTL_ST, ctrl_state.regval);

	ca = had_channel_allocation(intelhaddata, channels);
	if (intelhaddata->dp_output) {
		info_frame = DP_INFO_FRAME_WORD1;
		frame2.regval = (substream->runtime->channels - 1) | (ca << 24);
	} else {
		info_frame = HDMI_INFO_FRAME_WORD1;
		frame2.regx.chnl_cnt = substream->runtime->channels - 1;
		frame3.regx.chnl_alloc = ca;

		/* Calculte the byte wide checksum for all valid DIP words */
		for (i = 0; i < BYTES_PER_WORD; i++)
			checksum += (info_frame >> (i * 8)) & 0xff;
		for (i = 0; i < BYTES_PER_WORD; i++)
			checksum += (frame2.regval >> (i * 8)) & 0xff;
		for (i = 0; i < BYTES_PER_WORD; i++)
			checksum += (frame3.regval >> (i * 8)) & 0xff;

		frame2.regx.chksum = -(checksum);
	}

	had_write_register(intelhaddata, AUD_HDMIW_INFOFR, info_frame);
	had_write_register(intelhaddata, AUD_HDMIW_INFOFR, frame2.regval);
	had_write_register(intelhaddata, AUD_HDMIW_INFOFR, frame3.regval);

	/* program remaining DIP words with zero */
	for (i = 0; i < HAD_MAX_DIP_WORDS-VALID_DIP_WORDS; i++)
		had_write_register(intelhaddata, AUD_HDMIW_INFOFR, 0x0);

	ctrl_state.regx.dip_freq = 1;
	ctrl_state.regx.dip_en_sta = 1;
	had_write_register(intelhaddata, AUD_CNTL_ST, ctrl_state.regval);
}

static int had_calculate_maud_value(u32 aud_samp_freq, u32 link_rate)
{
	u32 maud_val;

	/* Select maud according to DP 1.2 spec */
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
 * Program HDMI audio CTS value
 *
 * @aud_samp_freq: sampling frequency of audio data
 * @tmds: sampling frequency of the display data
 * @link_rate: DP link rate
 * @n_param: N value, depends on aud_samp_freq
 * @intelhaddata: substream private data
 *
 * Program CTS register based on the audio and display sampling frequency
 */
static void had_prog_cts(u32 aud_samp_freq, u32 tmds, u32 link_rate,
			 u32 n_param, struct snd_intelhad *intelhaddata)
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
	int n_val;

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
 * Program HDMI audio N value
 *
 * @aud_samp_freq: sampling frequency of audio data
 * @n_param: N value, depends on aud_samp_freq
 * @intelhaddata: substream private data
 *
 * This function is called in the prepare callback.
 * It programs based on the audio and display sampling frequency
 */
static int had_prog_n(u32 aud_samp_freq, u32 *n_param,
		      struct snd_intelhad *intelhaddata)
{
	int n_val;

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

/*
 * PCM ring buffer handling
 *
 * The hardware provides a ring buffer with the fixed 4 buffer descriptors
 * (BDs).  The driver maps these 4 BDs onto the PCM ring buffer.  The mapping
 * moves at each period elapsed.  The below illustrates how it works:
 *
 * At time=0
 *  PCM | 0 | 1 | 2 | 3 | 4 | 5 | .... |n-1|
 *  BD  | 0 | 1 | 2 | 3 |
 *
 * At time=1 (period elapsed)
 *  PCM | 0 | 1 | 2 | 3 | 4 | 5 | .... |n-1|
 *  BD      | 1 | 2 | 3 | 0 |
 *
 * At time=2 (second period elapsed)
 *  PCM | 0 | 1 | 2 | 3 | 4 | 5 | .... |n-1|
 *  BD          | 2 | 3 | 0 | 1 |
 *
 * The bd_head field points to the index of the BD to be read.  It's also the
 * position to be filled at next.  The pcm_head and the pcm_filled fields
 * point to the indices of the current position and of the next position to
 * be filled, respectively.  For PCM buffer there are both _head and _filled
 * because they may be difference when nperiods > 4.  For example, in the
 * example above at t=1, bd_head=1 and pcm_head=1 while pcm_filled=5:
 *
 * pcm_head (=1) --v               v-- pcm_filled (=5)
 *       PCM | 0 | 1 | 2 | 3 | 4 | 5 | .... |n-1|
 *       BD      | 1 | 2 | 3 | 0 |
 *  bd_head (=1) --^               ^-- next to fill (= bd_head)
 *
 * For nperiods < 4, the remaining BDs out of 4 are marked as invalid, so that
 * the hardware skips those BDs in the loop.
 *
 * An exceptional setup is the case with nperiods=1.  Since we have to update
 * BDs after finishing one BD processing, we'd need at least two BDs, where
 * both BDs point to the same content, the same address, the same size of the
 * whole PCM buffer.
 */

#define AUD_BUF_ADDR(x)		(AUD_BUF_A_ADDR + (x) * HAD_REG_WIDTH)
#define AUD_BUF_LEN(x)		(AUD_BUF_A_LENGTH + (x) * HAD_REG_WIDTH)

/* Set up a buffer descriptor at the "filled" position */
static void had_prog_bd(struct snd_pcm_substream *substream,
			struct snd_intelhad *intelhaddata)
{
	int idx = intelhaddata->bd_head;
	int ofs = intelhaddata->pcmbuf_filled * intelhaddata->period_bytes;
	u32 addr = substream->runtime->dma_addr + ofs;

	addr |= AUD_BUF_VALID;
	if (!substream->runtime->no_period_wakeup)
		addr |= AUD_BUF_INTR_EN;
	had_write_register(intelhaddata, AUD_BUF_ADDR(idx), addr);
	had_write_register(intelhaddata, AUD_BUF_LEN(idx),
			   intelhaddata->period_bytes);

	/* advance the indices to the next */
	intelhaddata->bd_head++;
	intelhaddata->bd_head %= intelhaddata->num_bds;
	intelhaddata->pcmbuf_filled++;
	intelhaddata->pcmbuf_filled %= substream->runtime->periods;
}

/* invalidate a buffer descriptor with the given index */
static void had_invalidate_bd(struct snd_intelhad *intelhaddata,
			      int idx)
{
	had_write_register(intelhaddata, AUD_BUF_ADDR(idx), 0);
	had_write_register(intelhaddata, AUD_BUF_LEN(idx), 0);
}

/* Initial programming of ring buffer */
static void had_init_ringbuf(struct snd_pcm_substream *substream,
			     struct snd_intelhad *intelhaddata)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int i, num_periods;

	num_periods = runtime->periods;
	intelhaddata->num_bds = min(num_periods, HAD_NUM_OF_RING_BUFS);
	/* set the minimum 2 BDs for num_periods=1 */
	intelhaddata->num_bds = max(intelhaddata->num_bds, 2U);
	intelhaddata->period_bytes =
		frames_to_bytes(runtime, runtime->period_size);
	WARN_ON(intelhaddata->period_bytes & 0x3f);

	intelhaddata->bd_head = 0;
	intelhaddata->pcmbuf_head = 0;
	intelhaddata->pcmbuf_filled = 0;

	for (i = 0; i < HAD_NUM_OF_RING_BUFS; i++) {
		if (i < intelhaddata->num_bds)
			had_prog_bd(substream, intelhaddata);
		else /* invalidate the rest */
			had_invalidate_bd(intelhaddata, i);
	}

	intelhaddata->bd_head = 0; /* reset at head again before starting */
}

/* process a bd, advance to the next */
static void had_advance_ringbuf(struct snd_pcm_substream *substream,
				struct snd_intelhad *intelhaddata)
{
	int num_periods = substream->runtime->periods;

	/* reprogram the next buffer */
	had_prog_bd(substream, intelhaddata);

	/* proceed to next */
	intelhaddata->pcmbuf_head++;
	intelhaddata->pcmbuf_head %= num_periods;
}

/* process the current BD(s);
 * returns the current PCM buffer byte position, or -EPIPE for underrun.
 */
static int had_process_ringbuf(struct snd_pcm_substream *substream,
			       struct snd_intelhad *intelhaddata)
{
	int len, processed;
	unsigned long flags;

	processed = 0;
	spin_lock_irqsave(&intelhaddata->had_spinlock, flags);
	for (;;) {
		/* get the remaining bytes on the buffer */
		had_read_register(intelhaddata,
				  AUD_BUF_LEN(intelhaddata->bd_head),
				  &len);
		if (len < 0 || len > intelhaddata->period_bytes) {
			dev_dbg(intelhaddata->dev, "Invalid buf length %d\n",
				len);
			len = -EPIPE;
			goto out;
		}

		if (len > 0) /* OK, this is the current buffer */
			break;

		/* len=0 => already empty, check the next buffer */
		if (++processed >= intelhaddata->num_bds) {
			len = -EPIPE; /* all empty? - report underrun */
			goto out;
		}
		had_advance_ringbuf(substream, intelhaddata);
	}

	len = intelhaddata->period_bytes - len;
	len += intelhaddata->period_bytes * intelhaddata->pcmbuf_head;
 out:
	spin_unlock_irqrestore(&intelhaddata->had_spinlock, flags);
	return len;
}

/* called from irq handler */
static void had_process_buffer_done(struct snd_intelhad *intelhaddata)
{
	struct snd_pcm_substream *substream;

	substream = had_substream_get(intelhaddata);
	if (!substream)
		return; /* no stream? - bail out */

	if (!intelhaddata->connected) {
		snd_pcm_stop_xrun(substream);
		goto out; /* disconnected? - bail out */
	}

	/* process or stop the stream */
	if (had_process_ringbuf(substream, intelhaddata) < 0)
		snd_pcm_stop_xrun(substream);
	else
		snd_pcm_period_elapsed(substream);

 out:
	had_substream_put(intelhaddata);
}

/*
 * The interrupt status 'sticky' bits might not be cleared by
 * setting '1' to that bit once...
 */
static void wait_clear_underrun_bit(struct snd_intelhad *intelhaddata)
{
	int i;
	u32 val;

	for (i = 0; i < 100; i++) {
		/* clear bit30, 31 AUD_HDMI_STATUS */
		had_read_register(intelhaddata, AUD_HDMI_STATUS, &val);
		if (!(val & AUD_HDMI_STATUS_MASK_UNDERRUN))
			return;
		udelay(100);
		cond_resched();
		had_write_register(intelhaddata, AUD_HDMI_STATUS, val);
	}
	dev_err(intelhaddata->dev, "Unable to clear UNDERRUN bits\n");
}

/* Perform some reset procedure but only when need_reset is set;
 * this is called from prepare or hw_free callbacks once after trigger STOP
 * or underrun has been processed in order to settle down the h/w state.
 */
static void had_do_reset(struct snd_intelhad *intelhaddata)
{
	if (!intelhaddata->need_reset || !intelhaddata->connected)
		return;

	/* Reset buffer pointers */
	had_reset_audio(intelhaddata);
	wait_clear_underrun_bit(intelhaddata);
	intelhaddata->need_reset = false;
}

/* called from irq handler */
static void had_process_buffer_underrun(struct snd_intelhad *intelhaddata)
{
	struct snd_pcm_substream *substream;

	/* Report UNDERRUN error to above layers */
	substream = had_substream_get(intelhaddata);
	if (substream) {
		snd_pcm_stop_xrun(substream);
		had_substream_put(intelhaddata);
	}
	intelhaddata->need_reset = true;
}

/*
 * ALSA PCM open callback
 */
static int had_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_intelhad *intelhaddata;
	struct snd_pcm_runtime *runtime;
	int retval;

	intelhaddata = snd_pcm_substream_chip(substream);
	runtime = substream->runtime;

	pm_runtime_get_sync(intelhaddata->dev);

	/* set the runtime hw parameter with local snd_pcm_hardware struct */
	runtime->hw = had_pcm_hardware;

	retval = snd_pcm_hw_constraint_integer(runtime,
			 SNDRV_PCM_HW_PARAM_PERIODS);
	if (retval < 0)
		goto error;

	/* Make sure, that the period size is always aligned
	 * 64byte boundary
	 */
	retval = snd_pcm_hw_constraint_step(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 64);
	if (retval < 0)
		goto error;

	retval = snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	if (retval < 0)
		goto error;

	/* expose PCM substream */
	spin_lock_irq(&intelhaddata->had_spinlock);
	intelhaddata->stream_info.substream = substream;
	intelhaddata->stream_info.substream_refcount++;
	spin_unlock_irq(&intelhaddata->had_spinlock);

	return retval;
 error:
	pm_runtime_mark_last_busy(intelhaddata->dev);
	pm_runtime_put_autosuspend(intelhaddata->dev);
	return retval;
}

/*
 * ALSA PCM close callback
 */
static int had_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_intelhad *intelhaddata;

	intelhaddata = snd_pcm_substream_chip(substream);

	/* unreference and sync with the pending PCM accesses */
	spin_lock_irq(&intelhaddata->had_spinlock);
	intelhaddata->stream_info.substream = NULL;
	intelhaddata->stream_info.substream_refcount--;
	while (intelhaddata->stream_info.substream_refcount > 0) {
		spin_unlock_irq(&intelhaddata->had_spinlock);
		cpu_relax();
		spin_lock_irq(&intelhaddata->had_spinlock);
	}
	spin_unlock_irq(&intelhaddata->had_spinlock);

	pm_runtime_mark_last_busy(intelhaddata->dev);
	pm_runtime_put_autosuspend(intelhaddata->dev);
	return 0;
}

/*
 * ALSA PCM hw_params callback
 */
static int had_pcm_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hw_params)
{
	struct snd_intelhad *intelhaddata;
	unsigned long addr;
	int pages, buf_size, retval;

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

/*
 * ALSA PCM hw_free callback
 */
static int had_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_intelhad *intelhaddata;
	unsigned long addr;
	u32 pages;

	intelhaddata = snd_pcm_substream_chip(substream);
	had_do_reset(intelhaddata);

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

/*
 * ALSA PCM trigger callback
 */
static int had_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int retval = 0;
	struct snd_intelhad *intelhaddata;

	intelhaddata = snd_pcm_substream_chip(substream);

	spin_lock(&intelhaddata->had_spinlock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		/* Enable Audio */
		had_ack_irqs(intelhaddata); /* FIXME: do we need this? */
		had_enable_audio(intelhaddata, true);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		/* Disable Audio */
		had_enable_audio(intelhaddata, false);
		intelhaddata->need_reset = true;
		break;

	default:
		retval = -EINVAL;
	}
	spin_unlock(&intelhaddata->had_spinlock);
	return retval;
}

/*
 * ALSA PCM prepare callback
 */
static int had_pcm_prepare(struct snd_pcm_substream *substream)
{
	int retval;
	u32 disp_samp_freq, n_param;
	u32 link_rate = 0;
	struct snd_intelhad *intelhaddata;
	struct snd_pcm_runtime *runtime;

	intelhaddata = snd_pcm_substream_chip(substream);
	runtime = substream->runtime;

	dev_dbg(intelhaddata->dev, "period_size=%d\n",
		(int)frames_to_bytes(runtime, runtime->period_size));
	dev_dbg(intelhaddata->dev, "periods=%d\n", runtime->periods);
	dev_dbg(intelhaddata->dev, "buffer_size=%d\n",
		(int)snd_pcm_lib_buffer_bytes(substream));
	dev_dbg(intelhaddata->dev, "rate=%d\n", runtime->rate);
	dev_dbg(intelhaddata->dev, "channels=%d\n", runtime->channels);

	had_do_reset(intelhaddata);

	/* Get N value in KHz */
	disp_samp_freq = intelhaddata->tmds_clock_speed;

	retval = had_prog_n(substream->runtime->rate, &n_param, intelhaddata);
	if (retval) {
		dev_err(intelhaddata->dev,
			"programming N value failed %#x\n", retval);
		goto prep_end;
	}

	if (intelhaddata->dp_output)
		link_rate = intelhaddata->link_rate;

	had_prog_cts(substream->runtime->rate, disp_samp_freq, link_rate,
		     n_param, intelhaddata);

	had_prog_dip(substream, intelhaddata);

	retval = had_init_audio_ctrl(substream, intelhaddata);

	/* Prog buffer address */
	had_init_ringbuf(substream, intelhaddata);

	/*
	 * Program channel mapping in following order:
	 * FL, FR, C, LFE, RL, RR
	 */

	had_write_register(intelhaddata, AUD_BUF_CH_SWAP, SWAP_LFE_CENTER);

prep_end:
	return retval;
}

/*
 * ALSA PCM pointer callback
 */
static snd_pcm_uframes_t had_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_intelhad *intelhaddata;
	int len;

	intelhaddata = snd_pcm_substream_chip(substream);

	if (!intelhaddata->connected)
		return SNDRV_PCM_POS_XRUN;

	len = had_process_ringbuf(substream, intelhaddata);
	if (len < 0)
		return SNDRV_PCM_POS_XRUN;
	len = bytes_to_frames(substream->runtime, len);
	/* wrapping may happen when periods=1 */
	len %= substream->runtime->buffer_size;
	return len;
}

/*
 * ALSA PCM mmap callback
 */
static int had_pcm_mmap(struct snd_pcm_substream *substream,
			struct vm_area_struct *vma)
{
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start,
			substream->dma_buffer.addr >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

/*
 * ALSA PCM ops
 */
static const struct snd_pcm_ops had_pcm_ops = {
	.open =		had_pcm_open,
	.close =	had_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	had_pcm_hw_params,
	.hw_free =	had_pcm_hw_free,
	.prepare =	had_pcm_prepare,
	.trigger =	had_pcm_trigger,
	.pointer =	had_pcm_pointer,
	.mmap =		had_pcm_mmap,
};

/* process mode change of the running stream; called in mutex */
static int had_process_mode_change(struct snd_intelhad *intelhaddata)
{
	struct snd_pcm_substream *substream;
	int retval = 0;
	u32 disp_samp_freq, n_param;
	u32 link_rate = 0;

	substream = had_substream_get(intelhaddata);
	if (!substream)
		return 0;

	/* Disable Audio */
	had_enable_audio(intelhaddata, false);

	/* Update CTS value */
	disp_samp_freq = intelhaddata->tmds_clock_speed;

	retval = had_prog_n(substream->runtime->rate, &n_param, intelhaddata);
	if (retval) {
		dev_err(intelhaddata->dev,
			"programming N value failed %#x\n", retval);
		goto out;
	}

	if (intelhaddata->dp_output)
		link_rate = intelhaddata->link_rate;

	had_prog_cts(substream->runtime->rate, disp_samp_freq, link_rate,
		     n_param, intelhaddata);

	/* Enable Audio */
	had_enable_audio(intelhaddata, true);

out:
	had_substream_put(intelhaddata);
	return retval;
}

/* process hot plug, called from wq with mutex locked */
static void had_process_hot_plug(struct snd_intelhad *intelhaddata)
{
	struct snd_pcm_substream *substream;

	spin_lock_irq(&intelhaddata->had_spinlock);
	if (intelhaddata->connected) {
		dev_dbg(intelhaddata->dev, "Device already connected\n");
		spin_unlock_irq(&intelhaddata->had_spinlock);
		return;
	}

	/* Disable Audio */
	had_enable_audio(intelhaddata, false);

	intelhaddata->connected = true;
	dev_dbg(intelhaddata->dev,
		"%s @ %d:DEBUG PLUG/UNPLUG : HAD_DRV_CONNECTED\n",
			__func__, __LINE__);
	spin_unlock_irq(&intelhaddata->had_spinlock);

	had_build_channel_allocation_map(intelhaddata);

	/* Report to above ALSA layer */
	substream = had_substream_get(intelhaddata);
	if (substream) {
		snd_pcm_stop_xrun(substream);
		had_substream_put(intelhaddata);
	}

	snd_jack_report(intelhaddata->jack, SND_JACK_AVOUT);
}

/* process hot unplug, called from wq with mutex locked */
static void had_process_hot_unplug(struct snd_intelhad *intelhaddata)
{
	struct snd_pcm_substream *substream;

	spin_lock_irq(&intelhaddata->had_spinlock);
	if (!intelhaddata->connected) {
		dev_dbg(intelhaddata->dev, "Device already disconnected\n");
		spin_unlock_irq(&intelhaddata->had_spinlock);
		return;

	}

	/* Disable Audio */
	had_enable_audio(intelhaddata, false);

	intelhaddata->connected = false;
	dev_dbg(intelhaddata->dev,
		"%s @ %d:DEBUG PLUG/UNPLUG : HAD_DRV_DISCONNECTED\n",
			__func__, __LINE__);
	spin_unlock_irq(&intelhaddata->had_spinlock);

	kfree(intelhaddata->chmap->chmap);
	intelhaddata->chmap->chmap = NULL;

	/* Report to above ALSA layer */
	substream = had_substream_get(intelhaddata);
	if (substream) {
		snd_pcm_stop_xrun(substream);
		had_substream_put(intelhaddata);
	}

	snd_jack_report(intelhaddata->jack, 0);
}

/*
 * ALSA iec958 and ELD controls
 */

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

	mutex_lock(&intelhaddata->mutex);
	ucontrol->value.iec958.status[0] = (intelhaddata->aes_bits >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (intelhaddata->aes_bits >> 8) & 0xff;
	ucontrol->value.iec958.status[2] =
					(intelhaddata->aes_bits >> 16) & 0xff;
	ucontrol->value.iec958.status[3] =
					(intelhaddata->aes_bits >> 24) & 0xff;
	mutex_unlock(&intelhaddata->mutex);
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
	int changed = 0;

	val = (ucontrol->value.iec958.status[0] << 0) |
		(ucontrol->value.iec958.status[1] << 8) |
		(ucontrol->value.iec958.status[2] << 16) |
		(ucontrol->value.iec958.status[3] << 24);
	mutex_lock(&intelhaddata->mutex);
	if (intelhaddata->aes_bits != val) {
		intelhaddata->aes_bits = val;
		changed = 1;
	}
	mutex_unlock(&intelhaddata->mutex);
	return changed;
}

static int had_ctl_eld_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = HDMI_MAX_ELD_BYTES;
	return 0;
}

static int had_ctl_eld_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_intelhad *intelhaddata = snd_kcontrol_chip(kcontrol);

	mutex_lock(&intelhaddata->mutex);
	memcpy(ucontrol->value.bytes.data, intelhaddata->eld,
	       HDMI_MAX_ELD_BYTES);
	mutex_unlock(&intelhaddata->mutex);
	return 0;
}

static const struct snd_kcontrol_new had_controls[] = {
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, MASK),
		.info = had_iec958_info, /* shared */
		.get = had_iec958_mask_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.info = had_iec958_info,
		.get = had_iec958_get,
		.put = had_iec958_put,
	},
	{
		.access = (SNDRV_CTL_ELEM_ACCESS_READ |
			   SNDRV_CTL_ELEM_ACCESS_VOLATILE),
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "ELD",
		.info = had_ctl_eld_info,
		.get = had_ctl_eld_get,
	},
};

/*
 * audio interrupt handler
 */
static irqreturn_t display_pipe_interrupt_handler(int irq, void *dev_id)
{
	struct snd_intelhad_card *card_ctx = dev_id;
	u32 audio_stat[3] = {};
	int pipe, port;

	for_each_pipe(card_ctx, pipe) {
		/* use raw register access to ack IRQs even while disconnected */
		audio_stat[pipe] = had_read_register_raw(card_ctx, pipe,
							 AUD_HDMI_STATUS) &
			(HDMI_AUDIO_UNDERRUN | HDMI_AUDIO_BUFFER_DONE);

		if (audio_stat[pipe])
			had_write_register_raw(card_ctx, pipe,
					       AUD_HDMI_STATUS, audio_stat[pipe]);
	}

	for_each_port(card_ctx, port) {
		struct snd_intelhad *ctx = &card_ctx->pcm_ctx[port];
		int pipe = ctx->pipe;

		if (pipe < 0)
			continue;

		if (audio_stat[pipe] & HDMI_AUDIO_BUFFER_DONE)
			had_process_buffer_done(ctx);
		if (audio_stat[pipe] & HDMI_AUDIO_UNDERRUN)
			had_process_buffer_underrun(ctx);
	}

	return IRQ_HANDLED;
}

/*
 * monitor plug/unplug notification from i915; just kick off the work
 */
static void notify_audio_lpe(struct platform_device *pdev, int port)
{
	struct snd_intelhad_card *card_ctx = platform_get_drvdata(pdev);
	struct snd_intelhad *ctx;

	ctx = &card_ctx->pcm_ctx[single_port ? 0 : port];
	if (single_port)
		ctx->port = port;

	schedule_work(&ctx->hdmi_audio_wq);
}

/* the work to handle monitor hot plug/unplug */
static void had_audio_wq(struct work_struct *work)
{
	struct snd_intelhad *ctx =
		container_of(work, struct snd_intelhad, hdmi_audio_wq);
	struct intel_hdmi_lpe_audio_pdata *pdata = ctx->dev->platform_data;
	struct intel_hdmi_lpe_audio_port_pdata *ppdata = &pdata->port[ctx->port];

	pm_runtime_get_sync(ctx->dev);
	mutex_lock(&ctx->mutex);
	if (ppdata->pipe < 0) {
		dev_dbg(ctx->dev, "%s: Event: HAD_NOTIFY_HOT_UNPLUG : port = %d\n",
			__func__, ctx->port);

		memset(ctx->eld, 0, sizeof(ctx->eld)); /* clear the old ELD */

		ctx->dp_output = false;
		ctx->tmds_clock_speed = 0;
		ctx->link_rate = 0;

		/* Shut down the stream */
		had_process_hot_unplug(ctx);

		ctx->pipe = -1;
	} else {
		dev_dbg(ctx->dev, "%s: HAD_NOTIFY_ELD : port = %d, tmds = %d\n",
			__func__, ctx->port, ppdata->ls_clock);

		memcpy(ctx->eld, ppdata->eld, sizeof(ctx->eld));

		ctx->dp_output = ppdata->dp_output;
		if (ctx->dp_output) {
			ctx->tmds_clock_speed = 0;
			ctx->link_rate = ppdata->ls_clock;
		} else {
			ctx->tmds_clock_speed = ppdata->ls_clock;
			ctx->link_rate = 0;
		}

		/*
		 * Shut down the stream before we change
		 * the pipe assignment for this pcm device
		 */
		had_process_hot_plug(ctx);

		ctx->pipe = ppdata->pipe;

		/* Restart the stream if necessary */
		had_process_mode_change(ctx);
	}

	mutex_unlock(&ctx->mutex);
	pm_runtime_mark_last_busy(ctx->dev);
	pm_runtime_put_autosuspend(ctx->dev);
}

/*
 * Jack interface
 */
static int had_create_jack(struct snd_intelhad *ctx,
			   struct snd_pcm *pcm)
{
	char hdmi_str[32];
	int err;

	snprintf(hdmi_str, sizeof(hdmi_str),
		 "HDMI/DP,pcm=%d", pcm->device);

	err = snd_jack_new(ctx->card_ctx->card, hdmi_str,
			   SND_JACK_AVOUT, &ctx->jack,
			   true, false);
	if (err < 0)
		return err;
	ctx->jack->private_data = ctx;
	return 0;
}

/*
 * PM callbacks
 */

static int hdmi_lpe_audio_runtime_suspend(struct device *dev)
{
	struct snd_intelhad_card *card_ctx = dev_get_drvdata(dev);
	int port;

	for_each_port(card_ctx, port) {
		struct snd_intelhad *ctx = &card_ctx->pcm_ctx[port];
		struct snd_pcm_substream *substream;

		substream = had_substream_get(ctx);
		if (substream) {
			snd_pcm_suspend(substream);
			had_substream_put(ctx);
		}
	}

	return 0;
}

static int __maybe_unused hdmi_lpe_audio_suspend(struct device *dev)
{
	struct snd_intelhad_card *card_ctx = dev_get_drvdata(dev);
	int err;

	err = hdmi_lpe_audio_runtime_suspend(dev);
	if (!err)
		snd_power_change_state(card_ctx->card, SNDRV_CTL_POWER_D3hot);
	return err;
}

static int hdmi_lpe_audio_runtime_resume(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);
	return 0;
}

static int __maybe_unused hdmi_lpe_audio_resume(struct device *dev)
{
	struct snd_intelhad_card *card_ctx = dev_get_drvdata(dev);

	hdmi_lpe_audio_runtime_resume(dev);
	snd_power_change_state(card_ctx->card, SNDRV_CTL_POWER_D0);
	return 0;
}

/* release resources */
static void hdmi_lpe_audio_free(struct snd_card *card)
{
	struct snd_intelhad_card *card_ctx = card->private_data;
	struct intel_hdmi_lpe_audio_pdata *pdata = card_ctx->dev->platform_data;
	int port;

	spin_lock_irq(&pdata->lpe_audio_slock);
	pdata->notify_audio_lpe = NULL;
	spin_unlock_irq(&pdata->lpe_audio_slock);

	for_each_port(card_ctx, port) {
		struct snd_intelhad *ctx = &card_ctx->pcm_ctx[port];

		cancel_work_sync(&ctx->hdmi_audio_wq);
	}

	if (card_ctx->mmio_start)
		iounmap(card_ctx->mmio_start);
	if (card_ctx->irq >= 0)
		free_irq(card_ctx->irq, card_ctx);
}

/*
 * hdmi_lpe_audio_probe - start bridge with i915
 *
 * This function is called when the i915 driver creates the
 * hdmi-lpe-audio platform device.
 */
static int hdmi_lpe_audio_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	struct snd_intelhad_card *card_ctx;
	struct snd_intelhad *ctx;
	struct snd_pcm *pcm;
	struct intel_hdmi_lpe_audio_pdata *pdata;
	int irq;
	struct resource *res_mmio;
	int port, ret;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "%s: quit: pdata not allocated by i915!!\n", __func__);
		return -EINVAL;
	}

	/* get resources */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Could not get irq resource: %d\n", irq);
		return irq;
	}

	res_mmio = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mmio) {
		dev_err(&pdev->dev, "Could not get IO_MEM resources\n");
		return -ENXIO;
	}

	/* create a card instance with ALSA framework */
	ret = snd_card_new(&pdev->dev, hdmi_card_index, hdmi_card_id,
			   THIS_MODULE, sizeof(*card_ctx), &card);
	if (ret)
		return ret;

	card_ctx = card->private_data;
	card_ctx->dev = &pdev->dev;
	card_ctx->card = card;
	strcpy(card->driver, INTEL_HAD);
	strcpy(card->shortname, "Intel HDMI/DP LPE Audio");
	strcpy(card->longname, "Intel HDMI/DP LPE Audio");

	card_ctx->irq = -1;

	card->private_free = hdmi_lpe_audio_free;

	platform_set_drvdata(pdev, card_ctx);

	card_ctx->num_pipes = pdata->num_pipes;
	card_ctx->num_ports = single_port ? 1 : pdata->num_ports;

	for_each_port(card_ctx, port) {
		ctx = &card_ctx->pcm_ctx[port];
		ctx->card_ctx = card_ctx;
		ctx->dev = card_ctx->dev;
		ctx->port = single_port ? -1 : port;
		ctx->pipe = -1;

		spin_lock_init(&ctx->had_spinlock);
		mutex_init(&ctx->mutex);
		INIT_WORK(&ctx->hdmi_audio_wq, had_audio_wq);
	}

	dev_dbg(&pdev->dev, "%s: mmio_start = 0x%x, mmio_end = 0x%x\n",
		__func__, (unsigned int)res_mmio->start,
		(unsigned int)res_mmio->end);

	card_ctx->mmio_start = ioremap_nocache(res_mmio->start,
					       (size_t)(resource_size(res_mmio)));
	if (!card_ctx->mmio_start) {
		dev_err(&pdev->dev, "Could not get ioremap\n");
		ret = -EACCES;
		goto err;
	}

	/* setup interrupt handler */
	ret = request_irq(irq, display_pipe_interrupt_handler, 0,
			  pdev->name, card_ctx);
	if (ret < 0) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto err;
	}

	card_ctx->irq = irq;

	/* only 32bit addressable */
	dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));

	init_channel_allocations();

	card_ctx->num_pipes = pdata->num_pipes;
	card_ctx->num_ports = single_port ? 1 : pdata->num_ports;

	for_each_port(card_ctx, port) {
		int i;

		ctx = &card_ctx->pcm_ctx[port];
		ret = snd_pcm_new(card, INTEL_HAD, port, MAX_PB_STREAMS,
				  MAX_CAP_STREAMS, &pcm);
		if (ret)
			goto err;

		/* setup private data which can be retrieved when required */
		pcm->private_data = ctx;
		pcm->info_flags = 0;
		strlcpy(pcm->name, card->shortname, strlen(card->shortname));
		/* setup the ops for playabck */
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &had_pcm_ops);

		/* allocate dma pages;
		 * try to allocate 600k buffer as default which is large enough
		 */
		snd_pcm_lib_preallocate_pages_for_all(pcm,
						      SNDRV_DMA_TYPE_DEV, NULL,
						      HAD_DEFAULT_BUFFER, HAD_MAX_BUFFER);

		/* create controls */
		for (i = 0; i < ARRAY_SIZE(had_controls); i++) {
			struct snd_kcontrol *kctl;

			kctl = snd_ctl_new1(&had_controls[i], ctx);
			if (!kctl) {
				ret = -ENOMEM;
				goto err;
			}

			kctl->id.device = pcm->device;

			ret = snd_ctl_add(card, kctl);
			if (ret < 0)
				goto err;
		}

		/* Register channel map controls */
		ret = had_register_chmap_ctls(ctx, pcm);
		if (ret < 0)
			goto err;

		ret = had_create_jack(ctx, pcm);
		if (ret < 0)
			goto err;
	}

	ret = snd_card_register(card);
	if (ret)
		goto err;

	spin_lock_irq(&pdata->lpe_audio_slock);
	pdata->notify_audio_lpe = notify_audio_lpe;
	spin_unlock_irq(&pdata->lpe_audio_slock);

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);

	dev_dbg(&pdev->dev, "%s: handle pending notification\n", __func__);
	for_each_port(card_ctx, port) {
		struct snd_intelhad *ctx = &card_ctx->pcm_ctx[port];

		schedule_work(&ctx->hdmi_audio_wq);
	}

	return 0;

err:
	snd_card_free(card);
	return ret;
}

/*
 * hdmi_lpe_audio_remove - stop bridge with i915
 *
 * This function is called when the platform device is destroyed.
 */
static int hdmi_lpe_audio_remove(struct platform_device *pdev)
{
	struct snd_intelhad_card *card_ctx = platform_get_drvdata(pdev);

	snd_card_free(card_ctx->card);
	return 0;
}

static const struct dev_pm_ops hdmi_lpe_audio_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(hdmi_lpe_audio_suspend, hdmi_lpe_audio_resume)
	SET_RUNTIME_PM_OPS(hdmi_lpe_audio_runtime_suspend,
			   hdmi_lpe_audio_runtime_resume, NULL)
};

static struct platform_driver hdmi_lpe_audio_driver = {
	.driver		= {
		.name  = "hdmi-lpe-audio",
		.pm = &hdmi_lpe_audio_pm,
	},
	.probe          = hdmi_lpe_audio_probe,
	.remove		= hdmi_lpe_audio_remove,
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
