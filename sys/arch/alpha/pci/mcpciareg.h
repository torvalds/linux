/* $OpenBSD: mcpciareg.h,v 1.3 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: mcpciareg.h,v 1.7 2006/05/17 21:32:59 drochner Exp $ */

/*
 * Copyright (c) 1998 by Matthew Jacob
 * NASA AMES Research Center.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Taken from:
 *
 *	``RAWHIDE Systems Programmer's Manual, Revision 1.4''
 */

#define	REGVAL(r)	(*(volatile int32_t *)ALPHA_PHYS_TO_K0SEG(r))

/*
 * There are 4 possible PCI busses per MCBUS.
 *
 * (from mcpcia.h, Digital Unix 4.0E):
 *
 * I/O Space Per PCI Node (8GBytes per)
 * ------------------------------------
 * (8+x)8 0000 0000 - (8+x)9 FFFF FFFF  - I/O Space for PCI0
 * (8+x)A 0000 0000 - (8+x)B FFFF FFFF  - I/O Space for PCI1
 * (8+x)C 0000 0000 - (8+x)D FFFF FFFF  - I/O Space for PCI2
 * (8+x)E 0000 0000 - (8+x)F FFFF FFFF  - I/O Space for PCI3
 *
 * CPU to PCI Address Mapping:
 * ---------------------------
 *
 * +---+-------+-------+--+--+--+--+--+--+---------------+----------+-----+
 * | 1 |  GID  |  MID  |  |  |  |  |  |  | Byte Aligned  | Byte Len | Zero|
 * |   |       |       |  |  |  |  |  |  | I/O Address   |   Field  |     |
 * +---+-------+-------+--+--+--+--+--+--+---------------+----------+-----+
 *  39  38   36 35   33 32 31 30 29 28 27 26            5 4        3  2  0
 *
 * <39> - I/O Select (Always 1 for direct I/O access)
 *
 * <38-36> - Global Bus slot # (MCBUS #)
 *      GID slot #0->7 (MCBUS #0->7)
 *
 * <35-33> - MCBUS Slot #
 *      MCBUS slot 0->7
 *
 * <32-27> - PCI Address Space
 *      0.xxxxx = Sparse Memory Space   (  4GB on MCBUS; 128MB on PCI)
 *      1.0xxxx = Dense Memory Space    (  2GB on MCBUS;   2GB on PCI)
 *      1.10xxx = Sparse IO Space       (  1GB on MCBUS;  32MB on PCI)
 *      1.110xx = Sparse Config Space   (512MB on MCBUS;  16MB on PCI)
 *      1.1110x = PCI Bridge CSR Space  (256MB on MCBUS) -- Sparse-mapped!
 *      1.11110 = Interrupt Acknowledge (128MB on MCBUS)
 *      1.11111 = Unused                (128MB on MCBUS)
 *
 * ------------------------------------------------------------
 * CPU to PCI Address Mapping for MCBUS-PCIy Bridge on MCBUS x:
 * ------------------------------------------------------------
 *
 * CPU Address Range            PCI Address Range       PCI Address Space
 * ------------------------     ---------------------   ------------------------
 * (8+x)(8+y*2).0000.0000       0000.0000 - 00FF.FFFF   PCIy Sparse Memory Space
 * - (8+x)(8+y*2).1FFF.FFFF                              (fixed, lower 16MB)
 *
 * (8+x)(8+y*2).2000.0000       0100.0000 - 07FF.FFFF   PCIy Sparse Memory Space
 * - (8+x)(8+y*2).FFFF.FFFF                              (variable, offset = 0)
 *
 * (8+x)(9+y*2).0000.0000       0000.0000 - 7FFF.FFFF   PCIy Dense Memory Space
 * - (8+x)(9+y*2).7FFF.FFFF  or 8000.0000 - FFFF.FFFF      if HAE_DENSE_MEM = 1
 *
 * (8+x)(9+y*2).8000.0000       0000.0000 - 0000.FFFF   PCIy Sparse IO Space
 * - (8+x)(9+y*2).801F.FFFF                              (fixed, lower 64K)
 *
 * (8+x)(9+y*2).8020.0000       0001.0000 - 01FF.FFFF   PCIy Sparse IO Space
 * - (8+x)(9+y*2).BFFF.FFFF                              (variable, offset = 0)
 *
 * (8+x)(9+y*2).C000.0000       0000.0000 - 0FFF.FFFF   PCIy Config Space (16MB)
 * - (8+x)(9+y*2).DFFF.FFFF
 *
 * (8+x)(9+y*2).E000.0000               N/A             PCIy-Bridge CSR Space
 *							(8MB)
 * - (8+x)(9+y*2).EFFF.FFFF
 *
 * (8+x)(9+y*2).F000.0000               N/A             Unused
 * - (8+x)(9+y*2).F000.3EFF
 *
 * (8+x)(9+y*2).F000.3F00,              N/A             PCIy Interrupt ACK0
 * (8+x)(9+y*2).F000.3F40                               PCIy Interrupt ACK1
 *
 * (8+x)(9+y*2).F000.3F80               N/A             Unused
 * - (8+x)(9+y*2).FFFF.FFFF
 *
 */

/*
 * MC-PCI Bus Bridge CSRs
 *
 * Address Map Overview:
 *
 * Offset                Selected Space
 * ----------------      -------------------------------------------------
 * 0x00000000            General config, control, diag, error logging regs.
 * 0x00001000            PCI Error Status
 * 0x00001300            PCI Scatter/Gather Regs.
 * 0x00001800            Scatter/Gather TLB Regs.
 * 0x00004000            MDPA Error Status & Diagnostic Control
 * 0x00008000            MDPB Error Status & Diagnostic Control
 * 0x000E0000 -          Flash Rom Space --
 * 0x000FFFFF            offset address into PCI Dense Mem Space
 * 0x10003F00            Interrupt Acknowledge
 *
 */


/*
 * Address Space Cookies
 */

#define	MCPCIA_PCI_SPARSE	0x000000000UL
#define	MCPCIA_PCI_DENSE	0x100000000UL
#define	MCPCIA_PCI_IOSPACE	0x180000000UL
#define	MCPCIA_PCI_CONF		0x1C0000000UL
#define	MCPCIA_PCI_BRIDGE	0x1E0000000UL
#define	MCPCIA_PCI_IACK		0x1F0000000UL

/*
 * MCPCIA Bus Bridge Registers
 *
 * These are offsets that don't include GBUS, MID, or address space offsets.
 */

#define	_MCPCIA_PCI_REV		0x000000000	/* PCI Revision Register (R) */
#define	_MCPCIA_WHOAMI		0x000000040	/* PCI Who Am I (R) */
#define	_MCPCIA_PCI_LAT		0x000000080	/* PCI Latency Timer (RW) */
#define	_MCPCIA_CAP_CTRL	0x000000100	/* PCI Bridge Control (RW) */
#define	_MCPCIA_HAE_MEM		0x000000400	/* PCI HAE Sparse Memory (RW) */
#define	_MCPCIA_HAE_IO		0x000000440	/* PCI HAE Sparse I/O (RW) */
#define	_MCPCIA_IACK_SC		0x000000480	/* PCI Special Cycle Ack */
#define	_MCPCIA_HAE_DENSE	0x0000004C0	/* PCI HAE Dense Memory (RW) */

#define	_MCPCIA_INT_CTL		0x000000500	/* PCI Interrupt Control */
#define	_MCPCIA_INT_REQ		0x000000540	/* PCI Interrupt Request */
#define	_MCPCIA_INT_TARG	0x000000580	/* PCI Int Tgt Devices */
#define	_MCPCIA_INT_ADR		0x0000005C0	/* PCI Int Tgt Address */
#define	_MCPCIA_INT_ADR_EXT	0x000000600	/* PCI Int Tgt Addr Ext */
#define	_MCPCIA_INT_MASK0	0x000000640	/* PCI Int Mask 0 */
#define	_MCPCIA_INT_MASK1	0x000000680	/* PCI Int Mask 1 */

#define	_MCPCIA_INT_ACK0	0x100003F00	/* PCI Int Ack 0 */
#define	_MCPCIA_INT_ACK1	0x100003F40	/* PCI Int Ack 1 */

#define	_MCPCIA_PERF_MON	0x000000300	/* PCI Perf Monitor */
#define	_MCPCIA_PERF_CONT	0x000000340	/* PCI Perf Monitor Control */

#define	_MCPCIA_CAP_DIAG	0x000000700	/* MC-PCI Diagnostic Control */
#define	_MCPCIA_SCRATCH0	0x000000740	/* Diag General */
#define	_MCPCIA_SCRATCH1	0x000000780	/* Diag General */
#define	_MCPCIA_TOM		0x0000007C0	/* Top Of Memory */
#define	_MCPCIA_MC_ERR0		0x000000800	/* MC Err Info 0 */
#define	_MCPCIA_MC_ERR1		0x000000840	/* MC Err Info 1 */
#define	_MCPCIA_CAP_ERR		0x000000880	/* CAP Error Register */

#define	_MCPCIA_PCI_ERR1	0x000001040	/* PCI Error Status */

#define	_MCPCIA_MDPA_STAT	0x000004000	/* MDPA Status */
#define	_MCPCIA_MDPA_SYN	0x000004040	/* MDPA Syndrome */
#define	_MCPCIA_MDPA_DIAG	0x000004080	/* Diag Check MDPA */

#define	_MCPCIA_MDPB_STAT	0x000008000	/* MDPB Status */
#define	_MCPCIA_MDPB_SYN	0x000008040	/* MDPB Syndrome */
#define	_MCPCIA_MDPB_DIAG	0x000008080	/* Diag Check MDPB */

#define	_MCPCIA_SG_TBIA		0x000001300	/* Scatter/Gather TBIA */
#define	_MCPCIA_HBASE		0x000001340	/* PC "Hole" Compatibility */
#define	_MCPCIA_W0_BASE		0x000001400	/* Window Base 0 */
#define	_MCPCIA_W0_MASK		0x000001440	/* Window Mask 0 */
#define	_MCPCIA_T0_BASE		0x000001480	/* Translated Base 0 */
#define	_MCPCIA_W1_BASE		0x000001500	/* Window Base 1 */
#define	_MCPCIA_W1_MASK		0x000001540	/* Window Mask 1 */
#define	_MCPCIA_T1_BASE		0x000001580	/* Translated Base 1 */
#define	_MCPCIA_W2_BASE		0x000001600	/* Window Base 2 */
#define	_MCPCIA_W2_MASK		0x000001640	/* Window Mask 2 */
#define	_MCPCIA_T2_BASE		0x000001680	/* Translated Base 2 */
#define	_MCPCIA_W3_BASE		0x000001700	/* Window Base 3 */
#define	_MCPCIA_W3_MASK		0x000001740	/* Window Mask 3 */
#define	_MCPCIA_T3_BASE		0x000001780	/* Translated Base 3 */
#define	_MCPCIA_W_DAC		0x0000017C0	/* Window DAC Base */


/*
 * Handier defines- uses precalculated offset in softc.
 */
#define	_SYBRIDGE(ccp)	((ccp)->cc_sysbase | MCPCIA_PCI_BRIDGE)

#define	MCPCIA_PCI_REV(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_PCI_REV)
#define	MCPCIA_WHOAMI(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_WHOAMI)
#define	MCPCIA_PCI_LAT(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_PCI_LAT)
#define	MCPCIA_CAP_CTRL(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_CAP_CTRL)
#define	MCPCIA_HAE_MEM(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_HAE_MEM)
#define	MCPCIA_HAE_IO(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_HAE_IO)
#define	MCPCIA_IACK_SC(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_IACK_SC)
#define	MCPCIA_HAE_DENSE(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_HAE_DENSE)
#define	MCPCIA_INT_CTL(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_INT_CTL)
#define	MCPCIA_INT_REQ(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_INT_REQ)
#define	MCPCIA_INT_TARG(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_INT_TARG)
#define	MCPCIA_INT_ADR(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_INT_ADR)
#define	MCPCIA_INT_ADR_EXT(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_INT_ADR_EXT)
#define	MCPCIA_INT_MASK0(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_INT_MASK0)
#define	MCPCIA_INT_MASK1(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_INT_MASK1)
#define	MCPCIA_INT_ACK0(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_INT_ACK0)
#define	MCPCIA_INT_ACK1(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_INT_ACK1)
#define	MCPCIA_PERF_MON(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_PERF_MON)
#define	MCPCIA_PERF_CONT(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_PERF_CONT)
#define	MCPCIA_CAP_DIAG(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_CAP_DIAG)
#define	MCPCIA_SCRATCH0(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_SCRATCH0)
#define	MCPCIA_SCRATCH1(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_SCRATCH1)
#define	MCPCIA_TOM(ccp)		(_SYBRIDGE(ccp) | _MCPCIA_TOM)
#define	MCPCIA_MC_ERR0(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_MC_ERR0)
#define	MCPCIA_MC_ERR1(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_MC_ERR1)
#define	MCPCIA_CAP_ERR(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_CAP_ERR)
#define	MCPCIA_PCI_ERR1(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_PCI_ERR1)
#define	MCPCIA_MDPA_STAT(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_MDPA_STAT)
#define	MCPCIA_MDPA_SYN(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_MDPA_SYN)
#define	MCPCIA_MDPA_DIAG(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_MDPA_DIAG)
#define	MCPCIA_MDPB_STAT(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_MDPB_STAT)
#define	MCPCIA_MDPB_SYN(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_MDPB_SYN)
#define	MCPCIA_MDPB_DIAG(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_MDPB_DIAG)
#define	MCPCIA_SG_TBIA(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_SG_TBIA)
#define	MCPCIA_HBASE(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_HBASE)
#define	MCPCIA_W0_BASE(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_W0_BASE)
#define	MCPCIA_W0_MASK(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_W0_MASK)
#define	MCPCIA_T0_BASE(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_T0_BASE)
#define	MCPCIA_W1_BASE(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_W1_BASE)
#define	MCPCIA_W1_MASK(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_W1_MASK)
#define	MCPCIA_T1_BASE(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_T1_BASE)
#define	MCPCIA_W2_BASE(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_W2_BASE)
#define	MCPCIA_W2_MASK(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_W2_MASK)
#define	MCPCIA_T2_BASE(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_T2_BASE)
#define	MCPCIA_W3_BASE(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_W3_BASE)
#define	MCPCIA_W3_MASK(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_W3_MASK)
#define	MCPCIA_T3_BASE(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_T3_BASE)
#define	MCPCIA_W_DAC(ccp)	(_SYBRIDGE(ccp) | _MCPCIA_W_DAC)

/*
 * This is here for what error handling will get as a collected subpacket.
 */

struct mcpcia_iodsnap {
	u_int64_t	base_addr;
	u_int32_t	whami;
	u_int32_t	rsvd0;
	u_int32_t	pci_rev;
	u_int32_t	cap_ctrl;
	u_int32_t	hae_mem;
	u_int32_t	hae_io;
	u_int32_t	int_ctl;
	u_int32_t	int_reg;
	u_int32_t	int_mask0;
	u_int32_t	int_mask1;
	u_int32_t	mc_err0;
	u_int32_t	mc_err1;
	u_int32_t	cap_err;
	u_int32_t	sys_env;
	u_int32_t	pci_err1;
	u_int32_t	mdpa_stat;
	u_int32_t	mdpa_syn;
	u_int32_t	mdpb_stat;
	u_int32_t	mdpb_syn;
	u_int32_t	rsvd2;
	u_int32_t	rsvd3;
	u_int32_t	rsvd4;
};

/*
 * PCI_REV Register definitions
 */
#define	CAP_REV(reg)		((reg) & 0xf)
#define	HORSE_REV(reg)		(((reg) >> 4) & 0xf)
#define	SADDLE_REV(reg)		(((reg) >> 8) & 0xf)
#define	SADDLE_TYPE(reg)	(((reg) >> 12) & 0x3)
#define	EISA_PRESENT(reg)	((reg) & (1 << 15))
#define	IS_MCPCIA_MAGIC(reg)	(((reg) & 0xffff0000) == 0x6000000)


/*
 * WHOAMI Register definitions
 *
 * The Device ID is an echo of the MID of the CPU reading this register-
 * cheezy way to figure out who you are (ask someone else!).
 */
#define	MCBUS_CPU_MID(x)		((x) & 0x7)
#define	MCBUS_CPU_INFO(x)		(((x) >> 6) & 0xff)
#define		CPU_Fill_Err	0x80
#define		CPU_DTAG_Perr	0x40
#define		CPU_RevMask	0x38
#define		CPU_RevShift	3
#define		CPU_BCacheMask	0x3
#define			CPU_BCache_0MB	0
#define			CPU_BCache_1MB	1
#define			CPU_BCache_2MB	2
#define			CPU_BCache_4MB	3

/*
 * PCI Latency Register Definitions
 */
#define	PCI_LAT_SHIFT	8	/* it's in the 2nd byte. */

/*
 * CAP Control Register Definitions
 */
#define	CAP_LED_ON	0x00000001	/* Selftest LED passed */
#define	CAP_EV56_BW_EN	0x00000002	/* BW Enables (EV56, EV6 only) */
#define	CAP_DLY_RD_EN	0x00000010	/* PCI Delayed Reads Enabled */
#define	CAP_MEM_EN	0x00000020	/* Respond to PCI transactions */
#define	CAP_REQ64_EN	0x00000040	/* Request 64 bit data transactions */
#define	CAP_ACK64_EN	0x00000080	/* Respond to 64 bit data "" */
#define	CAP_ADR_PAR_EN	0x00000100	/* Check PCI address Parity */
#define	CAP_MC_CA_PAR	0x00000200	/* Check MC bus CMD/Address Parity */
#define	CAP_MC_NXM_EN	0x00000400	/* Check for MC NXM */
#define	CAP_BUS_MON	0x00000800	/* Check for PCI errs (as bystander) */
/* bits 19:16 control number of pending write transactions */
#define		SHORT	0
#define		MED	1
#define		LONG	2
#define	CAP_MEMRD_PREFETCH_SHIFT	20
#define	CAP_MEMRDLN_PREFETCH_SHIFT	22
#define	CAP_MEMRDMULT_PREFETCH_SHIFT	24
#define	CAP_PARTIAL_WRITE	(1 << 26)

#define	CAP_ARB_BPRI	0x00000000	/* Bridge Priority Arb */
#define	CAP_ARB_RROBIN	0x40000000	/* "" Round Robin */
#define	CAP_ARB_RROBIN1	0x80000000	/* "" Round Robin #1 */

/*
 * Diagnostic Register Bits
 */
/* CAP_DIAG register */
#define	CAP_DIAG_PCIRESET	0x1	/*
					 * WriteOnly. Assert 1 for 100usec min.,
					 * then write zero. NOTE: deadlocks
					 * exist in h/w if anything but this
					 * register is accessed while reset
					 * is asserted.
					 */
#define	CAP_DIAG_MC_ADRPE	(1<<30)	/* Invert MC Bus Address/Parity */
#define	CAP_DIAG_PCI_ADRPE	(1<<31)	/* Force bad PCI parity (low 32) */

/* MDPA_DIAG or MDPB_DIAG registers */
#define	MDPX_ECC_ENA		(1<<28)	/* Enable ECC on MC Bus (default 1) */
#define	MDPX_PAR_ENA		(1<<29)	/* Enable Parity on PCI (default 0) */
#define	MDPX_DIAG_FPE_PCI	(1<<30)	/* Force PCI parity error */
#define	MDPX_DIAG_USE_CHK	(1<<31)	/*
					 * When set, DMA write cycles use the
					 * value in the low 8 bits of this
					 * register (MDPA or MDPB) as ECC
					 * sent onto main memory.
					 */

/*
 * Interrupt Specific bits...
 *
 * Mostly we don't have to mess with any of the interrupt specific registers
 * as the SRM has set most of this pretty complex stuff up for us.
 *
 * However, to enable specific interrupts, we need to set some bits
 * in imask0 if we want to have them vectored to PALcode for appropriate
 * dispatch.
 */

/*
 * bits 0-15 correspond to 4 slots (time 4 buspins) for each PCI bus.
 * bit 16 is the NCR810 onboard SCSI interrupt.
 * bits 19-20 are reserved.
 */

#define	MCPCIA_I2C_CTRL_INTR		(1<<17)
#define	MCPCIA_I2C_CTRL_BUS_ERR		(1<<18)

#define	MCPCIA_8259_NMI_INTR		(1<<21)
#define	MCPCIA_SOFT_ERR_INTR		(1<<22)
#define	MCPCIA_HARD_ERR_INTR		(1<<23)

#ifdef	YET
#define	MCPCIA_GEN_IENABL	\
	(MCPCIA_I2C_CTRL_BUS_ERR|MCPCIA_SOFT_ERR_INTR|MCPCIA_HARD_ERR_INTR)
#else
#define	MCPCIA_GEN_IENABL	\
	(MCPCIA_SOFT_ERR_INTR|MCPCIA_HARD_ERR_INTR)
#endif

/*
 * DMA Address Specific bits...
 */

#define	MCPCIA_WBASE_EN			0x1
#define	MCPCIA_WBASE_SG			0x2
#define	MCPCIA_WBASE_DAC		0x8
#define	MCPCIA_WBASE_BSHIFT		20

#define	MCPCIA_WMASK_1M			0x00000000
#define	MCPCIA_WMASK_2M			0x00100000
#define	MCPCIA_WMASK_4M			0x00300000
#define	MCPCIA_WMASK_8M			0x00700000
#define	MCPCIA_WMASK_16M		0x00f00000
#define	MCPCIA_WMASK_32M		0x01f00000
#define	MCPCIA_WMASK_64M		0x03f00000
#define	MCPCIA_WMASK_128M		0x07f00000
#define	MCPCIA_WMASK_256M		0x0ff00000
#define	MCPCIA_WMASK_512M		0x1ff00000
#define	MCPCIA_WMASK_1G			0x3ff00000
#define	MCPCIA_WMASK_2G			0x7ff00000
#define	MCPCIA_WMASK_4G			0xfff00000

/*
 * The WBASEX register contains bits 39:10 of a physical address
 * shifted to bits 31:2 of this 32 bit register. Namely, shifted
 * right by 8 bits.
 */
#define	MCPCIA_TBASEX_SHIFT		8
