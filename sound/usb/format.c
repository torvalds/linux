/*
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
 *
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>

#include <sound/core.h>
#include <sound/pcm.h>

#include "usbaudio.h"
#include "card.h"
#include "quirks.h"
#include "helper.h"
#include "debug.h"
#include "clock.h"
#include "format.h"

/*
 * parse the audio format type I descriptor
 * and returns the corresponding pcm format
 *
 * @dev: usb device
 * @fp: audioformat record
 * @format: the format tag (wFormatTag)
 * @fmt: the format type descriptor
 */
static u64 parse_audio_format_i_type(struct snd_usb_audio *chip,
				     struct audioformat *fp,
				     unsigned int format, void *_fmt)
{
	int sample_width, sample_bytes;
	u64 pcm_formats = 0;

	switch (fp->protocol) {
	case UAC_VERSION_1:
	default: {
		struct uac_format_type_i_discrete_descriptor *fmt = _fmt;
		sample_width = fmt->bBitResolution;
		sample_bytes = fmt->bSubframeSize;
		format = 1 << format;
		break;
	}

	case UAC_VERSION_2: {
		struct uac_format_type_i_ext_descriptor *fmt = _fmt;
		sample_width = fmt->bBitResolution;
		sample_bytes = fmt->bSubslotSize;

		if (format & UAC2_FORMAT_TYPE_I_RAW_DATA)
			pcm_formats |= SNDRV_PCM_FMTBIT_SPECIAL;

		format <<= 1;
		break;
	}
	}

	if ((pcm_formats == 0) &&
	    (format == 0 || format == (1 << UAC_FORMAT_TYPE_I_UNDEFINED))) {
		/* some devices don't define this correctly... */
		snd_printdd(KERN_INFO "%d:%u:%d : format type 0 is detected, processed as PCM\n",
			    chip->dev->devnum, fp->iface, fp->altsetting);
		format = 1 << UAC_FORMAT_TYPE_I_PCM;
	}
	if (format & (1 << UAC_FORMAT_TYPE_I_PCM)) {
		if (chip->usb_id == USB_ID(0x0582, 0x0016) /* Edirol SD-90 */ &&
		    sample_width == 24 && sample_bytes == 2)
			sample_bytes = 3;
		else if (sample_width > sample_bytes * 8) {
			snd_printk(KERN_INFO "%d:%u:%d : sample bitwidth %d in over sample bytes %d\n",
				   chip->dev->devnum, fp->iface, fp->altsetting,
				   sample_width, sample_bytes);
		}
		/* check the format byte size */
		switch (sample_bytes) {
		case 1:
			pcm_formats |= SNDRV_PCM_FMTBIT_S8;
			break;
		case 2:
			if (snd_usb_is_big_endian_format(chip, fp))
				pcm_formats |= SNDRV_PCM_FMTBIT_S16_BE; /* grrr, big endian!! */
			else
				pcm_formats |= SNDRV_PCM_FMTBIT_S16_LE;
			break;
		case 3:
			if (snd_usb_is_big_endian_format(chip, fp))
				pcm_formats |= SNDRV_PCM_FMTBIT_S24_3BE; /* grrr, big endian!! */
			else
				pcm_formats |= SNDRV_PCM_FMTBIT_S24_3LE;
			break;
		case 4:
			pcm_formats |= SNDRV_PCM_FMTBIT_S32_LE;
			break;
		default:
			snd_printk(KERN_INFO "%d:%u:%d : unsupported sample bitwidth %d in %d bytes\n",
				   chip->dev->devnum, fp->iface, fp->altsetting,
				   sample_width, sample_bytes);
			break;
		}
	}
	if (format & (1 << UAC_FORMAT_TYPE_I_PCM8)) {
		/* Dallas DS4201 workaround: it advertises U8 format, but really
		   supports S8. */
		if (chip->usb_id == USB_ID(0x04fa, 0x4201))
			pcm_formats |= SNDRV_PCM_FMTBIT_S8;
		else
			pcm_formats |= SNDRV_PCM_FMTBIT_U8;
	}
	if (format & (1 << UAC_FORMAT_TYPE_I_IEEE_FLOAT)) {
		pcm_formats |= SNDRV_PCM_FMTBIT_FLOAT_LE;
	}
	if (format & (1 << UAC_FORMAT_TYPE_I_ALAW)) {
		pcm_formats |= SNDRV_PCM_FMTBIT_A_LAW;
	}
	if (format & (1 << UAC_FORMAT_TYPE_I_MULAW)) {
		pcm_formats |= SNDRV_PCM_FMTBIT_MU_LAW;
	}
	if (format & ~0x3f) {
		snd_printk(KERN_INFO "%d:%u:%d : unsupported format bits %#x\n",
			   chip->dev->devnum, fp->iface, fp->altsetting, format);
	}

	pcm_formats |= snd_usb_interface_dsd_format_quirks(chip, fp, sample_bytes);

	return pcm_formats;
}


/*
 * parse the format descriptor and stores the possible sample rates
 * on the audioformat table (audio class v1).
 *
 * @dev: usb device
 * @fp: audioformat record
 * @fmt: the format descriptor
 * @offset: the start offset of descriptor pointing the rate type
 *          (7 for type I and II, 8 for type II)
 */
static int parse_audio_format_rates_v1(struct snd_usb_audio *chip, struct audioformat *fp,
				       unsigned char *fmt, int offset)
{
	int nr_rates = fmt[offset];

	if (fmt[0] < offset + 1 + 3 * (nr_rates ? nr_rates : 2)) {
		snd_printk(KERN_ERR "%d:%u:%d : invalid UAC_FORMAT_TYPE desc\n",
				   chip->dev->devnum, fp->iface, fp->altsetting);
		return -EINVAL;
	}

	if (nr_rates) {
		/*
		 * build the rate table and bitmap flags
		 */
		int r, idx;

		fp->rate_table = kmalloc(sizeof(int) * nr_rates, GFP_KERNEL);
		if (fp->rate_table == NULL) {
			snd_printk(KERN_ERR "cannot malloc\n");
			return -ENOMEM;
		}

		fp->nr_rates = 0;
		fp->rate_min = fp->rate_max = 0;
		for (r = 0, idx = offset + 1; r < nr_rates; r++, idx += 3) {
			unsigned int rate = combine_triple(&fmt[idx]);
			if (!rate)
				continue;
			/* C-Media CM6501 mislabels its 96 kHz altsetting */
			/* Terratec Aureon 7.1 USB C-Media 6206, too */
			if (rate == 48000 && nr_rates == 1 &&
			    (chip->usb_id == USB_ID(0x0d8c, 0x0201) ||
			     chip->usb_id == USB_ID(0x0d8c, 0x0102) ||
			     chip->usb_id == USB_ID(0x0ccd, 0x00b1)) &&
			    fp->altsetting == 5 && fp->maxpacksize == 392)
				rate = 96000;
			/* Creative VF0470 Live Cam reports 16 kHz instead of 8kHz */
			if (rate == 16000 && chip->usb_id == USB_ID(0x041e, 0x4068))
				rate = 8000;

			fp->rate_table[fp->nr_rates] = rate;
			if (!fp->rate_min || rate < fp->rate_min)
				fp->rate_min = rate;
			if (!fp->rate_max || rate > fp->rate_max)
				fp->rate_max = rate;
			fp->rates |= snd_pcm_rate_to_rate_bit(rate);
			fp->nr_rates++;
		}
		if (!fp->nr_rates) {
			hwc_debug("All rates were zero. Skipping format!\n");
			return -EINVAL;
		}
	} else {
		/* continuous rates */
		fp->rates = SNDRV_PCM_RATE_CONTINUOUS;
		fp->rate_min = combine_triple(&fmt[offset + 1]);
		fp->rate_max = combine_triple(&fmt[offset + 4]);
	}
	return 0;
}

/*
 * Helper function to walk the array of sample rate triplets reported by
 * the device. The problem is that we need to parse whole array first to
 * get to know how many sample rates we have to expect.
 * Then fp->rate_table can be allocated and filled.
 */
static int parse_uac2_sample_rate_range(struct audioformat *fp, int nr_triplets,
					const unsigned char *data)
{
	int i, nr_rates = 0;

	fp->rates = fp->rate_min = fp->rate_max = 0;

	for (i = 0; i < nr_triplets; i++) {
		int min = combine_quad(&data[2 + 12 * i]);
		int max = combine_quad(&data[6 + 12 * i]);
		int res = combine_quad(&data[10 + 12 * i]);
		unsigned int rate;

		if ((max < 0) || (min < 0) || (res < 0) || (max < min))
			continue;

		/*
		 * for ranges with res == 1, we announce a continuous sample
		 * rate range, and this function should return 0 for no further
		 * parsing.
		 */
		if (res == 1) {
			fp->rate_min = min;
			fp->rate_max = max;
			fp->rates = SNDRV_PCM_RATE_CONTINUOUS;
			return 0;
		}

		for (rate = min; rate <= max; rate += res) {
			if (fp->rate_table)
				fp->rate_table[nr_rates] = rate;
			if (!fp->rate_min || rate < fp->rate_min)
				fp->rate_min = rate;
			if (!fp->rate_max || rate > fp->rate_max)
				fp->rate_max = rate;
			fp->rates |= snd_pcm_rate_to_rate_bit(rate);

			nr_rates++;
			if (nr_rates >= MAX_NR_RATES) {
				snd_printk(KERN_ERR "invalid uac2 rates\n");
				break;
			}

			/* avoid endless loop */
			if (res == 0)
				break;
		}
	}

	return nr_rates;
}

/*
 * parse the format descriptor and stores the possible sample rates
 * on the audioformat table (audio class v2).
 */
static int parse_audio_format_rates_v2(struct snd_usb_audio *chip,
				       struct audioformat *fp)
{
	struct usb_device *dev = chip->dev;
	unsigned char tmp[2], *data;
	int nr_triplets, data_size, ret = 0;
	int clock = snd_usb_clock_find_source(chip, fp->clock, false);

	if (clock < 0) {
		snd_printk(KERN_ERR "%s(): unable to find clock source (clock %d)\n",
				__func__, clock);
		goto err;
	}

	/* get the number of sample rates first by only fetching 2 bytes */
	ret = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC2_CS_RANGE,
			      USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			      UAC2_CS_CONTROL_SAM_FREQ << 8,
			      snd_usb_ctrl_intf(chip) | (clock << 8),
			      tmp, sizeof(tmp));

	if (ret < 0) {
		snd_printk(KERN_ERR "%s(): unable to retrieve number of sample rates (clock %d)\n",
				__func__, clock);
		goto err;
	}

	nr_triplets = (tmp[1] << 8) | tmp[0];
	data_size = 2 + 12 * nr_triplets;
	data = kzalloc(data_size, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto err;
	}

	/* now get the full information */
	ret = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC2_CS_RANGE,
			      USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			      UAC2_CS_CONTROL_SAM_FREQ << 8,
			      snd_usb_ctrl_intf(chip) | (clock << 8),
			      data, data_size);

	if (ret < 0) {
		snd_printk(KERN_ERR "%s(): unable to retrieve sample rate range (clock %d)\n",
				__func__, clock);
		ret = -EINVAL;
		goto err_free;
	}

	/* Call the triplet parser, and make sure fp->rate_table is NULL.
	 * We just use the return value to know how many sample rates we
	 * will have to deal with. */
	kfree(fp->rate_table);
	fp->rate_table = NULL;
	fp->nr_rates = parse_uac2_sample_rate_range(fp, nr_triplets, data);

	if (fp->nr_rates == 0) {
		/* SNDRV_PCM_RATE_CONTINUOUS */
		ret = 0;
		goto err_free;
	}

	fp->rate_table = kmalloc(sizeof(int) * fp->nr_rates, GFP_KERNEL);
	if (!fp->rate_table) {
		ret = -ENOMEM;
		goto err_free;
	}

	/* Call the triplet parser again, but this time, fp->rate_table is
	 * allocated, so the rates will be stored */
	parse_uac2_sample_rate_range(fp, nr_triplets, data);

err_free:
	kfree(data);
err:
	return ret;
}

/*
 * parse the format type I and III descriptors
 */
static int parse_audio_format_i(struct snd_usb_audio *chip,
				struct audioformat *fp, unsigned int format,
				struct uac_format_type_i_continuous_descriptor *fmt)
{
	snd_pcm_format_t pcm_format;
	int ret;

	if (fmt->bFormatType == UAC_FORMAT_TYPE_III) {
		/* FIXME: the format type is really IECxxx
		 *        but we give normal PCM format to get the existing
		 *        apps working...
		 */
		switch (chip->usb_id) {

		case USB_ID(0x0763, 0x2003): /* M-Audio Audiophile USB */
			if (chip->setup == 0x00 && 
			    fp->altsetting == 6)
				pcm_format = SNDRV_PCM_FORMAT_S16_BE;
			else
				pcm_format = SNDRV_PCM_FORMAT_S16_LE;
			break;
		default:
			pcm_format = SNDRV_PCM_FORMAT_S16_LE;
		}
		fp->formats = pcm_format_to_bits(pcm_format);
	} else {
		fp->formats = parse_audio_format_i_type(chip, fp, format, fmt);
		if (!fp->formats)
			return -EINVAL;
	}

	/* gather possible sample rates */
	/* audio class v1 reports possible sample rates as part of the
	 * proprietary class specific descriptor.
	 * audio class v2 uses class specific EP0 range requests for that.
	 */
	switch (fp->protocol) {
	default:
	case UAC_VERSION_1:
		fp->channels = fmt->bNrChannels;
		ret = parse_audio_format_rates_v1(chip, fp, (unsigned char *) fmt, 7);
		break;
	case UAC_VERSION_2:
		/* fp->channels is already set in this case */
		ret = parse_audio_format_rates_v2(chip, fp);
		break;
	}

	if (fp->channels < 1) {
		snd_printk(KERN_ERR "%d:%u:%d : invalid channels %d\n",
			   chip->dev->devnum, fp->iface, fp->altsetting, fp->channels);
		return -EINVAL;
	}

	return ret;
}

/*
 * parse the format type II descriptor
 */
static int parse_audio_format_ii(struct snd_usb_audio *chip,
				 struct audioformat *fp,
				 int format, void *_fmt)
{
	int brate, framesize, ret;

	switch (format) {
	case UAC_FORMAT_TYPE_II_AC3:
		/* FIXME: there is no AC3 format defined yet */
		// fp->formats = SNDRV_PCM_FMTBIT_AC3;
		fp->formats = SNDRV_PCM_FMTBIT_U8; /* temporary hack to receive byte streams */
		break;
	case UAC_FORMAT_TYPE_II_MPEG:
		fp->formats = SNDRV_PCM_FMTBIT_MPEG;
		break;
	default:
		snd_printd(KERN_INFO "%d:%u:%d : unknown format tag %#x is detected.  processed as MPEG.\n",
			   chip->dev->devnum, fp->iface, fp->altsetting, format);
		fp->formats = SNDRV_PCM_FMTBIT_MPEG;
		break;
	}

	fp->channels = 1;

	switch (fp->protocol) {
	default:
	case UAC_VERSION_1: {
		struct uac_format_type_ii_discrete_descriptor *fmt = _fmt;
		brate = le16_to_cpu(fmt->wMaxBitRate);
		framesize = le16_to_cpu(fmt->wSamplesPerFrame);
		snd_printd(KERN_INFO "found format II with max.bitrate = %d, frame size=%d\n", brate, framesize);
		fp->frame_size = framesize;
		ret = parse_audio_format_rates_v1(chip, fp, _fmt, 8); /* fmt[8..] sample rates */
		break;
	}
	case UAC_VERSION_2: {
		struct uac_format_type_ii_ext_descriptor *fmt = _fmt;
		brate = le16_to_cpu(fmt->wMaxBitRate);
		framesize = le16_to_cpu(fmt->wSamplesPerFrame);
		snd_printd(KERN_INFO "found format II with max.bitrate = %d, frame size=%d\n", brate, framesize);
		fp->frame_size = framesize;
		ret = parse_audio_format_rates_v2(chip, fp);
		break;
	}
	}

	return ret;
}

int snd_usb_parse_audio_format(struct snd_usb_audio *chip,
			       struct audioformat *fp, unsigned int format,
			       struct uac_format_type_i_continuous_descriptor *fmt,
			       int stream)
{
	int err;

	switch (fmt->bFormatType) {
	case UAC_FORMAT_TYPE_I:
	case UAC_FORMAT_TYPE_III:
		err = parse_audio_format_i(chip, fp, format, fmt);
		break;
	case UAC_FORMAT_TYPE_II:
		err = parse_audio_format_ii(chip, fp, format, fmt);
		break;
	default:
		snd_printd(KERN_INFO "%d:%u:%d : format type %d is not supported yet\n",
			   chip->dev->devnum, fp->iface, fp->altsetting,
			   fmt->bFormatType);
		return -ENOTSUPP;
	}
	fp->fmt_type = fmt->bFormatType;
	if (err < 0)
		return err;
#if 1
	/* FIXME: temporary hack for extigy/audigy 2 nx/zs */
	/* extigy apparently supports sample rates other than 48k
	 * but not in ordinary way.  so we enable only 48k atm.
	 */
	if (chip->usb_id == USB_ID(0x041e, 0x3000) ||
	    chip->usb_id == USB_ID(0x041e, 0x3020) ||
	    chip->usb_id == USB_ID(0x041e, 0x3061)) {
		if (fmt->bFormatType == UAC_FORMAT_TYPE_I &&
		    fp->rates != SNDRV_PCM_RATE_48000 &&
		    fp->rates != SNDRV_PCM_RATE_96000)
			return -ENOTSUPP;
	}
#endif
	return 0;
}

