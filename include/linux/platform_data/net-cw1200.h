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
	int reset;                     /* GPIO to RSTn signal (0 disables) */
	int powerup;                   /* GPIO to POWERUP signal (0 disables) */
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
	bool have_5ghz;
	bool no_nptb;       /* SDIO hardware does not support non-power-of-2-blocksizes */
	int reset;          /* GPIO to RSTn signal (0 disables) */
	int powerup;        /* GPIO to POWERUP signal (0 disables) */
	int irq;            /* IRQ line or 0 to use SDIO IRQ */
	int (*power_ctrl)(const struct cw1200_platform_data_sdio *pdata,
			  bool enable); /* Control 3v3 / 1v8 supply */
	int (*clk_ctrl)(const struct cw1200_platform_data_sdio *pdata,
			bool enable); /* Control CLK32K */
	const u8 *macaddr;  /* if NULL, use cw1200_mac_template module parameter */
	const char *sdd_file;  /* if NULL, will use default for detected hw type */
};


/* An example of SPI support in your board setup file:

   static struct cw1200_platform_data_spi cw1200_platform_data = {
       .ref_clk = 38400,
       .spi_bits_per_word = 16,
       .reset = GPIO_RF_RESET,
       .powerup = GPIO_RF_POWERUP,
       .macaddr = wifi_mac_addr,
       .sdd_file = "sdd_sagrad_1091_1098.bin",
  };
  static struct spi_board_info myboard_spi_devices[] __initdata = {
       {
               .modalias = "cw1200_wlan_spi",
               .max_speed_hz = 52000000,
               .bus_num = 0,
               .irq = WIFI_IRQ,
               .platform_data = &cw1200_platform_data,
               .chip_select = 0,
       },
  };

 */

/* An example of SDIO support in your board setup file:

  static struct cw1200_platform_data_sdio my_cw1200_platform_data = {
	.ref_clk = 38400,
	.have_5ghz = false,
	.sdd_file = "sdd_myplatform.bin",
  };
  cw1200_sdio_set_platform_data(&my_cw1200_platform_data);

 */

void __init cw1200_sdio_set_platform_data(struct cw1200_platform_data_sdio *pdata);

#endif /* CW1200_PLAT_H_INCLUDED */
