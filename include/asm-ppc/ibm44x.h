/*
 * include/asm-ppc/ibm44x.h
 *
 * PPC44x definitions
 *
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2002-2005 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef __ASM_IBM44x_H__
#define __ASM_IBM44x_H__


#ifndef NR_BOARD_IRQS
#define NR_BOARD_IRQS 0
#endif

#define _IO_BASE	isa_io_base
#define _ISA_MEM_BASE	isa_mem_base
#define PCI_DRAM_OFFSET	pci_dram_offset

/* TLB entry offset/size used for pinning kernel lowmem */
#define PPC44x_PIN_SHIFT	28
#define PPC_PIN_SIZE		(1 << PPC44x_PIN_SHIFT)

/* Lowest TLB slot consumed by the default pinned TLBs */
#define PPC44x_LOW_SLOT		63

/*
 * Least significant 32-bits and extended real page number (ERPN) of
 * UART0 physical address location for early serial text debug
 */
#if defined(CONFIG_440SP)
#define UART0_PHYS_ERPN		1
#define UART0_PHYS_IO_BASE	0xf0000200
#elif defined(CONFIG_440SPE)
#define UART0_PHYS_ERPN		4
#define UART0_PHYS_IO_BASE	0xf0000200
#elif defined(CONFIG_440EP)
#define UART0_PHYS_IO_BASE	0xe0000000
#else
#define UART0_PHYS_ERPN		1
#define UART0_PHYS_IO_BASE	0x40000200
#endif

/*
 * XXX This 36-bit trap stuff will move somewhere in syslib/
 * when we rework/abstract the PPC44x PCI-X handling -mdp
 */

/*
 * Standard 4GB "page" definitions
 */
#if defined(CONFIG_440SP)
#define	PPC44x_IO_PAGE		0x0000000100000000ULL
#define	PPC44x_PCICFG_PAGE	0x0000000900000000ULL
#define	PPC44x_PCIIO_PAGE	PPC44x_PCICFG_PAGE
#define	PPC44x_PCIMEM_PAGE	0x0000000a00000000ULL
#elif defined(CONFIG_440SPE)
#define	PPC44x_IO_PAGE		0x0000000400000000ULL
#define	PPC44x_PCICFG_PAGE	0x0000000c00000000ULL
#define	PPC44x_PCIIO_PAGE	PPC44x_PCICFG_PAGE
#define	PPC44x_PCIMEM_PAGE	0x0000000d00000000ULL
#elif defined(CONFIG_440EP)
#define PPC44x_IO_PAGE		0x0000000000000000ULL
#define PPC44x_PCICFG_PAGE	0x0000000000000000ULL
#define PPC44x_PCIIO_PAGE	PPC44x_PCICFG_PAGE
#define PPC44x_PCIMEM_PAGE	0x0000000000000000ULL
#else
#define	PPC44x_IO_PAGE		0x0000000100000000ULL
#define	PPC44x_PCICFG_PAGE	0x0000000200000000ULL
#define	PPC44x_PCIIO_PAGE	PPC44x_PCICFG_PAGE
#define	PPC44x_PCIMEM_PAGE	0x0000000300000000ULL
#endif

/*
 * 36-bit trap ranges
 */
#if defined(CONFIG_440SP) || defined(CONFIG_440SPE)
#define PPC44x_IO_LO		0xf0000000UL
#define PPC44x_IO_HI		0xf0000fffUL
#define PPC44x_PCI0CFG_LO	0x0ec00000UL
#define PPC44x_PCI0CFG_HI	0x0ec00007UL
#define PPC44x_PCI1CFG_LO	0x1ec00000UL
#define PPC44x_PCI1CFG_HI	0x1ec00007UL
#define PPC44x_PCI2CFG_LO	0x2ec00000UL
#define PPC44x_PCI2CFG_HI	0x2ec00007UL
#define PPC44x_PCIMEM_LO	0x80000000UL
#define PPC44x_PCIMEM_HI	0xdfffffffUL
#elif defined(CONFIG_440EP)
#define PPC44x_IO_LO		0xef500000UL
#define PPC44x_IO_HI		0xefffffffUL
#define PPC44x_PCI0CFG_LO	0xeec00000UL
#define PPC44x_PCI0CFG_HI	0xeecfffffUL
#define PPC44x_PCIMEM_LO	0xa0000000UL
#define PPC44x_PCIMEM_HI	0xdfffffffUL
#else
#define PPC44x_IO_LO		0x40000000UL
#define PPC44x_IO_HI		0x40000fffUL
#define PPC44x_PCI0CFG_LO	0x0ec00000UL
#define PPC44x_PCI0CFG_HI	0x0ec00007UL
#define PPC44x_PCIMEM_LO	0x80002000UL
#define PPC44x_PCIMEM_HI	0xffffffffUL
#endif

/*
 * The "residual" board information structure the boot loader passes
 * into the kernel.
 */
#ifndef __ASSEMBLY__

/*
 * DCRN definitions
 */


/* CPRs (440GX and 440SP/440SPe) */
#define DCRN_CPR_CONFIG_ADDR	0xc
#define DCRN_CPR_CONFIG_DATA	0xd

#define DCRN_CPR_CLKUPD		0x0020
#define DCRN_CPR_PLLC		0x0040
#define DCRN_CPR_PLLD		0x0060
#define DCRN_CPR_PRIMAD		0x0080
#define DCRN_CPR_PRIMBD		0x00a0
#define DCRN_CPR_OPBD		0x00c0
#define DCRN_CPR_PERD		0x00e0
#define DCRN_CPR_MALD		0x0100

/* CPRs read/write helper macros */
#define CPR_READ(offset) ({\
	mtdcr(DCRN_CPR_CONFIG_ADDR, offset); \
	mfdcr(DCRN_CPR_CONFIG_DATA);})
#define CPR_WRITE(offset, data) ({\
	mtdcr(DCRN_CPR_CONFIG_ADDR, offset); \
	mtdcr(DCRN_CPR_CONFIG_DATA, data);})

/* SDRs (440GX and 440SP/440SPe) */
#define DCRN_SDR_CONFIG_ADDR 	0xe
#define DCRN_SDR_CONFIG_DATA	0xf
#define DCRN_SDR_PFC0		0x4100
#define DCRN_SDR_PFC1		0x4101
#define DCRN_SDR_PFC1_EPS	0x1c00000
#define DCRN_SDR_PFC1_EPS_SHIFT	22
#define DCRN_SDR_PFC1_RMII	0x02000000
#define DCRN_SDR_MFR		0x4300
#define DCRN_SDR_MFR_TAH0 	0x80000000  	/* TAHOE0 Enable */
#define DCRN_SDR_MFR_TAH1 	0x40000000  	/* TAHOE1 Enable */
#define DCRN_SDR_MFR_PCM  	0x10000000  	/* PPC440GP irq compat mode */
#define DCRN_SDR_MFR_ECS  	0x08000000  	/* EMAC int clk */
#define DCRN_SDR_MFR_T0TXFL	0x00080000
#define DCRN_SDR_MFR_T0TXFH	0x00040000
#define DCRN_SDR_MFR_T1TXFL	0x00020000
#define DCRN_SDR_MFR_T1TXFH	0x00010000
#define DCRN_SDR_MFR_E0TXFL	0x00008000
#define DCRN_SDR_MFR_E0TXFH	0x00004000
#define DCRN_SDR_MFR_E0RXFL	0x00002000
#define DCRN_SDR_MFR_E0RXFH	0x00001000
#define DCRN_SDR_MFR_E1TXFL	0x00000800
#define DCRN_SDR_MFR_E1TXFH	0x00000400
#define DCRN_SDR_MFR_E1RXFL	0x00000200
#define DCRN_SDR_MFR_E1RXFH	0x00000100
#define DCRN_SDR_MFR_E2TXFL	0x00000080
#define DCRN_SDR_MFR_E2TXFH	0x00000040
#define DCRN_SDR_MFR_E2RXFL	0x00000020
#define DCRN_SDR_MFR_E2RXFH	0x00000010
#define DCRN_SDR_MFR_E3TXFL	0x00000008
#define DCRN_SDR_MFR_E3TXFH	0x00000004
#define DCRN_SDR_MFR_E3RXFL	0x00000002
#define DCRN_SDR_MFR_E3RXFH	0x00000001
#define DCRN_SDR_UART0		0x0120
#define DCRN_SDR_UART1		0x0121

#ifdef CONFIG_440EP
#define DCRN_SDR_UART2		0x0122
#define DCRN_SDR_UART3		0x0123
#define DCRN_SDR_CUST0		0x4000
#endif

/* SDR read/write helper macros */
#define SDR_READ(offset) ({\
	mtdcr(DCRN_SDR_CONFIG_ADDR, offset); \
	mfdcr(DCRN_SDR_CONFIG_DATA);})
#define SDR_WRITE(offset, data) ({\
	mtdcr(DCRN_SDR_CONFIG_ADDR, offset); \
	mtdcr(DCRN_SDR_CONFIG_DATA,data);})

/* DMA (excluding 440SP/440SPe) */
#define DCRN_DMA0_BASE		0x100
#define DCRN_DMA1_BASE		0x108
#define DCRN_DMA2_BASE		0x110
#define DCRN_DMA3_BASE		0x118
#define DCRN_DMASR_BASE		0x120
#define DCRNCAP_DMA_SG		1	/* have DMA scatter/gather capability */
#define DCRN_MAL_BASE		0x180

#ifdef CONFIG_440EP
#define DCRN_DMA2P40_BASE	0x300
#define DCRN_DMA2P41_BASE	0x308
#define DCRN_DMA2P42_BASE	0x310
#define DCRN_DMA2P43_BASE	0x318
#define DCRN_DMA2P4SR_BASE	0x320
#endif

/* UIC */
#define DCRN_UIC0_BASE	0xc0
#define DCRN_UIC1_BASE	0xd0
#define UIC0		DCRN_UIC0_BASE
#define UIC1		DCRN_UIC1_BASE

#ifdef CONFIG_440SPE
#define DCRN_UIC2_BASE	0xe0
#define DCRN_UIC3_BASE	0xf0
#define UIC2		DCRN_UIC2_BASE
#define UIC3		DCRN_UIC3_BASE
#else
#define DCRN_UIC2_BASE	0x210
#define DCRN_UICB_BASE	0x200
#define UIC2		DCRN_UIC2_BASE
#define UICB		DCRN_UICB_BASE
#endif

#define DCRN_UIC_SR(base)       (base + 0x0)
#define DCRN_UIC_ER(base)       (base + 0x2)
#define DCRN_UIC_CR(base)       (base + 0x3)
#define DCRN_UIC_PR(base)       (base + 0x4)
#define DCRN_UIC_TR(base)       (base + 0x5)
#define DCRN_UIC_MSR(base)      (base + 0x6)
#define DCRN_UIC_VR(base)       (base + 0x7)
#define DCRN_UIC_VCR(base)      (base + 0x8)

#define UIC0_UIC1NC      	0x00000002

#ifdef CONFIG_440SPE
#define UIC0_UIC1NC      0x00000002
#define UIC0_UIC2NC      0x00200000
#define UIC0_UIC3NC      0x00008000
#endif

#define UICB_UIC0NC		0x40000000
#define UICB_UIC1NC		0x10000000
#define UICB_UIC2NC		0x04000000

/* 440 MAL DCRs */
#define DCRN_MALCR(base)		(base + 0x0)	/* Configuration */
#define DCRN_MALESR(base)		(base + 0x1)	/* Error Status */
#define DCRN_MALIER(base)		(base + 0x2)	/* Interrupt Enable */
#define DCRN_MALTXCASR(base)		(base + 0x4)	/* Tx Channel Active Set */
#define DCRN_MALTXCARR(base)		(base + 0x5)	/* Tx Channel Active Reset */
#define DCRN_MALTXEOBISR(base)		(base + 0x6)	/* Tx End of Buffer Interrupt Status */
#define DCRN_MALTXDEIR(base)		(base + 0x7)	/* Tx Descriptor Error Interrupt */
#define DCRN_MALRXCASR(base)		(base + 0x10)	/* Rx Channel Active Set */
#define DCRN_MALRXCARR(base)		(base + 0x11)	/* Rx Channel Active Reset */
#define DCRN_MALRXEOBISR(base)		(base + 0x12)	/* Rx End of Buffer Interrupt Status */
#define DCRN_MALRXDEIR(base)		(base + 0x13)	/* Rx Descriptor Error Interrupt */
#define DCRN_MALTXCTP0R(base)		(base + 0x20)	/* Channel Tx 0 Channel Table Pointer */
#define DCRN_MALTXCTP1R(base)		(base + 0x21)	/* Channel Tx 1 Channel Table Pointer */
#define DCRN_MALTXCTP2R(base)		(base + 0x22)	/* Channel Tx 2 Channel Table Pointer */
#define DCRN_MALTXCTP3R(base)		(base + 0x23)	/* Channel Tx 3 Channel Table Pointer */
#define DCRN_MALRXCTP0R(base)		(base + 0x40)	/* Channel Rx 0 Channel Table Pointer */
#define DCRN_MALRXCTP1R(base)		(base + 0x41)	/* Channel Rx 1 Channel Table Pointer */
#define DCRN_MALRCBS0(base)		(base + 0x60)	/* Channel Rx 0 Channel Buffer Size */
#define DCRN_MALRCBS1(base)		(base + 0x61)	/* Channel Rx 1 Channel Buffer Size */

/* Compatibility DCRN's */
#define DCRN_MALRXCTP2R(base)	((base) + 0x42)	/* Channel Rx 2 Channel Table Pointer */
#define DCRN_MALRXCTP3R(base)	((base) + 0x43)	/* Channel Rx 3 Channel Table Pointer */
#define DCRN_MALTXCTP4R(base)	((base) + 0x24)	/* Channel Tx 4 Channel Table Pointer */
#define DCRN_MALTXCTP5R(base)	((base) + 0x25)	/* Channel Tx 5 Channel Table Pointer */
#define DCRN_MALTXCTP6R(base)	((base) + 0x26)	/* Channel Tx 6 Channel Table Pointer */
#define DCRN_MALTXCTP7R(base)	((base) + 0x27)	/* Channel Tx 7 Channel Table Pointer */
#define DCRN_MALRCBS2(base)	((base) + 0x62)	/* Channel Rx 2 Channel Buffer Size */
#define DCRN_MALRCBS3(base)	((base) + 0x63)	/* Channel Rx 3 Channel Buffer Size */

#define MALCR_MMSR		0x80000000	/* MAL Software reset */
#define MALCR_PLBP_1		0x00400000	/* MAL reqest priority: */
#define MALCR_PLBP_2		0x00800000	/* lowsest is 00 */
#define MALCR_PLBP_3		0x00C00000	/* highest */
#define MALCR_GA		0x00200000	/* Guarded Active Bit */
#define MALCR_OA		0x00100000	/* Ordered Active Bit */
#define MALCR_PLBLE		0x00080000	/* PLB Lock Error Bit */
#define MALCR_PLBLT_1		0x00040000	/* PLB Latency Timer */
#define MALCR_PLBLT_2 		0x00020000
#define MALCR_PLBLT_3		0x00010000
#define MALCR_PLBLT_4		0x00008000
#ifdef CONFIG_440GP
#define MALCR_PLBLT_DEFAULT	0x00330000	/* PLB Latency Timer default */
#else
#define MALCR_PLBLT_DEFAULT	0x00ff0000	/* PLB Latency Timer default */
#endif
#define MALCR_PLBB		0x00004000	/* PLB Burst Deactivation Bit */
#define MALCR_OPBBL		0x00000080	/* OPB Lock Bit */
#define MALCR_EOPIE		0x00000004	/* End Of Packet Interrupt Enable */
#define MALCR_LEA		0x00000002	/* Locked Error Active */
#define MALCR_MSD		0x00000001	/* MAL Scroll Descriptor Bit */
/* DCRN_MALESR */
#define MALESR_EVB		0x80000000	/* Error Valid Bit */
#define MALESR_CIDRX		0x40000000	/* Channel ID Receive */
#define MALESR_DE		0x00100000	/* Descriptor Error */
#define MALESR_OEN		0x00080000	/* OPB Non-Fullword Error */
#define MALESR_OTE		0x00040000	/* OPB Timeout Error */
#define MALESR_OSE		0x00020000	/* OPB Slave Error */
#define MALESR_PEIN		0x00010000	/* PLB Bus Error Indication */
#define MALESR_DEI		0x00000010	/* Descriptor Error Interrupt */
#define MALESR_ONEI		0x00000008	/* OPB Non-Fullword Error Interrupt */
#define MALESR_OTEI		0x00000004	/* OPB Timeout Error Interrupt */
#define MALESR_OSEI		0x00000002	/* OPB Slace Error Interrupt */
#define MALESR_PBEI		0x00000001	/* PLB Bus Error Interrupt */
/* DCRN_MALIER */
#define MALIER_DE		0x00000010	/* Descriptor Error Interrupt Enable */
#define MALIER_NE		0x00000008	/* OPB Non-word Transfer Int Enable */
#define MALIER_TE		0x00000004	/* OPB Time Out Error Interrupt Enable */
#define MALIER_OPBE		0x00000002	/* OPB Slave Error Interrupt Enable */
#define MALIER_PLBE		0x00000001	/* PLB Error Interrupt Enable */
/* DCRN_MALTXEOBISR */
#define MALOBISR_CH0		0x80000000	/* EOB channel 1 bit */
#define MALOBISR_CH2		0x40000000	/* EOB channel 2 bit */

#if defined(CONFIG_440SP) || defined(CONFIG_440SPE)
/* 440SP/440SPe PLB Arbiter DCRs */
#define DCRN_PLB_REVID	       0x080		/* PLB Revision ID */
#define DCRN_PLB_CCR	       0x088		/* PLB Crossbar Control */

#define DCRN_PLB0_ACR	       0x081		/* PLB Arbiter Control */
#define DCRN_PLB0_BESRL	       0x082		/* PLB Error Status */
#define DCRN_PLB0_BESRH	       0x083		/* PLB Error Status */
#define DCRN_PLB0_BEARL	       0x084		/* PLB Error Address Low */
#define DCRN_PLB0_BEARH	       0x085		/* PLB Error Address High */

#define DCRN_PLB1_ACR		0x089		/* PLB Arbiter Control */
#define DCRN_PLB1_BESRL		0x08a		/* PLB Error Status */
#define DCRN_PLB1_BESRH		0x08b		/* PLB Error Status */
#define DCRN_PLB1_BEARL		0x08c		/* PLB Error Address Low */
#define DCRN_PLB1_BEARH		0x08d		/* PLB Error Address High */
#else
/* 440GP/GX PLB Arbiter DCRs */
#define DCRN_PLB0_REVID		0x082		/* PLB Arbiter Revision ID */
#define DCRN_PLB0_ACR		0x083		/* PLB Arbiter Control */
#define DCRN_PLB0_BESR		0x084		/* PLB Error Status */
#define DCRN_PLB0_BEARL		0x086		/* PLB Error Address Low */
#define DCRN_PLB0_BEAR		DCRN_PLB0_BEARL	/* 40x compatibility */
#define DCRN_PLB0_BEARH		0x087		/* PLB Error Address High */
#endif

/* 440GP/GX PLB to OPB bridge DCRs */
#define DCRN_POB0_BESR0		0x090
#define DCRN_POB0_BESR1		0x094
#define DCRN_POB0_BEARL		0x092
#define DCRN_POB0_BEARH		0x093

/* 440GP/GX OPB to PLB bridge DCRs */
#define DCRN_OPB0_BSTAT		0x0a9
#define DCRN_OPB0_BEARL		0x0aa
#define DCRN_OPB0_BEARH		0x0ab

/* 440GP Clock, PM, chip control */
#define DCRN_CPC0_SR		0x0b0
#define DCRN_CPC0_ER		0x0b1
#define DCRN_CPC0_FR		0x0b2
#define DCRN_CPC0_SYS0		0x0e0
#define DCRN_CPC0_SYS1		0x0e1
#define DCRN_CPC0_CUST0		0x0e2
#define DCRN_CPC0_CUST1		0x0e3
#define DCRN_CPC0_STRP0		0x0e4
#define DCRN_CPC0_STRP1		0x0e5
#define DCRN_CPC0_STRP2		0x0e6
#define DCRN_CPC0_STRP3		0x0e7
#define DCRN_CPC0_GPIO		0x0e8
#define DCRN_CPC0_PLB		0x0e9
#define DCRN_CPC0_CR1		0x0ea
#define DCRN_CPC0_CR0		0x0eb
#define DCRN_CPC0_MIRQ0		0x0ec
#define DCRN_CPC0_MIRQ1		0x0ed
#define DCRN_CPC0_JTAGID	0x0ef

/* 440GP DMA controller DCRs */
#define DCRN_DMACR0	(DCRN_DMA0_BASE + 0x0)	/* DMA Channel Control 0 */
#define DCRN_DMACT0	(DCRN_DMA0_BASE + 0x1)  /* DMA Count 0 */
#define DCRN_DMASAH0	(DCRN_DMA0_BASE + 0x2)	/* DMA Src Addr High 0 */
#define DCRN_DMASA0	(DCRN_DMA0_BASE + 0x3)	/* DMA Src Addr Low 0 */
#define DCRN_DMADAH0	(DCRN_DMA0_BASE + 0x4)	/* DMA Dest Addr High 0 */
#define DCRN_DMADA0	(DCRN_DMA0_BASE + 0x5)	/* DMA Dest Addr Low 0 */
#define DCRN_ASGH0	(DCRN_DMA0_BASE + 0x6)	/* DMA SG Desc Addr High 0 */
#define DCRN_ASG0	(DCRN_DMA0_BASE + 0x7)	/* DMA SG Desc Addr Low 0 */

#define DCRN_DMACR1	(DCRN_DMA1_BASE + 0x0)	/* DMA Channel Control 1 */
#define DCRN_DMACT1	(DCRN_DMA1_BASE + 0x1)  /* DMA Count 1 */
#define DCRN_DMASAH1	(DCRN_DMA1_BASE + 0x2)	/* DMA Src Addr High 1 */
#define DCRN_DMASA1	(DCRN_DMA1_BASE + 0x3)	/* DMA Src Addr Low 1 */
#define DCRN_DMADAH1	(DCRN_DMA1_BASE + 0x4)	/* DMA Dest Addr High 1 */
#define DCRN_DMADA1	(DCRN_DMA1_BASE + 0x5)	/* DMA Dest Addr Low 1 */
#define DCRN_ASGH1	(DCRN_DMA1_BASE + 0x6)	/* DMA SG Desc Addr High 1 */
#define DCRN_ASG1	(DCRN_DMA1_BASE + 0x7)	/* DMA SG Desc Addr Low 1 */

#define DCRN_DMACR2	(DCRN_DMA2_BASE + 0x0)	/* DMA Channel Control 2 */
#define DCRN_DMACT2	(DCRN_DMA2_BASE + 0x1)  /* DMA Count 2 */
#define DCRN_DMASAH2	(DCRN_DMA2_BASE + 0x2)	/* DMA Src Addr High 2 */
#define DCRN_DMASA2	(DCRN_DMA2_BASE + 0x3)	/* DMA Src Addr Low 2 */
#define DCRN_DMADAH2	(DCRN_DMA2_BASE + 0x4)	/* DMA Dest Addr High 2 */
#define DCRN_DMADA2	(DCRN_DMA2_BASE + 0x5)	/* DMA Dest Addr Low 2 */
#define DCRN_ASGH2	(DCRN_DMA2_BASE + 0x6)	/* DMA SG Desc Addr High 2 */
#define DCRN_ASG2	(DCRN_DMA2_BASE + 0x7)	/* DMA SG Desc Addr Low 2 */

#define DCRN_DMACR3	(DCRN_DMA3_BASE + 0x0)	/* DMA Channel Control 3 */
#define DCRN_DMACT3	(DCRN_DMA3_BASE + 0x1)  /* DMA Count 3 */
#define DCRN_DMASAH3	(DCRN_DMA3_BASE + 0x2)	/* DMA Src Addr High 3 */
#define DCRN_DMASA3	(DCRN_DMA3_BASE + 0x3)	/* DMA Src Addr Low 3 */
#define DCRN_DMADAH3	(DCRN_DMA3_BASE + 0x4)	/* DMA Dest Addr High 3 */
#define DCRN_DMADA3	(DCRN_DMA3_BASE + 0x5)	/* DMA Dest Addr Low 3 */
#define DCRN_ASGH3	(DCRN_DMA3_BASE + 0x6)	/* DMA SG Desc Addr High 3 */
#define DCRN_ASG3	(DCRN_DMA3_BASE + 0x7)	/* DMA SG Desc Addr Low 3 */

#define DCRN_DMASR	(DCRN_DMASR_BASE + 0x0)	/* DMA Status Register */
#define DCRN_ASGC	(DCRN_DMASR_BASE + 0x3)	/* DMA Scatter/Gather Command */
#define DCRN_SLP	(DCRN_DMASR_BASE + 0x5)	/* DMA Sleep Register */
#define DCRN_POL	(DCRN_DMASR_BASE + 0x6)	/* DMA Polarity Register */

/* 440GP/440GX SDRAM controller DCRs */
#define DCRN_SDRAM0_CFGADDR		0x010
#define DCRN_SDRAM0_CFGDATA		0x011

#define SDRAM0_B0CR	0x40
#define SDRAM0_B1CR	0x44
#define SDRAM0_B2CR	0x48
#define SDRAM0_B3CR	0x4c

#define SDRAM_CONFIG_BANK_ENABLE	0x00000001
#define SDRAM_CONFIG_SIZE_MASK		0x000e0000
#define SDRAM_CONFIG_BANK_SIZE(reg)	((reg & SDRAM_CONFIG_SIZE_MASK) >> 17)
#define SDRAM_CONFIG_SIZE_8M		0x00000001
#define SDRAM_CONFIG_SIZE_16M		0x00000002
#define SDRAM_CONFIG_SIZE_32M		0x00000003
#define SDRAM_CONFIG_SIZE_64M		0x00000004
#define SDRAM_CONFIG_SIZE_128M		0x00000005
#define SDRAM_CONFIG_SIZE_256M		0x00000006
#define SDRAM_CONFIG_SIZE_512M		0x00000007
#define PPC44x_MEM_SIZE_8M		0x00800000
#define PPC44x_MEM_SIZE_16M		0x01000000
#define PPC44x_MEM_SIZE_32M		0x02000000
#define PPC44x_MEM_SIZE_64M		0x04000000
#define PPC44x_MEM_SIZE_128M		0x08000000
#define PPC44x_MEM_SIZE_256M		0x10000000
#define PPC44x_MEM_SIZE_512M		0x20000000
#define PPC44x_MEM_SIZE_1G		0x40000000
#define PPC44x_MEM_SIZE_2G		0x80000000

/* 440SP/440SPe memory controller DCRs */
#define DCRN_MQ0_BS0BAS			0x40
#if defined(CONFIG_440SP)
#define MQ0_NUM_BANKS			2
#elif defined(CONFIG_440SPE)
#define MQ0_NUM_BANKS			4
#endif

#define MQ0_CONFIG_SIZE_MASK		0x0000fff0
#define MQ0_CONFIG_SIZE_8M		0x0000ffc0
#define MQ0_CONFIG_SIZE_16M		0x0000ff80
#define MQ0_CONFIG_SIZE_32M		0x0000ff00
#define MQ0_CONFIG_SIZE_64M		0x0000fe00
#define MQ0_CONFIG_SIZE_128M		0x0000fc00
#define MQ0_CONFIG_SIZE_256M		0x0000f800
#define MQ0_CONFIG_SIZE_512M		0x0000f000
#define MQ0_CONFIG_SIZE_1G		0x0000e000
#define MQ0_CONFIG_SIZE_2G		0x0000c000
#define MQ0_CONFIG_SIZE_4G		0x00008000

/* Internal SRAM Controller 440GX/440SP/440SPe */
#define DCRN_SRAM0_BASE		0x000

#define DCRN_SRAM0_SB0CR	(DCRN_SRAM0_BASE + 0x020)
#define DCRN_SRAM0_SB1CR	(DCRN_SRAM0_BASE + 0x021)
#define DCRN_SRAM0_SB2CR	(DCRN_SRAM0_BASE + 0x022)
#define DCRN_SRAM0_SB3CR	(DCRN_SRAM0_BASE + 0x023)
#define  SRAM_SBCR_BAS0		0x80000000
#define  SRAM_SBCR_BAS1		0x80010000
#define  SRAM_SBCR_BAS2		0x80020000
#define  SRAM_SBCR_BAS3		0x80030000
#define  SRAM_SBCR_BU_MASK	0x00000180
#define  SRAM_SBCR_BS_64KB	0x00000800
#define  SRAM_SBCR_BU_RO	0x00000080
#define  SRAM_SBCR_BU_RW	0x00000180
#define DCRN_SRAM0_BEAR		(DCRN_SRAM0_BASE + 0x024)
#define DCRN_SRAM0_BESR0	(DCRN_SRAM0_BASE + 0x025)
#define DCRN_SRAM0_BESR1	(DCRN_SRAM0_BASE + 0x026)
#define DCRN_SRAM0_PMEG		(DCRN_SRAM0_BASE + 0x027)
#define DCRN_SRAM0_CID		(DCRN_SRAM0_BASE + 0x028)
#define DCRN_SRAM0_REVID	(DCRN_SRAM0_BASE + 0x029)
#define DCRN_SRAM0_DPC		(DCRN_SRAM0_BASE + 0x02a)
#define  SRAM_DPC_ENABLE	0x80000000

/* L2 Cache Controller 440GX/440SP/440SPe */
#define DCRN_L2C0_CFG		0x030
#define  L2C_CFG_L2M		0x80000000
#define  L2C_CFG_ICU		0x40000000
#define  L2C_CFG_DCU		0x20000000
#define  L2C_CFG_DCW_MASK	0x1e000000
#define  L2C_CFG_TPC		0x01000000
#define  L2C_CFG_CPC		0x00800000
#define  L2C_CFG_FRAN		0x00200000
#define  L2C_CFG_SS_MASK	0x00180000
#define  L2C_CFG_SS_256		0x00000000
#define  L2C_CFG_CPIM		0x00040000
#define  L2C_CFG_TPIM		0x00020000
#define  L2C_CFG_LIM		0x00010000
#define  L2C_CFG_PMUX_MASK	0x00007000
#define  L2C_CFG_PMUX_SNP	0x00000000
#define  L2C_CFG_PMUX_IF	0x00001000
#define  L2C_CFG_PMUX_DF	0x00002000
#define  L2C_CFG_PMUX_DS	0x00003000
#define  L2C_CFG_PMIM		0x00000800
#define  L2C_CFG_TPEI		0x00000400
#define  L2C_CFG_CPEI		0x00000200
#define  L2C_CFG_NAM		0x00000100
#define  L2C_CFG_SMCM		0x00000080
#define  L2C_CFG_NBRM		0x00000040
#define DCRN_L2C0_CMD		0x031
#define  L2C_CMD_CLR		0x80000000
#define  L2C_CMD_DIAG		0x40000000
#define  L2C_CMD_INV		0x20000000
#define  L2C_CMD_CCP		0x10000000
#define  L2C_CMD_CTE		0x08000000
#define  L2C_CMD_STRC		0x04000000
#define  L2C_CMD_STPC		0x02000000
#define  L2C_CMD_RPMC		0x01000000
#define  L2C_CMD_HCC		0x00800000
#define DCRN_L2C0_ADDR		0x032
#define DCRN_L2C0_DATA		0x033
#define DCRN_L2C0_SR		0x034
#define  L2C_SR_CC		0x80000000
#define  L2C_SR_CPE		0x40000000
#define  L2C_SR_TPE		0x20000000
#define  L2C_SR_LRU		0x10000000
#define  L2C_SR_PCS		0x08000000
#define DCRN_L2C0_REVID		0x035
#define DCRN_L2C0_SNP0		0x036
#define DCRN_L2C0_SNP1		0x037
#define  L2C_SNP_BA_MASK	0xffff0000
#define  L2C_SNP_SSR_MASK	0x0000f000
#define  L2C_SNP_SSR_32G	0x0000f000
#define  L2C_SNP_ESR		0x00000800

/*
 * PCI-X definitions
 */
#define PCIX0_CFGA		0x0ec00000UL
#define PCIX1_CFGA		0x1ec00000UL
#define PCIX2_CFGA		0x2ec00000UL
#define PCIX0_CFGD		0x0ec00004UL
#define PCIX1_CFGD		0x1ec00004UL
#define PCIX2_CFGD		0x2ec00004UL

#define PCIX0_IO_BASE		0x0000000908000000ULL
#define PCIX1_IO_BASE		0x0000000908000000ULL
#define PCIX2_IO_BASE		0x0000000908000000ULL
#define PCIX_IO_SIZE		0x00010000

#ifdef CONFIG_440SP
#define PCIX0_REG_BASE		0x000000090ec80000ULL
#else
#define PCIX0_REG_BASE		0x000000020ec80000ULL
#endif
#define PCIX_REG_OFFSET		0x10000000
#define PCIX_REG_SIZE		0x200

#define PCIX0_VENDID		0x000
#define PCIX0_DEVID		0x002
#define PCIX0_COMMAND		0x004
#define PCIX0_STATUS		0x006
#define PCIX0_REVID		0x008
#define PCIX0_CLS		0x009
#define PCIX0_CACHELS		0x00c
#define PCIX0_LATTIM		0x00d
#define PCIX0_HDTYPE		0x00e
#define PCIX0_BIST		0x00f
#define PCIX0_BAR0L		0x010
#define PCIX0_BAR0H		0x014
#define PCIX0_BAR1		0x018
#define PCIX0_BAR2L		0x01c
#define PCIX0_BAR2H		0x020
#define PCIX0_BAR3		0x024
#define PCIX0_CISPTR		0x028
#define PCIX0_SBSYSVID		0x02c
#define PCIX0_SBSYSID		0x02e
#define PCIX0_EROMBA		0x030
#define PCIX0_CAP		0x034
#define PCIX0_RES0		0x035
#define PCIX0_RES1		0x036
#define PCIX0_RES2		0x038
#define PCIX0_INTLN		0x03c
#define PCIX0_INTPN		0x03d
#define PCIX0_MINGNT		0x03e
#define PCIX0_MAXLTNCY		0x03f
#define PCIX0_BRDGOPT1		0x040
#define PCIX0_BRDGOPT2		0x044
#define PCIX0_ERREN		0x050
#define PCIX0_ERRSTS		0x054
#define PCIX0_PLBBESR		0x058
#define PCIX0_PLBBEARL		0x05c
#define PCIX0_PLBBEARH		0x060
#define PCIX0_POM0LAL		0x068
#define PCIX0_POM0LAH		0x06c
#define PCIX0_POM0SA		0x070
#define PCIX0_POM0PCIAL		0x074
#define PCIX0_POM0PCIAH		0x078
#define PCIX0_POM1LAL		0x07c
#define PCIX0_POM1LAH		0x080
#define PCIX0_POM1SA		0x084
#define PCIX0_POM1PCIAL		0x088
#define PCIX0_POM1PCIAH		0x08c
#define PCIX0_POM2SA		0x090
#define PCIX0_PIM0SAL		0x098
#define PCIX0_PIM0SA		PCIX0_PIM0SAL
#define PCIX0_PIM0LAL		0x09c
#define PCIX0_PIM0LAH		0x0a0
#define PCIX0_PIM1SA		0x0a4
#define PCIX0_PIM1LAL		0x0a8
#define PCIX0_PIM1LAH		0x0ac
#define PCIX0_PIM2SAL		0x0b0
#define PCIX0_PIM2SA		PCIX0_PIM2SAL
#define PCIX0_PIM2LAL		0x0b4
#define PCIX0_PIM2LAH		0x0b8
#define PCIX0_OMCAPID		0x0c0
#define PCIX0_OMNIPTR		0x0c1
#define PCIX0_OMMC		0x0c2
#define PCIX0_OMMA		0x0c4
#define PCIX0_OMMUA		0x0c8
#define PCIX0_OMMDATA		0x0cc
#define PCIX0_OMMEOI		0x0ce
#define PCIX0_PMCAPID		0x0d0
#define PCIX0_PMNIPTR		0x0d1
#define PCIX0_PMC		0x0d2
#define PCIX0_PMCSR		0x0d4
#define PCIX0_PMCSRBSE		0x0d6
#define PCIX0_PMDATA		0x0d7
#define PCIX0_PMSCRR		0x0d8
#define PCIX0_CAPID		0x0dc
#define PCIX0_NIPTR		0x0dd
#define PCIX0_CMD		0x0de
#define PCIX0_STS		0x0e0
#define PCIX0_IDR		0x0e4
#define PCIX0_CID		0x0e8
#define PCIX0_RID		0x0ec
#define PCIX0_PIM0SAH		0x0f8
#define PCIX0_PIM2SAH		0x0fc
#define PCIX0_MSGIL		0x100
#define PCIX0_MSGIH		0x104
#define PCIX0_MSGOL		0x108
#define PCIX0_MSGOH		0x10c
#define PCIX0_IM		0x1f8

#define IIC_OWN			0x55
#define IIC_CLOCK		50

#undef NR_UICS
#if defined(CONFIG_440GX)
#define NR_UICS 3
#elif defined(CONFIG_440SPE)
#define NR_UICS 4
#else
#define NR_UICS 2
#endif

#include <asm/ibm4xx.h>

#endif /* __ASSEMBLY__ */
#endif /* __ASM_IBM44x_H__ */
#endif /* __KERNEL__ */
