/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * omap-twl4030.h - ASoC machine driver for TI SoC based boards with twl4030
 *		    codec, header.
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
 * All rights reserved.
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#ifndef _OMAP_TWL4030_H_
#define _OMAP_TWL4030_H_

/* To select if only one channel is connected in a stereo port */
#define OMAP_TWL4030_LEFT	(1 << 0)
#define OMAP_TWL4030_RIGHT	(1 << 1)

struct omap_tw4030_pdata {
	const char *card_name;
	/* Voice port is connected to McBSP3 */
	bool voice_connected;

	/* The driver will parse the connection flags if this flag is set */
	bool	custom_routing;
	/* Flags to indicate connected audio ports. */
	u8	has_hs;
	u8	has_hf;
	u8	has_predriv;
	u8	has_carkit;
	bool	has_ear;

	bool	has_mainmic;
	bool	has_submic;
	bool	has_hsmic;
	bool	has_carkitmic;
	bool	has_digimic0;
	bool	has_digimic1;
	u8	has_linein;

	/* Jack detect GPIO or  <= 0 if it is not implemented */
	int jack_detect;
};

#endif /* _OMAP_TWL4030_H_ */
