/*
 * Common DCR / SDR / CPR register definitions used on various IBM/AMCC
 * 4xx processors
 *
 *    Copyright 2007 Benjamin Herrenschmidt, IBM Corp
 *                   <benh@kernel.crashing.org>
 *
 * Mostly lifted from asm-ppc/ibm4xx.h by
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 */

#ifndef __DCR_REGS_H__
#define __DCR_REGS_H__

/*
 * Most DCRs used for controlling devices such as the MAL, DMA engine,
 * etc... are obtained for the device tree.
 *
 * The definitions in this files are fixed DCRs and indirect DCRs that
 * are commonly used outside of specific drivers or refer to core
 * common registers that may occasionally have to be tweaked outside
 * of the driver main register set
 */

/* CPRs (440GX and 440SP/440SPe) */
#define DCRN_CPR0_CONFIG_ADDR	0xc
#define DCRN_CPR0_CONFIG_DATA	0xd

/* SDRs (440GX and 440SP/440SPe) */
#define DCRN_SDR0_CONFIG_ADDR 	0xe
#define DCRN_SDR0_CONFIG_DATA	0xf

#define SDR0_PFC0		0x4100
#define SDR0_PFC1		0x4101
#define SDR0_PFC1_EPS		0x1c00000
#define SDR0_PFC1_EPS_SHIFT	22
#define SDR0_PFC1_RMII		0x02000000
#define SDR0_MFR		0x4300
#define SDR0_MFR_TAH0 		0x80000000  	/* TAHOE0 Enable */
#define SDR0_MFR_TAH1 		0x40000000  	/* TAHOE1 Enable */
#define SDR0_MFR_PCM  		0x10000000  	/* PPC440GP irq compat mode */
#define SDR0_MFR_ECS  		0x08000000  	/* EMAC int clk */
#define SDR0_MFR_T0TXFL		0x00080000
#define SDR0_MFR_T0TXFH		0x00040000
#define SDR0_MFR_T1TXFL		0x00020000
#define SDR0_MFR_T1TXFH		0x00010000
#define SDR0_MFR_E0TXFL		0x00008000
#define SDR0_MFR_E0TXFH		0x00004000
#define SDR0_MFR_E0RXFL		0x00002000
#define SDR0_MFR_E0RXFH		0x00001000
#define SDR0_MFR_E1TXFL		0x00000800
#define SDR0_MFR_E1TXFH		0x00000400
#define SDR0_MFR_E1RXFL		0x00000200
#define SDR0_MFR_E1RXFH		0x00000100
#define SDR0_MFR_E2TXFL		0x00000080
#define SDR0_MFR_E2TXFH		0x00000040
#define SDR0_MFR_E2RXFL		0x00000020
#define SDR0_MFR_E2RXFH		0x00000010
#define SDR0_MFR_E3TXFL		0x00000008
#define SDR0_MFR_E3TXFH		0x00000004
#define SDR0_MFR_E3RXFL		0x00000002
#define SDR0_MFR_E3RXFH		0x00000001
#define SDR0_UART0		0x0120
#define SDR0_UART1		0x0121
#define SDR0_UART2		0x0122
#define SDR0_UART3		0x0123
#define SDR0_CUST0		0x4000

#endif /* __DCR_REGS_H__ */
