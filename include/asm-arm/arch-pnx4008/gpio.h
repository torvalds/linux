/*
 * include/asm-arm/arch-pnx4008/gpio.h
 *
 * PNX4008 GPIO driver - header file
 *
 * Author: Dmitry Chigirev <source@mvista.com>
 *
 * Based on reference code by Iwo Mergler and Z.Tabaaloute from Philips:
 * Copyright (c) 2005 Koninklijke Philips Electronics N.V.
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef _PNX4008_GPIO_H_
#define _PNX4008_GPIO_H_


/* Block numbers */
#define GPIO_IN		(0)
#define GPIO_OUT		(0x100)
#define GPIO_BID		(0x200)
#define GPIO_RAM		(0x300)
#define GPIO_MUX		(0x400)

#define GPIO_TYPE_MASK(K) ((K) & 0x700)

/* INPUT GPIOs */
/* GPI */
#define GPI_00		(GPIO_IN | 0)
#define GPI_01		(GPIO_IN | 1)
#define GPI_02   	(GPIO_IN | 2)
#define GPI_03 	 	(GPIO_IN | 3)
#define GPI_04   	(GPIO_IN | 4)
#define GPI_05   	(GPIO_IN | 5)
#define GPI_06   	(GPIO_IN | 6)
#define GPI_07   	(GPIO_IN | 7)
#define GPI_08   	(GPIO_IN | 8)
#define GPI_09   	(GPIO_IN | 9)
#define U1_RX 		(GPIO_IN | 15)
#define U2_HTCS 	(GPIO_IN | 16)
#define U2_RX	 	(GPIO_IN | 17)
#define U3_RX		(GPIO_IN | 18)
#define U4_RX		(GPIO_IN | 19)
#define U5_RX		(GPIO_IN | 20)
#define U6_IRRX 	(GPIO_IN | 21)
#define U7_HCTS 	(GPIO_IN | 22)
#define U7_RX		(GPIO_IN | 23)
/* MISC IN */
#define SPI1_DATIN	(GPIO_IN | 25)
#define DISP_SYNC	(GPIO_IN | 26)
#define SPI2_DATIN	(GPIO_IN | 27)
#define GPI_11  	(GPIO_IN | 28)

#define GPIO_IN_MASK   0x1eff83ff

/* OUTPUT GPIOs */
/* GPO */
#define GPO_00		(GPIO_OUT | 0)
#define GPO_01   	(GPIO_OUT | 1)
#define GPO_02   	(GPIO_OUT | 2)
#define GPO_03 	 	(GPIO_OUT | 3)
#define GPO_04   	(GPIO_OUT | 4)
#define GPO_05   	(GPIO_OUT | 5)
#define GPO_06   	(GPIO_OUT | 6)
#define GPO_07   	(GPIO_OUT | 7)
#define GPO_08		(GPIO_OUT | 8)
#define GPO_09   	(GPIO_OUT | 9)
#define GPO_10   	(GPIO_OUT | 10)
#define GPO_11 	 	(GPIO_OUT | 11)
#define GPO_12   	(GPIO_OUT | 12)
#define GPO_13   	(GPIO_OUT | 13)
#define GPO_14   	(GPIO_OUT | 14)
#define GPO_15   	(GPIO_OUT | 15)
#define GPO_16  	(GPIO_OUT | 16)
#define GPO_17 	 	(GPIO_OUT | 17)
#define GPO_18   	(GPIO_OUT | 18)
#define GPO_19   	(GPIO_OUT | 19)
#define GPO_20   	(GPIO_OUT | 20)
#define GPO_21   	(GPIO_OUT | 21)
#define GPO_22   	(GPIO_OUT | 22)
#define GPO_23   	(GPIO_OUT | 23)

#define GPIO_OUT_MASK   0xffffff

/* BIDIRECTIONAL GPIOs */
/* RAM pins */
#define RAM_D19		(GPIO_RAM | 0)
#define RAM_D20  	(GPIO_RAM | 1)
#define RAM_D21  	(GPIO_RAM | 2)
#define RAM_D22 	(GPIO_RAM | 3)
#define RAM_D23  	(GPIO_RAM | 4)
#define RAM_D24  	(GPIO_RAM | 5)
#define RAM_D25  	(GPIO_RAM | 6)
#define RAM_D26  	(GPIO_RAM | 7)
#define RAM_D27		(GPIO_RAM | 8)
#define RAM_D28  	(GPIO_RAM | 9)
#define RAM_D29  	(GPIO_RAM | 10)
#define RAM_D30 	(GPIO_RAM | 11)
#define RAM_D31  	(GPIO_RAM | 12)

#define GPIO_RAM_MASK   0x1fff

/* I/O pins */
#define GPIO_00  	(GPIO_BID | 25)
#define GPIO_01 	(GPIO_BID | 26)
#define GPIO_02  	(GPIO_BID | 27)
#define GPIO_03  	(GPIO_BID | 28)
#define GPIO_04 	(GPIO_BID | 29)
#define GPIO_05  	(GPIO_BID | 30)

#define GPIO_BID_MASK   0x7e000000

/* Non-GPIO multiplexed PIOs. For multiplexing with GPIO, please use GPIO macros */
#define GPIO_SDRAM_SEL 	(GPIO_MUX | 3)

#define GPIO_MUX_MASK   0x8

/* Extraction/assembly macros */
#define GPIO_BIT_MASK(K) ((K) & 0x1F)
#define GPIO_BIT(K) (1 << GPIO_BIT_MASK(K))
#define GPIO_ISMUX(K) ((GPIO_TYPE_MASK(K) == GPIO_MUX) && (GPIO_BIT(K) & GPIO_MUX_MASK))
#define GPIO_ISRAM(K) ((GPIO_TYPE_MASK(K) == GPIO_RAM) && (GPIO_BIT(K) & GPIO_RAM_MASK))
#define GPIO_ISBID(K) ((GPIO_TYPE_MASK(K) == GPIO_BID) && (GPIO_BIT(K) & GPIO_BID_MASK))
#define GPIO_ISOUT(K) ((GPIO_TYPE_MASK(K) == GPIO_OUT) && (GPIO_BIT(K) & GPIO_OUT_MASK))
#define GPIO_ISIN(K)  ((GPIO_TYPE_MASK(K) == GPIO_IN) && (GPIO_BIT(K) & GPIO_IN_MASK))

extern int pnx4008_gpio_register_pin(unsigned short pin);
extern int pnx4008_gpio_unregister_pin(unsigned short pin);
extern unsigned long pnx4008_gpio_read_pin(unsigned short pin);
extern int pnx4008_gpio_write_pin(unsigned short pin, int output);
extern int pnx4008_gpio_set_pin_direction(unsigned short pin, int output);
extern int pnx4008_gpio_read_pin_direction(unsigned short pin);
extern int pnx4008_gpio_set_pin_mux(unsigned short pin, int output);
extern int pnx4008_gpio_read_pin_mux(unsigned short pin);

#endif				/* _PNX4008_GPIO_H_ */
