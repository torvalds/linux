/*
 * linux/include/asm-mips/tx4938/spi.h
 * Definitions for TX4937/TX4938 SPI
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#ifndef __ASM_TX_BOARDS_TX4938_SPI_H
#define __ASM_TX_BOARDS_TX4938_SPI_H

/* SPI */
struct spi_dev_desc {
	unsigned int baud;
	unsigned short tcss, tcsh, tcsr; /* CS setup/hold/recovery time */
	unsigned int byteorder:1;	/* 0:LSB-First, 1:MSB-First */
	unsigned int polarity:1;	/* 0:High-Active */
	unsigned int phase:1;		/* 0:Sample-Then-Shift */
};

extern void txx9_spi_init(unsigned long base, int (*cs_func)(int chipid, int on)) __init;
extern void txx9_spi_irqinit(int irc_irq) __init;
extern int txx9_spi_io(int chipid, struct spi_dev_desc *desc,
		       unsigned char **inbufs, unsigned int *incounts,
		       unsigned char **outbufs, unsigned int *outcounts,
		       int cansleep);
extern int spi_eeprom_write_enable(int chipid, int enable);
extern int spi_eeprom_read_status(int chipid);
extern int spi_eeprom_read(int chipid, int address, unsigned char *buf, int len);
extern int spi_eeprom_write(int chipid, int address, unsigned char *buf, int len);
extern void spi_eeprom_proc_create(struct proc_dir_entry *dir, int chipid) __init;

#define TXX9_IMCLK     (txx9_gbus_clock / 2)

/*
* SPI
*/

/* SPMCR : SPI Master Control */
#define TXx9_SPMCR_OPMODE	0xc0
#define TXx9_SPMCR_CONFIG	0x40
#define TXx9_SPMCR_ACTIVE	0x80
#define TXx9_SPMCR_SPSTP	0x02
#define TXx9_SPMCR_BCLR	0x01

/* SPCR0 : SPI Status */
#define TXx9_SPCR0_TXIFL_MASK	0xc000
#define TXx9_SPCR0_RXIFL_MASK	0x3000
#define TXx9_SPCR0_SIDIE	0x0800
#define TXx9_SPCR0_SOEIE	0x0400
#define TXx9_SPCR0_RBSIE	0x0200
#define TXx9_SPCR0_TBSIE	0x0100
#define TXx9_SPCR0_IFSPSE	0x0010
#define TXx9_SPCR0_SBOS	0x0004
#define TXx9_SPCR0_SPHA	0x0002
#define TXx9_SPCR0_SPOL	0x0001

/* SPSR : SPI Status */
#define TXx9_SPSR_TBSI	0x8000
#define TXx9_SPSR_RBSI	0x4000
#define TXx9_SPSR_TBS_MASK	0x3800
#define TXx9_SPSR_RBS_MASK	0x0700
#define TXx9_SPSR_SPOE	0x0080
#define TXx9_SPSR_IFSD	0x0008
#define TXx9_SPSR_SIDLE	0x0004
#define TXx9_SPSR_STRDY	0x0002
#define TXx9_SPSR_SRRDY	0x0001

#endif /* __ASM_TX_BOARDS_TX4938_SPI_H */
