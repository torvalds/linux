/*
 *  Mu-Law conversion Plug-In Interface
 *  Copyright (c) 1999 by Jaroslav Kysela <perex@perex.cz>
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

typedef void (*mulaw_f)(struct snd_pcm_plugin *plugin,
			const struct snd_pcm_plugin_channel *src_channels,
			struct snd_pcm_plugin_channel *dst_channels,
			snd_pcm_uframes_t frames);

struct mulaw_priv {
	mulaw_f func;
	int cvt_endian;			/* need endian conversion? */
	unsigned int native_ofs;	/* byte offset in native format */
	unsigned int copy_ofs;		/* byte offset in s16 format */
	unsigned int native_bytes;	/* byte size of the native format */
	unsigned int copy_bytes;	/* bytes to copy per conversion */
	u16 flip; /* MSB flip for signedness, done after endian conversion */
};

static inline void cvt_s16_to_native(struct mulaw_priv *data,
				     unsigned char *dst, u16 sample)
{
	sample ^= data->flip;
	if (data->cvt_endian)
		sample = swab16(sample);
	if (data->native_bytes > data->copy_bytes)
		memset(dst, 0, data->native_bytes);
	memcpy(dst + data->native_ofs, (char *)&sample + data->copy_ofs,
	       data->copy_bytes);
}

static void mulaw_decode(struct snd_pcm_plugin *plugin,
			const struct snd_pcm_plugin_channel *src_channels,
			struct snd_pcm_plugin_channel *dst_channels,
			snd_pcm_uframes_t frames)
{
	struct mulaw_priv *data = (struct mulaw_priv *)plugin->extra_data;
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
			cvt_s16_to_native(data, dst, sample);
			src += src_step;
			dst += dst_step;
		}
	}
}

static inline signed short cvt_native_to_s16(struct mulaw_priv *data,
					     unsigned char *src)
{
	u16 sample = 0;
	memcpy((char *)&sample + data->copy_ofs, src + data->native_ofs,
	       data->copy_bytes);
	if (data->cvt_endian)
		sample = swab16(sample);
	sample ^= data->flip;
	return (signed short)sample;
}

static void mulaw_encode(struct snd_pcm_plugin *plugin,
			const struct snd_pcm_plugin_channel *src_channels,
			struct snd_pcm_plugin_channel *dst_channels,
			snd_pcm_uframes_t frames)
{
	struct mulaw_priv *data = (struct mulaw_priv *)plugin->extra_data;
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
			signed short sample = cvt_native_to_s16(data, src);
			*dst = linear2ulaw(sample);
			src += src_step;
			dst += dst_step;
		}
	}
}

static snd_pcm_sframes_t mulaw_transfer(struct snd_pcm_plugin *plugin,
			      const struct snd_pcm_plugin_channel *src_channels,
			      struct snd_pcm_plugin_channel *dst_channels,
			      snd_pcm_uframes_t frames)
{
	struct mulaw_priv *data;

	if (snd_BUG_ON(!plugin || !src_channels || !dst_channels))
		return -ENXIO;
	if (frames == 0)
		return 0;
#ifdef CONFIG_SND_DEBUG
	{
		unsigned int channel;
		for (channel = 0; channel < plugin->src_format.channels; channel++) {
			if (snd_BUG_ON(src_channels[channel].area.first % 8 ||
				       src_channels[channel].area.step % 8))
				return -ENXIO;
			if (snd_BUG_ON(dst_channels[channel].area.first % 8 ||
				       dst_channels[channel].area.step % 8))
				return -ENXIO;
		}
	}
#endif
	if (frames > dst_channels[0].frames)
		frames = dst_channels[0].frames;
	data = (struct mulaw_priv *)plugin->extra_data;
	data->func(plugin, src_channels, dst_channels, frames);
	return frames;
}

static void init_data(struct mulaw_priv *data, snd_pcm_format_t format)
{
#ifdef SNDRV_LITTLE_ENDIAN
	data->cvt_endian = snd_pcm_format_big_endian(format) > 0;
#else
	data->cvt_endian = snd_pcm_format_little_endian(format) > 0;
#endif
	if (!snd_pcm_format_signed(format))
		data->flip = 0x8000;
	data->native_bytes = snd_pcm_format_physical_width(format) / 8;
	data->copy_bytes = data->native_bytes < 2 ? 1 : 2;
	if (snd_pcm_format_little_endian(format)) {
		data->native_ofs = data->native_bytes - data->copy_bytes;
		data->copy_ofs = 2 - data->copy_bytes;
	} else {
		/* S24 in 4bytes need an 1 byte offset */
		data->native_ofs = data->native_bytes -
			snd_pcm_format_width(format) / 8;
	}
}

int snd_pcm_plugin_build_mulaw(struct snd_pcm_substream *plug,
			       struct snd_pcm_plugin_format *src_format,
			       struct snd_pcm_plugin_format *dst_format,
			       struct snd_pcm_plugin **r_plugin)
{
	int err;
	struct mulaw_priv *data;
	struct snd_pcm_plugin *plugin;
	struct snd_pcm_plugin_format *format;
	mulaw_f func;

	if (snd_BUG_ON(!r_plugin))
		return -ENXIO;
	*r_plugin = NULL;

	if (snd_BUG_ON(src_format->rate != dst_format->rate))
		return -ENXIO;
	if (snd_BUG_ON(src_format->channels != dst_format->channels))
		return -ENXIO;

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
	if (snd_BUG_ON(!snd_pcm_format_linear(format->format)))
		return -ENXIO;

	err = snd_pcm_plugin_build(plug, "Mu-Law<->linear conversion",
				   src_format, dst_format,
				   sizeof(struct mulaw_priv), &plugin);
	if (err < 0)
		return err;
	data = (struct mulaw_priv *)plugin->extra_data;
	data->func = func;
	init_data(data, format->format);
	plugin->transfer = mulaw_transfer;
	*r_plugin = plugin;
	return 0;
}
