/*
 * Pin definitions for AT32AP7000.
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARCH_AT32AP700X_H__
#define __ASM_ARCH_AT32AP700X_H__

#define GPIO_PERIPH_A	0
#define GPIO_PERIPH_B	1

/*
 * Pin numbers identifying specific GPIO pins on the chip. They can
 * also be converted to IRQ numbers by passing them through
 * gpio_to_irq().
 */
#define GPIO_PIOA_BASE	(0)
#define GPIO_PIOB_BASE	(GPIO_PIOA_BASE + 32)
#define GPIO_PIOC_BASE	(GPIO_PIOB_BASE + 32)
#define GPIO_PIOD_BASE	(GPIO_PIOC_BASE + 32)
#define GPIO_PIOE_BASE	(GPIO_PIOD_BASE + 32)

#define GPIO_PIN_PA(N)	(GPIO_PIOA_BASE + (N))
#define GPIO_PIN_PB(N)	(GPIO_PIOB_BASE + (N))
#define GPIO_PIN_PC(N)	(GPIO_PIOC_BASE + (N))
#define GPIO_PIN_PD(N)	(GPIO_PIOD_BASE + (N))
#define GPIO_PIN_PE(N)	(GPIO_PIOE_BASE + (N))


/*
 * DMAC peripheral hardware handshaking interfaces, used with dw_dmac
 */
#define DMAC_MCI_RX		0
#define DMAC_MCI_TX		1
#define DMAC_DAC_TX		2
#define DMAC_AC97_A_RX		3
#define DMAC_AC97_A_TX		4
#define DMAC_AC97_B_RX		5
#define DMAC_AC97_B_TX		6
#define DMAC_DMAREQ_0		7
#define DMAC_DMAREQ_1		8
#define DMAC_DMAREQ_2		9
#define DMAC_DMAREQ_3		10

#endif /* __ASM_ARCH_AT32AP700X_H__ */
