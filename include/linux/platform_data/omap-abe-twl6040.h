/**
 * omap-abe-twl6040.h - ASoC machine driver OMAP4+ devices, header.
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com
 * All rights reserved.
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef _OMAP_ABE_TWL6040_H_
#define _OMAP_ABE_TWL6040_H_

/* To select if only one channel is connected in a stereo port */
#define ABE_TWL6040_LEFT	(1 << 0)
#define ABE_TWL6040_RIGHT	(1 << 1)

struct omap_abe_twl6040_data {
	char *card_name;
	/* Feature flags for connected audio pins */
	u8	has_hs;
	u8	has_hf;
	bool	has_ep;
	u8	has_aux;
	u8	has_vibra;
	bool	has_dmic;
	bool	has_hsmic;
	bool	has_mainmic;
	bool	has_submic;
	u8	has_afm;
	/* Other features */
	bool	jack_detection;	/* board can detect jack events */
	int	mclk_freq;	/* MCLK frequency speed for twl6040 */
};

#endif /* _OMAP_ABE_TWL6040_H_ */
