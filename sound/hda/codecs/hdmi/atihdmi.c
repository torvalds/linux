// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ATI/AMD codec support
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/unaligned.h>
#include <sound/core.h>
#include <sound/tlv.h>
#include <sound/hdaudio.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hdmi_local.h"

#define is_amdhdmi_rev3_or_later(codec) \
	((codec)->core.vendor_id == 0x1002aa01 && \
	 ((codec)->core.revision_id & 0xff00) >= 0x0300)
#define has_amd_full_remap_support(codec) is_amdhdmi_rev3_or_later(codec)

/* ATI/AMD specific HDA pin verbs, see the AMD HDA Verbs specification */
#define ATI_VERB_SET_CHANNEL_ALLOCATION	0x771
#define ATI_VERB_SET_DOWNMIX_INFO	0x772
#define ATI_VERB_SET_MULTICHANNEL_01	0x777
#define ATI_VERB_SET_MULTICHANNEL_23	0x778
#define ATI_VERB_SET_MULTICHANNEL_45	0x779
#define ATI_VERB_SET_MULTICHANNEL_67	0x77a
#define ATI_VERB_SET_HBR_CONTROL	0x77c
#define ATI_VERB_SET_MULTICHANNEL_1	0x785
#define ATI_VERB_SET_MULTICHANNEL_3	0x786
#define ATI_VERB_SET_MULTICHANNEL_5	0x787
#define ATI_VERB_SET_MULTICHANNEL_7	0x788
#define ATI_VERB_SET_MULTICHANNEL_MODE	0x789
#define ATI_VERB_GET_CHANNEL_ALLOCATION	0xf71
#define ATI_VERB_GET_DOWNMIX_INFO	0xf72
#define ATI_VERB_GET_MULTICHANNEL_01	0xf77
#define ATI_VERB_GET_MULTICHANNEL_23	0xf78
#define ATI_VERB_GET_MULTICHANNEL_45	0xf79
#define ATI_VERB_GET_MULTICHANNEL_67	0xf7a
#define ATI_VERB_GET_HBR_CONTROL	0xf7c
#define ATI_VERB_GET_MULTICHANNEL_1	0xf85
#define ATI_VERB_GET_MULTICHANNEL_3	0xf86
#define ATI_VERB_GET_MULTICHANNEL_5	0xf87
#define ATI_VERB_GET_MULTICHANNEL_7	0xf88
#define ATI_VERB_GET_MULTICHANNEL_MODE	0xf89

/* AMD specific HDA cvt verbs */
#define ATI_VERB_SET_RAMP_RATE		0x770
#define ATI_VERB_GET_RAMP_RATE		0xf70

#define ATI_OUT_ENABLE 0x1

#define ATI_MULTICHANNEL_MODE_PAIRED	0
#define ATI_MULTICHANNEL_MODE_SINGLE	1

#define ATI_HBR_CAPABLE 0x01
#define ATI_HBR_ENABLE 0x10

/* ATI/AMD specific ELD emulation */

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

static int get_eld_ati(struct hda_codec *codec, hda_nid_t nid,
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

static int atihdmi_pin_get_eld(struct hda_codec *codec, hda_nid_t nid,
			       int dev_id, unsigned char *buf, int *eld_size)
{
	WARN_ON(dev_id != 0);
	/* call hda_eld.c ATI/AMD-specific function */
	return get_eld_ati(codec, nid, buf, eld_size,
			   is_amdhdmi_rev3_or_later(codec));
}

static void atihdmi_pin_setup_infoframe(struct hda_codec *codec,
					hda_nid_t pin_nid, int dev_id, int ca,
					int active_channels, int conn_type)
{
	WARN_ON(dev_id != 0);
	snd_hda_codec_write(codec, pin_nid, 0, ATI_VERB_SET_CHANNEL_ALLOCATION, ca);
}

static int atihdmi_paired_swap_fc_lfe(int pos)
{
	/*
	 * ATI/AMD have automatic FC/LFE swap built-in
	 * when in pairwise mapping mode.
	 */

	switch (pos) {
	/* see channel_allocations[].speakers[] */
	case 2: return 3;
	case 3: return 2;
	default: return pos;
	}
}

static int atihdmi_paired_chmap_validate(struct hdac_chmap *chmap,
			int ca, int chs, unsigned char *map)
{
	struct hdac_cea_channel_speaker_allocation *cap;
	int i, j;

	/* check that only channel pairs need to be remapped on old pre-rev3 ATI/AMD */

	cap = snd_hdac_get_ch_alloc_from_ca(ca);
	for (i = 0; i < chs; ++i) {
		int mask = snd_hdac_chmap_to_spk_mask(map[i]);
		bool ok = false;
		bool companion_ok = false;

		if (!mask)
			continue;

		for (j = 0 + i % 2; j < 8; j += 2) {
			int chan_idx = 7 - atihdmi_paired_swap_fc_lfe(j);

			if (cap->speakers[chan_idx] == mask) {
				/* channel is in a supported position */
				ok = true;

				if (i % 2 == 0 && i + 1 < chs) {
					/* even channel, check the odd companion */
					int comp_chan_idx = 7 - atihdmi_paired_swap_fc_lfe(j + 1);
					int comp_mask_req = snd_hdac_chmap_to_spk_mask(map[i+1]);
					int comp_mask_act = cap->speakers[comp_chan_idx];

					if (comp_mask_req == comp_mask_act)
						companion_ok = true;
					else
						return -EINVAL;
				}
				break;
			}
		}

		if (!ok)
			return -EINVAL;

		if (companion_ok)
			i++; /* companion channel already checked */
	}

	return 0;
}

static int atihdmi_pin_set_slot_channel(struct hdac_device *hdac,
		hda_nid_t pin_nid, int hdmi_slot, int stream_channel)
{
	struct hda_codec *codec = hdac_to_hda_codec(hdac);
	int verb;
	int ati_channel_setup = 0;

	if (hdmi_slot > 7)
		return -EINVAL;

	if (!has_amd_full_remap_support(codec)) {
		hdmi_slot = atihdmi_paired_swap_fc_lfe(hdmi_slot);

		/* In case this is an odd slot but without stream channel, do not
		 * disable the slot since the corresponding even slot could have a
		 * channel. In case neither have a channel, the slot pair will be
		 * disabled when this function is called for the even slot.
		 */
		if (hdmi_slot % 2 != 0 && stream_channel == 0xf)
			return 0;

		hdmi_slot -= hdmi_slot % 2;

		if (stream_channel != 0xf)
			stream_channel -= stream_channel % 2;
	}

	verb = ATI_VERB_SET_MULTICHANNEL_01 + hdmi_slot/2 + (hdmi_slot % 2) * 0x00e;

	/* ati_channel_setup format: [7..4] = stream_channel_id, [1] = mute, [0] = enable */

	if (stream_channel != 0xf)
		ati_channel_setup = (stream_channel << 4) | ATI_OUT_ENABLE;

	return snd_hda_codec_write(codec, pin_nid, 0, verb, ati_channel_setup);
}

static int atihdmi_pin_get_slot_channel(struct hdac_device *hdac,
				hda_nid_t pin_nid, int asp_slot)
{
	struct hda_codec *codec = hdac_to_hda_codec(hdac);
	bool was_odd = false;
	int ati_asp_slot = asp_slot;
	int verb;
	int ati_channel_setup;

	if (asp_slot > 7)
		return -EINVAL;

	if (!has_amd_full_remap_support(codec)) {
		ati_asp_slot = atihdmi_paired_swap_fc_lfe(asp_slot);
		if (ati_asp_slot % 2 != 0) {
			ati_asp_slot -= 1;
			was_odd = true;
		}
	}

	verb = ATI_VERB_GET_MULTICHANNEL_01 + ati_asp_slot/2 + (ati_asp_slot % 2) * 0x00e;

	ati_channel_setup = snd_hda_codec_read(codec, pin_nid, 0, verb, 0);

	if (!(ati_channel_setup & ATI_OUT_ENABLE))
		return 0xf;

	return ((ati_channel_setup & 0xf0) >> 4) + !!was_odd;
}

static int atihdmi_paired_chmap_cea_alloc_validate_get_type(
		struct hdac_chmap *chmap,
		struct hdac_cea_channel_speaker_allocation *cap,
		int channels)
{
	int c;

	/*
	 * Pre-rev3 ATI/AMD codecs operate in a paired channel mode, so
	 * we need to take that into account (a single channel may take 2
	 * channel slots if we need to carry a silent channel next to it).
	 * On Rev3+ AMD codecs this function is not used.
	 */
	int chanpairs = 0;

	/* We only produce even-numbered channel count TLVs */
	if ((channels % 2) != 0)
		return -1;

	for (c = 0; c < 7; c += 2) {
		if (cap->speakers[c] || cap->speakers[c+1])
			chanpairs++;
	}

	if (chanpairs * 2 != channels)
		return -1;

	return SNDRV_CTL_TLVT_CHMAP_PAIRED;
}

static void atihdmi_paired_cea_alloc_to_tlv_chmap(struct hdac_chmap *hchmap,
		struct hdac_cea_channel_speaker_allocation *cap,
		unsigned int *chmap, int channels)
{
	/* produce paired maps for pre-rev3 ATI/AMD codecs */
	int count = 0;
	int c;

	for (c = 7; c >= 0; c--) {
		int chan = 7 - atihdmi_paired_swap_fc_lfe(7 - c);
		int spk = cap->speakers[chan];

		if (!spk) {
			/* add N/A channel if the companion channel is occupied */
			if (cap->speakers[chan + (chan % 2 ? -1 : 1)])
				chmap[count++] = SNDRV_CHMAP_NA;

			continue;
		}

		chmap[count++] = snd_hdac_spk_to_chmap(spk);
	}

	WARN_ON(count != channels);
}

static int atihdmi_pin_hbr_setup(struct hda_codec *codec, hda_nid_t pin_nid,
				 int dev_id, bool hbr)
{
	int hbr_ctl, hbr_ctl_new;

	WARN_ON(dev_id != 0);

	hbr_ctl = snd_hda_codec_read(codec, pin_nid, 0, ATI_VERB_GET_HBR_CONTROL, 0);
	if (hbr_ctl >= 0 && (hbr_ctl & ATI_HBR_CAPABLE)) {
		if (hbr)
			hbr_ctl_new = hbr_ctl | ATI_HBR_ENABLE;
		else
			hbr_ctl_new = hbr_ctl & ~ATI_HBR_ENABLE;

		codec_dbg(codec,
			  "%s: NID=0x%x, %shbr-ctl=0x%x\n",
			  __func__,
			  pin_nid,
			  hbr_ctl == hbr_ctl_new ? "" : "new-",
			  hbr_ctl_new);

		if (hbr_ctl != hbr_ctl_new)
			snd_hda_codec_write(codec, pin_nid, 0,
						ATI_VERB_SET_HBR_CONTROL,
						hbr_ctl_new);

	} else if (hbr)
		return -EINVAL;

	return 0;
}

static int atihdmi_setup_stream(struct hda_codec *codec, hda_nid_t cvt_nid,
				hda_nid_t pin_nid, int dev_id,
				u32 stream_tag, int format)
{
	if (is_amdhdmi_rev3_or_later(codec)) {
		int ramp_rate = 180; /* default as per AMD spec */
		/* disable ramp-up/down for non-pcm as per AMD spec */
		if (format & AC_FMT_TYPE_NON_PCM)
			ramp_rate = 0;

		snd_hda_codec_write(codec, cvt_nid, 0, ATI_VERB_SET_RAMP_RATE, ramp_rate);
	}

	return snd_hda_hdmi_setup_stream(codec, cvt_nid, pin_nid, dev_id,
					 stream_tag, format);
}


static int atihdmi_init(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx, err;

	err = snd_hda_hdmi_generic_init(codec);

	if (err)
		return err;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);

		/* make sure downmix information in infoframe is zero */
		snd_hda_codec_write(codec, per_pin->pin_nid, 0, ATI_VERB_SET_DOWNMIX_INFO, 0);

		/* enable channel-wise remap mode if supported */
		if (has_amd_full_remap_support(codec))
			snd_hda_codec_write(codec, per_pin->pin_nid, 0,
					    ATI_VERB_SET_MULTICHANNEL_MODE,
					    ATI_MULTICHANNEL_MODE_SINGLE);
	}
	codec->auto_runtime_pm = 1;

	return 0;
}

/* map from pin NID to port; port is 0-based */
/* for AMD: assume widget NID starting from 3, with step 2 (3, 5, 7, ...) */
static int atihdmi_pin2port(void *audio_ptr, int pin_nid)
{
	return pin_nid / 2 - 1;
}

/* reverse-map from port to pin NID: see above */
static int atihdmi_port2pin(struct hda_codec *codec, int port)
{
	return port * 2 + 3;
}

static const struct drm_audio_component_audio_ops atihdmi_audio_ops = {
	.pin2port = atihdmi_pin2port,
	.pin_eld_notify = snd_hda_hdmi_acomp_pin_eld_notify,
	.master_bind = snd_hda_hdmi_acomp_master_bind,
	.master_unbind = snd_hda_hdmi_acomp_master_unbind,
};

static int atihdmi_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	struct hdmi_spec *spec;
	struct hdmi_spec_per_cvt *per_cvt;
	int err, cvt_idx;

	err = snd_hda_hdmi_generic_probe(codec);
	if (err)
		return err;

	spec = codec->spec;

	spec->static_pcm_mapping = true;

	spec->ops.pin_get_eld = atihdmi_pin_get_eld;
	spec->ops.pin_setup_infoframe = atihdmi_pin_setup_infoframe;
	spec->ops.pin_hbr_setup = atihdmi_pin_hbr_setup;
	spec->ops.setup_stream = atihdmi_setup_stream;

	spec->chmap.ops.pin_get_slot_channel = atihdmi_pin_get_slot_channel;
	spec->chmap.ops.pin_set_slot_channel = atihdmi_pin_set_slot_channel;

	if (!has_amd_full_remap_support(codec)) {
		/* override to ATI/AMD-specific versions with pairwise mapping */
		spec->chmap.ops.chmap_cea_alloc_validate_get_type =
			atihdmi_paired_chmap_cea_alloc_validate_get_type;
		spec->chmap.ops.cea_alloc_to_tlv_chmap =
				atihdmi_paired_cea_alloc_to_tlv_chmap;
		spec->chmap.ops.chmap_validate = atihdmi_paired_chmap_validate;
	}

	/* ATI/AMD converters do not advertise all of their capabilities */
	for (cvt_idx = 0; cvt_idx < spec->num_cvts; cvt_idx++) {
		per_cvt = get_cvt(spec, cvt_idx);
		per_cvt->channels_max = max(per_cvt->channels_max, 8u);
		per_cvt->rates |= SUPPORTED_RATES;
		per_cvt->formats |= SUPPORTED_FORMATS;
		per_cvt->maxbps = max(per_cvt->maxbps, 24u);
	}

	spec->chmap.channels_max = max(spec->chmap.channels_max, 8u);

	/* AMD GPUs have neither EPSS nor CLKSTOP bits, hence preventing
	 * the link-down as is.  Tell the core to allow it.
	 */
	codec->link_down_at_suspend = 1;

	snd_hda_hdmi_acomp_init(codec, &atihdmi_audio_ops, atihdmi_port2pin);

	return 0;
}

static const struct hda_codec_ops atihdmi_codec_ops = {
	.probe = atihdmi_probe,
	.remove = snd_hda_hdmi_generic_remove,
	.init = atihdmi_init,
	.build_pcms = snd_hda_hdmi_generic_build_pcms,
	.build_controls = snd_hda_hdmi_generic_build_controls,
	.unsol_event = snd_hda_hdmi_generic_unsol_event,
	.suspend = snd_hda_hdmi_generic_suspend,
	.resume	 = snd_hda_hdmi_generic_resume,
};

/*
 * driver entries
 */
static const struct hda_device_id snd_hda_id_atihdmi[] = {
	HDA_CODEC_ID(0x1002793c, "RS600 HDMI"),
	HDA_CODEC_ID(0x10027919, "RS600 HDMI"),
	HDA_CODEC_ID(0x1002791a, "RS690/780 HDMI"),
	HDA_CODEC_ID(0x1002aa01, "R6xx HDMI"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_atihdmi);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD/ATI HDMI HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_HDMI");

static struct hda_codec_driver atihdmi_driver = {
	.id = snd_hda_id_atihdmi,
	.ops = &atihdmi_codec_ops,
};

module_hda_codec_driver(atihdmi_driver);
