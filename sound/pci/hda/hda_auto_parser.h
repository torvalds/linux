/*
 * BIOS auto-parser helper functions for HD-audio
 *
 * Copyright (c) 2012 Takashi Iwai <tiwai@suse.de>
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __SOUND_HDA_AUTO_PARSER_H
#define __SOUND_HDA_AUTO_PARSER_H

/*
 * Helper for automatic pin configuration
 */

enum {
	AUTO_PIN_MIC,
	AUTO_PIN_LINE_IN,
	AUTO_PIN_CD,
	AUTO_PIN_AUX,
	AUTO_PIN_LAST
};

enum {
	AUTO_PIN_LINE_OUT,
	AUTO_PIN_SPEAKER_OUT,
	AUTO_PIN_HP_OUT
};

#define AUTO_CFG_MAX_OUTS	HDA_MAX_OUTS
#define AUTO_CFG_MAX_INS	8

struct auto_pin_cfg_item {
	hda_nid_t pin;
	int type;
};

struct auto_pin_cfg;
const char *hda_get_autocfg_input_label(struct hda_codec *codec,
					const struct auto_pin_cfg *cfg,
					int input);
int snd_hda_get_pin_label(struct hda_codec *codec, hda_nid_t nid,
			  const struct auto_pin_cfg *cfg,
			  char *label, int maxlen, int *indexp);

enum {
	INPUT_PIN_ATTR_UNUSED,	/* pin not connected */
	INPUT_PIN_ATTR_INT,	/* internal mic/line-in */
	INPUT_PIN_ATTR_DOCK,	/* docking mic/line-in */
	INPUT_PIN_ATTR_NORMAL,	/* mic/line-in jack */
	INPUT_PIN_ATTR_REAR,	/* mic/line-in jack in rear */
	INPUT_PIN_ATTR_FRONT,	/* mic/line-in jack in front */
	INPUT_PIN_ATTR_LAST = INPUT_PIN_ATTR_FRONT,
};

int snd_hda_get_input_pin_attr(unsigned int def_conf);

struct auto_pin_cfg {
	int line_outs;
	/* sorted in the order of Front/Surr/CLFE/Side */
	hda_nid_t line_out_pins[AUTO_CFG_MAX_OUTS];
	int speaker_outs;
	hda_nid_t speaker_pins[AUTO_CFG_MAX_OUTS];
	int hp_outs;
	int line_out_type;	/* AUTO_PIN_XXX_OUT */
	hda_nid_t hp_pins[AUTO_CFG_MAX_OUTS];
	int num_inputs;
	struct auto_pin_cfg_item inputs[AUTO_CFG_MAX_INS];
	int dig_outs;
	hda_nid_t dig_out_pins[2];
	hda_nid_t dig_in_pin;
	hda_nid_t mono_out_pin;
	int dig_out_type[2]; /* HDA_PCM_TYPE_XXX */
	int dig_in_type; /* HDA_PCM_TYPE_XXX */
};

/* bit-flags for snd_hda_parse_pin_def_config() behavior */
#define HDA_PINCFG_NO_HP_FIXUP	(1 << 0) /* no HP-split */
#define HDA_PINCFG_NO_LO_FIXUP	(1 << 1) /* don't take other outs as LO */

int snd_hda_parse_pin_defcfg(struct hda_codec *codec,
			     struct auto_pin_cfg *cfg,
			     const hda_nid_t *ignore_nids,
			     unsigned int cond_flags);

/* older function */
#define snd_hda_parse_pin_def_config(codec, cfg, ignore) \
	snd_hda_parse_pin_defcfg(codec, cfg, ignore, 0)

#endif /* __SOUND_HDA_AUTO_PARSER_H */
