/* mb93093-fpga-irqs.h: MB93093 CPU board FPGA IRQs
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_MB93093_FPGA_IRQS_H
#define _ASM_MB93093_FPGA_IRQS_H

#ifndef __ASSEMBLY__

#include <asm/irq-routing.h>

#define IRQ_BASE_FPGA		(NR_IRQ_ACTIONS_PER_GROUP * 1)

/* IRQ IDs presented to drivers */
enum {
	IRQ_FPGA_PUSH_BUTTON_SW1_5		= IRQ_BASE_FPGA + 8,
	IRQ_FPGA_ROCKER_C_SW8			= IRQ_BASE_FPGA + 9,
	IRQ_FPGA_ROCKER_C_SW9			= IRQ_BASE_FPGA + 10,
};


#endif /* !__ASSEMBLY__ */

#endif /* _ASM_MB93093_FPGA_IRQS_H */
