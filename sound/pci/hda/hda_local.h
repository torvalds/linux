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
	  .info = snd_hda_mixer_amp_volume_info, \
	  .get = snd_hda_mixer_amp_volume_get, \
	  .put = snd_hda_mixer_amp_volume_put, \
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

int snd_hda_mixer_amp_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo);
int snd_hda_mixer_amp_volume_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol);
int snd_hda_mixer_amp_volume_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol);
int snd_hda_mixer_amp_switch_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo);
int snd_hda_mixer_amp_switch_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol);
int snd_hda_mixer_amp_switch_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol);

/* mono switch binding multiple inputs */
#define HDA_BIND_MUTE_MONO(xname, nid, channel, indices, direction) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = snd_hda_mixer_amp_switch_info, \
	  .get = snd_hda_mixer_bind_switch_get, \
	  .put = snd_hda_mixer_bind_switch_put, \
	  .private_value = HDA_COMPOSE_AMP_VAL(nid, channel, indices, direction) }

/* stereo switch binding multiple inputs */
#define HDA_BIND_MUTE(xname,nid,indices,dir) HDA_BIND_MUTE_MONO(xname,nid,3,indices,dir)

int snd_hda_mixer_bind_switch_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol);
int snd_hda_mixer_bind_switch_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol);

int snd_hda_create_spdif_out_ctls(struct hda_codec *codec, hda_nid_t nid);
int snd_hda_create_spdif_in_ctls(struct hda_codec *codec, hda_nid_t nid);

/*
 * input MUX helper
 */
#define HDA_MAX_NUM_INPUTS	8
struct hda_input_mux_item {
	const char *label;
	unsigned int index;
};
struct hda_input_mux {
	unsigned int num_items;
	struct hda_input_mux_item items[HDA_MAX_NUM_INPUTS];
};

int snd_hda_input_mux_info(const struct hda_input_mux *imux, snd_ctl_elem_info_t *uinfo);
int snd_hda_input_mux_put(struct hda_codec *codec, const struct hda_input_mux *imux,
			  snd_ctl_elem_value_t *ucontrol, hda_nid_t nid,
			  unsigned int *cur_val);

/*
 * Multi-channel / digital-out PCM helper
 */

enum { HDA_FRONT, HDA_REAR, HDA_CLFE, HDA_SIDE }; /* index for dac_nidx */
enum { HDA_DIG_NONE, HDA_DIG_EXCLUSIVE, HDA_DIG_ANALOG_DUP }; /* dig_out_used */

struct hda_multi_out {
	int num_dacs;		/* # of DACs, must be more than 1 */
	hda_nid_t *dac_nids;	/* DAC list */
	hda_nid_t hp_nid;	/* optional DAC for HP, 0 when not exists */
	hda_nid_t dig_out_nid;	/* digital out audio widget */
	int max_channels;	/* currently supported analog channels */
	int dig_out_used;	/* current usage of digital out (HDA_DIG_XXX) */
};

int snd_hda_multi_out_dig_open(struct hda_codec *codec, struct hda_multi_out *mout);
int snd_hda_multi_out_dig_close(struct hda_codec *codec, struct hda_multi_out *mout);
int snd_hda_multi_out_analog_open(struct hda_codec *codec, struct hda_multi_out *mout,
				  snd_pcm_substream_t *substream);
int snd_hda_multi_out_analog_prepare(struct hda_codec *codec, struct hda_multi_out *mout,
				     unsigned int stream_tag,
				     unsigned int format,
				     snd_pcm_substream_t *substream);
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
struct hda_board_config {
	const char *modelname;
	int config;
	unsigned short pci_subvendor;
	unsigned short pci_subdevice;
};

int snd_hda_check_board_config(struct hda_codec *codec, const struct hda_board_config *tbl);
int snd_hda_add_new_ctls(struct hda_codec *codec, snd_kcontrol_new_t *knew);

/*
 * power management
 */
#ifdef CONFIG_PM
int snd_hda_resume_ctls(struct hda_codec *codec, snd_kcontrol_new_t *knew);
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
	struct workqueue_struct *workq;
	struct work_struct work;
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

struct auto_pin_cfg {
	int line_outs;
	hda_nid_t line_out_pins[4]; /* sorted in the order of Front/Surr/CLFE/Side */
	hda_nid_t hp_pin;
	hda_nid_t input_pins[AUTO_PIN_LAST];
	hda_nid_t dig_out_pin;
	hda_nid_t dig_in_pin;
};

#define get_defcfg_connect(cfg) ((cfg & AC_DEFCFG_PORT_CONN) >> AC_DEFCFG_PORT_CONN_SHIFT)
#define get_defcfg_association(cfg) ((cfg & AC_DEFCFG_DEF_ASSOC) >> AC_DEFCFG_ASSOC_SHIFT)
#define get_defcfg_location(cfg) ((cfg & AC_DEFCFG_LOCATION) >> AC_DEFCFG_LOCATION_SHIFT)
#define get_defcfg_sequence(cfg) (cfg & AC_DEFCFG_SEQUENCE)
#define get_defcfg_device(cfg) ((cfg & AC_DEFCFG_DEVICE) >> AC_DEFCFG_DEVICE_SHIFT)

int snd_hda_parse_pin_def_config(struct hda_codec *codec, struct auto_pin_cfg *cfg);

#endif /* __SOUND_HDA_LOCAL_H */
