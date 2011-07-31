/*
 *   Copyright (c) 2007 Daniel Mack
 *   friendly supported by NI.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/usb.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/pcm.h>

#include "device.h"
#include "control.h"

#define CNT_INTVAL 0x10000

static int control_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct snd_usb_audio *chip = snd_kcontrol_chip(kcontrol);
	struct snd_usb_caiaqdev *dev = caiaqdev(chip->card);
	int pos = kcontrol->private_value;
	int is_intval = pos & CNT_INTVAL;
	int maxval = 63;

	uinfo->count = 1;
	pos &= ~CNT_INTVAL;

	switch (dev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO8DJ):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO4DJ):
		if (pos == 0) {
			/* current input mode of A8DJ and A4DJ */
			uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 2;
			return 0;
		}
		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLX1):
		maxval = 127;
		break;
	}

	if (is_intval) {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = maxval;
	} else {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = 1;
	}

	return 0;
}

static int control_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_usb_audio *chip = snd_kcontrol_chip(kcontrol);
	struct snd_usb_caiaqdev *dev = caiaqdev(chip->card);
	int pos = kcontrol->private_value;

	if (pos & CNT_INTVAL)
		ucontrol->value.integer.value[0]
			= dev->control_state[pos & ~CNT_INTVAL];
	else
		ucontrol->value.integer.value[0]
			= !!(dev->control_state[pos / 8] & (1 << pos % 8));

	return 0;
}

static int control_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_usb_audio *chip = snd_kcontrol_chip(kcontrol);
	struct snd_usb_caiaqdev *dev = caiaqdev(chip->card);
	int pos = kcontrol->private_value;
	unsigned char cmd = EP1_CMD_WRITE_IO;

	if (dev->chip.usb_id ==
		USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLX1))
		cmd = EP1_CMD_DIMM_LEDS;

	if (pos & CNT_INTVAL) {
		dev->control_state[pos & ~CNT_INTVAL]
			= ucontrol->value.integer.value[0];
		snd_usb_caiaq_send_command(dev, cmd,
				dev->control_state, sizeof(dev->control_state));
	} else {
		if (ucontrol->value.integer.value[0])
			dev->control_state[pos / 8] |= 1 << (pos % 8);
		else
			dev->control_state[pos / 8] &= ~(1 << (pos % 8));

		snd_usb_caiaq_send_command(dev, cmd,
				dev->control_state, sizeof(dev->control_state));
	}

	return 1;
}

static struct snd_kcontrol_new kcontrol_template __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_HWDEP,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.index = 0,
	.info = control_info,
	.get  = control_get,
	.put  = control_put,
	/* name and private_value filled later */
};

struct caiaq_controller {
	char *name;
	int index;
};

static struct caiaq_controller ak1_controller[] = {
	{ "LED left", 	2 },
	{ "LED middle", 1 },
	{ "LED right", 	0 },
	{ "LED ring", 	3 }
};

static struct caiaq_controller rk2_controller[] = {
	{ "LED 1",		5  },
	{ "LED 2",		4  },
	{ "LED 3",		3  },
	{ "LED 4",		2  },
	{ "LED 5",		1  },
	{ "LED 6",		0  },
	{ "LED pedal",		6  },
	{ "LED 7seg_1b",	8  },
	{ "LED 7seg_1c",	9  },
	{ "LED 7seg_2a",	10 },
	{ "LED 7seg_2b",	11 },
	{ "LED 7seg_2c",	12 },
	{ "LED 7seg_2d",	13 },
	{ "LED 7seg_2e",	14 },
	{ "LED 7seg_2f",	15 },
	{ "LED 7seg_2g",	16 },
	{ "LED 7seg_3a",	17 },
	{ "LED 7seg_3b",	18 },
	{ "LED 7seg_3c",	19 },
	{ "LED 7seg_3d",	20 },
	{ "LED 7seg_3e",	21 },
	{ "LED 7seg_3f",	22 },
	{ "LED 7seg_3g",	23 }
};

static struct caiaq_controller rk3_controller[] = {
	{ "LED 7seg_1a",        0 + 0 },
	{ "LED 7seg_1b",        0 + 1 },
	{ "LED 7seg_1c",        0 + 2 },
	{ "LED 7seg_1d",        0 + 3 },
	{ "LED 7seg_1e",        0 + 4 },
	{ "LED 7seg_1f",        0 + 5 },
	{ "LED 7seg_1g",        0 + 6 },
	{ "LED 7seg_1p",        0 + 7 },

	{ "LED 7seg_2a",        8 + 0 },
	{ "LED 7seg_2b",        8 + 1 },
	{ "LED 7seg_2c",        8 + 2 },
	{ "LED 7seg_2d",        8 + 3 },
	{ "LED 7seg_2e",        8 + 4 },
	{ "LED 7seg_2f",        8 + 5 },
	{ "LED 7seg_2g",        8 + 6 },
	{ "LED 7seg_2p",        8 + 7 },

	{ "LED 7seg_3a",        16 + 0 },
	{ "LED 7seg_3b",        16 + 1 },
	{ "LED 7seg_3c",        16 + 2 },
	{ "LED 7seg_3d",        16 + 3 },
	{ "LED 7seg_3e",        16 + 4 },
	{ "LED 7seg_3f",        16 + 5 },
	{ "LED 7seg_3g",        16 + 6 },
	{ "LED 7seg_3p",        16 + 7 },

	{ "LED 7seg_4a",        24 + 0 },
	{ "LED 7seg_4b",        24 + 1 },
	{ "LED 7seg_4c",        24 + 2 },
	{ "LED 7seg_4d",        24 + 3 },
	{ "LED 7seg_4e",        24 + 4 },
	{ "LED 7seg_4f",        24 + 5 },
	{ "LED 7seg_4g",        24 + 6 },
	{ "LED 7seg_4p",        24 + 7 },

	{ "LED 1",		32 + 0 },
	{ "LED 2",		32 + 1 },
	{ "LED 3",		32 + 2 },
	{ "LED 4",		32 + 3 },
	{ "LED 5",		32 + 4 },
	{ "LED 6",		32 + 5 },
	{ "LED 7",		32 + 6 },
	{ "LED 8",		32 + 7 },
	{ "LED pedal",		32 + 8 }
};

static struct caiaq_controller kore_controller[] = {
	{ "LED F1",		8   | CNT_INTVAL },
	{ "LED F2",		12  | CNT_INTVAL },
	{ "LED F3",		0   | CNT_INTVAL },
	{ "LED F4",		4   | CNT_INTVAL },
	{ "LED F5",		11  | CNT_INTVAL },
	{ "LED F6",		15  | CNT_INTVAL },
	{ "LED F7",		3   | CNT_INTVAL },
	{ "LED F8",		7   | CNT_INTVAL },
	{ "LED touch1",	     	10  | CNT_INTVAL },
	{ "LED touch2",	     	14  | CNT_INTVAL },
	{ "LED touch3",	     	2   | CNT_INTVAL },
	{ "LED touch4",	     	6   | CNT_INTVAL },
	{ "LED touch5",	     	9   | CNT_INTVAL },
	{ "LED touch6",	     	13  | CNT_INTVAL },
	{ "LED touch7",	     	1   | CNT_INTVAL },
	{ "LED touch8",	     	5   | CNT_INTVAL },
	{ "LED left",	       	18  | CNT_INTVAL },
	{ "LED right",	     	22  | CNT_INTVAL },
	{ "LED up",		16  | CNT_INTVAL },
	{ "LED down",	       	20  | CNT_INTVAL },
	{ "LED stop",	       	23  | CNT_INTVAL },
	{ "LED play",	       	21  | CNT_INTVAL },
	{ "LED record",	     	19  | CNT_INTVAL },
	{ "LED listen",		17  | CNT_INTVAL },
	{ "LED lcd",		30  | CNT_INTVAL },
	{ "LED menu",		28  | CNT_INTVAL },
	{ "LED sound",	 	31  | CNT_INTVAL },
	{ "LED esc",		29  | CNT_INTVAL },
	{ "LED view",		27  | CNT_INTVAL },
	{ "LED enter",		24  | CNT_INTVAL },
	{ "LED control",	26  | CNT_INTVAL }
};

static struct caiaq_controller a8dj_controller[] = {
	{ "Current input mode",			0 | CNT_INTVAL 	},
	{ "GND lift for TC Vinyl mode", 	24 + 0 		},
	{ "GND lift for TC CD/Line mode", 	24 + 1 		},
	{ "GND lift for phono mode", 		24 + 2 		},
	{ "Software lock", 			40 		}
};

static struct caiaq_controller a4dj_controller[] = {
	{ "Current input mode",	0 | CNT_INTVAL 	}
};

static struct caiaq_controller kontrolx1_controller[] = {
	{ "LED FX A: ON",		7 | CNT_INTVAL	},
	{ "LED FX A: 1",		6 | CNT_INTVAL	},
	{ "LED FX A: 2",		5 | CNT_INTVAL	},
	{ "LED FX A: 3",		4 | CNT_INTVAL	},
	{ "LED FX B: ON",		3 | CNT_INTVAL	},
	{ "LED FX B: 1",		2 | CNT_INTVAL	},
	{ "LED FX B: 2",		1 | CNT_INTVAL	},
	{ "LED FX B: 3",		0 | CNT_INTVAL	},

	{ "LED Hotcue",			28 | CNT_INTVAL	},
	{ "LED Shift (white)",		29 | CNT_INTVAL	},
	{ "LED Shift (green)",		30 | CNT_INTVAL	},

	{ "LED Deck A: FX1",		24 | CNT_INTVAL	},
	{ "LED Deck A: FX2",		25 | CNT_INTVAL	},
	{ "LED Deck A: IN",		17 | CNT_INTVAL	},
	{ "LED Deck A: OUT",		16 | CNT_INTVAL	},
	{ "LED Deck A: < BEAT",		19 | CNT_INTVAL	},
	{ "LED Deck A: BEAT >",		18 | CNT_INTVAL	},
	{ "LED Deck A: CUE/ABS",	21 | CNT_INTVAL	},
	{ "LED Deck A: CUP/REL",	20 | CNT_INTVAL	},
	{ "LED Deck A: PLAY",		23 | CNT_INTVAL	},
	{ "LED Deck A: SYNC",		22 | CNT_INTVAL	},

	{ "LED Deck B: FX1",		26 | CNT_INTVAL	},
	{ "LED Deck B: FX2",		27 | CNT_INTVAL	},
	{ "LED Deck B: IN",		15 | CNT_INTVAL	},
	{ "LED Deck B: OUT",		14 | CNT_INTVAL	},
	{ "LED Deck B: < BEAT",		13 | CNT_INTVAL	},
	{ "LED Deck B: BEAT >",		12 | CNT_INTVAL	},
	{ "LED Deck B: CUE/ABS",	11 | CNT_INTVAL	},
	{ "LED Deck B: CUP/REL",	10 | CNT_INTVAL	},
	{ "LED Deck B: PLAY",		9  | CNT_INTVAL	},
	{ "LED Deck B: SYNC",		8  | CNT_INTVAL	},
};

static int __devinit add_controls(struct caiaq_controller *c, int num,
				  struct snd_usb_caiaqdev *dev)
{
	int i, ret;
	struct snd_kcontrol *kc;

	for (i = 0; i < num; i++, c++) {
		kcontrol_template.name = c->name;
		kcontrol_template.private_value = c->index;
		kc = snd_ctl_new1(&kcontrol_template, dev);
		ret = snd_ctl_add(dev->chip.card, kc);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int __devinit snd_usb_caiaq_control_init(struct snd_usb_caiaqdev *dev)
{
	int ret = 0;

	switch (dev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AK1):
		ret = add_controls(ak1_controller,
			ARRAY_SIZE(ak1_controller), dev);
		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL2):
		ret = add_controls(rk2_controller,
			ARRAY_SIZE(rk2_controller), dev);
		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL3):
		ret = add_controls(rk3_controller,
			ARRAY_SIZE(rk3_controller), dev);
		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER2):
		ret = add_controls(kore_controller,
			ARRAY_SIZE(kore_controller), dev);
		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO8DJ):
		ret = add_controls(a8dj_controller,
			ARRAY_SIZE(a8dj_controller), dev);
		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO4DJ):
		ret = add_controls(a4dj_controller,
			ARRAY_SIZE(a4dj_controller), dev);
		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLX1):
		ret = add_controls(kontrolx1_controller,
			ARRAY_SIZE(kontrolx1_controller), dev);
		break;
	}

	return ret;
}

