/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI DaVinci Audio Serial Port support
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - https://www.ti.com/
 */

#ifndef __DAVINCI_ASP_H
#define __DAVINCI_ASP_H

#include <linux/genalloc.h>

struct davinci_mcasp_pdata {
	u32 tx_dma_offset;
	u32 rx_dma_offset;
	int asp_chan_q;	/* event queue number for ASP channel */
	int ram_chan_q;	/* event queue number for RAM channel */
	/*
	 * Allowing this is more efficient and eliminates left and right swaps
	 * caused by underruns, but will swap the left and right channels
	 * when compared to previous behavior.
	 */
	unsigned enable_channel_combine:1;
	unsigned sram_size_playback;
	unsigned sram_size_capture;
	struct gen_pool *sram_pool;

	/*
	 * If McBSP peripheral gets the clock from an external pin,
	 * there are three chooses, that are MCBSP_CLKX, MCBSP_CLKR
	 * and MCBSP_CLKS.
	 * Depending on different hardware connections it is possible
	 * to use this setting to change the behaviour of McBSP
	 * driver.
	 */
	int clk_input_pin;

	/*
	 * This flag works when both clock and FS are outputs for the cpu
	 * and makes clock more accurate (FS is not symmetrical and the
	 * clock is very fast.
	 * The clock becoming faster is named
	 * i2s continuous serial clock (I2S_SCK) and it is an externally
	 * visible bit clock.
	 *
	 * first line : WordSelect
	 * second line : ContinuousSerialClock
	 * third line: SerialData
	 *
	 * SYMMETRICAL APPROACH:
	 *   _______________________          LEFT
	 * _|         RIGHT         |______________________|
	 *     _   _         _   _   _   _         _   _
	 *   _| |_| |_ x16 _| |_| |_| |_| |_ x16 _| |_| |_
	 *     _   _         _   _   _   _         _   _
	 *   _/ \_/ \_ ... _/ \_/ \_/ \_/ \_ ... _/ \_/ \_
	 *    \_/ \_/       \_/ \_/ \_/ \_/       \_/ \_/
	 *
	 * ACCURATE CLOCK APPROACH:
	 *   ______________          LEFT
	 * _|     RIGHT    |_______________________________|
	 *     _         _   _         _   _   _   _   _   _
	 *   _| |_ x16 _| |_| |_ x16 _| |_| |_| |_| |_| |_| |
	 *     _         _   _          _      dummy cycles
	 *   _/ \_ ... _/ \_/ \_  ... _/ \__________________
	 *    \_/       \_/ \_/        \_/
	 *
	 */
	bool i2s_accurate_sck;

	/* McASP specific fields */
	int tdm_slots;
	u8 op_mode;
	u8 dismod;
	u8 num_serializer;
	u8 *serial_dir;
	u8 version;
	u8 txnumevt;
	u8 rxnumevt;
	int tx_dma_channel;
	int rx_dma_channel;
};
/* TODO: Fix arch/arm/mach-davinci/ users and remove this define */
#define snd_platform_data davinci_mcasp_pdata

enum {
	MCASP_VERSION_1 = 0,	/* DM646x */
	MCASP_VERSION_2,	/* DA8xx/OMAPL1x */
	MCASP_VERSION_3,        /* TI81xx/AM33xx */
	MCASP_VERSION_4,	/* DRA7xxx */
	MCASP_VERSION_OMAP,	/* OMAP4/5 */
};

enum mcbsp_clk_input_pin {
	MCBSP_CLKR = 0,		/* as in DM365 */
	MCBSP_CLKS,
};

#define INACTIVE_MODE	0
#define TX_MODE		1
#define RX_MODE		2

#define DAVINCI_MCASP_IIS_MODE	0
#define DAVINCI_MCASP_DIT_MODE	1

#endif
