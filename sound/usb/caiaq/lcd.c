// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LCD support for the Native Instruments Kore controllers
 *
 * Copyright (c) 2026 Niko Huuskonen <niko.huuskonen.00@gmail.com>
 *
 * Both Kore controllers have a 128x64 pixel monochrome LCD behind an
 * ST7565-style controller. The firmware passes EP1 packets with the
 * command byte EP1_CMD_LCD on to that controller:
 *
 *   EP1_CMD_LCD 0x00 <n> <n controller command bytes>
 *   EP1_CMD_LCD 0x01 <n> <n display RAM bytes>
 *
 * The display is exposed as an exclusive hwdep device. Each write carries
 * a whole frame of CAIAQ_LCD_FRAME_SIZE bytes: 8 pages of 128 columns,
 * one byte per column and page, with bit 0 as the top pixel of the page.
 * Only the pages that changed since the previous frame are sent to the
 * device. The contrast is an ALSA control; the backlight is the existing
 * "LED lcd" control.
 *
 * The controller setup and the packet layout follow the USB traffic of
 * the vendor software, as documented by the OpenKoreBridge project.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/hwdep.h>
#include <sound/pcm.h>

#include "device.h"
#include "lcd.h"

#define LCD_WIDTH		128
#define LCD_PAGES		(CAIAQ_LCD_FRAME_SIZE / LCD_WIDTH)
#define LCD_COLUMN_OFFSET	4	/* first visible controller column */
#define LCD_CHUNK		32	/* display RAM bytes per packet */
#define LCD_CONTRAST_MAX	0x3f
#define LCD_CONTRAST_DEFAULT	0x1c

#define LCD_KIND_COMMAND	0x00
#define LCD_KIND_DATA		0x01

static int lcd_send(struct snd_usb_caiaqdev *cdev, u8 kind,
		    const u8 *bytes, unsigned int len)
{
	u8 buf[2 + LCD_CHUNK];

	if (WARN_ON(len > LCD_CHUNK))
		return -EINVAL;

	buf[0] = kind;
	buf[1] = len;
	memcpy(buf + 2, bytes, len);
	return snd_usb_caiaq_send_command(cdev, EP1_CMD_LCD, buf, len + 2);
}

static int lcd_command(struct snd_usb_caiaqdev *cdev, u8 cmd)
{
	return lcd_send(cdev, LCD_KIND_COMMAND, &cmd, 1);
}

static int lcd_set_contrast(struct snd_usb_caiaqdev *cdev)
{
	const u8 cmd[] = { 0x81, cdev->lcd_contrast };

	return lcd_send(cdev, LCD_KIND_COMMAND, cmd, sizeof(cmd));
}

/* controller setup, in the order used by the vendor software */
static int lcd_setup(struct snd_usb_caiaqdev *cdev)
{
	static const u8 head[] = {
		0xe2,			/* reset */
		0xa1,			/* reverse column direction */
		0xc8,			/* reverse row direction */
		0xa2,			/* 1/9 bias */
		0x2c, 0x2e, 0x2f,	/* power up in three steps */
		0x27,			/* regulator resistor ratio */
	};
	static const u8 tail[] = {
		0xa6,			/* normal, non-inverted display */
		0x88, 0xef,		/* sent by the vendor software */
		0xaf,			/* display on */
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(head); i++) {
		ret = lcd_command(cdev, head[i]);
		if (ret)
			return ret;
	}

	ret = lcd_set_contrast(cdev);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(tail); i++) {
		ret = lcd_command(cdev, tail[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int lcd_write_page(struct snd_usb_caiaqdev *cdev, unsigned int page,
			  const u8 *data)
{
	unsigned int col;
	int ret;

	for (col = 0; col < LCD_WIDTH; col += LCD_CHUNK) {
		unsigned int addr = LCD_COLUMN_OFFSET + col;
		const u8 column[] = { 0x10 | (addr >> 4), addr & 0x0f };

		ret = lcd_command(cdev, 0xb0 | page);
		if (!ret)
			ret = lcd_send(cdev, LCD_KIND_COMMAND,
				       column, sizeof(column));
		if (!ret)
			ret = lcd_send(cdev, LCD_KIND_DATA,
				       data + col, LCD_CHUNK);
		if (ret)
			return ret;
	}

	return 0;
}

static long lcd_hwdep_write(struct snd_hwdep *hw, const char __user *buf,
			    long count, loff_t *offset)
{
	struct snd_usb_caiaqdev *cdev = hw->private_data;
	unsigned int page;
	int ret;

	if (count != CAIAQ_LCD_FRAME_SIZE)
		return -EINVAL;

	u8 *frame __free(kfree) = memdup_user(buf, count);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	guard(mutex)(&cdev->lcd_mutex);

	if (!cdev->lcd_ready) {
		ret = lcd_setup(cdev);
		if (ret)
			return ret;
		cdev->lcd_ready = true;
		cdev->lcd_frame_valid = false;
	}

	for (page = 0; page < LCD_PAGES; page++) {
		u8 *shown = cdev->lcd_frame + page * LCD_WIDTH;
		const u8 *next = frame + page * LCD_WIDTH;

		if (cdev->lcd_frame_valid && !memcmp(shown, next, LCD_WIDTH))
			continue;

		ret = lcd_write_page(cdev, page, next);
		if (ret) {
			cdev->lcd_frame_valid = false;
			return ret;
		}
		memcpy(shown, next, LCD_WIDTH);
	}
	cdev->lcd_frame_valid = true;

	return count;
}

static int lcd_contrast_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = LCD_CONTRAST_MAX;
	return 0;
}

static int lcd_contrast_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_usb_caiaqdev *cdev = snd_kcontrol_chip(kcontrol);

	guard(mutex)(&cdev->lcd_mutex);
	ucontrol->value.integer.value[0] = cdev->lcd_contrast;
	return 0;
}

static int lcd_contrast_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_usb_caiaqdev *cdev = snd_kcontrol_chip(kcontrol);
	long val = ucontrol->value.integer.value[0];
	int ret;

	if (val < 0 || val > LCD_CONTRAST_MAX)
		return -EINVAL;

	guard(mutex)(&cdev->lcd_mutex);
	if (val == cdev->lcd_contrast)
		return 0;

	cdev->lcd_contrast = val;
	if (cdev->lcd_ready) {
		ret = lcd_set_contrast(cdev);
		if (ret)
			return ret;
	}

	return 1;
}

static const struct snd_kcontrol_new lcd_contrast_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_HWDEP,
	.name = "LCD Contrast",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = lcd_contrast_info,
	.get = lcd_contrast_get,
	.put = lcd_contrast_put,
};

int snd_usb_caiaq_lcd_init(struct snd_usb_caiaqdev *cdev)
{
	struct snd_card *card = cdev->chip.card;
	struct snd_hwdep *hw;
	int ret;

	switch (cdev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_KORECONTROLLER2):
		break;
	default:
		return 0;
	}

	mutex_init(&cdev->lcd_mutex);
	cdev->lcd_contrast = LCD_CONTRAST_DEFAULT;

	ret = snd_hwdep_new(card, "Kore LCD", 0, &hw);
	if (ret < 0)
		return ret;

	strscpy(hw->name, "Kore LCD", sizeof(hw->name));
	hw->iface = SNDRV_HWDEP_IFACE_CAIAQ;
	hw->private_data = cdev;
	hw->exclusive = 1;
	hw->ops.write = lcd_hwdep_write;

	return snd_ctl_add(card, snd_ctl_new1(&lcd_contrast_control, cdev));
}
