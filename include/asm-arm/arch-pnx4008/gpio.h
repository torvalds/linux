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

/* Start Enable Pin Interrupts - table 58 page 66 */

#define SE_PIN_BASE_INT   32

#define SE_U7_RX_INT            63
#define SE_U7_HCTS_INT          62
#define SE_BT_CLKREQ_INT        61
#define SE_U6_IRRX_INT          60
/*59 unused*/
#define SE_U5_RX_INT            58
#define SE_GPI_11_INT           57
#define SE_U3_RX_INT            56
#define SE_U2_HCTS_INT          55
#define SE_U2_RX_INT            54
#define SE_U1_RX_INT            53
#define SE_DISP_SYNC_INT        52
/*51 unused*/
#define SE_SDIO_INT_N           50
#define SE_MSDIO_START_INT      49
#define SE_GPI_06_INT           48
#define SE_GPI_05_INT           47
#define SE_GPI_04_INT           46
#define SE_GPI_03_INT           45
#define SE_GPI_02_INT           44
#define SE_GPI_01_INT           43
#define SE_GPI_00_INT           42
#define SE_SYSCLKEN_PIN_INT     41
#define SE_SPI1_DATAIN_INT      40
#define SE_GPI_07_INT           39
#define SE_SPI2_DATAIN_INT      38
#define SE_GPI_10_INT           37
#define SE_GPI_09_INT           36
#define SE_GPI_08_INT           35
/*34-32 unused*/

/* Start Enable Internal Interrupts - table 57 page 65 */

#define SE_INT_BASE_INT   0

#define SE_TS_IRQ               31
#define SE_TS_P_INT             30
#define SE_TS_AUX_INT           29
/*27-28 unused*/
#define SE_USB_AHB_NEED_CLK_INT 26
#define SE_MSTIMER_INT          25
#define SE_RTC_INT              24
#define SE_USB_NEED_CLK_INT     23
#define SE_USB_INT              22
#define SE_USB_I2C_INT          21
#define SE_USB_OTG_TIMER_INT    20
#define SE_USB_OTG_ATX_INT_N    19
/*18 unused*/
#define SE_DSP_GPIO4_INT        17
#define SE_KEY_IRQ              16
#define SE_DSP_SLAVEPORT_INT    15
#define SE_DSP_GPIO1_INT        14
#define SE_DSP_GPIO0_INT        13
#define SE_DSP_AHB_INT          12
/*11-6 unused*/
#define SE_GPIO_05_INT          5
#define SE_GPIO_04_INT          4
#define SE_GPIO_03_INT          3
#define SE_GPIO_02_INT          2
#define SE_GPIO_01_INT          1
#define SE_GPIO_00_INT          0

#define START_INT_REG_BIT(irq) (1<<((irq)&0x1F))

#define START_INT_ER_REG(irq)     IO_ADDRESS((PNX4008_PWRMAN_BASE + 0x20 + (((irq)&(0x1<<5))>>1)))
#define START_INT_RSR_REG(irq)    IO_ADDRESS((PNX4008_PWRMAN_BASE + 0x24 + (((irq)&(0x1<<5))>>1)))
#define START_INT_SR_REG(irq)     IO_ADDRESS((PNX4008_PWRMAN_BASE + 0x28 + (((irq)&(0x1<<5))>>1)))
#define START_INT_APR_REG(irq)    IO_ADDRESS((PNX4008_PWRMAN_BASE + 0x2C + (((irq)&(0x1<<5))>>1)))

extern int pnx4008_gpio_register_pin(unsigned short pin);
extern int pnx4008_gpio_unregister_pin(unsigned short pin);
extern unsigned long pnx4008_gpio_read_pin(unsigned short pin);
extern int pnx4008_gpio_write_pin(unsigned short pin, int output);
extern int pnx4008_gpio_set_pin_direction(unsigned short pin, int output);
extern int pnx4008_gpio_read_pin_direction(unsigned short pin);
extern int pnx4008_gpio_set_pin_mux(unsigned short pin, int output);
extern int pnx4008_gpio_read_pin_mux(unsigned short pin);

static inline void start_int_umask(u8 irq)
{
	__raw_writel(__raw_readl(START_INT_ER_REG(irq)) |
		     START_INT_REG_BIT(irq), START_INT_ER_REG(irq));
}

static inline void start_int_mask(u8 irq)
{
	__raw_writel(__raw_readl(START_INT_ER_REG(irq)) &
		     ~START_INT_REG_BIT(irq), START_INT_ER_REG(irq));
}

static inline void start_int_ack(u8 irq)
{
	__raw_writel(START_INT_REG_BIT(irq), START_INT_RSR_REG(irq));
}

static inline void start_int_set_falling_edge(u8 irq)
{
	__raw_writel(__raw_readl(START_INT_APR_REG(irq)) &
		     ~START_INT_REG_BIT(irq), START_INT_APR_REG(irq));
}

static inline void start_int_set_rising_edge(u8 irq)
{
	__raw_writel(__raw_readl(START_INT_APR_REG(irq)) |
		     START_INT_REG_BIT(irq), START_INT_APR_REG(irq));
}

#endif				/* _PNX4008_GPIO_H_ */
