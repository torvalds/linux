/*
 * imx-hdmi-dma.c  --  HDMI DMA driver for ALSA Soc Audio Layer
 *
 * Copyright (C) 2011-2014 Freescale Semiconductor, Inc.
 *
 * based on imx-pcm-dma-mx2.c
 * Copyright 2009 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This code is based on code copyrighted by Freescale,
 * Liam Girdwood, Javier Martin and probably others.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/mxc-hdmi-core.h>
#include <linux/platform_data/dma-imx.h>

#include <video/mxc_hdmi.h>

#include "imx-hdmi.h"

#define HDMI_DMA_BURST_UNSPECIFIED_LEGNTH	0
#define HDMI_DMA_BURST_INCR4			1
#define HDMI_DMA_BURST_INCR8			2
#define HDMI_DMA_BURST_INCR16			3

#define HDMI_BASE_ADDR 0x00120000

struct hdmi_sdma_script {
	int control_reg_addr;
	int status_reg_addr;
	int dma_start_addr;
	u32 buffer[20];
};

struct hdmi_dma_priv {
	struct snd_pcm_substream *substream;
	struct platform_device *pdev;

	struct snd_dma_buffer hw_buffer;
	unsigned long buffer_bytes;
	unsigned long appl_bytes;

	int periods;
	int period_time;
	int period_bytes;
	int dma_period_bytes;
	int buffer_ratio;

	unsigned long offset;

	snd_pcm_format_t format;
	int sample_align;
	int sample_bits;
	int channels;
	int rate;

	int frame_idx;

	bool tx_active;
	spinlock_t irq_lock;

	/* SDMA part */
	dma_addr_t phy_hdmi_sdma_t;
	struct hdmi_sdma_script *hdmi_sdma_t;
	struct dma_chan *dma_channel;
	struct dma_async_tx_descriptor *desc;
	struct imx_hdmi_sdma_params sdma_params;
};

/* bit 0:0:0:b:p(0):c:(u)0:(v)0 */
/* max 8 channels supported; channels are interleaved */
static u8 g_packet_head_table[48 * 8];

void hdmi_dma_copy_16_neon_lut(unsigned short *src, unsigned int *dst,
		int samples, unsigned char *lookup_table);
void hdmi_dma_copy_16_neon_fast(unsigned short *src, unsigned int *dst,
		int samples);
void hdmi_dma_copy_24_neon_lut(unsigned int *src, unsigned int *dst,
		int samples, unsigned char *lookup_table);
void hdmi_dma_copy_24_neon_fast(unsigned int *src, unsigned int *dst,
		int samples);
static void hdmi_dma_irq_enable(struct hdmi_dma_priv *priv);
static void hdmi_dma_irq_disable(struct hdmi_dma_priv *priv);

union hdmi_audio_header_t iec_header;
EXPORT_SYMBOL(iec_header);

/*
 * Note that the period size for DMA != period size for ALSA because the
 * driver adds iec frame info to the audio samples (in hdmi_dma_copy).
 *
 * Each 4 byte subframe = 1 byte of iec data + 3 byte audio sample.
 *
 * A 16 bit audio sample becomes 32 bits including the frame info. Ratio=2
 * A 24 bit audio sample becomes 32 bits including the frame info. Ratio=3:4
 * If the 24 bit raw audio is in 32 bit words, the
 *
 *  Original  Packed into  subframe  Ratio of size        Format
 *   sample    how many      size    of DMA buffer
 *   (bits)      bits                to ALSA buffer
 *  --------  -----------  --------  --------------  ------------------------
 *     16         16          32          2          SNDRV_PCM_FORMAT_S16_LE
 *     24         24          32          1.33       SNDRV_PCM_FORMAT_S24_3LE*
 *     24         32          32          1          SNDRV_PCM_FORMAT_S24_LE
 *
 * *so SNDRV_PCM_FORMAT_S24_3LE is not supported.
 */

/*
 * The minimum dma period is one IEC audio frame (192 * 4 * channels).
 * The maximum dma period for the HDMI DMA is 8K.
 *
 *   channels       minimum          maximum
 *                 dma period       dma period
 *   --------  ------------------   ----------
 *       2     192 * 4 * 2 = 1536   * 4 = 6144
 *       4     192 * 4 * 4 = 3072   * 2 = 6144
 *       6     192 * 4 * 6 = 4608   * 1 = 4608
 *       8     192 * 4 * 8 = 6144   * 1 = 6144
 *
 * Bottom line:
 * 1. Must keep the ratio of DMA buffer to ALSA buffer consistent.
 * 2. frame_idx is saved in the private data, so even if a frame cannot be
 *    transmitted in a period, it can be continued in the next period.  This
 *    is necessary for 6 ch.
 */
#define HDMI_DMA_PERIOD_BYTES		(12288)
#define HDMI_DMA_BUF_SIZE		(128 * 1024)
#define HDMI_PCM_BUF_SIZE		(128 * 1024)

#define hdmi_audio_debug(dev, reg) \
	dev_dbg(dev, #reg ": 0x%02x\n", hdmi_readb(reg))

#ifdef DEBUG
static void dumpregs(struct device *dev)
{
	hdmi_audio_debug(dev, HDMI_AHB_DMA_CONF0);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_START);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_STOP);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_THRSLD);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_STRADDR0);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_STPADDR0);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_BSTADDR0);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_MBLENGTH0);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_MBLENGTH1);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_STAT);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_INT);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_MASK);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_POL);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_CONF1);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_BUFFSTAT);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_BUFFINT);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_BUFFMASK);
	hdmi_audio_debug(dev, HDMI_AHB_DMA_BUFFPOL);
	hdmi_audio_debug(dev, HDMI_IH_MUTE_AHBDMAAUD_STAT0);
	hdmi_audio_debug(dev, HDMI_IH_AHBDMAAUD_STAT0);
	hdmi_audio_debug(dev, HDMI_IH_MUTE);
}

static void dumppriv(struct device *dev, struct hdmi_dma_priv *priv)
{
	dev_dbg(dev, "channels         = %d\n", priv->channels);
	dev_dbg(dev, "periods          = %d\n", priv->periods);
	dev_dbg(dev, "period_bytes     = %d\n", priv->period_bytes);
	dev_dbg(dev, "dma period_bytes = %d\n", priv->dma_period_bytes);
	dev_dbg(dev, "buffer_ratio     = %d\n", priv->buffer_ratio);
	dev_dbg(dev, "hw dma buffer    = 0x%08x\n", (int)priv->hw_buffer.addr);
	dev_dbg(dev, "dma buf size     = %d\n", (int)priv->buffer_bytes);
	dev_dbg(dev, "sample_rate      = %d\n", (int)priv->rate);
}
#else
static void dumpregs(struct device *dev) {}
static void dumppriv(struct device *dev, struct hdmi_dma_priv *priv) {}
#endif

/*
 * Conditions for DMA to work:
 * ((final_addr - initial_addr)>>2)+1) < 2k.  So max period is 8k.
 * (inital_addr & 0x3) == 0
 * (final_addr  & 0x3) == 0x3
 *
 * The DMA Period should be an integer multiple of the IEC 60958 audio
 * frame size, which is 768 bytes (192 * 4).
 */
static void hdmi_dma_set_addr(int start_addr, int dma_period_bytes)
{
	int final_addr = start_addr + dma_period_bytes - 1;

	hdmi_write4(start_addr, HDMI_AHB_DMA_STRADDR0);
	hdmi_write4(final_addr, HDMI_AHB_DMA_STPADDR0);
}

static void hdmi_dma_irq_set(bool set)
{
	u8 val = hdmi_readb(HDMI_AHB_DMA_MASK);

	if (set)
		val |= HDMI_AHB_DMA_DONE;
	else
		val &= (u8)~HDMI_AHB_DMA_DONE;

	hdmi_writeb(val, HDMI_AHB_DMA_MASK);
}

static void hdmi_mask(int mask)
{
	u8 regval = hdmi_readb(HDMI_AHB_DMA_MASK);

	if (mask)
		regval |= HDMI_AHB_DMA_ERROR | HDMI_AHB_DMA_FIFO_EMPTY;
	else
		regval &= (u8)~(HDMI_AHB_DMA_ERROR | HDMI_AHB_DMA_FIFO_EMPTY);

	hdmi_writeb(regval, HDMI_AHB_DMA_MASK);
}

int odd_ones(unsigned a)
{
	a ^= a >> 8;
	a ^= a >> 4;
	a ^= a >> 2;
	a ^= a >> 1;

	return a & 1;
}

/* Add frame information for one pcm subframe */
static u32 hdmi_dma_add_frame_info(struct hdmi_dma_priv *priv,
				   u32 pcm_data, int subframe_idx)
{
	union hdmi_audio_dma_data_t subframe;

	subframe.U = 0;
	iec_header.B.channel = subframe_idx;

	/* fill b (start-of-block) */
	subframe.B.b = (priv->frame_idx == 0) ? 1 : 0;

	/* fill c (channel status) */
	if (priv->frame_idx < 42)
		subframe.B.c = (iec_header.U >> priv->frame_idx) & 0x1;
	else
		subframe.B.c = 0;

	subframe.B.p = odd_ones(pcm_data);
	subframe.B.p ^= subframe.B.c;
	subframe.B.p ^= subframe.B.u;
	subframe.B.p ^= subframe.B.v;

	/* fill data */
	if (priv->sample_bits == 16)
		subframe.B.data = pcm_data << 8;
	else
		subframe.B.data = pcm_data;

	return subframe.U;
}

static void init_table(int channels)
{
	unsigned char *p = g_packet_head_table;
	int i, ch = 0;

	for (i = 0; i < 48; i++) {
		int b = 0;
		if (i == 0)
			b = 1;

		for (ch = 0; ch < channels; ch++) {
			int c = 0;
			if (i < 42) {
				iec_header.B.channel = ch+1;
				c = (iec_header.U >> i) & 0x1;
			}
			/* preset bit p as c */
			*p++ = (b << 4) | (c << 2) | (c << 3);
		}
	}
}

/*
 * FIXME: Disable NEON Optimization in hdmi, or it will cause crash of
 * pulseaudio in the userspace. There is no issue for the Optimization
 * implemenation, if only use "VPUSH, VPOP" in the function, the pulseaudio
 * will crash also. So for my assumption, we can't use the NEON in the
 * interrupt.(tasklet is implemented by softirq.)
 * Disable SMP, preempt, change the dma buffer to nocached, add protection of
 * Dn register and fpscr, all these operation have no effect to the result.
 */
#define HDMI_DMA_NO_NEON

#ifdef HDMI_DMA_NO_NEON
/* Optimization for IEC head */
static void hdmi_dma_copy_16_c_lut(u16 *src, u32 *dst, int samples,
				u8 *lookup_table)
{
	u32 sample, head, p;
	int i;

	for (i = 0; i < samples; i++) {
		/* get source sample */
		sample = *src++;

		/* xor every bit */
		p = sample ^ (sample >> 8);
		p ^= (p >> 4);
		p ^= (p >> 2);
		p ^= (p >> 1);
		p &= 1;	/* only want last bit */
		p <<= 3; /* bit p */

		/* get packet header */
		head = *lookup_table++;

		/* fix head */
		head ^= p;

		/* store */
		*dst++ = (head << 24) | (sample << 8);
	}
}

static void hdmi_dma_copy_16_c_fast(u16 *src, u32 *dst, int samples)
{
	u32 sample, p;
	int i;

	for (i = 0; i < samples; i++) {
		/* get source sample */
		sample = *src++;

		/* xor every bit */
		p = sample ^ (sample >> 8);
		p ^= (p >> 4);
		p ^= (p >> 2);
		p ^= (p >> 1);
		p &= 1;	/* only want last bit */
		p <<= 3; /* bit p */

		/* store */
		*dst++ = (p << 24) | (sample << 8);
	}
}

static void hdmi_dma_copy_16(u16 *src, u32 *dst, int framecnt, int channelcnt)
{
	/* split input frames into 192-frame each */
	int count_in_192 = (framecnt + 191) / 192;
	int i;

	for (i = 0; i < count_in_192; i++) {
		int count, samples;

		/* handles frame index [0, 48) */
		count = (framecnt < 48) ? framecnt : 48;
		samples = count * channelcnt;
		hdmi_dma_copy_16_c_lut(src, dst, samples, g_packet_head_table);
		framecnt -= count;
		if (framecnt == 0)
			break;

		src  += samples;
		dst += samples;

		/* handles frame index [48, 192) */
		count = (framecnt < 192 - 48) ? framecnt : 192 - 48;
		samples = count * channelcnt;
		hdmi_dma_copy_16_c_fast(src, dst, samples);
		framecnt -= count;
		src  += samples;
		dst += samples;
	}
}
#else
/* NEON optimization for IEC head*/

/**
 * Convert pcm samples to iec samples suitable for HDMI transfer.
 * PCM sample is 16 bits length.
 * Frame index always starts from 0.
 * Channel count can be 1, 2, 4, 6, or 8
 * Sample count (frame_count * channel_count) is multipliable by 8.
 */
static void hdmi_dma_copy_16(u16 *src, u32 *dst, int framecount, int channelcount)
{
	/* split input frames into 192-frame each */
	int i, count_in_192 = (framecount + 191) / 192;

	for (i = 0; i < count_in_192; i++) {
		int count, samples;

		/* handles frame index [0, 48) */
		count = (framecount < 48) ? framecount : 48;
		samples = count * channelcount;
		hdmi_dma_copy_16_neon_lut(src, dst, samples, g_packet_head_table);
		framecount -= count;
		if (framecount == 0)
			break;

		src += samples;
		dst += samples;

		/* handles frame index [48, 192) */
		count = (framecount < 192 - 48) ? framecount : (192 - 48);
		samples = count * channelcount;
		hdmi_dma_copy_16_neon_fast(src, dst, samples);
		framecount -= count;
		src += samples;
		dst += samples;
	}
}
#endif

static void hdmi_dma_mmap_copy(struct snd_pcm_substream *substream,
				int offset, int count)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hdmi_dma_priv *priv = runtime->private_data;
	struct device *dev = rtd->platform->dev;
	u32 framecount, *dst;
	u16 *src16;

	framecount = count / (priv->sample_align * priv->channels);

	/* hw_buffer is the destination for pcm data plus frame info. */
	dst = (u32 *)(priv->hw_buffer.area + (offset * priv->buffer_ratio));

	switch (priv->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		/* dma_buffer is the mmapped buffer we are copying pcm from. */
		src16 = (u16 *)(runtime->dma_area + offset);
		hdmi_dma_copy_16(src16, dst, framecount, priv->channels);
		break;
	default:
		dev_err(dev, "unsupported sample format %s\n",
				snd_pcm_format_name(priv->format));
		return;
	}
}

static void hdmi_dma_data_copy(struct snd_pcm_substream *substream,
				struct hdmi_dma_priv *priv, char type)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long offset, count, appl_bytes, space_to_end;

	if (runtime->access != SNDRV_PCM_ACCESS_MMAP_INTERLEAVED)
		return;

	appl_bytes =  runtime->status->hw_ptr * (runtime->frame_bits / 8);
	if (type == 'p')
		appl_bytes += 2 * priv->period_bytes;
	offset = appl_bytes % priv->buffer_bytes;

	switch (type) {
	case 'p':
		count = priv->period_bytes;
		space_to_end = priv->period_bytes;
		break;
	case 'b':
		count = priv->buffer_bytes;
		space_to_end = priv->buffer_bytes - offset;

		break;
	default:
		return;
	}

	if (count <= space_to_end) {
		hdmi_dma_mmap_copy(substream, offset, count);
	} else {
		hdmi_dma_mmap_copy(substream, offset, space_to_end);
		hdmi_dma_mmap_copy(substream, 0, count - space_to_end);
	}
}

static void hdmi_sdma_callback(void *data)
{
	struct hdmi_dma_priv *priv = (struct hdmi_dma_priv *)data;
	struct snd_pcm_substream *substream = priv->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);

	if (runtime && runtime->dma_area && priv->tx_active) {
		priv->offset += priv->period_bytes;
		priv->offset %= priv->period_bytes * priv->periods;

		/* Copy data by period_bytes */
		hdmi_dma_data_copy(substream, priv, 'p');

		snd_pcm_period_elapsed(substream);
	}

	spin_unlock_irqrestore(&priv->irq_lock, flags);

	return;
}

static int hdmi_dma_set_thrsld_incrtype(struct device *dev, int channels)
{
	u8 mask = HDMI_AHB_DMA_CONF0_BURST_MODE | HDMI_AHB_DMA_CONF0_INCR_TYPE_MASK;
	u8 val = hdmi_readb(HDMI_AHB_DMA_CONF0) & ~mask;
	int incr_type, threshold;

	switch (hdmi_readb(HDMI_REVISION_ID)) {
	case 0x0a:
		incr_type = HDMI_DMA_BURST_INCR4;
		if (channels == 2)
			threshold = 126;
		else
			threshold = 124;
		break;
	case 0x1a:
		incr_type = HDMI_DMA_BURST_INCR8;
		threshold = 128;
		break;
	default:
		dev_err(dev, "unknown hdmi controller!\n");
		return -ENODEV;
	}

	hdmi_writeb(threshold, HDMI_AHB_DMA_THRSLD);

	switch (incr_type) {
	case HDMI_DMA_BURST_UNSPECIFIED_LEGNTH:
		break;
	case HDMI_DMA_BURST_INCR4:
		val |= HDMI_AHB_DMA_CONF0_BURST_MODE;
		break;
	case HDMI_DMA_BURST_INCR8:
		val |= HDMI_AHB_DMA_CONF0_BURST_MODE |
			 HDMI_AHB_DMA_CONF0_INCR8;
		break;
	case HDMI_DMA_BURST_INCR16:
		val |= HDMI_AHB_DMA_CONF0_BURST_MODE |
			 HDMI_AHB_DMA_CONF0_INCR16;
		break;
	default:
		dev_err(dev, "invalid increment type: %d!", incr_type);
		return -EINVAL;
	}

	hdmi_writeb(val, HDMI_AHB_DMA_CONF0);

	hdmi_audio_debug(dev, HDMI_AHB_DMA_THRSLD);

	return 0;
}

static int hdmi_dma_configure_dma(struct device *dev, int channels)
{
	u8 i, val = 0;
	int ret;

	if (channels <= 0 || channels > 8 || channels % 2 != 0) {
		dev_err(dev, "unsupported channel number: %d\n", channels);
		return -EINVAL;
	}

	hdmi_audio_writeb(AHB_DMA_CONF0, EN_HLOCK, 0x1);

	ret = hdmi_dma_set_thrsld_incrtype(dev, channels);
	if (ret)
		return ret;

	for (i = 0; i < channels; i += 2)
		val |= 0x3 << i;

	hdmi_writeb(val, HDMI_AHB_DMA_CONF1);

	return 0;
}

static void hdmi_dma_init_iec_header(void)
{
	iec_header.U = 0;

	iec_header.B.consumer = 0;		/* Consumer use */
	iec_header.B.linear_pcm = 0;		/* linear pcm audio */
	iec_header.B.copyright = 1;		/* no copyright */
	iec_header.B.pre_emphasis = 0;		/* 2 channels without pre-emphasis */
	iec_header.B.mode = 0;			/* Mode 0 */

	iec_header.B.category_code = 0;

	iec_header.B.source = 2;		/* stereo */
	iec_header.B.channel = 0;

	iec_header.B.sample_freq = 0x02;	/* 48 KHz */
	iec_header.B.clock_acc = 0;		/* Level II */

	iec_header.B.word_length = 0x02;	/* 16 bits */
	iec_header.B.org_sample_freq = 0x0D;	/* 48 KHz */

	iec_header.B.cgms_a = 0;		/* Copying is permitted without restriction */
}

static int hdmi_dma_update_iec_header(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hdmi_dma_priv *priv = runtime->private_data;
	struct device *dev = rtd->platform->dev;

	iec_header.B.source = priv->channels;

	switch (priv->rate) {
	case 32000:
		iec_header.B.sample_freq = 0x03;
		iec_header.B.org_sample_freq = 0x0C;
		break;
	case 44100:
		iec_header.B.sample_freq = 0x00;
		iec_header.B.org_sample_freq = 0x0F;
		break;
	case 48000:
		iec_header.B.sample_freq = 0x02;
		iec_header.B.org_sample_freq = 0x0D;
		break;
	case 88200:
		iec_header.B.sample_freq = 0x08;
		iec_header.B.org_sample_freq = 0x07;
		break;
	case 96000:
		iec_header.B.sample_freq = 0x0A;
		iec_header.B.org_sample_freq = 0x05;
		break;
	case 176400:
		iec_header.B.sample_freq = 0x0C;
		iec_header.B.org_sample_freq = 0x03;
		break;
	case 192000:
		iec_header.B.sample_freq = 0x0E;
		iec_header.B.org_sample_freq = 0x01;
		break;
	default:
		dev_err(dev, "unsupported sample rate\n");
		return -EFAULT;
	}

	switch (priv->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		iec_header.B.word_length = 0x02;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iec_header.B.word_length = 0x0b;
		break;
	default:
		return -EFAULT;
	}

	return 0;
}

/*
 * The HDMI block transmits the audio data without adding any of the audio
 * frame bits.  So we have to copy the raw dma data from the ALSA buffer
 * to the DMA buffer, adding the frame information.
 */
static int hdmi_dma_copy(struct snd_pcm_substream *substream, int channel,
			snd_pcm_uframes_t pos, void __user *buf,
			snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hdmi_dma_priv *priv = runtime->private_data;
	unsigned int count = frames_to_bytes(runtime, frames);
	unsigned int pos_bytes = frames_to_bytes(runtime, pos);
	u32 *hw_buf;
	int subframe_idx;
	u32 pcm_data;

	/* Adding frame info to pcm data from userspace and copy to hw_buffer */
	hw_buf = (u32 *)(priv->hw_buffer.area + (pos_bytes * priv->buffer_ratio));

	while (count > 0) {
		for (subframe_idx = 1 ; subframe_idx <= priv->channels ; subframe_idx++) {
			if (copy_from_user(&pcm_data, buf, priv->sample_align))
				return -EFAULT;

			buf += priv->sample_align;
			count -= priv->sample_align;

			/* Save the header info to the audio dma buffer */
			*hw_buf++ = hdmi_dma_add_frame_info(priv, pcm_data, subframe_idx);
		}

		priv->frame_idx++;
		if (priv->frame_idx == 192)
			priv->frame_idx = 0;
	}

	return 0;
}

static int hdmi_sdma_initbuf(struct device *dev, struct hdmi_dma_priv *priv)
{
	struct hdmi_sdma_script *hdmi_sdma_t = priv->hdmi_sdma_t;
	u32 *head, *tail, i;

	if (!hdmi_sdma_t) {
		dev_err(dev, "hdmi private addr invalid!!!\n");
		return -EINVAL;
	}

	hdmi_sdma_t->control_reg_addr = HDMI_BASE_ADDR + HDMI_AHB_DMA_START;
	hdmi_sdma_t->status_reg_addr = HDMI_BASE_ADDR + HDMI_IH_AHBDMAAUD_STAT0;
	hdmi_sdma_t->dma_start_addr = HDMI_BASE_ADDR + HDMI_AHB_DMA_STRADDR0;

	head = &hdmi_sdma_t->buffer[0];
	tail = &hdmi_sdma_t->buffer[1];

	for (i = 0; i < priv->sdma_params.buffer_num; i++) {
		*head = priv->hw_buffer.addr + i * priv->period_bytes * priv->buffer_ratio;
		*tail = *head + priv->dma_period_bytes - 1;
		head += 2;
		tail += 2;
	}

	return 0;
}

static int hdmi_sdma_config(struct snd_pcm_substream *substream,
			struct hdmi_dma_priv *priv)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct device *dai_dev = &priv->pdev->dev;
	struct device *dev = rtd->platform->dev;
	struct dma_slave_config slave_config;
	int ret;

	priv->dma_channel = dma_request_slave_channel(dai_dev, "tx");
	if (priv->dma_channel == NULL) {
		dev_err(dev, "failed to alloc dma channel\n");
		return -EBUSY;
	}

	slave_config.direction = DMA_TRANS_NONE;
	slave_config.src_addr = (dma_addr_t)priv->sdma_params.buffer_num;
	slave_config.dst_addr = (dma_addr_t)priv->sdma_params.phyaddr;

	ret = dmaengine_slave_config(priv->dma_channel, &slave_config);
	if (ret) {
		dev_err(dev, "failed to config slave dma\n");
		return -EINVAL;
	}

	return 0;
}

static int hdmi_dma_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hdmi_dma_priv *priv = runtime->private_data;

	if (priv->dma_channel) {
		dma_release_channel(priv->dma_channel);
		priv->dma_channel = NULL;
	}

	return 0;
}

static int hdmi_dma_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hdmi_dma_priv *priv = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct device *dev = rtd->platform->dev;
	int ret;

	priv->buffer_bytes = params_buffer_bytes(params);
	priv->periods = params_periods(params);
	priv->period_bytes = params_period_bytes(params);
	priv->channels = params_channels(params);
	priv->format = params_format(params);
	priv->rate = params_rate(params);

	priv->offset = 0;
	priv->period_time = HZ / (priv->rate / params_period_size(params));

	switch (priv->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		priv->buffer_ratio = 2;
		priv->sample_align = 2;
		priv->sample_bits = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		/* 24 bit audio in 32 bit word */
		priv->buffer_ratio = 1;
		priv->sample_align = 4;
		priv->sample_bits = 24;
		break;
	default:
		dev_err(dev, "unsupported sample format: %d\n", priv->format);
		return -EINVAL;
	}

	priv->dma_period_bytes = priv->period_bytes * priv->buffer_ratio;
	priv->sdma_params.buffer_num = priv->periods;
	priv->sdma_params.phyaddr = priv->phy_hdmi_sdma_t;

	ret = hdmi_sdma_initbuf(dev, priv);
	if (ret)
		return ret;

	ret = hdmi_sdma_config(substream, priv);
	if (ret)
		return ret;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	ret = hdmi_dma_configure_dma(dev, priv->channels);
	if (ret)
		return ret;

	hdmi_dma_set_addr(priv->hw_buffer.addr, priv->dma_period_bytes);

	dumppriv(dev, priv);

	hdmi_dma_update_iec_header(substream);

	/* Init par for mmap optimizate */
	init_table(priv->channels);

	priv->appl_bytes = 0;

	return 0;
}

static void hdmi_dma_trigger_init(struct snd_pcm_substream *substream,
				struct hdmi_dma_priv *priv)
{
	unsigned long status;

	priv->offset = 0;
	priv->frame_idx = 0;

	/* Copy data by buffer_bytes */
	hdmi_dma_data_copy(substream, priv, 'b');

	hdmi_audio_writeb(AHB_DMA_CONF0, SW_FIFO_RST, 0x1);

	/* Delay after reset */
	udelay(1);

	status = hdmi_readb(HDMI_IH_AHBDMAAUD_STAT0);
	hdmi_writeb(status, HDMI_IH_AHBDMAAUD_STAT0);
}

static int hdmi_dma_prepare_and_submit(struct snd_pcm_substream *substream,
					struct hdmi_dma_priv *priv)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct device *dev = rtd->platform->dev;

	priv->desc = dmaengine_prep_dma_cyclic(priv->dma_channel, 0, 0, 0,
						DMA_TRANS_NONE, 0);
	if (!priv->desc) {
		dev_err(dev, "failed to prepare slave dma\n");
		return -EINVAL;
	}

	priv->desc->callback = hdmi_sdma_callback;
	priv->desc->callback_param = (void *)priv;
	dmaengine_submit(priv->desc);

	return 0;
}

static int hdmi_dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct hdmi_dma_priv *priv = runtime->private_data;
	struct device *dev = rtd->platform->dev;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!check_hdmi_state())
			return 0;
		hdmi_dma_trigger_init(substream, priv);

		dumpregs(dev);

		priv->tx_active = true;
		hdmi_audio_writeb(AHB_DMA_START, START, 0x1);
		hdmi_dma_irq_set(false);
		hdmi_set_dma_mode(1);
		ret = hdmi_dma_prepare_and_submit(substream, priv);
		if (ret)
			return ret;
		dma_async_issue_pending(priv->desc->chan);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dmaengine_terminate_all(priv->dma_channel);
		hdmi_set_dma_mode(0);
		hdmi_dma_irq_set(true);
		hdmi_audio_writeb(AHB_DMA_STOP, STOP, 0x1);
		priv->tx_active = false;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t hdmi_dma_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hdmi_dma_priv *priv = runtime->private_data;

	return bytes_to_frames(runtime, priv->offset);
}

static struct snd_pcm_hardware snd_imx_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME,
	.formats = MXC_HDMI_FORMATS_PLAYBACK,
	.rate_min = 32000,
	.channels_min = 2,
	.channels_max = 8,
	.buffer_bytes_max = HDMI_PCM_BUF_SIZE,
	.period_bytes_min = HDMI_DMA_PERIOD_BYTES / 2,
	.period_bytes_max = HDMI_DMA_PERIOD_BYTES / 2,
	.periods_min = 8,
	.periods_max = 8,
	.fifo_size = 0,
};

static void hdmi_dma_irq_enable(struct hdmi_dma_priv *priv)
{
	unsigned long flags;

	hdmi_writeb(0xff, HDMI_AHB_DMA_POL);
	hdmi_writeb(0xff, HDMI_AHB_DMA_BUFFPOL);

	spin_lock_irqsave(&priv->irq_lock, flags);

	hdmi_writeb(0xff, HDMI_IH_AHBDMAAUD_STAT0);
	hdmi_writeb(0xff, HDMI_IH_MUTE_AHBDMAAUD_STAT0);
	hdmi_dma_irq_set(false);
	hdmi_mask(0);

	spin_unlock_irqrestore(&priv->irq_lock, flags);
}

static void hdmi_dma_irq_disable(struct hdmi_dma_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);

	hdmi_dma_irq_set(true);
	hdmi_writeb(0x0, HDMI_IH_MUTE_AHBDMAAUD_STAT0);
	hdmi_writeb(0xff, HDMI_IH_AHBDMAAUD_STAT0);
	hdmi_mask(1);

	spin_unlock_irqrestore(&priv->irq_lock, flags);
}

static int hdmi_dma_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct device *dev = rtd->platform->dev;
	struct hdmi_dma_priv *priv = dev_get_drvdata(dev);
	int ret;

	runtime->private_data = priv;

	ret = mxc_hdmi_register_audio(substream);
	if (ret < 0) {
		dev_err(dev, "HDMI Video is not ready!\n");
		return ret;
	}

	hdmi_audio_writeb(AHB_DMA_CONF0, SW_FIFO_RST, 0x1);

	ret = snd_pcm_hw_constraint_integer(substream->runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	snd_soc_set_runtime_hwparams(substream, &snd_imx_hardware);

	hdmi_dma_irq_enable(priv);

	return 0;
}

static int hdmi_dma_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hdmi_dma_priv *priv = runtime->private_data;

	hdmi_dma_irq_disable(priv);
	mxc_hdmi_unregister_audio(substream);

	return 0;
}

static struct snd_pcm_ops imx_hdmi_dma_pcm_ops = {
	.open		= hdmi_dma_open,
	.close		= hdmi_dma_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= hdmi_dma_hw_params,
	.hw_free	= hdmi_dma_hw_free,
	.trigger	= hdmi_dma_trigger,
	.pointer	= hdmi_dma_pointer,
	.copy		= hdmi_dma_copy,
};

static int imx_hdmi_dma_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct hdmi_dma_priv *priv = dev_get_drvdata(rtd->platform->dev);
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm_substream *substream;
	struct snd_pcm *pcm = rtd->pcm;
	u64 dma_mask = DMA_BIT_MASK(32);
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &dma_mask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;

	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, pcm->card->dev,
			HDMI_PCM_BUF_SIZE, &substream->dma_buffer);
	if (ret) {
		dev_err(card->dev, "failed to alloc playback dma buffer\n");
		return ret;
	}

	priv->substream = substream;

	/* Alloc the hw_buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, pcm->card->dev,
			HDMI_DMA_BUF_SIZE, &priv->hw_buffer);
	if (ret) {
		dev_err(card->dev, "failed to alloc hw dma buffer\n");
		return ret;
	}

	return ret;
}

static void imx_hdmi_dma_pcm_free(struct snd_pcm *pcm)
{
	int stream = SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_soc_pcm_runtime *rtd = pcm->private_data;
	struct hdmi_dma_priv *priv = dev_get_drvdata(rtd->platform->dev);

	if (substream) {
		snd_dma_free_pages(&substream->dma_buffer);
		substream->dma_buffer.area = NULL;
		substream->dma_buffer.addr = 0;
	}

	/* Free the hw_buffer */
	snd_dma_free_pages(&priv->hw_buffer);
	priv->hw_buffer.area = NULL;
	priv->hw_buffer.addr = 0;
}

static struct snd_soc_platform_driver imx_hdmi_platform = {
	.ops		= &imx_hdmi_dma_pcm_ops,
	.pcm_new	= imx_hdmi_dma_pcm_new,
	.pcm_free	= imx_hdmi_dma_pcm_free,
};

static int imx_soc_platform_probe(struct platform_device *pdev)
{
	struct imx_hdmi *hdmi_drvdata = platform_get_drvdata(pdev);
	struct hdmi_dma_priv *priv;
	int ret = 0;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "Failed to alloc hdmi_dma\n");
		return -ENOMEM;
	}

	priv->hdmi_sdma_t = dma_alloc_coherent(NULL,
			sizeof(struct hdmi_sdma_script),
			&priv->phy_hdmi_sdma_t, GFP_KERNEL);
	if (!priv->hdmi_sdma_t) {
		dev_err(&pdev->dev, "Failed to alloc hdmi_sdma_t\n");
		return -ENOMEM;
	}

	priv->tx_active = false;
	spin_lock_init(&priv->irq_lock);

	priv->pdev = hdmi_drvdata->pdev;

	hdmi_dma_init_iec_header();

	dev_set_drvdata(&pdev->dev, priv);

	switch (hdmi_readb(HDMI_REVISION_ID)) {
	case 0x0a:
		snd_imx_hardware.period_bytes_max = HDMI_DMA_PERIOD_BYTES / 4;
		snd_imx_hardware.period_bytes_min = HDMI_DMA_PERIOD_BYTES / 4;
		break;
	default:
		break;
	}

	ret = snd_soc_register_platform(&pdev->dev, &imx_hdmi_platform);
	if (ret)
		goto err_plat;

	return 0;

err_plat:
	dma_free_coherent(NULL, sizeof(struct hdmi_sdma_script),
			priv->hdmi_sdma_t, priv->phy_hdmi_sdma_t);

	return ret;
}

static int imx_soc_platform_remove(struct platform_device *pdev)
{
	struct hdmi_dma_priv *priv = dev_get_drvdata(&pdev->dev);

	dma_free_coherent(NULL, sizeof(struct hdmi_sdma_script),
			priv->hdmi_sdma_t, priv->phy_hdmi_sdma_t);

	snd_soc_unregister_platform(&pdev->dev);

	return 0;
}

static struct platform_driver imx_hdmi_dma_driver = {
	.driver = {
		.name = "imx-hdmi-audio",
		.owner = THIS_MODULE,
	},
	.probe = imx_soc_platform_probe,
	.remove = imx_soc_platform_remove,
};

module_platform_driver(imx_hdmi_dma_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("i.MX HDMI audio DMA");
MODULE_LICENSE("GPL");
