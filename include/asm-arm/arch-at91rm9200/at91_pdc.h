/*
 * include/asm-arm/arch-at91rm9200/at91_pdc.h
 *
 * Copyright (C) 2005 Ivan Kokshaysky
 * Copyright (C) SAN People
 *
 * Peripheral Data Controller (PDC) registers.
 * Based on AT91RM9200 datasheet revision E.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91_PDC_H
#define AT91_PDC_H

#define AT91_PDC_RPR		0x100	/* Receive Pointer Register */
#define AT91_PDC_RCR		0x104	/* Receive Counter Register */
#define AT91_PDC_TPR		0x108	/* Transmit Pointer Register */
#define AT91_PDC_TCR		0x10c	/* Transmit Counter Register */
#define AT91_PDC_RNPR		0x110	/* Receive Next Pointer Register */
#define AT91_PDC_RNCR		0x114	/* Receive Next Counter Register */
#define AT91_PDC_TNPR		0x118	/* Transmit Next Pointer Register */
#define AT91_PDC_TNCR		0x11c	/* Transmit Next Counter Register */

#define AT91_PDC_PTCR		0x120	/* Transfer Control Register */
#define		AT91_PDC_RXTEN		(1 << 0)	/* Receiver Transfer Enable */
#define		AT91_PDC_RXTDIS		(1 << 1)	/* Receiver Transfer Disable */
#define		AT91_PDC_TXTEN		(1 << 8)	/* Transmitter Transfer Enable */
#define		AT91_PDC_TXTDIS		(1 << 9)	/* Transmitter Transfer Disable */

#define AT91_PDC_PTSR		0x124	/* Transfer Status Register */

#endif
