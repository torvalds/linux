// SPDX-License-Identifier: GPL-2.0-only
//
// rt-sdw-common.c
//
// Copyright(c) 2024 Realtek Semiconductor Corp.
//

/*
 * This file defines common functions used with Realtek soundwire codecs.
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/bitops.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/jack.h>

#include "rt-sdw-common.h"

/**
 * rt_sdca_index_write - Write a value to Realtek defined register.
 *
 * @map: map for setting.
 * @nid: Realtek-defined ID.
 * @reg: register.
 * @value: value.
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int rt_sdca_index_write(struct regmap *map, unsigned int nid,
	unsigned int reg, unsigned int value)
{
	unsigned int addr = (nid << 20) | reg;
	int ret;

	ret = regmap_write(map, addr, value);
	if (ret < 0)
		pr_err("Failed to set value: %06x <= %04x ret=%d\n",
			addr, value, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(rt_sdca_index_write);

/**
 * rt_sdca_index_read - Read value from Realtek defined register.
 *
 * @map: map for setting.
 * @nid: Realtek-defined ID.
 * @reg: register.
 * @value: value.
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int rt_sdca_index_read(struct regmap *map, unsigned int nid,
	unsigned int reg, unsigned int *value)
{
	unsigned int addr = (nid << 20) | reg;
	int ret;

	ret = regmap_read(map, addr, value);
	if (ret < 0)
		pr_err("Failed to get value: %06x => %04x ret=%d\n",
			addr, *value, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(rt_sdca_index_read);

/**
 * rt_sdca_index_update_bits - Update value on Realtek defined register.
 *
 * @map: map for setting.
 * @nid: Realtek-defined ID.
 * @reg: register.
 * @mask: Bitmask to change
 * @val: New value for bitmask
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */

int rt_sdca_index_update_bits(struct regmap *map,
	unsigned int nid, unsigned int reg, unsigned int mask, unsigned int val)
{
	unsigned int tmp;
	int ret;

	ret = rt_sdca_index_read(map, nid, reg, &tmp);
	if (ret < 0)
		return ret;

	set_mask_bits(&tmp, mask, val);
	return rt_sdca_index_write(map, nid, reg, tmp);
}
EXPORT_SYMBOL_GPL(rt_sdca_index_update_bits);

/**
 * rt_sdca_btn_type - Decision of button type.
 *
 * @buffer: UMP message buffer.
 *
 * A button type will be returned regarding to buffer,
 * it returns zero if buffer cannot be recognized.
 */
int rt_sdca_btn_type(unsigned char *buffer)
{
	u8 btn_type = 0;
	int ret = 0;

	btn_type |= buffer[0] & 0xf;
	btn_type |= (buffer[0] >> 4) & 0xf;
	btn_type |= buffer[1] & 0xf;
	btn_type |= (buffer[1] >> 4) & 0xf;

	if (btn_type & BIT(0))
		ret |= SND_JACK_BTN_2;
	if (btn_type & BIT(1))
		ret |= SND_JACK_BTN_3;
	if (btn_type & BIT(2))
		ret |= SND_JACK_BTN_0;
	if (btn_type & BIT(3))
		ret |= SND_JACK_BTN_1;

	return ret;
}
EXPORT_SYMBOL_GPL(rt_sdca_btn_type);

/**
 * rt_sdca_headset_detect - Headset jack type detection.
 *
 * @map: map for setting.
 * @entity_id: SDCA entity ID.
 *
 * A headset jack type will be returned, a negative errno will
 * be returned in error cases.
 */
int rt_sdca_headset_detect(struct regmap *map, unsigned int entity_id)
{
	unsigned int det_mode, jack_type;
	int ret;

	/* get detected_mode */
	ret = regmap_read(map, SDW_SDCA_CTL(SDCA_NUM_JACK_CODEC, entity_id,
			RT_SDCA_CTL_DETECTED_MODE, 0), &det_mode);

	if (ret < 0)
		goto io_error;

	switch (det_mode) {
	case 0x03:
		jack_type = SND_JACK_HEADPHONE;
		break;
	case 0x05:
		jack_type = SND_JACK_HEADSET;
		break;
	default:
		jack_type = 0;
		break;
	}

	/* write selected_mode */
	if (det_mode) {
		ret = regmap_write(map, SDW_SDCA_CTL(SDCA_NUM_JACK_CODEC, entity_id,
				RT_SDCA_CTL_SELECTED_MODE, 0), det_mode);
		if (ret < 0)
			goto io_error;
	}

	return jack_type;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(rt_sdca_headset_detect);

/**
 * rt_sdca_button_detect - Read UMP message and decide button type.
 *
 * @map: map for setting.
 * @entity_id: SDCA entity ID.
 * @hid_buf_addr: HID buffer address.
 * @hid_id: Report ID for HID.
 *
 * A button type will be returned regarding to buffer,
 * it returns zero if buffer cannot be recognized.
 */
int rt_sdca_button_detect(struct regmap *map, unsigned int entity_id,
	unsigned int hid_buf_addr, unsigned int hid_id)
{
	unsigned int btn_type = 0, offset, idx, val, owner;
	unsigned char buf[3];
	int ret;

	/* get current UMP message owner */
	ret = regmap_read(map, SDW_SDCA_CTL(SDCA_NUM_HID, entity_id,
			RT_SDCA_CTL_HIDTX_CURRENT_OWNER, 0), &owner);
	if (ret < 0)
		return 0;

	/* if owner is device then there is no button event from device */
	if (owner == 1)
		return 0;

	/* read UMP message offset */
	ret = regmap_read(map, SDW_SDCA_CTL(SDCA_NUM_HID, entity_id,
			RT_SDCA_CTL_HIDTX_MESSAGE_OFFSET, 0), &offset);
	if (ret < 0)
		goto _end_btn_det_;

	for (idx = 0; idx < sizeof(buf); idx++) {
		ret = regmap_read(map, hid_buf_addr + offset + idx, &val);
		if (ret < 0)
			goto _end_btn_det_;
		buf[idx] = val & 0xff;
	}
	/* Report ID for HID */
	if (buf[0] == hid_id)
		btn_type = rt_sdca_btn_type(&buf[1]);

_end_btn_det_:
	/* Host is owner, so set back to device */
	if (owner == 0)
		/* set owner to device */
		regmap_write(map,
			SDW_SDCA_CTL(SDCA_NUM_HID, entity_id,
				RT_SDCA_CTL_HIDTX_CURRENT_OWNER, 0), 0x01);

	return btn_type;
}
EXPORT_SYMBOL_GPL(rt_sdca_button_detect);

MODULE_DESCRIPTION("Realtek soundwire common functions");
MODULE_AUTHOR("jack yu <jack.yu@realtek.com>");
MODULE_LICENSE("GPL");
