// SPDX-License-Identifier: GPL-2.0-only
/*
 *  PCM DRM helpers
 */
#include <linux/bitfield.h>
#include <linux/export.h>
#include <linux/hdmi.h>
#include <linux/unaligned.h>
#include <drm/drm_edid.h>
#include <drm/drm_eld.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_drm_eld.h>

#define SAD0_CHANNELS_MASK	GENMASK(2, 0) /* max number of channels - 1 */
#define SAD0_FORMAT_MASK	GENMASK(6, 3) /* audio format */

#define SAD1_RATE_MASK		GENMASK(6, 0) /* bitfield of supported rates */
#define SAD1_RATE_32000_MASK	BIT(0)
#define SAD1_RATE_44100_MASK	BIT(1)
#define SAD1_RATE_48000_MASK	BIT(2)
#define SAD1_RATE_88200_MASK	BIT(3)
#define SAD1_RATE_96000_MASK	BIT(4)
#define SAD1_RATE_176400_MASK	BIT(5)
#define SAD1_RATE_192000_MASK	BIT(6)

static const unsigned int eld_rates[] = {
	32000,
	44100,
	48000,
	88200,
	96000,
	176400,
	192000,
};

static unsigned int map_rate_families(const u8 *sad,
				      unsigned int mask_32000,
				      unsigned int mask_44100,
				      unsigned int mask_48000)
{
	unsigned int rate_mask = 0;

	if (sad[1] & SAD1_RATE_32000_MASK)
		rate_mask |= mask_32000;
	if (sad[1] & (SAD1_RATE_44100_MASK | SAD1_RATE_88200_MASK | SAD1_RATE_176400_MASK))
		rate_mask |= mask_44100;
	if (sad[1] & (SAD1_RATE_48000_MASK | SAD1_RATE_96000_MASK | SAD1_RATE_192000_MASK))
		rate_mask |= mask_48000;
	return rate_mask;
}

static unsigned int sad_rate_mask(const u8 *sad)
{
	switch (FIELD_GET(SAD0_FORMAT_MASK, sad[0])) {
	case HDMI_AUDIO_CODING_TYPE_PCM:
		return sad[1] & SAD1_RATE_MASK;
	case HDMI_AUDIO_CODING_TYPE_AC3:
	case HDMI_AUDIO_CODING_TYPE_DTS:
		return map_rate_families(sad,
					 SAD1_RATE_32000_MASK,
					 SAD1_RATE_44100_MASK,
					 SAD1_RATE_48000_MASK);
	case HDMI_AUDIO_CODING_TYPE_EAC3:
	case HDMI_AUDIO_CODING_TYPE_DTS_HD:
	case HDMI_AUDIO_CODING_TYPE_MLP:
		return map_rate_families(sad,
					 0,
					 SAD1_RATE_176400_MASK,
					 SAD1_RATE_192000_MASK);
	default:
		/* TODO adjust for other compressed formats as well */
		return sad[1] & SAD1_RATE_MASK;
	}
}

static unsigned int sad_max_channels(const u8 *sad)
{
	switch (FIELD_GET(SAD0_FORMAT_MASK, sad[0])) {
	case HDMI_AUDIO_CODING_TYPE_PCM:
		return 1 + FIELD_GET(SAD0_CHANNELS_MASK, sad[0]);
	case HDMI_AUDIO_CODING_TYPE_AC3:
	case HDMI_AUDIO_CODING_TYPE_DTS:
	case HDMI_AUDIO_CODING_TYPE_EAC3:
		return 2;
	case HDMI_AUDIO_CODING_TYPE_DTS_HD:
	case HDMI_AUDIO_CODING_TYPE_MLP:
		return 8;
	default:
		/* TODO adjust for other compressed formats as well */
		return 1 + FIELD_GET(SAD0_CHANNELS_MASK, sad[0]);
	}
}

static int eld_limit_rates(struct snd_pcm_hw_params *params,
			   struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *r = hw_param_interval(params, rule->var);
	const struct snd_interval *c;
	unsigned int rate_mask = 7, i;
	const u8 *sad, *eld = rule->private;

	sad = drm_eld_sad(eld);
	if (sad) {
		c = hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);

		for (i = drm_eld_sad_count(eld); i > 0; i--, sad += 3) {
			unsigned max_channels = sad_max_channels(sad);

			/*
			 * Exclude SADs which do not include the
			 * requested number of channels.
			 */
			if (c->min <= max_channels)
				rate_mask |= sad_rate_mask(sad);
		}
	}

	return snd_interval_list(r, ARRAY_SIZE(eld_rates), eld_rates,
				 rate_mask);
}

static int eld_limit_channels(struct snd_pcm_hw_params *params,
			      struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *c = hw_param_interval(params, rule->var);
	const struct snd_interval *r;
	struct snd_interval t = { .min = 1, .max = 2, .integer = 1, };
	unsigned int i;
	const u8 *sad, *eld = rule->private;

	sad = drm_eld_sad(eld);
	if (sad) {
		unsigned int rate_mask = 0;

		/* Convert the rate interval to a mask */
		r = hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
		for (i = 0; i < ARRAY_SIZE(eld_rates); i++)
			if (r->min <= eld_rates[i] && r->max >= eld_rates[i])
				rate_mask |= BIT(i);

		for (i = drm_eld_sad_count(eld); i > 0; i--, sad += 3)
			if (rate_mask & sad_rate_mask(sad))
				t.max = max(t.max, sad_max_channels(sad));
	}

	return snd_interval_refine(c, &t);
}

int snd_pcm_hw_constraint_eld(struct snd_pcm_runtime *runtime, void *eld)
{
	int ret;

	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  eld_limit_rates, eld,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (ret < 0)
		return ret;

	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  eld_limit_channels, eld,
				  SNDRV_PCM_HW_PARAM_RATE, -1);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_pcm_hw_constraint_eld);

#define SND_PRINT_RATES_ADVISED_BUFSIZE	80
#define SND_PRINT_BITS_ADVISED_BUFSIZE	16
#define SND_PRINT_CHANNEL_ALLOCATION_ADVISED_BUFSIZE 80

static const char * const eld_connection_type_names[4] = {
	"HDMI",
	"DisplayPort",
	"2-reserved",
	"3-reserved"
};

static const char * const cea_audio_coding_type_names[] = {
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

static const char * const cea_speaker_allocation_names[] = {
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

/*
 * SS1:SS0 index => sample size
 */
static const int cea_sample_sizes[4] = {
	0,			/* 0: Refer to Stream Header */
	ELD_PCM_BITS_16,	/* 1: 16 bits */
	ELD_PCM_BITS_20,	/* 2: 20 bits */
	ELD_PCM_BITS_24,	/* 3: 24 bits */
};

/*
 * SF2:SF1:SF0 index => sampling frequency
 */
static const int cea_sampling_frequencies[8] = {
	0,			/* 0: Refer to Stream Header */
	SNDRV_PCM_RATE_32000,	/* 1:  32000Hz */
	SNDRV_PCM_RATE_44100,	/* 2:  44100Hz */
	SNDRV_PCM_RATE_48000,	/* 3:  48000Hz */
	SNDRV_PCM_RATE_88200,	/* 4:  88200Hz */
	SNDRV_PCM_RATE_96000,	/* 5:  96000Hz */
	SNDRV_PCM_RATE_176400,	/* 6: 176400Hz */
	SNDRV_PCM_RATE_192000,	/* 7: 192000Hz */
};

#define GRAB_BITS(buf, byte, lowbit, bits)		\
({							\
	BUILD_BUG_ON(lowbit > 7);			\
	BUILD_BUG_ON(bits > 8);				\
	BUILD_BUG_ON(bits <= 0);			\
							\
	(buf[byte] >> (lowbit)) & ((1 << (bits)) - 1);	\
})

static void hdmi_update_short_audio_desc(struct device *dev,
					 struct snd_cea_sad *a,
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
		dev_info(dev, "HDMI: audio coding type 0 not expected\n");
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
			dev_info(dev,
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
int snd_parse_eld(struct device *dev, struct snd_parsed_hdmi_eld *e,
		  const unsigned char *buf, int size)
{
	int mnl;
	int i;

	memset(e, 0, sizeof(*e));
	e->eld_ver = GRAB_BITS(buf, 0, 3, 5);
	if (e->eld_ver != ELD_VER_CEA_861D &&
	    e->eld_ver != ELD_VER_PARTIAL) {
		dev_info(dev, "HDMI: Unknown ELD version %d\n", e->eld_ver);
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
		dev_info(dev, "HDMI: MNL is reserved value %d\n", mnl);
		goto out_fail;
	} else if (ELD_FIXED_BYTES + mnl > size) {
		dev_info(dev, "HDMI: out of range MNL %d\n", mnl);
		goto out_fail;
	} else
		strscpy(e->monitor_name, buf + ELD_FIXED_BYTES, mnl + 1);

	for (i = 0; i < e->sad_count; i++) {
		if (ELD_FIXED_BYTES + mnl + 3 * (i + 1) > size) {
			dev_info(dev, "HDMI: out of range SAD %d\n", i);
			goto out_fail;
		}
		hdmi_update_short_audio_desc(dev, e->sad + i,
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
EXPORT_SYMBOL_GPL(snd_parse_eld);

/*
 * SNDRV_PCM_RATE_* and AC_PAR_PCM values don't match, print correct rates with
 * hdmi-specific routine.
 */
static void hdmi_print_pcm_rates(int pcm, char *buf, int buflen)
{
	static const unsigned int alsa_rates[] = {
		5512, 8000, 11025, 16000, 22050, 32000, 44100, 48000, 64000,
		88200, 96000, 176400, 192000, 384000
	};
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(alsa_rates); i++)
		if (pcm & (1 << i))
			j += scnprintf(buf + j, buflen - j,  " %d",
				alsa_rates[i]);

	buf[j] = '\0'; /* necessary when j == 0 */
}

static void eld_print_pcm_bits(int pcm, char *buf, int buflen)
{
	static const unsigned int bits[] = { 8, 16, 20, 24, 32 };
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(bits); i++)
		if (pcm & (ELD_PCM_BITS_8 << i))
			j += scnprintf(buf + j, buflen - j,  " %d", bits[i]);

	buf[j] = '\0'; /* necessary when j == 0 */
}

static void hdmi_show_short_audio_desc(struct device *dev,
				       struct snd_cea_sad *a)
{
	char buf[SND_PRINT_RATES_ADVISED_BUFSIZE];
	char buf2[8 + SND_PRINT_BITS_ADVISED_BUFSIZE] = ", bits =";

	if (!a->format)
		return;

	hdmi_print_pcm_rates(a->rates, buf, sizeof(buf));

	if (a->format == AUDIO_CODING_TYPE_LPCM)
		eld_print_pcm_bits(a->sample_bits, buf2 + 8, sizeof(buf2) - 8);
	else if (a->max_bitrate)
		snprintf(buf2, sizeof(buf2),
				", max bitrate = %d", a->max_bitrate);
	else
		buf2[0] = '\0';

	dev_dbg(dev,
		"HDMI: supports coding type %s: channels = %d, rates =%s%s\n",
		cea_audio_coding_type_names[a->format],
		a->channels, buf, buf2);
}

static void snd_eld_print_channel_allocation(int spk_alloc, char *buf, int buflen)
{
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(cea_speaker_allocation_names); i++) {
		if (spk_alloc & (1 << i))
			j += scnprintf(buf + j, buflen - j,  " %s",
					cea_speaker_allocation_names[i]);
	}
	buf[j] = '\0';	/* necessary when j == 0 */
}

void snd_show_eld(struct device *dev, struct snd_parsed_hdmi_eld *e)
{
	int i;

	dev_dbg(dev, "HDMI: detected monitor %s at connection type %s\n",
		e->monitor_name,
		eld_connection_type_names[e->conn_type]);

	if (e->spk_alloc) {
		char buf[SND_PRINT_CHANNEL_ALLOCATION_ADVISED_BUFSIZE];

		snd_eld_print_channel_allocation(e->spk_alloc, buf, sizeof(buf));
		dev_dbg(dev, "HDMI: available speakers:%s\n", buf);
	}

	for (i = 0; i < e->sad_count; i++)
		hdmi_show_short_audio_desc(dev, e->sad + i);
}
EXPORT_SYMBOL_GPL(snd_show_eld);

#ifdef CONFIG_SND_PROC_FS
static void hdmi_print_sad_info(int i, struct snd_cea_sad *a,
				struct snd_info_buffer *buffer)
{
	char buf[SND_PRINT_RATES_ADVISED_BUFSIZE];

	snd_iprintf(buffer, "sad%d_coding_type\t[0x%x] %s\n",
			i, a->format, cea_audio_coding_type_names[a->format]);
	snd_iprintf(buffer, "sad%d_channels\t\t%d\n", i, a->channels);

	hdmi_print_pcm_rates(a->rates, buf, sizeof(buf));
	snd_iprintf(buffer, "sad%d_rates\t\t[0x%x]%s\n", i, a->rates, buf);

	if (a->format == AUDIO_CODING_TYPE_LPCM) {
		eld_print_pcm_bits(a->sample_bits, buf, sizeof(buf));
		snd_iprintf(buffer, "sad%d_bits\t\t[0x%x]%s\n",
							i, a->sample_bits, buf);
	}

	if (a->max_bitrate)
		snd_iprintf(buffer, "sad%d_max_bitrate\t%d\n",
							i, a->max_bitrate);

	if (a->profile)
		snd_iprintf(buffer, "sad%d_profile\t\t%d\n", i, a->profile);
}

void snd_print_eld_info(struct snd_parsed_hdmi_eld *e,
			struct snd_info_buffer *buffer)
{
	char buf[SND_PRINT_CHANNEL_ALLOCATION_ADVISED_BUFSIZE];
	int i;
	static const char * const eld_version_names[32] = {
		"reserved",
		"reserved",
		"CEA-861D or below",
		[3 ... 30] = "reserved",
		[31] = "partial"
	};
	static const char * const cea_edid_version_names[8] = {
		"no CEA EDID Timing Extension block present",
		"CEA-861",
		"CEA-861-A",
		"CEA-861-B, C or D",
		[4 ... 7] = "reserved"
	};

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

	snd_eld_print_channel_allocation(e->spk_alloc, buf, sizeof(buf));
	snd_iprintf(buffer, "speakers\t\t[0x%x]%s\n", e->spk_alloc, buf);

	snd_iprintf(buffer, "sad_count\t\t%d\n", e->sad_count);

	for (i = 0; i < e->sad_count; i++)
		hdmi_print_sad_info(i, e->sad + i, buffer);
}
EXPORT_SYMBOL_GPL(snd_print_eld_info);
#endif /* CONFIG_SND_PROC_FS */
