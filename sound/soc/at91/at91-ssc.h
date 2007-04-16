/*
 * at91-ssc.h - ALSA SSC interface for the Atmel AT91 SoC
 *
 * Author:	Frank Mandarino <fmandarino@endrelia.com>
 *		Endrelia Technologies Inc.
 * Created:	Jan 9, 2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _AT91_SSC_H
#define _AT91_SSC_H

/* SSC system clock ids */
#define AT91_SYSCLK_MCK		0 /* SSC uses AT91 MCK as system clock */

/* SSC divider ids */
#define AT91SSC_CMR_DIV		0 /* MCK divider for BCLK */
#define AT91SSC_TCMR_PERIOD	1 /* BCLK divider for transmit FS */
#define AT91SSC_RCMR_PERIOD	2 /* BCLK divider for receive FS */

extern struct snd_soc_cpu_dai at91_ssc_dai[];

#endif /* _AT91_SSC_H */

