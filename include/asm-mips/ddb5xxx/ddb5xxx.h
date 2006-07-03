/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                    Sony Software Development Center Europe (SDCE), Brussels
 *
 * include/asm-mips/ddb5xxx/ddb5xxx.h
 *     Common header for all NEC DDB 5xxx boards, including 5074, 5476, 5477.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __ASM_DDB5XXX_DDB5XXX_H
#define __ASM_DDB5XXX_DDB5XXX_H

#include <linux/types.h>

/*
 *  This file is based on the following documentation:
 *
 *	NEC Vrc 5074 System Controller Data Sheet, June 1998
 *
 * [jsun] It is modified so that this file only contains the macros
 * that are true for all DDB 5xxx boards.  The modification is based on
 *
 *	uPD31577(VRC5477) VR5432-SDRAM/PCI Bridge (Luke)
 *	Preliminary Specification Decoment, Rev 1.1, 27 Dec, 2000
 *
 */


#define DDB_BASE		0xbfa00000
#define DDB_SIZE		0x00200000		/* 2 MB */


/*
 *  Physical Device Address Registers (PDARs)
 */

#define DDB_SDRAM0	0x0000	/* SDRAM Bank 0 [R/W] */
#define DDB_SDRAM1	0x0008	/* SDRAM Bank 1 [R/W] */
#define DDB_DCS2	0x0010	/* Device Chip-Select 2 [R/W] */
#define DDB_DCS3	0x0018	/* Device Chip-Select 3 [R/W] */
#define DDB_DCS4	0x0020	/* Device Chip-Select 4 [R/W] */
#define DDB_DCS5	0x0028	/* Device Chip-Select 5 [R/W] */
#define DDB_DCS6	0x0030	/* Device Chip-Select 6 [R/W] */
#define DDB_DCS7	0x0038	/* Device Chip-Select 7 [R/W] */
#define DDB_DCS8	0x0040	/* Device Chip-Select 8 [R/W] */
#define DDB_PCIW0	0x0060	/* PCI Address Window 0 [R/W] */
#define DDB_PCIW1	0x0068	/* PCI Address Window 1 [R/W] */
#define DDB_INTCS	0x0070	/* Controller Internal Registers and Devices */
				/* [R/W] */
#define DDB_BOOTCS	0x0078	/* Boot ROM Chip-Select [R/W] */
/* Vrc5477 has two more, IOPCIW0, IOPCIW1 */

/*
 *  CPU Interface Registers
 */
#define DDB_CPUSTAT	0x0080	/* CPU Status [R/W] */
#define DDB_INTCTRL	0x0088	/* Interrupt Control [R/W] */
#define DDB_INTSTAT0	0x0090	/* Interrupt Status 0 [R] */
#define DDB_INTSTAT1	0x0098	/* Interrupt Status 1 and CPU Interrupt */
				/* Enable [R/W] */
#define DDB_INTCLR	0x00A0	/* Interrupt Clear [R/W] */
#define DDB_INTPPES	0x00A8	/* PCI Interrupt Control [R/W] */


/*
 *  Memory-Interface Registers
 */
#define DDB_MEMCTRL	0x00C0	/* Memory Control */
#define DDB_ACSTIME	0x00C8	/* Memory Access Timing [R/W] */
#define DDB_CHKERR	0x00D0	/* Memory Check Error Status [R] */


/*
 *  PCI-Bus Registers
 */
#define DDB_PCICTRL	0x00E0	/* PCI Control [R/W] */
#define DDB_PCIARB	0x00E8	/* PCI Arbiter [R/W] */
#define DDB_PCIINIT0	0x00F0	/* PCI Master (Initiator) 0 [R/W] */
#define DDB_PCIINIT1	0x00F8	/* PCI Master (Initiator) 1 [R/W] */
#define DDB_PCIERR	0x00B8	/* PCI Error [R/W] */


/*
 *  Local-Bus Registers
 */
#define DDB_LCNFG	0x0100	/* Local Bus Configuration [R/W] */
#define DDB_LCST2	0x0110	/* Local Bus Chip-Select Timing 2 [R/W] */
#define DDB_LCST3	0x0118	/* Local Bus Chip-Select Timing 3 [R/W] */
#define DDB_LCST4	0x0120	/* Local Bus Chip-Select Timing 4 [R/W] */
#define DDB_LCST5	0x0128	/* Local Bus Chip-Select Timing 5 [R/W] */
#define DDB_LCST6	0x0130	/* Local Bus Chip-Select Timing 6 [R/W] */
#define DDB_LCST7	0x0138	/* Local Bus Chip-Select Timing 7 [R/W] */
#define DDB_LCST8	0x0140	/* Local Bus Chip-Select Timing 8 [R/W] */
#define DDB_DCSFN	0x0150	/* Device Chip-Select Muxing and Output */
				/* Enables [R/W] */
#define DDB_DCSIO	0x0158	/* Device Chip-Selects As I/O Bits [R/W] */
#define DDB_BCST	0x0178	/* Local Boot Chip-Select Timing [R/W] */


/*
 *  DMA Registers
 */
#define DDB_DMACTRL0	0x0180	/* DMA Control 0 [R/W] */
#define DDB_DMASRCA0	0x0188	/* DMA Source Address 0 [R/W] */
#define DDB_DMADESA0	0x0190	/* DMA Destination Address 0 [R/W] */
#define DDB_DMACTRL1	0x0198	/* DMA Control 1 [R/W] */
#define DDB_DMASRCA1	0x01A0	/* DMA Source Address 1 [R/W] */
#define DDB_DMADESA1	0x01A8	/* DMA Destination Address 1 [R/W] */


/*
 *  Timer Registers
 */
#define DDB_T0CTRL	0x01C0	/* SDRAM Refresh Control [R/W] */
#define DDB_T0CNTR	0x01C8	/* SDRAM Refresh Counter [R/W] */
#define DDB_T1CTRL	0x01D0	/* CPU-Bus Read Time-Out Control [R/W] */
#define DDB_T1CNTR	0x01D8	/* CPU-Bus Read Time-Out Counter [R/W] */
#define DDB_T2CTRL	0x01E0	/* General-Purpose Timer Control [R/W] */
#define DDB_T2CNTR	0x01E8	/* General-Purpose Timer Counter [R/W] */
#define DDB_T3CTRL	0x01F0	/* Watchdog Timer Control [R/W] */
#define DDB_T3CNTR	0x01F8	/* Watchdog Timer Counter [R/W] */


/*
 *  PCI Configuration Space Registers
 */
#define DDB_PCI_BASE	0x0200

#define DDB_VID		0x0200	/* PCI Vendor ID [R] */
#define DDB_DID		0x0202	/* PCI Device ID [R] */
#define DDB_PCICMD	0x0204	/* PCI Command [R/W] */
#define DDB_PCISTS	0x0206	/* PCI Status [R/W] */
#define DDB_REVID	0x0208	/* PCI Revision ID [R] */
#define DDB_CLASS	0x0209	/* PCI Class Code [R] */
#define DDB_CLSIZ	0x020C	/* PCI Cache Line Size [R/W] */
#define DDB_MLTIM	0x020D	/* PCI Latency Timer [R/W] */
#define DDB_HTYPE	0x020E	/* PCI Header Type [R] */
#define DDB_BIST	0x020F	/* BIST [R] (unimplemented) */
#define DDB_BARC	0x0210	/* PCI Base Address Register Control [R/W] */
#define DDB_BAR0	0x0218	/* PCI Base Address Register 0 [R/W] */
#define DDB_BAR1	0x0220	/* PCI Base Address Register 1 [R/W] */
#define DDB_CIS		0x0228	/* PCI Cardbus CIS Pointer [R] */
				/* (unimplemented) */
#define DDB_SSVID	0x022C	/* PCI Sub-System Vendor ID [R/W] */
#define DDB_SSID	0x022E	/* PCI Sub-System ID [R/W] */
#define DDB_ROM		0x0230	/* Expansion ROM Base Address [R] */
				/* (unimplemented) */
#define DDB_INTLIN	0x023C	/* PCI Interrupt Line [R/W] */
#define DDB_INTPIN	0x023D	/* PCI Interrupt Pin [R] */
#define DDB_MINGNT	0x023E	/* PCI Min_Gnt [R] (unimplemented) */
#define DDB_MAXLAT	0x023F	/* PCI Max_Lat [R] (unimplemented) */
#define DDB_BAR2	0x0240	/* PCI Base Address Register 2 [R/W] */
#define DDB_BAR3	0x0248	/* PCI Base Address Register 3 [R/W] */
#define DDB_BAR4	0x0250	/* PCI Base Address Register 4 [R/W] */
#define DDB_BAR5	0x0258	/* PCI Base Address Register 5 [R/W] */
#define DDB_BAR6	0x0260	/* PCI Base Address Register 6 [R/W] */
#define DDB_BAR7	0x0268	/* PCI Base Address Register 7 [R/W] */
#define DDB_BAR8	0x0270	/* PCI Base Address Register 8 [R/W] */
#define DDB_BARB	0x0278	/* PCI Base Address Register BOOT [R/W] */


/*
 *  Nile 4 Register Access
 */

static inline void ddb_sync(void)
{
    volatile u32 *p = (volatile u32 *)0xbfc00000;
    (void)(*p);
}

static inline void ddb_out32(u32 offset, u32 val)
{
    *(volatile u32 *)(DDB_BASE+offset) = val;
    ddb_sync();
}

static inline u32 ddb_in32(u32 offset)
{
    u32 val = *(volatile u32 *)(DDB_BASE+offset);
    ddb_sync();
    return val;
}

static inline void ddb_out16(u32 offset, u16 val)
{
    *(volatile u16 *)(DDB_BASE+offset) = val;
    ddb_sync();
}

static inline u16 ddb_in16(u32 offset)
{
    u16 val = *(volatile u16 *)(DDB_BASE+offset);
    ddb_sync();
    return val;
}

static inline void ddb_out8(u32 offset, u8 val)
{
    *(volatile u8 *)(DDB_BASE+offset) = val;
    ddb_sync();
}

static inline u8 ddb_in8(u32 offset)
{
    u8 val = *(volatile u8 *)(DDB_BASE+offset);
    ddb_sync();
    return val;
}


/*
 *  Physical Device Address Registers
 */

extern u32
ddb_calc_pdar(u32 phys, u32 size, int width, int on_memory_bus, int pci_visible);
extern void
ddb_set_pdar(u32 pdar, u32 phys, u32 size, int width,
	     int on_memory_bus, int pci_visible);

/*
 *  PCI Master Registers
 */

#define DDB_PCICMD_IACK		0	/* PCI Interrupt Acknowledge */
#define DDB_PCICMD_IO		1	/* PCI I/O Space */
#define DDB_PCICMD_MEM		3	/* PCI Memory Space */
#define DDB_PCICMD_CFG		5	/* PCI Configuration Space */

/*
 * additional options for pci init reg (no shifting needed)
 */
#define DDB_PCI_CFGTYPE1     0x200   /* for pci init0/1 regs */
#define DDB_PCI_ACCESS_32    0x10    /* for pci init0/1 regs */


extern void ddb_set_pmr(u32 pmr, u32 type, u32 addr, u32 options);

/*
 * we need to reset pci bus when we start up and shutdown
 */
extern void ddb_pci_reset_bus(void);


/*
 * include the board dependent part
 */
#if defined(CONFIG_DDB5477)
#include <asm/ddb5xxx/ddb5477.h>
#else
#error "Unknown DDB board!"
#endif

#endif /* __ASM_DDB5XXX_DDB5XXX_H */
