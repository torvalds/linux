/*	$OpenBSD: elroyreg.h,v 1.1 2007/05/21 22:43:38 kettenis Exp $	*/

/*
 * Copyright (c) 2005 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

struct elroy_regs {
			/* std PCI bridge header */
	u_int32_t	pci_id;		/* 0x000 rw PCI_ID */
	u_int32_t	pci_cmdstat;	/* 0x004 rw PCI_COMMAND_STATUS_REG */
	u_int32_t	pci_class;	/* 0x008 ro PCI_CLASS_REG */
	u_int32_t	pci_bhlc;	/* 0x00c rw PCI_BHLC_REG */
	u_int32_t	res0[0x30/4];	/* 0x010 */

			/* HW Bridge registers */
	u_int32_t	pci_conf_addr;	/* 0x040 rw config space address */
	u_int32_t	pad040;
	u_int32_t	pci_conf_data;	/* 0x048 rw config space data */
	u_int32_t	pad048;
	u_int64_t	elroy_mtlt;	/* 0x050 */
	u_int32_t	busnum;		/* 0x058 bus number/scratch */
	u_int32_t	par058;
	u_int64_t	res1;		/* 0x060 */
	u_int64_t	rope;		/* 0x068 rope parity, loopback */
	u_int64_t	err_addr;	/* 0x070 error log: address */
	u_int64_t	suspend;	/* 0x078 rw suspend control */
	u_int32_t	arb_mask;	/* 0x080 rw arbitration mask */
	u_int32_t	pad080;
#define	ELROY_ARB_ENABLE	0x01		/* enable arbitration */
#define	ELROY_ARB_PCIDEVA	0x02		/* PCI device A allow */
#define	ELROY_ARB_PCIDEVB	0x04		/* PCI device A allow */
#define	ELROY_ARB_PCIDEVC	0x08		/* PCI device A allow */
#define	ELROY_ARB_PCIDEVD	0x10		/* PCI device A allow */
#define	ELROY_ARB_PCIDEVE	0x20		/* PCI device A allow */
#define	ELROY_ARB_PCIDEVF	0x40		/* PCI device A allow */
#define	ELROY_ARB_PCIDEVG	0x80		/* PCI device A allow */
	u_int64_t	arb_pri;	/* 0x088 arbitration priority */
	u_int64_t	arb_mode;	/* 0x090 arbitration mode */
	u_int64_t	mtlt;		/* 0x098 */
	u_int64_t	res2[12];	/* 0x0a0 */
	u_int64_t	mod_info;	/* 0x100 */
	u_int32_t	control;	/* 0x108 */
#define	ELROY_CONTROL_RF	0x01		/* reset pci */
#define	ELROY_CONTROL_VE	0x08		/* VGA enable */
#define	ELROY_CONTROL_CL	0x10		/* clear error log */
#define	ELROY_CONTROL_CE	0x20		/* clear error log enable */
#define	ELROY_CONTROL_HF	0x40		/* hard fail enable */
	u_int32_t	status;		/* 0x10c */
#define	ELROY_STATUS_RC		0x01		/* reset complete */
#define	ELROY_STATUS_BITS	"\020\01RC"
	u_int64_t	res3[30];	/* 0x110 */
	u_int64_t	lmmio_base;	/* 0x200 */
	u_int64_t	lmmio_mask;	/* 0x208 */
	u_int64_t	gmmio_base;	/* 0x210 */
	u_int64_t	gmmio_mask;	/* 0x218 */
	u_int64_t	wlmmio_base;	/* 0x220 */
	u_int64_t	wlmmio_mask;	/* 0x228 */
	u_int64_t	wgmmio_base;	/* 0x230 */
	u_int64_t	wgmmio_mask;	/* 0x238 */
	u_int32_t	io_base;	/* 0x240 */
	u_int32_t	pad240;
	u_int32_t	io_mask;	/* 0x248 */
	u_int32_t	pad248;
	u_int32_t	res4[4];	/* 0x250 */
	u_int32_t	eio_base;	/* 0x260 */
	u_int32_t	pad260;
	u_int32_t	eio_mask;	/* 0x268 */
	u_int32_t	pad268;
#define	ELROY_BASE_RE	0x01			/* range enable */
	u_int64_t	res5;		/* 0x270 */
	u_int64_t	dmac_ctrl;	/* 0x278 DMA connection control */
	u_int64_t	res6[16];	/* 0x280 */
	u_int32_t	ibase;		/* 0x300 */
	u_int32_t	pad300;
	u_int32_t	imask;		/* 0x308 */
	u_int32_t	pad308;
	u_int64_t	hint_cfg;	/* 0x310 */
	u_int64_t	res7[13];	/* 0x318 */
	u_int64_t	hints[14];	/* 0x380 */
	u_int64_t	res8[2];	/* 0x3f0 */
	u_int64_t	res9[64];	/* 0x400 */
	u_int64_t	pad0;		/* 0x600 */
	u_int64_t	pci_drive;	/* 0x608 */
	u_int64_t	rope_cfg;	/* 0x610 */
	u_int64_t	clk_ctl;	/* 0x618 */
	u_int32_t	pad1;		/* 0x620 */
	u_int32_t	res10[23];	/* 0x624 */
	u_int32_t	err_cfg;	/* 0x680 error config */
	u_int32_t	pad680;
#define	ELROY_ERRCFG_PW		0x01		/* PIO writes parity errors */
#define	ELROY_ERRCFG_PR		0x02		/* PIO reads parity errors */
#define	ELROY_ERRCFG_DW		0x04		/* DMA writes parity errors */
#define	ELROY_ERRCFG_DR		0x08		/* DMA reads parity errors */
#define	ELROY_ERRCFG_CM		0x10		/* no fatal on config space */
#define	ELROY_ERRCFG_SMART	0x20		/* smart bus mode */
	u_int64_t	err_stat;	/* 0x688 error status */
	u_int64_t	err_mid;	/* 0x690 error log: master id */
	u_int64_t	rope_estat;	/* 0x698 rope error status */
	u_int64_t	rope_eclr;	/* 0x6a0 rope error clear */
	u_int64_t	res11[42];	/* 0x6a8 */
	u_int64_t	regbus;		/* 0x7f8 reads 0x3ff */
	u_int32_t	apic_addr;	/* 0x800 APIC address register */
	u_int32_t	pad800;
	u_int64_t	res12;
	u_int32_t	apic_data;	/* 0x810 APIC data register */
	u_int32_t	pad808;
	u_int64_t	res13[5];
	u_int32_t	apic_eoi;	/* 0x840 APIC interrupt ack */
	u_int32_t	pad840;
	u_int32_t	apic_softint;	/* 0x850 write generates softint */
	u_int32_t	pad850;
	u_int64_t	res14[123];	/* 0x858 */
					/*0x1000 */
} __packed;

/* APIC registers */
#define	APIC_VERSION	0x01
#define	APIC_VERSION_MASK	0xff
#define	APIC_VERSION_NENT	0xff0000
#define	APIC_VERSION_NENT_SHIFT	16
#define	APIC_ENT0(i)	(0x10 + (i)*2)
#define	APIC_ENT0_VEC	0x000ff
#define	APIC_ENT0_MOD	0x00700	/* delivery mode */
#define	APIC_ENT0_FXD	0x00000
#define	APIC_ENT0_RDR	0x00100
#define	APIC_ENT0_PMI	0x00200
#define	APIC_ENT0_NMI	0x00400
#define	APIC_ENT0_INI	0x00500
#define	APIC_ENT0_EXT	0x00700
#define	APIC_ENT0_PEND	0x01000	/* int is pending */
#define	APIC_ENT0_LOW	0x02000	/* polarity */
#define	APIC_ENT0_LEV	0x08000	/* edge/level */
#define	APIC_ENT0_MASK	0x10000	/* mask int */
#define	APIC_ENT1(i)	(0x11 + (i)*2)

