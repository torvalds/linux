/*	$OpenBSD: viper.h,v 1.8 2025/07/16 07:15:42 jsg Exp $	*/

/* 
 * Copyright (c) 1991,1994 The University of Utah and
 * the Computer Systems Laboratory (CSL).  All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 * 	Utah $Hdr: viper.h 1.8 94/12/14$
 */

/*
 * Viper control register.
 *
 * With respect to arbitration preference (*_prf), only one of these may be
 * set at any one time.  "preference" means that a particular device will
 * be granted the bus on every other arbitration cycle; these bits default
 * to unset (0).  Similarly, a device may be denied the bus (*_den); these
 * bits default to *set* (1).
 *
 * The macros V_CTRL_ANYPRF or V_CTRL_ANYDEN should be used to determine
 * if any preference or deny bits are set.
 */

#define VI_CTRL_EISA_DEN 0x00000001 /* EISA denied bus grants */
#define VI_CTRL_EISA_PRF 0x00000002 /* EISA bus has arbitration preference */
#define VI_CTRL_CORE_DEN 0x00000004 /* CORE denied bus grants */
#define VI_CTRL_CORE_PRF 0x00000008 /* CORE bus has arbitration preference */
#define VI_CTRL_SGC0_DEN 0x00000010 /* SGC0 denied bus grants */
#define VI_CTRL_SGC0_PRF 0x00000020 /* SGC0 has arbitration preference */
#define VI_CTRL_SGC1_DEN 0x00000040 /* SGC1 denied bus grants */
#define VI_CTRL_SGC1_PRF 0x00000080 /* SGC1 has arbitration preference */
#define VI_CTRL_CPU_PRF  0x00000200 /* CPU  has arbitration preference */
#define VI_CTRL_LPMC_EN  0x00010000 /* enable Low Priority Machine Checks */
#define VI_CTRL_IPREF_EN 0x00020000 /* enable instruction prefetching */
#define VI_CTRL_VSC_TOUT 0xfff80000 /* VSC clocks to wait before buserr tmo */

#define	VI_CTRL_ANYPRF	0x02AA
#define	VI_CTRL_ANYDEN	0x0055
#define	VI_CTRL		PAGE0->pz_Pdep.pd_Viper.v_Ctrlcpy
#define	VI_CTRL_BITS	"\020\001eisa_den\002eisa_prf\003core_den\004core_prf" \
			"\005sgc1_den\006sgc1_prf\007sgc0_den\010sgc0_prf" \
			"\012cpu_prf\021lpmc_en\022ipref_en"

#define	VI_STAT_BITS	"\020\001grf_buserr\002cpu_buserr\003ven_tmo" \
			"\004ven_buserr\005toc\006hardecc\007softecc\010cmdrst"
struct vi_stat {		/* (RO) */
	u_int	hw_rev	:24,	/* Viper hardware revision (24 bits!) */
		cmdreset: 1,	/* set if last chip reset caused by CMD_RESET */
		softecc	: 1,	/* correctable memory error (lpmc_en set) */
		hardecc	: 1,	/* uncorrectable memory error (HPMC) */
		toc	: 1,	/* Transfer Of Control signaled */
		vn_ader	: 1,	/* Venom address error (lpmc_en set) */
		vn_vscto: 1,	/* Venom VSC timeout (lpmc_en set) */
		cpu_ader: 1,	/* CPU address error or timeout (HPMC) */
		grf_ader: 1;	/* Graphics address error */
};


/*
 * Viper TRS.  The structures have been defined above; the remaining
 * fields are described here.
 *
 *	vi_intrwd (WO)
 *		If a high to low transition of the interrupt line occurs,
 *		Viper will send this to the CPU to be or'd into its EIR.
 *		In general, this is an ASP interrupt request.
 *
 *	vi_mem_ctrl (WO)
 *		Set various DRAM attributes (row, cols, refresh, etc).
 *
 *	vi_mem_wrchk (WO), vi_mem_rdchk (RO)
 *		read/write data to be for copyin/memtest.
 *
 *	vi_mem_limit (WO)
 *		Set an upper limit for non-IO memory accesses; this must
 *		be less than the actual memory size, low 22 bits ignored.
 *
 *	vi_merr_w0, vi_merr_w1, vi_merr_ckbyte, vi_merr_addr (RO)
 *		If memory error detection enabled and soft/hard ECC error,
 *		raw double word is stored here (w0: most significant word).
 *		The raw checkbyte data is stored in "vi_merr_ckbyte".
 *		The address of last logged error is in "vi_merr_addr".
 *
 */
struct vi_trs {
	u_int		vi_control;	/* PAGE0->pz_Pdep.pd_Viper.v_Ctrlcpy */
	struct vi_stat	vi_status;
	u_int		vi_intrwd;
	u_int		vi_resv1[13];
	u_int		vi_mem_ctrl;
	u_int		vi_mem_wrchk;
	u_int		vi_mem_limit;
	u_int		vi_resv2[1];
	u_int		vi_merr_w1;
	u_int		vi_merr_w2;
	u_int		vi_merr_ckbyte;
	u_int		vi_mem_rdchk;
	u_int		vi_merr_addr;
	u_int		vi_resv3[135];
};


/*
** Viper also creates HPA registers for the graphics accelerator (Venom).
** Venom has two sets of registers; the User HPA contains registers that
** users are allowed to access, while the Supervisor HPA is only accessible
** by code running at the most privileged level.  Both sets of registers
** are defined below.
*/

#define	VENOM_USER	((struct vn_user *)0xFFFBC000)
#define	VENOM_SUPR	((struct vn_supr *)0xFFFBD000)

/*
 * Define bits in the Venom "User Control" register.
 */
struct vnu_ctl {
	u_int	sdt_msk	:16,	/* screen door transparency mask */
		: 6,
		d_z_intp: 1,	/* disable Z Interpolation when set */
		d_c_intp: 1,	/* disable Color Interpolation when set */
		d_ad_inc: 1,	/* disable I/O Addr Incrementing when set */
		: 1,
		z_fast	: 1,	/* enable Fast Z Interpolation when set */
		c_pseudo: 1,	/* enable Pseudo Color when set (disable RG) */
		z_prec24: 1,	/* enable 24-bit Z integer precision (o/w 16) */
		cmp_intp: 3;	/* enable cond: Z intp owrites old Z (<,>,=) */
};

/*
 * When vnu_ctl's "z_prec24" is set, 24-bit Z integer precision is enabled
 * (otherwise 16-bit integer precision is used).  When enabled, the format
 * of various User Control registers is changed; `vnu_prec' (defined below)
 * should make this format more clear.
 */
union vnu_prec {		/* 16 or 24 bit precision */
	struct {
		u_int	zero1;		/* must be zero */
		u_int	intg	:16,	/* integer part (16 bits) */
			frac	:12,	/* fractional part (12 bits) */
			zero2	: 4;	/* must be zero */
	} prec16;
	struct {
		u_int	frac_lo	: 4,	/* fractional part (lower 4 bits) */
			zero1	:28;	/* must be zero */
		u_int	intg	:24,	/* integer part (24 bits) */
			frac_hi	: 8;	/* fractional part (upper 8 bits) */
	} prec24;
};
#define	vnu_p16i	prec16.intg
#define	vnu_p16f	prec16.frac
#define	vnu_p24i	prec24.intg
#define	vnu_p24f	((prec24.frac_hi << 4) | prec24.frac_lo)
#define	vnu_p24fh	prec24.frac_hi
#define	vnu_p24fl	prec24.frac_lo

/*
 * Venom User HPA registers.
 */
struct vn_user {
	u_int		vnu_resv1[32];
	struct vnu_ctl	vnu_uctl;	/* user control */
	u_int		vnu_spancnt;	/* span count (13 bits, signed) */
	u_int		vnu_graddr;	/* graphics address (24 bits: 6-29) */
	u_int		vnu_resv2;
	union vnu_prec	vnu_zslope;	/* Z Slope */
	union vnu_prec	vnu_z;		/* Z */
	u_int		vnu_resv3[8];
	u_int		vnu_bslope;	/* Blue Slope (12-19:int, 20-31:fra) */
	u_int		vnu_bcolor;	/* Blue Color (12-19:int, 20-31:fra) */
	u_int		vnu_resv4[2];
	u_int		vnu_rslope;	/* Red Slope (12-19:int, 20-31:fra) */
	u_int		vnu_rcolor;	/* Red Color (12-19:int, 20-31:fra) */
	u_int		vnu_resv5[2];
	u_int		vnu_gslope;	/* Green Slope (12-19:int, 20-31:fra) */
	u_int		vnu_gcolor;	/* Green Color (12-19:int, 20-31:fra) */
};


/*
 * Define bits in Venom "Supervisor Control" register.
 */
struct vns_ctl {
	u_int		: 4,
		ioaddr	: 2,	/* graphics addr (bits 4 & 5 of `vnu_graddr') */
		d_venom	: 1,	/* disable Venom operation processing */
			:25;
};

/*
 * Venom Supervisor HPA registers.
 */
struct vn_supr {
	u_int		vns_resv1[32];
	struct vns_ctl	vns_sctl;	/* supervisor control */
	u_int		vns_zaddr;	/* Z Buffer Address (RO) */
};

void viper_setintrwnd(u_int32_t mask);
void viper_eisa_en(void);

