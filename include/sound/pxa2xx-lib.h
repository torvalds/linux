/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PXA2XX_LIB_H
#define PXA2XX_LIB_H

#include <linux/types.h>

/* modem registers, used by touchscreen driver */
u32 pxa2xx_ac97_read_modr(void);
u32 pxa2xx_ac97_read_misr(void);

#endif
