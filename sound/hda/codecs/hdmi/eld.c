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
