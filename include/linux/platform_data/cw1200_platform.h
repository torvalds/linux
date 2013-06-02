/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef CW1200_PLAT_H_INCLUDED
#define CW1200_PLAT_H_INCLUDED

struct cw1200_platform_data_spi {
	u8 spi_bits_per_word;           /* REQUIRED */
	u16 ref_clk;                    /* REQUIRED (in KHz) */

	/* All others are optional */
	bool have_5ghz;
	const struct resource *reset;   /* GPIO to RSTn signal */
	const struct resource *powerup; /* GPIO to POWERUP signal */
	int (*power_ctrl)(const struct cw1200_platform_data_spi *pdata,
			  bool enable); /* Control 3v3 / 1v8 supply */
	int (*clk_ctrl)(const struct cw1200_platform_data_spi *pdata,
			bool enable); /* Control CLK32K */
	const u8 *macaddr;  /* if NULL, use cw1200_mac_template module parameter */
	const char *sdd_file;  /* if NULL, will use default for detected hw type */
};

struct cw1200_platform_data_sdio {
	u16 ref_clk;                    /* REQUIRED (in KHz) */

	/* All others are optional */
	const struct resource *irq;     /* if using GPIO for IRQ */
	bool have_5ghz;
	bool no_nptb;                   /* SDIO hardware does not support non-power-of-2-blocksizes */
	const struct resource *reset;   /* GPIO to RSTn signal */
	const struct resource *powerup; /* GPIO to POWERUP signal */
	int (*power_ctrl)(const struct cw1200_platform_data_sdio *pdata,
			  bool enable); /* Control 3v3 / 1v8 supply */
	int (*clk_ctrl)(const struct cw1200_platform_data_sdio *pdata,
			bool enable); /* Control CLK32K */
	const u8 *macaddr;  /* if NULL, use cw1200_mac_template module parameter */
	const char *sdd_file;  /* if NULL, will use default for detected hw type */
};

const void *cw1200_get_platform_data(void);

#endif /* CW1200_PLAT_H_INCLUDED */
