/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999 Matthew R. Green
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1998, 1999 Eduardo E. Horvath
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: psychoreg.h,v 1.14 2008/05/30 02:29:37 mrg Exp
 *
 * $FreeBSD$
 */

#ifndef _SPARC64_PCI_PSYCHOREG_H_
#define _SPARC64_PCI_PSYCHOREG_H_

/*
 * Sun4u PCI definitions.  Here's where we deal w/the machine
 * dependencies of Psycho and the PCI controller on the UltraIIi.
 *
 * All PCI registers are bit-swapped, however they are not byte-swapped.
 * This means that they must be accessed using little-endian access modes,
 * either map the pages little-endian or use little-endian ASIs.
 *
 * PSYCHO implements two PCI buses, A and B.
 */

#define	PSYCHO_NINTR		6

/*
 * Psycho register offsets
 *
 * NB: FFB0 and FFB1 intr map regs also appear at 0x6000 and 0x8000
 * respectively.
 */
#define	PSR_UPA_PORTID		0x0000	/* UPA port ID register */
#define	PSR_UPA_CONFIG		0x0008	/* UPA config register */
#define	PSR_CS			0x0010	/* PSYCHO control/status register */
#define	PSR_ECCC		0x0020	/* ECC control register */
#define	PSR_UE_AFS		0x0030	/* Uncorrectable Error AFSR */
#define	PSR_UE_AFA		0x0038	/* Uncorrectable Error AFAR */
#define	PSR_CE_AFS		0x0040	/* Correctable Error AFSR */
#define	PSR_CE_AFA		0x0048	/* Correctable Error AFAR */
#define	PSR_PM_CTL		0x0100	/* Performance monitor control reg */
#define	PSR_PM_COUNT		0x0108	/* Performance monitor counter reg */
#define	PSR_IOMMU		0x0200	/* IOMMU registers */
#define	PSR_PCIA0_INT_MAP	0x0c00	/* PCI bus a slot 0 irq map reg */
#define	PSR_PCIA1_INT_MAP	0x0c08	/* PCI bus a slot 1 irq map reg */
#define	PSR_PCIA2_INT_MAP	0x0c10	/* PCI bus a slot 2 irq map reg (IIi) */
#define	PSR_PCIA3_INT_MAP	0x0c18	/* PCI bus a slot 3 irq map reg (IIi) */
#define	PSR_PCIB0_INT_MAP	0x0c20	/* PCI bus b slot 0 irq map reg */
#define	PSR_PCIB1_INT_MAP	0x0c28	/* PCI bus b slot 1 irq map reg */
#define	PSR_PCIB2_INT_MAP	0x0c30	/* PCI bus b slot 2 irq map reg */
#define	PSR_PCIB3_INT_MAP	0x0c38	/* PCI bus b slot 3 irq map reg */
#define	PSR_SCSI_INT_MAP	0x1000	/* SCSI interrupt map reg */
#define	PSR_ETHER_INT_MAP	0x1008	/* ethernet interrupt map reg */
#define	PSR_BPP_INT_MAP		0x1010	/* parallel interrupt map reg */
#define	PSR_AUDIOR_INT_MAP	0x1018	/* audio record interrupt map reg */
#define	PSR_AUDIOP_INT_MAP	0x1020	/* audio playback interrupt map reg */
#define	PSR_POWER_INT_MAP	0x1028	/* power fail interrupt map reg */
#define	PSR_SKBDMS_INT_MAP	0x1030	/* serial/kbd/mouse interrupt map reg */
#define	PSR_FD_INT_MAP		0x1038	/* floppy interrupt map reg */
#define	PSR_SPARE_INT_MAP	0x1040	/* spare interrupt map reg */
#define	PSR_KBD_INT_MAP		0x1048	/* kbd [unused] interrupt map reg */
#define	PSR_MOUSE_INT_MAP	0x1050	/* mouse [unused] interrupt map reg */
#define	PSR_SERIAL_INT_MAP	0x1058	/* second serial interrupt map reg */
#define	PSR_TIMER0_INT_MAP	0x1060	/* timer 0 interrupt map reg */
#define	PSR_TIMER1_INT_MAP	0x1068	/* timer 1 interrupt map reg */
#define	PSR_UE_INT_MAP		0x1070	/* UE interrupt map reg */
#define	PSR_CE_INT_MAP		0x1078	/* CE interrupt map reg */
#define	PSR_PCIAERR_INT_MAP	0x1080	/* PCI bus a error interrupt map reg */
#define	PSR_PCIBERR_INT_MAP	0x1088	/* PCI bus b error interrupt map reg */
#define	PSR_PWRMGT_INT_MAP	0x1090	/* power mgmt wake interrupt map reg */
#define	PSR_FFB0_INT_MAP	0x1098	/* FFB0 graphics interrupt map reg */
#define	PSR_FFB1_INT_MAP	0x10a0	/* FFB1 graphics interrupt map reg */
/* Note: Clear interrupt 0 registers are not really used. */
#define	PSR_PCIA0_INT_CLR	0x1400	/* PCI a slot 0 clear int regs 0..3 */
#define	PSR_PCIA1_INT_CLR	0x1420	/* PCI a slot 1 clear int regs 0..3 */
#define	PSR_PCIA2_INT_CLR	0x1440	/* PCI a slot 2 clear int regs 0..3 */
#define	PSR_PCIA3_INT_CLR	0x1460	/* PCI a slot 3 clear int regs 0..3 */
#define	PSR_PCIB0_INT_CLR	0x1480	/* PCI b slot 0 clear int regs 0..3 */
#define	PSR_PCIB1_INT_CLR	0x14a0	/* PCI b slot 1 clear int regs 0..3 */
#define	PSR_PCIB2_INT_CLR	0x14c0	/* PCI b slot 2 clear int regs 0..3 */
#define	PSR_PCIB3_INT_CLR	0x14d0	/* PCI b slot 3 clear int regs 0..3 */
#define	PSR_SCSI_INT_CLR	0x1800	/* SCSI clear int reg */
#define	PSR_ETHER_INT_CLR	0x1808	/* ethernet clear int reg */
#define	PSR_BPP_INT_CLR		0x1810	/* parallel clear int reg */
#define	PSR_AUDIOR_INT_CLR	0x1818	/* audio record clear int reg */
#define	PSR_AUDIOP_INT_CLR	0x1820	/* audio playback clear int reg */
#define	PSR_POWER_INT_CLR	0x1828	/* power fail clear int reg */
#define	PSR_SKBDMS_INT_CLR	0x1830	/* serial/kbd/mouse clear int reg */
#define	PSR_FD_INT_CLR		0x1838	/* floppy clear int reg */
#define	PSR_SPARE_INT_CLR	0x1840	/* spare clear int reg */
#define	PSR_KBD_INT_CLR		0x1848	/* kbd [unused] clear int reg */
#define	PSR_MOUSE_INT_CLR	0x1850	/* mouse [unused] clear int reg */
#define	PSR_SERIAL_INT_CLR	0x1858	/* second serial clear int reg */
#define	PSR_TIMER0_INT_CLR	0x1860	/* timer 0 clear int reg */
#define	PSR_TIMER1_INT_CLR	0x1868	/* timer 1 clear int reg */
#define	PSR_UE_INT_CLR		0x1870	/* UE clear int reg */
#define	PSR_CE_INT_CLR		0x1878	/* CE clear int reg */
#define	PSR_PCIAERR_INT_CLR	0x1880	/* PCI bus a error clear int reg */
#define	PSR_PCIBERR_INT_CLR	0x1888	/* PCI bus b error clear int reg */
#define	PSR_PWRMGT_INT_CLR	0x1890	/* power mgmt wake clr interrupt reg */
#define	PSR_INTR_RETRY_TIM	0x1a00	/* interrupt retry timer */
#define	PSR_TC0			0x1c00	/* timer/counter 0 */
#define	PSR_TC1			0x1c10	/* timer/counter 1 */
#define	PSR_DMA_WRITE_SYNC	0x1c20	/* PCI DMA write sync register (IIi) */
#define	PSR_PCICTL0		0x2000	/* PCICTL registers for 1st Psycho */
#define	PSR_PCICTL1		0x4000	/* PCICTL registers for 2nd Psycho */
#define	PSR_DMA_SCB_DIAG0	0xa000	/* DMA scoreboard diag reg 0 */
#define	PSR_DMA_SCB_DIAG1	0xa008	/* DMA scoreboard diag reg 1 */
#define	PSR_IOMMU_SVADIAG	0xa400	/* IOMMU virtual addr diag reg */
#define	PSR_IOMMU_TLB_CMP_DIAG	0xa408	/* IOMMU TLB tag compare diag reg */
#define	PSR_IOMMU_QUEUE_DIAG	0xa500	/* IOMMU LRU queue diag regs 0..15 */
#define	PSR_IOMMU_TLB_TAG_DIAG	0xa580	/* TLB tag diag regs 0..15 */
#define	PSR_IOMMU_TLB_DATA_DIAG	0xa600	/* TLB data RAM diag regs 0..15 */
#define	PSR_PCI_INT_DIAG	0xa800	/* PCI int state diag reg */
#define	PSR_OBIO_INT_DIAG	0xa808	/* OBIO and misc int state diag reg */
#define	PSR_STRBUF_DIAG		0xb000	/* Streaming buffer diag regs */

/*
 * Here is the rest of the map, which we're not specifying:
 *
 * 1fe.0100.0000 - 1fe.01ff.ffff	PCI configuration space
 * 1fe.0100.0000 - 1fe.0100.00ff	PCI B configuration header
 * 1fe.0101.0000 - 1fe.0101.00ff	PCI A configuration header
 * 1fe.0200.0000 - 1fe.0200.ffff	PCI A I/O space
 * 1fe.0201.0000 - 1fe.0201.ffff	PCI B I/O space
 * 1ff.0000.0000 - 1ff.7fff.ffff	PCI A memory space
 * 1ff.8000.0000 - 1ff.ffff.ffff	PCI B memory space
 *
 * NB: Config and I/O space can use 1-4 byte accesses, not 8 byte
 * accesses.  Memory space can use any sized accesses.
 *
 * Note that the SUNW,sabre/SUNW,simba combinations found on the
 * Ultra5 and Ultra10 machines uses slightly differrent addresses
 * than the above.  This is mostly due to the fact that the APB is
 * a multi-function PCI device with two PCI bridges, and the U2P is
 * two separate PCI bridges.  It uses the same PCI configuration
 * space, though the configuration header for each PCI bus is
 * located differently due to the SUNW,simba PCI busses being
 * function 0 and function 1 of the APB, whereas the Psycho's are
 * each their own PCI device.  The I/O and memory spaces are each
 * split into 8 equally sized areas (8x2MB blocks for I/O space,
 * and 8x512MB blocks for memory space).  These are allocated in to
 * either PCI A or PCI B, or neither in the APB's `I/O Address Map
 * Register A/B' (0xde) and `Memory Address Map Register A/B' (0xdf)
 * registers of each Simba.  We must ensure that both of the
 * following are correct (the prom should do this for us):
 *
 *    (PCI A Memory Address Map) & (PCI B Memory Address Map) == 0
 *
 *    (PCI A I/O Address Map) & (PCI B I/O Address Map) == 0
 *
 * 1fe.0100.0000 - 1fe.01ff.ffff	PCI configuration space
 * 1fe.0100.0800 - 1fe.0100.08ff	PCI B configuration header
 * 1fe.0100.0900 - 1fe.0100.09ff	PCI A configuration header
 * 1fe.0200.0000 - 1fe.02ff.ffff	PCI I/O space (divided)
 * 1ff.0000.0000 - 1ff.ffff.ffff	PCI memory space (divided)
 */

/*
 * PSR_CS defines:
 *
 * 63     59     55     50     45     4        3       2     1      0
 * +------+------+------+------+--//---+--------+-------+-----+------+
 * | IMPL | VERS | MID  | IGN  |  xxx  | APCKEN | APERR | IAP | MODE |
 * +------+------+------+------+--//---+--------+-------+-----+------+
 *
 */
#define PSYCHO_GCSR_IMPL(csr)	((u_int)(((csr) >> 60) & 0xf))
#define PSYCHO_GCSR_VERS(csr)	((u_int)(((csr) >> 56) & 0xf))
#define PSYCHO_GCSR_MID(csr)	((u_int)(((csr) >> 51) & 0x1f))
#define PSYCHO_GCSR_IGN(csr)	((u_int)(((csr) >> 46) & 0x1f))
#define PSYCHO_CSR_APCKEN	8	/* UPA addr parity check enable */
#define PSYCHO_CSR_APERR	4	/* UPA addr parity error */
#define PSYCHO_CSR_IAP		2	/* invert UPA address parity */
#define PSYCHO_CSR_MODE		1	/* UPA/PCI handshake */

/* Offsets into the PSR_PCICTL* register block */
#define	PCR_CS			0x0000	/* PCI control/status register */
#define	PCR_AFS			0x0010	/* PCI AFSR register */
#define	PCR_AFA			0x0018	/* PCI AFAR register */
#define	PCR_DIAG		0x0020	/* PCI diagnostic register */
#define	PCR_TAS			0x0028	/* PCI target address space reg (IIi) */
#define	PCR_STRBUF		0x0800	/* IOMMU streaming buffer registers. */

/* INO defines */
#define	PSYCHO_MAX_INO		0x3f

/* Device space defines */
#define	PSYCHO_CONF_SIZE	0x1000000
#define	PSYCHO_CONF_BUS_SHIFT	16
#define	PSYCHO_CONF_DEV_SHIFT	11
#define	PSYCHO_CONF_FUNC_SHIFT	8
#define	PSYCHO_CONF_REG_SHIFT	0
#define	PSYCHO_IO_SIZE		0x1000000
#define	PSYCHO_MEM_SIZE		0x100000000

#define	PSYCHO_CONF_OFF(bus, slot, func, reg)				\
	(((bus) << PSYCHO_CONF_BUS_SHIFT) |				\
	((slot) << PSYCHO_CONF_DEV_SHIFT) |				\
	((func) << PSYCHO_CONF_FUNC_SHIFT) |				\
	((reg) << PSYCHO_CONF_REG_SHIFT))

/* what the bits mean! */

/*
 * PCI [a|b] control/status register
 * Note that the Hummingbird/Sabre only has one set of PCI control/status
 * registers.
 */
#define	PCICTL_SBHERR	0x0000000800000000	/* strm. byte hole error; W1C */
#define	PCICTL_SERR	0x0000000400000000	/* SERR asserted; W1C */
#define	PCICTL_PCISPEED	0x0000000200000000	/* 0:half 1:full bus speed */
#define	PCICTL_ARB_PARK	0x0000000000200000	/* PCI arbitration parking */
#define	PCICTL_SBHINTEN	0x0000000000000400	/* strm. byte hole int. en. */
#define	PCICTL_WAKEUPEN	0x0000000000000200	/* power mgmt. wakeup enable */
#define	PCICTL_ERRINTEN	0x0000000000000100	/* PCI error interrupt enable */
#define	PCICTL_ARB_4	0x000000000000000f	/* DVMA arb. 4 PCI slots mask */
#define	PCICTL_ARB_6	0x000000000000003f	/* DVMA arb. 6 PCI slots mask */
/* The following are Hummingbird/Sabre only. */
#define	PCICTL_MRLM	0x0000001000000000	/* Memory Read Line/Multiple */
#define	PCICTL_CPU_PRIO	0x0000000000100000	/* CPU extra arb. prio. en. */
#define	PCICTL_ARB_PRIO	0x00000000000f0000	/* PCI extra arb. prio. en. */
#define	PCICTL_RTRYWAIT 0x0000000000000080	/* 0:wait 1:retry DMA write */

/* Uncorrectable error asynchronous fault status register */
#define	UEAFSR_BLK	(1UL << 23)	/* Error caused by block transaction */
#define	UEAFSR_P_DTE	(1UL << 56)	/* Pri. DVMA translation error */
#define	UEAFSR_S_DTE	(1UL << 57)	/* Sec. DVMA translation error */
#define	UEAFSR_S_DWR	(1UL << 58)	/* Sec. error during DVMA write */
#define	UEAFSR_S_DRD	(1UL << 59)	/* Sec. error during DVMA read */
#define	UEAFSR_S_PIO	(1UL << 60)	/* Sec. error during PIO access */
#define	UEAFSR_P_DWR	(1UL << 61)	/* Pri. error during DVMA write */
#define	UEAFSR_P_DRD	(1UL << 62)	/* Pri. error during DVMA read */
#define	UEAFSR_P_PIO	(1UL << 63)	/* Pri. error during PIO access */

/* Correctable error asynchronous fault status register */
#define	CEAFSR_BLK	(1UL << 23)	/* Error caused by block transaction */
#define	CEAFSR_S_DWR	(1UL << 58)	/* Sec. error caused by DVMA write */
#define	CEAFSR_S_DRD	(1UL << 59)	/* Sec. error caused by DVMA read */
#define	CEAFSR_S_PIO	(1UL << 60)	/* Sec. error caused by PIO access */
#define	CEAFSR_P_DWR	(1UL << 61)	/* Pri. error caused by DVMA write */
#define	CEAFSR_P_DRD	(1UL << 62)	/* Pri. error caused by DVMA read */
#define	CEAFSR_P_PIO	(1UL << 63)	/* Pri. error caused by PIO access */

/* PCI asynchronous fault status register */
#define	PCIAFSR_P_MA	(1UL << 63)	/* Pri. master abort */
#define	PCIAFSR_P_TA	(1UL << 62)	/* Pri. target abort */
#define	PCIAFSR_P_RTRY	(1UL << 61)	/* Pri. excessive retries */
#define	PCIAFSR_P_RERR	(1UL << 60)	/* Pri. parity error */
#define	PCIAFSR_S_MA	(1UL << 59)	/* Sec. master abort */
#define	PCIAFSR_S_TA	(1UL << 58)	/* Sec. target abort */
#define	PCIAFSR_S_RTRY	(1UL << 57)	/* Sec. excessive retries */
#define	PCIAFSR_S_RERR	(1UL << 56)	/* Sec. parity error */
#define	PCIAFSR_BMASK	(0xffffUL << 32)/* Bytemask of failed pri. transfer */
#define	PCIAFSR_BLK	(1UL << 31)	/* failed pri. transfer was block r/w */
#define	PCIAFSR_MID	(0x3eUL << 25)	/* UPA MID causing error transaction */

/* PCI diagnostic register */
#define	DIAG_RTRY_DIS	0x0000000000000040	/* dis. retry limit */
#define	DIAG_ISYNC_DIS	0x0000000000000020	/* dis. DMA write / int sync */
#define	DIAG_DWSYNC_DIS	0x0000000000000010	/* dis. DMA write / PIO sync */

/* Definitions for the target address space register */
#define	PCITAS_ADDR_SHIFT	29

/* Definitions for the Psycho configuration space */
#define	PCS_DEVICE	0		/* Device number of Psycho CS entry */
#define	PCS_FUNC	0		/* Function number of Psycho CS entry */

/* Non-Standard registers in the configration space */
#define	PCSR_SECBUS	0x40		/* Secondary bus number register */
#define	PCSR_SUBBUS	0x41		/* Subordinate bus number register */

/* Width of the physical addresses the IOMMU translates to */
#define	PSYCHO_IOMMU_BITS	41
#define	SABRE_IOMMU_BITS	34

#endif /* !_SPARC64_PCI_PSYCHOREG_H_ */
