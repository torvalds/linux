/*
 * include/asm-ppc/mpc85xx.h
 *
 * MPC85xx definitions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2004 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef __ASM_MPC85xx_H__
#define __ASM_MPC85xx_H__

#include <linux/config.h>
#include <asm/mmu.h>

#ifdef CONFIG_85xx

#ifdef CONFIG_MPC8540_ADS
#include <platforms/85xx/mpc8540_ads.h>
#endif
#if defined(CONFIG_MPC8555_CDS) || defined(CONFIG_MPC8548_CDS)
#include <platforms/85xx/mpc8555_cds.h>
#endif
#ifdef CONFIG_MPC8560_ADS
#include <platforms/85xx/mpc8560_ads.h>
#endif
#ifdef CONFIG_SBC8560
#include <platforms/85xx/sbc8560.h>
#endif
#ifdef CONFIG_STX_GP3
#include <platforms/85xx/stx_gp3.h>
#endif

#define _IO_BASE        isa_io_base
#define _ISA_MEM_BASE   isa_mem_base
#ifdef CONFIG_PCI
#define PCI_DRAM_OFFSET pci_dram_offset
#else
#define PCI_DRAM_OFFSET 0
#endif

/*
 * The "residual" board information structure the boot loader passes
 * into the kernel.
 */
extern unsigned char __res[];

/* Offset from CCSRBAR */
#define MPC85xx_CPM_OFFSET	(0x80000)
#define MPC85xx_CPM_SIZE	(0x40000)
#define MPC85xx_DMA_OFFSET	(0x21000)
#define MPC85xx_DMA_SIZE	(0x01000)
#define MPC85xx_DMA0_OFFSET	(0x21100)
#define MPC85xx_DMA0_SIZE	(0x00080)
#define MPC85xx_DMA1_OFFSET	(0x21180)
#define MPC85xx_DMA1_SIZE	(0x00080)
#define MPC85xx_DMA2_OFFSET	(0x21200)
#define MPC85xx_DMA2_SIZE	(0x00080)
#define MPC85xx_DMA3_OFFSET	(0x21280)
#define MPC85xx_DMA3_SIZE	(0x00080)
#define MPC85xx_ENET1_OFFSET	(0x24000)
#define MPC85xx_ENET1_SIZE	(0x01000)
#define MPC85xx_MIIM_OFFSET	(0x24520)
#define MPC85xx_MIIM_SIZE	(0x00018)
#define MPC85xx_ENET2_OFFSET	(0x25000)
#define MPC85xx_ENET2_SIZE	(0x01000)
#define MPC85xx_ENET3_OFFSET	(0x26000)
#define MPC85xx_ENET3_SIZE	(0x01000)
#define MPC85xx_GUTS_OFFSET	(0xe0000)
#define MPC85xx_GUTS_SIZE	(0x01000)
#define MPC85xx_IIC1_OFFSET	(0x03000)
#define MPC85xx_IIC1_SIZE	(0x00100)
#define MPC85xx_OPENPIC_OFFSET	(0x40000)
#define MPC85xx_OPENPIC_SIZE	(0x40000)
#define MPC85xx_PCI1_OFFSET	(0x08000)
#define MPC85xx_PCI1_SIZE	(0x01000)
#define MPC85xx_PCI2_OFFSET	(0x09000)
#define MPC85xx_PCI2_SIZE	(0x01000)
#define MPC85xx_PERFMON_OFFSET	(0xe1000)
#define MPC85xx_PERFMON_SIZE	(0x01000)
#define MPC85xx_SEC2_OFFSET	(0x30000)
#define MPC85xx_SEC2_SIZE	(0x10000)
#define MPC85xx_UART0_OFFSET	(0x04500)
#define MPC85xx_UART0_SIZE	(0x00100)
#define MPC85xx_UART1_OFFSET	(0x04600)
#define MPC85xx_UART1_SIZE	(0x00100)

#define MPC85xx_CCSRBAR_SIZE	(1024*1024)

/* Let modules/drivers get at CCSRBAR */
extern phys_addr_t get_ccsrbar(void);

#ifdef MODULE
#define CCSRBAR get_ccsrbar()
#else
#define CCSRBAR BOARD_CCSRBAR
#endif

enum ppc_sys_devices {
	MPC85xx_TSEC1,
	MPC85xx_TSEC2,
	MPC85xx_FEC,
	MPC85xx_IIC1,
	MPC85xx_DMA0,
	MPC85xx_DMA1,
	MPC85xx_DMA2,
	MPC85xx_DMA3,
	MPC85xx_DUART,
	MPC85xx_PERFMON,
	MPC85xx_SEC2,
	MPC85xx_CPM_SPI,
	MPC85xx_CPM_I2C,
	MPC85xx_CPM_USB,
	MPC85xx_CPM_SCC1,
	MPC85xx_CPM_SCC2,
	MPC85xx_CPM_SCC3,
	MPC85xx_CPM_SCC4,
	MPC85xx_CPM_FCC1,
	MPC85xx_CPM_FCC2,
	MPC85xx_CPM_FCC3,
	MPC85xx_CPM_MCC1,
	MPC85xx_CPM_MCC2,
	MPC85xx_CPM_SMC1,
	MPC85xx_CPM_SMC2,
	MPC85xx_eTSEC1,
	MPC85xx_eTSEC2,
	MPC85xx_eTSEC3,
	MPC85xx_eTSEC4,
	MPC85xx_IIC2,
	MPC85xx_MDIO,
};

/* Internal interrupts are all Level Sensitive, and Positive Polarity */
#define MPC85XX_INTERNAL_IRQ_SENSES \
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  0 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  1 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  2 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  3 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  4 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  5 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  6 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  7 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  8 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  9 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 10 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 11 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 12 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 13 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 14 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 15 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 16 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 17 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 18 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 19 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 20 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 21 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 22 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 23 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 24 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 25 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 26 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 27 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 28 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 29 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 30 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 31 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 32 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 33 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 34 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 35 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 36 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 37 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 38 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 39 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 40 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 41 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 42 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 43 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 44 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 45 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 46 */	\
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE)	/* Internal 47 */

#endif /* CONFIG_85xx */
#endif /* __ASM_MPC85xx_H__ */
#endif /* __KERNEL__ */
