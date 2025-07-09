// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic routines and proc interface for ELD(EDID Like Data) information
 *
 * Copyright(c) 2008 Intel Corporation.
 * Copyright (c) 2013 Anssi Hannula <anssi.hannula@iki.fi>
 *
 * Authors:
 * 		Wu Fengguang <wfg@linux.intel.com>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <linux/unaligned.h>
#include <sound/hda_chmap.h>
#include <sound/hda_codec.h>
#include "hda_local.h"

enum cea_edid_versions {
	CEA_EDID_VER_NONE	= 0,
	CEA_EDID_VER_CEA861	= 1,
	CEA_EDID_VER_CEA861A	= 2,
	CEA_EDID_VER_CEA861BCD	= 3,
	CEA_EDID_VER_RESERVED	= 4,
};

/*
 * The following two lists are shared between
 * 	- HDMI audio InfoFrame (source to sink)
 * 	- CEA E-EDID Extension (sink to source)
 */

static unsigned int hdmi_get_eld_data(struct hda_codec *codec, hda_nid_t nid,
					int byte_index)
{
	unsigned int val;

	val = snd_hda_codec_read(codec, nid, 0,
					AC_VERB_GET_HDMI_ELDD, byte_index);
#ifdef BE_PARANOID
	codec_info(codec, "HDMI: ELD data byte %d: 0x%x\n", byte_index, val);
#endif
	return val;
}

int snd_hdmi_get_eld_size(struct hda_codec *codec, hda_nid_t nid)
{
	return snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_HDMI_DIP_SIZE,
						 AC_DIPSIZE_ELD_BUF);
}

int snd_hdmi_get_eld(struct hda_codec *codec, hda_nid_t nid,
		     unsigned char *buf, int *eld_size)
{
	int i;
	int ret = 0;
	int size;

	/*
	 * ELD size is initialized to zero in caller function. If no errors and
	 * ELD is valid, actual eld_size is assigned.
	 */

	size = snd_hdmi_get_eld_size(codec, nid);
	if (size == 0) {
		/* wfg: workaround for ASUS P5E-VM HDMI board */
		codec_info(codec, "HDMI: ELD buf size is 0, force 128\n");
		size = 128;
	}
	if (size < ELD_FIXED_BYTES || size > ELD_MAX_SIZE) {
		codec_info(codec, "HDMI: invalid ELD buf size %d\n", size);
		return -ERANGE;
	}

	/* set ELD buffer */
	for (i = 0; i < size; i++) {
		unsigned int val = hdmi_get_eld_data(codec, nid, i);
		/*
		 * Graphics driver might be writing to ELD buffer right now.
		 * Just abort. The caller will repoll after a while.
		 */
		if (!(val & AC_ELDD_ELD_VALID)) {
			codec_info(codec, "HDMI: invalid ELD data byte %d\n", i);
			ret = -EINVAL;
			goto error;
		}
		val &= AC_ELDD_ELD_DATA;
		/*
		 * The first byte cannot be zero. This can happen on some DVI
		 * connections. Some Intel chips may also need some 250ms delay
		 * to return non-zero ELD data, even when the graphics driver
		 * correctly writes ELD content before setting ELD_valid bit.
		 */
		if (!val && !i) {
			codec_dbg(codec, "HDMI: 0 ELD data\n");
			ret = -EINVAL;
			goto error;
		}
		buf[i] = val;
	}

	*eld_size = size;
error:
	return ret;
}

#ifdef CONFIG_SND_PROC_FS
void snd_hdmi_print_eld_info(struct hdmi_eld *eld,
			     struct snd_info_buffer *buffer,
			     hda_nid_t pin_nid, int dev_id, hda_nid_t cvt_nid)
{
	snd_iprintf(buffer, "monitor_present\t\t%d\n", eld->monitor_present);
	snd_iprintf(buffer, "eld_valid\t\t%d\n", eld->eld_valid);
	snd_iprintf(buffer, "codec_pin_nid\t\t0x%x\n", pin_nid);
	snd_iprintf(buffer, "codec_dev_id\t\t0x%x\n", dev_id);
	snd_iprintf(buffer, "codec_cvt_nid\t\t0x%x\n", cvt_nid);

	if (!eld->eld_valid)
		return;

	snd_print_eld_info(&eld->info, buffer);
}

void snd_hdmi_write_eld_info(struct hdmi_eld *eld,
			     struct snd_info_buffer *buffer)
{
	struct snd_parsed_hdmi_eld *e = &eld->info;
	char line[64];
	char name[64];
	char *sname;
	long long val;
	unsigned int n;

	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%s %llx", name, &val) != 2)
			continue;
		/*
		 * We don't allow modification to these fields:
		 * 	monitor_name manufacture_id product_id
		 * 	eld_version edid_version
		 */
		if (!strcmp(name, "monitor_present"))
			eld->monitor_present = val;
		else if (!strcmp(name, "eld_valid"))
			eld->eld_valid = val;
		else if (!strcmp(name, "connection_type"))
			e->conn_type = val;
		else if (!strcmp(name, "port_id"))
			e->port_id = val;
		else if (!strcmp(name, "support_hdcp"))
			e->support_hdcp = val;
		else if (!strcmp(name, "support_ai"))
			e->support_ai = val;
		else if (!strcmp(name, "audio_sync_delay"))
			e->aud_synch_delay = val;
		else if (!strcmp(name, "speakers"))
			e->spk_alloc = val;
		else if (!strcmp(name, "sad_count"))
			e->sad_count = val;
		else if (!strncmp(name, "sad", 3)) {
			sname = name + 4;
			n = name[3] - '0';
			if (name[4] >= '0' && name[4] <= '9') {
				sname++;
				n = 10 * n + name[4] - '0';
			}
			if (n >= ELD_MAX_SAD)
				continue;
			if (!strcmp(sname, "_coding_type"))
				e->sad[n].format = val;
			else if (!strcmp(sname, "_channels"))
				e->sad[n].channels = val;
			else if (!strcmp(sname, "_rates"))
				e->sad[n].rates = val;
			else if (!strcmp(sname, "_bits"))
				e->sad[n].sample_bits = val;
			else if (!strcmp(sname, "_max_bitrate"))
				e->sad[n].max_bitrate = val;
			else if (!strcmp(sname, "_profile"))
				e->sad[n].profile = val;
			if (n >= e->sad_count)
				e->sad_count = n + 1;
		}
	}
}
#endif /* CONFIG_SND_PROC_FS */

/* update PCM info based on ELD */
void snd_hdmi_eld_update_pcm_info(struct snd_parsed_hdmi_eld *e,
			      struct hda_pcm_stream *hinfo)
{
	u32 rates;
	u64 formats;
	unsigned int maxbps;
	unsigned int channels_max;
	int i;

	/* assume basic audio support (the basic audio flag is not in ELD;
	 * however, all audio capable sinks are required to support basic
	 * audio) */
	rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
		SNDRV_PCM_RATE_48000;
	formats = SNDRV_PCM_FMTBIT_S16_LE;
	maxbps = 16;
	channels_max = 2;
	for (i = 0; i < e->sad_count; i++) {
		struct snd_cea_sad *a = &e->sad[i];
		rates |= a->rates;
		if (a->channels > channels_max)
			channels_max = a->channels;
		if (a->format == AUDIO_CODING_TYPE_LPCM) {
			if (a->sample_bits & ELD_PCM_BITS_20) {
				formats |= SNDRV_PCM_FMTBIT_S32_LE;
				if (maxbps < 20)
					maxbps = 20;
			}
			if (a->sample_bits & ELD_PCM_BITS_24) {
				formats |= SNDRV_PCM_FMTBIT_S32_LE;
				if (maxbps < 24)
					maxbps = 24;
			}
		}
	}

	/* restrict the parameters by the values the codec provides */
	hinfo->rates &= rates;
	hinfo->formats &= formats;
	hinfo->maxbps = min(hinfo->maxbps, maxbps);
	hinfo->channels_max = min(hinfo->channels_max, channels_max);
}


/* ATI/AMD specific stuff (ELD emulation) */

#define ATI_VERB_SET_AUDIO_DESCRIPTOR	0x776
#define ATI_VERB_SET_SINK_INFO_INDEX	0x780
#define ATI_VERB_GET_SPEAKER_ALLOCATION	0xf70
#define ATI_VERB_GET_AUDIO_DESCRIPTOR	0xf76
#define ATI_VERB_GET_AUDIO_VIDEO_DELAY	0xf7b
#define ATI_VERB_GET_SINK_INFO_INDEX	0xf80
#define ATI_VERB_GET_SINK_INFO_DATA	0xf81

#define ATI_SPKALLOC_SPKALLOC		0x007f
#define ATI_SPKALLOC_TYPE_HDMI		0x0100
#define ATI_SPKALLOC_TYPE_DISPLAYPORT	0x0200

/* first three bytes are just standard SAD */
#define ATI_AUDIODESC_CHANNELS		0x00000007
#define ATI_AUDIODESC_RATES		0x0000ff00
#define ATI_AUDIODESC_LPCM_STEREO_RATES	0xff000000

/* in standard HDMI VSDB format */
#define ATI_DELAY_VIDEO_LATENCY		0x000000ff
#define ATI_DELAY_AUDIO_LATENCY		0x0000ff00

enum ati_sink_info_idx {
	ATI_INFO_IDX_MANUFACTURER_ID	= 0,
	ATI_INFO_IDX_PRODUCT_ID		= 1,
	ATI_INFO_IDX_SINK_DESC_LEN	= 2,
	ATI_INFO_IDX_PORT_ID_LOW	= 3,
	ATI_INFO_IDX_PORT_ID_HIGH	= 4,
	ATI_INFO_IDX_SINK_DESC_FIRST	= 5,
	ATI_INFO_IDX_SINK_DESC_LAST	= 22, /* max len 18 bytes */
};

int snd_hdmi_get_eld_ati(struct hda_codec *codec, hda_nid_t nid,
			 unsigned char *buf, int *eld_size, bool rev3_or_later)
{
	int spkalloc, ati_sad, aud_synch;
	int sink_desc_len = 0;
	int pos, i;

	/* ATI/AMD does not have ELD, emulate it */

	spkalloc = snd_hda_codec_read(codec, nid, 0, ATI_VERB_GET_SPEAKER_ALLOCATION, 0);

	if (spkalloc <= 0) {
		codec_info(codec, "HDMI ATI/AMD: no speaker allocation for ELD\n");
		return -EINVAL;
	}

	memset(buf, 0, ELD_FIXED_BYTES + ELD_MAX_MNL + ELD_MAX_SAD * 3);

	/* version */
	buf[0] = ELD_VER_CEA_861D << 3;

	/* speaker allocation from EDID */
	buf[7] = spkalloc & ATI_SPKALLOC_SPKALLOC;

	/* is DisplayPort? */
	if (spkalloc & ATI_SPKALLOC_TYPE_DISPLAYPORT)
		buf[5] |= 0x04;

	pos = ELD_FIXED_BYTES;

	if (rev3_or_later) {
		int sink_info;

		snd_hda_codec_write(codec, nid, 0, ATI_VERB_SET_SINK_INFO_INDEX, ATI_INFO_IDX_PORT_ID_LOW);
		sink_info = snd_hda_codec_read(codec, nid, 0, ATI_VERB_GET_SINK_INFO_DATA, 0);
		put_unaligned_le32(sink_info, buf + 8);

		snd_hda_codec_write(codec, nid, 0, ATI_VERB_SET_SINK_INFO_INDEX, ATI_INFO_IDX_PORT_ID_HIGH);
		sink_info = snd_hda_codec_read(codec, nid, 0, ATI_VERB_GET_SINK_INFO_DATA, 0);
		put_unaligned_le32(sink_info, buf + 12);

		snd_hda_codec_write(codec, nid, 0, ATI_VERB_SET_SINK_INFO_INDEX, ATI_INFO_IDX_MANUFACTURER_ID);
		sink_info = snd_hda_codec_read(codec, nid, 0, ATI_VERB_GET_SINK_INFO_DATA, 0);
		put_unaligned_le16(sink_info, buf + 16);

		snd_hda_codec_write(codec, nid, 0, ATI_VERB_SET_SINK_INFO_INDEX, ATI_INFO_IDX_PRODUCT_ID);
		sink_info = snd_hda_codec_read(codec, nid, 0, ATI_VERB_GET_SINK_INFO_DATA, 0);
		put_unaligned_le16(sink_info, buf + 18);

		snd_hda_codec_write(codec, nid, 0, ATI_VERB_SET_SINK_INFO_INDEX, ATI_INFO_IDX_SINK_DESC_LEN);
		sink_desc_len = snd_hda_codec_read(codec, nid, 0, ATI_VERB_GET_SINK_INFO_DATA, 0);

		if (sink_desc_len > ELD_MAX_MNL) {
			codec_info(codec, "HDMI ATI/AMD: Truncating HDMI sink description with length %d\n",
				   sink_desc_len);
			sink_desc_len = ELD_MAX_MNL;
		}

		buf[4] |= sink_desc_len;

		for (i = 0; i < sink_desc_len; i++) {
			snd_hda_codec_write(codec, nid, 0, ATI_VERB_SET_SINK_INFO_INDEX, ATI_INFO_IDX_SINK_DESC_FIRST + i);
			buf[pos++] = snd_hda_codec_read(codec, nid, 0, ATI_VERB_GET_SINK_INFO_DATA, 0);
		}
	}

	for (i = AUDIO_CODING_TYPE_LPCM; i <= AUDIO_CODING_TYPE_WMAPRO; i++) {
		if (i == AUDIO_CODING_TYPE_SACD || i == AUDIO_CODING_TYPE_DST)
			continue; /* not handled by ATI/AMD */

		snd_hda_codec_write(codec, nid, 0, ATI_VERB_SET_AUDIO_DESCRIPTOR, i << 3);
		ati_sad = snd_hda_codec_read(codec, nid, 0, ATI_VERB_GET_AUDIO_DESCRIPTOR, 0);

		if (ati_sad <= 0)
			continue;

		if (ati_sad & ATI_AUDIODESC_RATES) {
			/* format is supported, copy SAD as-is */
			buf[pos++] = (ati_sad & 0x0000ff) >> 0;
			buf[pos++] = (ati_sad & 0x00ff00) >> 8;
			buf[pos++] = (ati_sad & 0xff0000) >> 16;
		}

		if (i == AUDIO_CODING_TYPE_LPCM
		    && (ati_sad & ATI_AUDIODESC_LPCM_STEREO_RATES)
		    && (ati_sad & ATI_AUDIODESC_LPCM_STEREO_RATES) >> 16 != (ati_sad & ATI_AUDIODESC_RATES)) {
			/* for PCM there is a separate stereo rate mask */
			buf[pos++] = ((ati_sad & 0x000000ff) & ~ATI_AUDIODESC_CHANNELS) | 0x1;
			/* rates from the extra byte */
			buf[pos++] = (ati_sad & 0xff000000) >> 24;
			buf[pos++] = (ati_sad & 0x00ff0000) >> 16;
		}
	}

	if (pos == ELD_FIXED_BYTES + sink_desc_len) {
		codec_info(codec, "HDMI ATI/AMD: no audio descriptors for ELD\n");
		return -EINVAL;
	}

	/*
	 * HDMI VSDB latency format:
	 * separately for both audio and video:
	 *  0          field not valid or unknown latency
	 *  [1..251]   msecs = (x-1)*2  (max 500ms with x = 251 = 0xfb)
	 *  255        audio/video not supported
	 *
	 * HDA latency format:
	 * single value indicating video latency relative to audio:
	 *  0          unknown or 0ms
	 *  [1..250]   msecs = x*2  (max 500ms with x = 250 = 0xfa)
	 *  [251..255] reserved
	 */
	aud_synch = snd_hda_codec_read(codec, nid, 0, ATI_VERB_GET_AUDIO_VIDEO_DELAY, 0);
	if ((aud_synch & ATI_DELAY_VIDEO_LATENCY) && (aud_synch & ATI_DELAY_AUDIO_LATENCY)) {
		int video_latency_hdmi = (aud_synch & ATI_DELAY_VIDEO_LATENCY);
		int audio_latency_hdmi = (aud_synch & ATI_DELAY_AUDIO_LATENCY) >> 8;

		if (video_latency_hdmi <= 0xfb && audio_latency_hdmi <= 0xfb &&
		    video_latency_hdmi > audio_latency_hdmi)
			buf[6] = video_latency_hdmi - audio_latency_hdmi;
		/* else unknown/invalid or 0ms or video ahead of audio, so use zero */
	}

	/* SAD count */
	buf[5] |= ((pos - ELD_FIXED_BYTES - sink_desc_len) / 3) << 4;

	/* Baseline ELD block length is 4-byte aligned */
	pos = round_up(pos, 4);

	/* Baseline ELD length (4-byte header is not counted in) */
	buf[2] = (pos - 4) / 4;

	*eld_size = pos;

	return 0;
}
