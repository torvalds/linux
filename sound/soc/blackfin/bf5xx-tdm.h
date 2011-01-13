/*
 * sound/soc/blackfin/bf5xx-tdm.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BF5XX_TDM_H
#define _BF5XX_TDM_H

#define BFIN_TDM_DAI_MAX_SLOTS 8
struct bf5xx_tdm_port {
	u16 tcr1;
	u16 rcr1;
	u16 tcr2;
	u16 rcr2;
	unsigned int tx_map[BFIN_TDM_DAI_MAX_SLOTS];
	unsigned int rx_map[BFIN_TDM_DAI_MAX_SLOTS];
	int configured;
};

#endif
