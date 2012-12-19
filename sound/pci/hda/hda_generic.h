/*
 * Generic BIOS auto-parser helper functions for HD-audio
 *
 * Copyright (c) 2012 Takashi Iwai <tiwai@suse.de>
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __SOUND_HDA_GENERIC_H
#define __SOUND_HDA_GENERIC_H

/* unsol event tags */
enum {
	HDA_GEN_HP_EVENT, HDA_GEN_FRONT_EVENT, HDA_GEN_MIC_EVENT,
	HDA_GEN_LAST_EVENT = HDA_GEN_MIC_EVENT
};

/* table entry for multi-io paths */
struct hda_multi_io {
	hda_nid_t pin;		/* multi-io widget pin NID */
	hda_nid_t dac;		/* DAC to be connected */
	unsigned int ctl_in;	/* cached input-pin control value */
};

/* Widget connection path
 *
 * For output, stored in the order of DAC -> ... -> pin,
 * for input, pin -> ... -> ADC.
 *
 * idx[i] contains the source index number to select on of the widget path[i];
 * e.g. idx[1] is the index of the DAC (path[0]) selected by path[1] widget
 * multi[] indicates whether it's a selector widget with multi-connectors
 * (i.e. the connection selection is mandatory)
 * vol_ctl and mute_ctl contains the NIDs for the assigned mixers
 */

#define MAX_NID_PATH_DEPTH	5

enum {
	NID_PATH_VOL_CTL,
	NID_PATH_MUTE_CTL,
	NID_PATH_BOOST_CTL,
	NID_PATH_NUM_CTLS
};

struct nid_path {
	int depth;
	hda_nid_t path[MAX_NID_PATH_DEPTH];
	unsigned char idx[MAX_NID_PATH_DEPTH];
	unsigned char multi[MAX_NID_PATH_DEPTH];
	unsigned int ctls[NID_PATH_NUM_CTLS]; /* NID_PATH_XXX_CTL */
	bool active;
};

/* mic/line-in auto switching entry */

#define MAX_AUTO_MIC_PINS	3

struct automic_entry {
	hda_nid_t pin;		/* pin */
	int idx;		/* imux index, -1 = invalid */
	unsigned int attr;	/* pin attribute (INPUT_PIN_ATTR_*) */
};

struct hda_gen_spec {
	char stream_name_analog[32];	/* analog PCM stream */
	const struct hda_pcm_stream *stream_analog_playback;
	const struct hda_pcm_stream *stream_analog_capture;
	const struct hda_pcm_stream *stream_analog_alt_playback;
	const struct hda_pcm_stream *stream_analog_alt_capture;

	char stream_name_digital[32];	/* digital PCM stream */
	const struct hda_pcm_stream *stream_digital_playback;
	const struct hda_pcm_stream *stream_digital_capture;

	/* playback */
	struct hda_multi_out multiout;	/* playback set-up
					 * max_channels, dacs must be set
					 * dig_out_nid and hp_nid are optional
					 */
	hda_nid_t alt_dac_nid;
	hda_nid_t slave_dig_outs[3];	/* optional - for auto-parsing */
	int dig_out_type;

	/* capture */
	unsigned int num_adc_nids;
	hda_nid_t adc_nids[AUTO_CFG_MAX_OUTS];
	hda_nid_t dig_in_nid;		/* digital-in NID; optional */
	hda_nid_t mixer_nid;		/* analog-mixer NID */

	/* capture setup for dynamic dual-adc switch */
	hda_nid_t cur_adc;
	unsigned int cur_adc_stream_tag;
	unsigned int cur_adc_format;

	/* capture source */
	struct hda_input_mux input_mux;
	unsigned int cur_mux[3];

	/* channel model */
	const struct hda_channel_mode *channel_mode;
	int num_channel_mode;
	int const_channel_count;	/* min. channel count (for speakers) */
	int ext_channel_count;		/* current channel count for multi-io */

	/* PCM information */
	struct hda_pcm pcm_rec[3];	/* used in build_pcms() */

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	struct snd_array kctls;
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];
	hda_nid_t imux_pins[HDA_MAX_NUM_INPUTS];
	unsigned int dyn_adc_idx[HDA_MAX_NUM_INPUTS];
	hda_nid_t shared_mic_vref_pin;

	/* DAC list */
	int num_all_dacs;
	hda_nid_t all_dacs[16];

	/* path list */
	struct snd_array paths;

	/* auto-mic stuff */
	int am_num_entries;
	struct automic_entry am_entry[MAX_AUTO_MIC_PINS];

	/* for pin sensing */
	unsigned int hp_jack_present:1;
	unsigned int line_jack_present:1;
	unsigned int master_mute:1;
	unsigned int auto_mic:1;
	unsigned int automute_speaker:1; /* automute speaker outputs */
	unsigned int automute_lo:1; /* automute LO outputs */
	unsigned int detect_hp:1;	/* Headphone detection enabled */
	unsigned int detect_lo:1;	/* Line-out detection enabled */
	unsigned int automute_speaker_possible:1; /* there are speakers and either LO or HP */
	unsigned int automute_lo_possible:1;	  /* there are line outs and HP */
	unsigned int keep_vref_in_automute:1; /* Don't clear VREF in automute */
	unsigned int line_in_auto_switch:1; /* allow line-in auto switch */

	/* other flags */
	unsigned int need_dac_fix:1; /* need to limit DACs for multi channels */
	unsigned int no_analog:1; /* digital I/O only */
	unsigned int dyn_adc_switch:1; /* switch ADCs (for ALC275) */
	unsigned int shared_mic_hp:1; /* HP/Mic-in sharing */
	unsigned int no_primary_hp:1; /* Don't prefer HP pins to speaker pins */
	unsigned int multi_cap_vol:1; /* allow multiple capture xxx volumes */
	unsigned int inv_dmic_split:1; /* inverted dmic w/a for conexant */
	unsigned int own_eapd_ctl:1; /* set EAPD by own function */

	unsigned int parse_flags; /* passed to snd_hda_parse_pin_defcfg() */

	/* for virtual master */
	hda_nid_t vmaster_nid;
	struct hda_vmaster_mute_hook vmaster_mute;
#ifdef CONFIG_PM
	struct hda_loopback_check loopback;
	int num_loopbacks;
	struct hda_amp_list loopback_list[8];
#endif

	/* multi-io */
	int multi_ios;
	struct hda_multi_io multi_io[4];

	/* bind volumes */
	struct snd_array bind_ctls;

	/* hooks */
	void (*init_hook)(struct hda_codec *codec);
	void (*automute_hook)(struct hda_codec *codec);
	void (*cap_sync_hook)(struct hda_codec *codec);
};

int snd_hda_gen_spec_init(struct hda_gen_spec *spec);
void snd_hda_gen_spec_free(struct hda_gen_spec *spec);

int snd_hda_gen_init(struct hda_codec *codec);

struct nid_path *snd_hda_get_nid_path(struct hda_codec *codec,
				      hda_nid_t from_nid, hda_nid_t to_nid);
bool snd_hda_parse_nid_path(struct hda_codec *codec, hda_nid_t from_nid,
			    hda_nid_t to_nid, int with_aa_mix,
			    struct nid_path *path);
struct nid_path *
snd_hda_add_new_path(struct hda_codec *codec, hda_nid_t from_nid,
		     hda_nid_t to_nid, int with_aa_mix);
void snd_hda_activate_path(struct hda_codec *codec, struct nid_path *path,
			   bool enable, bool add_aamix);

int snd_hda_gen_parse_auto_config(struct hda_codec *codec,
				  const hda_nid_t *ignore_nids);
int snd_hda_gen_build_controls(struct hda_codec *codec);
int snd_hda_gen_build_pcms(struct hda_codec *codec);

#endif /* __SOUND_HDA_GENERIC_H */
