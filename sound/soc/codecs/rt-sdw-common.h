/* SPDX-License-Identifier: GPL-2.0-only */
//
// rt-sdw-common.h
//
// Copyright(c) 2024 Realtek Semiconductor Corp.
//

/*
 * This file defines common functions used with Realtek soundwire codecs.
 */

#ifndef __RT_SDW_COMMON_H__
#define __RT_SDW_COMMON_H__

#define SDCA_NUM_JACK_CODEC			0x01
#define SDCA_NUM_MIC_ARRAY			0x02
#define SDCA_NUM_HID				0x03
#define SDCA_NUM_AMP				0x04
#define RT_SDCA_CTL_SELECTED_MODE		0x01
#define RT_SDCA_CTL_DETECTED_MODE		0x02
#define RT_SDCA_CTL_HIDTX_CURRENT_OWNER		0x10
#define RT_SDCA_CTL_HIDTX_MESSAGE_OFFSET	0x12

struct rt_sdca_dmic_kctrl_priv {
	unsigned int reg_base;
	unsigned int count;
	unsigned int max;
	unsigned int invert;
};

#define RT_SDCA_PR_VALUE(xreg_base, xcount, xmax, xinvert) \
	((unsigned long)&(struct rt_sdca_dmic_kctrl_priv) \
		{.reg_base = xreg_base, .count = xcount, .max = xmax, \
		.invert = xinvert})

#define RT_SDCA_FU_CTRL(xname, reg_base, xmax, xinvert, xcount, \
	xinfo, xget, xput) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.info = xinfo, \
	.get = xget, \
	.put = xput, \
	.private_value = RT_SDCA_PR_VALUE(reg_base, xcount, xmax, xinvert)}

#define RT_SDCA_EXT_TLV(xname, reg_base, xhandler_get,\
	 xhandler_put, xcount, xmax, tlv_array, xinfo) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		 SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = xinfo, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = RT_SDCA_PR_VALUE(reg_base, xcount, xmax, 0) }


int rt_sdca_index_write(struct regmap *map, unsigned int nid,
	unsigned int reg, unsigned int value);
int rt_sdca_index_read(struct regmap *map, unsigned int nid,
	unsigned int reg, unsigned int *value);
int rt_sdca_index_update_bits(struct regmap *map,
	unsigned int nid, unsigned int reg, unsigned int mask, unsigned int val);
int rt_sdca_btn_type(unsigned char *buffer);
int rt_sdca_headset_detect(struct regmap *map, unsigned int entity_id);
int rt_sdca_button_detect(struct regmap *map, unsigned int entity_id,
	unsigned int hid_buf_addr, unsigned int hid_id);

#endif /* __RT_SDW_COMMON_H__ */
