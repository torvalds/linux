/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * Local helper functions
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __SOUND_HDA_LOCAL_H
#define __SOUND_HDA_LOCAL_H

/*
 * for mixer controls
 */
#define HDA_COMPOSE_AMP_VAL(nid,chs,idx,dir) ((nid) | ((chs)<<16) | ((dir)<<18) | ((idx)<<19))
/* mono volume with index (index=0,1,...) (channel=1,2) */
#define HDA_CODEC_VOLUME_MONO_IDX(xname, xcidx, nid, channel, xindex, direction) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xcidx,  \
	  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE | \
	  	    SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
	  	    SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK, \
	  .info = snd_hda_mixer_amp_volume_info, \
	  .get = snd_hda_mixer_amp_volume_get, \
	  .put = snd_hda_mixer_amp_volume_put, \
	  .tlv = { .c = snd_hda_mixer_amp_tlv },		\
	  .private_value = HDA_COMPOSE_AMP_VAL(nid, channel, xindex, direction) }
/* stereo volume with index */
#define HDA_CODEC_VOLUME_IDX(xname, xcidx, nid, xindex, direction) \
	HDA_CODEC_VOLUME_MONO_IDX(xname, xcidx, nid, 3, xindex, direction)
/* mono volume */
#define HDA_CODEC_VOLUME_MONO(xname, nid, channel, xindex, direction) \
	HDA_CODEC_VOLUME_MONO_IDX(xname, 0, nid, channel, xindex, direction)
/* stereo volume */
#define HDA_CODEC_VOLUME(xname, nid, xindex, direction) \
	HDA_CODEC_VOLUME_MONO(xname, nid, 3, xindex, direction)
/* mono mute switch with index (index=0,1,...) (channel=1,2) */
#define HDA_CODEC_MUTE_MONO_IDX(xname, xcidx, nid, channel, xindex, direction) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xcidx, \
	  .info = snd_hda_mixer_amp_switch_info, \
	  .get = snd_hda_mixer_amp_switch_get, \
	  .put = snd_hda_mixer_amp_switch_put, \
	  .private_value = HDA_COMPOSE_AMP_VAL(nid, channel, xindex, direction) }
/* stereo mute switch with index */
#define HDA_CODEC_MUTE_IDX(xname, xcidx, nid, xindex, direction) \
	HDA_CODEC_MUTE_MONO_IDX(xname, xcidx, nid, 3, xindex, direction)
/* mono mute switch */
#define HDA_CODEC_MUTE_MONO(xname, nid, channel, xindex, direction) \
	HDA_CODEC_MUTE_MONO_IDX(xname, 0, nid, channel, xindex, direction)
/* stereo mute switch */
#define HDA_CODEC_MUTE(xname, nid, xindex, direction) \
	HDA_CODEC_MUTE_MONO(xname, nid, 3, xindex, direction)

int snd_hda_mixer_amp_volume_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo);
int snd_hda_mixer_amp_volume_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
int snd_hda_mixer_amp_volume_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
int snd_hda_mixer_amp_tlv(struct snd_kcontrol *kcontrol, int op_flag, unsigned int size, unsigned int __user *tlv);
int snd_hda_mixer_amp_switch_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo);
int snd_hda_mixer_amp_switch_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
int snd_hda_mixer_amp_switch_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
/* lowlevel accessor with caching; use carefully */
int snd_hda_codec_amp_read(struct hda_codec *codec, hda_nid_t nid, int ch,
			   int direction, int index);
int snd_hda_codec_amp_update(struct hda_codec *codec, hda_nid_t nid, int ch,
			     int direction, int idx, int mask, int val);

/* mono switch binding multiple inputs */
#define HDA_BIND_MUTE_MONO(xname, nid, channel, indices, direction) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = snd_hda_mixer_amp_switch_info, \
	  .get = snd_hda_mixer_bind_switch_get, \
	  .put = snd_hda_mixer_bind_switch_put, \
	  .private_value = HDA_COMPOSE_AMP_VAL(nid, channel, indices, direction) }

/* stereo switch binding multiple inputs */
#define HDA_BIND_MUTE(xname,nid,indices,dir) HDA_BIND_MUTE_MONO(xname,nid,3,indices,dir)

int snd_hda_mixer_bind_switch_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
int snd_hda_mixer_bind_switch_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);

int snd_hda_create_spdif_out_ctls(struct hda_codec *codec, hda_nid_t nid);
int snd_hda_create_spdif_in_ctls(struct hda_codec *codec, hda_nid_t nid);

/*
 * input MUX helper
 */
#define HDA_MAX_NUM_INPUTS	16
struct hda_input_mux_item {
	const char *label;
	unsigned int index;
};
struct hda_input_mux {
	unsigned int num_items;
	struct hda_input_mux_item items[HDA_MAX_NUM_INPUTS];
};

int snd_hda_input_mux_info(const struct hda_input_mux *imux, struct snd_ctl_elem_info *uinfo);
int snd_hda_input_mux_put(struct hda_codec *codec, const struct hda_input_mux *imux,
			  struct snd_ctl_elem_value *ucontrol, hda_nid_t nid,
			  unsigned int *cur_val);

/*
 * Channel mode helper
 */
struct hda_channel_mode {
	int channels;
	const struct hda_verb *sequence;
};

int snd_hda_ch_mode_info(struct hda_codec *codec, struct snd_ctl_elem_info *uinfo,
			 const struct hda_channel_mode *chmode, int num_chmodes);
int snd_hda_ch_mode_get(struct hda_codec *codec, struct snd_ctl_elem_value *ucontrol,
			const struct hda_channel_mode *chmode, int num_chmodes,
			int max_channels);
int snd_hda_ch_mode_put(struct hda_codec *codec, struct snd_ctl_elem_value *ucontrol,
			const struct hda_channel_mode *chmode, int num_chmodes,
			int *max_channelsp);

/*
 * Multi-channel / digital-out PCM helper
 */

enum { HDA_FRONT, HDA_REAR, HDA_CLFE, HDA_SIDE }; /* index for dac_nidx */
enum { HDA_DIG_NONE, HDA_DIG_EXCLUSIVE, HDA_DIG_ANALOG_DUP }; /* dig_out_used */

struct hda_multi_out {
	int num_dacs;		/* # of DACs, must be more than 1 */
	hda_nid_t *dac_nids;	/* DAC list */
	hda_nid_t hp_nid;	/* optional DAC for HP, 0 when not exists */
	hda_nid_t extra_out_nid[3];	/* optional DACs, 0 when not exists */
	hda_nid_t dig_out_nid;	/* digital out audio widget */
	int max_channels;	/* currently supported analog channels */
	int dig_out_used;	/* current usage of digital out (HDA_DIG_XXX) */
};

int snd_hda_multi_out_dig_open(struct hda_codec *codec, struct hda_multi_out *mout);
int snd_hda_multi_out_dig_close(struct hda_codec *codec, struct hda_multi_out *mout);
int snd_hda_multi_out_dig_prepare(struct hda_codec *codec,
				  struct hda_multi_out *mout,
				  unsigned int stream_tag,
				  unsigned int format,
				  struct snd_pcm_substream *substream);
int snd_hda_multi_out_analog_open(struct hda_codec *codec, struct hda_multi_out *mout,
				  struct snd_pcm_substream *substream);
int snd_hda_multi_out_analog_prepare(struct hda_codec *codec, struct hda_multi_out *mout,
				     unsigned int stream_tag,
				     unsigned int format,
				     struct snd_pcm_substream *substream);
int snd_hda_multi_out_analog_cleanup(struct hda_codec *codec, struct hda_multi_out *mout);

/*
 * generic codec parser
 */
int snd_hda_parse_generic_codec(struct hda_codec *codec);

/*
 * generic proc interface
 */
#ifdef CONFIG_PROC_FS
int snd_hda_codec_proc_new(struct hda_codec *codec);
#else
static inline int snd_hda_codec_proc_new(struct hda_codec *codec) { return 0; }
#endif

/*
 * Misc
 */
int snd_hda_check_board_config(struct hda_codec *codec, int num_configs,
			       const char **modelnames,
			       const struct snd_pci_quirk *pci_list);
int snd_hda_add_new_ctls(struct hda_codec *codec, struct snd_kcontrol_new *knew);

/*
 * power management
 */
#ifdef CONFIG_PM
int snd_hda_resume_ctls(struct hda_codec *codec, struct snd_kcontrol_new *knew);
int snd_hda_resume_spdif_out(struct hda_codec *codec);
int snd_hda_resume_spdif_in(struct hda_codec *codec);
#endif

/*
 * unsolicited event handler
 */

#define HDA_UNSOL_QUEUE_SIZE	64

struct hda_bus_unsolicited {
	/* ring buffer */
	u32 queue[HDA_UNSOL_QUEUE_SIZE * 2];
	unsigned int rp, wp;

	/* workqueue */
	struct work_struct work;
	struct hda_bus *bus;
};

/*
 * Helper for automatic ping configuration
 */

enum {
	AUTO_PIN_MIC,
	AUTO_PIN_FRONT_MIC,
	AUTO_PIN_LINE,
	AUTO_PIN_FRONT_LINE,
	AUTO_PIN_CD,
	AUTO_PIN_AUX,
	AUTO_PIN_LAST
};

enum {
	AUTO_PIN_LINE_OUT,
	AUTO_PIN_SPEAKER_OUT,
	AUTO_PIN_HP_OUT
};

extern const char *auto_pin_cfg_labels[AUTO_PIN_LAST];

struct auto_pin_cfg {
	int line_outs;
	hda_nid_t line_out_pins[5]; /* sorted in the order of Front/Surr/CLFE/Side */
	int speaker_outs;
	hda_nid_t speaker_pins[5];
	int hp_outs;
	int line_out_type;	/* AUTO_PIN_XXX_OUT */
	hda_nid_t hp_pins[5];
	hda_nid_t input_pins[AUTO_PIN_LAST];
	hda_nid_t dig_out_pin;
	hda_nid_t dig_in_pin;
};

#define get_defcfg_connect(cfg) ((cfg & AC_DEFCFG_PORT_CONN) >> AC_DEFCFG_PORT_CONN_SHIFT)
#define get_defcfg_association(cfg) ((cfg & AC_DEFCFG_DEF_ASSOC) >> AC_DEFCFG_ASSOC_SHIFT)
#define get_defcfg_location(cfg) ((cfg & AC_DEFCFG_LOCATION) >> AC_DEFCFG_LOCATION_SHIFT)
#define get_defcfg_sequence(cfg) (cfg & AC_DEFCFG_SEQUENCE)
#define get_defcfg_device(cfg) ((cfg & AC_DEFCFG_DEVICE) >> AC_DEFCFG_DEVICE_SHIFT)

int snd_hda_parse_pin_def_config(struct hda_codec *codec, struct auto_pin_cfg *cfg,
				 hda_nid_t *ignore_nids);

/* amp values */
#define AMP_IN_MUTE(idx)	(0x7080 | ((idx)<<8))
#define AMP_IN_UNMUTE(idx)	(0x7000 | ((idx)<<8))
#define AMP_OUT_MUTE	0xb080
#define AMP_OUT_UNMUTE	0xb000
#define AMP_OUT_ZERO	0xb000
/* pinctl values */
#define PIN_IN		0x20
#define PIN_VREF80	0x24
#define PIN_VREF50	0x21
#define PIN_OUT		0x40
#define PIN_HP		0xc0
#define PIN_HP_AMP	0x80

/*
 * get widget capabilities
 */
static inline u32 get_wcaps(struct hda_codec *codec, hda_nid_t nid)
{
	if (nid < codec->start_nid ||
	    nid >= codec->start_nid + codec->num_nodes)
		return snd_hda_param_read(codec, nid, AC_PAR_AUDIO_WIDGET_CAP);
	return codec->wcaps[nid - codec->start_nid];
}


#endif /* __SOUND_HDA_LOCAL_H */
