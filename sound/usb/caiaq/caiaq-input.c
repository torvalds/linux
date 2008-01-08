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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/spinlock.h>
#include <sound/core.h>
#include <sound/rawmidi.h>
#include <sound/pcm.h>
#include "caiaq-device.h"
#include "caiaq-input.h"

static unsigned short keycode_ak1[] =  { KEY_C, KEY_B, KEY_A };
static unsigned short keycode_rk2[] =  { KEY_1, KEY_2, KEY_3, KEY_4,
					 KEY_5, KEY_6, KEY_7 };
static unsigned short keycode_rk3[] =  { KEY_1, KEY_2, KEY_3, KEY_4,
					 KEY_5, KEY_6, KEY_7, KEY_5, KEY_6 };

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


static void snd_caiaq_input_read_analog(struct snd_usb_caiaqdev *dev,
					const unsigned char *buf,
					unsigned int len)
{
	struct input_dev *input_dev = dev->input_dev;

	switch (dev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL2):
		input_report_abs(input_dev, ABS_X, (buf[4] << 8) | buf[5]);
		input_report_abs(input_dev, ABS_Y, (buf[0] << 8) | buf[1]);
		input_report_abs(input_dev, ABS_Z, (buf[2] << 8) | buf[3]);
		input_sync(input_dev);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL3):
		input_report_abs(input_dev, ABS_X, (buf[0] << 8) | buf[1]);
		input_report_abs(input_dev, ABS_Y, (buf[2] << 8) | buf[3]);
		input_report_abs(input_dev, ABS_Z, (buf[4] << 8) | buf[5]);
		input_sync(input_dev);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER2):
		input_report_abs(input_dev, ABS_X, (buf[0] << 8) | buf[1]);
		input_report_abs(input_dev, ABS_Y, (buf[2] << 8) | buf[3]);
		input_report_abs(input_dev, ABS_Z, (buf[4] << 8) | buf[5]);
		input_sync(input_dev);
		break;
	}
}

static void snd_caiaq_input_read_erp(struct snd_usb_caiaqdev *dev,
				     const char *buf, unsigned int len)
{
	struct input_dev *input_dev = dev->input_dev;
	int i;

	switch (dev->chip.usb_id) {
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
	}
}

static void snd_caiaq_input_read_io(struct snd_usb_caiaqdev *dev,
				    char *buf, unsigned int len)
{
	struct input_dev *input_dev = dev->input_dev;
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

	if (dev->chip.usb_id ==
		USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER) ||
	    dev->chip.usb_id ==
		USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER2))
		input_report_abs(dev->input_dev, ABS_MISC, 255 - buf[4]);

	input_sync(input_dev);
}

void snd_usb_caiaq_input_dispatch(struct snd_usb_caiaqdev *dev,
				  char *buf,
				  unsigned int len)
{
	if (!dev->input_dev || len < 1)
		return;

	switch (buf[0]) {
	case EP1_CMD_READ_ANALOG:
		snd_caiaq_input_read_analog(dev, buf + 1, len - 1);
		break;
	case EP1_CMD_READ_ERP:
		snd_caiaq_input_read_erp(dev, buf + 1, len - 1);
		break;
	case EP1_CMD_READ_IO:
		snd_caiaq_input_read_io(dev, buf + 1, len - 1);
		break;
	}
}

int snd_usb_caiaq_input_init(struct snd_usb_caiaqdev *dev)
{
	struct usb_device *usb_dev = dev->chip.dev;
	struct input_dev *input;
	int i, ret;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	usb_make_path(usb_dev, dev->phys, sizeof(dev->phys));
	strlcat(dev->phys, "/input0", sizeof(dev->phys));

	input->name = dev->product_name;
	input->phys = dev->phys;
	usb_to_input_id(usb_dev, &input->id);
	input->dev.parent = &usb_dev->dev;

        switch (dev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL2):
		input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input->absbit[0] = BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) |
			BIT_MASK(ABS_Z);
		BUILD_BUG_ON(sizeof(dev->keycode) < sizeof(keycode_rk2));
		memcpy(dev->keycode, keycode_rk2, sizeof(keycode_rk2));
		input->keycodemax = ARRAY_SIZE(keycode_rk2);
		input_set_abs_params(input, ABS_X, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_Y, 0, 4096, 0, 10);
		input_set_abs_params(input, ABS_Z, 0, 4096, 0, 10);
		snd_usb_caiaq_set_auto_msg(dev, 1, 10, 0);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL3):
		input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input->absbit[0] = BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) |
			BIT_MASK(ABS_Z);
		BUILD_BUG_ON(sizeof(dev->keycode) < sizeof(keycode_rk3));
		memcpy(dev->keycode, keycode_rk3, sizeof(keycode_rk3));
		input->keycodemax = ARRAY_SIZE(keycode_rk3);
		input_set_abs_params(input, ABS_X, 0, 1024, 0, 10);
		input_set_abs_params(input, ABS_Y, 0, 1024, 0, 10);
		input_set_abs_params(input, ABS_Z, 0, 1024, 0, 10);
		snd_usb_caiaq_set_auto_msg(dev, 1, 10, 0);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AK1):
		input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input->absbit[0] = BIT_MASK(ABS_X);
		BUILD_BUG_ON(sizeof(dev->keycode) < sizeof(keycode_ak1));
		memcpy(dev->keycode, keycode_ak1, sizeof(keycode_ak1));
		input->keycodemax = ARRAY_SIZE(keycode_ak1);
		input_set_abs_params(input, ABS_X, 0, 999, 0, 10);
		snd_usb_caiaq_set_auto_msg(dev, 1, 0, 5);
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
		BUILD_BUG_ON(sizeof(dev->keycode) < sizeof(keycode_kore));
		memcpy(dev->keycode, keycode_kore, sizeof(keycode_kore));
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
		snd_usb_caiaq_set_auto_msg(dev, 1, 10, 5);
		break;
	default:
		/* no input methods supported on this device */
		input_free_device(input);
		return 0;
	}

	input->keycode = dev->keycode;
	input->keycodesize = sizeof(unsigned short);
	for (i = 0; i < input->keycodemax; i++)
		__set_bit(dev->keycode[i], input->keybit);

	ret = input_register_device(input);
	if (ret < 0) {
		input_free_device(input);
		return ret;
	}

	dev->input_dev = input;
	return 0;
}

void snd_usb_caiaq_input_free(struct snd_usb_caiaqdev *dev)
{
	if (!dev || !dev->input_dev)
		return;

	input_unregister_device(dev->input_dev);
	dev->input_dev = NULL;
}

