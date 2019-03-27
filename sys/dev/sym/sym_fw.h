/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *  Device driver optimized for the Symbios/LSI 53C896/53C895A/53C1010 
 *  PCI-SCSI controllers.
 *
 *  Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 *  This driver also supports the following Symbios/LSI PCI-SCSI chips:
 *	53C810A, 53C825A, 53C860, 53C875, 53C876, 53C885, 53C895,
 *	53C810,  53C815,  53C825 and the 53C1510D is 53C8XX mode.
 *
 *  
 *  This driver for FreeBSD-CAM is derived from the Linux sym53c8xx driver.
 *  Copyright (C) 1998-1999  Gerard Roudier
 *
 *  The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 *  a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 *  The original ncr driver has been written for 386bsd and FreeBSD by
 *          Wolfgang Stanglmeier        <wolf@cologne.de>
 *          Stefan Esser                <se@mi.Uni-Koeln.de>
 *  Copyright (C) 1994  Wolfgang Stanglmeier
 *
 *  The initialisation code, and part of the code that addresses 
 *  FreeBSD-CAM services is based on the aic7xxx driver for FreeBSD-CAM 
 *  written by Justin T. Gibbs.
 *
 *  Other major contributions:
 *
 *  NVRAM detection and reading.
 *  Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
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

/* $FreeBSD$ */

#ifndef	SYM_FW_H
#define	SYM_FW_H
/*
 *  Macro used to generate interfaces for script A.
 */
#define SYM_GEN_FW_A(s)							\
	SYM_GEN_A(s, start)		SYM_GEN_A(s, getjob_begin)	\
	SYM_GEN_A(s, getjob_end)					\
	SYM_GEN_A(s, select)		SYM_GEN_A(s, wf_sel_done)	\
	SYM_GEN_A(s, send_ident)					\
	SYM_GEN_A(s, dispatch)		SYM_GEN_A(s, init)		\
	SYM_GEN_A(s, clrack)		SYM_GEN_A(s, complete_error)	\
	SYM_GEN_A(s, done)		SYM_GEN_A(s, done_end)		\
	SYM_GEN_A(s, idle)		SYM_GEN_A(s, ungetjob)		\
	SYM_GEN_A(s, reselect)						\
	SYM_GEN_A(s, resel_tag)		SYM_GEN_A(s, resel_dsa)		\
	SYM_GEN_A(s, resel_no_tag)					\
	SYM_GEN_A(s, data_in)		SYM_GEN_A(s, data_in2)		\
	SYM_GEN_A(s, data_out)		SYM_GEN_A(s, data_out2)		\
	SYM_GEN_A(s, pm0_data)		SYM_GEN_A(s, pm1_data)

/*
 *  Macro used to generate interfaces for script B.
 */
#define SYM_GEN_FW_B(s)							\
	SYM_GEN_B(s, no_data)						\
	SYM_GEN_B(s, sel_for_abort)	SYM_GEN_B(s, sel_for_abort_1)	\
	SYM_GEN_B(s, msg_bad)		SYM_GEN_B(s, msg_weird)		\
	SYM_GEN_B(s, wdtr_resp)		SYM_GEN_B(s, send_wdtr)		\
	SYM_GEN_B(s, sdtr_resp)		SYM_GEN_B(s, send_sdtr)		\
	SYM_GEN_B(s, ppr_resp)		SYM_GEN_B(s, send_ppr)		\
	SYM_GEN_B(s, nego_bad_phase)					\
	SYM_GEN_B(s, ident_break) 	SYM_GEN_B(s, ident_break_atn)	\
	SYM_GEN_B(s, sdata_in)		SYM_GEN_B(s, resel_bad_lun)	\
	SYM_GEN_B(s, bad_i_t_l)		SYM_GEN_B(s, bad_i_t_l_q)	\
	SYM_GEN_B(s, wsr_ma_helper)					\
	SYM_GEN_B(s, snooptest)		SYM_GEN_B(s, snoopend)

/*
 *  Generates structure interface that contains 
 *  offsets within script A and script B.
 */
#define	SYM_GEN_A(s, label)	s label;
#define	SYM_GEN_B(s, label)	s label;
struct sym_fwa_ofs {
	SYM_GEN_FW_A(u_short)
};
struct sym_fwb_ofs {
	SYM_GEN_FW_B(u_short)
	SYM_GEN_B(u_short, start64)
	SYM_GEN_B(u_short, pm_handle)
};

/*
 *  Generates structure interface that contains 
 *  bus addresses within script A and script B.
 */
struct sym_fwa_ba {
	SYM_GEN_FW_A(u32)
};
struct sym_fwb_ba {
	SYM_GEN_FW_B(u32)
	SYM_GEN_B(u32, start64)
	SYM_GEN_B(u32, pm_handle)
};
#undef	SYM_GEN_A
#undef	SYM_GEN_B

/*
 *  Let cc know about the name of the controller data structure.
 *  We need this for function prototype declarations just below.
 */
struct sym_hcb;

/*
 *  Generic structure that defines a firmware.
 */ 
struct sym_fw {
	const char	*name;	/* Name we want to print out	*/
	const u32	*a_base;/* Pointer to script A template	*/
	int	a_size;		/* Size of script A		*/
	const struct	sym_fwa_ofs
		*a_ofs;		/* Useful offsets in script A	*/
	const u32	*b_base;/* Pointer to script B template	*/
	int	b_size;		/* Size of script B		*/
	const struct	sym_fwb_ofs
		*b_ofs;		/* Useful offsets in script B	*/
	/* Setup and patch methods for this firmware */
	void	(*setup)(struct sym_hcb *, const struct sym_fw *);
	void	(*patch)(struct sym_hcb *);
};

/*
 *  Macro used to declare a firmware.
 */
#define SYM_FW_ENTRY(fw, name)					\
{								\
	name,							\
	(const u32 *) &fw##a_scr, sizeof(fw##a_scr), &fw##a_ofs,\
	(const u32 *) &fw##b_scr, sizeof(fw##b_scr), &fw##b_ofs,\
	fw##_setup, fw##_patch					\
}

/*
 *  Macros used from the C code to get useful
 *  SCRIPTS bus addresses.
 */
#define SCRIPTA_BA(np, label)	(np->fwa_bas.label)
#define SCRIPTB_BA(np, label)	(np->fwb_bas.label)
#define SCRIPTB0_BA(np,label)	\
	(np->scriptb0_ba + (np->fwb_bas.label - np->scriptb_ba))

/*
 *  Macros used by scripts definitions.
 *
 *  HADDR_1 generates a reference to a field of the controller data.
 *  HADDR_2 generates a reference to a field of the controller data
 *          with offset.
 *  RADDR_1 generates a reference to a script processor register.
 *  RADDR_2 generates a reference to a script processor register
 *          with offset.
 *  PADDR_A generates a reference to another part of script A.
 *  PADDR_B generates a reference to another part of script B.
 *
 *  SYM_GEN_PADDR_A and SYM_GEN_PADDR_B are used to define respectively 
 *  the PADDR_A and PADDR_B macros for each firmware by setting argument 
 *  `s' to the name of the corresponding structure.
 *
 *  SCR_DATA_ZERO is used to allocate a DWORD of data in scripts areas.
 */

#define	RELOC_SOFTC	0x40000000
#define	RELOC_LABEL_A	0x50000000
#define	RELOC_REGISTER	0x60000000
#define	RELOC_LABEL_B	0x80000000
#define	RELOC_MASK	0xf0000000

#define	HADDR_1(label)	   (RELOC_SOFTC    | offsetof(struct sym_hcb, label))
#define	HADDR_2(label,ofs) (RELOC_SOFTC    | \
				(offsetof(struct sym_hcb, label)+(ofs)))
#define	RADDR_1(label)	   (RELOC_REGISTER | REG(label))
#define	RADDR_2(label,ofs) (RELOC_REGISTER | ((REG(label))+(ofs)))

#define SYM_GEN_PADDR_A(s, label) (RELOC_LABEL_A  | offsetof(s, label))
#define SYM_GEN_PADDR_B(s, label) (RELOC_LABEL_B  | offsetof(s, label))

#define SCR_DATA_ZERO	0xf00ff00f

#endif	/* SYM_FW_H */
