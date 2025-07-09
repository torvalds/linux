/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic BIOS auto-parser helper functions for HD-audio
 *
 * Copyright (c) 2012 Takashi Iwai <tiwai@suse.de>
 */

#ifndef __SOUND_HDA_GENERIC_H
#define __SOUND_HDA_GENERIC_H

#include <linux/leds.h>
#include "hda_auto_parser.h"

struct hda_jack_callback;

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

#define MAX_NID_PATH_DEPTH	10

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
	bool active:1;		/* activated by driver */
	bool pin_enabled:1;	/* pins are enabled */
	bool pin_fixed:1;	/* path with fixed pin */
	bool stream_enabled:1;	/* stream is active */
};

/* mic/line-in auto switching entry */

#define MAX_AUTO_MIC_PINS	3

struct automic_entry {
	hda_nid_t pin;		/* pin */
	int idx;		/* imux index, -1 = invalid */
	unsigned int attr;	/* pin attribute (INPUT_PIN_ATTR_*) */
};

/* active stream id */
enum { STREAM_MULTI_OUT, STREAM_INDEP_HP };

/* PCM hook action */
enum {
	HDA_GEN_PCM_ACT_OPEN,
	HDA_GEN_PCM_ACT_PREPARE,
	HDA_GEN_PCM_ACT_CLEANUP,
	HDA_GEN_PCM_ACT_CLOSE,
};

/* DAC assignment badness table */
struct badness_table {
	int no_primary_dac;	/* no primary DAC */
	int no_dac;		/* no secondary DACs */
	int shared_primary;	/* primary DAC is shared with main output */
	int shared_surr;	/* secondary DAC shared with main or primary */
	int shared_clfe;	/* third DAC shared with main or primary */
	int shared_surr_main;	/* secondary DAC sahred with main/DAC0 */
};

extern const struct badness_table hda_main_out_badness;
extern const struct badness_table hda_extra_out_badness;

struct hda_gen_spec {
	char stream_name_analog[32];	/* analog PCM stream */
	const struct hda_pcm_stream *stream_analog_playback;
	const struct hda_pcm_stream *stream_analog_capture;

	char stream_name_alt_analog[32]; /* alternative analog PCM stream */
	const struct hda_pcm_stream *stream_analog_alt_playback;
	const struct hda_pcm_stream *stream_analog_alt_capture;

	char stream_name_digital[32];	/* digital PCM stream */
	const struct hda_pcm_stream *stream_digital_playback;
	const struct hda_pcm_stream *stream_digital_capture;

	/* PCM */
	unsigned int active_streams;
	struct mutex pcm_mutex;

	/* playback */
	struct hda_multi_out multiout;	/* playback set-up
					 * max_channels, dacs must be set
					 * dig_out_nid and hp_nid are optional
					 */
	hda_nid_t alt_dac_nid;
	hda_nid_t follower_dig_outs[3];	/* optional - for auto-parsing */
	int dig_out_type;

	/* capture */
	unsigned int num_adc_nids;
	hda_nid_t adc_nids[AUTO_CFG_MAX_INS];
	hda_nid_t dig_in_nid;		/* digital-in NID; optional */
	hda_nid_t mixer_nid;		/* analog-mixer NID */
	hda_nid_t mixer_merge_nid;	/* aamix merge-point NID (optional) */
	const char *input_labels[HDA_MAX_NUM_INPUTS];
	int input_label_idxs[HDA_MAX_NUM_INPUTS];

	/* capture setup for dynamic dual-adc switch */
	hda_nid_t cur_adc;
	unsigned int cur_adc_stream_tag;
	unsigned int cur_adc_format;

	/* capture source */
	struct hda_input_mux input_mux;
	unsigned int cur_mux[3];

	/* channel model */
	/* min_channel_count contains the minimum channel count for primary
	 * outputs.  When multi_ios is set, the channels can be configured
	 * between min_channel_count and (min_channel_count + multi_ios * 2).
	 *
	 * ext_channel_count contains the current channel count of the primary
	 * out.  This varies in the range above.
	 *
	 * Meanwhile, const_channel_count is the channel count for all outputs
	 * including headphone and speakers.  It's a constant value, and the
	 * PCM is set up as max(ext_channel_count, const_channel_count).
	 */
	int min_channel_count;		/* min. channel count for primary out */
	int ext_channel_count;		/* current channel count for primary */
	int const_channel_count;	/* channel count for all */

	/* PCM information */
	struct hda_pcm *pcm_rec[3];	/* used in build_pcms() */

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	struct snd_array kctls;
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];
	hda_nid_t imux_pins[HDA_MAX_NUM_INPUTS];
	unsigned int dyn_adc_idx[HDA_MAX_NUM_INPUTS];
	/* shared hp/mic */
	hda_nid_t shared_mic_vref_pin;
	hda_nid_t hp_mic_pin;
	int hp_mic_mux_idx;

	/* DAC/ADC lists */
	int num_all_dacs;
	hda_nid_t all_dacs[16];
	int num_all_adcs;
	hda_nid_t all_adcs[AUTO_CFG_MAX_INS];

	/* path list */
	struct snd_array paths;

	/* path indices */
	int out_paths[AUTO_CFG_MAX_OUTS];
	int hp_paths[AUTO_CFG_MAX_OUTS];
	int speaker_paths[AUTO_CFG_MAX_OUTS];
	int aamix_out_paths[3];
	int digout_paths[AUTO_CFG_MAX_OUTS];
	int input_paths[HDA_MAX_NUM_INPUTS][AUTO_CFG_MAX_INS];
	int loopback_paths[HDA_MAX_NUM_INPUTS];
	int loopback_merge_path;
	int digin_path;

	/* auto-mic stuff */
	int am_num_entries;
	struct automic_entry am_entry[MAX_AUTO_MIC_PINS];

	/* for pin sensing */
	/* current status; set in hda_generic.c */
	unsigned int hp_jack_present:1;
	unsigned int line_jack_present:1;
	unsigned int speaker_muted:1; /* current status of speaker mute */
	unsigned int line_out_muted:1; /* current status of LO mute */

	/* internal states of automute / autoswitch behavior */
	unsigned int auto_mic:1;
	unsigned int automute_speaker:1; /* automute speaker outputs */
	unsigned int automute_lo:1; /* automute LO outputs */

	/* capabilities detected by parser */
	unsigned int detect_hp:1;	/* Headphone detection enabled */
	unsigned int detect_lo:1;	/* Line-out detection enabled */
	unsigned int automute_speaker_possible:1; /* there are speakers and either LO or HP */
	unsigned int automute_lo_possible:1;	  /* there are line outs and HP */

	/* additional parameters set by codec drivers */
	unsigned int master_mute:1;	/* master mute over all */
	unsigned int keep_vref_in_automute:1; /* Don't clear VREF in automute */
	unsigned int line_in_auto_switch:1; /* allow line-in auto switch */
	unsigned int auto_mute_via_amp:1; /* auto-mute via amp instead of pinctl */

	/* parser behavior flags; set before snd_hda_gen_parse_auto_config() */
	unsigned int suppress_auto_mute:1; /* suppress input jack auto mute */
	unsigned int suppress_auto_mic:1; /* suppress input jack auto switch */

	/* other parse behavior flags */
	unsigned int need_dac_fix:1; /* need to limit DACs for multi channels */
	unsigned int hp_mic:1; /* Allow HP as a mic-in */
	unsigned int suppress_hp_mic_detect:1; /* Don't detect HP/mic */
	unsigned int no_primary_hp:1; /* Don't prefer HP pins to speaker pins */
	unsigned int no_multi_io:1; /* Don't try multi I/O config */
	unsigned int multi_cap_vol:1; /* allow multiple capture xxx volumes */
	unsigned int inv_dmic_split:1; /* inverted dmic w/a for conexant */
	unsigned int own_eapd_ctl:1; /* set EAPD by own function */
	unsigned int keep_eapd_on:1; /* don't turn off EAPD automatically */
	unsigned int vmaster_mute_led:1; /* add SPK-LED flag to vmaster mute switch */
	unsigned int mic_mute_led:1; /* add MIC-LED flag to capture mute switch */
	unsigned int indep_hp:1; /* independent HP supported */
	unsigned int prefer_hp_amp:1; /* enable HP amp for speaker if any */
	unsigned int add_stereo_mix_input:2; /* add aamix as a capture src */
	unsigned int add_jack_modes:1; /* add i/o jack mode enum ctls */
	unsigned int power_down_unused:1; /* power down unused widgets */
	unsigned int dac_min_mute:1; /* minimal = mute for DACs */
	unsigned int suppress_vmaster:1; /* don't create vmaster kctls */

	/* other internal flags */
	unsigned int no_analog:1; /* digital I/O only */
	unsigned int dyn_adc_switch:1; /* switch ADCs (for ALC275) */
	unsigned int indep_hp_enabled:1; /* independent HP enabled */
	unsigned int have_aamix_ctl:1;
	unsigned int hp_mic_jack_modes:1;
	unsigned int skip_verbs:1; /* don't apply verbs at snd_hda_gen_init() */

	/* additional mute flags (only effective with auto_mute_via_amp=1) */
	u64 mute_bits;

	/* bitmask for skipping volume controls */
	u64 out_vol_mask;

	/* badness tables for output path evaluations */
	const struct badness_table *main_out_badness;
	const struct badness_table *extra_out_badness;

	/* preferred pin/DAC pairs; an array of paired NIDs */
	const hda_nid_t *preferred_dacs;

	/* loopback mixing mode */
	bool aamix_mode;

	/* digital beep */
	hda_nid_t beep_nid;

	/* for virtual master */
	hda_nid_t vmaster_nid;
	unsigned int vmaster_tlv[4];
	struct hda_vmaster_mute_hook vmaster_mute;

	struct hda_loopback_check loopback;
	struct snd_array loopback_list;

	/* multi-io */
	int multi_ios;
	struct hda_multi_io multi_io[4];

	/* hooks */
	void (*init_hook)(struct hda_codec *codec);
	void (*automute_hook)(struct hda_codec *codec);
	void (*cap_sync_hook)(struct hda_codec *codec,
			      struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol);

	/* PCM hooks */
	void (*pcm_playback_hook)(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream,
				  int action);
	void (*pcm_capture_hook)(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream,
				 int action);

	/* automute / autoswitch hooks */
	void (*hp_automute_hook)(struct hda_codec *codec,
				 struct hda_jack_callback *cb);
	void (*line_automute_hook)(struct hda_codec *codec,
				   struct hda_jack_callback *cb);
	void (*mic_autoswitch_hook)(struct hda_codec *codec,
				    struct hda_jack_callback *cb);

	/* leds */
	struct led_classdev *led_cdevs[NUM_AUDIO_LEDS];
};

/* values for add_stereo_mix_input flag */
enum {
	HDA_HINT_STEREO_MIX_DISABLE,	/* No stereo mix input */
	HDA_HINT_STEREO_MIX_ENABLE,	/* Add stereo mix input */
	HDA_HINT_STEREO_MIX_AUTO,	/* Add only if auto-mic is disabled */
};

int snd_hda_gen_spec_init(struct hda_gen_spec *spec);

int snd_hda_gen_init(struct hda_codec *codec);
void snd_hda_gen_free(struct hda_codec *codec);

int snd_hda_get_path_idx(struct hda_codec *codec, struct nid_path *path);
struct nid_path *snd_hda_get_path_from_idx(struct hda_codec *codec, int idx);
struct nid_path *
snd_hda_add_new_path(struct hda_codec *codec, hda_nid_t from_nid,
		     hda_nid_t to_nid, int anchor_nid);
void snd_hda_activate_path(struct hda_codec *codec, struct nid_path *path,
			   bool enable, bool add_aamix);

struct snd_kcontrol_new *
snd_hda_gen_add_kctl(struct hda_gen_spec *spec, const char *name,
		     const struct snd_kcontrol_new *temp);

int snd_hda_gen_parse_auto_config(struct hda_codec *codec,
				  struct auto_pin_cfg *cfg);
int snd_hda_gen_build_controls(struct hda_codec *codec);
int snd_hda_gen_build_pcms(struct hda_codec *codec);

/* standard jack event callbacks */
void snd_hda_gen_hp_automute(struct hda_codec *codec,
			     struct hda_jack_callback *jack);
void snd_hda_gen_line_automute(struct hda_codec *codec,
			       struct hda_jack_callback *jack);
void snd_hda_gen_mic_autoswitch(struct hda_codec *codec,
				struct hda_jack_callback *jack);
void snd_hda_gen_update_outputs(struct hda_codec *codec);

int snd_hda_gen_check_power_status(struct hda_codec *codec, hda_nid_t nid);
unsigned int snd_hda_gen_path_power_filter(struct hda_codec *codec,
					   hda_nid_t nid,
					   unsigned int power_state);
void snd_hda_gen_stream_pm(struct hda_codec *codec, hda_nid_t nid, bool on);
int snd_hda_gen_fix_pin_power(struct hda_codec *codec, hda_nid_t pin);

int snd_hda_gen_add_mute_led_cdev(struct hda_codec *codec,
				  int (*callback)(struct led_classdev *,
						  enum led_brightness));
int snd_hda_gen_add_micmute_led_cdev(struct hda_codec *codec,
				     int (*callback)(struct led_classdev *,
						     enum led_brightness));
bool snd_hda_gen_shutup_speakers(struct hda_codec *codec);

#endif /* __SOUND_HDA_GENERIC_H */
