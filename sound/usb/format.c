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
#include <linux/usb.h>
#include <linux/usb/audio.h>

#include <sound/core.h>
#include <sound/pcm.h>

#include "usbaudio.h"
#include "card.h"
#include "quirks.h"
#include "helper.h"
#include "debug.h"

/*
 * parse the audio format type I descriptor
 * and returns the corresponding pcm format
 *
 * @dev: usb device
 * @fp: audioformat record
 * @format: the format tag (wFormatTag)
 * @fmt: the format type descriptor
 */
static int parse_audio_format_i_type(struct snd_usb_audio *chip,
				     struct audioformat *fp,
				     int format, void *_fmt,
				     int protocol)
{
	int pcm_format, i;
	int sample_width, sample_bytes;

	switch (protocol) {
	case UAC_VERSION_1: {
		struct uac_format_type_i_discrete_descriptor *fmt = _fmt;
		sample_width = fmt->bBitResolution;
		sample_bytes = fmt->bSubframeSize;
		break;
	}

	case UAC_VERSION_2: {
		struct uac_format_type_i_ext_descriptor *fmt = _fmt;
		sample_width = fmt->bBitResolution;
		sample_bytes = fmt->bSubslotSize;

		/*
		 * FIXME
		 * USB audio class v2 devices specify a bitmap of possible
		 * audio formats rather than one fix value. For now, we just
		 * pick one of them and report that as the only possible
		 * value for this setting.
		 * The bit allocation map is in fact compatible to the
		 * wFormatTag of the v1 AS streaming descriptors, which is why
		 * we can simply map the matrix.
		 */

		for (i = 0; i < 5; i++)
			if (format & (1UL << i)) {
				format = i + 1;
				break;
			}

		break;
	}

	default:
		return -EINVAL;
	}

	/* FIXME: correct endianess and sign? */
	pcm_format = -1;

	switch (format) {
	case UAC_FORMAT_TYPE_I_UNDEFINED: /* some devices don't define this correctly... */
		snd_printdd(KERN_INFO "%d:%u:%d : format type 0 is detected, processed as PCM\n",
			    chip->dev->devnum, fp->iface, fp->altsetting);
		/* fall-through */
	case UAC_FORMAT_TYPE_I_PCM:
		if (sample_width > sample_bytes * 8) {
			snd_printk(KERN_INFO "%d:%u:%d : sample bitwidth %d in over sample bytes %d\n",
				   chip->dev->devnum, fp->iface, fp->altsetting,
				   sample_width, sample_bytes);
		}
		/* check the format byte size */
		switch (sample_bytes) {
		case 1:
			pcm_format = SNDRV_PCM_FORMAT_S8;
			break;
		case 2:
			if (snd_usb_is_big_endian_format(chip, fp))
				pcm_format = SNDRV_PCM_FORMAT_S16_BE; /* grrr, big endian!! */
			else
				pcm_format = SNDRV_PCM_FORMAT_S16_LE;
			break;
		case 3:
			if (snd_usb_is_big_endian_format(chip, fp))
				pcm_format = SNDRV_PCM_FORMAT_S24_3BE; /* grrr, big endian!! */
			else
				pcm_format = SNDRV_PCM_FORMAT_S24_3LE;
			break;
		case 4:
			pcm_format = SNDRV_PCM_FORMAT_S32_LE;
			break;
		default:
			snd_printk(KERN_INFO "%d:%u:%d : unsupported sample bitwidth %d in %d bytes\n",
				   chip->dev->devnum, fp->iface, fp->altsetting,
				   sample_width, sample_bytes);
			break;
		}
		break;
	case UAC_FORMAT_TYPE_I_PCM8:
		pcm_format = SNDRV_PCM_FORMAT_U8;

		/* Dallas DS4201 workaround: it advertises U8 format, but really
		   supports S8. */
		if (chip->usb_id == USB_ID(0x04fa, 0x4201))
			pcm_format = SNDRV_PCM_FORMAT_S8;
		break;
	case UAC_FORMAT_TYPE_I_IEEE_FLOAT:
		pcm_format = SNDRV_PCM_FORMAT_FLOAT_LE;
		break;
	case UAC_FORMAT_TYPE_I_ALAW:
		pcm_format = SNDRV_PCM_FORMAT_A_LAW;
		break;
	case UAC_FORMAT_TYPE_I_MULAW:
		pcm_format = SNDRV_PCM_FORMAT_MU_LAW;
		break;
	default:
		snd_printk(KERN_INFO "%d:%u:%d : unsupported format type %d\n",
			   chip->dev->devnum, fp->iface, fp->altsetting, format);
		break;
	}
	return pcm_format;
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
		return -1;
	}

	if (nr_rates) {
		/*
		 * build the rate table and bitmap flags
		 */
		int r, idx;

		fp->rate_table = kmalloc(sizeof(int) * nr_rates, GFP_KERNEL);
		if (fp->rate_table == NULL) {
			snd_printk(KERN_ERR "cannot malloc\n");
			return -1;
		}

		fp->nr_rates = 0;
		fp->rate_min = fp->rate_max = 0;
		for (r = 0, idx = offset + 1; r < nr_rates; r++, idx += 3) {
			unsigned int rate = combine_triple(&fmt[idx]);
			if (!rate)
				continue;
			/* C-Media CM6501 mislabels its 96 kHz altsetting */
			if (rate == 48000 && nr_rates == 1 &&
			    (chip->usb_id == USB_ID(0x0d8c, 0x0201) ||
			     chip->usb_id == USB_ID(0x0d8c, 0x0102)) &&
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
			return -1;
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
 * parse the format descriptor and stores the possible sample rates
 * on the audioformat table (audio class v2).
 */
static int parse_audio_format_rates_v2(struct snd_usb_audio *chip,
				       struct audioformat *fp,
				       struct usb_host_interface *iface)
{
	struct usb_device *dev = chip->dev;
	unsigned char tmp[2], *data;
	int i, nr_rates, data_size, ret = 0;

	/* get the number of sample rates first by only fetching 2 bytes */
	ret = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC2_CS_RANGE,
			      USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			      0x0100, chip->clock_id << 8, tmp, sizeof(tmp), 1000);

	if (ret < 0) {
		snd_printk(KERN_ERR "unable to retrieve number of sample rates\n");
		goto err;
	}

	nr_rates = (tmp[1] << 8) | tmp[0];
	data_size = 2 + 12 * nr_rates;
	data = kzalloc(data_size, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto err;
	}

	/* now get the full information */
	ret = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC2_CS_RANGE,
			       USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			       0x0100, chip->clock_id << 8, data, data_size, 1000);

	if (ret < 0) {
		snd_printk(KERN_ERR "unable to retrieve sample rate range\n");
		ret = -EINVAL;
		goto err_free;
	}

	fp->rate_table = kmalloc(sizeof(int) * nr_rates, GFP_KERNEL);
	if (!fp->rate_table) {
		ret = -ENOMEM;
		goto err_free;
	}

	fp->nr_rates = 0;
	fp->rate_min = fp->rate_max = 0;

	for (i = 0; i < nr_rates; i++) {
		int rate = combine_quad(&data[2 + 12 * i]);

		fp->rate_table[fp->nr_rates] = rate;
		if (!fp->rate_min || rate < fp->rate_min)
			fp->rate_min = rate;
		if (!fp->rate_max || rate > fp->rate_max)
			fp->rate_max = rate;
		fp->rates |= snd_pcm_rate_to_rate_bit(rate);
		fp->nr_rates++;
	}

err_free:
	kfree(data);
err:
	return ret;
}

/*
 * parse the format type I and III descriptors
 */
static int parse_audio_format_i(struct snd_usb_audio *chip,
				struct audioformat *fp,
				int format, void *_fmt,
				struct usb_host_interface *iface)
{
	struct usb_interface_descriptor *altsd = get_iface_desc(iface);
	struct uac_format_type_i_discrete_descriptor *fmt = _fmt;
	int protocol = altsd->bInterfaceProtocol;
	int pcm_format, ret;

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
	} else {
		pcm_format = parse_audio_format_i_type(chip, fp, format, fmt, protocol);
		if (pcm_format < 0)
			return -1;
	}

	fp->format = pcm_format;

	/* gather possible sample rates */
	/* audio class v1 reports possible sample rates as part of the
	 * proprietary class specific descriptor.
	 * audio class v2 uses class specific EP0 range requests for that.
	 */
	switch (protocol) {
	case UAC_VERSION_1:
		fp->channels = fmt->bNrChannels;
		ret = parse_audio_format_rates_v1(chip, fp, _fmt, 7);
		break;
	case UAC_VERSION_2:
		/* fp->channels is already set in this case */
		ret = parse_audio_format_rates_v2(chip, fp, iface);
		break;
	}

	if (fp->channels < 1) {
		snd_printk(KERN_ERR "%d:%u:%d : invalid channels %d\n",
			   chip->dev->devnum, fp->iface, fp->altsetting, fp->channels);
		return -1;
	}

	return ret;
}

/*
 * parse the format type II descriptor
 */
static int parse_audio_format_ii(struct snd_usb_audio *chip,
				 struct audioformat *fp,
				 int format, void *_fmt,
				 struct usb_host_interface *iface)
{
	int brate, framesize, ret;
	struct usb_interface_descriptor *altsd = get_iface_desc(iface);
	int protocol = altsd->bInterfaceProtocol;

	switch (format) {
	case UAC_FORMAT_TYPE_II_AC3:
		/* FIXME: there is no AC3 format defined yet */
		// fp->format = SNDRV_PCM_FORMAT_AC3;
		fp->format = SNDRV_PCM_FORMAT_U8; /* temporarily hack to receive byte streams */
		break;
	case UAC_FORMAT_TYPE_II_MPEG:
		fp->format = SNDRV_PCM_FORMAT_MPEG;
		break;
	default:
		snd_printd(KERN_INFO "%d:%u:%d : unknown format tag %#x is detected.  processed as MPEG.\n",
			   chip->dev->devnum, fp->iface, fp->altsetting, format);
		fp->format = SNDRV_PCM_FORMAT_MPEG;
		break;
	}

	fp->channels = 1;

	switch (protocol) {
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
		ret = parse_audio_format_rates_v2(chip, fp, iface);
		break;
	}
	}

	return ret;
}

int snd_usb_parse_audio_format(struct snd_usb_audio *chip, struct audioformat *fp,
		       int format, unsigned char *fmt, int stream,
		       struct usb_host_interface *iface)
{
	int err;

	switch (fmt[3]) {
	case UAC_FORMAT_TYPE_I:
	case UAC_FORMAT_TYPE_III:
		err = parse_audio_format_i(chip, fp, format, fmt, iface);
		break;
	case UAC_FORMAT_TYPE_II:
		err = parse_audio_format_ii(chip, fp, format, fmt, iface);
		break;
	default:
		snd_printd(KERN_INFO "%d:%u:%d : format type %d is not supported yet\n",
			   chip->dev->devnum, fp->iface, fp->altsetting, fmt[3]);
		return -1;
	}
	fp->fmt_type = fmt[3];
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
		if (fmt[3] == UAC_FORMAT_TYPE_I &&
		    fp->rates != SNDRV_PCM_RATE_48000 &&
		    fp->rates != SNDRV_PCM_RATE_96000)
			return -1;
	}
#endif
	return 0;
}

