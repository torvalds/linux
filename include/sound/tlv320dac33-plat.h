/*
 * Platform header for Texas Instruments TLV320DAC33 codec driver
 *
 * Author:	Peter Ujfalusi <peter.ujfalusi@nokia.com>
 *
 * Copyright:   (C) 2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __TLV320DAC33_PLAT_H
#define __TLV320DAC33_PLAT_H

struct tlv320dac33_platform_data {
	int power_gpio;
	int keep_bclk;	/* Keep the BCLK running in FIFO modes */
	u8 burst_bclkdiv;
};

#endif /* __TLV320DAC33_PLAT_H */
