/*
 * Generic routines and proc interface for ELD(EDID Like Data) information
 *
 * Copyright(c) 2008 Intel Corporation.
 * Copyright (c) 2013 Anssi Hannula <anssi.hannula@iki.fi>
 *
 * Authors:
 * 		Wu Fengguang <wfg@linux.intel.com>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <asm/unaligned.h>
#include "hda_codec.h"
#include "hda_local.h"

enum eld_versions {
	ELD_VER_CEA_861D	= 2,
	ELD_VER_PARTIAL		= 31,
};

enum cea_edid_versions {
	CEA_EDID_VER_NONE	= 0,
	CEA_EDID_VER_CEA861	= 1,
	CEA_EDID_VER_CEA861A	= 2,
	CEA_EDID_VER_CEA861BCD	= 3,
	CEA_EDID_VER_RESERVED	= 4,
};

static char *cea_speaker_allocation_names[] = {
	/*  0 */ "FL/FR",
	/*  1 */ "LFE",
	/*  2 */ "FC",
	/*  3 */ "RL/RR",
	/*  4 */ "RC",
	/*  5 */ "FLC/FRC",
	/*  6 */ "RLC/RRC",
	/*  7 */ "FLW/FRW",
	/*  8 */ "FLH/FRH",
	/*  9 */ "TC",
	/* 10 */ "FCH",
};

static char *eld_connection_type_names[4] = {
	"HDMI",
	"DisplayPort",
	"2-reserved",
	"3-reserved"
};

enum cea_audio_coding_types {
	AUDIO_CODING_TYPE_REF_STREAM_HEADER	=  0,
	AUDIO_CODING_TYPE_LPCM			=  1,
	AUDIO_CODING_TYPE_AC3			=  2,
	AUDIO_CODING_TYPE_MPEG1			=  3,
	AUDIO_CODING_TYPE_MP3			=  4,
	AUDIO_CODING_TYPE_MPEG2			=  5,
	AUDIO_CODING_TYPE_AACLC			=  6,
	AUDIO_CODING_TYPE_DTS			=  7,
	AUDIO_CODING_TYPE_ATRAC			=  8,
	AUDIO_CODING_TYPE_SACD			=  9,
	AUDIO_CODING_TYPE_EAC3			= 10,
	AUDIO_CODING_TYPE_DTS_HD		= 11,
	AUDIO_CODING_TYPE_MLP			= 12,
	AUDIO_CODING_TYPE_DST			= 13,
	AUDIO_CODING_TYPE_WMAPRO		= 14,
	AUDIO_CODING_TYPE_REF_CXT		= 15,
	/* also include valid xtypes below */
	AUDIO_CODING_TYPE_HE_AAC		= 15,
	AUDIO_CODING_TYPE_HE_AAC2		= 16,
	AUDIO_CODING_TYPE_MPEG_SURROUND		= 17,
};

enum cea_audio_coding_xtypes {
	AUDIO_CODING_XTYPE_HE_REF_CT		= 0,
	AUDIO_CODING_XTYPE_HE_AAC		= 1,
	AUDIO_CODING_XTYPE_HE_AAC2		= 2,
	AUDIO_CODING_XTYPE_MPEG_SURROUND	= 3,
	AUDIO_CODING_XTYPE_FIRST_RESERVED	= 4,
};

static char *cea_audio_coding_type_names[] = {
	/*  0 */ "undefined",
	/*  1 */ "LPCM",
	/*  2 */ "AC-3",
	/*  3 */ "MPEG1",
	/*  4 */ "MP3",
	/*  5 */ "MPEG2",
	/*  6 */ "AAC-LC",
	/*  7 */ "DTS",
	/*  8 */ "ATRAC",
	/*  9 */ "DSD (One Bit Audio)",
	/* 10 */ "E-AC-3/DD+ (Dolby Digital Plus)",
	/* 11 */ "DTS-HD",
	/* 12 */ "MLP (Dolby TrueHD)",
	/* 13 */ "DST",
	/* 14 */ "WMAPro",
	/* 15 */ "HE-AAC",
	/* 16 */ "HE-AACv2",
	/* 17 */ "MPEG Surround",
};

/*
 * The following two lists are shared between
 * 	- HDMI audio InfoFrame (source to sink)
 * 	- CEA E-EDID Extension (sink to source)
 */

/*
 * SS1:SS0 index => sample size
 */
static int cea_sample_sizes[4] = {
	0,	 		/* 0: Refer to Stream Header */
	AC_SUPPCM_BITS_16,	/* 1: 16 bits */
	AC_SUPPCM_BITS_20,	/* 2: 20 bits */
	AC_SUPPCM_BITS_24,	/* 3: 24 bits */
};

/*
 * SF2:SF1:SF0 index => sampling frequency
 */
static int cea_sampling_frequencies[8] = {
	0,			/* 0: Refer to Stream Header */
	SNDRV_PCM_RATE_32000,	/* 1:  32000Hz */
	SNDRV_PCM_RATE_44100,	/* 2:  44100Hz */
	SNDRV_PCM_RATE_48000,	/* 3:  48000Hz */
	SNDRV_PCM_RATE_88200,	/* 4:  88200Hz */
	SNDRV_PCM_RATE_96000,	/* 5:  96000Hz */
	SNDRV_PCM_RATE_176400,	/* 6: 176400Hz */
	SNDRV_PCM_RATE_192000,	/* 7: 192000Hz */
};

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

#define GRAB_BITS(buf, byte, lowbit, bits) 		\
({							\
	BUILD_BUG_ON(lowbit > 7);			\
	BUILD_BUG_ON(bits > 8);				\
	BUILD_BUG_ON(bits <= 0);			\
							\
	(buf[byte] >> (lowbit)) & ((1 << (bits)) - 1);	\
})

static void hdmi_update_short_audio_desc(struct hda_codec *codec,
					 struct cea_sad *a,
					 const unsigned char *buf)
{
	int i;
	int val;

	val = GRAB_BITS(buf, 1, 0, 7);
	a->rates = 0;
	for (i = 0; i < 7; i++)
		if (val & (1 << i))
			a->rates |= cea_sampling_frequencies[i + 1];

	a->channels = GRAB_BITS(buf, 0, 0, 3);
	a->channels++;

	a->sample_bits = 0;
	a->max_bitrate = 0;

	a->format = GRAB_BITS(buf, 0, 3, 4);
	switch (a->format) {
	case AUDIO_CODING_TYPE_REF_STREAM_HEADER:
		codec_info(codec, "HDMI: audio coding type 0 not expected\n");
		break;

	case AUDIO_CODING_TYPE_LPCM:
		val = GRAB_BITS(buf, 2, 0, 3);
		for (i = 0; i < 3; i++)
			if (val & (1 << i))
				a->sample_bits |= cea_sample_sizes[i + 1];
		break;

	case AUDIO_CODING_TYPE_AC3:
	case AUDIO_CODING_TYPE_MPEG1:
	case AUDIO_CODING_TYPE_MP3:
	case AUDIO_CODING_TYPE_MPEG2:
	case AUDIO_CODING_TYPE_AACLC:
	case AUDIO_CODING_TYPE_DTS:
	case AUDIO_CODING_TYPE_ATRAC:
		a->max_bitrate = GRAB_BITS(buf, 2, 0, 8);
		a->max_bitrate *= 8000;
		break;

	case AUDIO_CODING_TYPE_SACD:
		break;

	case AUDIO_CODING_TYPE_EAC3:
		break;

	case AUDIO_CODING_TYPE_DTS_HD:
		break;

	case AUDIO_CODING_TYPE_MLP:
		break;

	case AUDIO_CODING_TYPE_DST:
		break;

	case AUDIO_CODING_TYPE_WMAPRO:
		a->profile = GRAB_BITS(buf, 2, 0, 3);
		break;

	case AUDIO_CODING_TYPE_REF_CXT:
		a->format = GRAB_BITS(buf, 2, 3, 5);
		if (a->format == AUDIO_CODING_XTYPE_HE_REF_CT ||
		    a->format >= AUDIO_CODING_XTYPE_FIRST_RESERVED) {
			codec_info(codec,
				   "HDMI: audio coding xtype %d not expected\n",
				   a->format);
			a->format = 0;
		} else
			a->format += AUDIO_CODING_TYPE_HE_AAC -
				     AUDIO_CODING_XTYPE_HE_AAC;
		break;
	}
}

/*
 * Be careful, ELD buf could be totally rubbish!
 */
int snd_hdmi_parse_eld(struct hda_codec *codec, struct parsed_hdmi_eld *e,
			  const unsigned char *buf, int size)
{
	int mnl;
	int i;

	e->eld_ver = GRAB_BITS(buf, 0, 3, 5);
	if (e->eld_ver != ELD_VER_CEA_861D &&
	    e->eld_ver != ELD_VER_PARTIAL) {
		codec_info(codec, "HDMI: Unknown ELD version %d\n", e->eld_ver);
		goto out_fail;
	}

	e->baseline_len = GRAB_BITS(buf, 2, 0, 8);
	mnl		= GRAB_BITS(buf, 4, 0, 5);
	e->cea_edid_ver	= GRAB_BITS(buf, 4, 5, 3);

	e->support_hdcp	= GRAB_BITS(buf, 5, 0, 1);
	e->support_ai	= GRAB_BITS(buf, 5, 1, 1);
	e->conn_type	= GRAB_BITS(buf, 5, 2, 2);
	e->sad_count	= GRAB_BITS(buf, 5, 4, 4);

	e->aud_synch_delay = GRAB_BITS(buf, 6, 0, 8) * 2;
	e->spk_alloc	= GRAB_BITS(buf, 7, 0, 7);

	e->port_id	  = get_unaligned_le64(buf + 8);

	/* not specified, but the spec's tendency is little endian */
	e->manufacture_id = get_unaligned_le16(buf + 16);
	e->product_id	  = get_unaligned_le16(buf + 18);

	if (mnl > ELD_MAX_MNL) {
		codec_info(codec, "HDMI: MNL is reserved value %d\n", mnl);
		goto out_fail;
	} else if (ELD_FIXED_BYTES + mnl > size) {
		codec_info(codec, "HDMI: out of range MNL %d\n", mnl);
		goto out_fail;
	} else
		strlcpy(e->monitor_name, buf + ELD_FIXED_BYTES, mnl + 1);

	for (i = 0; i < e->sad_count; i++) {
		if (ELD_FIXED_BYTES + mnl + 3 * (i + 1) > size) {
			codec_info(codec, "HDMI: out of range SAD %d\n", i);
			goto out_fail;
		}
		hdmi_update_short_audio_desc(codec, e->sad + i,
					buf + ELD_FIXED_BYTES + mnl + 3 * i);
	}

	/*
	 * HDMI sink's ELD info cannot always be retrieved for now, e.g.
	 * in console or for audio devices. Assume the highest speakers
	 * configuration, to _not_ prohibit multi-channel audio playback.
	 */
	if (!e->spk_alloc)
		e->spk_alloc = 0xffff;

	return 0;

out_fail:
	return -EINVAL;
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

/*
 * SNDRV_PCM_RATE_* and AC_PAR_PCM values don't match, print correct rates with
 * hdmi-specific routine.
 */
static void hdmi_print_pcm_rates(int pcm, char *buf, int buflen)
{
	static unsigned int alsa_rates[] = {
		5512, 8000, 11025, 16000, 22050, 32000, 44100, 48000, 64000,
		88200, 96000, 176400, 192000, 384000
	};
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(alsa_rates); i++)
		if (pcm & (1 << i))
			j += snprintf(buf + j, buflen - j,  " %d",
				alsa_rates[i]);

	buf[j] = '\0'; /* necessary when j == 0 */
}

#define SND_PRINT_RATES_ADVISED_BUFSIZE	80

static void hdmi_show_short_audio_desc(struct hda_codec *codec,
				       struct cea_sad *a)
{
	char buf[SND_PRINT_RATES_ADVISED_BUFSIZE];
	char buf2[8 + SND_PRINT_BITS_ADVISED_BUFSIZE] = ", bits =";

	if (!a->format)
		return;

	hdmi_print_pcm_rates(a->rates, buf, sizeof(buf));

	if (a->format == AUDIO_CODING_TYPE_LPCM)
		snd_print_pcm_bits(a->sample_bits, buf2 + 8, sizeof(buf2) - 8);
	else if (a->max_bitrate)
		snprintf(buf2, sizeof(buf2),
				", max bitrate = %d", a->max_bitrate);
	else
		buf2[0] = '\0';

	codec_dbg(codec,
		  "HDMI: supports coding type %s: channels = %d, rates =%s%s\n",
		  cea_audio_coding_type_names[a->format],
		  a->channels, buf, buf2);
}

void snd_print_channel_allocation(int spk_alloc, char *buf, int buflen)
{
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(cea_speaker_allocation_names); i++) {
		if (spk_alloc & (1 << i))
			j += snprintf(buf + j, buflen - j,  " %s",
					cea_speaker_allocation_names[i]);
	}
	buf[j] = '\0';	/* necessary when j == 0 */
}

void snd_hdmi_show_eld(struct hda_codec *codec, struct parsed_hdmi_eld *e)
{
	int i;

	codec_dbg(codec, "HDMI: detected monitor %s at connection type %s\n",
			e->monitor_name,
			eld_connection_type_names[e->conn_type]);

	if (e->spk_alloc) {
		char buf[SND_PRINT_CHANNEL_ALLOCATION_ADVISED_BUFSIZE];
		snd_print_channel_allocation(e->spk_alloc, buf, sizeof(buf));
		codec_dbg(codec, "HDMI: available speakers:%s\n", buf);
	}

	for (i = 0; i < e->sad_count; i++)
		hdmi_show_short_audio_desc(codec, e->sad + i);
}

#ifdef CONFIG_SND_PROC_FS

static void hdmi_print_sad_info(int i, struct cea_sad *a,
				struct snd_info_buffer *buffer)
{
	char buf[SND_PRINT_RATES_ADVISED_BUFSIZE];

	snd_iprintf(buffer, "sad%d_coding_type\t[0x%x] %s\n",
			i, a->format, cea_audio_coding_type_names[a->format]);
	snd_iprintf(buffer, "sad%d_channels\t\t%d\n", i, a->channels);

	hdmi_print_pcm_rates(a->rates, buf, sizeof(buf));
	snd_iprintf(buffer, "sad%d_rates\t\t[0x%x]%s\n", i, a->rates, buf);

	if (a->format == AUDIO_CODING_TYPE_LPCM) {
		snd_print_pcm_bits(a->sample_bits, buf, sizeof(buf));
		snd_iprintf(buffer, "sad%d_bits\t\t[0x%x]%s\n",
							i, a->sample_bits, buf);
	}

	if (a->max_bitrate)
		snd_iprintf(buffer, "sad%d_max_bitrate\t%d\n",
							i, a->max_bitrate);

	if (a->profile)
		snd_iprintf(buffer, "sad%d_profile\t\t%d\n", i, a->profile);
}

void snd_hdmi_print_eld_info(struct hdmi_eld *eld,
			     struct snd_info_buffer *buffer)
{
	struct parsed_hdmi_eld *e = &eld->info;
	char buf[SND_PRINT_CHANNEL_ALLOCATION_ADVISED_BUFSIZE];
	int i;
	static char *eld_version_names[32] = {
		"reserved",
		"reserved",
		"CEA-861D or below",
		[3 ... 30] = "reserved",
		[31] = "partial"
	};
	static char *cea_edid_version_names[8] = {
		"no CEA EDID Timing Extension block present",
		"CEA-861",
		"CEA-861-A",
		"CEA-861-B, C or D",
		[4 ... 7] = "reserved"
	};

	snd_iprintf(buffer, "monitor_present\t\t%d\n", eld->monitor_present);
	snd_iprintf(buffer, "eld_valid\t\t%d\n", eld->eld_valid);
	if (!eld->eld_valid)
		return;
	snd_iprintf(buffer, "monitor_name\t\t%s\n", e->monitor_name);
	snd_iprintf(buffer, "connection_type\t\t%s\n",
				eld_connection_type_names[e->conn_type]);
	snd_iprintf(buffer, "eld_version\t\t[0x%x] %s\n", e->eld_ver,
					eld_version_names[e->eld_ver]);
	snd_iprintf(buffer, "edid_version\t\t[0x%x] %s\n", e->cea_edid_ver,
				cea_edid_version_names[e->cea_edid_ver]);
	snd_iprintf(buffer, "manufacture_id\t\t0x%x\n", e->manufacture_id);
	snd_iprintf(buffer, "product_id\t\t0x%x\n", e->product_id);
	snd_iprintf(buffer, "port_id\t\t\t0x%llx\n", (long long)e->port_id);
	snd_iprintf(buffer, "support_hdcp\t\t%d\n", e->support_hdcp);
	snd_iprintf(buffer, "support_ai\t\t%d\n", e->support_ai);
	snd_iprintf(buffer, "audio_sync_delay\t%d\n", e->aud_synch_delay);

	snd_print_channel_allocation(e->spk_alloc, buf, sizeof(buf));
	snd_iprintf(buffer, "speakers\t\t[0x%x]%s\n", e->spk_alloc, buf);

	snd_iprintf(buffer, "sad_count\t\t%d\n", e->sad_count);

	for (i = 0; i < e->sad_count; i++)
		hdmi_print_sad_info(i, e->sad + i, buffer);
}

void snd_hdmi_write_eld_info(struct hdmi_eld *eld,
			     struct snd_info_buffer *buffer)
{
	struct parsed_hdmi_eld *e = &eld->info;
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
void snd_hdmi_eld_update_pcm_info(struct parsed_hdmi_eld *e,
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
		struct cea_sad *a = &e->sad[i];
		rates |= a->rates;
		if (a->channels > channels_max)
			channels_max = a->channels;
		if (a->format == AUDIO_CODING_TYPE_LPCM) {
			if (a->sample_bits & AC_SUPPCM_BITS_20) {
				formats |= SNDRV_PCM_FMTBIT_S32_LE;
				if (maxbps < 20)
					maxbps = 20;
			}
			if (a->sample_bits & AC_SUPPCM_BITS_24) {
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
