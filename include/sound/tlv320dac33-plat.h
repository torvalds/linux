/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Platform header for Texas Instruments TLV320DAC33 codec driver
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * Copyright:   (C) 2009 Nokia Corporation
 */

#ifndef __TLV320DAC33_PLAT_H
#define __TLV320DAC33_PLAT_H

struct tlv320dac33_platform_data {
	int power_gpio;
	int mode1_latency; /* latency caused by the i2c writes in us */
	int auto_fifo_config; /* FIFO config based on the period size */
	int keep_bclk;	/* Keep the BCLK running in FIFO modes */
	u8 burst_bclkdiv;
};

#endif /* __TLV320DAC33_PLAT_H */
