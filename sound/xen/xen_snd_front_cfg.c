// SPDX-License-Identifier: GPL-2.0 OR MIT

/*
 * Xen para-virtual sound device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#include <xen/xenbus.h>

#include <xen/interface/io/sndif.h>

#include "xen_snd_front.h"
#include "xen_snd_front_cfg.h"

/* Maximum number of supported streams. */
#define VSND_MAX_STREAM		8

struct cfg_hw_sample_rate {
	const char *name;
	unsigned int mask;
	unsigned int value;
};

static const struct cfg_hw_sample_rate CFG_HW_SUPPORTED_RATES[] = {
	{ .name = "5512",   .mask = SNDRV_PCM_RATE_5512,   .value = 5512 },
	{ .name = "8000",   .mask = SNDRV_PCM_RATE_8000,   .value = 8000 },
	{ .name = "11025",  .mask = SNDRV_PCM_RATE_11025,  .value = 11025 },
	{ .name = "16000",  .mask = SNDRV_PCM_RATE_16000,  .value = 16000 },
	{ .name = "22050",  .mask = SNDRV_PCM_RATE_22050,  .value = 22050 },
	{ .name = "32000",  .mask = SNDRV_PCM_RATE_32000,  .value = 32000 },
	{ .name = "44100",  .mask = SNDRV_PCM_RATE_44100,  .value = 44100 },
	{ .name = "48000",  .mask = SNDRV_PCM_RATE_48000,  .value = 48000 },
	{ .name = "64000",  .mask = SNDRV_PCM_RATE_64000,  .value = 64000 },
	{ .name = "96000",  .mask = SNDRV_PCM_RATE_96000,  .value = 96000 },
	{ .name = "176400", .mask = SNDRV_PCM_RATE_176400, .value = 176400 },
	{ .name = "192000", .mask = SNDRV_PCM_RATE_192000, .value = 192000 },
};

struct cfg_hw_sample_format {
	const char *name;
	u64 mask;
};

static const struct cfg_hw_sample_format CFG_HW_SUPPORTED_FORMATS[] = {
	{
		.name = XENSND_PCM_FORMAT_U8_STR,
		.mask = SNDRV_PCM_FMTBIT_U8
	},
	{
		.name = XENSND_PCM_FORMAT_S8_STR,
		.mask = SNDRV_PCM_FMTBIT_S8
	},
	{
		.name = XENSND_PCM_FORMAT_U16_LE_STR,
		.mask = SNDRV_PCM_FMTBIT_U16_LE
	},
	{
		.name = XENSND_PCM_FORMAT_U16_BE_STR,
		.mask = SNDRV_PCM_FMTBIT_U16_BE
	},
	{
		.name = XENSND_PCM_FORMAT_S16_LE_STR,
		.mask = SNDRV_PCM_FMTBIT_S16_LE
	},
	{
		.name = XENSND_PCM_FORMAT_S16_BE_STR,
		.mask = SNDRV_PCM_FMTBIT_S16_BE
	},
	{
		.name = XENSND_PCM_FORMAT_U24_LE_STR,
		.mask = SNDRV_PCM_FMTBIT_U24_LE
	},
	{
		.name = XENSND_PCM_FORMAT_U24_BE_STR,
		.mask = SNDRV_PCM_FMTBIT_U24_BE
	},
	{
		.name = XENSND_PCM_FORMAT_S24_LE_STR,
		.mask = SNDRV_PCM_FMTBIT_S24_LE
	},
	{
		.name = XENSND_PCM_FORMAT_S24_BE_STR,
		.mask = SNDRV_PCM_FMTBIT_S24_BE
	},
	{
		.name = XENSND_PCM_FORMAT_U32_LE_STR,
		.mask = SNDRV_PCM_FMTBIT_U32_LE
	},
	{
		.name = XENSND_PCM_FORMAT_U32_BE_STR,
		.mask = SNDRV_PCM_FMTBIT_U32_BE
	},
	{
		.name = XENSND_PCM_FORMAT_S32_LE_STR,
		.mask = SNDRV_PCM_FMTBIT_S32_LE
	},
	{
		.name = XENSND_PCM_FORMAT_S32_BE_STR,
		.mask = SNDRV_PCM_FMTBIT_S32_BE
	},
	{
		.name = XENSND_PCM_FORMAT_A_LAW_STR,
		.mask = SNDRV_PCM_FMTBIT_A_LAW
	},
	{
		.name = XENSND_PCM_FORMAT_MU_LAW_STR,
		.mask = SNDRV_PCM_FMTBIT_MU_LAW
	},
	{
		.name = XENSND_PCM_FORMAT_F32_LE_STR,
		.mask = SNDRV_PCM_FMTBIT_FLOAT_LE
	},
	{
		.name = XENSND_PCM_FORMAT_F32_BE_STR,
		.mask = SNDRV_PCM_FMTBIT_FLOAT_BE
	},
	{
		.name = XENSND_PCM_FORMAT_F64_LE_STR,
		.mask = SNDRV_PCM_FMTBIT_FLOAT64_LE
	},
	{
		.name = XENSND_PCM_FORMAT_F64_BE_STR,
		.mask = SNDRV_PCM_FMTBIT_FLOAT64_BE
	},
	{
		.name = XENSND_PCM_FORMAT_IEC958_SUBFRAME_LE_STR,
		.mask = SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE
	},
	{
		.name = XENSND_PCM_FORMAT_IEC958_SUBFRAME_BE_STR,
		.mask = SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_BE
	},
	{
		.name = XENSND_PCM_FORMAT_IMA_ADPCM_STR,
		.mask = SNDRV_PCM_FMTBIT_IMA_ADPCM
	},
	{
		.name = XENSND_PCM_FORMAT_MPEG_STR,
		.mask = SNDRV_PCM_FMTBIT_MPEG
	},
	{
		.name = XENSND_PCM_FORMAT_GSM_STR,
		.mask = SNDRV_PCM_FMTBIT_GSM
	},
};

static void cfg_hw_rates(char *list, unsigned int len,
			 const char *path, struct snd_pcm_hardware *pcm_hw)
{
	char *cur_rate;
	unsigned int cur_mask;
	unsigned int cur_value;
	unsigned int rates;
	unsigned int rate_min;
	unsigned int rate_max;
	int i;

	rates = 0;
	rate_min = -1;
	rate_max = 0;
	while ((cur_rate = strsep(&list, XENSND_LIST_SEPARATOR))) {
		for (i = 0; i < ARRAY_SIZE(CFG_HW_SUPPORTED_RATES); i++)
			if (!strncasecmp(cur_rate,
					 CFG_HW_SUPPORTED_RATES[i].name,
					 XENSND_SAMPLE_RATE_MAX_LEN)) {
				cur_mask = CFG_HW_SUPPORTED_RATES[i].mask;
				cur_value = CFG_HW_SUPPORTED_RATES[i].value;
				rates |= cur_mask;
				if (rate_min > cur_value)
					rate_min = cur_value;
				if (rate_max < cur_value)
					rate_max = cur_value;
			}
	}

	if (rates) {
		pcm_hw->rates = rates;
		pcm_hw->rate_min = rate_min;
		pcm_hw->rate_max = rate_max;
	}
}

static void cfg_formats(char *list, unsigned int len,
			const char *path, struct snd_pcm_hardware *pcm_hw)
{
	u64 formats;
	char *cur_format;
	int i;

	formats = 0;
	while ((cur_format = strsep(&list, XENSND_LIST_SEPARATOR))) {
		for (i = 0; i < ARRAY_SIZE(CFG_HW_SUPPORTED_FORMATS); i++)
			if (!strncasecmp(cur_format,
					 CFG_HW_SUPPORTED_FORMATS[i].name,
					 XENSND_SAMPLE_FORMAT_MAX_LEN))
				formats |= CFG_HW_SUPPORTED_FORMATS[i].mask;
	}

	if (formats)
		pcm_hw->formats = formats;
}

#define MAX_BUFFER_SIZE		(64 * 1024)
#define MIN_PERIOD_SIZE		64
#define MAX_PERIOD_SIZE		MAX_BUFFER_SIZE
#define USE_FORMATS		(SNDRV_PCM_FMTBIT_U8 | \
				 SNDRV_PCM_FMTBIT_S16_LE)
#define USE_RATE		(SNDRV_PCM_RATE_CONTINUOUS | \
				 SNDRV_PCM_RATE_8000_48000)
#define USE_RATE_MIN		5512
#define USE_RATE_MAX		48000
#define USE_CHANNELS_MIN	1
#define USE_CHANNELS_MAX	2
#define USE_PERIODS_MIN		2
#define USE_PERIODS_MAX		(MAX_BUFFER_SIZE / MIN_PERIOD_SIZE)

static const struct snd_pcm_hardware SND_DRV_PCM_HW_DEFAULT = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_RESUME |
		 SNDRV_PCM_INFO_MMAP_VALID),
	.formats = USE_FORMATS,
	.rates = USE_RATE,
	.rate_min = USE_RATE_MIN,
	.rate_max = USE_RATE_MAX,
	.channels_min = USE_CHANNELS_MIN,
	.channels_max = USE_CHANNELS_MAX,
	.buffer_bytes_max = MAX_BUFFER_SIZE,
	.period_bytes_min = MIN_PERIOD_SIZE,
	.period_bytes_max = MAX_PERIOD_SIZE,
	.periods_min = USE_PERIODS_MIN,
	.periods_max = USE_PERIODS_MAX,
	.fifo_size = 0,
};

static void cfg_read_pcm_hw(const char *path,
			    struct snd_pcm_hardware *parent_pcm_hw,
			    struct snd_pcm_hardware *pcm_hw)
{
	char *list;
	int val;
	size_t buf_sz;
	unsigned int len;

	/* Inherit parent's PCM HW and read overrides from XenStore. */
	if (parent_pcm_hw)
		*pcm_hw = *parent_pcm_hw;
	else
		*pcm_hw = SND_DRV_PCM_HW_DEFAULT;

	val = xenbus_read_unsigned(path, XENSND_FIELD_CHANNELS_MIN, 0);
	if (val)
		pcm_hw->channels_min = val;

	val = xenbus_read_unsigned(path, XENSND_FIELD_CHANNELS_MAX, 0);
	if (val)
		pcm_hw->channels_max = val;

	list = xenbus_read(XBT_NIL, path, XENSND_FIELD_SAMPLE_RATES, &len);
	if (!IS_ERR(list)) {
		cfg_hw_rates(list, len, path, pcm_hw);
		kfree(list);
	}

	list = xenbus_read(XBT_NIL, path, XENSND_FIELD_SAMPLE_FORMATS, &len);
	if (!IS_ERR(list)) {
		cfg_formats(list, len, path, pcm_hw);
		kfree(list);
	}

	buf_sz = xenbus_read_unsigned(path, XENSND_FIELD_BUFFER_SIZE, 0);
	if (buf_sz)
		pcm_hw->buffer_bytes_max = buf_sz;

	/* Update configuration to match new values. */
	if (pcm_hw->channels_min > pcm_hw->channels_max)
		pcm_hw->channels_min = pcm_hw->channels_max;

	if (pcm_hw->rate_min > pcm_hw->rate_max)
		pcm_hw->rate_min = pcm_hw->rate_max;

	pcm_hw->period_bytes_max = pcm_hw->buffer_bytes_max;

	pcm_hw->periods_max = pcm_hw->period_bytes_max /
		pcm_hw->period_bytes_min;
}

static int cfg_get_stream_type(const char *path, int index,
			       int *num_pb, int *num_cap)
{
	char *str = NULL;
	char *stream_path;
	int ret;

	*num_pb = 0;
	*num_cap = 0;
	stream_path = kasprintf(GFP_KERNEL, "%s/%d", path, index);
	if (!stream_path) {
		ret = -ENOMEM;
		goto fail;
	}

	str = xenbus_read(XBT_NIL, stream_path, XENSND_FIELD_TYPE, NULL);
	if (IS_ERR(str)) {
		ret = PTR_ERR(str);
		str = NULL;
		goto fail;
	}

	if (!strncasecmp(str, XENSND_STREAM_TYPE_PLAYBACK,
			 sizeof(XENSND_STREAM_TYPE_PLAYBACK))) {
		(*num_pb)++;
	} else if (!strncasecmp(str, XENSND_STREAM_TYPE_CAPTURE,
			      sizeof(XENSND_STREAM_TYPE_CAPTURE))) {
		(*num_cap)++;
	} else {
		ret = -EINVAL;
		goto fail;
	}
	ret = 0;

fail:
	kfree(stream_path);
	kfree(str);
	return ret;
}

static int cfg_stream(struct xen_snd_front_info *front_info,
		      struct xen_front_cfg_pcm_instance *pcm_instance,
		      const char *path, int index, int *cur_pb, int *cur_cap,
		      int *stream_cnt)
{
	char *str = NULL;
	char *stream_path;
	struct xen_front_cfg_stream *stream;
	int ret;

	stream_path = devm_kasprintf(&front_info->xb_dev->dev,
				     GFP_KERNEL, "%s/%d", path, index);
	if (!stream_path) {
		ret = -ENOMEM;
		goto fail;
	}

	str = xenbus_read(XBT_NIL, stream_path, XENSND_FIELD_TYPE, NULL);
	if (IS_ERR(str)) {
		ret = PTR_ERR(str);
		str = NULL;
		goto fail;
	}

	if (!strncasecmp(str, XENSND_STREAM_TYPE_PLAYBACK,
			 sizeof(XENSND_STREAM_TYPE_PLAYBACK))) {
		stream = &pcm_instance->streams_pb[(*cur_pb)++];
	} else if (!strncasecmp(str, XENSND_STREAM_TYPE_CAPTURE,
			      sizeof(XENSND_STREAM_TYPE_CAPTURE))) {
		stream = &pcm_instance->streams_cap[(*cur_cap)++];
	} else {
		ret = -EINVAL;
		goto fail;
	}

	/* Get next stream index. */
	stream->index = (*stream_cnt)++;
	stream->xenstore_path = stream_path;
	/*
	 * Check XenStore if PCM HW configuration exists for this stream
	 * and update if so, e.g. we inherit all values from device's PCM HW,
	 * but can still override some of the values for the stream.
	 */
	cfg_read_pcm_hw(stream->xenstore_path,
			&pcm_instance->pcm_hw, &stream->pcm_hw);
	ret = 0;

fail:
	kfree(str);
	return ret;
}

static int cfg_device(struct xen_snd_front_info *front_info,
		      struct xen_front_cfg_pcm_instance *pcm_instance,
		      struct snd_pcm_hardware *parent_pcm_hw,
		      const char *path, int node_index, int *stream_cnt)
{
	char *str;
	char *device_path;
	int ret, i, num_streams;
	int num_pb, num_cap;
	int cur_pb, cur_cap;
	char node[3];

	device_path = kasprintf(GFP_KERNEL, "%s/%d", path, node_index);
	if (!device_path)
		return -ENOMEM;

	str = xenbus_read(XBT_NIL, device_path, XENSND_FIELD_DEVICE_NAME, NULL);
	if (!IS_ERR(str)) {
		strscpy(pcm_instance->name, str, sizeof(pcm_instance->name));
		kfree(str);
	}

	pcm_instance->device_id = node_index;

	/*
	 * Check XenStore if PCM HW configuration exists for this device
	 * and update if so, e.g. we inherit all values from card's PCM HW,
	 * but can still override some of the values for the device.
	 */
	cfg_read_pcm_hw(device_path, parent_pcm_hw, &pcm_instance->pcm_hw);

	/* Find out how many streams were configured in Xen store. */
	num_streams = 0;
	do {
		snprintf(node, sizeof(node), "%d", num_streams);
		if (!xenbus_exists(XBT_NIL, device_path, node))
			break;

		num_streams++;
	} while (num_streams < VSND_MAX_STREAM);

	pcm_instance->num_streams_pb = 0;
	pcm_instance->num_streams_cap = 0;
	/* Get number of playback and capture streams. */
	for (i = 0; i < num_streams; i++) {
		ret = cfg_get_stream_type(device_path, i, &num_pb, &num_cap);
		if (ret < 0)
			goto fail;

		pcm_instance->num_streams_pb += num_pb;
		pcm_instance->num_streams_cap += num_cap;
	}

	if (pcm_instance->num_streams_pb) {
		pcm_instance->streams_pb =
				devm_kcalloc(&front_info->xb_dev->dev,
					     pcm_instance->num_streams_pb,
					     sizeof(struct xen_front_cfg_stream),
					     GFP_KERNEL);
		if (!pcm_instance->streams_pb) {
			ret = -ENOMEM;
			goto fail;
		}
	}

	if (pcm_instance->num_streams_cap) {
		pcm_instance->streams_cap =
				devm_kcalloc(&front_info->xb_dev->dev,
					     pcm_instance->num_streams_cap,
					     sizeof(struct xen_front_cfg_stream),
					     GFP_KERNEL);
		if (!pcm_instance->streams_cap) {
			ret = -ENOMEM;
			goto fail;
		}
	}

	cur_pb = 0;
	cur_cap = 0;
	for (i = 0; i < num_streams; i++) {
		ret = cfg_stream(front_info, pcm_instance, device_path, i,
				 &cur_pb, &cur_cap, stream_cnt);
		if (ret < 0)
			goto fail;
	}
	ret = 0;

fail:
	kfree(device_path);
	return ret;
}

int xen_snd_front_cfg_card(struct xen_snd_front_info *front_info,
			   int *stream_cnt)
{
	struct xenbus_device *xb_dev = front_info->xb_dev;
	struct xen_front_cfg_card *cfg = &front_info->cfg;
	int ret, num_devices, i;
	char node[3];

	*stream_cnt = 0;
	num_devices = 0;
	do {
		snprintf(node, sizeof(node), "%d", num_devices);
		if (!xenbus_exists(XBT_NIL, xb_dev->nodename, node))
			break;

		num_devices++;
	} while (num_devices < SNDRV_PCM_DEVICES);

	if (!num_devices) {
		dev_warn(&xb_dev->dev,
			 "No devices configured for sound card at %s\n",
			 xb_dev->nodename);
		return -ENODEV;
	}

	/* Start from default PCM HW configuration for the card. */
	cfg_read_pcm_hw(xb_dev->nodename, NULL, &cfg->pcm_hw);

	cfg->pcm_instances =
			devm_kcalloc(&front_info->xb_dev->dev, num_devices,
				     sizeof(struct xen_front_cfg_pcm_instance),
				     GFP_KERNEL);
	if (!cfg->pcm_instances)
		return -ENOMEM;

	for (i = 0; i < num_devices; i++) {
		ret = cfg_device(front_info, &cfg->pcm_instances[i],
				 &cfg->pcm_hw, xb_dev->nodename, i, stream_cnt);
		if (ret < 0)
			return ret;
	}
	cfg->num_pcm_instances = num_devices;
	return 0;
}

