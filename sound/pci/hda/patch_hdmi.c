/*
 *
 *  patch_hdmi.c - routines for HDMI/DisplayPort codecs
 *
 *  Copyright(c) 2008-2010 Intel Corporation. All rights reserved.
 *  Copyright (c) 2006 ATI Technologies Inc.
 *  Copyright (c) 2008 NVIDIA Corp.  All rights reserved.
 *  Copyright (c) 2008 Wei Ni <wni@nvidia.com>
 *
 *  Authors:
 *			Wu Fengguang <wfg@linux.intel.com>
 *
 *  Maintained by:
 *			Wu Fengguang <wfg@linux.intel.com>
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
#include <linux/module.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/asoundef.h>
#include <sound/tlv.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_jack.h"

static bool static_hdmi_pcm;
module_param(static_hdmi_pcm, bool, 0644);
MODULE_PARM_DESC(static_hdmi_pcm, "Don't restrict PCM parameters per ELD info");

/*
 * The HDMI/DisplayPort configuration can be highly dynamic. A graphics device
 * could support N independent pipes, each of them can be connected to one or
 * more ports (DVI, HDMI or DisplayPort).
 *
 * The HDA correspondence of pipes/ports are converter/pin nodes.
 */
#define MAX_HDMI_CVTS	8
#define MAX_HDMI_PINS	8

struct hdmi_spec_per_cvt {
	hda_nid_t cvt_nid;
	int assigned;
	unsigned int channels_min;
	unsigned int channels_max;
	u32 rates;
	u64 formats;
	unsigned int maxbps;
};

struct hdmi_spec_per_pin {
	hda_nid_t pin_nid;
	int num_mux_nids;
	hda_nid_t mux_nids[HDA_MAX_CONNECTIONS];

	struct hda_codec *codec;
	struct hdmi_eld sink_eld;
	struct delayed_work work;
	int repoll_count;
	bool non_pcm;
	bool chmap_set;		/* channel-map override by ALSA API? */
	unsigned char chmap[8]; /* ALSA API channel-map */
};

struct hdmi_spec {
	int num_cvts;
	struct hdmi_spec_per_cvt cvts[MAX_HDMI_CVTS];

	int num_pins;
	struct hdmi_spec_per_pin pins[MAX_HDMI_PINS];
	struct hda_pcm pcm_rec[MAX_HDMI_PINS];
	unsigned int channels_max; /* max over all cvts */

	/*
	 * Non-generic ATI/NVIDIA specific
	 */
	struct hda_multi_out multiout;
	struct hda_pcm_stream pcm_playback;
};


struct hdmi_audio_infoframe {
	u8 type; /* 0x84 */
	u8 ver;  /* 0x01 */
	u8 len;  /* 0x0a */

	u8 checksum;

	u8 CC02_CT47;	/* CC in bits 0:2, CT in 4:7 */
	u8 SS01_SF24;
	u8 CXT04;
	u8 CA;
	u8 LFEPBL01_LSV36_DM_INH7;
};

struct dp_audio_infoframe {
	u8 type; /* 0x84 */
	u8 len;  /* 0x1b */
	u8 ver;  /* 0x11 << 2 */

	u8 CC02_CT47;	/* match with HDMI infoframe from this on */
	u8 SS01_SF24;
	u8 CXT04;
	u8 CA;
	u8 LFEPBL01_LSV36_DM_INH7;
};

union audio_infoframe {
	struct hdmi_audio_infoframe hdmi;
	struct dp_audio_infoframe dp;
	u8 bytes[0];
};

/*
 * CEA speaker placement:
 *
 *        FLH       FCH        FRH
 *  FLW    FL  FLC   FC   FRC   FR   FRW
 *
 *                                  LFE
 *                     TC
 *
 *          RL  RLC   RC   RRC   RR
 *
 * The Left/Right Surround channel _notions_ LS/RS in SMPTE 320M corresponds to
 * CEA RL/RR; The SMPTE channel _assignment_ C/LFE is swapped to CEA LFE/FC.
 */
enum cea_speaker_placement {
	FL  = (1 <<  0),	/* Front Left           */
	FC  = (1 <<  1),	/* Front Center         */
	FR  = (1 <<  2),	/* Front Right          */
	FLC = (1 <<  3),	/* Front Left Center    */
	FRC = (1 <<  4),	/* Front Right Center   */
	RL  = (1 <<  5),	/* Rear Left            */
	RC  = (1 <<  6),	/* Rear Center          */
	RR  = (1 <<  7),	/* Rear Right           */
	RLC = (1 <<  8),	/* Rear Left Center     */
	RRC = (1 <<  9),	/* Rear Right Center    */
	LFE = (1 << 10),	/* Low Frequency Effect */
	FLW = (1 << 11),	/* Front Left Wide      */
	FRW = (1 << 12),	/* Front Right Wide     */
	FLH = (1 << 13),	/* Front Left High      */
	FCH = (1 << 14),	/* Front Center High    */
	FRH = (1 << 15),	/* Front Right High     */
	TC  = (1 << 16),	/* Top Center           */
};

/*
 * ELD SA bits in the CEA Speaker Allocation data block
 */
static int eld_speaker_allocation_bits[] = {
	[0] = FL | FR,
	[1] = LFE,
	[2] = FC,
	[3] = RL | RR,
	[4] = RC,
	[5] = FLC | FRC,
	[6] = RLC | RRC,
	/* the following are not defined in ELD yet */
	[7] = FLW | FRW,
	[8] = FLH | FRH,
	[9] = TC,
	[10] = FCH,
};

struct cea_channel_speaker_allocation {
	int ca_index;
	int speakers[8];

	/* derived values, just for convenience */
	int channels;
	int spk_mask;
};

/*
 * ALSA sequence is:
 *
 *       surround40   surround41   surround50   surround51   surround71
 * ch0   front left   =            =            =            =
 * ch1   front right  =            =            =            =
 * ch2   rear left    =            =            =            =
 * ch3   rear right   =            =            =            =
 * ch4                LFE          center       center       center
 * ch5                                          LFE          LFE
 * ch6                                                       side left
 * ch7                                                       side right
 *
 * surround71 = {FL, FR, RLC, RRC, FC, LFE, RL, RR}
 */
static int hdmi_channel_mapping[0x32][8] = {
	/* stereo */
	[0x00] = { 0x00, 0x11, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7 },
	/* 2.1 */
	[0x01] = { 0x00, 0x11, 0x22, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7 },
	/* Dolby Surround */
	[0x02] = { 0x00, 0x11, 0x23, 0xf2, 0xf4, 0xf5, 0xf6, 0xf7 },
	/* surround40 */
	[0x08] = { 0x00, 0x11, 0x24, 0x35, 0xf3, 0xf2, 0xf6, 0xf7 },
	/* 4ch */
	[0x03] = { 0x00, 0x11, 0x23, 0x32, 0x44, 0xf5, 0xf6, 0xf7 },
	/* surround41 */
	[0x09] = { 0x00, 0x11, 0x24, 0x35, 0x42, 0xf3, 0xf6, 0xf7 },
	/* surround50 */
	[0x0a] = { 0x00, 0x11, 0x24, 0x35, 0x43, 0xf2, 0xf6, 0xf7 },
	/* surround51 */
	[0x0b] = { 0x00, 0x11, 0x24, 0x35, 0x43, 0x52, 0xf6, 0xf7 },
	/* 7.1 */
	[0x13] = { 0x00, 0x11, 0x26, 0x37, 0x43, 0x52, 0x64, 0x75 },
};

/*
 * This is an ordered list!
 *
 * The preceding ones have better chances to be selected by
 * hdmi_channel_allocation().
 */
static struct cea_channel_speaker_allocation channel_allocations[] = {
/*			  channel:   7     6    5    4    3     2    1    0  */
{ .ca_index = 0x00,  .speakers = {   0,    0,   0,   0,   0,    0,  FR,  FL } },
				 /* 2.1 */
{ .ca_index = 0x01,  .speakers = {   0,    0,   0,   0,   0,  LFE,  FR,  FL } },
				 /* Dolby Surround */
{ .ca_index = 0x02,  .speakers = {   0,    0,   0,   0,  FC,    0,  FR,  FL } },
				 /* surround40 */
{ .ca_index = 0x08,  .speakers = {   0,    0,  RR,  RL,   0,    0,  FR,  FL } },
				 /* surround41 */
{ .ca_index = 0x09,  .speakers = {   0,    0,  RR,  RL,   0,  LFE,  FR,  FL } },
				 /* surround50 */
{ .ca_index = 0x0a,  .speakers = {   0,    0,  RR,  RL,  FC,    0,  FR,  FL } },
				 /* surround51 */
{ .ca_index = 0x0b,  .speakers = {   0,    0,  RR,  RL,  FC,  LFE,  FR,  FL } },
				 /* 6.1 */
{ .ca_index = 0x0f,  .speakers = {   0,   RC,  RR,  RL,  FC,  LFE,  FR,  FL } },
				 /* surround71 */
{ .ca_index = 0x13,  .speakers = { RRC,  RLC,  RR,  RL,  FC,  LFE,  FR,  FL } },

{ .ca_index = 0x03,  .speakers = {   0,    0,   0,   0,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x04,  .speakers = {   0,    0,   0,  RC,   0,    0,  FR,  FL } },
{ .ca_index = 0x05,  .speakers = {   0,    0,   0,  RC,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x06,  .speakers = {   0,    0,   0,  RC,  FC,    0,  FR,  FL } },
{ .ca_index = 0x07,  .speakers = {   0,    0,   0,  RC,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x0c,  .speakers = {   0,   RC,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x0d,  .speakers = {   0,   RC,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x0e,  .speakers = {   0,   RC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x10,  .speakers = { RRC,  RLC,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x11,  .speakers = { RRC,  RLC,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x12,  .speakers = { RRC,  RLC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x14,  .speakers = { FRC,  FLC,   0,   0,   0,    0,  FR,  FL } },
{ .ca_index = 0x15,  .speakers = { FRC,  FLC,   0,   0,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x16,  .speakers = { FRC,  FLC,   0,   0,  FC,    0,  FR,  FL } },
{ .ca_index = 0x17,  .speakers = { FRC,  FLC,   0,   0,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x18,  .speakers = { FRC,  FLC,   0,  RC,   0,    0,  FR,  FL } },
{ .ca_index = 0x19,  .speakers = { FRC,  FLC,   0,  RC,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x1a,  .speakers = { FRC,  FLC,   0,  RC,  FC,    0,  FR,  FL } },
{ .ca_index = 0x1b,  .speakers = { FRC,  FLC,   0,  RC,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x1c,  .speakers = { FRC,  FLC,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x1d,  .speakers = { FRC,  FLC,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x1e,  .speakers = { FRC,  FLC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x1f,  .speakers = { FRC,  FLC,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x20,  .speakers = {   0,  FCH,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x21,  .speakers = {   0,  FCH,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x22,  .speakers = {  TC,    0,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x23,  .speakers = {  TC,    0,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x24,  .speakers = { FRH,  FLH,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x25,  .speakers = { FRH,  FLH,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x26,  .speakers = { FRW,  FLW,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x27,  .speakers = { FRW,  FLW,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x28,  .speakers = {  TC,   RC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x29,  .speakers = {  TC,   RC,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x2a,  .speakers = { FCH,   RC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x2b,  .speakers = { FCH,   RC,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x2c,  .speakers = {  TC,  FCH,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x2d,  .speakers = {  TC,  FCH,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x2e,  .speakers = { FRH,  FLH,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x2f,  .speakers = { FRH,  FLH,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x30,  .speakers = { FRW,  FLW,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x31,  .speakers = { FRW,  FLW,  RR,  RL,  FC,  LFE,  FR,  FL } },
};


/*
 * HDMI routines
 */

static int pin_nid_to_pin_index(struct hdmi_spec *spec, hda_nid_t pin_nid)
{
	int pin_idx;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++)
		if (spec->pins[pin_idx].pin_nid == pin_nid)
			return pin_idx;

	snd_printk(KERN_WARNING "HDMI: pin nid %d not registered\n", pin_nid);
	return -EINVAL;
}

static int hinfo_to_pin_index(struct hdmi_spec *spec,
			      struct hda_pcm_stream *hinfo)
{
	int pin_idx;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++)
		if (&spec->pcm_rec[pin_idx].stream[0] == hinfo)
			return pin_idx;

	snd_printk(KERN_WARNING "HDMI: hinfo %p not registered\n", hinfo);
	return -EINVAL;
}

static int cvt_nid_to_cvt_index(struct hdmi_spec *spec, hda_nid_t cvt_nid)
{
	int cvt_idx;

	for (cvt_idx = 0; cvt_idx < spec->num_cvts; cvt_idx++)
		if (spec->cvts[cvt_idx].cvt_nid == cvt_nid)
			return cvt_idx;

	snd_printk(KERN_WARNING "HDMI: cvt nid %d not registered\n", cvt_nid);
	return -EINVAL;
}

static int hdmi_eld_ctl_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hdmi_spec *spec;
	int pin_idx;

	spec = codec->spec;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;

	pin_idx = kcontrol->private_value;
	uinfo->count = spec->pins[pin_idx].sink_eld.eld_size;

	return 0;
}

static int hdmi_eld_ctl_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hdmi_spec *spec;
	int pin_idx;

	spec = codec->spec;
	pin_idx = kcontrol->private_value;

	memcpy(ucontrol->value.bytes.data,
		spec->pins[pin_idx].sink_eld.eld_buffer, ELD_MAX_SIZE);

	return 0;
}

static struct snd_kcontrol_new eld_bytes_ctl = {
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name = "ELD",
	.info = hdmi_eld_ctl_info,
	.get = hdmi_eld_ctl_get,
};

static int hdmi_create_eld_ctl(struct hda_codec *codec, int pin_idx,
			int device)
{
	struct snd_kcontrol *kctl;
	struct hdmi_spec *spec = codec->spec;
	int err;

	kctl = snd_ctl_new1(&eld_bytes_ctl, codec);
	if (!kctl)
		return -ENOMEM;
	kctl->private_value = pin_idx;
	kctl->id.device = device;

	err = snd_hda_ctl_add(codec, spec->pins[pin_idx].pin_nid, kctl);
	if (err < 0)
		return err;

	return 0;
}

#ifdef BE_PARANOID
static void hdmi_get_dip_index(struct hda_codec *codec, hda_nid_t pin_nid,
				int *packet_index, int *byte_index)
{
	int val;

	val = snd_hda_codec_read(codec, pin_nid, 0,
				 AC_VERB_GET_HDMI_DIP_INDEX, 0);

	*packet_index = val >> 5;
	*byte_index = val & 0x1f;
}
#endif

static void hdmi_set_dip_index(struct hda_codec *codec, hda_nid_t pin_nid,
				int packet_index, int byte_index)
{
	int val;

	val = (packet_index << 5) | (byte_index & 0x1f);

	snd_hda_codec_write(codec, pin_nid, 0, AC_VERB_SET_HDMI_DIP_INDEX, val);
}

static void hdmi_write_dip_byte(struct hda_codec *codec, hda_nid_t pin_nid,
				unsigned char val)
{
	snd_hda_codec_write(codec, pin_nid, 0, AC_VERB_SET_HDMI_DIP_DATA, val);
}

static void hdmi_init_pin(struct hda_codec *codec, hda_nid_t pin_nid)
{
	/* Unmute */
	if (get_wcaps(codec, pin_nid) & AC_WCAP_OUT_AMP)
		snd_hda_codec_write(codec, pin_nid, 0,
				AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);
	/* Disable pin out until stream is active*/
	snd_hda_codec_write(codec, pin_nid, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0);
}

static int hdmi_get_channel_count(struct hda_codec *codec, hda_nid_t cvt_nid)
{
	return 1 + snd_hda_codec_read(codec, cvt_nid, 0,
					AC_VERB_GET_CVT_CHAN_COUNT, 0);
}

static void hdmi_set_channel_count(struct hda_codec *codec,
				   hda_nid_t cvt_nid, int chs)
{
	if (chs != hdmi_get_channel_count(codec, cvt_nid))
		snd_hda_codec_write(codec, cvt_nid, 0,
				    AC_VERB_SET_CVT_CHAN_COUNT, chs - 1);
}


/*
 * Channel mapping routines
 */

/*
 * Compute derived values in channel_allocations[].
 */
static void init_channel_allocations(void)
{
	int i, j;
	struct cea_channel_speaker_allocation *p;

	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		p = channel_allocations + i;
		p->channels = 0;
		p->spk_mask = 0;
		for (j = 0; j < ARRAY_SIZE(p->speakers); j++)
			if (p->speakers[j]) {
				p->channels++;
				p->spk_mask |= p->speakers[j];
			}
	}
}

static int get_channel_allocation_order(int ca)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		if (channel_allocations[i].ca_index == ca)
			break;
	}
	return i;
}

/*
 * The transformation takes two steps:
 *
 *	eld->spk_alloc => (eld_speaker_allocation_bits[]) => spk_mask
 *	      spk_mask => (channel_allocations[])         => ai->CA
 *
 * TODO: it could select the wrong CA from multiple candidates.
*/
static int hdmi_channel_allocation(struct hdmi_eld *eld, int channels)
{
	int i;
	int ca = 0;
	int spk_mask = 0;
	char buf[SND_PRINT_CHANNEL_ALLOCATION_ADVISED_BUFSIZE];

	/*
	 * CA defaults to 0 for basic stereo audio
	 */
	if (channels <= 2)
		return 0;

	/*
	 * expand ELD's speaker allocation mask
	 *
	 * ELD tells the speaker mask in a compact(paired) form,
	 * expand ELD's notions to match the ones used by Audio InfoFrame.
	 */
	for (i = 0; i < ARRAY_SIZE(eld_speaker_allocation_bits); i++) {
		if (eld->spk_alloc & (1 << i))
			spk_mask |= eld_speaker_allocation_bits[i];
	}

	/* search for the first working match in the CA table */
	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		if (channels == channel_allocations[i].channels &&
		    (spk_mask & channel_allocations[i].spk_mask) ==
				channel_allocations[i].spk_mask) {
			ca = channel_allocations[i].ca_index;
			break;
		}
	}

	snd_print_channel_allocation(eld->spk_alloc, buf, sizeof(buf));
	snd_printdd("HDMI: select CA 0x%x for %d-channel allocation: %s\n",
		    ca, channels, buf);

	return ca;
}

static void hdmi_debug_channel_mapping(struct hda_codec *codec,
				       hda_nid_t pin_nid)
{
#ifdef CONFIG_SND_DEBUG_VERBOSE
	int i;
	int slot;

	for (i = 0; i < 8; i++) {
		slot = snd_hda_codec_read(codec, pin_nid, 0,
						AC_VERB_GET_HDMI_CHAN_SLOT, i);
		printk(KERN_DEBUG "HDMI: ASP channel %d => slot %d\n",
						slot >> 4, slot & 0xf);
	}
#endif
}


static void hdmi_std_setup_channel_mapping(struct hda_codec *codec,
				       hda_nid_t pin_nid,
				       bool non_pcm,
				       int ca)
{
	int i;
	int err;
	int order;
	int non_pcm_mapping[8];

	order = get_channel_allocation_order(ca);

	if (hdmi_channel_mapping[ca][1] == 0) {
		for (i = 0; i < channel_allocations[order].channels; i++)
			hdmi_channel_mapping[ca][i] = i | (i << 4);
		for (; i < 8; i++)
			hdmi_channel_mapping[ca][i] = 0xf | (i << 4);
	}

	if (non_pcm) {
		for (i = 0; i < channel_allocations[order].channels; i++)
			non_pcm_mapping[i] = i | (i << 4);
		for (; i < 8; i++)
			non_pcm_mapping[i] = 0xf | (i << 4);
	}

	for (i = 0; i < 8; i++) {
		err = snd_hda_codec_write(codec, pin_nid, 0,
					  AC_VERB_SET_HDMI_CHAN_SLOT,
					  non_pcm ? non_pcm_mapping[i] : hdmi_channel_mapping[ca][i]);
		if (err) {
			snd_printdd(KERN_NOTICE
				    "HDMI: channel mapping failed\n");
			break;
		}
	}

	hdmi_debug_channel_mapping(codec, pin_nid);
}

struct channel_map_table {
	unsigned char map;		/* ALSA API channel map position */
	unsigned char cea_slot;		/* CEA slot value */
	int spk_mask;			/* speaker position bit mask */
};

static struct channel_map_table map_tables[] = {
	{ SNDRV_CHMAP_FL,	0x00,	FL },
	{ SNDRV_CHMAP_FR,	0x01,	FR },
	{ SNDRV_CHMAP_RL,	0x04,	RL },
	{ SNDRV_CHMAP_RR,	0x05,	RR },
	{ SNDRV_CHMAP_LFE,	0x02,	LFE },
	{ SNDRV_CHMAP_FC,	0x03,	FC },
	{ SNDRV_CHMAP_RLC,	0x06,	RLC },
	{ SNDRV_CHMAP_RRC,	0x07,	RRC },
	{} /* terminator */
};

/* from ALSA API channel position to speaker bit mask */
static int to_spk_mask(unsigned char c)
{
	struct channel_map_table *t = map_tables;
	for (; t->map; t++) {
		if (t->map == c)
			return t->spk_mask;
	}
	return 0;
}

/* from ALSA API channel position to CEA slot */
static int to_cea_slot(unsigned char c)
{
	struct channel_map_table *t = map_tables;
	for (; t->map; t++) {
		if (t->map == c)
			return t->cea_slot;
	}
	return 0x0f;
}

/* from CEA slot to ALSA API channel position */
static int from_cea_slot(unsigned char c)
{
	struct channel_map_table *t = map_tables;
	for (; t->map; t++) {
		if (t->cea_slot == c)
			return t->map;
	}
	return 0;
}

/* from speaker bit mask to ALSA API channel position */
static int spk_to_chmap(int spk)
{
	struct channel_map_table *t = map_tables;
	for (; t->map; t++) {
		if (t->spk_mask == spk)
			return t->map;
	}
	return 0;
}

/* get the CA index corresponding to the given ALSA API channel map */
static int hdmi_manual_channel_allocation(int chs, unsigned char *map)
{
	int i, spks = 0, spk_mask = 0;

	for (i = 0; i < chs; i++) {
		int mask = to_spk_mask(map[i]);
		if (mask) {
			spk_mask |= mask;
			spks++;
		}
	}

	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		if ((chs == channel_allocations[i].channels ||
		     spks == channel_allocations[i].channels) &&
		    (spk_mask & channel_allocations[i].spk_mask) ==
				channel_allocations[i].spk_mask)
			return channel_allocations[i].ca_index;
	}
	return -1;
}

/* set up the channel slots for the given ALSA API channel map */
static int hdmi_manual_setup_channel_mapping(struct hda_codec *codec,
					     hda_nid_t pin_nid,
					     int chs, unsigned char *map)
{
	int i;
	for (i = 0; i < 8; i++) {
		int val, err;
		if (i < chs)
			val = to_cea_slot(map[i]);
		else
			val = 0xf;
		val |= (i << 4);
		err = snd_hda_codec_write(codec, pin_nid, 0,
					  AC_VERB_SET_HDMI_CHAN_SLOT, val);
		if (err)
			return -EINVAL;
	}
	return 0;
}

/* store ALSA API channel map from the current default map */
static void hdmi_setup_fake_chmap(unsigned char *map, int ca)
{
	int i;
	for (i = 0; i < 8; i++) {
		if (i < channel_allocations[ca].channels)
			map[i] = from_cea_slot((hdmi_channel_mapping[ca][i] >> 4) & 0x0f);
		else
			map[i] = 0;
	}
}

static void hdmi_setup_channel_mapping(struct hda_codec *codec,
				       hda_nid_t pin_nid, bool non_pcm, int ca,
				       int channels, unsigned char *map)
{
	if (!non_pcm && map) {
		hdmi_manual_setup_channel_mapping(codec, pin_nid,
						  channels, map);
	} else {
		hdmi_std_setup_channel_mapping(codec, pin_nid, non_pcm, ca);
		hdmi_setup_fake_chmap(map, ca);
	}
}

/*
 * Audio InfoFrame routines
 */

/*
 * Enable Audio InfoFrame Transmission
 */
static void hdmi_start_infoframe_trans(struct hda_codec *codec,
				       hda_nid_t pin_nid)
{
	hdmi_set_dip_index(codec, pin_nid, 0x0, 0x0);
	snd_hda_codec_write(codec, pin_nid, 0, AC_VERB_SET_HDMI_DIP_XMIT,
						AC_DIPXMIT_BEST);
}

/*
 * Disable Audio InfoFrame Transmission
 */
static void hdmi_stop_infoframe_trans(struct hda_codec *codec,
				      hda_nid_t pin_nid)
{
	hdmi_set_dip_index(codec, pin_nid, 0x0, 0x0);
	snd_hda_codec_write(codec, pin_nid, 0, AC_VERB_SET_HDMI_DIP_XMIT,
						AC_DIPXMIT_DISABLE);
}

static void hdmi_debug_dip_size(struct hda_codec *codec, hda_nid_t pin_nid)
{
#ifdef CONFIG_SND_DEBUG_VERBOSE
	int i;
	int size;

	size = snd_hdmi_get_eld_size(codec, pin_nid);
	printk(KERN_DEBUG "HDMI: ELD buf size is %d\n", size);

	for (i = 0; i < 8; i++) {
		size = snd_hda_codec_read(codec, pin_nid, 0,
						AC_VERB_GET_HDMI_DIP_SIZE, i);
		printk(KERN_DEBUG "HDMI: DIP GP[%d] buf size is %d\n", i, size);
	}
#endif
}

static void hdmi_clear_dip_buffers(struct hda_codec *codec, hda_nid_t pin_nid)
{
#ifdef BE_PARANOID
	int i, j;
	int size;
	int pi, bi;
	for (i = 0; i < 8; i++) {
		size = snd_hda_codec_read(codec, pin_nid, 0,
						AC_VERB_GET_HDMI_DIP_SIZE, i);
		if (size == 0)
			continue;

		hdmi_set_dip_index(codec, pin_nid, i, 0x0);
		for (j = 1; j < 1000; j++) {
			hdmi_write_dip_byte(codec, pin_nid, 0x0);
			hdmi_get_dip_index(codec, pin_nid, &pi, &bi);
			if (pi != i)
				snd_printd(KERN_INFO "dip index %d: %d != %d\n",
						bi, pi, i);
			if (bi == 0) /* byte index wrapped around */
				break;
		}
		snd_printd(KERN_INFO
			"HDMI: DIP GP[%d] buf reported size=%d, written=%d\n",
			i, size, j);
	}
#endif
}

static void hdmi_checksum_audio_infoframe(struct hdmi_audio_infoframe *hdmi_ai)
{
	u8 *bytes = (u8 *)hdmi_ai;
	u8 sum = 0;
	int i;

	hdmi_ai->checksum = 0;

	for (i = 0; i < sizeof(*hdmi_ai); i++)
		sum += bytes[i];

	hdmi_ai->checksum = -sum;
}

static void hdmi_fill_audio_infoframe(struct hda_codec *codec,
				      hda_nid_t pin_nid,
				      u8 *dip, int size)
{
	int i;

	hdmi_debug_dip_size(codec, pin_nid);
	hdmi_clear_dip_buffers(codec, pin_nid); /* be paranoid */

	hdmi_set_dip_index(codec, pin_nid, 0x0, 0x0);
	for (i = 0; i < size; i++)
		hdmi_write_dip_byte(codec, pin_nid, dip[i]);
}

static bool hdmi_infoframe_uptodate(struct hda_codec *codec, hda_nid_t pin_nid,
				    u8 *dip, int size)
{
	u8 val;
	int i;

	if (snd_hda_codec_read(codec, pin_nid, 0, AC_VERB_GET_HDMI_DIP_XMIT, 0)
							    != AC_DIPXMIT_BEST)
		return false;

	hdmi_set_dip_index(codec, pin_nid, 0x0, 0x0);
	for (i = 0; i < size; i++) {
		val = snd_hda_codec_read(codec, pin_nid, 0,
					 AC_VERB_GET_HDMI_DIP_DATA, 0);
		if (val != dip[i])
			return false;
	}

	return true;
}

static void hdmi_setup_audio_infoframe(struct hda_codec *codec, int pin_idx,
				       bool non_pcm,
				       struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = &spec->pins[pin_idx];
	hda_nid_t pin_nid = per_pin->pin_nid;
	int channels = substream->runtime->channels;
	struct hdmi_eld *eld;
	int ca;
	union audio_infoframe ai;

	eld = &spec->pins[pin_idx].sink_eld;
	if (!eld->monitor_present)
		return;

	if (!non_pcm && per_pin->chmap_set)
		ca = hdmi_manual_channel_allocation(channels, per_pin->chmap);
	else
		ca = hdmi_channel_allocation(eld, channels);
	if (ca < 0)
		ca = 0;

	memset(&ai, 0, sizeof(ai));
	if (eld->conn_type == 0) { /* HDMI */
		struct hdmi_audio_infoframe *hdmi_ai = &ai.hdmi;

		hdmi_ai->type		= 0x84;
		hdmi_ai->ver		= 0x01;
		hdmi_ai->len		= 0x0a;
		hdmi_ai->CC02_CT47	= channels - 1;
		hdmi_ai->CA		= ca;
		hdmi_checksum_audio_infoframe(hdmi_ai);
	} else if (eld->conn_type == 1) { /* DisplayPort */
		struct dp_audio_infoframe *dp_ai = &ai.dp;

		dp_ai->type		= 0x84;
		dp_ai->len		= 0x1b;
		dp_ai->ver		= 0x11 << 2;
		dp_ai->CC02_CT47	= channels - 1;
		dp_ai->CA		= ca;
	} else {
		snd_printd("HDMI: unknown connection type at pin %d\n",
			    pin_nid);
		return;
	}

	/*
	 * sizeof(ai) is used instead of sizeof(*hdmi_ai) or
	 * sizeof(*dp_ai) to avoid partial match/update problems when
	 * the user switches between HDMI/DP monitors.
	 */
	if (!hdmi_infoframe_uptodate(codec, pin_nid, ai.bytes,
					sizeof(ai))) {
		snd_printdd("hdmi_setup_audio_infoframe: "
			    "pin=%d channels=%d\n",
			    pin_nid,
			    channels);
		hdmi_setup_channel_mapping(codec, pin_nid, non_pcm, ca,
					   channels, per_pin->chmap);
		hdmi_stop_infoframe_trans(codec, pin_nid);
		hdmi_fill_audio_infoframe(codec, pin_nid,
					    ai.bytes, sizeof(ai));
		hdmi_start_infoframe_trans(codec, pin_nid);
	} else {
		/* For non-pcm audio switch, setup new channel mapping
		 * accordingly */
		if (per_pin->non_pcm != non_pcm)
			hdmi_setup_channel_mapping(codec, pin_nid, non_pcm, ca,
						   channels, per_pin->chmap);
	}

	per_pin->non_pcm = non_pcm;
}


/*
 * Unsolicited events
 */

static void hdmi_present_sense(struct hdmi_spec_per_pin *per_pin, int repoll);

static void hdmi_intrinsic_event(struct hda_codec *codec, unsigned int res)
{
	struct hdmi_spec *spec = codec->spec;
	int tag = res >> AC_UNSOL_RES_TAG_SHIFT;
	int pin_nid;
	int pin_idx;
	struct hda_jack_tbl *jack;

	jack = snd_hda_jack_tbl_get_from_tag(codec, tag);
	if (!jack)
		return;
	pin_nid = jack->nid;
	jack->jack_dirty = 1;

	_snd_printd(SND_PR_VERBOSE,
		"HDMI hot plug event: Codec=%d Pin=%d Presence_Detect=%d ELD_Valid=%d\n",
		codec->addr, pin_nid,
		!!(res & AC_UNSOL_RES_PD), !!(res & AC_UNSOL_RES_ELDV));

	pin_idx = pin_nid_to_pin_index(spec, pin_nid);
	if (pin_idx < 0)
		return;

	hdmi_present_sense(&spec->pins[pin_idx], 1);
	snd_hda_jack_report_sync(codec);
}

static void hdmi_non_intrinsic_event(struct hda_codec *codec, unsigned int res)
{
	int tag = res >> AC_UNSOL_RES_TAG_SHIFT;
	int subtag = (res & AC_UNSOL_RES_SUBTAG) >> AC_UNSOL_RES_SUBTAG_SHIFT;
	int cp_state = !!(res & AC_UNSOL_RES_CP_STATE);
	int cp_ready = !!(res & AC_UNSOL_RES_CP_READY);

	printk(KERN_INFO
		"HDMI CP event: CODEC=%d TAG=%d SUBTAG=0x%x CP_STATE=%d CP_READY=%d\n",
		codec->addr,
		tag,
		subtag,
		cp_state,
		cp_ready);

	/* TODO */
	if (cp_state)
		;
	if (cp_ready)
		;
}


static void hdmi_unsol_event(struct hda_codec *codec, unsigned int res)
{
	int tag = res >> AC_UNSOL_RES_TAG_SHIFT;
	int subtag = (res & AC_UNSOL_RES_SUBTAG) >> AC_UNSOL_RES_SUBTAG_SHIFT;

	if (!snd_hda_jack_tbl_get_from_tag(codec, tag)) {
		snd_printd(KERN_INFO "Unexpected HDMI event tag 0x%x\n", tag);
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

/* HBR should be Non-PCM, 8 channels */
#define is_hbr_format(format) \
	((format & AC_FMT_TYPE_NON_PCM) && (format & AC_FMT_CHAN_MASK) == 7)

static int hdmi_setup_stream(struct hda_codec *codec, hda_nid_t cvt_nid,
			      hda_nid_t pin_nid, u32 stream_tag, int format)
{
	int pinctl;
	int new_pinctl = 0;

	if (snd_hda_query_pin_caps(codec, pin_nid) & AC_PINCAP_HBR) {
		pinctl = snd_hda_codec_read(codec, pin_nid, 0,
					    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);

		new_pinctl = pinctl & ~AC_PINCTL_EPT;
		if (is_hbr_format(format))
			new_pinctl |= AC_PINCTL_EPT_HBR;
		else
			new_pinctl |= AC_PINCTL_EPT_NATIVE;

		snd_printdd("hdmi_setup_stream: "
			    "NID=0x%x, %spinctl=0x%x\n",
			    pin_nid,
			    pinctl == new_pinctl ? "" : "new-",
			    new_pinctl);

		if (pinctl != new_pinctl)
			snd_hda_codec_write(codec, pin_nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    new_pinctl);

	}
	if (is_hbr_format(format) && !new_pinctl) {
		snd_printdd("hdmi_setup_stream: HBR is not supported\n");
		return -EINVAL;
	}

	snd_hda_codec_setup_stream(codec, cvt_nid, stream_tag, 0, format);
	return 0;
}

/*
 * HDA PCM callbacks
 */
static int hdmi_pcm_open(struct hda_pcm_stream *hinfo,
			 struct hda_codec *codec,
			 struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int pin_idx, cvt_idx, mux_idx = 0;
	struct hdmi_spec_per_pin *per_pin;
	struct hdmi_eld *eld;
	struct hdmi_spec_per_cvt *per_cvt = NULL;

	/* Validate hinfo */
	pin_idx = hinfo_to_pin_index(spec, hinfo);
	if (snd_BUG_ON(pin_idx < 0))
		return -EINVAL;
	per_pin = &spec->pins[pin_idx];
	eld = &per_pin->sink_eld;

	/* Dynamically assign converter to stream */
	for (cvt_idx = 0; cvt_idx < spec->num_cvts; cvt_idx++) {
		per_cvt = &spec->cvts[cvt_idx];

		/* Must not already be assigned */
		if (per_cvt->assigned)
			continue;
		/* Must be in pin's mux's list of converters */
		for (mux_idx = 0; mux_idx < per_pin->num_mux_nids; mux_idx++)
			if (per_pin->mux_nids[mux_idx] == per_cvt->cvt_nid)
				break;
		/* Not in mux list */
		if (mux_idx == per_pin->num_mux_nids)
			continue;
		break;
	}
	/* No free converters */
	if (cvt_idx == spec->num_cvts)
		return -ENODEV;

	/* Claim converter */
	per_cvt->assigned = 1;
	hinfo->nid = per_cvt->cvt_nid;

	snd_hda_codec_write(codec, per_pin->pin_nid, 0,
			    AC_VERB_SET_CONNECT_SEL,
			    mux_idx);
	snd_hda_spdif_ctls_assign(codec, pin_idx, per_cvt->cvt_nid);

	/* Initially set the converter's capabilities */
	hinfo->channels_min = per_cvt->channels_min;
	hinfo->channels_max = per_cvt->channels_max;
	hinfo->rates = per_cvt->rates;
	hinfo->formats = per_cvt->formats;
	hinfo->maxbps = per_cvt->maxbps;

	/* Restrict capabilities by ELD if this isn't disabled */
	if (!static_hdmi_pcm && eld->eld_valid) {
		snd_hdmi_eld_update_pcm_info(eld, hinfo);
		if (hinfo->channels_min > hinfo->channels_max ||
		    !hinfo->rates || !hinfo->formats)
			return -ENODEV;
	}

	/* Store the updated parameters */
	runtime->hw.channels_min = hinfo->channels_min;
	runtime->hw.channels_max = hinfo->channels_max;
	runtime->hw.formats = hinfo->formats;
	runtime->hw.rates = hinfo->rates;

	snd_pcm_hw_constraint_step(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	return 0;
}

/*
 * HDA/HDMI auto parsing
 */
static int hdmi_read_pin_conn(struct hda_codec *codec, int pin_idx)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = &spec->pins[pin_idx];
	hda_nid_t pin_nid = per_pin->pin_nid;

	if (!(get_wcaps(codec, pin_nid) & AC_WCAP_CONN_LIST)) {
		snd_printk(KERN_WARNING
			   "HDMI: pin %d wcaps %#x "
			   "does not support connection list\n",
			   pin_nid, get_wcaps(codec, pin_nid));
		return -EINVAL;
	}

	per_pin->num_mux_nids = snd_hda_get_connections(codec, pin_nid,
							per_pin->mux_nids,
							HDA_MAX_CONNECTIONS);

	return 0;
}

static void hdmi_present_sense(struct hdmi_spec_per_pin *per_pin, int repoll)
{
	struct hda_codec *codec = per_pin->codec;
	struct hdmi_eld *eld = &per_pin->sink_eld;
	hda_nid_t pin_nid = per_pin->pin_nid;
	/*
	 * Always execute a GetPinSense verb here, even when called from
	 * hdmi_intrinsic_event; for some NVIDIA HW, the unsolicited
	 * response's PD bit is not the real PD value, but indicates that
	 * the real PD value changed. An older version of the HD-audio
	 * specification worked this way. Hence, we just ignore the data in
	 * the unsolicited response to avoid custom WARs.
	 */
	int present = snd_hda_pin_sense(codec, pin_nid);
	bool eld_valid = false;

	memset(eld, 0, offsetof(struct hdmi_eld, eld_buffer));

	eld->monitor_present	= !!(present & AC_PINSENSE_PRESENCE);
	if (eld->monitor_present)
		eld_valid	= !!(present & AC_PINSENSE_ELDV);

	_snd_printd(SND_PR_VERBOSE,
		"HDMI status: Codec=%d Pin=%d Presence_Detect=%d ELD_Valid=%d\n",
		codec->addr, pin_nid, eld->monitor_present, eld_valid);

	if (eld_valid) {
		if (!snd_hdmi_get_eld(eld, codec, pin_nid))
			snd_hdmi_show_eld(eld);
		else if (repoll) {
			queue_delayed_work(codec->bus->workq,
					   &per_pin->work,
					   msecs_to_jiffies(300));
		}
	}
}

static void hdmi_repoll_eld(struct work_struct *work)
{
	struct hdmi_spec_per_pin *per_pin =
	container_of(to_delayed_work(work), struct hdmi_spec_per_pin, work);

	if (per_pin->repoll_count++ > 6)
		per_pin->repoll_count = 0;

	hdmi_present_sense(per_pin, per_pin->repoll_count);
}

static int hdmi_add_pin(struct hda_codec *codec, hda_nid_t pin_nid)
{
	struct hdmi_spec *spec = codec->spec;
	unsigned int caps, config;
	int pin_idx;
	struct hdmi_spec_per_pin *per_pin;
	int err;

	caps = snd_hda_param_read(codec, pin_nid, AC_PAR_PIN_CAP);
	if (!(caps & (AC_PINCAP_HDMI | AC_PINCAP_DP)))
		return 0;

	config = snd_hda_codec_read(codec, pin_nid, 0,
				AC_VERB_GET_CONFIG_DEFAULT, 0);
	if (get_defcfg_connect(config) == AC_JACK_PORT_NONE)
		return 0;

	if (snd_BUG_ON(spec->num_pins >= MAX_HDMI_PINS))
		return -E2BIG;

	pin_idx = spec->num_pins;
	per_pin = &spec->pins[pin_idx];

	per_pin->pin_nid = pin_nid;
	per_pin->non_pcm = false;

	err = hdmi_read_pin_conn(codec, pin_idx);
	if (err < 0)
		return err;

	spec->num_pins++;

	return 0;
}

static int hdmi_add_cvt(struct hda_codec *codec, hda_nid_t cvt_nid)
{
	struct hdmi_spec *spec = codec->spec;
	int cvt_idx;
	struct hdmi_spec_per_cvt *per_cvt;
	unsigned int chans;
	int err;

	if (snd_BUG_ON(spec->num_cvts >= MAX_HDMI_CVTS))
		return -E2BIG;

	chans = get_wcaps(codec, cvt_nid);
	chans = get_wcaps_channels(chans);

	cvt_idx = spec->num_cvts;
	per_cvt = &spec->cvts[cvt_idx];

	per_cvt->cvt_nid = cvt_nid;
	per_cvt->channels_min = 2;
	if (chans <= 16) {
		per_cvt->channels_max = chans;
		if (chans > spec->channels_max)
			spec->channels_max = chans;
	}

	err = snd_hda_query_supported_pcm(codec, cvt_nid,
					  &per_cvt->rates,
					  &per_cvt->formats,
					  &per_cvt->maxbps);
	if (err < 0)
		return err;

	spec->num_cvts++;

	return 0;
}

static int hdmi_parse_codec(struct hda_codec *codec)
{
	hda_nid_t nid;
	int i, nodes;

	nodes = snd_hda_get_sub_nodes(codec, codec->afg, &nid);
	if (!nid || nodes < 0) {
		snd_printk(KERN_WARNING "HDMI: failed to get afg sub nodes\n");
		return -EINVAL;
	}

	for (i = 0; i < nodes; i++, nid++) {
		unsigned int caps;
		unsigned int type;

		caps = snd_hda_param_read(codec, nid, AC_PAR_AUDIO_WIDGET_CAP);
		type = get_wcaps_type(caps);

		if (!(caps & AC_WCAP_DIGITAL))
			continue;

		switch (type) {
		case AC_WID_AUD_OUT:
			hdmi_add_cvt(codec, nid);
			break;
		case AC_WID_PIN:
			hdmi_add_pin(codec, nid);
			break;
		}
	}

	/*
	 * G45/IbexPeak don't support EPSS: the unsolicited pin hot plug event
	 * can be lost and presence sense verb will become inaccurate if the
	 * HDA link is powered off at hot plug or hw initialization time.
	 */
#ifdef CONFIG_PM
	if (!(snd_hda_param_read(codec, codec->afg, AC_PAR_POWER_STATE) &
	      AC_PWRST_EPSS))
		codec->bus->power_keep_link_on = 1;
#endif

	return 0;
}

/*
 */
static char *get_hdmi_pcm_name(int idx)
{
	static char names[MAX_HDMI_PINS][8];
	sprintf(&names[idx][0], "HDMI %d", idx);
	return &names[idx][0];
}

static bool check_non_pcm_per_cvt(struct hda_codec *codec, hda_nid_t cvt_nid)
{
	struct hda_spdif_out *spdif;
	bool non_pcm;

	mutex_lock(&codec->spdif_mutex);
	spdif = snd_hda_spdif_out_of_nid(codec, cvt_nid);
	non_pcm = !!(spdif->status & IEC958_AES0_NONAUDIO);
	mutex_unlock(&codec->spdif_mutex);
	return non_pcm;
}


/*
 * HDMI callbacks
 */

static int generic_hdmi_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   unsigned int stream_tag,
					   unsigned int format,
					   struct snd_pcm_substream *substream)
{
	hda_nid_t cvt_nid = hinfo->nid;
	struct hdmi_spec *spec = codec->spec;
	int pin_idx = hinfo_to_pin_index(spec, hinfo);
	hda_nid_t pin_nid = spec->pins[pin_idx].pin_nid;
	int pinctl;
	bool non_pcm;

	non_pcm = check_non_pcm_per_cvt(codec, cvt_nid);

	hdmi_set_channel_count(codec, cvt_nid, substream->runtime->channels);

	hdmi_setup_audio_infoframe(codec, pin_idx, non_pcm, substream);

	pinctl = snd_hda_codec_read(codec, pin_nid, 0,
				    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	snd_hda_codec_write(codec, pin_nid, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, pinctl | PIN_OUT);

	return hdmi_setup_stream(codec, cvt_nid, pin_nid, stream_tag, format);
}

static int generic_hdmi_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					     struct hda_codec *codec,
					     struct snd_pcm_substream *substream)
{
	snd_hda_codec_cleanup_stream(codec, hinfo->nid);
	return 0;
}

static int hdmi_pcm_close(struct hda_pcm_stream *hinfo,
			  struct hda_codec *codec,
			  struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	int cvt_idx, pin_idx;
	struct hdmi_spec_per_cvt *per_cvt;
	struct hdmi_spec_per_pin *per_pin;
	int pinctl;

	if (hinfo->nid) {
		cvt_idx = cvt_nid_to_cvt_index(spec, hinfo->nid);
		if (snd_BUG_ON(cvt_idx < 0))
			return -EINVAL;
		per_cvt = &spec->cvts[cvt_idx];

		snd_BUG_ON(!per_cvt->assigned);
		per_cvt->assigned = 0;
		hinfo->nid = 0;

		pin_idx = hinfo_to_pin_index(spec, hinfo);
		if (snd_BUG_ON(pin_idx < 0))
			return -EINVAL;
		per_pin = &spec->pins[pin_idx];

		pinctl = snd_hda_codec_read(codec, per_pin->pin_nid, 0,
					    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_codec_write(codec, per_pin->pin_nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL,
				    pinctl & ~PIN_OUT);
		snd_hda_spdif_ctls_unassign(codec, pin_idx);
		per_pin->chmap_set = false;
		memset(per_pin->chmap, 0, sizeof(per_pin->chmap));
	}

	return 0;
}

static const struct hda_pcm_ops generic_ops = {
	.open = hdmi_pcm_open,
	.close = hdmi_pcm_close,
	.prepare = generic_hdmi_playback_pcm_prepare,
	.cleanup = generic_hdmi_playback_pcm_cleanup,
};

/*
 * ALSA API channel-map control callbacks
 */
static int hdmi_chmap_ctl_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct hda_codec *codec = info->private_data;
	struct hdmi_spec *spec = codec->spec;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = spec->channels_max;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SNDRV_CHMAP_LAST;
	return 0;
}

static int hdmi_chmap_ctl_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			      unsigned int size, unsigned int __user *tlv)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct hda_codec *codec = info->private_data;
	struct hdmi_spec *spec = codec->spec;
	const unsigned int valid_mask =
		FL | FR | RL | RR | LFE | FC | RLC | RRC;
	unsigned int __user *dst;
	int chs, count = 0;

	if (size < 8)
		return -ENOMEM;
	if (put_user(SNDRV_CTL_TLVT_CONTAINER, tlv))
		return -EFAULT;
	size -= 8;
	dst = tlv + 2;
	for (chs = 2; chs <= spec->channels_max; chs += 2) {
		int i, c;
		struct cea_channel_speaker_allocation *cap;
		cap = channel_allocations;
		for (i = 0; i < ARRAY_SIZE(channel_allocations); i++, cap++) {
			int chs_bytes = chs * 4;
			if (cap->channels != chs)
				continue;
			if (cap->spk_mask & ~valid_mask)
				continue;
			if (size < 8)
				return -ENOMEM;
			if (put_user(SNDRV_CTL_TLVT_CHMAP_VAR, dst) ||
			    put_user(chs_bytes, dst + 1))
				return -EFAULT;
			dst += 2;
			size -= 8;
			count += 8;
			if (size < chs_bytes)
				return -ENOMEM;
			size -= chs_bytes;
			count += chs_bytes;
			for (c = 7; c >= 0; c--) {
				int spk = cap->speakers[c];
				if (!spk)
					continue;
				if (put_user(spk_to_chmap(spk), dst))
					return -EFAULT;
				dst++;
			}
		}
	}
	if (put_user(count, tlv + 1))
		return -EFAULT;
	return 0;
}

static int hdmi_chmap_ctl_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct hda_codec *codec = info->private_data;
	struct hdmi_spec *spec = codec->spec;
	int pin_idx = kcontrol->private_value;
	struct hdmi_spec_per_pin *per_pin = &spec->pins[pin_idx];
	int i;

	for (i = 0; i < ARRAY_SIZE(per_pin->chmap); i++)
		ucontrol->value.integer.value[i] = per_pin->chmap[i];
	return 0;
}

static int hdmi_chmap_ctl_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct hda_codec *codec = info->private_data;
	struct hdmi_spec *spec = codec->spec;
	int pin_idx = kcontrol->private_value;
	struct hdmi_spec_per_pin *per_pin = &spec->pins[pin_idx];
	unsigned int ctl_idx;
	struct snd_pcm_substream *substream;
	unsigned char chmap[8];
	int i, ca, prepared = 0;

	ctl_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	substream = snd_pcm_chmap_substream(info, ctl_idx);
	if (!substream || !substream->runtime)
		return -EBADFD;
	switch (substream->runtime->status->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_SETUP:
		break;
	case SNDRV_PCM_STATE_PREPARED:
		prepared = 1;
		break;
	default:
		return -EBUSY;
	}
	memset(chmap, 0, sizeof(chmap));
	for (i = 0; i < ARRAY_SIZE(chmap); i++)
		chmap[i] = ucontrol->value.integer.value[i];
	if (!memcmp(chmap, per_pin->chmap, sizeof(chmap)))
		return 0;
	ca = hdmi_manual_channel_allocation(ARRAY_SIZE(chmap), chmap);
	if (ca < 0)
		return -EINVAL;
	per_pin->chmap_set = true;
	memcpy(per_pin->chmap, chmap, sizeof(chmap));
	if (prepared)
		hdmi_setup_audio_infoframe(codec, pin_idx, per_pin->non_pcm,
					   substream);

	return 0;
}

static int generic_hdmi_build_pcms(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hda_pcm *info;
		struct hda_pcm_stream *pstr;

		info = &spec->pcm_rec[pin_idx];
		info->name = get_hdmi_pcm_name(pin_idx);
		info->pcm_type = HDA_PCM_TYPE_HDMI;
		info->own_chmap = true;

		pstr = &info->stream[SNDRV_PCM_STREAM_PLAYBACK];
		pstr->substreams = 1;
		pstr->ops = generic_ops;
		/* other pstr fields are set in open */
	}

	codec->num_pcms = spec->num_pins;
	codec->pcm_info = spec->pcm_rec;

	return 0;
}

static int generic_hdmi_build_jack(struct hda_codec *codec, int pin_idx)
{
	char hdmi_str[32] = "HDMI/DP";
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = &spec->pins[pin_idx];
	int pcmdev = spec->pcm_rec[pin_idx].device;

	if (pcmdev > 0)
		sprintf(hdmi_str + strlen(hdmi_str), ",pcm=%d", pcmdev);

	return snd_hda_jack_add_kctl(codec, per_pin->pin_nid, hdmi_str, 0);
}

static int generic_hdmi_build_controls(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int err;
	int pin_idx;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = &spec->pins[pin_idx];

		err = generic_hdmi_build_jack(codec, pin_idx);
		if (err < 0)
			return err;

		err = snd_hda_create_spdif_out_ctls(codec,
						    per_pin->pin_nid,
						    per_pin->mux_nids[0]);
		if (err < 0)
			return err;
		snd_hda_spdif_ctls_unassign(codec, pin_idx);

		/* add control for ELD Bytes */
		err = hdmi_create_eld_ctl(codec,
					pin_idx,
					spec->pcm_rec[pin_idx].device);

		if (err < 0)
			return err;

		hdmi_present_sense(per_pin, 0);
	}

	/* add channel maps */
	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct snd_pcm_chmap *chmap;
		struct snd_kcontrol *kctl;
		int i;
		err = snd_pcm_add_chmap_ctls(codec->pcm_info[pin_idx].pcm,
					     SNDRV_PCM_STREAM_PLAYBACK,
					     NULL, 0, pin_idx, &chmap);
		if (err < 0)
			return err;
		/* override handlers */
		chmap->private_data = codec;
		kctl = chmap->kctl;
		for (i = 0; i < kctl->count; i++)
			kctl->vd[i].access |= SNDRV_CTL_ELEM_ACCESS_WRITE;
		kctl->info = hdmi_chmap_ctl_info;
		kctl->get = hdmi_chmap_ctl_get;
		kctl->put = hdmi_chmap_ctl_put;
		kctl->tlv.c = hdmi_chmap_ctl_tlv;
	}

	return 0;
}

static int generic_hdmi_init_per_pins(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = &spec->pins[pin_idx];
		struct hdmi_eld *eld = &per_pin->sink_eld;

		per_pin->codec = codec;
		INIT_DELAYED_WORK(&per_pin->work, hdmi_repoll_eld);
		snd_hda_eld_proc_new(codec, eld, pin_idx);
	}
	return 0;
}

static int generic_hdmi_init(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = &spec->pins[pin_idx];
		hda_nid_t pin_nid = per_pin->pin_nid;

		hdmi_init_pin(codec, pin_nid);
		snd_hda_jack_detect_enable(codec, pin_nid, pin_nid);
	}
	return 0;
}

static void generic_hdmi_free(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = &spec->pins[pin_idx];
		struct hdmi_eld *eld = &per_pin->sink_eld;

		cancel_delayed_work(&per_pin->work);
		snd_hda_eld_proc_free(codec, eld);
	}

	flush_workqueue(codec->bus->workq);
	kfree(spec);
}

static const struct hda_codec_ops generic_hdmi_patch_ops = {
	.init			= generic_hdmi_init,
	.free			= generic_hdmi_free,
	.build_pcms		= generic_hdmi_build_pcms,
	.build_controls		= generic_hdmi_build_controls,
	.unsol_event		= hdmi_unsol_event,
};

static int patch_generic_hdmi(struct hda_codec *codec)
{
	struct hdmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	if (hdmi_parse_codec(codec) < 0) {
		codec->spec = NULL;
		kfree(spec);
		return -EINVAL;
	}
	codec->patch_ops = generic_hdmi_patch_ops;
	generic_hdmi_init_per_pins(codec);

	init_channel_allocations();

	return 0;
}

/*
 * Shared non-generic implementations
 */

static int simple_playback_build_pcms(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;
	unsigned int chans;
	struct hda_pcm_stream *pstr;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	chans = get_wcaps(codec, spec->cvts[0].cvt_nid);
	chans = get_wcaps_channels(chans);

	info->name = get_hdmi_pcm_name(0);
	info->pcm_type = HDA_PCM_TYPE_HDMI;
	pstr = &info->stream[SNDRV_PCM_STREAM_PLAYBACK];
	*pstr = spec->pcm_playback;
	pstr->nid = spec->cvts[0].cvt_nid;
	if (pstr->channels_max <= 2 && chans && chans <= 16)
		pstr->channels_max = chans;

	return 0;
}

/* unsolicited event for jack sensing */
static void simple_hdmi_unsol_event(struct hda_codec *codec,
				    unsigned int res)
{
	snd_hda_jack_set_dirty_all(codec);
	snd_hda_jack_report_sync(codec);
}

/* generic_hdmi_build_jack can be used for simple_hdmi, too,
 * as long as spec->pins[] is set correctly
 */
#define simple_hdmi_build_jack	generic_hdmi_build_jack

static int simple_playback_build_controls(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int err;

	err = snd_hda_create_spdif_out_ctls(codec,
					    spec->cvts[0].cvt_nid,
					    spec->cvts[0].cvt_nid);
	if (err < 0)
		return err;
	return simple_hdmi_build_jack(codec, 0);
}

static int simple_playback_init(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	hda_nid_t pin = spec->pins[0].pin_nid;

	snd_hda_codec_write(codec, pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
	/* some codecs require to unmute the pin */
	if (get_wcaps(codec, pin) & AC_WCAP_OUT_AMP)
		snd_hda_codec_write(codec, pin, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_OUT_UNMUTE);
	snd_hda_jack_detect_enable(codec, pin, pin);
	return 0;
}

static void simple_playback_free(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;

	kfree(spec);
}

/*
 * Nvidia specific implementations
 */

#define Nv_VERB_SET_Channel_Allocation          0xF79
#define Nv_VERB_SET_Info_Frame_Checksum         0xF7A
#define Nv_VERB_SET_Audio_Protection_On         0xF98
#define Nv_VERB_SET_Audio_Protection_Off        0xF99

#define nvhdmi_master_con_nid_7x	0x04
#define nvhdmi_master_pin_nid_7x	0x05

static const hda_nid_t nvhdmi_con_nids_7x[4] = {
	/*front, rear, clfe, rear_surr */
	0x6, 0x8, 0xa, 0xc,
};

static const struct hda_verb nvhdmi_basic_init_7x_2ch[] = {
	/* set audio protect on */
	{ 0x1, Nv_VERB_SET_Audio_Protection_On, 0x1},
	/* enable digital output on pin widget */
	{ 0x5, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{} /* terminator */
};

static const struct hda_verb nvhdmi_basic_init_7x_8ch[] = {
	/* set audio protect on */
	{ 0x1, Nv_VERB_SET_Audio_Protection_On, 0x1},
	/* enable digital output on pin widget */
	{ 0x5, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0x7, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0x9, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0xb, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0xd, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{} /* terminator */
};

#ifdef LIMITED_RATE_FMT_SUPPORT
/* support only the safe format and rate */
#define SUPPORTED_RATES		SNDRV_PCM_RATE_48000
#define SUPPORTED_MAXBPS	16
#define SUPPORTED_FORMATS	SNDRV_PCM_FMTBIT_S16_LE
#else
/* support all rates and formats */
#define SUPPORTED_RATES \
	(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
	SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
	 SNDRV_PCM_RATE_192000)
#define SUPPORTED_MAXBPS	24
#define SUPPORTED_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE)
#endif

static int nvhdmi_7x_init_2ch(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, nvhdmi_basic_init_7x_2ch);
	return 0;
}

static int nvhdmi_7x_init_8ch(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, nvhdmi_basic_init_7x_8ch);
	return 0;
}

static unsigned int channels_2_6_8[] = {
	2, 6, 8
};

static unsigned int channels_2_8[] = {
	2, 8
};

static struct snd_pcm_hw_constraint_list hw_constraints_2_6_8_channels = {
	.count = ARRAY_SIZE(channels_2_6_8),
	.list = channels_2_6_8,
	.mask = 0,
};

static struct snd_pcm_hw_constraint_list hw_constraints_2_8_channels = {
	.count = ARRAY_SIZE(channels_2_8),
	.list = channels_2_8,
	.mask = 0,
};

static int simple_playback_pcm_open(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	struct snd_pcm_hw_constraint_list *hw_constraints_channels = NULL;

	switch (codec->preset->id) {
	case 0x10de0002:
	case 0x10de0003:
	case 0x10de0005:
	case 0x10de0006:
		hw_constraints_channels = &hw_constraints_2_8_channels;
		break;
	case 0x10de0007:
		hw_constraints_channels = &hw_constraints_2_6_8_channels;
		break;
	default:
		break;
	}

	if (hw_constraints_channels != NULL) {
		snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_CHANNELS,
				hw_constraints_channels);
	} else {
		snd_pcm_hw_constraint_step(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	}

	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int simple_playback_pcm_close(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int simple_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
}

static const struct hda_pcm_stream simple_pcm_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.open = simple_playback_pcm_open,
		.close = simple_playback_pcm_close,
		.prepare = simple_playback_pcm_prepare
	},
};

static const struct hda_codec_ops simple_hdmi_patch_ops = {
	.build_controls = simple_playback_build_controls,
	.build_pcms = simple_playback_build_pcms,
	.init = simple_playback_init,
	.free = simple_playback_free,
	.unsol_event = simple_hdmi_unsol_event,
};

static int patch_simple_hdmi(struct hda_codec *codec,
			     hda_nid_t cvt_nid, hda_nid_t pin_nid)
{
	struct hdmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	codec->spec = spec;

	spec->multiout.num_dacs = 0;  /* no analog */
	spec->multiout.max_channels = 2;
	spec->multiout.dig_out_nid = cvt_nid;
	spec->num_cvts = 1;
	spec->num_pins = 1;
	spec->cvts[0].cvt_nid = cvt_nid;
	spec->pins[0].pin_nid = pin_nid;
	spec->pcm_playback = simple_pcm_playback;

	codec->patch_ops = simple_hdmi_patch_ops;

	return 0;
}

static void nvhdmi_8ch_7x_set_info_frame_parameters(struct hda_codec *codec,
						    int channels)
{
	unsigned int chanmask;
	int chan = channels ? (channels - 1) : 1;

	switch (channels) {
	default:
	case 0:
	case 2:
		chanmask = 0x00;
		break;
	case 4:
		chanmask = 0x08;
		break;
	case 6:
		chanmask = 0x0b;
		break;
	case 8:
		chanmask = 0x13;
		break;
	}

	/* Set the audio infoframe channel allocation and checksum fields.  The
	 * channel count is computed implicitly by the hardware. */
	snd_hda_codec_write(codec, 0x1, 0,
			Nv_VERB_SET_Channel_Allocation, chanmask);

	snd_hda_codec_write(codec, 0x1, 0,
			Nv_VERB_SET_Info_Frame_Checksum,
			(0x71 - chan - chanmask));
}

static int nvhdmi_8ch_7x_pcm_close(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	int i;

	snd_hda_codec_write(codec, nvhdmi_master_con_nid_7x,
			0, AC_VERB_SET_CHANNEL_STREAMID, 0);
	for (i = 0; i < 4; i++) {
		/* set the stream id */
		snd_hda_codec_write(codec, nvhdmi_con_nids_7x[i], 0,
				AC_VERB_SET_CHANNEL_STREAMID, 0);
		/* set the stream format */
		snd_hda_codec_write(codec, nvhdmi_con_nids_7x[i], 0,
				AC_VERB_SET_STREAM_FORMAT, 0);
	}

	/* The audio hardware sends a channel count of 0x7 (8ch) when all the
	 * streams are disabled. */
	nvhdmi_8ch_7x_set_info_frame_parameters(codec, 8);

	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int nvhdmi_8ch_7x_pcm_prepare(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     unsigned int stream_tag,
				     unsigned int format,
				     struct snd_pcm_substream *substream)
{
	int chs;
	unsigned int dataDCC2, channel_id;
	int i;
	struct hdmi_spec *spec = codec->spec;
	struct hda_spdif_out *spdif;

	mutex_lock(&codec->spdif_mutex);
	spdif = snd_hda_spdif_out_of_nid(codec, spec->cvts[0].cvt_nid);

	chs = substream->runtime->channels;

	dataDCC2 = 0x2;

	/* turn off SPDIF once; otherwise the IEC958 bits won't be updated */
	if (codec->spdif_status_reset && (spdif->ctls & AC_DIG1_ENABLE))
		snd_hda_codec_write(codec,
				nvhdmi_master_con_nid_7x,
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				spdif->ctls & ~AC_DIG1_ENABLE & 0xff);

	/* set the stream id */
	snd_hda_codec_write(codec, nvhdmi_master_con_nid_7x, 0,
			AC_VERB_SET_CHANNEL_STREAMID, (stream_tag << 4) | 0x0);

	/* set the stream format */
	snd_hda_codec_write(codec, nvhdmi_master_con_nid_7x, 0,
			AC_VERB_SET_STREAM_FORMAT, format);

	/* turn on again (if needed) */
	/* enable and set the channel status audio/data flag */
	if (codec->spdif_status_reset && (spdif->ctls & AC_DIG1_ENABLE)) {
		snd_hda_codec_write(codec,
				nvhdmi_master_con_nid_7x,
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				spdif->ctls & 0xff);
		snd_hda_codec_write(codec,
				nvhdmi_master_con_nid_7x,
				0,
				AC_VERB_SET_DIGI_CONVERT_2, dataDCC2);
	}

	for (i = 0; i < 4; i++) {
		if (chs == 2)
			channel_id = 0;
		else
			channel_id = i * 2;

		/* turn off SPDIF once;
		 *otherwise the IEC958 bits won't be updated
		 */
		if (codec->spdif_status_reset &&
		(spdif->ctls & AC_DIG1_ENABLE))
			snd_hda_codec_write(codec,
				nvhdmi_con_nids_7x[i],
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				spdif->ctls & ~AC_DIG1_ENABLE & 0xff);
		/* set the stream id */
		snd_hda_codec_write(codec,
				nvhdmi_con_nids_7x[i],
				0,
				AC_VERB_SET_CHANNEL_STREAMID,
				(stream_tag << 4) | channel_id);
		/* set the stream format */
		snd_hda_codec_write(codec,
				nvhdmi_con_nids_7x[i],
				0,
				AC_VERB_SET_STREAM_FORMAT,
				format);
		/* turn on again (if needed) */
		/* enable and set the channel status audio/data flag */
		if (codec->spdif_status_reset &&
		(spdif->ctls & AC_DIG1_ENABLE)) {
			snd_hda_codec_write(codec,
					nvhdmi_con_nids_7x[i],
					0,
					AC_VERB_SET_DIGI_CONVERT_1,
					spdif->ctls & 0xff);
			snd_hda_codec_write(codec,
					nvhdmi_con_nids_7x[i],
					0,
					AC_VERB_SET_DIGI_CONVERT_2, dataDCC2);
		}
	}

	nvhdmi_8ch_7x_set_info_frame_parameters(codec, chs);

	mutex_unlock(&codec->spdif_mutex);
	return 0;
}

static const struct hda_pcm_stream nvhdmi_pcm_playback_8ch_7x = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = nvhdmi_master_con_nid_7x,
	.rates = SUPPORTED_RATES,
	.maxbps = SUPPORTED_MAXBPS,
	.formats = SUPPORTED_FORMATS,
	.ops = {
		.open = simple_playback_pcm_open,
		.close = nvhdmi_8ch_7x_pcm_close,
		.prepare = nvhdmi_8ch_7x_pcm_prepare
	},
};

static int patch_nvhdmi_2ch(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err = patch_simple_hdmi(codec, nvhdmi_master_con_nid_7x,
				    nvhdmi_master_pin_nid_7x);
	if (err < 0)
		return err;

	codec->patch_ops.init = nvhdmi_7x_init_2ch;
	/* override the PCM rates, etc, as the codec doesn't give full list */
	spec = codec->spec;
	spec->pcm_playback.rates = SUPPORTED_RATES;
	spec->pcm_playback.maxbps = SUPPORTED_MAXBPS;
	spec->pcm_playback.formats = SUPPORTED_FORMATS;
	return 0;
}

static int patch_nvhdmi_8ch_7x(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err = patch_nvhdmi_2ch(codec);
	if (err < 0)
		return err;
	spec = codec->spec;
	spec->multiout.max_channels = 8;
	spec->pcm_playback = nvhdmi_pcm_playback_8ch_7x;
	codec->patch_ops.init = nvhdmi_7x_init_8ch;

	/* Initialize the audio infoframe channel mask and checksum to something
	 * valid */
	nvhdmi_8ch_7x_set_info_frame_parameters(codec, 8);

	return 0;
}

/*
 * ATI-specific implementations
 *
 * FIXME: we may omit the whole this and use the generic code once after
 * it's confirmed to work.
 */

#define ATIHDMI_CVT_NID		0x02	/* audio converter */
#define ATIHDMI_PIN_NID		0x03	/* HDMI output pin */

static int atihdmi_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	int chans = substream->runtime->channels;
	int i, err;

	err = simple_playback_pcm_prepare(hinfo, codec, stream_tag, format,
					  substream);
	if (err < 0)
		return err;
	snd_hda_codec_write(codec, spec->cvts[0].cvt_nid, 0,
			    AC_VERB_SET_CVT_CHAN_COUNT, chans - 1);
	/* FIXME: XXX */
	for (i = 0; i < chans; i++) {
		snd_hda_codec_write(codec, spec->cvts[0].cvt_nid, 0,
				    AC_VERB_SET_HDMI_CHAN_SLOT,
				    (i << 4) | i);
	}
	return 0;
}

static int patch_atihdmi(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err = patch_simple_hdmi(codec, ATIHDMI_CVT_NID, ATIHDMI_PIN_NID);
	if (err < 0)
		return err;
	spec = codec->spec;
	spec->pcm_playback.ops.prepare = atihdmi_playback_pcm_prepare;
	return 0;
}

/* VIA HDMI Implementation */
#define VIAHDMI_CVT_NID	0x02	/* audio converter1 */
#define VIAHDMI_PIN_NID	0x03	/* HDMI output pin1 */

static int patch_via_hdmi(struct hda_codec *codec)
{
	return patch_simple_hdmi(codec, VIAHDMI_CVT_NID, VIAHDMI_PIN_NID);
}

/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_hdmi[] = {
{ .id = 0x1002793c, .name = "RS600 HDMI",	.patch = patch_atihdmi },
{ .id = 0x10027919, .name = "RS600 HDMI",	.patch = patch_atihdmi },
{ .id = 0x1002791a, .name = "RS690/780 HDMI",	.patch = patch_atihdmi },
{ .id = 0x1002aa01, .name = "R6xx HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x10951390, .name = "SiI1390 HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x10951392, .name = "SiI1392 HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x17e80047, .name = "Chrontel HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x10de0002, .name = "MCP77/78 HDMI",	.patch = patch_nvhdmi_8ch_7x },
{ .id = 0x10de0003, .name = "MCP77/78 HDMI",	.patch = patch_nvhdmi_8ch_7x },
{ .id = 0x10de0005, .name = "MCP77/78 HDMI",	.patch = patch_nvhdmi_8ch_7x },
{ .id = 0x10de0006, .name = "MCP77/78 HDMI",	.patch = patch_nvhdmi_8ch_7x },
{ .id = 0x10de0007, .name = "MCP79/7A HDMI",	.patch = patch_nvhdmi_8ch_7x },
{ .id = 0x10de000a, .name = "GPU 0a HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de000b, .name = "GPU 0b HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de000c, .name = "MCP89 HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x10de000d, .name = "GPU 0d HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0010, .name = "GPU 10 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0011, .name = "GPU 11 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0012, .name = "GPU 12 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0013, .name = "GPU 13 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0014, .name = "GPU 14 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0015, .name = "GPU 15 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0016, .name = "GPU 16 HDMI/DP",	.patch = patch_generic_hdmi },
/* 17 is known to be absent */
{ .id = 0x10de0018, .name = "GPU 18 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0019, .name = "GPU 19 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de001a, .name = "GPU 1a HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de001b, .name = "GPU 1b HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de001c, .name = "GPU 1c HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0040, .name = "GPU 40 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0041, .name = "GPU 41 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0042, .name = "GPU 42 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0043, .name = "GPU 43 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0044, .name = "GPU 44 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0051, .name = "GPU 51 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x10de0067, .name = "MCP67 HDMI",	.patch = patch_nvhdmi_2ch },
{ .id = 0x10de8001, .name = "MCP73 HDMI",	.patch = patch_nvhdmi_2ch },
{ .id = 0x11069f80, .name = "VX900 HDMI/DP",	.patch = patch_via_hdmi },
{ .id = 0x11069f81, .name = "VX900 HDMI/DP",	.patch = patch_via_hdmi },
{ .id = 0x11069f84, .name = "VX11 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x11069f85, .name = "VX11 HDMI/DP",	.patch = patch_generic_hdmi },
{ .id = 0x80860054, .name = "IbexPeak HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x80862801, .name = "Bearlake HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x80862802, .name = "Cantiga HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x80862803, .name = "Eaglelake HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x80862804, .name = "IbexPeak HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x80862805, .name = "CougarPoint HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x80862806, .name = "PantherPoint HDMI", .patch = patch_generic_hdmi },
{ .id = 0x80862807, .name = "Haswell HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x80862880, .name = "CedarTrail HDMI",	.patch = patch_generic_hdmi },
{ .id = 0x808629fb, .name = "Crestline HDMI",	.patch = patch_generic_hdmi },
{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:1002793c");
MODULE_ALIAS("snd-hda-codec-id:10027919");
MODULE_ALIAS("snd-hda-codec-id:1002791a");
MODULE_ALIAS("snd-hda-codec-id:1002aa01");
MODULE_ALIAS("snd-hda-codec-id:10951390");
MODULE_ALIAS("snd-hda-codec-id:10951392");
MODULE_ALIAS("snd-hda-codec-id:10de0002");
MODULE_ALIAS("snd-hda-codec-id:10de0003");
MODULE_ALIAS("snd-hda-codec-id:10de0005");
MODULE_ALIAS("snd-hda-codec-id:10de0006");
MODULE_ALIAS("snd-hda-codec-id:10de0007");
MODULE_ALIAS("snd-hda-codec-id:10de000a");
MODULE_ALIAS("snd-hda-codec-id:10de000b");
MODULE_ALIAS("snd-hda-codec-id:10de000c");
MODULE_ALIAS("snd-hda-codec-id:10de000d");
MODULE_ALIAS("snd-hda-codec-id:10de0010");
MODULE_ALIAS("snd-hda-codec-id:10de0011");
MODULE_ALIAS("snd-hda-codec-id:10de0012");
MODULE_ALIAS("snd-hda-codec-id:10de0013");
MODULE_ALIAS("snd-hda-codec-id:10de0014");
MODULE_ALIAS("snd-hda-codec-id:10de0015");
MODULE_ALIAS("snd-hda-codec-id:10de0016");
MODULE_ALIAS("snd-hda-codec-id:10de0018");
MODULE_ALIAS("snd-hda-codec-id:10de0019");
MODULE_ALIAS("snd-hda-codec-id:10de001a");
MODULE_ALIAS("snd-hda-codec-id:10de001b");
MODULE_ALIAS("snd-hda-codec-id:10de001c");
MODULE_ALIAS("snd-hda-codec-id:10de0040");
MODULE_ALIAS("snd-hda-codec-id:10de0041");
MODULE_ALIAS("snd-hda-codec-id:10de0042");
MODULE_ALIAS("snd-hda-codec-id:10de0043");
MODULE_ALIAS("snd-hda-codec-id:10de0044");
MODULE_ALIAS("snd-hda-codec-id:10de0051");
MODULE_ALIAS("snd-hda-codec-id:10de0067");
MODULE_ALIAS("snd-hda-codec-id:10de8001");
MODULE_ALIAS("snd-hda-codec-id:11069f80");
MODULE_ALIAS("snd-hda-codec-id:11069f81");
MODULE_ALIAS("snd-hda-codec-id:11069f84");
MODULE_ALIAS("snd-hda-codec-id:11069f85");
MODULE_ALIAS("snd-hda-codec-id:17e80047");
MODULE_ALIAS("snd-hda-codec-id:80860054");
MODULE_ALIAS("snd-hda-codec-id:80862801");
MODULE_ALIAS("snd-hda-codec-id:80862802");
MODULE_ALIAS("snd-hda-codec-id:80862803");
MODULE_ALIAS("snd-hda-codec-id:80862804");
MODULE_ALIAS("snd-hda-codec-id:80862805");
MODULE_ALIAS("snd-hda-codec-id:80862806");
MODULE_ALIAS("snd-hda-codec-id:80862807");
MODULE_ALIAS("snd-hda-codec-id:80862880");
MODULE_ALIAS("snd-hda-codec-id:808629fb");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HDMI HD-audio codec");
MODULE_ALIAS("snd-hda-codec-intelhdmi");
MODULE_ALIAS("snd-hda-codec-nvhdmi");
MODULE_ALIAS("snd-hda-codec-atihdmi");

static struct hda_codec_preset_list intel_list = {
	.preset = snd_hda_preset_hdmi,
	.owner = THIS_MODULE,
};

static int __init patch_hdmi_init(void)
{
	return snd_hda_add_codec_preset(&intel_list);
}

static void __exit patch_hdmi_exit(void)
{
	snd_hda_delete_codec_preset(&intel_list);
}

module_init(patch_hdmi_init)
module_exit(patch_hdmi_exit)
