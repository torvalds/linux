/*
 * Authors: Armin Kuster <akuster@mvista.com> and Tom Rini <trini@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */


#ifdef __KERNEL__
#ifndef __ASM_IBM403_H__
#define __ASM_IBM403_H__

#include <linux/config.h>

#if defined(CONFIG_403GCX)

#define	DCRN_BE_BASE		0x090
#define	DCRN_DMA0_BASE		0x0C0
#define	DCRN_DMA1_BASE		0x0C8
#define	DCRN_DMA2_BASE		0x0D0
#define	DCRN_DMA3_BASE		0x0D8
#define DCRNCAP_DMA_CC		1	/* have DMA chained count capability */
#define	DCRN_DMASR_BASE		0x0E0

#define	DCRN_EXIER_BASE		0x042
#define	DCRN_EXISR_BASE		0x040
#define	DCRN_IOCR_BASE		0x0A0


/* ------------------------------------------------------------------------- */
#endif



#ifdef DCRN_BE_BASE
#define	DCRN_BEAR	(DCRN_BE_BASE + 0x0)	/* Bus Error Address Register */
#define	DCRN_BESR	(DCRN_BE_BASE + 0x1)	/* Bus Error Syndrome Register*/
#endif
/* DCRN_BESR */
#define BESR_DSES	0x80000000	/* Data-Side Error Status */
#define BESR_DMES	0x40000000	/* DMA Error Status */
#define BESR_RWS	0x20000000	/* Read/Write Status */
#define BESR_ETMASK	0x1C000000	/* Error Type */
#define ET_PROT	0
#define ET_PARITY	1
#define ET_NCFG	2
#define ET_BUSERR	4
#define ET_BUSTO	6

#ifdef DCRN_CHCR_BASE
#define DCRN_CHCR0	(DCRN_CHCR_BASE + 0x0)	/* Chip Control Register 1 */
#define DCRN_CHCR1	(DCRN_CHCR_BASE + 0x1)	/* Chip Control Register 2 */
#endif
#define CHR1_CETE	0x00800000		 /* CPU external timer enable */
#define CHR1_PCIPW	0x00008000 /* PCI Int enable/Peripheral Write enable */

#ifdef DCRN_CHPSR_BASE
#define DCRN_CHPSR	(DCRN_CHPSR_BASE + 0x0)	/* Chip Pin Strapping */
#endif

#ifdef DCRN_CIC_BASE
#define DCRN_CICCR	(DCRN_CIC_BASE + 0x0)	/* CIC Control Register */
#define DCRN_DMAS1	(DCRN_CIC_BASE + 0x1)	/* DMA Select1 Register */
#define DCRN_DMAS2	(DCRN_CIC_BASE + 0x2)	/* DMA Select2 Register */
#define DCRN_CICVCR	(DCRN_CIC_BASE + 0x3)	/* CIC Video COntro Register */
#define DCRN_CICSEL3	(DCRN_CIC_BASE + 0x5)	/* CIC Select 3 Register */
#define DCRN_SGPO	(DCRN_CIC_BASE + 0x6)	/* CIC GPIO Output Register */
#define DCRN_SGPOD	(DCRN_CIC_BASE + 0x7)	/* CIC GPIO OD Register */
#define DCRN_SGPTC	(DCRN_CIC_BASE + 0x8)	/* CIC GPIO Tristate Ctrl Reg */
#define DCRN_SGPI	(DCRN_CIC_BASE + 0x9)	/* CIC GPIO Input Reg */
#endif

#ifdef DCRN_CPMFR_BASE
#define DCRN_CPMFR	(DCRN_CPMFR_BASE + 0x0)	/* CPM Force */
#endif

#ifndef CPM_AUD
#define CPM_AUD		0x00000000
#endif
#ifndef CPM_BRG
#define CPM_BRG		0x00000000
#endif
#ifndef CPM_CBS
#define CPM_CBS		0x00000000
#endif
#ifndef CPM_CPU
#define CPM_CPU		0x00000000
#endif
#ifndef CPM_DCP
#define CPM_DCP		0x00000000
#endif
#ifndef CPM_DCRX
#define CPM_DCRX	0x00000000
#endif
#ifndef CPM_DENC
#define CPM_DENC	0x00000000
#endif
#ifndef CPM_DMA
#define CPM_DMA		0x00000000
#endif
#ifndef CPM_DSCR
#define CPM_DSCR	0x00000000
#endif
#ifndef CPM_EBC
#define CPM_EBC		0x00000000
#endif
#ifndef CPM_EBIU
#define CPM_EBIU	0x00000000
#endif
#ifndef CPM_EMAC_MM
#define CPM_EMAC_MM	0x00000000
#endif
#ifndef CPM_EMAC_RM
#define CPM_EMAC_RM	0x00000000
#endif
#ifndef CPM_EMAC_TM
#define CPM_EMAC_TM	0x00000000
#endif
#ifndef CPM_GPIO0
#define CPM_GPIO0	0x00000000
#endif
#ifndef CPM_GPT
#define CPM_GPT		0x00000000
#endif
#ifndef CPM_I1284
#define CPM_I1284	0x00000000
#endif
#ifndef CPM_IIC0
#define CPM_IIC0	0x00000000
#endif
#ifndef CPM_IIC1
#define CPM_IIC1	0x00000000
#endif
#ifndef CPM_MSI
#define CPM_MSI		0x00000000
#endif
#ifndef CPM_PCI
#define CPM_PCI		0x00000000
#endif
#ifndef CPM_PLB
#define CPM_PLB		0x00000000
#endif
#ifndef CPM_SC0
#define CPM_SC0		0x00000000
#endif
#ifndef CPM_SC1
#define CPM_SC1		0x00000000
#endif
#ifndef CPM_SDRAM0
#define CPM_SDRAM0	0x00000000
#endif
#ifndef CPM_SDRAM1
#define CPM_SDRAM1	0x00000000
#endif
#ifndef CPM_TMRCLK
#define CPM_TMRCLK	0x00000000
#endif
#ifndef CPM_UART0
#define CPM_UART0	0x00000000
#endif
#ifndef CPM_UART1
#define CPM_UART1	0x00000000
#endif
#ifndef CPM_UART2
#define CPM_UART2	0x00000000
#endif
#ifndef CPM_UIC
#define CPM_UIC		0x00000000
#endif
#ifndef CPM_VID2
#define CPM_VID2	0x00000000
#endif
#ifndef CPM_XPT27
#define CPM_XPT27	0x00000000
#endif
#ifndef CPM_XPT54
#define CPM_XPT54	0x00000000
#endif

#ifdef DCRN_CPMSR_BASE
#define DCRN_CPMSR	(DCRN_CPMSR_BASE + 0x0)	/* CPM Status */
#define DCRN_CPMER	(DCRN_CPMSR_BASE + 0x1)	/* CPM Enable */
#endif

#ifdef DCRN_DCP0_BASE
#define DCRN_DCP0_CFGADDR	(DCRN_DCP0_BASE + 0x0)	/* Decompression Controller Address */
#define DCRN_DCP0_CFGDATA	(DCRN_DCP0_BASE + 0x1)	/* Decompression Controller Data */
#endif

#ifdef DCRN_DCRX_BASE
#define DCRN_DCRXICR	(DCRN_DCRX_BASE + 0x0)	/* Internal Control Register */
#define DCRN_DCRXISR	(DCRN_DCRX_BASE + 0x1)	/* Internal Status Register */
#define DCRN_DCRXECR	(DCRN_DCRX_BASE + 0x2)	/* External Control Register */
#define DCRN_DCRXESR	(DCRN_DCRX_BASE + 0x3)	/* External Status Register */
#define DCRN_DCRXTAR	(DCRN_DCRX_BASE + 0x4)	/* Target Address Register */
#define DCRN_DCRXTDR	(DCRN_DCRX_BASE + 0x5)	/* Target Data Register */
#define DCRN_DCRXIGR	(DCRN_DCRX_BASE + 0x6)	/* Interrupt Generation Register */
#define DCRN_DCRXBCR	(DCRN_DCRX_BASE + 0x7)	/* Line Buffer Control Register */
#endif

#ifdef DCRN_DMA0_BASE
#define	DCRN_DMACR0	(DCRN_DMA0_BASE + 0x0)	/* DMA Channel Control Register 0 */
#define	DCRN_DMACT0	(DCRN_DMA0_BASE + 0x1)	/* DMA Count Register 0 */
#define	DCRN_DMADA0	(DCRN_DMA0_BASE + 0x2)	/* DMA Destination Address Register 0 */
#define	DCRN_DMASA0	(DCRN_DMA0_BASE + 0x3)	/* DMA Source Address Register 0 */
#ifdef DCRNCAP_DMA_CC
#define	DCRN_DMACC0	(DCRN_DMA0_BASE + 0x4)	/* DMA Chained Count Register 0 */
#endif

#ifdef DCRNCAP_DMA_SG
#define DCRN_ASG0	(DCRN_DMA0_BASE + 0x4)	/* DMA Scatter/Gather Descriptor Addr 0 */
#endif
#endif

#ifdef DCRN_DMA1_BASE
#define	DCRN_DMACR1	(DCRN_DMA1_BASE + 0x0)	/* DMA Channel Control Register 1 */
#define	DCRN_DMACT1	(DCRN_DMA1_BASE + 0x1)	/* DMA Count Register 1 */
#define	DCRN_DMADA1	(DCRN_DMA1_BASE + 0x2)	/* DMA Destination Address Register 1 */
#define	DCRN_DMASA1	(DCRN_DMA1_BASE + 0x3)	/* DMA Source Address Register 1 */

#ifdef DCRNCAP_DMA_CC
#define	DCRN_DMACC1	(DCRN_DMA1_BASE + 0x4)	/* DMA Chained Count Register 1 */
#endif
#ifdef DCRNCAP_DMA_SG
#define DCRN_ASG1	(DCRN_DMA1_BASE + 0x4)	/* DMA Scatter/Gather Descriptor Addr 1 */
#endif
#endif

#ifdef DCRN_DMA2_BASE
#define	DCRN_DMACR2	(DCRN_DMA2_BASE + 0x0)	/* DMA Channel Control Register 2 */
#define	DCRN_DMACT2	(DCRN_DMA2_BASE + 0x1)	/* DMA Count Register 2 */
#define	DCRN_DMADA2	(DCRN_DMA2_BASE + 0x2)	/* DMA Destination Address Register 2 */
#define	DCRN_DMASA2	(DCRN_DMA2_BASE + 0x3)	/* DMA Source Address Register 2 */
#ifdef DCRNCAP_DMA_CC
#define	DCRN_DMACC2	(DCRN_DMA2_BASE + 0x4)	/* DMA Chained Count Register 2 */
#endif
#ifdef DCRNCAP_DMA_SG
#define DCRN_ASG2	(DCRN_DMA2_BASE + 0x4)	/* DMA Scatter/Gather Descriptor Addr 2 */
#endif
#endif

#ifdef DCRN_DMA3_BASE
#define	DCRN_DMACR3	(DCRN_DMA3_BASE + 0x0)	/* DMA Channel Control Register 3 */
#define	DCRN_DMACT3	(DCRN_DMA3_BASE + 0x1)	/* DMA Count Register 3 */
#define	DCRN_DMADA3	(DCRN_DMA3_BASE + 0x2)	/* DMA Destination Address Register 3 */
#define	DCRN_DMASA3	(DCRN_DMA3_BASE + 0x3)	/* DMA Source Address Register 3 */
#ifdef DCRNCAP_DMA_CC
#define	DCRN_DMACC3	(DCRN_DMA3_BASE + 0x4)	/* DMA Chained Count Register 3 */
#endif
#ifdef DCRNCAP_DMA_SG
#define DCRN_ASG3	(DCRN_DMA3_BASE + 0x4)	/* DMA Scatter/Gather Descriptor Addr 3 */
#endif
#endif

#ifdef DCRN_DMASR_BASE
#define	DCRN_DMASR	(DCRN_DMASR_BASE + 0x0)	/* DMA Status Register */
#ifdef DCRNCAP_DMA_SG
#define DCRN_ASGC	(DCRN_DMASR_BASE + 0x3)	/* DMA Scatter/Gather Command */
/* don't know if these two registers always exist if scatter/gather exists */
#define DCRN_POL	(DCRN_DMASR_BASE + 0x6)	/* DMA Polarity Register */
#define DCRN_SLP	(DCRN_DMASR_BASE + 0x5)	/* DMA Sleep Register */
#endif
#endif

#ifdef DCRN_EBC_BASE
#define DCRN_EBCCFGADR	(DCRN_EBC_BASE + 0x0)	/* Peripheral Controller Address */
#define DCRN_EBCCFGDATA	(DCRN_EBC_BASE + 0x1)	/* Peripheral Controller Data */
#endif

#ifdef DCRN_EXIER_BASE
#define	DCRN_EXIER	(DCRN_EXIER_BASE + 0x0)	/* External Interrupt Enable Register */
#endif

#ifdef DCRN_EBIMC_BASE
#define DCRN_BRCRH0	(DCRN_EBIMC_BASE + 0x0) /* Bus Region Config High 0 */
#define DCRN_BRCRH1	(DCRN_EBIMC_BASE + 0x1) /* Bus Region Config High 1 */
#define DCRN_BRCRH2	(DCRN_EBIMC_BASE + 0x2) /* Bus Region Config High 2 */
#define DCRN_BRCRH3	(DCRN_EBIMC_BASE + 0x3) /* Bus Region Config High 3 */
#define DCRN_BRCRH4	(DCRN_EBIMC_BASE + 0x4) /* Bus Region Config High 4 */
#define DCRN_BRCRH5	(DCRN_EBIMC_BASE + 0x5) /* Bus Region Config High 5 */
#define DCRN_BRCRH6	(DCRN_EBIMC_BASE + 0x6) /* Bus Region Config High 6 */
#define DCRN_BRCRH7	(DCRN_EBIMC_BASE + 0x7) /* Bus Region Config High 7 */
#define DCRN_BRCR0	(DCRN_EBIMC_BASE + 0x10)/* BRC 0 */
#define DCRN_BRCR1	(DCRN_EBIMC_BASE + 0x11)/* BRC 1 */
#define DCRN_BRCR2	(DCRN_EBIMC_BASE + 0x12)/* BRC 2 */
#define DCRN_BRCR3	(DCRN_EBIMC_BASE + 0x13)/* BRC 3 */
#define DCRN_BRCR4	(DCRN_EBIMC_BASE + 0x14)/* BRC 4 */
#define DCRN_BRCR5	(DCRN_EBIMC_BASE + 0x15)/* BRC 5 */
#define DCRN_BRCR6	(DCRN_EBIMC_BASE + 0x16)/* BRC 6 */
#define DCRN_BRCR7	(DCRN_EBIMC_BASE + 0x17)/* BRC 7 */
#define DCRN_BEAR0	(DCRN_EBIMC_BASE + 0x20)/* Bus Error Address Register */
#define DCRN_BESR0	(DCRN_EBIMC_BASE + 0x21)/* Bus Error Status Register */
#define DCRN_BIUCR	(DCRN_EBIMC_BASE + 0x2A)/* Bus Interfac Unit Ctrl Reg */
#endif

#ifdef DCRN_EXISR_BASE
#define	DCRN_EXISR	(DCRN_EXISR_BASE + 0x0)	/* External Interrupt Status Register */
#endif
#define EXIER_CIE	0x80000000	/* Critical Interrupt Enable */
#define EXIER_SRIE	0x08000000	/* Serial Port Rx Int. Enable */
#define EXIER_STIE	0x04000000	/* Serial Port Tx Int. Enable */
#define EXIER_JRIE	0x02000000	/* JTAG Serial Port Rx Int. Enable */
#define EXIER_JTIE	0x01000000	/* JTAG Serial Port Tx Int. Enable */
#define EXIER_D0IE	0x00800000	/* DMA Channel 0 Interrupt Enable */
#define EXIER_D1IE	0x00400000	/* DMA Channel 1 Interrupt Enable */
#define EXIER_D2IE	0x00200000	/* DMA Channel 2 Interrupt Enable */
#define EXIER_D3IE	0x00100000	/* DMA Channel 3 Interrupt Enable */
#define EXIER_E0IE	0x00000010	/* External Interrupt 0 Enable */
#define EXIER_E1IE	0x00000008	/* External Interrupt 1 Enable */
#define EXIER_E2IE	0x00000004	/* External Interrupt 2 Enable */
#define EXIER_E3IE	0x00000002	/* External Interrupt 3 Enable */
#define EXIER_E4IE	0x00000001	/* External Interrupt 4 Enable */

#ifdef DCRN_IOCR_BASE
#define	DCRN_IOCR	(DCRN_IOCR_BASE + 0x0)	/* Input/Output Configuration Register */
#endif
#define IOCR_E0TE	0x80000000
#define IOCR_E0LP	0x40000000
#define IOCR_E1TE	0x20000000
#define IOCR_E1LP	0x10000000
#define IOCR_E2TE	0x08000000
#define IOCR_E2LP	0x04000000
#define IOCR_E3TE	0x02000000
#define IOCR_E3LP	0x01000000
#define IOCR_E4TE	0x00800000
#define IOCR_E4LP	0x00400000
#define IOCR_EDT	0x00080000
#define IOCR_SOR	0x00040000
#define IOCR_EDO	0x00008000
#define IOCR_2XC	0x00004000
#define IOCR_ATC	0x00002000
#define IOCR_SPD	0x00001000
#define IOCR_BEM	0x00000800
#define IOCR_PTD	0x00000400
#define IOCR_ARE	0x00000080
#define IOCR_DRC	0x00000020
#define IOCR_RDM(x)	(((x) & 0x3) << 3)
#define IOCR_TCS	0x00000004
#define IOCR_SCS	0x00000002
#define IOCR_SPC	0x00000001

#ifdef DCRN_MAL_BASE
#define DCRN_MALCR		(DCRN_MAL_BASE + 0x0) /* MAL Configuration */
#define DCRN_MALDBR		(DCRN_MAL_BASE + 0x3) /* Debug Register */
#define DCRN_MALESR		(DCRN_MAL_BASE + 0x1) /* Error Status */
#define DCRN_MALIER		(DCRN_MAL_BASE + 0x2) /* Interrupt Enable */
#define DCRN_MALTXCARR		(DCRN_MAL_BASE + 0x5) /* TX Channed Active Reset Register */
#define DCRN_MALTXCASR		(DCRN_MAL_BASE + 0x4) /* TX Channel Active Set Register */
#define DCRN_MALTXDEIR		(DCRN_MAL_BASE + 0x7) /* Tx Descriptor Error Interrupt */
#define DCRN_MALTXEOBISR	(DCRN_MAL_BASE + 0x6) /* Tx End of Buffer Interrupt Status  */
#define DCRN_MALRXCARR		(DCRN_MAL_BASE + 0x11) /* RX Channed Active Reset Register */
#define DCRN_MALRXCASR		(DCRN_MAL_BASE + 0x10) /* RX Channel Active Set Register */
#define DCRN_MALRXDEIR		(DCRN_MAL_BASE + 0x13) /* Rx Descriptor Error Interrupt */
#define DCRN_MALRXEOBISR	(DCRN_MAL_BASE + 0x12) /* Rx End of Buffer Interrupt Status  */
#define DCRN_MALRXCTP0R		(DCRN_MAL_BASE + 0x40) /* Channel Rx 0 Channel Table Pointer */
#define DCRN_MALTXCTP0R		(DCRN_MAL_BASE + 0x20) /* Channel Tx 0 Channel Table Pointer */
#define DCRN_MALTXCTP1R		(DCRN_MAL_BASE + 0x21) /* Channel Tx 1 Channel Table Pointer */
#define DCRN_MALRCBS0		(DCRN_MAL_BASE + 0x60) /* Channel Rx 0 Channel Buffer Size */
#endif
/* DCRN_MALCR */
#define MALCR_MMSR		0x80000000/* MAL Software reset */
#define MALCR_PLBP_1		0x00400000 /* MAL reqest priority: */
#define MALCR_PLBP_2		0x00800000 /* lowsest is 00 */
#define MALCR_PLBP_3		0x00C00000 /* highest */
#define MALCR_GA		0x00200000 /* Guarded Active Bit */
#define MALCR_OA		0x00100000 /* Ordered Active Bit */
#define MALCR_PLBLE		0x00080000 /* PLB Lock Error Bit */
#define MALCR_PLBLT_1		0x00040000 /* PLB Latency Timer */
#define MALCR_PLBLT_2		0x00020000
#define MALCR_PLBLT_3		0x00010000
#define MALCR_PLBLT_4		0x00008000
#define MALCR_PLBLT_DEFAULT	0x00078000 /* JSP: Is this a valid default?? */
#define MALCR_PLBB		0x00004000 /* PLB Burst Deactivation Bit */
#define MALCR_OPBBL		0x00000080 /* OPB Lock Bit */
#define MALCR_EOPIE		0x00000004 /* End Of Packet Interrupt Enable */
#define MALCR_LEA		0x00000002 /* Locked Error Active */
#define MALCR_MSD		0x00000001 /* MAL Scroll Descriptor Bit */
/* DCRN_MALESR */
#define MALESR_EVB		0x80000000 /* Error Valid Bit */
#define MALESR_CIDRX		0x40000000 /* Channel ID Receive */
#define MALESR_DE		0x00100000 /* Descriptor Error */
#define MALESR_OEN		0x00080000 /* OPB Non-Fullword Error */
#define MALESR_OTE		0x00040000 /* OPB Timeout Error */
#define MALESR_OSE		0x00020000 /* OPB Slave Error */
#define MALESR_PEIN		0x00010000 /* PLB Bus Error Indication */
#define MALESR_DEI		0x00000010 /* Descriptor Error Interrupt */
#define MALESR_ONEI		0x00000008 /* OPB Non-Fullword Error Interrupt */
#define MALESR_OTEI		0x00000004 /* OPB Timeout Error Interrupt */
#define MALESR_OSEI		0x00000002 /* OPB Slace Error Interrupt */
#define MALESR_PBEI		0x00000001 /* PLB Bus Error Interrupt */
/* DCRN_MALIER */
#define MALIER_DE		0x00000010 /* Descriptor Error Interrupt Enable */
#define MALIER_NE		0x00000008 /* OPB Non-word Transfer Int Enable */
#define MALIER_TE		0x00000004 /* OPB Time Out Error Interrupt Enable  */
#define MALIER_OPBE		0x00000002 /* OPB Slave Error Interrupt Enable */
#define MALIER_PLBE		0x00000001 /* PLB Error Interrupt Enable */
/* DCRN_MALTXEOBISR */
#define MALOBISR_CH0		0x80000000 /* EOB channel 1 bit */
#define MALOBISR_CH2		0x40000000 /* EOB channel 2 bit */

#ifdef DCRN_OCM0_BASE
#define DCRN_OCMISARC	(DCRN_OCM0_BASE + 0x0)	/* OCM Instr Side Addr Range Compare */
#define DCRN_OCMISCR	(DCRN_OCM0_BASE + 0x1)	/* OCM Instr Side Control */
#define DCRN_OCMDSARC	(DCRN_OCM0_BASE + 0x2)	/* OCM Data Side Addr Range Compare */
#define DCRN_OCMDSCR	(DCRN_OCM0_BASE + 0x3)	/* OCM Data Side Control */
#endif

#ifdef DCRN_PLB0_BASE
#define DCRN_PLB0_BESR	(DCRN_PLB0_BASE + 0x0)
#define DCRN_PLB0_BEAR	(DCRN_PLB0_BASE + 0x2)
/* doesn't exist on stb03xxx? */
#define DCRN_PLB0_ACR	(DCRN_PLB0_BASE + 0x3)
#endif

#ifdef DCRN_PLB1_BASE
#define DCRN_PLB1_BESR	(DCRN_PLB1_BASE + 0x0)
#define DCRN_PLB1_BEAR	(DCRN_PLB1_BASE + 0x1)
/* doesn't exist on stb03xxx? */
#define DCRN_PLB1_ACR	(DCRN_PLB1_BASE + 0x2)
#endif

#ifdef DCRN_PLLMR_BASE
#define DCRN_PLLMR	(DCRN_PLLMR_BASE + 0x0)	/* PL1 Mode */
#endif

#ifdef DCRN_POB0_BASE
#define DCRN_POB0_BESR0	(DCRN_POB0_BASE + 0x0)
#define DCRN_POB0_BEAR	(DCRN_POB0_BASE + 0x2)
#define DCRN_POB0_BESR1	(DCRN_POB0_BASE + 0x4)
#endif

#ifdef DCRN_SCCR_BASE
#define DCRN_SCCR	(DCRN_SCCR_BASE + 0x0)
#endif

#ifdef DCRN_SDRAM0_BASE
#define DCRN_SDRAM0_CFGADDR	(DCRN_SDRAM0_BASE + 0x0) /* Mem Ctrlr Address */
#define DCRN_SDRAM0_CFGDATA	(DCRN_SDRAM0_BASE + 0x1) /* Mem Ctrlr Data */
#endif

#ifdef DCRN_UIC0_BASE
#define DCRN_UIC0_SR	(DCRN_UIC0_BASE + 0x0)
#define DCRN_UIC0_ER	(DCRN_UIC0_BASE + 0x2)
#define DCRN_UIC0_CR	(DCRN_UIC0_BASE + 0x3)
#define DCRN_UIC0_PR	(DCRN_UIC0_BASE + 0x4)
#define DCRN_UIC0_TR	(DCRN_UIC0_BASE + 0x5)
#define DCRN_UIC0_MSR	(DCRN_UIC0_BASE + 0x6)
#define DCRN_UIC0_VR	(DCRN_UIC0_BASE + 0x7)
#define DCRN_UIC0_VCR	(DCRN_UIC0_BASE + 0x8)
#endif

#ifdef DCRN_UIC1_BASE
#define DCRN_UIC1_SR	(DCRN_UIC1_BASE + 0x0)
#define DCRN_UIC1_SRS	(DCRN_UIC1_BASE + 0x1)
#define DCRN_UIC1_ER	(DCRN_UIC1_BASE + 0x2)
#define DCRN_UIC1_CR	(DCRN_UIC1_BASE + 0x3)
#define DCRN_UIC1_PR	(DCRN_UIC1_BASE + 0x4)
#define DCRN_UIC1_TR	(DCRN_UIC1_BASE + 0x5)
#define DCRN_UIC1_MSR	(DCRN_UIC1_BASE + 0x6)
#define DCRN_UIC1_VR	(DCRN_UIC1_BASE + 0x7)
#define DCRN_UIC1_VCR	(DCRN_UIC1_BASE + 0x8)
#endif

#ifdef DCRN_SDRAM0_BASE
#define DCRN_SDRAM0_CFGADDR	(DCRN_SDRAM0_BASE + 0x0) /* Memory Controller Address */
#define DCRN_SDRAM0_CFGDATA	(DCRN_SDRAM0_BASE + 0x1) /* Memory Controller Data */
#endif

#ifdef DCRN_OCM0_BASE
#define DCRN_OCMISARC	(DCRN_OCM0_BASE + 0x0) /* OCM Instr Side Addr Range Compare */
#define DCRN_OCMISCR	(DCRN_OCM0_BASE + 0x1) /* OCM Instr Side Control */
#define DCRN_OCMDSARC	(DCRN_OCM0_BASE + 0x2) /* OCM Data Side Addr Range Compare */
#define DCRN_OCMDSCR	(DCRN_OCM0_BASE + 0x3) /* OCM Data Side Control */
#endif

#endif /* __ASM_IBM403_H__ */
#endif /* __KERNEL__ */
