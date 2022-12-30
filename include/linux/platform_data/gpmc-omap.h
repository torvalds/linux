/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OMAP GPMC Platform data
 *
 * Copyright (C) 2014 Texas Instruments, Inc. - https://www.ti.com
 *	Roger Quadros <rogerq@ti.com>
 */

#ifndef _GPMC_OMAP_H_
#define _GPMC_OMAP_H_

/* Maximum Number of Chip Selects */
#define GPMC_CS_NUM		8

/* bool type time settings */
struct gpmc_bool_timings {
	bool cycle2cyclediffcsen;
	bool cycle2cyclesamecsen;
	bool we_extra_delay;
	bool oe_extra_delay;
	bool adv_extra_delay;
	bool cs_extra_delay;
	bool time_para_granularity;
};

/*
 * Note that all values in this struct are in nanoseconds except sync_clk
 * (which is in picoseconds), while the register values are in gpmc_fck cycles.
 */
struct gpmc_timings {
	/* Minimum clock period for synchronous mode (in picoseconds) */
	u32 sync_clk;

	/* Chip-select signal timings corresponding to GPMC_CS_CONFIG2 */
	u32 cs_on;		/* Assertion time */
	u32 cs_rd_off;		/* Read deassertion time */
	u32 cs_wr_off;		/* Write deassertion time */

	/* ADV signal timings corresponding to GPMC_CONFIG3 */
	u32 adv_on;		/* Assertion time */
	u32 adv_rd_off;		/* Read deassertion time */
	u32 adv_wr_off;		/* Write deassertion time */
	u32 adv_aad_mux_on;	/* ADV assertion time for AAD */
	u32 adv_aad_mux_rd_off;	/* ADV read deassertion time for AAD */
	u32 adv_aad_mux_wr_off;	/* ADV write deassertion time for AAD */

	/* WE signals timings corresponding to GPMC_CONFIG4 */
	u32 we_on;		/* WE assertion time */
	u32 we_off;		/* WE deassertion time */

	/* OE signals timings corresponding to GPMC_CONFIG4 */
	u32 oe_on;		/* OE assertion time */
	u32 oe_off;		/* OE deassertion time */
	u32 oe_aad_mux_on;	/* OE assertion time for AAD */
	u32 oe_aad_mux_off;	/* OE deassertion time for AAD */

	/* Access time and cycle time timings corresponding to GPMC_CONFIG5 */
	u32 page_burst_access;	/* Multiple access word delay */
	u32 access;		/* Start-cycle to first data valid delay */
	u32 rd_cycle;		/* Total read cycle time */
	u32 wr_cycle;		/* Total write cycle time */

	u32 bus_turnaround;
	u32 cycle2cycle_delay;

	u32 wait_monitoring;
	u32 clk_activation;

	/* The following are only on OMAP3430 */
	u32 wr_access;		/* WRACCESSTIME */
	u32 wr_data_mux_bus;	/* WRDATAONADMUXBUS */

	struct gpmc_bool_timings bool_timings;
};

/* Device timings in picoseconds */
struct gpmc_device_timings {
	u32 t_ceasu;	/* address setup to CS valid */
	u32 t_avdasu;	/* address setup to ADV valid */
	/* XXX: try to combine t_avdp_r & t_avdp_w. Issue is
	 * of tusb using these timings even for sync whilst
	 * ideally for adv_rd/(wr)_off it should have considered
	 * t_avdh instead. This indirectly necessitates r/w
	 * variations of t_avdp as it is possible to have one
	 * sync & other async
	 */
	u32 t_avdp_r;	/* ADV low time (what about t_cer ?) */
	u32 t_avdp_w;
	u32 t_aavdh;	/* address hold time */
	u32 t_oeasu;	/* address setup to OE valid */
	u32 t_aa;	/* access time from ADV assertion */
	u32 t_iaa;	/* initial access time */
	u32 t_oe;	/* access time from OE assertion */
	u32 t_ce;	/* access time from CS asertion */
	u32 t_rd_cycle;	/* read cycle time */
	u32 t_cez_r;	/* read CS deassertion to high Z */
	u32 t_cez_w;	/* write CS deassertion to high Z */
	u32 t_oez;	/* OE deassertion to high Z */
	u32 t_weasu;	/* address setup to WE valid */
	u32 t_wpl;	/* write assertion time */
	u32 t_wph;	/* write deassertion time */
	u32 t_wr_cycle;	/* write cycle time */

	u32 clk;
	u32 t_bacc;	/* burst access valid clock to output delay */
	u32 t_ces;	/* CS setup time to clk */
	u32 t_avds;	/* ADV setup time to clk */
	u32 t_avdh;	/* ADV hold time from clk */
	u32 t_ach;	/* address hold time from clk */
	u32 t_rdyo;	/* clk to ready valid */

	u32 t_ce_rdyz;	/* XXX: description ?, or use t_cez instead */
	u32 t_ce_avd;	/* CS on to ADV on delay */

	/* XXX: check the possibility of combining
	 * cyc_aavhd_oe & cyc_aavdh_we
	 */
	u8 cyc_aavdh_oe;/* read address hold time in cycles */
	u8 cyc_aavdh_we;/* write address hold time in cycles */
	u8 cyc_oe;	/* access time from OE assertion in cycles */
	u8 cyc_wpl;	/* write deassertion time in cycles */
	u32 cyc_iaa;	/* initial access time in cycles */

	/* extra delays */
	bool ce_xdelay;
	bool avd_xdelay;
	bool oe_xdelay;
	bool we_xdelay;
};

#define GPMC_BURST_4			4	/* 4 word burst */
#define GPMC_BURST_8			8	/* 8 word burst */
#define GPMC_BURST_16			16	/* 16 word burst */
#define GPMC_DEVWIDTH_8BIT		1	/* 8-bit device width */
#define GPMC_DEVWIDTH_16BIT		2	/* 16-bit device width */
#define GPMC_MUX_AAD			1	/* Addr-Addr-Data multiplex */
#define GPMC_MUX_AD			2	/* Addr-Data multiplex */

/* Wait pin polarity values */
#define GPMC_WAITPINPOLARITY_INVALID UINT_MAX
#define GPMC_WAITPINPOLARITY_ACTIVE_LOW 0
#define GPMC_WAITPINPOLARITY_ACTIVE_HIGH 1

#define GPMC_WAITPIN_INVALID UINT_MAX

struct gpmc_settings {
	bool burst_wrap;	/* enables wrap bursting */
	bool burst_read;	/* enables read page/burst mode */
	bool burst_write;	/* enables write page/burst mode */
	bool device_nand;	/* device is NAND */
	bool sync_read;		/* enables synchronous reads */
	bool sync_write;	/* enables synchronous writes */
	bool wait_on_read;	/* monitor wait on reads */
	bool wait_on_write;	/* monitor wait on writes */
	u32 burst_len;		/* page/burst length */
	u32 device_width;	/* device bus width (8 or 16 bit) */
	u32 mux_add_data;	/* multiplex address & data */
	u32 wait_pin;		/* wait-pin to be used */
	u32 wait_pin_polarity;
};

/* Data for each chip select */
struct gpmc_omap_cs_data {
	bool valid;			/* data is valid */
	bool is_nand;			/* device within this CS is NAND */
	struct gpmc_settings *settings;
	struct gpmc_device_timings *device_timings;
	struct gpmc_timings *gpmc_timings;
	struct platform_device *pdev;	/* device within this CS region */
	unsigned int pdata_size;
};

struct gpmc_omap_platform_data {
	struct gpmc_omap_cs_data cs[GPMC_CS_NUM];
};

#endif /* _GPMC_OMAP_H */
