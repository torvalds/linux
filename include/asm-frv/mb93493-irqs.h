/* mb93493-irqs.h: MB93493 companion chip IRQs
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_MB93493_IRQS_H
#define _ASM_MB93493_IRQS_H

#ifndef __ASSEMBLY__

#include <asm/irq-routing.h>

#define IRQ_BASE_MB93493	(NR_IRQ_ACTIONS_PER_GROUP * 2)

/* IRQ IDs presented to drivers */
enum {
	IRQ_MB93493_VDC			= IRQ_BASE_MB93493 + 0,
	IRQ_MB93493_VCC			= IRQ_BASE_MB93493 + 1,
	IRQ_MB93493_AUDIO_OUT		= IRQ_BASE_MB93493 + 2,
	IRQ_MB93493_I2C_0		= IRQ_BASE_MB93493 + 3,
	IRQ_MB93493_I2C_1		= IRQ_BASE_MB93493 + 4,
	IRQ_MB93493_USB			= IRQ_BASE_MB93493 + 5,
	IRQ_MB93493_LOCAL_BUS		= IRQ_BASE_MB93493 + 7,
	IRQ_MB93493_PCMCIA		= IRQ_BASE_MB93493 + 8,
	IRQ_MB93493_GPIO		= IRQ_BASE_MB93493 + 9,
	IRQ_MB93493_AUDIO_IN		= IRQ_BASE_MB93493 + 10,
};

/* IRQ multiplexor mappings */
#define ROUTE_VIA_IRQ0	0	/* route IRQ by way of CPU external IRQ 0 */
#define ROUTE_VIA_IRQ1	1	/* route IRQ by way of CPU external IRQ 1 */

#define IRQ_MB93493_VDC_ROUTE		ROUTE_VIA_IRQ0
#define IRQ_MB93493_VCC_ROUTE		ROUTE_VIA_IRQ1
#define IRQ_MB93493_AUDIO_OUT_ROUTE	ROUTE_VIA_IRQ1
#define IRQ_MB93493_I2C_0_ROUTE		ROUTE_VIA_IRQ1
#define IRQ_MB93493_I2C_1_ROUTE		ROUTE_VIA_IRQ1
#define IRQ_MB93493_USB_ROUTE		ROUTE_VIA_IRQ1
#define IRQ_MB93493_LOCAL_BUS_ROUTE	ROUTE_VIA_IRQ1
#define IRQ_MB93493_PCMCIA_ROUTE	ROUTE_VIA_IRQ1
#define IRQ_MB93493_GPIO_ROUTE		ROUTE_VIA_IRQ1
#define IRQ_MB93493_AUDIO_IN_ROUTE	ROUTE_VIA_IRQ1

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_MB93493_IRQS_H */
