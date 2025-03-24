/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Internal header file for Samsung S3C2410 serial ports (UART0-2)
 *
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *
 *  Additional defines, Copyright 2003 Simtec Electronics (linux@simtec.co.uk)
 *
 *  Adapted from:
 *
 *  Internal header file for MX1ADS serial ports (UART1 & 2)
 *
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 */

#ifndef __ASM_ARM_REGS_SERIAL_H
#define __ASM_ARM_REGS_SERIAL_H

#define S3C2410_URXH	  (0x24)
#define S3C2410_UTXH	  (0x20)
#define S3C2410_ULCON	  (0x00)
#define S3C2410_UCON	  (0x04)
#define S3C2410_UFCON	  (0x08)
#define S3C2410_UMCON	  (0x0C)
#define S3C2410_UBRDIV	  (0x28)
#define S3C2410_UTRSTAT	  (0x10)
#define S3C2410_UERSTAT	  (0x14)
#define S3C2410_UFSTAT	  (0x18)
#define S3C2410_UMSTAT	  (0x1C)

#define S3C2410_LCON_CFGMASK	  ((0xF<<3)|(0x3))

#define S3C2410_LCON_CS5	  (0x0)
#define S3C2410_LCON_CS6	  (0x1)
#define S3C2410_LCON_CS7	  (0x2)
#define S3C2410_LCON_CS8	  (0x3)
#define S3C2410_LCON_CSMASK	  (0x3)

#define S3C2410_LCON_PNONE	  (0x0)
#define S3C2410_LCON_PEVEN	  (0x5 << 3)
#define S3C2410_LCON_PODD	  (0x4 << 3)
#define S3C2410_LCON_PMASK	  (0x7 << 3)

#define S3C2410_LCON_STOPB	  (1<<2)
#define S3C2410_LCON_IRM          (1<<6)

#define S3C2440_UCON_CLKMASK	  (3<<10)
#define S3C2440_UCON_CLKSHIFT	  (10)
#define S3C2440_UCON_PCLK	  (0<<10)
#define S3C2440_UCON_UCLK	  (1<<10)
#define S3C2440_UCON_PCLK2	  (2<<10)
#define S3C2440_UCON_FCLK	  (3<<10)
#define S3C2443_UCON_EPLL	  (3<<10)

#define S3C6400_UCON_CLKMASK	(3<<10)
#define S3C6400_UCON_CLKSHIFT	(10)
#define S3C6400_UCON_PCLK	(0<<10)
#define S3C6400_UCON_PCLK2	(2<<10)
#define S3C6400_UCON_UCLK0	(1<<10)
#define S3C6400_UCON_UCLK1	(3<<10)

#define S3C2440_UCON2_FCLK_EN	  (1<<15)
#define S3C2440_UCON0_DIVMASK	  (15 << 12)
#define S3C2440_UCON1_DIVMASK	  (15 << 12)
#define S3C2440_UCON2_DIVMASK	  (7 << 12)
#define S3C2440_UCON_DIVSHIFT	  (12)

#define S3C2412_UCON_CLKMASK	(3<<10)
#define S3C2412_UCON_CLKSHIFT	(10)
#define S3C2412_UCON_UCLK	(1<<10)
#define S3C2412_UCON_USYSCLK	(3<<10)
#define S3C2412_UCON_PCLK	(0<<10)
#define S3C2412_UCON_PCLK2	(2<<10)

#define S3C2410_UCON_CLKMASK	(1 << 10)
#define S3C2410_UCON_CLKSHIFT	(10)
#define S3C2410_UCON_UCLK	  (1<<10)
#define S3C2410_UCON_SBREAK	  (1<<4)

#define S3C2410_UCON_TXILEVEL	  (1<<9)
#define S3C2410_UCON_RXILEVEL	  (1<<8)
#define S3C2410_UCON_TXIRQMODE	  (1<<2)
#define S3C2410_UCON_RXIRQMODE	  (1<<0)
#define S3C2410_UCON_RXFIFO_TOI	  (1<<7)
#define S3C2443_UCON_RXERR_IRQEN  (1<<6)
#define S3C2410_UCON_LOOPBACK	  (1<<5)

#define S3C2410_UCON_DEFAULT	  (S3C2410_UCON_TXILEVEL  | \
				   S3C2410_UCON_RXILEVEL  | \
				   S3C2410_UCON_TXIRQMODE | \
				   S3C2410_UCON_RXIRQMODE | \
				   S3C2410_UCON_RXFIFO_TOI)

#define S3C64XX_UCON_TXBURST_1          (0<<20)
#define S3C64XX_UCON_TXBURST_4          (1<<20)
#define S3C64XX_UCON_TXBURST_8          (2<<20)
#define S3C64XX_UCON_TXBURST_16         (3<<20)
#define S3C64XX_UCON_TXBURST_MASK       (0xf<<20)
#define S3C64XX_UCON_RXBURST_1          (0<<16)
#define S3C64XX_UCON_RXBURST_4          (1<<16)
#define S3C64XX_UCON_RXBURST_8          (2<<16)
#define S3C64XX_UCON_RXBURST_16         (3<<16)
#define S3C64XX_UCON_RXBURST_MASK       (0xf<<16)
#define S3C64XX_UCON_TIMEOUT_SHIFT      (12)
#define S3C64XX_UCON_TIMEOUT_MASK       (0xf<<12)
#define S3C64XX_UCON_EMPTYINT_EN        (1<<11)
#define S3C64XX_UCON_DMASUS_EN          (1<<10)
#define S3C64XX_UCON_TXINT_LEVEL        (1<<9)
#define S3C64XX_UCON_RXINT_LEVEL        (1<<8)
#define S3C64XX_UCON_TIMEOUT_EN         (1<<7)
#define S3C64XX_UCON_ERRINT_EN          (1<<6)
#define S3C64XX_UCON_TXMODE_DMA         (2<<2)
#define S3C64XX_UCON_TXMODE_CPU         (1<<2)
#define S3C64XX_UCON_TXMODE_MASK        (3<<2)
#define S3C64XX_UCON_RXMODE_DMA         (2<<0)
#define S3C64XX_UCON_RXMODE_CPU         (1<<0)
#define S3C64XX_UCON_RXMODE_MASK        (3<<0)

#define S3C2410_UFCON_FIFOMODE	  (1<<0)
#define S3C2410_UFCON_TXTRIG0	  (0<<6)
#define S3C2410_UFCON_RXTRIG8	  (1<<4)
#define S3C2410_UFCON_RXTRIG12	  (2<<4)

/* S3C2440 FIFO trigger levels */
#define S3C2440_UFCON_RXTRIG1	  (0<<4)
#define S3C2440_UFCON_RXTRIG8	  (1<<4)
#define S3C2440_UFCON_RXTRIG16	  (2<<4)
#define S3C2440_UFCON_RXTRIG32	  (3<<4)

#define S3C2440_UFCON_TXTRIG0	  (0<<6)
#define S3C2440_UFCON_TXTRIG16	  (1<<6)
#define S3C2440_UFCON_TXTRIG32	  (2<<6)
#define S3C2440_UFCON_TXTRIG48	  (3<<6)

#define S3C2410_UFCON_RESETBOTH	  (3<<1)
#define S3C2410_UFCON_RESETTX	  (1<<2)
#define S3C2410_UFCON_RESETRX	  (1<<1)

#define S3C2410_UFCON_DEFAULT	  (S3C2410_UFCON_FIFOMODE | \
				   S3C2410_UFCON_TXTRIG0  | \
				   S3C2410_UFCON_RXTRIG8 )

#define	S3C2410_UMCOM_AFC	  (1<<4)
#define	S3C2410_UMCOM_RTS_LOW	  (1<<0)

#define S3C2412_UMCON_AFC_63	(0<<5)		/* same as s3c2443 */
#define S3C2412_UMCON_AFC_56	(1<<5)
#define S3C2412_UMCON_AFC_48	(2<<5)
#define S3C2412_UMCON_AFC_40	(3<<5)
#define S3C2412_UMCON_AFC_32	(4<<5)
#define S3C2412_UMCON_AFC_24	(5<<5)
#define S3C2412_UMCON_AFC_16	(6<<5)
#define S3C2412_UMCON_AFC_8	(7<<5)

#define S3C2410_UFSTAT_TXFULL	  (1<<9)
#define S3C2410_UFSTAT_RXFULL	  (1<<8)
#define S3C2410_UFSTAT_TXMASK	  (15<<4)
#define S3C2410_UFSTAT_TXSHIFT	  (4)
#define S3C2410_UFSTAT_RXMASK	  (15<<0)
#define S3C2410_UFSTAT_RXSHIFT	  (0)

/* UFSTAT S3C2443 same as S3C2440 */
#define S3C2440_UFSTAT_TXFULL	  (1<<14)
#define S3C2440_UFSTAT_RXFULL	  (1<<6)
#define S3C2440_UFSTAT_TXSHIFT	  (8)
#define S3C2440_UFSTAT_RXSHIFT	  (0)
#define S3C2440_UFSTAT_TXMASK	  (63<<8)
#define S3C2440_UFSTAT_RXMASK	  (63)

#define S3C2410_UTRSTAT_TIMEOUT   (1<<3)
#define S3C2410_UTRSTAT_TXE	  (1<<2)
#define S3C2410_UTRSTAT_TXFE	  (1<<1)
#define S3C2410_UTRSTAT_RXDR	  (1<<0)

#define S3C2410_UERSTAT_OVERRUN	  (1<<0)
#define S3C2410_UERSTAT_FRAME	  (1<<2)
#define S3C2410_UERSTAT_BREAK	  (1<<3)
#define S3C2443_UERSTAT_PARITY	  (1<<1)

#define S3C2410_UERSTAT_ANY	  (S3C2410_UERSTAT_OVERRUN | \
				   S3C2410_UERSTAT_FRAME | \
				   S3C2410_UERSTAT_BREAK)

#define S3C2410_UMSTAT_CTS	  (1<<0)
#define S3C2410_UMSTAT_DeltaCTS	  (1<<2)

#define S3C2443_DIVSLOT		  (0x2C)

/* S3C64XX interrupt registers. */
#define S3C64XX_UINTP		0x30
#define S3C64XX_UINTSP		0x34
#define S3C64XX_UINTM		0x38

#define S3C64XX_UINTM_RXD	(0)
#define S3C64XX_UINTM_ERROR     (1)
#define S3C64XX_UINTM_TXD	(2)
#define S3C64XX_UINTM_RXD_MSK	(1 << S3C64XX_UINTM_RXD)
#define S3C64XX_UINTM_ERR_MSK   (1 << S3C64XX_UINTM_ERROR)
#define S3C64XX_UINTM_TXD_MSK	(1 << S3C64XX_UINTM_TXD)

/* Following are specific to S5PV210 */
#define S5PV210_UCON_CLKMASK	(1<<10)
#define S5PV210_UCON_CLKSHIFT	(10)
#define S5PV210_UCON_PCLK	(0<<10)
#define S5PV210_UCON_UCLK	(1<<10)

#define S5PV210_UFCON_TXTRIG0	(0<<8)
#define S5PV210_UFCON_TXTRIG4	(1<<8)
#define S5PV210_UFCON_TXTRIG8	(2<<8)
#define S5PV210_UFCON_TXTRIG16	(3<<8)
#define S5PV210_UFCON_TXTRIG32	(4<<8)
#define S5PV210_UFCON_TXTRIG64	(5<<8)
#define S5PV210_UFCON_TXTRIG128 (6<<8)
#define S5PV210_UFCON_TXTRIG256 (7<<8)

#define S5PV210_UFCON_RXTRIG1	(0<<4)
#define S5PV210_UFCON_RXTRIG4	(1<<4)
#define S5PV210_UFCON_RXTRIG8	(2<<4)
#define S5PV210_UFCON_RXTRIG16	(3<<4)
#define S5PV210_UFCON_RXTRIG32	(4<<4)
#define S5PV210_UFCON_RXTRIG64	(5<<4)
#define S5PV210_UFCON_RXTRIG128	(6<<4)
#define S5PV210_UFCON_RXTRIG256	(7<<4)

#define S5PV210_UFSTAT_TXFULL	(1<<24)
#define S5PV210_UFSTAT_RXFULL	(1<<8)
#define S5PV210_UFSTAT_TXMASK	(255<<16)
#define S5PV210_UFSTAT_TXSHIFT	(16)
#define S5PV210_UFSTAT_RXMASK	(255<<0)
#define S5PV210_UFSTAT_RXSHIFT	(0)

#define S3C2410_UCON_CLKSEL0	(1 << 0)
#define S3C2410_UCON_CLKSEL1	(1 << 1)
#define S3C2410_UCON_CLKSEL2	(1 << 2)
#define S3C2410_UCON_CLKSEL3	(1 << 3)

/* Default values for s5pv210 UCON and UFCON uart registers */
#define S5PV210_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define S5PV210_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

#define APPLE_S5L_UCON_RXTO_ENA			9
#define APPLE_S5L_UCON_RXTO_LEGACY_ENA		11
#define APPLE_S5L_UCON_RXTHRESH_ENA		12
#define APPLE_S5L_UCON_TXTHRESH_ENA		13
#define APPLE_S5L_UCON_RXTO_ENA_MSK		BIT(APPLE_S5L_UCON_RXTO_ENA)
#define APPLE_S5L_UCON_RXTO_LEGACY_ENA_MSK	BIT(APPLE_S5L_UCON_RXTO_LEGACY_ENA)
#define APPLE_S5L_UCON_RXTHRESH_ENA_MSK		BIT(APPLE_S5L_UCON_RXTHRESH_ENA)
#define APPLE_S5L_UCON_TXTHRESH_ENA_MSK		BIT(APPLE_S5L_UCON_TXTHRESH_ENA)

#define APPLE_S5L_UCON_DEFAULT		(S3C2410_UCON_TXIRQMODE | \
					 S3C2410_UCON_RXIRQMODE | \
					 S3C2410_UCON_RXFIFO_TOI)
#define APPLE_S5L_UCON_MASK		(APPLE_S5L_UCON_RXTO_ENA_MSK | \
					 APPLE_S5L_UCON_RXTO_LEGACY_ENA_MSK | \
					 APPLE_S5L_UCON_RXTHRESH_ENA_MSK | \
					 APPLE_S5L_UCON_TXTHRESH_ENA_MSK)

#define APPLE_S5L_UTRSTAT_RXTO_LEGACY	BIT(3)
#define APPLE_S5L_UTRSTAT_RXTHRESH	BIT(4)
#define APPLE_S5L_UTRSTAT_TXTHRESH	BIT(5)
#define APPLE_S5L_UTRSTAT_RXTO		BIT(9)
#define APPLE_S5L_UTRSTAT_ALL_FLAGS	GENMASK(9, 3)

#ifndef __ASSEMBLY__

#include <linux/serial_core.h>

/* configuration structure for per-machine configurations for the
 * serial port
 *
 * the pointer is setup by the machine specific initialisation from the
 * arch/arm/mach-s3c/ directory.
*/

struct s3c2410_uartcfg {
	unsigned char	   hwport;	 /* hardware port number */
	unsigned char	   unused;
	unsigned short	   flags;
	upf_t		   uart_flags;	 /* default uart flags */
	unsigned int	   clk_sel;

	unsigned int	   has_fracval;

	unsigned long	   ucon;	 /* value of ucon for port */
	unsigned long	   ulcon;	 /* value of ulcon for port */
	unsigned long	   ufcon;	 /* value of ufcon for port */
};

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ARM_REGS_SERIAL_H */

