/*
 *   Copyright (c) 2006,2007 Daniel Mack, Tim Ruetz
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

#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <sound/core.h>
#include <sound/pcm.h>

#include "device.h"
#include "input.h"

static unsigned short keycode_ak1[] =  { KEY_C, KEY_B, KEY_A };
static unsigned short keycode_rk2[] =  { KEY_1, KEY_2, KEY_3, KEY_4,
					 KEY_5, KEY_6, KEY_7 };
static unsigned short keycode_rk3[] =  { KEY_1, KEY_2, KEY_3, KEY_4,
					 KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };

static unsigned short keycode_kore[] = {
	KEY_FN_F1,      /* "menu"               */
	KEY_FN_F7,      /* "lcd backlight       */
	KEY_FN_F2,      /* "control"            */
	KEY_FN_F3,      /* "enter"              */
	KEY_FN_F4,      /* "view"               */
	KEY_FN_F5,      /* "esc"                */
	KEY_FN_F6,      /* "sound"              */
	KEY_FN_F8,      /* array spacer, never triggered. */
	KEY_RIGHT,
	KEY_DOWN,
	KEY_UP,
	KEY_LEFT,
	KEY_SOUND,      /* "listen"             */
	KEY_RECORD,
	KEY_PLAYPAUSE,
	KEY_STOP,
	BTN_4,          /* 8 softkeys */
	BTN_3,
	BTN_2,
	BTN_1,
	BTN_8,
	BTN_7,
	BTN_6,
	BTN_5,
	KEY_BRL_DOT4,   /* touch sensitive knobs */
	KEY_BRL_DOT3,
	KEY_BRL_DOT2,
	KEY_BRL_DOT1,
	KEY_BRL_DOT8,
	KEY_BRL_DOT7,
	KEY_BRL_DOT6,
	KEY_BRL_DOT5
};

#define MASCHINE_BUTTONS   (42)
#define MASCHINE_BUTTON(X) ((X) + BTN_MISC)
#define MASCHINE_PADS      (16)
#define MASCHINE_PAD(X)    ((X) + ABS_PRESSURE)

static unsigned short keycode_maschine[] = {
	MASCHINE_BUTTON(40), /* mute       */
	MASCHINE_BUTTON(39), /* solo       */
	MASCHINE_BUTTON(38), /* select     */
	MASCHINE_BUTTON(37), /* duplicate  */
	MASCHINE_BUTTON(36), /* navigate   */
	MASCHINE_BUTTON(35), /* pad mode   */
	MASCHINE_BUTTON(34), /* pattern    */
	MASCHINE_BUTTON(33), /* scene      */
	KEY_RESERVED, /* spacer */

	MASCHINE_BUTTON(30), /* rec        */
	MASCHINE_BUTTON(31), /* erase      */
	MASCHINE_BUTTON(32), /* shift      */
	MASCHINE_BUTTON(28), /* grid       */
	MASCHINE_BUTTON(27), /* >          */
	MASCHINE_BUTTON(26), /* <          */
	MASCHINE_BUTTON(25), /* restart    */

	MASCHINE_BUTTON(21), /* E          */
	MASCHINE_BUTTON(22), /* F          */
	MASCHINE_BUTTON(23), /* G          */
	MASCHINE_BUTTON(24), /* H          */
	MASCHINE_BUTTON(20), /* D          */
	MASCHINE_BUTTON(19), /* C          */
	MASCHINE_BUTTON(18), /* B          */
	MASCHINE_BUTTON(17), /* A          */

	MASCHINE_BUTTON(0),  /* control    */
	MASCHINE_BUTTON(2),  /* browse     */
	MASCHINE_BUTTON(4),  /* <          */
	MASCHINE_BUTTON(6),  /* snap       */
	MASCHINE_BUTTON(7),  /* autowrite  */
	MASCHINE_BUTTON(5),  /* >          */
	MASCHINE_BUTTON(3),  /* sampling   */
	MASCHINE_BUTTON(1),  /* step       */

	MASCHINE_BUTTON(15), /* 8 softkeys */
	MASCHINE_BUTTON(14),
	MASCHINE_BUTTON(13),
	MASCHINE_BUTTON(12),
	MASCHINE_BUTTON(11),
	MASCHINE_BUTTON(10),
	MASCHINE_BUTTON(9),
	MASCHINE_BUTTON(8),

	MASCHINE_BUTTON(16), /* note repeat */
	MASCHINE_BUTTON(29)  /* play        */
};

#define KONTROLX1_INPUTS	(40)
#define KONTROLS4_BUTTONS	(12 * 8)
#define KONTROLS4_AXIS		(46)

#define KONTROLS4_BUTTON(X)	((X) + BTN_MISC)
#define KONTROLS4_ABS(X)	((X) + ABS_HAT0X)

#define DEG90		(range / 2)
#define DEG180		(range)
#define DEG270		(DEG90 + DEG180)
#define DEG360		(DEG180 * 2)
#define HIGH_PEAK	(268)
#define LOW_PEAK	(-7)

/* some of these devices have endless rotation potentiometers
 * built in which use two tapers, 90 degrees phase shifted.
 * this algorithm decodes them to one single value, ranging
 * from 0 to 999 */
static unsigned int decode_erp(unsigned char a, unsigned char b)
{
	int weight_a, weight_b;
	int pos_a, pos_b;
	int ret;
	int range = HIGH_PEAK - LOW_PEAK;
	int mid_value = (HIGH_PEAK + LOW_PEAK) / 2;

	weight_b = abs(mid_value - a) - (range / 2 - 100) / 2;

	if (weight_b < 0)
		weight_b = 0;

	if (weight_b > 100)
		weight_b = 100;

	weight_a = 100 - weight_b;

	if (a < mid_value) {
		/* 0..90 and 270..360 degrees */
		pos_b = b - LOW_PEAK + DEG270;
		if (pos_b >= DEG360)
			pos_b -= DEG360;
	} else
		/* 90..270 degrees */
		pos_b = HIGH_PEAK - b + DEG90;


	if (b > mid_value)
		/* 0..180 degrees */
		pos_a = a - LOW_PEAK;
	else
		/* 180..360 degrees */
		pos_a = HIGH_PEAK - a + DEG180;

	/* interpolate both slider values, depending on weight factors */
	/* 0..99 x DEG360 */
	ret = pos_a * weight_a + pos_b * weight_b;

	/* normalize to 0..999 */
	ret *= 10;
	ret /= DEG360;

	if (ret < 0)
		ret += 1000;

	if (ret >= 1000)
		ret -= 1000;

	return ret;
}

#undef DEG90
#undef DEG180
#undef DEG270
#undef DEG360
#undef HIGH_PEAK
#undef LOW_PEAK

static inline void snd_caiaq_input_report_abs(struct snd_usb_caiaqdev *cdev,
					      int axis, const unsigned char *buf,
					      int offset)
{
	input_report_abs(cdev->input_dev, axis,
			 (buf[offset * 2] << 8) | buf[offset * 2 + 1]);
}

static void snd_caiaq_input_read_analog(struct snd_usb_caiaqdev *cdev,
					const unsigned char *buf,
					unsigned int len)
{
	struct input_dev *input_dev = cdev->input_dev;

	switch (cdev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL2):
		snd_caiaq_input_report_abs(cdev, ABS_X, buf, 2);
		snd_caiaq_input_report_abs(cdev, ABS_Y, buf, 0);
		snd_caiaq_input_report_abs(cdev, ABS_Z, buf, 1);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL3):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER2):
		snd_caiaq_input_report_abs(cdev, ABS_X, buf, 0);
		snd_caiaq_input_report_abs(cdev, ABS_Y, buf, 1);
		snd_caiaq_input_report_abs(cdev, ABS_Z, buf, 2);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLX1):
		snd_caiaq_input_report_abs(cdev, ABS_HAT0X, buf, 4);
		snd_caiaq_input_report_abs(cdev, ABS_HAT0Y, buf, 2);
		snd_caiaq_input_report_abs(cdev, ABS_HAT1X, buf, 6);
		snd_caiaq_input_report_abs(cdev, ABS_HAT1Y, buf, 1);
		snd_caiaq_input_report_abs(cdev, ABS_HAT2X, buf, 7);
		snd_caiaq_input_report_abs(cdev, ABS_HAT2Y, buf, 0);
		snd_caiaq_input_report_abs(cdev, ABS_HAT3X, buf, 5);
		snd_caiaq_input_report_abs(cdev, ABS_HAT3Y, buf, 3);
		break;
	}

	input_sync(input_dev);
}

static void snd_caiaq_input_read_erp(struct snd_usb_caiaqdev *cdev,
				     const char *buf, unsigned int len)
{
	struct input_dev *input_dev = cdev->input_dev;
	int i;

	switch (cdev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AK1):
		i = decode_erp(buf[0], buf[1]);
		input_report_abs(input_dev, ABS_X, i);
		input_sync(input_dev);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER2):
		i = decode_erp(buf[7], buf[5]);
		input_report_abs(input_dev, ABS_HAT0X, i);
		i = decode_erp(buf[12], buf[14]);
		input_report_abs(input_dev, ABS_HAT0Y, i);
		i = decode_erp(buf[15], buf[13]);
		input_report_abs(input_dev, ABS_HAT1X, i);
		i = decode_erp(buf[0], buf[2]);
		input_report_abs(input_dev, ABS_HAT1Y, i);
		i = decode_erp(buf[3], buf[1]);
		input_report_abs(input_dev, ABS_HAT2X, i);
		i = decode_erp(buf[8], buf[10]);
		input_report_abs(input_dev, ABS_HAT2Y, i);
		i = decode_erp(buf[11], buf[9]);
		input_report_abs(input_dev, ABS_HAT3X, i);
		i = decode_erp(buf[4], buf[6]);
		input_report_abs(input_dev, ABS_HAT3Y, i);
		input_sync(input_dev);
		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_MASCHINECONTROLLER):
		/* 4 under the left screen */
		input_report_abs(input_dev, ABS_HAT0X, decode_erp(buf[21], buf[20]));
		input_report_abs(input_dev, ABS_HAT0Y, decode_erp(buf[15], buf[14]));
		input_report_abs(input_dev, ABS_HAT1X, decode_erp(buf[9],  buf[8]));
		input_report_abs(input_dev, ABS_HAT1Y, decode_erp(buf[3],  buf[2]));

		/* 4 under the right screen */
		input_report_abs(input_dev, ABS_HAT2X, decode_erp(buf[19], buf[18]));
		input_report_abs(input_dev, ABS_HAT2Y, decode_erp(buf[13], buf[12]));
		input_report_abs(input_dev, ABS_HAT3X, decode_erp(buf[7],  buf[6]));
		input_report_abs(input_dev, ABS_HAT3Y, decode_erp(buf[1],  buf[0]));

		/* volume */
		input_report_abs(input_dev, ABS_RX, decode_erp(buf[17], buf[16]));
		/* tempo */
		input_report_abs(input_dev, ABS_RY, decode_erp(buf[11], buf[10]));
		/* swing */
		input_report_abs(input_dev, ABS_RZ, decode_erp(buf[5],  buf[4]));

		input_sync(input_dev);
		break;
	}
}

static void snd_caiaq_input_read_io(struct snd_usb_caiaqdev *cdev,
				    unsigned char *buf, unsigned int len)
{
	struct input_dev *input_dev = cdev->input_dev;
	unsigned short *keycode = input_dev->keycode;
	int i;

	if (!keycode)
		return;

	if (input_dev->id.product == USB_PID_RIGKONTROL2)
		for (i = 0; i < len; i++)
			buf[i] = ~buf[i];

	for (i = 0; i < input_dev->keycodemax && i < len * 8; i++)
		input_report_key(input_dev, keycode[i],
				 buf[i / 8] & (1 << (i % 8)));

	switch (cdev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER2):
		input_report_abs(cdev->input_dev, ABS_MISC, 255 - buf[4]);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLX1):
		/* rotary encoders */
		input_report_abs(cdev->input_dev, ABS_X, buf[5] & 0xf);
		input_report_abs(cdev->input_dev, ABS_Y, buf[5] >> 4);
		input_report_abs(cdev->input_dev, ABS_Z, buf[6] & 0xf);
		input_report_abs(cdev->input_dev, ABS_MISC, buf[6] >> 4);
		break;
	}

	input_sync(input_dev);
}

#define TKS4_MSGBLOCK_SIZE	16

static void snd_usb_caiaq_tks4_dispatch(struct snd_usb_caiaqdev *cdev,
					const unsigned char *buf,
					unsigned int len)
{
	struct device *dev = caiaqdev_to_dev(cdev);

	while (len) {
		unsigned int i, block_id = (buf[0] << 8) | buf[1];

		switch (block_id) {
		case 0:
			/* buttons */
			for (i = 0; i < KONTROLS4_BUTTONS; i++)
				input_report_key(cdev->input_dev, KONTROLS4_BUTTON(i),
						 (buf[4 + (i / 8)] >> (i % 8)) & 1);
			break;

		case 1:
			/* left wheel */
			input_report_abs(cdev->input_dev, KONTROLS4_ABS(36), buf[9] | ((buf[8] & 0x3) << 8));
			/* right wheel */
			input_report_abs(cdev->input_dev, KONTROLS4_ABS(37), buf[13] | ((buf[12] & 0x3) << 8));

			/* rotary encoders */
			input_report_abs(cdev->input_dev, KONTROLS4_ABS(38), buf[3] & 0xf);
			input_report_abs(cdev->input_dev, KONTROLS4_ABS(39), buf[4] >> 4);
			input_report_abs(cdev->input_dev, KONTROLS4_ABS(40), buf[4] & 0xf);
			input_report_abs(cdev->input_dev, KONTROLS4_ABS(41), buf[5] >> 4);
			input_report_abs(cdev->input_dev, KONTROLS4_ABS(42), buf[5] & 0xf);
			input_report_abs(cdev->input_dev, KONTROLS4_ABS(43), buf[6] >> 4);
			input_report_abs(cdev->input_dev, KONTROLS4_ABS(44), buf[6] & 0xf);
			input_report_abs(cdev->input_dev, KONTROLS4_ABS(45), buf[7] >> 4);
			input_report_abs(cdev->input_dev, KONTROLS4_ABS(46), buf[7] & 0xf);

			break;
		case 2:
			/* Volume Fader Channel D */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(0), buf, 1);
			/* Volume Fader Channel B */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(1), buf, 2);
			/* Volume Fader Channel A */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(2), buf, 3);
			/* Volume Fader Channel C */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(3), buf, 4);
			/* Loop Volume */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(4), buf, 6);
			/* Crossfader */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(7), buf, 7);

			break;

		case 3:
			/* Tempo Fader R */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(6), buf, 3);
			/* Tempo Fader L */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(5), buf, 4);
			/* Mic Volume */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(8), buf, 6);
			/* Cue Mix */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(9), buf, 7);

			break;

		case 4:
			/* Wheel distance sensor L */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(10), buf, 1);
			/* Wheel distance sensor R */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(11), buf, 2);
			/* Channel D EQ - Filter */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(12), buf, 3);
			/* Channel D EQ - Low */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(13), buf, 4);
			/* Channel D EQ - Mid */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(14), buf, 5);
			/* Channel D EQ - Hi */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(15), buf, 6);
			/* FX2 - dry/wet */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(16), buf, 7);

			break;

		case 5:
			/* FX2 - 1 */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(17), buf, 1);
			/* FX2 - 2 */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(18), buf, 2);
			/* FX2 - 3 */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(19), buf, 3);
			/* Channel B EQ - Filter */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(20), buf, 4);
			/* Channel B EQ - Low */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(21), buf, 5);
			/* Channel B EQ - Mid */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(22), buf, 6);
			/* Channel B EQ - Hi */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(23), buf, 7);

			break;

		case 6:
			/* Channel A EQ - Filter */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(24), buf, 1);
			/* Channel A EQ - Low */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(25), buf, 2);
			/* Channel A EQ - Mid */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(26), buf, 3);
			/* Channel A EQ - Hi */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(27), buf, 4);
			/* Channel C EQ - Filter */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(28), buf, 5);
			/* Channel C EQ - Low */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(29), buf, 6);
			/* Channel C EQ - Mid */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(30), buf, 7);

			break;

		case 7:
			/* Channel C EQ - Hi */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(31), buf, 1);
			/* FX1 - wet/dry */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(32), buf, 2);
			/* FX1 - 1 */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(33), buf, 3);
			/* FX1 - 2 */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(34), buf, 4);
			/* FX1 - 3 */
			snd_caiaq_input_report_abs(cdev, KONTROLS4_ABS(35), buf, 5);

			break;

		default:
			dev_dbg(dev, "%s(): bogus block (id %d)\n",
				__func__, block_id);
			return;
		}

		len -= TKS4_MSGBLOCK_SIZE;
		buf += TKS4_MSGBLOCK_SIZE;
	}

	input_sync(cdev->input_dev);
}

#define MASCHINE_MSGBLOCK_SIZE 2

static void snd_usb_caiaq_maschine_dispatch(struct snd_usb_caiaqdev *cdev,
					const unsigned char *buf,
					unsigned int len)
{
	unsigned int i, pad_id;
	uint16_t pressure;

	for (i = 0; i < MASCHINE_PADS; i++) {
		pressure = be16_to_cpu(buf[i * 2] << 8 | buf[(i * 2) + 1]);
		pad_id = pressure >> 12;

		input_report_abs(cdev->input_dev, MASCHINE_PAD(pad_id), pressure & 0xfff);
	}

	input_sync(cdev->input_dev);
}

static void snd_usb_caiaq_ep4_reply_dispatch(struct urb *urb)
{
	struct snd_usb_caiaqdev *cdev = urb->context;
	unsigned char *buf = urb->transfer_buffer;
	struct device *dev = &urb->dev->dev;
	int ret;

	if (urb->status || !cdev || urb != cdev->ep4_in_urb)
		return;

	switch (cdev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLX1):
		if (urb->actual_length < 24)
			goto requeue;

		if (buf[0] & 0x3)
			snd_caiaq_input_read_io(cdev, buf + 1, 7);

		if (buf[0] & 0x4)
			snd_caiaq_input_read_analog(cdev, buf + 8, 16);

		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLS4):
		snd_usb_caiaq_tks4_dispatch(cdev, buf, urb->actual_length);
		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_MASCHINECONTROLLER):
		if (urb->actual_length < (MASCHINE_PADS * MASCHINE_MSGBLOCK_SIZE))
			goto requeue;

		snd_usb_caiaq_maschine_dispatch(cdev, buf, urb->actual_length);
		break;
	}

requeue:
	cdev->ep4_in_urb->actual_length = 0;
	ret = usb_submit_urb(cdev->ep4_in_urb, GFP_ATOMIC);
	if (ret < 0)
		dev_err(dev, "unable to submit urb. OOM!?\n");
}

static int snd_usb_caiaq_input_open(struct input_dev *idev)
{
	struct snd_usb_caiaqdev *cdev = input_get_drvdata(idev);

	if (!cdev)
		return -EINVAL;

	switch (cdev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLX1):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLS4):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_MASCHINECONTROLLER):
		if (usb_submit_urb(cdev->ep4_in_urb, GFP_KERNEL) != 0)
			return -EIO;
		break;
	}

	return 0;
}

static void snd_usb_caiaq_input_close(struct input_dev *idev)
{
	struct snd_usb_caiaqdev *cdev = input_get_drvdata(idev);

	if (!cdev)
		return;

	switch (cdev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLX1):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLS4):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_MASCHINECONTROLLER):
		usb_kill_urb(cdev->ep4_in_urb);
		break;
	}
}

void snd_usb_caiaq_input_dispatch(struct snd_usb_caiaqdev *cdev,
				  char *buf,
				  unsigned int len)
{
	if (!cdev->input_dev || len < 1)
		return;

	switch (buf[0]) {
	case EP1_CMD_READ_ANALOG:
		snd_caiaq_input_read_analog(cdev, buf + 1, len - 1);
		break;
	case EP1_CMD_READ_ERP:
		snd_caiaq_input_read_erp(cdev, buf + 1, len - 1);
		break;
	case EP1_CMD_READ_IO:
		snd_caiaq_input_read_io(cdev, buf + 1, len - 1);
		break;
	}
}

int snd_usb_caiaq_input_init(struct snd_usb_caiaqdev *cdev)
{
	struct usb_device *usb_dev = cdev->chip.dev;
	struct input_dev *input;
	int i, ret = 0;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	usb_make_path(usb_dev, cdev->phys, sizeof(cdev->phys));
	strlcat(cdev->phys, "/input0", sizeof(cdev->phys));

	input->name = cdev->product_name;
	input->phys = cdev->phys;
	usb_to_input_id(usb_dev, &input->id);
	input->dev.parent = &usb_dev->dev;

	input_set_drvdata(input, cdev);

	switch (cdev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL2):
		input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input->absbit[0] = BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) |
			BIT_MASK(ABS_Z);
		BUILD_BUG_ON(sizeof(cdev->keycode) < sizeof(keycode_rk2));
		memcpy(cdev->keycode, keycode_rk2, sizeof(keycode_rk2));
		input->keycodemax = ARRAY_SIZE(keycode_rk2);
		input_set_abs_params(input, ABS_X, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_Y, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_Z, 0, 4096, 0, 10);
		snd_usb_caiaq_set_auto_msg(cdev, 1, 10, 0);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL3):
		input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input->absbit[0] = BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) |
			BIT_MASK(ABS_Z);
		BUILD_BUG_ON(sizeof(cdev->keycode) < sizeof(keycode_rk3));
		memcpy(cdev->keycode, keycode_rk3, sizeof(keycode_rk3));
		input->keycodemax = ARRAY_SIZE(keycode_rk3);
		input_set_abs_params(input, ABS_X, 0, 1024, 0, 10);
		input_set_abs_params(input, ABS_Y, 0, 1024, 0, 10);
		input_set_abs_params(input, ABS_Z, 0, 1024, 0, 10);
		snd_usb_caiaq_set_auto_msg(cdev, 1, 10, 0);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AK1):
		input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input->absbit[0] = BIT_MASK(ABS_X);
		BUILD_BUG_ON(sizeof(cdev->keycode) < sizeof(keycode_ak1));
		memcpy(cdev->keycode, keycode_ak1, sizeof(keycode_ak1));
		input->keycodemax = ARRAY_SIZE(keycode_ak1);
		input_set_abs_params(input, ABS_X, 0, 999, 0, 10);
		snd_usb_caiaq_set_auto_msg(cdev, 1, 0, 5);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER2):
		input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input->absbit[0] = BIT_MASK(ABS_HAT0X) | BIT_MASK(ABS_HAT0Y) |
				   BIT_MASK(ABS_HAT1X) | BIT_MASK(ABS_HAT1Y) |
				   BIT_MASK(ABS_HAT2X) | BIT_MASK(ABS_HAT2Y) |
				   BIT_MASK(ABS_HAT3X) | BIT_MASK(ABS_HAT3Y) |
				   BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) |
				   BIT_MASK(ABS_Z);
		input->absbit[BIT_WORD(ABS_MISC)] |= BIT_MASK(ABS_MISC);
		BUILD_BUG_ON(sizeof(cdev->keycode) < sizeof(keycode_kore));
		memcpy(cdev->keycode, keycode_kore, sizeof(keycode_kore));
		input->keycodemax = ARRAY_SIZE(keycode_kore);
		input_set_abs_params(input, ABS_HAT0X, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT0Y, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT1X, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT1Y, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT2X, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT2Y, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT3X, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT3Y, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_X, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_Y, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_Z, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_MISC, 0, 255, 0, 1);
		snd_usb_caiaq_set_auto_msg(cdev, 1, 10, 5);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLX1):
		input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input->absbit[0] = BIT_MASK(ABS_HAT0X) | BIT_MASK(ABS_HAT0Y) |
				   BIT_MASK(ABS_HAT1X) | BIT_MASK(ABS_HAT1Y) |
				   BIT_MASK(ABS_HAT2X) | BIT_MASK(ABS_HAT2Y) |
				   BIT_MASK(ABS_HAT3X) | BIT_MASK(ABS_HAT3Y) |
				   BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) |
				   BIT_MASK(ABS_Z);
		input->absbit[BIT_WORD(ABS_MISC)] |= BIT_MASK(ABS_MISC);
		BUILD_BUG_ON(sizeof(cdev->keycode) < KONTROLX1_INPUTS);
		for (i = 0; i < KONTROLX1_INPUTS; i++)
			cdev->keycode[i] = BTN_MISC + i;
		input->keycodemax = KONTROLX1_INPUTS;

		/* analog potentiometers */
		input_set_abs_params(input, ABS_HAT0X, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_HAT0Y, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_HAT1X, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_HAT1Y, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_HAT2X, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_HAT2Y, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_HAT3X, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_HAT3Y, 0, 4096, 0, 10);

		/* rotary encoders */
		input_set_abs_params(input, ABS_X, 0, 0xf, 0, 1);
		input_set_abs_params(input, ABS_Y, 0, 0xf, 0, 1);
		input_set_abs_params(input, ABS_Z, 0, 0xf, 0, 1);
		input_set_abs_params(input, ABS_MISC, 0, 0xf, 0, 1);

		cdev->ep4_in_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!cdev->ep4_in_urb) {
			ret = -ENOMEM;
			goto exit_free_idev;
		}

		usb_fill_bulk_urb(cdev->ep4_in_urb, usb_dev,
				  usb_rcvbulkpipe(usb_dev, 0x4),
				  cdev->ep4_in_buf, EP4_BUFSIZE,
				  snd_usb_caiaq_ep4_reply_dispatch, cdev);

		snd_usb_caiaq_set_auto_msg(cdev, 1, 10, 5);

		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORKONTROLS4):
		input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		BUILD_BUG_ON(sizeof(cdev->keycode) < KONTROLS4_BUTTONS);
		for (i = 0; i < KONTROLS4_BUTTONS; i++)
			cdev->keycode[i] = KONTROLS4_BUTTON(i);
		input->keycodemax = KONTROLS4_BUTTONS;

		for (i = 0; i < KONTROLS4_AXIS; i++) {
			int axis = KONTROLS4_ABS(i);
			input->absbit[BIT_WORD(axis)] |= BIT_MASK(axis);
		}

		/* 36 analog potentiometers and faders */
		for (i = 0; i < 36; i++)
			input_set_abs_params(input, KONTROLS4_ABS(i), 0, 0xfff, 0, 10);

		/* 2 encoder wheels */
		input_set_abs_params(input, KONTROLS4_ABS(36), 0, 0x3ff, 0, 1);
		input_set_abs_params(input, KONTROLS4_ABS(37), 0, 0x3ff, 0, 1);

		/* 9 rotary encoders */
		for (i = 0; i < 9; i++)
			input_set_abs_params(input, KONTROLS4_ABS(38+i), 0, 0xf, 0, 1);

		cdev->ep4_in_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!cdev->ep4_in_urb) {
			ret = -ENOMEM;
			goto exit_free_idev;
		}

		usb_fill_bulk_urb(cdev->ep4_in_urb, usb_dev,
				  usb_rcvbulkpipe(usb_dev, 0x4),
				  cdev->ep4_in_buf, EP4_BUFSIZE,
				  snd_usb_caiaq_ep4_reply_dispatch, cdev);

		snd_usb_caiaq_set_auto_msg(cdev, 1, 10, 5);

		break;

	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_MASCHINECONTROLLER):
		input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input->absbit[0] = BIT_MASK(ABS_HAT0X) | BIT_MASK(ABS_HAT0Y) |
			BIT_MASK(ABS_HAT1X) | BIT_MASK(ABS_HAT1Y) |
			BIT_MASK(ABS_HAT2X) | BIT_MASK(ABS_HAT2Y) |
			BIT_MASK(ABS_HAT3X) | BIT_MASK(ABS_HAT3Y) |
			BIT_MASK(ABS_RX) | BIT_MASK(ABS_RY) |
			BIT_MASK(ABS_RZ);

		BUILD_BUG_ON(sizeof(cdev->keycode) < sizeof(keycode_maschine));
		memcpy(cdev->keycode, keycode_maschine, sizeof(keycode_maschine));
		input->keycodemax = ARRAY_SIZE(keycode_maschine);

		for (i = 0; i < MASCHINE_PADS; i++) {
			input->absbit[0] |= MASCHINE_PAD(i);
			input_set_abs_params(input, MASCHINE_PAD(i), 0, 0xfff, 5, 10);
		}

		input_set_abs_params(input, ABS_HAT0X, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT0Y, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT1X, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT1Y, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT2X, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT2Y, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT3X, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_HAT3Y, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_RX, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_RY, 0, 999, 0, 10);
		input_set_abs_params(input, ABS_RZ, 0, 999, 0, 10);

		cdev->ep4_in_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!cdev->ep4_in_urb) {
			ret = -ENOMEM;
			goto exit_free_idev;
		}

		usb_fill_bulk_urb(cdev->ep4_in_urb, usb_dev,
				  usb_rcvbulkpipe(usb_dev, 0x4),
				  cdev->ep4_in_buf, EP4_BUFSIZE,
				  snd_usb_caiaq_ep4_reply_dispatch, cdev);

		snd_usb_caiaq_set_auto_msg(cdev, 1, 10, 5);
		break;

	default:
		/* no input methods supported on this device */
		goto exit_free_idev;
	}

	input->open = snd_usb_caiaq_input_open;
	input->close = snd_usb_caiaq_input_close;
	input->keycode = cdev->keycode;
	input->keycodesize = sizeof(unsigned short);
	for (i = 0; i < input->keycodemax; i++)
		__set_bit(cdev->keycode[i], input->keybit);

	cdev->input_dev = input;

	ret = input_register_device(input);
	if (ret < 0)
		goto exit_free_idev;

	return 0;

exit_free_idev:
	input_free_device(input);
	cdev->input_dev = NULL;
	return ret;
}

void snd_usb_caiaq_input_free(struct snd_usb_caiaqdev *cdev)
{
	if (!cdev || !cdev->input_dev)
		return;

	usb_kill_urb(cdev->ep4_in_urb);
	usb_free_urb(cdev->ep4_in_urb);
	cdev->ep4_in_urb = NULL;

	input_unregister_device(cdev->input_dev);
	cdev->input_dev = NULL;
}
