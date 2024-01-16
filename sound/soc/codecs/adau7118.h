/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ADAU7118_H
#define _LINUX_ADAU7118_H

struct regmap;
struct device;

/* register map */
#define ADAU7118_REG_VENDOR_ID		0x00
#define ADAU7118_REG_DEVICE_ID1		0x01
#define ADAU7118_REG_DEVICE_ID2		0x02
#define ADAU7118_REG_REVISION_ID	0x03
#define ADAU7118_REG_ENABLES		0x04
#define ADAU7118_REG_DEC_RATIO_CLK_MAP	0x05
#define ADAU7118_REG_HPF_CONTROL	0x06
#define ADAU7118_REG_SPT_CTRL1		0x07
#define ADAU7118_REG_SPT_CTRL2		0x08
#define ADAU7118_REG_SPT_CX(num)	(0x09 + (num))
#define ADAU7118_REG_DRIVE_STRENGTH	0x11
#define ADAU7118_REG_RESET		0x12

int adau7118_probe(struct device *dev, struct regmap *map, bool hw_mode);

#endif
