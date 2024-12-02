/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Author:	Torsten Schenk <torsten.schenk@zoho.com>
 * Created:	Jan 01, 2011
 * Copyright:	(C) Torsten Schenk
 */

#ifndef USB6FIRE_CONTROL_H
#define USB6FIRE_CONTROL_H

#include "common.h"

enum {
	CONTROL_MAX_ELEMENTS = 32
};

enum {
	CONTROL_RATE_44KHZ,
	CONTROL_RATE_48KHZ,
	CONTROL_RATE_88KHZ,
	CONTROL_RATE_96KHZ,
	CONTROL_RATE_176KHZ,
	CONTROL_RATE_192KHZ,
	CONTROL_N_RATES
};

struct control_runtime {
	int (*update_streaming)(struct control_runtime *rt);
	int (*set_rate)(struct control_runtime *rt, int rate);
	int (*set_channels)(struct control_runtime *rt, int n_analog_out,
		int n_analog_in, bool spdif_out, bool spdif_in);

	struct sfire_chip *chip;

	struct snd_kcontrol *element[CONTROL_MAX_ELEMENTS];
	bool opt_coax_switch;
	bool line_phono_switch;
	bool digital_thru_switch;
	bool usb_streaming;
	u8 output_vol[6];
	u8 ovol_updated;
	u8 output_mute;
	s8 input_vol[2];
	u8 ivol_updated;
};

int usb6fire_control_init(struct sfire_chip *chip);
void usb6fire_control_abort(struct sfire_chip *chip);
void usb6fire_control_destroy(struct sfire_chip *chip);
#endif /* USB6FIRE_CONTROL_H */

