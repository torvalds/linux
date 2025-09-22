/* $OpenBSD: rpb.h,v 1.12 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: rpb.h,v 1.38 2000/07/06 23:29:13 thorpej Exp $ */

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Keith Bostic, Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * From DEC 3000 300/400/500/600/700/800/900 System Programmer's Manual,
 * EK-D3SYS-PM.B01.
 */

/*
 * HWRPB (Hardware Restart Parameter Block).
 */
#define	HWRPB_ADDR	0x10000000		/* virtual address, at boot */

#ifndef	ASSEMBLER
struct rpb {
	u_int64_t	rpb_phys;		/*   0: HWRPB phys. address. */
	char		rpb_magic[8];		/*   8: "HWRPB" (in ASCII) */
	u_int64_t	rpb_version;		/*  10 */
	u_int64_t	rpb_size;		/*  18: HWRPB size in bytes */
	u_int64_t	rpb_primary_cpu_id;	/*  20 */
	u_int64_t	rpb_page_size;		/*  28: (8192) */
	u_int32_t	rpb_phys_addr_size;	/*  30: physical address size */
	u_int32_t	rpb_extended_va_size;	/*  34: extended VA size (4L) */
	u_int64_t	rpb_max_asn;		/*  38:   (16) */
	char		rpb_ssn[16];		/*  40: only first 10 valid */

#define	ST_ADU			1		/* Alpha Demo. Unit (?) */
#define	ST_DEC_4000		2		/* "Cobra" */
#define	ST_DEC_7000		3		/* "Ruby" */
#define	ST_DEC_3000_500		4		/* "Flamingo" family (TC) */
#define	ST_DEC_2000_300		6		/* "Jensen" (EISA/ISA) */
#define	ST_DEC_3000_300		7		/* "Pelican" (TC) */
#define	ST_AVALON_A12		8		/* XXX Avalon Multicomputer */
#define	ST_DEC_2100_A500	9		/* "Sable" */
#define	ST_DEC_APXVME_64	10		/* "AXPvme" (VME) */
#define	ST_DEC_AXPPCI_33	11		/* "NoName" (PCI/ISA) */
#define	ST_DEC_21000		12		/* "TurboLaser" (PCI/EISA) */
#define	ST_DEC_2100_A50		13		/* "Avanti" (PCI/ISA) */
#define	ST_DEC_MUSTANG		14		/* "Mustang" */
#define	ST_DEC_KN20AA		15		/* kn20aa (PCI/EISA) */
#define	ST_DEC_1000		17		/* "Mikasa" (PCI/EISA) */
#define	ST_EB66			19		/* EB66 (PCI/ISA?) */
#define	ST_EB64P		20		/* EB64+ (PCI/ISA?) */
#define	ST_ALPHABOOK1		21		/* Alphabook1 */
#define	ST_DEC_4100		22		/* "Rawhide" (PCI/EISA) */
#define	ST_DEC_EV45_PBP		23		/* "Lego" K2 Passive SBC */
#define	ST_DEC_2100A_A500	24		/* "Lynx" */
#define	ST_EB164		26		/* EB164 (PCI/ISA) */
#define	ST_DEC_1000A		27		/* "Noritake" (PCI/EISA)*/
#define	ST_DEC_ALPHAVME_224	28		/* "Cortex" */
#define	ST_DEC_550		30		/* "Miata" (PCI/ISA) */
#define	ST_DEC_EV56_PBP		32		/* "Takara" */
#define	ST_DEC_ALPHAVME_320	33		/* "Yukon" (VME) */
#define	ST_DEC_6600		34		/* EV6-Tsunami based systems */
#define	ST_DEC_WILDFIRE		35		/* "Wildfire" */
#define	ST_DEC_CUSCO		36		/* "CUSCO" */
#define	ST_DEC_EIGER		37		/* "Eiger" */
#define	ST_DEC_TITAN		38		/* EV6-Titan based systems */
#define	ST_DEC_MARVEL		39		/* "Marvel" */

	/* Alpha Processor, Inc. systypes */
#define	ST_API_NAUTILUS		201		/* EV6-AMD 751 UP1000 */

	u_int64_t	rpb_type;		/*  50: */

#define	SV_MPCAP		0x00000001	/* multiprocessor capable */

#define	SV_CONSOLE		0x0000001e	/* console hardware mask */
#define	SV_CONSOLE_DETACHED	0x00000002
#define	SV_CONSOLE_EMBEDDED	0x00000004

#define	SV_POWERFAIL		0x000000e0	/* powerfail mask */
#define	SV_PF_UNITED		0x00000020
#define	SV_PF_SEPARATE		0x00000040
#define	SV_PF_BBACKUP		0x00000060
#define	SV_PF_ACTION		0x00000100	/* powerfail restart */

#define	SV_GRAPHICS		0x00000200	/* graphic engine present */

#define	SV_ST_MASK		0x0000fc00	/* system type mask */
#define	SV_ST_RESERVED		0x00000000	/* RESERVED */

/*
 * System types for the DEC 3000/500 (Flamingo) Family
 */
#define	SV_ST_SANDPIPER		0x00000400	/* Sandpiper;	3000/400 */
#define	SV_ST_FLAMINGO		0x00000800	/* Flamingo;	3000/500 */
#define	SV_ST_HOTPINK		0x00000c00	/* "Hot Pink";	3000/500X */
#define	SV_ST_FLAMINGOPLUS	0x00001000	/* Flamingo+;	3000/800 */
#define	SV_ST_ULTRA		0x00001400	/* "Ultra", aka Flamingo+ */
#define	SV_ST_SANDPLUS		0x00001800	/* Sandpiper+;	3000/600 */
#define	SV_ST_SANDPIPER45	0x00001c00	/* Sandpiper45;	3000/700 */
#define	SV_ST_FLAMINGO45	0x00002000	/* Flamingo45;	3000/900 */

/*
 * System types for ???
 */
#define	SV_ST_SABLE		0x00000400	/* Sable (???) */

/*
 * System types for the DEC 3000/300 (Pelican) Family
 */
#define	SV_ST_PELICAN		0x00000000	/* Pelican;	 3000/300 */
#define	SV_ST_PELICA		0x00000400	/* Pelica;	 3000/300L */
#define	SV_ST_PELICANPLUS	0x00000800	/* Pelican+;	 3000/300X */
#define	SV_ST_PELICAPLUS	0x00000c00	/* Pelica+;	 3000/300LX */

/*
 * System types for the AlphaStation Family
 */
#define	SV_ST_AVANTI		0x00000000	/* Avanti;	400 4/233 */
#define	SV_ST_MUSTANG2_4_166	0x00000800	/* Mustang II;	200 4/166 */
#define	SV_ST_MUSTANG2_4_233	0x00001000	/* Mustang II;	200 4/233 */
#define	SV_ST_AVANTI_XXX	0x00001400	/* also Avanti;	400 4/233 */
#define	SV_ST_AVANTI_4_266	0x00002000
#define	SV_ST_MUSTANG2_4_100	0x00002400	/* Mustang II;	200 4/100 */
#define	SV_ST_AVANTI_4_233	0x0000a800	/* AlphaStation 255/233 */

#define	SV_ST_KN20AA		0x00000400	/* AlphaStation 500/600 */

/*
 * System types for the AXPvme Family
 */
#define	SV_ST_AXPVME_64		0x00000000	/* 21068, 64MHz */
#define	SV_ST_AXPVME_160	0x00000400	/* 21066, 160MHz */
#define	SV_ST_AXPVME_100	0x00000c00	/* 21066A, 99MHz */
#define	SV_ST_AXPVME_230	0x00001000	/* 21066A, 231MHz */
#define	SV_ST_AXPVME_66		0x00001400	/* 21066A, 66MHz */
#define	SV_ST_AXPVME_166	0x00001800	/* 21066A, 165MHz */
#define	SV_ST_AXPVME_264	0x00001c00	/* 21066A, 264MHz */

/*
 * System types for the EB164 Family
 */
#define	SV_ST_EB164_266		0x00000400	/* EB164, 266MHz */
#define	SV_ST_EB164_300		0x00000800	/* EB164, 300MHz */
#define	SV_ST_ALPHAPC164_366	0x00000c00	/* AlphaPC164, 366MHz */
#define	SV_ST_ALPHAPC164_400	0x00001000	/* AlphaPC164, 400MHz */
#define	SV_ST_ALPHAPC164_433	0x00001400	/* AlphaPC164, 433MHz */
#define	SV_ST_ALPHAPC164_466	0x00001800	/* AlphaPC164, 466MHz */
#define	SV_ST_ALPHAPC164_500	0x00001c00	/* AlphaPC164, 500MHz */
#define	SV_ST_ALPHAPC164LX_400	0x00002000	/* AlphaPC164LX, 400MHz */
#define	SV_ST_ALPHAPC164LX_466	0x00002400	/* AlphaPC164LX, 466MHz */
#define	SV_ST_ALPHAPC164LX_533	0x00002800	/* AlphaPC164LX, 533MHz */
#define	SV_ST_ALPHAPC164LX_600	0x00002c00	/* AlphaPC164LX, 600MHz */
#define	SV_ST_ALPHAPC164SX_400	0x00003000	/* AlphaPC164SX, 400MHz */
#define	SV_ST_ALPHAPC164SX_466	0x00003400	/* AlphaPC164SX, 433MHz */
#define	SV_ST_ALPHAPC164SX_533	0x00003800	/* AlphaPC164SX, 533MHz */
#define	SV_ST_ALPHAPC164SX_600	0x00003c00	/* AlphaPC164SX, 600MHz */

/*
 * System types for the Digital Personal Workstation (Miata) Family
 * XXX These are not very complete!
 */
#define	SV_ST_MIATA_1_5		0x00004c00	/* Miata 1.5 */

	u_int64_t	rpb_variation;		/*  58 */

	char		rpb_revision[8];	/*  60; only first 4 valid */
	u_int64_t	rpb_intr_freq;		/*  68; scaled by 4096 */
	u_int64_t	rpb_cc_freq;		/*  70: cycle cntr frequency */
	u_long		rpb_vptb;		/*  78: */
	u_int64_t	rpb_reserved_arch;	/*  80: */
	u_long		rpb_tbhint_off;		/*  88: */
	u_int64_t	rpb_pcs_cnt;		/*  90: */
	u_int64_t	rpb_pcs_size;		/*  98; pcs size in bytes */
	u_long		rpb_pcs_off;		/*  A0: offset to pcs info */
	u_int64_t	rpb_ctb_cnt;		/*  A8: console terminal */
	u_int64_t	rpb_ctb_size;		/*  B0: ctb size in bytes */
	u_long		rpb_ctb_off;		/*  B8: offset to ctb */
	u_long		rpb_crb_off;		/*  C0: offset to crb */
	u_long		rpb_memdat_off;		/*  C8: memory data offset */
	u_long		rpb_condat_off;		/*  D0: config data offset */
	u_long		rpb_fru_off;		/*  D8: FRU table offset */
	u_int64_t	rpb_save_term;		/*  E0: terminal save */
	u_int64_t	rpb_save_term_val;	/*  E8: */
	u_int64_t	rpb_rest_term;		/*  F0: terminal restore */
	u_int64_t	rpb_rest_term_val;	/*  F8: */
	u_int64_t	rpb_restart;		/* 100: restart */
	u_int64_t	rpb_restart_val;	/* 108: */
	u_int64_t	rpb_reserve_os;		/* 110: */
	u_int64_t	rpb_reserve_hw;		/* 118: */
	u_int64_t	rpb_checksum;		/* 120: HWRPB checksum */
	u_int64_t	rpb_rxrdy;		/* 128: receive ready */
	u_int64_t	rpb_txrdy;		/* 130: transmit ready */
	u_long		rpb_dsrdb_off;		/* 138: HWRPB + DSRDB offset */
	u_int64_t	rpb_tbhint[8];		/* 149: TB hint block */
};

#define	LOCATE_PCS(h,cpunumber) ((struct pcs *)	\
	((char *)(h) + (h)->rpb_pcs_off + ((cpunumber) * (h)->rpb_pcs_size)))

/*
 * PCS: Per-CPU information.
 */
struct pcs {
	u_int8_t	pcs_hwpcb[128];		/*   0: PAL dependent */

#define	PCS_BIP			0x000001	/* boot in progress */
#define	PCS_RC			0x000002	/* restart possible */
#define	PCS_PA			0x000004	/* processor available */
#define	PCS_PP			0x000008	/* processor present */
#define	PCS_OH			0x000010	/* user halted */
#define	PCS_CV			0x000020	/* context valid */
#define	PCS_PV			0x000040	/* PALcode valid */
#define	PCS_PMV			0x000080	/* PALcode memory valid */
#define	PCS_PL			0x000100	/* PALcode loaded */

#define	PCS_HALT_REQ		0xff0000	/* halt request mask */
#define	PCS_HALT_DEFAULT	0x000000
#define	PCS_HALT_SAVE_EXIT	0x010000
#define	PCS_HALT_COLD_BOOT	0x020000
#define	PCS_HALT_WARM_BOOT	0x030000
#define	PCS_HALT_STAY_HALTED	0x040000
#define	PCS_mbz	      0xffffffffff000000	/* 24:63 -- must be zero */
	u_int64_t	pcs_flags;		/*  80: */

	u_int64_t	pcs_pal_memsize;	/*  88: PAL memory size */
	u_int64_t	pcs_pal_scrsize;	/*  90: PAL scratch size */
	u_long		pcs_pal_memaddr;	/*  98: PAL memory addr */	
	u_long		pcs_pal_scraddr;	/*  A0: PAL scratch addr */
	struct {
		u_int64_t
			minorrev	: 8,	/* alphabetic char 'a' - 'z' */
			majorrev	: 8,	/* alphabetic char 'a' - 'z' */
#define	PAL_TYPE_STANDARD	0
#define	PAL_TYPE_VMS		1
#define	PAL_TYPE_OSF1		2
			pal_type	: 8,	/* PALcode type:
						 * 0 == standard
						 * 1 == OpenVMS
						 * 2 == OSF/1
						 * 3-127 DIGITAL reserv.
						 * 128-255 non-DIGITAL reserv.
						 */
			sbz1		: 8,
			compatibility	: 16,	/* Compatibility revision */
			proc_cnt	: 16;	/* Processor count */
	} pcs_pal_rev;				/*  A8: */
#define pcs_minorrev	pcs_pal_rev.minorrev	
#define pcs_majorrev	pcs_pal_rev.majorrev	
#define pcs_pal_type	pcs_pal_rev.pal_type
#define pcs_compatibility	pcs_pal_rev.compatibility
#define pcs_proc_cnt	pcs_pal_rev.proc_cnt

	u_int64_t	pcs_proc_type;		/*  B0: processor type */

#define	PCS_PROC_EV3		1			/* EV3 */
#define	PCS_PROC_EV4		2			/* EV4: 21064 */
#define	PCS_PROC_SIMULATION	3			/* Simulation */
#define	PCS_PROC_LCA4		4			/* LCA4: 2106[68] */
#define	PCS_PROC_EV5		5			/* EV5: 21164 */
#define	PCS_PROC_EV45		6			/* EV45: 21064A */
#define	PCS_PROC_EV56		7			/* EV56: 21164A */
#define	PCS_PROC_EV6		8			/* EV6: 21264 */
#define	PCS_PROC_PCA56		9			/* PCA56: 21164PC */
#define	PCS_PROC_PCA57		10			/* PCA57: 21164?? */
#define	PCS_PROC_EV67		11			/* EV67: 21246A */
#define	PCS_PROC_EV68CB		12			/* EV68CB: 21264C */
#define	PCS_PROC_EV68AL		13			/* EV68AL: 21264B */
#define	PCS_PROC_EV68CX		14			/* EV68CX: 21264D */

#define	PCS_CPU_MAJORTYPE(p) ((p)->pcs_proc_type & 0xffffffff)
#define	PCS_CPU_MINORTYPE(p) ((p)->pcs_proc_type >> 32)

	/* Minor number interpretation is processor specific.  See cpu.c. */

	u_int64_t	pcs_proc_var;		/* B8: processor variation. */

#define	PCS_VAR_VAXFP		0x0000000000000001	/* VAX FP support */
#define	PCS_VAR_IEEEFP		0x0000000000000002	/* IEEE FP support */
#define	PCS_VAR_PE		0x0000000000000004	/* Primary Eligible */
#define	PCS_VAR_RESERVED	0xfffffffffffffff8	/* Reserved */

	char		pcs_proc_revision[8];	/*  C0: only first 4 valid */
	char		pcs_proc_sn[16];	/*  C8: only first 10 valid */
	u_long		pcs_machcheck;		/*  D8: mach chk phys addr. */
	u_int64_t	pcs_machcheck_len;	/*  E0: length in bytes */
	u_long		pcs_halt_pcbb;		/*  E8: phys addr of halt PCB */
	u_long		pcs_halt_pc;		/*  F0: halt PC */
	u_int64_t	pcs_halt_ps;		/*  F8: halt PS */
	u_int64_t	pcs_halt_r25;		/* 100: halt argument list */
	u_int64_t	pcs_halt_r26;		/* 108: halt return addr list */
	u_int64_t	pcs_halt_r27;		/* 110: halt procedure value */

#define	PCS_HALT_RESERVED		0
#define	PCS_HALT_POWERUP		1
#define	PCS_HALT_CONSOLE_HALT		2
#define	PCS_HALT_CONSOLE_CRASH		3
#define	PCS_HALT_KERNEL_MODE		4
#define	PCS_HALT_KERNEL_STACK_INVALID	5
#define	PCS_HALT_DOUBLE_ERROR_ABORT	6
#define	PCS_HALT_SCBB			7
#define	PCS_HALT_PTBR			8	/* 9-FF: reserved */
	u_int64_t	pcs_halt_reason;	/* 118: */

	u_int64_t	pcs_reserved_soft;	/* 120: preserved software */

	struct {				/* 128: inter-console buffers */
		u_int	iccb_rxlen;
		u_int	iccb_txlen;
		char	iccb_rxbuf[80];
		char	iccb_txbuf[80];
	} pcs_iccb;

#define	PALvar_reserved	0
#define	PALvar_OpenVMS	1
#define	PALvar_OSF1	2
	u_int64_t	pcs_palrevisions[16];	/* 1D0: PALcode revisions */

	u_int64_t	pcs_reserved_arch[6];	/* 250: reserved arch */
};

/*
 * CTB: Console Terminal Block
 */
struct ctb {
	u_int64_t	ctb_type;		/*   0: CTB type */
	u_int64_t	ctb_unit;		/*   8: */
	u_int64_t	ctb_reserved;		/*  16: */
	u_int64_t	ctb_len;		/*  24: bytes of info */
	u_int64_t	ctb_ipl;		/*  32: console ipl level */
	u_long		ctb_tintr_vec;		/*  40: transmit vec (0x800) */
	u_long		ctb_rintr_vec;		/*  48: receive vec (0x800) */

#define	CTB_NONE		0x00		/* no console present */
#define	CTB_SERVICE		0x01		/* service processor */
#define	CTB_PRINTERPORT		0x02		/* printer port on the SCC */
#define	CTB_GRAPHICS		0x03		/* graphics device */
#define	CTB_TYPE4		0x04		/* type 4 CTB */
#define	CTB_NETWORK		0xC0		/* network device */
	u_int64_t	ctb_term_type;		/*  56: terminal type */

	u_int64_t	ctb_keybd_type;		/*  64: keyboard nationality */
	u_long		ctb_keybd_trans;	/*  72: trans. table addr */
	u_long		ctb_keybd_map;		/*  80: map table addr */
	u_int64_t	ctb_keybd_state;	/*  88: keyboard flags */
	u_int64_t	ctb_keybd_last;		/*  96: last key entered */
	u_long		ctb_font_us;		/* 104: US font table addr */
	u_long		ctb_font_mcs;		/* 112: MCS font table addr */
	u_int64_t	ctb_font_width;		/* 120: font width, height */
	u_int64_t	ctb_font_height;	/* 128:		in pixels */
	u_int64_t	ctb_mon_width;		/* 136: monitor width, height */
	u_int64_t	ctb_mon_height;		/* 144:		in pixels */
	u_int64_t	ctb_dpi;		/* 152: monitor dots per inch */
	u_int64_t	ctb_planes;		/* 160: # of planes */
	u_int64_t	ctb_cur_width;		/* 168: cursor width, height */
	u_int64_t	ctb_cur_height;		/* 176:		in pixels */
	u_int64_t	ctb_head_cnt;		/* 184: # of heads */
	u_int64_t	ctb_opwindow;		/* 192: opwindow on screen */
	u_long		ctb_head_offset;	/* 200: offset to head info */
	u_long		ctb_putchar;		/* 208: output char to TURBO */
	u_int64_t	ctb_io_state;		/* 216: I/O flags */
	u_int64_t	ctb_listen_state;	/* 224: listener flags */
	u_long		ctb_xaddr;		/* 232: extended info addr */
	u_int64_t	ctb_turboslot;		/* 248: TURBOchannel slot # */
	u_int64_t	ctb_server_off;		/* 256: offset to server info */
	u_int64_t	ctb_line_off;		/* 264: line parameter offset */
	u_int8_t	ctb_csd;		/* 272: console specific data */
};

struct ctb_tt {
	u_int64_t	ctb_type;		/*   0: CTB type */
	u_int64_t	ctb_unit;		/*   8: console unit */
	u_int64_t	ctb_reserved;		/*  16: reserved */
	u_int64_t	ctb_length;		/*  24: length */
	u_int64_t	ctb_csr;		/*  32: address */
	u_int64_t	ctb_tivec;		/*  40: Tx intr vector */
	u_int64_t	ctb_rivec;		/*  48: Rx intr vector */
	u_int64_t	ctb_baud;		/*  56: baud rate */
	u_int64_t	ctb_put_sts;		/*  64: PUTS status */
	u_int64_t	ctb_get_sts;		/*  72: GETS status */
	u_int64_t	ctb_reserved0;		/*  80: reserved */
};

/*
 * Format of the Console Terminal Block Type 4 `turboslot' field:
 *
 *  63                   40 39       32 31     24 23      16 15   8 7    0
 *  |      reserved        |  channel  |  hose   | bus type |  bus | slot|
 */
#define	CTB_TURBOSLOT_CHANNEL(x)	(((x) >> 32) & 0xff)
#define	CTB_TURBOSLOT_HOSE(x)		(((x) >> 24) & 0xff)
#define	CTB_TURBOSLOT_TYPE(x)		(((x) >> 16) & 0xff)
#define	CTB_TURBOSLOT_BUS(x)		(((x) >> 8) & 0xff)
#define	CTB_TURBOSLOT_SLOT(x)		((x) & 0xff)

#define	CTB_TURBOSLOT_TYPE_TC		0	/* TURBOchannel */
#define	CTB_TURBOSLOT_TYPE_ISA		1	/* ISA */
#define	CTB_TURBOSLOT_TYPE_EISA		2	/* EISA */
#define	CTB_TURBOSLOT_TYPE_PCI		3	/* PCI */

/*
 * CRD: Console Routine Descriptor
 */
struct crd {
	int64_t		descriptor;
	u_int64_t	entry_va;
};

/*
 * CRB: Console Routine Block
 */
struct crb {
	struct crd	*crb_v_dispatch;	/*   0: virtual dispatch addr */
	u_long		 crb_p_dispatch;	/*   8: phys dispatch addr */
	struct crd	*crb_v_fixup;		/*  10: virtual fixup addr */
	u_long		 crb_p_fixup;		/*  18: phys fixup addr */
	u_int64_t	 crb_map_cnt;		/*  20: phys/virt map entries */
	u_int64_t	 crb_page_cnt;		/*  28: pages to be mapped */
};

/*
 * MDDT: Memory Data Descriptor Table
 */
struct mddt {
	int64_t	 	mddt_cksum;		/*   0: 7-N checksum */
	u_long		mddt_physaddr;		/*   8: bank config addr
						 * IMPLEMENTATION SPECIFIC
						 */
	u_int64_t	mddt_cluster_cnt;	/*  10: memory cluster count */
	struct mddt_cluster {
		u_long		mddt_pfn;	/*   0: starting PFN */
		u_int64_t	mddt_pg_cnt;	/*   8: 8KB page count */
		u_int64_t	mddt_pg_test;	/*  10: tested page count */
		u_long		mddt_v_bitaddr;	/*  18: bitmap virt addr */
		u_long		mddt_p_bitaddr;	/*  20: bitmap phys addr */
		int64_t		mddt_bit_cksum;	/*  28: bitmap checksum */

#define	MDDT_NONVOLATILE		0x02	/* cluster is non-volatile */
#define	MDDT_PALCODE			0x01	/* console and PAL only */
#define	MDDT_SYSTEM			0x00	/* system software only */
#define	MDDT_mbz	  0xfffffffffffffffc	/* 2:63 -- must be zero */
		int64_t		mddt_usage;	/*  30: bitmap permissions */
	} mddt_clusters[1];			/* variable length array */
};

/*
 * DSR: Dynamic System Recognition.  We're interested in the sysname
 * offset.  The data pointed to by sysname is:
 *
 *	[8 bytes: length of system name][N bytes: system name string]
 *
 * The system name string is NUL-terminated.
 */
struct dsrdb {
	int64_t		dsr_smm;		/*  0: SMM number */
	u_int64_t	dsr_lurt_off;		/*  8: LURT table offset */
	u_int64_t	dsr_sysname_off;	/* 16: offset to sysname */
};

/*
 * The DSR appeared in version 5 of the HWRPB.
 */
#define	HWRPB_DSRDB_MINVERS	5

#ifdef	_KERNEL
extern int cputype;
extern struct rpb *hwrpb;
#endif

#endif /* ASSEMBLER */
