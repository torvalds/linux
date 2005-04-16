/*
 *  Mu-Law conversion Plug-In Interface
 *  Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>
 *                        Uros Bizjak <uros@kss-loka.si>
 *
 *  Based on reference implementation by Sun Microsystems, Inc.
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
  
#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "pcm_plugin.h"

#define	SIGN_BIT	(0x80)		/* Sign bit for a u-law byte. */
#define	QUANT_MASK	(0xf)		/* Quantization field mask. */
#define	NSEGS		(8)		/* Number of u-law segments. */
#define	SEG_SHIFT	(4)		/* Left shift for segment number. */
#define	SEG_MASK	(0x70)		/* Segment field mask. */

static inline int val_seg(int val)
{
	int r = 0;
	val >>= 7;
	if (val & 0xf0) {
		val >>= 4;
		r += 4;
	}
	if (val & 0x0c) {
		val >>= 2;
		r += 2;
	}
	if (val & 0x02)
		r += 1;
	return r;
}

#define	BIAS		(0x84)		/* Bias for linear code. */

/*
 * linear2ulaw() - Convert a linear PCM value to u-law
 *
 * In order to simplify the encoding process, the original linear magnitude
 * is biased by adding 33 which shifts the encoding range from (0 - 8158) to
 * (33 - 8191). The result can be seen in the following encoding table:
 *
 *	Biased Linear Input Code	Compressed Code
 *	------------------------	---------------
 *	00000001wxyza			000wxyz
 *	0000001wxyzab			001wxyz
 *	000001wxyzabc			010wxyz
 *	00001wxyzabcd			011wxyz
 *	0001wxyzabcde			100wxyz
 *	001wxyzabcdef			101wxyz
 *	01wxyzabcdefg			110wxyz
 *	1wxyzabcdefgh			111wxyz
 *
 * Each biased linear code has a leading 1 which identifies the segment
 * number. The value of the segment number is equal to 7 minus the number
 * of leading 0's. The quantization interval is directly available as the
 * four bits wxyz.  * The trailing bits (a - h) are ignored.
 *
 * Ordinarily the complement of the resulting code word is used for
 * transmission, and so the code word is complemented before it is returned.
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */
static unsigned char linear2ulaw(int pcm_val)	/* 2's complement (16-bit range) */
{
	int mask;
	int seg;
	unsigned char uval;

	/* Get the sign and the magnitude of the value. */
	if (pcm_val < 0) {
		pcm_val = BIAS - pcm_val;
		mask = 0x7F;
	} else {
		pcm_val += BIAS;
		mask = 0xFF;
	}
	if (pcm_val > 0x7FFF)
		pcm_val = 0x7FFF;

	/* Convert the scaled magnitude to segment number. */
	seg = val_seg(pcm_val);

	/*
	 * Combine the sign, segment, quantization bits;
	 * and complement the code word.
	 */
	uval = (seg << 4) | ((pcm_val >> (seg + 3)) & 0xF);
	return uval ^ mask;
}

/*
 * ulaw2linear() - Convert a u-law value to 16-bit linear PCM
 *
 * First, a biased linear code is derived from the code word. An unbiased
 * output can then be obtained by subtracting 33 from the biased code.
 *
 * Note that this function expects to be passed the complement of the
 * original code word. This is in keeping with ISDN conventions.
 */
static int ulaw2linear(unsigned char u_val)
{
	int t;

	/* Complement to obtain normal u-law value. */
	u_val = ~u_val;

	/*
	 * Extract and bias the quantization bits. Then
	 * shift up by the segment number and subtract out the bias.
	 */
	t = ((u_val & QUANT_MASK) << 3) + BIAS;
	t <<= ((unsigned)u_val & SEG_MASK) >> SEG_SHIFT;

	return ((u_val & SIGN_BIT) ? (BIAS - t) : (t - BIAS));
}

/*
 *  Basic Mu-Law plugin
 */

typedef void (*mulaw_f)(snd_pcm_plugin_t *plugin,
			const snd_pcm_plugin_channel_t *src_channels,
			snd_pcm_plugin_channel_t *dst_channels,
			snd_pcm_uframes_t frames);

typedef struct mulaw_private_data {
	mulaw_f func;
	int conv;
} mulaw_t;

static void mulaw_decode(snd_pcm_plugin_t *plugin,
			const snd_pcm_plugin_channel_t *src_channels,
			snd_pcm_plugin_channel_t *dst_channels,
			snd_pcm_uframes_t frames)
{
#define PUT_S16_LABELS
#include "plugin_ops.h"
#undef PUT_S16_LABELS
	mulaw_t *data = (mulaw_t *)plugin->extra_data;
	void *put = put_s16_labels[data->conv];
	int channel;
	int nchannels = plugin->src_format.channels;
	for (channel = 0; channel < nchannels; ++channel) {
		char *src;
		char *dst;
		int src_step, dst_step;
		snd_pcm_uframes_t frames1;
		if (!src_channels[channel].enabled) {
			if (dst_channels[channel].wanted)
				snd_pcm_area_silence(&dst_channels[channel].area, 0, frames, plugin->dst_format.format);
			dst_channels[channel].enabled = 0;
			continue;
		}
		dst_channels[channel].enabled = 1;
		src = src_channels[channel].area.addr + src_channels[channel].area.first / 8;
		dst = dst_channels[channel].area.addr + dst_channels[channel].area.first / 8;
		src_step = src_channels[channel].area.step / 8;
		dst_step = dst_channels[channel].area.step / 8;
		frames1 = frames;
		while (frames1-- > 0) {
			signed short sample = ulaw2linear(*src);
			goto *put;
#define PUT_S16_END after
#include "plugin_ops.h"
#undef PUT_S16_END
		after:
			src += src_step;
			dst += dst_step;
		}
	}
}

static void mulaw_encode(snd_pcm_plugin_t *plugin,
			const snd_pcm_plugin_channel_t *src_channels,
			snd_pcm_plugin_channel_t *dst_channels,
			snd_pcm_uframes_t frames)
{
#define GET_S16_LABELS
#include "plugin_ops.h"
#undef GET_S16_LABELS
	mulaw_t *data = (mulaw_t *)plugin->extra_data;
	void *get = get_s16_labels[data->conv];
	int channel;
	int nchannels = plugin->src_format.channels;
	signed short sample = 0;
	for (channel = 0; channel < nchannels; ++channel) {
		char *src;
		char *dst;
		int src_step, dst_step;
		snd_pcm_uframes_t frames1;
		if (!src_channels[channel].enabled) {
			if (dst_channels[channel].wanted)
				snd_pcm_area_silence(&dst_channels[channel].area, 0, frames, plugin->dst_format.format);
			dst_channels[channel].enabled = 0;
			continue;
		}
		dst_channels[channel].enabled = 1;
		src = src_channels[channel].area.addr + src_channels[channel].area.first / 8;
		dst = dst_channels[channel].area.addr + dst_channels[channel].area.first / 8;
		src_step = src_channels[channel].area.step / 8;
		dst_step = dst_channels[channel].area.step / 8;
		frames1 = frames;
		while (frames1-- > 0) {
			goto *get;
#define GET_S16_END after
#include "plugin_ops.h"
#undef GET_S16_END
		after:
			*dst = linear2ulaw(sample);
			src += src_step;
			dst += dst_step;
		}
	}
}

static snd_pcm_sframes_t mulaw_transfer(snd_pcm_plugin_t *plugin,
			      const snd_pcm_plugin_channel_t *src_channels,
			      snd_pcm_plugin_channel_t *dst_channels,
			      snd_pcm_uframes_t frames)
{
	mulaw_t *data;

	snd_assert(plugin != NULL && src_channels != NULL && dst_channels != NULL, return -ENXIO);
	if (frames == 0)
		return 0;
#ifdef CONFIG_SND_DEBUG
	{
		unsigned int channel;
		for (channel = 0; channel < plugin->src_format.channels; channel++) {
			snd_assert(src_channels[channel].area.first % 8 == 0 &&
				   src_channels[channel].area.step % 8 == 0,
				   return -ENXIO);
			snd_assert(dst_channels[channel].area.first % 8 == 0 &&
				   dst_channels[channel].area.step % 8 == 0,
				   return -ENXIO);
		}
	}
#endif
	data = (mulaw_t *)plugin->extra_data;
	data->func(plugin, src_channels, dst_channels, frames);
	return frames;
}

int snd_pcm_plugin_build_mulaw(snd_pcm_plug_t *plug,
			       snd_pcm_plugin_format_t *src_format,
			       snd_pcm_plugin_format_t *dst_format,
			       snd_pcm_plugin_t **r_plugin)
{
	int err;
	mulaw_t *data;
	snd_pcm_plugin_t *plugin;
	snd_pcm_plugin_format_t *format;
	mulaw_f func;

	snd_assert(r_plugin != NULL, return -ENXIO);
	*r_plugin = NULL;

	snd_assert(src_format->rate == dst_format->rate, return -ENXIO);
	snd_assert(src_format->channels == dst_format->channels, return -ENXIO);

	if (dst_format->format == SNDRV_PCM_FORMAT_MU_LAW) {
		format = src_format;
		func = mulaw_encode;
	}
	else if (src_format->format == SNDRV_PCM_FORMAT_MU_LAW) {
		format = dst_format;
		func = mulaw_decode;
	}
	else {
		snd_BUG();
		return -EINVAL;
	}
	snd_assert(snd_pcm_format_linear(format->format) != 0, return -ENXIO);

	err = snd_pcm_plugin_build(plug, "Mu-Law<->linear conversion",
				   src_format, dst_format,
				   sizeof(mulaw_t), &plugin);
	if (err < 0)
		return err;
	data = (mulaw_t*)plugin->extra_data;
	data->func = func;
	data->conv = getput_index(format->format);
	snd_assert(data->conv >= 0 && data->conv < 4*2*2, return -EINVAL);
	plugin->transfer = mulaw_transfer;
	*r_plugin = plugin;
	return 0;
}
