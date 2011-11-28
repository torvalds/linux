/*
 * Copyright (c) 2011 Bosch Sensortec GmbH
 * Copyright (c) 2011 Unixphere
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _BMA150_H_
#define _BMA150_H_

#define BMA150_DRIVER		"bma150"

struct bma150_cfg {
	bool any_motion_int;		/* Set to enable any-motion interrupt */
	bool hg_int;			/* Set to enable high-G interrupt */
	bool lg_int;			/* Set to enable low-G interrupt */
	unsigned char any_motion_dur;	/* Any-motion duration */
	unsigned char any_motion_thres;	/* Any-motion threshold */
	unsigned char hg_hyst;		/* High-G hysterisis */
	unsigned char hg_dur;		/* High-G duration */
	unsigned char hg_thres;		/* High-G threshold */
	unsigned char lg_hyst;		/* Low-G hysterisis */
	unsigned char lg_dur;		/* Low-G duration */
	unsigned char lg_thres;		/* Low-G threshold */
	unsigned char range;		/* BMA0150_RANGE_xxx (in G) */
	unsigned char bandwidth;	/* BMA0150_BW_xxx (in Hz) */
};

struct bma150_platform_data {
	struct bma150_cfg cfg;
	int (*irq_gpio_cfg)(void);
};

#endif /* _BMA150_H_ */
