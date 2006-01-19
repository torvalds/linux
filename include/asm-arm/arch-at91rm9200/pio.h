/*
 * include/asm-arm/arch-at91rm9200/pio.h
 *
 *  Copyright (C) 2003 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_PIO_H
#define __ASM_ARCH_PIO_H

#include <asm/arch/hardware.h>

static inline void AT91_CfgPIO_USART0(void) {
	at91_sys_write(AT91_PIOA + PIO_PDR, AT91_PA17_TXD0 | AT91_PA18_RXD0 | AT91_PA20_CTS0);

	/*
	 * Errata #39 - RTS0 is not internally connected to PA21.  We need to drive
	 *  the pin manually.  Default is off (RTS is active low).
	 */
	at91_sys_write(AT91_PIOA + PIO_PER, AT91_PA21_RTS0);
	at91_sys_write(AT91_PIOA + PIO_OER, AT91_PA21_RTS0);
	at91_sys_write(AT91_PIOA + PIO_SODR, AT91_PA21_RTS0);
}

static inline void AT91_CfgPIO_USART1(void) {
	at91_sys_write(AT91_PIOB + PIO_PDR, AT91_PB18_RI1 | AT91_PB19_DTR1
			| AT91_PB20_TXD1 | AT91_PB21_RXD1 | AT91_PB23_DCD1
			| AT91_PB24_CTS1 | AT91_PB25_DSR1 | AT91_PB26_RTS1);
}

static inline void AT91_CfgPIO_USART2(void) {
	at91_sys_write(AT91_PIOA + PIO_PDR, AT91_PA22_RXD2 | AT91_PA23_TXD2);
}

static inline void AT91_CfgPIO_USART3(void) {
	at91_sys_write(AT91_PIOA + PIO_PDR, AT91_PA5_TXD3 | AT91_PA6_RXD3);
	at91_sys_write(AT91_PIOA + PIO_BSR, AT91_PA5_TXD3 | AT91_PA6_RXD3);
}

static inline void AT91_CfgPIO_DBGU(void) {
	at91_sys_write(AT91_PIOA + PIO_PDR, AT91_PA31_DTXD | AT91_PA30_DRXD);
}

/*
 * Enable the Two-Wire interface.
 */
static inline void AT91_CfgPIO_TWI(void) {
	at91_sys_write(AT91_PIOA + PIO_PDR, AT91_PA25_TWD | AT91_PA26_TWCK);
	at91_sys_write(AT91_PIOA + PIO_ASR, AT91_PA25_TWD | AT91_PA26_TWCK);
	at91_sys_write(AT91_PIOA + PIO_MDER, AT91_PA25_TWD | AT91_PA26_TWCK);		/* open drain */
}

/*
 * Enable the Serial Peripheral Interface.
 */
static inline void AT91_CfgPIO_SPI(void) {
	at91_sys_write(AT91_PIOA + PIO_PDR, AT91_PA0_MISO | AT91_PA1_MOSI | AT91_PA2_SPCK);
}

static inline void AT91_CfgPIO_SPI_CS0(void) {
	at91_sys_write(AT91_PIOA + PIO_PDR, AT91_PA3_NPCS0);
}

static inline void AT91_CfgPIO_SPI_CS1(void) {
	at91_sys_write(AT91_PIOA + PIO_PDR, AT91_PA4_NPCS1);
}

static inline void AT91_CfgPIO_SPI_CS2(void) {
	at91_sys_write(AT91_PIOA + PIO_PDR, AT91_PA5_NPCS2);
}

static inline void AT91_CfgPIO_SPI_CS3(void) {
	at91_sys_write(AT91_PIOA + PIO_PDR, AT91_PA6_NPCS3);
}

/*
 * Select the DataFlash card.
 */
static inline void AT91_CfgPIO_DataFlashCard(void) {
	at91_sys_write(AT91_PIOB + PIO_PER, AT91_PIO_P(7));
	at91_sys_write(AT91_PIOB + PIO_OER, AT91_PIO_P(7));
	at91_sys_write(AT91_PIOB + PIO_CODR, AT91_PIO_P(7));
}

/*
 * Enable NAND Flash (SmartMedia) interface.
 */
static inline void AT91_CfgPIO_SmartMedia(void) {
	/* enable PC0=SMCE, PC1=SMOE, PC3=SMWE, A21=CLE, A22=ALE */
	at91_sys_write(AT91_PIOC + PIO_ASR, AT91_PC0_BFCK | AT91_PC1_BFRDY_SMOE | AT91_PC3_BFBAA_SMWE);
	at91_sys_write(AT91_PIOC + PIO_PDR, AT91_PC0_BFCK | AT91_PC1_BFRDY_SMOE | AT91_PC3_BFBAA_SMWE);

	/* Configure PC2 as input (signal READY of the SmartMedia) */
	at91_sys_write(AT91_PIOC + PIO_PER, AT91_PC2_BFAVD);	/* enable direct output enable */
	at91_sys_write(AT91_PIOC + PIO_ODR, AT91_PC2_BFAVD);	/* disable output */

	/* Configure PB1 as input (signal Card Detect of the SmartMedia) */
	at91_sys_write(AT91_PIOB + PIO_PER, AT91_PIO_P(1));	/* enable direct output enable */
	at91_sys_write(AT91_PIOB + PIO_ODR, AT91_PIO_P(1));	/* disable output */
}

static inline int AT91_PIO_SmartMedia_RDY(void) {
	return (at91_sys_read(AT91_PIOC + PIO_PDSR) & AT91_PIO_P(2)) ? 1 : 0;
}

static inline int AT91_PIO_SmartMedia_CardDetect(void) {
	return (at91_sys_read(AT91_PIOB + PIO_PDSR) & AT91_PIO_P(1)) ? 1 : 0;
}

#endif
