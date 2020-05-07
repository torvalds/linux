// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/usb/audio-v3.h>

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
 * @fmt: the format type descriptor (v1/v2) or AudioStreaming descriptor (v3)
 */
static u64 parse_audio_format_i_type(struct snd_usb_audio *chip,
				     struct audioformat *fp,
				     u64 format, void *_fmt)
{
	int sample_width, sample_bytes;
	u64 pcm_formats = 0;

	switch (fp->protocol) {
	case UAC_VERSION_1:
	default: {
		struct uac_format_type_i_discrete_descriptor *fmt = _fmt;
		sample_width = fmt->bBitResolution;
		sample_bytes = fmt->bSubframeSize;
		format = 1ULL << format;
		break;
	}

	case UAC_VERSION_2: {
		struct uac_format_type_i_ext_descriptor *fmt = _fmt;
		sample_width = fmt->bBitResolution;
		sample_bytes = fmt->bSubslotSize;

		if (format & UAC2_FORMAT_TYPE_I_RAW_DATA) {
			pcm_formats |= SNDRV_PCM_FMTBIT_SPECIAL;
			/* flag potentially raw DSD capable altsettings */
			fp->dsd_raw = true;
		}

		format <<= 1;
		break;
	}
	case UAC_VERSION_3: {
		struct uac3_as_header_descriptor *as = _fmt;

		sample_width = as->bBitResolution;
		sample_bytes = as->bSubslotSize;

		if (format & UAC3_FORMAT_TYPE_I_RAW_DATA)
			pcm_formats |= SNDRV_PCM_FMTBIT_SPECIAL;

		format <<= 1;
		break;
	}
	}

	fp->fmt_bits = sample_width;

	if ((pcm_formats == 0) &&
	    (format == 0 || format == (1 << UAC_FORMAT_TYPE_I_UNDEFINED))) {
		/* some devices don't define this correctly... */
		usb_audio_info(chip, "%u:%d : format type 0 is detected, processed as PCM\n",
			fp->iface, fp->altsetting);
		format = 1 << UAC_FORMAT_TYPE_I_PCM;
	}
	if (format & (1 << UAC_FORMAT_TYPE_I_PCM)) {
		if (((chip->usb_id == USB_ID(0x0582, 0x0016)) ||
		     /* Edirol SD-90 */
		     (chip->usb_id == USB_ID(0x0582, 0x000c))) &&
		     /* Roland SC-D70 */
		    sample_width == 24 && sample_bytes == 2)
			sample_bytes = 3;
		else if (sample_width > sample_bytes * 8) {
			usb_audio_info(chip, "%u:%d : sample bitwidth %d in over sample bytes %d\n",
				 fp->iface, fp->altsetting,
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
			usb_audio_info(chip,
				 "%u:%d : unsupported sample bitwidth %d in %d bytes\n",
				 fp->iface, fp->altsetting,
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
		usb_audio_info(chip,
			 "%u:%d : unsupported format bits %#llx\n",
			 fp->iface, fp->altsetting, format);
	}

	pcm_formats |= snd_usb_interface_dsd_format_quirks(chip, fp, sample_bytes);

	return pcm_formats;
}

static int set_fixed_rate(struct audioformat *fp, int rate, int rate_bits)
{
	kfree(fp->rate_table);
	fp->rate_table = kmalloc(sizeof(int), GFP_KERNEL);
	if (!fp->rate_table)
		return -ENOMEM;
	fp->nr_rates = 1;
	fp->rate_min = rate;
	fp->rate_max = rate;
	fp->rates = rate_bits;
	fp->rate_table[0] = rate;
	return 0;
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
		usb_audio_err(chip,
			"%u:%d : invalid UAC_FORMAT_TYPE desc\n",
			fp->iface, fp->altsetting);
		return -EINVAL;
	}

	if (nr_rates) {
		/*
		 * build the rate table and bitmap flags
		 */
		int r, idx;

		fp->rate_table = kmalloc_array(nr_rates, sizeof(int),
					       GFP_KERNEL);
		if (fp->rate_table == NULL)
			return -ENOMEM;

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
			/* Creative VF0420/VF0470 Live Cams report 16 kHz instead of 8kHz */
			if (rate == 16000 &&
			    (chip->usb_id == USB_ID(0x041e, 0x4064) ||
			     chip->usb_id == USB_ID(0x041e, 0x4068)))
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

	/* Jabra Evolve 65 headset */
	if (chip->usb_id == USB_ID(0x0b0e, 0x030b)) {
		/* only 48kHz for playback while keeping 16kHz for capture */
		if (fp->nr_rates != 1)
			return set_fixed_rate(fp, 48000, SNDRV_PCM_RATE_48000);
	}

	return 0;
}


/*
 * Presonus Studio 1810c supports a limited set of sampling
 * rates per altsetting but reports the full set each time.
 * If we don't filter out the unsupported rates and attempt
 * to configure the card, it will hang refusing to do any
 * further audio I/O until a hard reset is performed.
 *
 * The list of supported rates per altsetting (set of available
 * I/O channels) is described in the owner's manual, section 2.2.
 */
static bool s1810c_valid_sample_rate(struct audioformat *fp,
				     unsigned int rate)
{
	switch (fp->altsetting) {
	case 1:
		/* All ADAT ports available */
		return rate <= 48000;
	case 2:
		/* Half of ADAT ports available */
		return (rate == 88200 || rate == 96000);
	case 3:
		/* Analog I/O only (no S/PDIF nor ADAT) */
		return rate >= 176400;
	default:
		return false;
	}
	return false;
}

/*
 * Many Focusrite devices supports a limited set of sampling rates per
 * altsetting. Maximum rate is exposed in the last 4 bytes of Format Type
 * descriptor which has a non-standard bLength = 10.
 */
static bool focusrite_valid_sample_rate(struct snd_usb_audio *chip,
					struct audioformat *fp,
					unsigned int rate)
{
	struct usb_interface *iface;
	struct usb_host_interface *alts;
	unsigned char *fmt;
	unsigned int max_rate;

	iface = usb_ifnum_to_if(chip->dev, fp->iface);
	if (!iface)
		return true;

	alts = &iface->altsetting[fp->altset_idx];
	fmt = snd_usb_find_csint_desc(alts->extra, alts->extralen,
				      NULL, UAC_FORMAT_TYPE);
	if (!fmt)
		return true;

	if (fmt[0] == 10) { /* bLength */
		max_rate = combine_quad(&fmt[6]);

		/* Validate max rate */
		if (max_rate != 48000 &&
		    max_rate != 96000 &&
		    max_rate != 192000 &&
		    max_rate != 384000) {

			usb_audio_info(chip,
				"%u:%d : unexpected max rate: %u\n",
				fp->iface, fp->altsetting, max_rate);

			return true;
		}

		return rate <= max_rate;
	}

	return true;
}

/*
 * Helper function to walk the array of sample rate triplets reported by
 * the device. The problem is that we need to parse whole array first to
 * get to know how many sample rates we have to expect.
 * Then fp->rate_table can be allocated and filled.
 */
static int parse_uac2_sample_rate_range(struct snd_usb_audio *chip,
					struct audioformat *fp, int nr_triplets,
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

			/* Filter out invalid rates on Presonus Studio 1810c */
			if (chip->usb_id == USB_ID(0x0194f, 0x010c) &&
			    !s1810c_valid_sample_rate(fp, rate))
				goto skip_rate;

			/* Filter out invalid rates on Focusrite devices */
			if (USB_ID_VENDOR(chip->usb_id) == 0x1235 &&
			    !focusrite_valid_sample_rate(chip, fp, rate))
				goto skip_rate;

			if (fp->rate_table)
				fp->rate_table[nr_rates] = rate;
			if (!fp->rate_min || rate < fp->rate_min)
				fp->rate_min = rate;
			if (!fp->rate_max || rate > fp->rate_max)
				fp->rate_max = rate;
			fp->rates |= snd_pcm_rate_to_rate_bit(rate);

			nr_rates++;
			if (nr_rates >= MAX_NR_RATES) {
				usb_audio_err(chip, "invalid uac2 rates\n");
				break;
			}

skip_rate:
			/* avoid endless loop */
			if (res == 0)
				break;
		}
	}

	return nr_rates;
}

/* Line6 Helix series don't support the UAC2_CS_RANGE usb function
 * call. Return a static table of known clock rates.
 */
static int line6_parse_audio_format_rates_quirk(struct snd_usb_audio *chip,
						struct audioformat *fp)
{
	switch (chip->usb_id) {
	case USB_ID(0x0e41, 0x4241): /* Line6 Helix */
	case USB_ID(0x0e41, 0x4242): /* Line6 Helix Rack */
	case USB_ID(0x0e41, 0x4244): /* Line6 Helix LT */
	case USB_ID(0x0e41, 0x4246): /* Line6 HX-Stomp */
	case USB_ID(0x0e41, 0x4248): /* Line6 Helix >= fw 2.82 */
	case USB_ID(0x0e41, 0x4249): /* Line6 Helix Rack >= fw 2.82 */
	case USB_ID(0x0e41, 0x424a): /* Line6 Helix LT >= fw 2.82 */
		return set_fixed_rate(fp, 48000, SNDRV_PCM_RATE_48000);
	}

	return -ENODEV;
}

/*
 * parse the format descriptor and stores the possible sample rates
 * on the audioformat table (audio class v2 and v3).
 */
static int parse_audio_format_rates_v2v3(struct snd_usb_audio *chip,
				       struct audioformat *fp)
{
	struct usb_device *dev = chip->dev;
	unsigned char tmp[2], *data;
	int nr_triplets, data_size, ret = 0, ret_l6;
	int clock = snd_usb_clock_find_source(chip, fp, false);

	if (clock < 0) {
		dev_err(&dev->dev,
			"%s(): unable to find clock source (clock %d)\n",
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
		/* line6 helix devices don't support UAC2_CS_CONTROL_SAM_FREQ call */
		ret_l6 = line6_parse_audio_format_rates_quirk(chip, fp);
		if (ret_l6 == -ENODEV) {
			/* no line6 device found continue showing the error */
			dev_err(&dev->dev,
				"%s(): unable to retrieve number of sample rates (clock %d)\n",
				__func__, clock);
			goto err;
		}
		if (ret_l6 == 0) {
			dev_info(&dev->dev,
				"%s(): unable to retrieve number of sample rates: set it to a predefined value (clock %d).\n",
				__func__, clock);
			return 0;
		}
		ret = ret_l6;
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
		dev_err(&dev->dev,
			"%s(): unable to retrieve sample rate range (clock %d)\n",
				__func__, clock);
		ret = -EINVAL;
		goto err_free;
	}

	/* Call the triplet parser, and make sure fp->rate_table is NULL.
	 * We just use the return value to know how many sample rates we
	 * will have to deal with. */
	kfree(fp->rate_table);
	fp->rate_table = NULL;
	fp->nr_rates = parse_uac2_sample_rate_range(chip, fp, nr_triplets, data);

	if (fp->nr_rates == 0) {
		/* SNDRV_PCM_RATE_CONTINUOUS */
		ret = 0;
		goto err_free;
	}

	fp->rate_table = kmalloc_array(fp->nr_rates, sizeof(int), GFP_KERNEL);
	if (!fp->rate_table) {
		ret = -ENOMEM;
		goto err_free;
	}

	/* Call the triplet parser again, but this time, fp->rate_table is
	 * allocated, so the rates will be stored */
	parse_uac2_sample_rate_range(chip, fp, nr_triplets, data);

err_free:
	kfree(data);
err:
	return ret;
}

/*
 * parse the format type I and III descriptors
 */
static int parse_audio_format_i(struct snd_usb_audio *chip,
				struct audioformat *fp, u64 format,
				void *_fmt)
{
	snd_pcm_format_t pcm_format;
	unsigned int fmt_type;
	int ret;

	switch (fp->protocol) {
	default:
	case UAC_VERSION_1:
	case UAC_VERSION_2: {
		struct uac_format_type_i_continuous_descriptor *fmt = _fmt;

		fmt_type = fmt->bFormatType;
		break;
	}
	case UAC_VERSION_3: {
		/* fp->fmt_type is already set in this case */
		fmt_type = fp->fmt_type;
		break;
	}
	}

	if (fmt_type == UAC_FORMAT_TYPE_III) {
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
		fp->formats = parse_audio_format_i_type(chip, fp, format, _fmt);
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
	case UAC_VERSION_1: {
		struct uac_format_type_i_continuous_descriptor *fmt = _fmt;

		fp->channels = fmt->bNrChannels;
		ret = parse_audio_format_rates_v1(chip, fp, (unsigned char *) fmt, 7);
		break;
	}
	case UAC_VERSION_2:
	case UAC_VERSION_3: {
		/* fp->channels is already set in this case */
		ret = parse_audio_format_rates_v2v3(chip, fp);
		break;
	}
	}

	if (fp->channels < 1) {
		usb_audio_err(chip,
			"%u:%d : invalid channels %d\n",
			fp->iface, fp->altsetting, fp->channels);
		return -EINVAL;
	}

	return ret;
}

/*
 * parse the format type II descriptor
 */
static int parse_audio_format_ii(struct snd_usb_audio *chip,
				 struct audioformat *fp,
				 u64 format, void *_fmt)
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
		usb_audio_info(chip,
			 "%u:%d : unknown format tag %#llx is detected.  processed as MPEG.\n",
			 fp->iface, fp->altsetting, format);
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
		usb_audio_info(chip, "found format II with max.bitrate = %d, frame size=%d\n", brate, framesize);
		fp->frame_size = framesize;
		ret = parse_audio_format_rates_v1(chip, fp, _fmt, 8); /* fmt[8..] sample rates */
		break;
	}
	case UAC_VERSION_2: {
		struct uac_format_type_ii_ext_descriptor *fmt = _fmt;
		brate = le16_to_cpu(fmt->wMaxBitRate);
		framesize = le16_to_cpu(fmt->wSamplesPerFrame);
		usb_audio_info(chip, "found format II with max.bitrate = %d, frame size=%d\n", brate, framesize);
		fp->frame_size = framesize;
		ret = parse_audio_format_rates_v2v3(chip, fp);
		break;
	}
	}

	return ret;
}

int snd_usb_parse_audio_format(struct snd_usb_audio *chip,
			       struct audioformat *fp, u64 format,
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
		usb_audio_info(chip,
			 "%u:%d : format type %d is not supported yet\n",
			 fp->iface, fp->altsetting,
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

int snd_usb_parse_audio_format_v3(struct snd_usb_audio *chip,
			       struct audioformat *fp,
			       struct uac3_as_header_descriptor *as,
			       int stream)
{
	u64 format = le64_to_cpu(as->bmFormats);
	int err;

	/*
	 * Type I format bits are D0..D6
	 * This test works because type IV is not supported
	 */
	if (format & 0x7f)
		fp->fmt_type = UAC_FORMAT_TYPE_I;
	else
		fp->fmt_type = UAC_FORMAT_TYPE_III;

	err = parse_audio_format_i(chip, fp, format, as);
	if (err < 0)
		return err;

	return 0;
}
