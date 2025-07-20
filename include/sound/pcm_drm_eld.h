/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOUND_PCM_DRM_ELD_H
#define __SOUND_PCM_DRM_ELD_H

enum eld_versions {
	ELD_VER_CEA_861D	= 2,
	ELD_VER_PARTIAL		= 31,
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

/*
 * CEA Short Audio Descriptor data
 */
struct snd_cea_sad {
	int	channels;
	int	format;		/* (format == 0) indicates invalid SAD */
	int	rates;
	int	sample_bits;	/* for LPCM */
	int	max_bitrate;	/* for AC3...ATRAC */
	int	profile;	/* for WMAPRO */
};

#define ELD_FIXED_BYTES	20
#define ELD_MAX_SIZE    256
#define ELD_MAX_MNL	16
#define ELD_MAX_SAD	16

#define ELD_PCM_BITS_8		BIT(0)
#define ELD_PCM_BITS_16		BIT(1)
#define ELD_PCM_BITS_20		BIT(2)
#define ELD_PCM_BITS_24		BIT(3)
#define ELD_PCM_BITS_32		BIT(4)

/*
 * ELD: EDID Like Data
 */
struct snd_parsed_hdmi_eld {
	/*
	 * all fields will be cleared before updating ELD
	 */
	int	baseline_len;
	int	eld_ver;
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
	struct snd_cea_sad sad[ELD_MAX_SAD];
};

int snd_pcm_hw_constraint_eld(struct snd_pcm_runtime *runtime, void *eld);

int snd_parse_eld(struct device *dev, struct snd_parsed_hdmi_eld *e,
		  const unsigned char *buf, int size);
void snd_show_eld(struct device *dev, struct snd_parsed_hdmi_eld *e);

#ifdef CONFIG_SND_PROC_FS
void snd_print_eld_info(struct snd_parsed_hdmi_eld *eld,
			struct snd_info_buffer *buffer);
#endif

#endif
