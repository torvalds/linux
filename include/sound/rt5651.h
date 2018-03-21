/*
 * linux/sound/rt286.h -- Platform data for RT286
 *
 * Copyright 2013 Realtek Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_RT5651_H
#define __LINUX_SND_RT5651_H

/*
 * Note these MUST match the values from the DT binding:
 * Documentation/devicetree/bindings/sound/rt5651.txt
 */
enum rt5651_jd_src {
	RT5651_JD_NULL,
	RT5651_JD1_1,
	RT5651_JD1_2,
	RT5651_JD2,
};

/*
 * Note these MUST match the values from the DT binding:
 * Documentation/devicetree/bindings/sound/rt5651.txt
 */
enum rt5651_ovcd_sf {
	RT5651_OVCD_SF_0P5,
	RT5651_OVCD_SF_0P75,
	RT5651_OVCD_SF_1P0,
	RT5651_OVCD_SF_1P5,
};

#endif
