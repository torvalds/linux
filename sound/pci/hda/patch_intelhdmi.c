/*
 *
 *  patch_intelhdmi.c - Patch for Intel HDMI codecs
 *
 *  Copyright(c) 2008 Intel Corporation. All rights reserved.
 *
 *  Authors:
 *  			Jiang Zhe <zhe.jiang@intel.com>
 *  			Wu Fengguang <wfg@linux.intel.com>
 *
 *  Maintained by:
 *  			Wu Fengguang <wfg@linux.intel.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <asm/unaligned.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_patch.h"

#define CVT_NID		0x02	/* audio converter */
#define PIN_NID		0x03	/* HDMI output pin */

#define INTEL_HDMI_EVENT_TAG		0x08

/*
 * CEA Short Audio Descriptor data
 */
struct cea_sad {
	int	channels;
	int	format;		/* (format == 0) indicates invalid SAD */
	int	rates;
	int	sample_bits;	/* for LPCM */
	int	max_bitrate;	/* for AC3...ATRAC */
	int	profile;	/* for WMAPRO */
};

#define ELD_FIXED_BYTES	20
#define ELD_MAX_MNL	16
#define ELD_MAX_SAD	16

/*
 * ELD: EDID Like Data
 */
struct sink_eld {
	int	eld_size;
	int	baseline_len;
	int	eld_ver;	/* (eld_ver == 0) indicates invalid ELD */
	int	cea_edid_ver;
	char	monitor_name[ELD_MAX_MNL + 1];
	int	manufacture_id;
	int	product_id;
	u64	port_id;
	int	support_hdcp;
	int	support_ai;
	int	conn_type;
	int	aud_synch_delay;
	int	spk_alloc;
	int	sad_count;
	struct cea_sad sad[ELD_MAX_SAD];
};

struct intel_hdmi_spec {
	struct hda_multi_out multiout;
	struct hda_pcm pcm_rec;
	struct sink_eld sink;
};

static struct hda_verb pinout_enable_verb[] = {
	{PIN_NID, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{} /* terminator */
};

static struct hda_verb pinout_disable_verb[] = {
	{PIN_NID, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00},
	{}
};

static struct hda_verb unsolicited_response_verb[] = {
	{PIN_NID, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN |
						  INTEL_HDMI_EVENT_TAG},
	{}
};

static struct hda_verb def_chan_map[] = {
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x00},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x11},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x22},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x33},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x44},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x55},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x66},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x77},
	{}
};


struct hdmi_audio_infoframe {
	u8 type; /* 0x84 */
	u8 ver;  /* 0x01 */
	u8 len;  /* 0x0a */

	u8 checksum;	/* PB0 */
	u8 CC02_CT47;	/* CC in bits 0:2, CT in 4:7 */
	u8 SS01_SF24;
	u8 CXT04;
	u8 CA;
	u8 LFEPBL01_LSV36_DM_INH7;
	u8 reserved[5];	/* PB6  - PB10 */
};

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

enum eld_versions {
	ELD_VER_CEA_861D	= 2,
	ELD_VER_PARTIAL		= 31,
};

static char *eld_versoin_names[32] = {
	"0-reserved",
	"1-reserved",
	"CEA-861D or below",
	"3-reserved",
	[4 ... 30] = "reserved",
	[31] = "partial"
};

enum cea_edid_versions {
	CEA_EDID_VER_NONE	= 0,
	CEA_EDID_VER_CEA861	= 1,
	CEA_EDID_VER_CEA861A	= 2,
	CEA_EDID_VER_CEA861BCD	= 3,
	CEA_EDID_VER_RESERVED	= 4,
};

static char *cea_edid_version_names[8] = {
	"no CEA EDID Timing Extension block present",
	"CEA-861",
	"CEA-861-A",
	"CEA-861-B, C or D",
	"4-reserved",
	[5 ... 7] = "reserved"
};

/*
 * CEA Speaker Allocation data block bits
 */
#define CEA_SA_FLR	(0 << 0)
#define CEA_SA_LFE	(1 << 1)
#define CEA_SA_FC	(1 << 2)
#define CEA_SA_RLR	(1 << 3)
#define CEA_SA_RC	(1 << 4)
#define CEA_SA_FLRC	(1 << 5)
#define CEA_SA_RLRC	(1 << 6)
/* the following are not defined in ELD yet */
#define CEA_SA_FLRW	(1 << 7)
#define CEA_SA_FLRH	(1 << 8)
#define CEA_SA_TC	(1 << 9)
#define CEA_SA_FCH	(1 << 10)

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
	"Display Port",
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
	/*  9 */ "DSD(1-bit audio)",
	/* 10 */ "Dolby Digital Plus(E-AC-3/DD+)",
	/* 11 */ "DTS-HD",
	/* 12 */ "Dolby TrueHD(MLP)",
	/* 13 */ "DST",
	/* 14 */ "WMAPro",
	/* 15 */ "HE-AAC",
	/* 16 */ "HE-AACv2",
	/* 17 */ "MPEG Surround",
};


/*
 * HDMI routines
 */

static int hdmi_get_eld_size(struct hda_codec *codec, hda_nid_t nid)
{
	return snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_HDMI_DIP_SIZE,
						 AC_DIPSIZE_ELD_BUF);
}

static void hdmi_get_dip_index(struct hda_codec *codec, hda_nid_t nid,
				int *packet_index, int *byte_index)
{
	int val;

	val = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_HDMI_DIP_INDEX, 0);

	*packet_index = val >> 5;
	*byte_index = val & 0x1f;
}

static void hdmi_set_dip_index(struct hda_codec *codec, hda_nid_t nid,
				int packet_index, int byte_index)
{
	int val;

	val = (packet_index << 5) | (byte_index & 0x1f);

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_HDMI_DIP_INDEX, val);
}

static void hdmi_write_dip_byte(struct hda_codec *codec, hda_nid_t nid,
				unsigned char val)
{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_HDMI_DIP_DATA, val);
}

static void hdmi_enable_output(struct hda_codec *codec)
{
	/* Enable pin out and unmute */
	snd_hda_sequence_write(codec, pinout_enable_verb);
	if (get_wcaps(codec, PIN_NID) & AC_WCAP_OUT_AMP)
		snd_hda_codec_write(codec, PIN_NID, 0,
				AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);

	/* Enable Audio InfoFrame Transmission */
	hdmi_set_dip_index(codec, PIN_NID, 0x0, 0x0);
	snd_hda_codec_write(codec, PIN_NID, 0, AC_VERB_SET_HDMI_DIP_XMIT,
						AC_DIPXMIT_BEST);
}

static void hdmi_disable_output(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, pinout_disable_verb);
	if (get_wcaps(codec, PIN_NID) & AC_WCAP_OUT_AMP)
		snd_hda_codec_write(codec, PIN_NID, 0,
				AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	/*
	 * FIXME: noises may arise when playing music after reloading the
	 * kernel module, until the next X restart or monitor repower.
	 */
}

static int hdmi_get_channel_count(struct hda_codec *codec)
{
	return 1 + snd_hda_codec_read(codec, CVT_NID, 0,
					AC_VERB_GET_CVT_CHAN_COUNT, 0);
}

static void hdmi_set_channel_count(struct hda_codec *codec, int chs)
{
	snd_hda_codec_write(codec, CVT_NID, 0,
					AC_VERB_SET_CVT_CHAN_COUNT, chs - 1);

	if (chs != hdmi_get_channel_count(codec))
		snd_printd(KERN_INFO "Channel count expect=%d, real=%d\n",
				chs, hdmi_get_channel_count(codec));
}

static void hdmi_debug_slot_mapping(struct hda_codec *codec)
{
#ifdef CONFIG_SND_DEBUG_VERBOSE
	int i;
	int slot;

	for (i = 0; i < 8; i++) {
		slot = snd_hda_codec_read(codec, CVT_NID, 0,
						AC_VERB_GET_HDMI_CHAN_SLOT, i);
		printk(KERN_DEBUG "ASP channel %d => slot %d\n",
				slot >> 4, slot & 0x7);
	}
#endif
}

static void hdmi_setup_channel_mapping(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, def_chan_map);
	hdmi_debug_slot_mapping(codec);
}


/*
 * ELD(EDID Like Data) routines
 */

static int hdmi_present_sense(struct hda_codec *codec, hda_nid_t nid)
{
	return snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_PIN_SENSE, 0);
}

static void hdmi_debug_present_sense(struct hda_codec *codec)
{
#ifdef CONFIG_SND_DEBUG_VERBOSE
	int eldv;
	int present;

	present = hdmi_present_sense(codec, PIN_NID);
	eldv    = (present & AC_PINSENSE_ELDV);
	present = (present & AC_PINSENSE_PRESENCE);

	printk(KERN_INFO "pinp = %d, eldv = %d\n", !!present, !!eldv);
#endif
}

static unsigned char hdmi_get_eld_byte(struct hda_codec *codec, int byte_index)
{
	unsigned int val;

	val = snd_hda_codec_read(codec, PIN_NID, 0,
					AC_VERB_GET_HDMI_ELDD, byte_index);

#ifdef BE_PARANOID
	printk(KERN_INFO "ELD data byte %d: 0x%x\n", byte_index, val);
#endif

	if ((val & AC_ELDD_ELD_VALID) == 0) {
		snd_printd(KERN_INFO "Invalid ELD data byte %d\n",
								byte_index);
		val = 0;
	}

	return val & AC_ELDD_ELD_DATA;
}

static inline unsigned char grab_bits(const unsigned char *buf,
						int byte, int lowbit, int bits)
{
	BUG_ON(lowbit > 7);
	BUG_ON(bits > 8);
	BUG_ON(bits <= 0);

	return (buf[byte] >> lowbit) & ((1 << bits) - 1);
}

static void hdmi_update_short_audio_desc(struct cea_sad *a,
					 const unsigned char *buf)
{
	int i;
	int val;

	val = grab_bits(buf, 1, 0, 7);
	a->rates = 0;
	for (i = 0; i < 7; i++)
		if (val & (1 << i))
			a->rates |= cea_sampling_frequencies[i + 1];

	a->channels = grab_bits(buf, 0, 0, 3);
	a->channels++;

	a->format = grab_bits(buf, 0, 3, 4);
	switch (a->format) {
	case AUDIO_CODING_TYPE_REF_STREAM_HEADER:
		snd_printd(KERN_INFO
				"audio coding type 0 not expected in ELD\n");
		break;

	case AUDIO_CODING_TYPE_LPCM:
		val = grab_bits(buf, 2, 0, 3);
		a->sample_bits = 0;
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
		a->max_bitrate = grab_bits(buf, 2, 0, 8);
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
		a->profile = grab_bits(buf, 2, 0, 3);
		break;

	case AUDIO_CODING_TYPE_REF_CXT:
		a->format = grab_bits(buf, 2, 3, 5);
		if (a->format == AUDIO_CODING_XTYPE_HE_REF_CT ||
		    a->format >= AUDIO_CODING_XTYPE_FIRST_RESERVED) {
			snd_printd(KERN_INFO
				"audio coding xtype %d not expected in ELD\n",
				a->format);
			a->format = 0;
		} else
			a->format += AUDIO_CODING_TYPE_HE_AAC -
				     AUDIO_CODING_XTYPE_HE_AAC;
		break;
	}
}

static int hdmi_update_sink_eld(struct hda_codec *codec,
				const unsigned char *buf, int size)
{
	struct intel_hdmi_spec *spec = codec->spec;
	struct sink_eld *e = &spec->sink;
	int mnl;
	int i;

	e->eld_ver = grab_bits(buf, 0, 3, 5);
	if (e->eld_ver != ELD_VER_CEA_861D &&
	    e->eld_ver != ELD_VER_PARTIAL) {
		snd_printd(KERN_INFO "Unknown ELD version %d\n", e->eld_ver);
		goto out_fail;
	}

	e->eld_size = size;
	e->baseline_len = grab_bits(buf, 2, 0, 8);
	mnl		= grab_bits(buf, 4, 0, 5);
	e->cea_edid_ver	= grab_bits(buf, 4, 5, 3);

	e->support_hdcp	= grab_bits(buf, 5, 0, 1);
	e->support_ai	= grab_bits(buf, 5, 1, 1);
	e->conn_type	= grab_bits(buf, 5, 2, 2);
	e->sad_count	= grab_bits(buf, 5, 4, 4);

	e->aud_synch_delay = grab_bits(buf, 6, 0, 8);
	e->spk_alloc	= grab_bits(buf, 7, 0, 7);

	e->port_id	  = get_unaligned_le64(buf + 8);

	/* not specified, but the spec's tendency is little endian */
	e->manufacture_id = get_unaligned_le16(buf + 16);
	e->product_id	  = get_unaligned_le16(buf + 18);

	if (mnl > ELD_MAX_MNL) {
		snd_printd(KERN_INFO "MNL is reserved value %d\n", mnl);
		goto out_fail;
	} else if (ELD_FIXED_BYTES + mnl > size) {
		snd_printd(KERN_INFO "out of range MNL %d\n", mnl);
		goto out_fail;
	} else
		strlcpy(e->monitor_name, buf + ELD_FIXED_BYTES, mnl);

	for (i = 0; i < e->sad_count; i++) {
		if (ELD_FIXED_BYTES + mnl + 3 * (i + 1) > size) {
			snd_printd(KERN_INFO "out of range SAD %d\n", i);
			goto out_fail;
		}
		hdmi_update_short_audio_desc(e->sad + i,
					buf + ELD_FIXED_BYTES + mnl + 3 * i);
	}

	return 0;

out_fail:
	e->eld_ver = 0;
	return -EINVAL;
}

static int hdmi_get_eld(struct hda_codec *codec)
{
	int i;
	int ret;
	int size;
	unsigned char *buf;

	i = hdmi_present_sense(codec, PIN_NID) & AC_PINSENSE_ELDV;
	if (!i)
		return -ENOENT;

	size = hdmi_get_eld_size(codec, PIN_NID);
	if (size == 0) {
		/* wfg: workaround for ASUS P5E-VM HDMI board */
		snd_printd(KERN_INFO "ELD buf size is 0, force 128\n");
		size = 128;
	}
	if (size < ELD_FIXED_BYTES || size > PAGE_SIZE) {
		snd_printd(KERN_INFO "Invalid ELD buf size %d\n", size);
		return -ERANGE;
	}

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < size; i++)
		buf[i] = hdmi_get_eld_byte(codec, i);

	ret = hdmi_update_sink_eld(codec, buf, size);

	kfree(buf);
	return ret;
}

static void hdmi_show_short_audio_desc(struct cea_sad *a)
{
	printk(KERN_INFO "coding type: %s\n",
					cea_audio_coding_type_names[a->format]);
	printk(KERN_INFO "channels: %d\n", a->channels);
	printk(KERN_INFO "sampling frequencies: 0x%x\n", a->rates);

	if (a->format == AUDIO_CODING_TYPE_LPCM)
		printk(KERN_INFO "sample bits: 0x%x\n", a->sample_bits);

	if (a->max_bitrate)
		printk(KERN_INFO "max bitrate: %d HZ\n", a->max_bitrate);

	if (a->profile)
		printk(KERN_INFO "profile: %d\n", a->profile);
}

static void hdmi_show_eld(struct hda_codec *codec)
{
	int i;
	int j;
	struct intel_hdmi_spec *spec = codec->spec;
	struct sink_eld *e = &spec->sink;
	char buf[80];

	printk(KERN_INFO "ELD buffer size  is %d\n", e->eld_size);
	printk(KERN_INFO "ELD baseline len is %d*4\n", e->baseline_len);
	printk(KERN_INFO "vendor block len is %d\n",
					e->eld_size - e->baseline_len * 4 - 4);
	printk(KERN_INFO "ELD version      is %s\n",
					eld_versoin_names[e->eld_ver]);
	printk(KERN_INFO "CEA EDID version is %s\n",
				cea_edid_version_names[e->cea_edid_ver]);
	printk(KERN_INFO "manufacture id   is 0x%x\n", e->manufacture_id);
	printk(KERN_INFO "product id       is 0x%x\n", e->product_id);
	printk(KERN_INFO "port id          is 0x%llx\n", (long long)e->port_id);
	printk(KERN_INFO "HDCP support     is %d\n", e->support_hdcp);
	printk(KERN_INFO "AI support       is %d\n", e->support_ai);
	printk(KERN_INFO "SAD count        is %d\n", e->sad_count);
	printk(KERN_INFO "audio sync delay is %x\n", e->aud_synch_delay);
	printk(KERN_INFO "connection type  is %s\n",
				eld_connection_type_names[e->conn_type]);
	printk(KERN_INFO "monitor name     is %s\n", e->monitor_name);

	j = 0;
	for (i = 0; i < ARRAY_SIZE(cea_speaker_allocation_names); i++) {
		if (e->spk_alloc & (1 << i))
			j += snprintf(buf + j, sizeof(buf) - j,  " %s",
					cea_speaker_allocation_names[i]);
	}
	buf[j] = '\0'; /* necessary when j == 0 */
	printk(KERN_INFO "speaker allocations: (0x%x)%s\n", e->spk_alloc, buf);

	for (i = 0; i < e->sad_count; i++)
		hdmi_show_short_audio_desc(e->sad + i);
}

/*
 * Be careful, ELD buf could be totally rubbish!
 */
static void hdmi_parse_eld(struct hda_codec *codec)
{
	hdmi_debug_present_sense(codec);

	if (!hdmi_get_eld(codec))
		hdmi_show_eld(codec);
}


/*
 * Audio Infoframe routines
 */

static void hdmi_debug_dip_size(struct hda_codec *codec)
{
#ifdef CONFIG_SND_DEBUG_VERBOSE
	int i;
	int size;

	size = hdmi_get_eld_size(codec, PIN_NID);
	printk(KERN_DEBUG "ELD buf size is %d\n", size);

	for (i = 0; i < 8; i++) {
		size = snd_hda_codec_read(codec, PIN_NID, 0,
						AC_VERB_GET_HDMI_DIP_SIZE, i);
		printk(KERN_DEBUG "DIP GP[%d] buf size is %d\n", i, size);
	}
#endif
}

static void hdmi_clear_dip_buffers(struct hda_codec *codec)
{
#ifdef BE_PARANOID
	int i, j;
	int size;
	int pi, bi;
	for (i = 0; i < 8; i++) {
		size = snd_hda_codec_read(codec, PIN_NID, 0,
						AC_VERB_GET_HDMI_DIP_SIZE, i);
		if (size == 0)
			continue;

		hdmi_set_dip_index(codec, PIN_NID, i, 0x0);
		for (j = 1; j < 1000; j++) {
			hdmi_write_dip_byte(codec, PIN_NID, 0x0);
			hdmi_get_dip_index(codec, PIN_NID, &pi, &bi);
			if (pi != i)
				snd_printd(KERN_INFO "dip index %d: %d != %d\n",
						bi, pi, i);
			if (bi == 0) /* byte index wrapped around */
				break;
		}
		snd_printd(KERN_INFO
				"DIP GP[%d] buf reported size=%d, written=%d\n",
				i, size, j);
	}
#endif
}

static void hdmi_setup_audio_infoframe(struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct hdmi_audio_infoframe audio_infoframe = {
		.type		= 0x84,
		.ver		= 0x01,
		.len		= 0x0a,
		.CC02_CT47	= substream->runtime->channels - 1,
	};
	u8 *params = (u8 *)&audio_infoframe;
	int i;

	hdmi_debug_dip_size(codec);
	hdmi_clear_dip_buffers(codec); /* be paranoid */

	hdmi_set_dip_index(codec, PIN_NID, 0x0, 0x0);
	for (i = 0; i < sizeof(audio_infoframe); i++)
		hdmi_write_dip_byte(codec, PIN_NID, params[i]);
}


/*
 * Unsolicited events
 */

static void hdmi_intrinsic_event(struct hda_codec *codec, unsigned int res)
{
	int pind = !!(res & AC_UNSOL_RES_PD);
	int eldv = !!(res & AC_UNSOL_RES_ELDV);

	printk(KERN_INFO "HDMI intrinsic event: PD=%d ELDV=%d\n", pind, eldv);

	if (pind && eldv) {
		hdmi_parse_eld(codec);
		/* TODO: do real things about ELD */
	}
}

static void hdmi_non_intrinsic_event(struct hda_codec *codec, unsigned int res)
{
	int subtag = (res & AC_UNSOL_RES_SUBTAG) >> AC_UNSOL_RES_SUBTAG_SHIFT;
	int cp_state = !!(res & AC_UNSOL_RES_CP_STATE);
	int cp_ready = !!(res & AC_UNSOL_RES_CP_READY);

	printk(KERN_INFO "HDMI non-intrinsic event: "
			"SUBTAG=0x%x CP_STATE=%d CP_READY=%d\n",
			subtag,
			cp_state,
			cp_ready);

	/* who cares? */
	if (cp_state)
		;
	if (cp_ready)
		;
}


static void intel_hdmi_unsol_event(struct hda_codec *codec, unsigned int res)
{
	int tag = res >> AC_UNSOL_RES_TAG_SHIFT;
	int subtag = (res & AC_UNSOL_RES_SUBTAG) >> AC_UNSOL_RES_SUBTAG_SHIFT;

	if (tag != INTEL_HDMI_EVENT_TAG) {
		snd_printd(KERN_INFO
				"Unexpected HDMI unsolicited event tag 0x%x\n",
				tag);
		return;
	}

	if (subtag == 0)
		hdmi_intrinsic_event(codec, res);
	else
		hdmi_non_intrinsic_event(codec, res);
}

/*
 * Callbacks
 */

static int intel_hdmi_playback_pcm_open(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct intel_hdmi_spec *spec = codec->spec;

	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int intel_hdmi_playback_pcm_close(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct intel_hdmi_spec *spec = codec->spec;

	hdmi_disable_output(codec);

	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int intel_hdmi_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   unsigned int stream_tag,
					   unsigned int format,
					   struct snd_pcm_substream *substream)
{
	struct intel_hdmi_spec *spec = codec->spec;

	snd_hda_multi_out_dig_prepare(codec, &spec->multiout, stream_tag,
					     format, substream);

	hdmi_set_channel_count(codec, substream->runtime->channels);

	/* wfg: channel mapping not supported by DEVCTG */
	hdmi_setup_channel_mapping(codec);

	hdmi_setup_audio_infoframe(codec, substream);

	hdmi_enable_output(codec);

	return 0;
}

static struct hda_pcm_stream intel_hdmi_pcm_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = CVT_NID, /* NID to query formats and rates and setup streams */
	.ops = {
		.open = intel_hdmi_playback_pcm_open,
		.close = intel_hdmi_playback_pcm_close,
		.prepare = intel_hdmi_playback_pcm_prepare
	},
};

static int intel_hdmi_build_pcms(struct hda_codec *codec)
{
	struct intel_hdmi_spec *spec = codec->spec;
	struct hda_pcm *info = &spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "INTEL HDMI";
	info->pcm_type = HDA_PCM_TYPE_HDMI;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = intel_hdmi_pcm_playback;

	return 0;
}

static int intel_hdmi_build_controls(struct hda_codec *codec)
{
	struct intel_hdmi_spec *spec = codec->spec;
	int err;

	err = snd_hda_create_spdif_out_ctls(codec, spec->multiout.dig_out_nid);
	if (err < 0)
		return err;

	return 0;
}

static int intel_hdmi_init(struct hda_codec *codec)
{
	/* disable audio output as early as possible */
	hdmi_disable_output(codec);

	snd_hda_sequence_write(codec, unsolicited_response_verb);

	return 0;
}

static void intel_hdmi_free(struct hda_codec *codec)
{
	kfree(codec->spec);
}

static struct hda_codec_ops intel_hdmi_patch_ops = {
	.init			= intel_hdmi_init,
	.free			= intel_hdmi_free,
	.build_pcms		= intel_hdmi_build_pcms,
	.build_controls 	= intel_hdmi_build_controls,
	.unsol_event		= intel_hdmi_unsol_event,
};

static int patch_intel_hdmi(struct hda_codec *codec)
{
	struct intel_hdmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	spec->multiout.num_dacs = 0;	  /* no analog */
	spec->multiout.max_channels = 8;
	spec->multiout.dig_out_nid = CVT_NID;

	codec->spec = spec;
	codec->patch_ops = intel_hdmi_patch_ops;

	return 0;
}

struct hda_codec_preset snd_hda_preset_intelhdmi[] = {
	{ .id = 0x808629fb, .name = "INTEL G45 DEVCL",  .patch = patch_intel_hdmi },
	{ .id = 0x80862801, .name = "INTEL G45 DEVBLC", .patch = patch_intel_hdmi },
	{ .id = 0x80862802, .name = "INTEL G45 DEVCTG", .patch = patch_intel_hdmi },
	{ .id = 0x80862803, .name = "INTEL G45 DEVELK", .patch = patch_intel_hdmi },
	{} /* terminator */
};
