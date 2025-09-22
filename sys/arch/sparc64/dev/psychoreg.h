/*	$OpenBSD: psychoreg.h,v 1.14 2021/03/11 11:17:00 jsg Exp $	*/
/*	$NetBSD: psychoreg.h,v 1.6.4.2 2001/09/13 01:14:40 thorpej Exp $ */

/*
 * Copyright (c) 1998, 1999 Eduardo E. Horvath
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
#ifndef _SPARC64_DEV_PSYCHOREG_H_
#define _SPARC64_DEV_PSYCHOREG_H_

/*
 * Sun4u PCI definitions.  Here's where we deal w/the machine
 * dependencies of psycho and the PCI controller on the UltraIIi.
 *
 * All PCI registers are bit-swapped, however they are not byte-swapped.
 * This means that they must be accessed using little-endian access modes,
 * either map the pages little-endian or use little-endian ASIs.
 *
 * PSYCHO implements two PCI buses, A and B.
 */

struct psychoreg {
	struct upareg {
		u_int64_t	upa_portid;	/* UPA port ID register */		/* 1fe.0000.0000 */
		u_int64_t	upa_config;	/* UPA config register */		/* 1fe.0000.0008 */
	} sys_upa;

	u_int64_t	psy_csr;		/* PSYCHO control/status register */	/* 1fe.0000.0010 */
	/* 
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

	u_int64_t	pad0;
	u_int64_t	psy_ecccr;		/* ECC control register */		/* 1fe.0000.0020 */
	u_int64_t	reserved;							/* 1fe.0000.0028 */
	u_int64_t	psy_ue_afsr;		/* Uncorrectable Error AFSR */		/* 1fe.0000.0030 */
	u_int64_t	psy_ue_afar;		/* Uncorrectable Error AFAR */		/* 1fe.0000.0038 */
	u_int64_t	psy_ce_afsr;		/* Correctable Error AFSR */		/* 1fe.0000.0040 */
	u_int64_t	psy_ce_afar;		/* Correctable Error AFAR */		/* 1fe.0000.0048 */

	u_int64_t	pad1[22];

	struct perfmon {
		u_int64_t	pm_cr;		/* Performance monitor control reg */	/* 1fe.0000.0100 */
		u_int64_t	pm_count;	/* Performance monitor counter reg */	/* 1fe.0000.0108 */
	} psy_pm;

	u_int64_t	pad2[30];

	struct iommureg psy_iommu;							/* 1fe.0000.0200,0210 */

	u_int64_t	pad3[256];

	u_int64_t	pcia_slot0_int;		/* PCI bus a slot 0 irq map reg */	/* 1fe.0000.0c00 */
	u_int64_t	pcia_slot1_int;		/* PCI bus a slot 1 irq map reg */	/* 1fe.0000.0c08 */
	u_int64_t	pcia_slot2_int;		/* PCI bus a slot 2 irq map reg (IIi)*/	/* 1fe.0000.0c10 */
	u_int64_t	pcia_slot3_int;		/* PCI bus a slot 3 irq map reg (IIi)*/	/* 1fe.0000.0c18 */
	u_int64_t	pcib_slot0_int;		/* PCI bus b slot 0 irq map reg */	/* 1fe.0000.0c20 */
	u_int64_t	pcib_slot1_int;		/* PCI bus b slot 1 irq map reg */	/* 1fe.0000.0c28 */
	u_int64_t	pcib_slot2_int;		/* PCI bus b slot 2 irq map reg */	/* 1fe.0000.0c30 */
	u_int64_t	pcib_slot3_int;		/* PCI bus b slot 3 irq map reg */	/* 1fe.0000.0c38 */

	u_int64_t	pad4[120];

	u_int64_t	scsi_int_map;		/* SCSI interrupt map reg */		/* 1fe.0000.1000 */
	u_int64_t	ether_int_map;		/* ethernet interrupt map reg */	/* 1fe.0000.1008 */
	u_int64_t	bpp_int_map;		/* parallel interrupt map reg */	/* 1fe.0000.1010 */
	u_int64_t	audior_int_map;		/* audio record interrupt map reg */	/* 1fe.0000.1018 */
	u_int64_t	audiop_int_map;		/* audio playback interrupt map reg */	/* 1fe.0000.1020 */
	u_int64_t	power_int_map;		/* power fail interrupt map reg */	/* 1fe.0000.1028 */
	u_int64_t	ser_kbd_ms_int_map;	/* serial/kbd/mouse interrupt map reg *//* 1fe.0000.1030 */
	u_int64_t	fd_int_map;		/* floppy interrupt map reg */		/* 1fe.0000.1038 */
	u_int64_t	spare_int_map;		/* spare interrupt map reg */		/* 1fe.0000.1040 */
	u_int64_t	kbd_int_map;		/* kbd [unused] interrupt map reg */	/* 1fe.0000.1048 */
	u_int64_t	mouse_int_map;		/* mouse [unused] interrupt map reg */	/* 1fe.0000.1050 */
	u_int64_t	serial_int_map;		/* second serial interrupt map reg */	/* 1fe.0000.1058 */
	u_int64_t	timer0_int_map;		/* timer 0 interrupt map reg */		/* 1fe.0000.1060 */
	u_int64_t	timer1_int_map;		/* timer 1 interrupt map reg */		/* 1fe.0000.1068 */
	u_int64_t	ue_int_map;		/* UE interrupt map reg */		/* 1fe.0000.1070 */
	u_int64_t	ce_int_map;		/* CE interrupt map reg */		/* 1fe.0000.1078 */
	u_int64_t	pciaerr_int_map;	/* PCI bus a error interrupt map reg */	/* 1fe.0000.1080 */
	u_int64_t	pciberr_int_map;	/* PCI bus b error interrupt map reg */	/* 1fe.0000.1088 */
	u_int64_t	pwrmgt_int_map;		/* power mgmt wake interrupt map reg */	/* 1fe.0000.1090 */
	u_int64_t	ffb0_int_map;		/* FFB0 graphics interrupt map reg */	/* 1fe.0000.1098 */
	u_int64_t	ffb1_int_map;		/* FFB1 graphics interrupt map reg */	/* 1fe.0000.10a0 */
	
	u_int64_t	pad5[107];

	/* Note: clear interrupt 0 registers are not really used */
	u_int64_t	pcia0_clr_int[4];	/* PCI a slot 0 clear int regs 0..7 */	/* 1fe.0000.1400-1418 */
	u_int64_t	pcia1_clr_int[4];	/* PCI a slot 1 clear int regs 0..7 */	/* 1fe.0000.1420-1438 */
	u_int64_t	pcia2_clr_int[4];	/* PCI a slot 2 clear int regs 0..7 */	/* 1fe.0000.1440-1458 */
	u_int64_t	pcia3_clr_int[4];	/* PCI a slot 3 clear int regs 0..7 */	/* 1fe.0000.1480-1478 */
	u_int64_t	pcib0_clr_int[4];	/* PCI b slot 0 clear int regs 0..7 */	/* 1fe.0000.1480-1498 */
	u_int64_t	pcib1_clr_int[4];	/* PCI b slot 1 clear int regs 0..7 */	/* 1fe.0000.14a0-14b8 */
	u_int64_t	pcib2_clr_int[4];	/* PCI b slot 2 clear int regs 0..7 */	/* 1fe.0000.14c0-14d8 */
	u_int64_t	pcib3_clr_int[4];	/* PCI b slot 3 clear int regs 0..7 */	/* 1fe.0000.14d0-14f8 */

	u_int64_t	pad6[96];

	u_int64_t	scsi_clr_int;		/* SCSI clear int reg */		/* 1fe.0000.1800 */
	u_int64_t	ether_clr_int;		/* ethernet clear int reg */		/* 1fe.0000.1808 */
	u_int64_t	bpp_clr_int;		/* parallel clear int reg */		/* 1fe.0000.1810 */
	u_int64_t	audior_clr_int;		/* audio record clear int reg */	/* 1fe.0000.1818 */
	u_int64_t	audiop_clr_int;		/* audio playback clear int reg */	/* 1fe.0000.1820 */
	u_int64_t	power_clr_int;		/* power fail clear int reg */		/* 1fe.0000.1828 */
	u_int64_t	ser_kb_ms_clr_int;	/* serial/kbd/mouse clear int reg */	/* 1fe.0000.1830 */
	u_int64_t	fd_clr_int;		/* floppy clear int reg */		/* 1fe.0000.1838 */
	u_int64_t	spare_clr_int;		/* spare clear int reg */		/* 1fe.0000.1840 */
	u_int64_t	kbd_clr_int;		/* kbd [unused] clear int reg */	/* 1fe.0000.1848 */
	u_int64_t	mouse_clr_int;		/* mouse [unused] clear int reg */	/* 1fe.0000.1850 */
	u_int64_t	serial_clr_int;		/* second serial clear int reg */	/* 1fe.0000.1858 */
	u_int64_t	timer0_clr_int;		/* timer 0 clear int reg */		/* 1fe.0000.1860 */
	u_int64_t	timer1_clr_int;		/* timer 1 clear int reg */		/* 1fe.0000.1868 */
	u_int64_t	ue_clr_int;		/* UE clear int reg */			/* 1fe.0000.1870 */
	u_int64_t	ce_clr_int;		/* CE clear int reg */			/* 1fe.0000.1878 */
	u_int64_t	pciaerr_clr_int;	/* PCI bus a error clear int reg */	/* 1fe.0000.1880 */
	u_int64_t	pciberr_clr_int;	/* PCI bus b error clear int reg */	/* 1fe.0000.1888 */
	u_int64_t	pwrmgt_clr_int;		/* power mgmt wake clr interrupt reg */	/* 1fe.0000.1890 */

	u_int64_t	pad7[45];

	u_int64_t	intr_retry_timer;	/* interrupt retry timer */		/* 1fe.0000.1a00 */

	u_int64_t	pad8[63];

	struct timer_counter {
		u_int64_t	tc_count;	/* timer/counter 0/1 count register */	/* 1fe.0000.1c00,1c10 */
		u_int64_t	tc_limit;	/* timer/counter 0/1 limit register */	/* 1fe.0000.1c08,1c18 */
	} tc[2];

	u_int64_t	pci_dma_write_sync;	/* PCI DMA write sync register (IIi) */	/* 1fe.0000.1c20 */

	u_int64_t	pad9[123];

	struct pci_ctl {
		u_int64_t	pci_csr;	/* PCI a/b control/status register */	/* 1fe.0000.2000,4000 */
		u_int64_t	pad10;
		u_int64_t	pci_afsr;	/* PCI a/b AFSR register */		/* 1fe.0000.2010,4010 */
		u_int64_t	pci_afar;	/* PCI a/b AFAR register */		/* 1fe.0000.2018,4018 */
		u_int64_t	pci_diag;	/* PCI a/b diagnostic register */	/* 1fe.0000.2020,4020 */
		u_int64_t	pci_tasr;	/* PCI target address space reg (IIi)*/	/* 1fe.0000.2028,4028 */

		u_int64_t	pad11[250];

		/* This is really the IOMMU's, not the PCI bus's */
		struct iommu_strbuf pci_strbuf;						/* 1fe.0000.2800-210 */
#define psy_iommu_strbuf psy_pcictl[0].pci_strbuf
		
		u_int64_t	pad12[765];
	} psy_pcictl[2];			/* For PCI a and b */

	/* NB: FFB0 and FFB1 intr map regs also appear at 1fe.0000.6000 and 1fe.0000.8000 respectively */
	u_int64_t	pad13[2048];

	u_int64_t	dma_scb_diag0;		/* DMA scoreboard diag reg 0 */		/* 1fe.0000.a000 */
	u_int64_t	dma_scb_diag1;		/* DMA scoreboard diag reg 1 */		/* 1fe.0000.a008 */

	u_int64_t	pad14[126];

	u_int64_t	iommu_svadiag;		/* IOMMU virtual addr diag reg */	/* 1fe.0000.a400 */
	u_int64_t	iommu_tlb_comp_diag;	/* IOMMU TLB tag compare diag reg */	/* 1fe.0000.a408 */
	
	u_int64_t	pad15[30];

	u_int64_t	iommu_queue_diag[16];	/* IOMMU LRU queue diag */		/* 1fe.0000.a500-a578 */
	u_int64_t	tlb_tag_diag[16];	/* TLB tag diag */			/* 1fe.0000.a580-a5f8 */
	u_int64_t	tlb_data_diag[16];	/* TLB data RAM diag */			/* 1fe.0000.a600-a678 */

	u_int64_t	pad16[48];

	u_int64_t	pci_int_diag;		/* PCI int state diag reg */		/* 1fe.0000.a800 */
	u_int64_t	obio_int_diag;		/* OBIO and misc int state diag reg */	/* 1fe.0000.a808 */

	u_int64_t	pad17[254];

	struct strbuf_diag {
		u_int64_t	strbuf_data_diag[128];	/* streaming buffer data RAM diag */	/* 1fe.0000.b000-b3f8 */
		u_int64_t	strbuf_error_diag[128];	/* streaming buffer error status diag *//* 1fe.0000.b400-b7f8 */
		u_int64_t	strbuf_pg_tag_diag[16];	/* streaming buffer page tag diag */	/* 1fe.0000.b800-b878 */
		u_int64_t	pad18[16];
		u_int64_t	strbuf_ln_tag_diag[16];	/* streaming buffer line tag diag */	/* 1fe.0000.b900-b978 */
		u_int64_t	pad19[208];
	} psy_strbufdiag[2];					/* For PCI a and b */

	u_int64_t	pad18[1036];

	u_int64_t	stick_cmp_low;		/* STICK comparison low reg */			/* 1fe.0000.f060 */
	u_int64_t	stick_cmp_high;		/* STICK comparison high reg */			/* 1fe.0000.f068 */
	u_int64_t	stick_reg_low;		/* STICK counter low reg */			/* 1fe.0000.f070 */
	u_int64_t	stick_reg_high;		/* STICK counter high reg */			/* 1fe.0000.f078 */

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
	 * NB: config and I/O space can use 1-4 byte accesses, not 8 byte
	 * accesses.  Memory space can use any sized accesses.
	 *
	 * Note that the SUNW,sabre/SUNW,simba combinations found on the
	 * Ultra5 and Ultra10 machines uses slightly different addresses
	 * than the above.  This is mostly due to the fact that the APB is
	 * a multi-function PCI device with two PCI bridges, and the U2P is
	 * two separate PCI bridges.  It uses the same PCI configuration
	 * space, though the configuration header for each PCI bus is
	 * located differently due to the SUNW,simba PCI busses being
	 * function 0 and function 1 of the APB, whereas the psycho's are
	 * each their own PCI device.  The I/O and memory spaces are each
	 * split into 8 equally sized areas (8x2MB blocks for I/O space,
	 * and 8x512MB blocks for memory space).  These are allocated in to
	 * either PCI A or PCI B, or neither in the APB's `I/O Address Map
	 * Register A/B' (0xde) and `Memory Address Map Register A/B' (0xdf)
	 * registers of each simba.  We must ensure that both of the
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
};

/* what the bits mean! */

/* uncorrectable error fault status */
#define	PSY_UEAFSR_PDRD	0x4000000000000000	/* primary pci dma read */
#define	PSY_UEAFSR_PDWR	0x2000000000000000	/* primary pci dma write */
#define	PSY_UEAFSR_SDRD	0x0800000000000000	/* secondary pci dma read */
#define	PSY_UEAFSR_SDWR	0x0400000000000000	/* secondary pci dma write */
#define	PSY_UEAFSR_SDTE	0x0200000000000000	/* secondary dma translation error */
#define	PSY_UEAFSR_PDTE	0x0100000000000000	/* primary dma translation error */
#define	PSY_UEAFSR_MASK	0x0000ffff00000000	/* byte mask */
#define	PSY_UEAFSR_OFF	0x00000000e0000000	/* offset (afar [5:3]) */
#define	PSY_UEAFSR_BLK	0x0000000000800000	/* block operation */

/* correctable error fault status */
#define	PSY_CEAFSR_PDRD	0x4000000000000000	/* primary pci dma read */
#define	PSY_CEAFSR_PDWR	0x2000000000000000	/* primary pci dma write */
#define	PSY_CEAFSR_SDRD	0x0800000000000000	/* secondary pci dma read */
#define	PSY_CEAFSR_SDWR	0x0400000000000000	/* secondary pci dma write */
#define	PSY_CEAFSR_SYND	0x00ff000000000000	/* ecc syndrome */
#define	PSY_CEAFSR_MASK	0x0000ffff00000000	/* byte mask */
#define	PSY_CEAFSR_OFF	0x00000000e0000000	/* offset (afar [5:3]) */
#define	PSY_CEAFSR_BLK	0x0000000000800000	/* block operation */

/* PCI [a|b] control/status register */
/* note that the sabre only has one set of PCI control/status registers */
#define	PCICTL_MRLM	0x0000001000000000	/* Memory Read Line/Multiple */
#define	PCICTL_SERR	0x0000000400000000	/* SERR asserted; W1C */
#define	PCICTL_ARB_PARK	0x0000000000200000	/* PCI arbitration parking */
#define	PCICTL_CPU_PRIO	0x0000000000100000	/* PCI arbitration parking */
#define	PCICTL_ARB_PRIO	0x00000000000f0000	/* PCI arbitration parking */
#define	PCICTL_ERRINTEN	0x0000000000000100	/* PCI error interrupt enable */
#define	PCICTL_RTRYWAIT 0x0000000000000080	/* PCI error interrupt enable */
#define	PCICTL_4ENABLE	0x000000000000000f	/* enable 4 PCI slots */
#define	PCICTL_6ENABLE	0x000000000000003f	/* enable 6 PCI slots */

/* PCI [a|b] afsr */
#define	PSY_PCIAFSR_PMA		0x8000000000000000	/* primary master abort */
#define	PSY_PCIAFSR_PTA		0x4000000000000000	/* primary target abort */
#define	PSY_PCIAFSR_PTRY	0x2000000000000000	/* primary excessive retry */
#define	PSY_PCIAFSR_PPERR	0x1000000000000000	/* primary parity error */
#define	PSY_PCIAFSR_SMA		0x0800000000000000	/* secondary master abort */
#define	PSY_PCIAFSR_STA		0x0400000000000000	/* secondary target abort */
#define	PSY_PCIAFSR_STRY	0x0200000000000000	/* secondary excessive retry */
#define	PSY_PCIAFSR_SPERR	0x0100000000000000	/* secondary parity error */
#define	PSY_PCIAFSR_RESV1	0x00ff000000000000	/* reserved */
#define	PSY_PCIAFSR_BMSK	0x0000ffff00000000	/* byte mask of transfer */
#define	PSY_PCIAFSR_BLK		0x0000000080000000	/* block operation */
#define	PSY_PCIAFSR_RESV2	0x0000000040000000	/* reserved */
#define	PSY_PCIAFSR_MID		0x000000003e000000	/* mid causing error */
#define	PSY_PCIAFSR_RESV3	0x0000000001ffffff	/* reserved */

/* performance counter control */
#define	PSY_PMCR_CLR1		0x0000000000008000	/* clear cnt 1 */
#define	PSY_PMCR_SEL1		0x0000000000001f00	/* set cnt 1 src */
#define	PSY_PMCR_CLR0		0x0000000000000080	/* clear cnt 0 */
#define	PSY_PMCR_SEL0		0x000000000000001f	/* set cnt 0 src */

#define	PSY_PMCRSEL_SDVRA	0x0000000000000000	/* stream dvma read, A */
#define	PSY_PMCRSEL_SDVWA	0x0000000000000001	/* stream dvma write, A */
#define	PSY_PMCRSEL_CDVRA	0x0000000000000002	/* consist dvma read, A */
#define	PSY_PMCRSEL_CDVWA	0x0000000000000003	/* consist dvma write, A */
#define	PSY_PMCRSEL_SBMA	0x0000000000000004	/* stream buf miss, A */
#define	PSY_PMCRSEL_DVA		0x0000000000000005	/* dvma cycles, A */
#define	PSY_PMCRSEL_DVWA	0x0000000000000006	/* dvma words, A */
#define	PSY_PMCRSEL_PIOA	0x0000000000000007	/* pio cycles, A */
#define	PSY_PMCRSEL_SDVRB	0x0000000000000008	/* stream dvma read, B */
#define	PSY_PMCRSEL_SDVWB	0x0000000000000009	/* stream dvma write, B */
#define	PSY_PMCRSEL_CDVRB	0x000000000000000a	/* consist dvma read, B */
#define	PSY_PMCRSEL_CDVWB	0x000000000000000b	/* consist dvma write, B */
#define	PSY_PMCRSEL_SBMB	0x000000000000000c	/* stream buf miss, B */
#define	PSY_PMCRSEL_DVB		0x000000000000000d	/* dvma cycles, B */
#define	PSY_PMCRSEL_DVWB	0x000000000000000e	/* dvma words, B */
#define	PSY_PMCRSEL_PIOB	0x000000000000000f	/* pio cycles, B */
#define	PSY_PMCRSEL_TLBMISS	0x0000000000000010	/* tlb misses */
#define	PSY_PMCRSEL_NINTRS	0x0000000000000011	/* interrupts */
#define	PSY_PMCRSEL_INACK	0x0000000000000012	/* intr nacks */
#define	PSY_PMCRSEL_PIOR	0x0000000000000013	/* pio read xfers */
#define	PSY_PMCRSEL_PIOW	0x0000000000000014	/* pio write xfers */
#define	PSY_PMCRSEL_MERGE	0x0000000000000015	/* merge buffer xacts */
#define	PSY_PMCRSEL_TBLA	0x0000000000000016	/* tbl walk retries, A */
#define	PSY_PMCRSEL_STCA	0x0000000000000017	/* stc retries, A */
#define	PSY_PMCRSEL_TBLB	0x0000000000000018	/* tbl walk retries, B */
#define	PSY_PMCRSEL_STCB	0x0000000000000019	/* stc retries, B */

/*
 * these are the PROM structures we grovel
 */

/*
 * For the physical addresses split into 3 32 bit values, we decode
 * them like the following (IEEE1275 PCI Bus binding 2.0, 2.2.1.1
 * Numerical Representation):
 *
 * 	phys.hi cell:	npt000ss bbbbbbbb dddddfff rrrrrrrr
 * 	phys.mid cell:	hhhhhhhh hhhhhhhh hhhhhhhh hhhhhhhh
 * 	phys.lo cell:	llllllll llllllll llllllll llllllll
 *
 * where these bits affect the address' properties:
 *	n	not-relocatable
 *	p	prefetchable
 *	t	aliased (non-relocatable IO), below 1MB (memory) or
 *		below 64KB (reloc. IO)
 *	ss	address space code:
 *		00 - configuration space
 *		01 - I/O space
 *		10 - 32 bit memory space
 *		11 - 64 bit memory space
 *	bb..bb	8 bit bus number
 *	ddddd	5 bit device number
 *	fff	3 bit function number
 *	rr..rr	8 bit register number
 *	hh..hh	32 bit unsigned value
 *	ll..ll	32 bit unsigned value
 * the values of hh..hh and ll..ll are combined to form a larger number.
 *
 * For config space, we don't have to do much special.  For I/O space,
 * hh..hh must be zero, and if n == 0 ll..ll is the offset from the
 * start of I/O space, otherwise ll..ll is the I/O space.  For memory
 * space, hh..hh must be zero for the 32 bit space, and is the high 32
 * bits in 64 bit space, with ll..ll being the low 32 bits in both cases,
 * with offset handling being driver via `n == 0' as for I/O space.
 */

/* commonly used */
#define TAG2BUS(tag)	((tag) >> 16) & 0xff;
#define TAG2DEV(tag)	((tag) >> 11) & 0x1f;
#define TAG2FN(tag)	((tag) >> 8) & 0x7;

struct psycho_registers {
	u_int32_t	phys_hi;
	u_int32_t	phys_mid;
	u_int32_t	phys_lo;
	u_int32_t	size_hi;
	u_int32_t	size_lo;
};

struct psycho_ranges {
	u_int32_t	cspace;
	u_int32_t	child_hi;
	u_int32_t	child_lo;
	u_int32_t	phys_hi;
	u_int32_t	phys_lo;
	u_int32_t	size_hi;
	u_int32_t	size_lo;
};

struct psycho_interrupt_map {
	u_int32_t	phys_hi;
	u_int32_t	phys_mid;
	u_int32_t	phys_lo;
	u_int32_t	intr;
	int32_t		child_node;
	u_int32_t	child_intr;
};

struct psycho_interrupt_map_mask {
	u_int32_t	phys_hi;
	u_int32_t	phys_mid;
	u_int32_t	phys_lo;
	u_int32_t	intr;
};

#endif /* _SPARC64_DEV_PSYCHOREG_H_ */
