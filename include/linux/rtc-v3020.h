/*
 * v3020.h - Registers definition and platform data structure for the v3020 RTC.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006, 8D Technologies inc.
 */
#ifndef __LINUX_V3020_H
#define __LINUX_V3020_H

/* The v3020 has only one data pin but which one
 * is used depends on the board. */
struct v3020_platform_data {
	int leftshift; /* (1<<(leftshift)) & readl() */

	int use_gpio:1;
	unsigned int gpio_cs;
	unsigned int gpio_wr;
	unsigned int gpio_rd;
	unsigned int gpio_io;
};

#define V3020_STATUS_0	0x00
#define V3020_STATUS_1	0x01
#define V3020_SECONDS	0x02
#define V3020_MINUTES	0x03
#define V3020_HOURS		0x04
#define V3020_MONTH_DAY	0x05
#define V3020_MONTH		0x06
#define V3020_YEAR		0x07
#define V3020_WEEK_DAY	0x08
#define V3020_WEEK		0x09

#define V3020_IS_COMMAND(val) ((val)>=0x0E)

#define V3020_CMD_RAM2CLOCK	0x0E
#define V3020_CMD_CLOCK2RAM	0x0F

#endif /* __LINUX_V3020_H */
