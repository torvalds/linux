/*
 * at91-i2s.h - ALSA I2S interface for the Atmel AT91 SoC
 *
 * Author:	Frank Mandarino <fmandarino@endrelia.com>
 *		Endrelia Technologies Inc.
 * Created:	Jan 9, 2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _AT91_I2S_H
#define _AT91_I2S_H

/* I2S system clock ids */
#define AT91_SYSCLK_MCK		0 /* SSC uses AT91 MCK as system clock */

/* I2S divider ids */
#define AT91SSC_CMR_DIV		0 /* MCK divider for BCLK */
#define AT91SSC_TCMR_PERIOD	1 /* BCLK divider for transmit FS */
#define AT91SSC_RCMR_PERIOD	2 /* BCLK divider for receive FS */

extern struct snd_soc_cpu_dai at91_i2s_dai[];

#endif /* _AT91_I2S_H */

