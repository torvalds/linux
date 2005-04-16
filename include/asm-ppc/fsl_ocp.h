/*
 * include/asm-ppc/fsl_ocp.h
 *
 * Definitions for the on-chip peripherals on Freescale PPC processors
 *
 * Maintainer: Kumar Gala (kumar.gala@freescale.com)
 *
 * Copyright 2004 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef __ASM_FS_OCP_H__
#define __ASM_FS_OCP_H__

/* A table of information for supporting the Gianfar Ethernet Controller
 * This helps identify which enet controller we are dealing with,
 * and what type of enet controller it is
 */
struct ocp_gfar_data {
	uint interruptTransmit;
	uint interruptError;
	uint interruptReceive;
	uint interruptPHY;
	uint flags;
	uint phyid;
	uint phyregidx;
	unsigned char mac_addr[6];
};

/* Flags in the flags field */
#define GFAR_HAS_COALESCE		0x20
#define GFAR_HAS_RMON			0x10
#define GFAR_HAS_MULTI_INTR		0x08
#define GFAR_FIRM_SET_MACADDR		0x04
#define GFAR_HAS_PHY_INTR		0x02	/* if not set use a timer */
#define GFAR_HAS_GIGABIT		0x01

/* Data structure for I2C support.  Just contains a couple flags
 * to distinguish various I2C implementations*/
struct ocp_fs_i2c_data {
	uint flags;
};

/* Flags for I2C */
#define FS_I2C_SEPARATE_DFSRR	0x02
#define FS_I2C_CLOCK_5200	0x01

#endif	/* __ASM_FS_OCP_H__ */
#endif	/* __KERNEL__ */
