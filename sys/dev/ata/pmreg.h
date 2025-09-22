/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * SATA port multiplier registers
 */
#define SATA_PMREG_SSTS		0	/* use SATA_PM_SSTS_ bit defs */
#define SATA_PMREG_SERR		1	/* use SATA_PM_SERR_ bit defs */
#define SATA_PMREG_SCTL		2	/* use SATA_PM_SCTL_ bit defs */
#define SATA_PMREG_SACT		3	/* (not implemented on PM) */

#define  SATA_PM_SSTS_DET		0xf /* Device Detection */
#define  SATA_PM_SSTS_DET_NONE		0x0
#define  SATA_PM_SSTS_DET_DEV_NE	0x1
#define  SATA_PM_SSTS_DET_DEV		0x3
#define  SATA_PM_SSTS_DET_PHYOFFLINE	0x4
#define  SATA_PM_SSTS_SPD		0xf0 /* Current Interface Speed */
#define  SATA_PM_SSTS_SPD_NONE		0x00
#define  SATA_PM_SSTS_SPD_GEN1		0x10
#define  SATA_PM_SSTS_SPD_GEN2		0x20
#define  SATA_PM_SSTS_IPM		0xf00 /* Interface Power Management */
#define  SATA_PM_SSTS_IPM_NONE		0x000
#define  SATA_PM_SSTS_IPM_ACTIVE	0x100
#define  SATA_PM_SSTS_IPM_PARTIAL	0x200
#define  SATA_PM_SSTS_IPM_SLUMBER	0x600

#define  SATA_PM_SCTL_DET		0xf /* Device Detection */
#define  SATA_PM_SCTL_DET_NONE		0x0
#define  SATA_PM_SCTL_DET_INIT		0x1
#define  SATA_PM_SCTL_DET_DISABLE	0x4
#define  SATA_PM_SCTL_SPD		0xf0 /* Speed Allowed */
#define  SATA_PM_SCTL_SPD_ANY		0x00
#define  SATA_PM_SCTL_SPD_GEN1		0x10
#define  SATA_PM_SCTL_SPD_GEN2		0x20
#define  SATA_PM_SCTL_IPM		0xf00 /* Interface Power Management */
#define  SATA_PM_SCTL_IPM_NONE		0x000
#define  SATA_PM_SCTL_IPM_NOPARTIAL	0x100
#define  SATA_PM_SCTL_IPM_NOSLUMBER	0x200
#define  SATA_PM_SCTL_IPM_DISABLED	0x300
#define	 SATA_PM_SCTL_SPM		0xf000	/* Select Power Management */
#define	 SATA_PM_SCTL_SPM_NONE		0x0000
#define	 SATA_PM_SCTL_SPM_NOPARTIAL	0x1000
#define	 SATA_PM_SCTL_SPM_NOSLUMBER	0x2000
#define	 SATA_PM_SCTL_SPM_DISABLED	0x3000
#define  SATA_PM_SCTL_PMP		0xf0000	/* Set PM port for xmit FISes */
#define  SATA_PM_SCTL_PMP_SHIFT		16

#define  SATA_PM_SERR_ERR_I		(1<<0) /* Recovered Data Integrity */
#define  SATA_PM_SERR_ERR_M		(1<<1) /* Recovered Communications */
#define  SATA_PM_SERR_ERR_T		(1<<8) /* Transient Data Integrity */
#define  SATA_PM_SERR_ERR_C		(1<<9) /* Persistent Comm/Data */
#define  SATA_PM_SERR_ERR_P		(1<<10) /* Protocol */
#define  SATA_PM_SERR_ERR_E		(1<<11) /* Internal */
#define  SATA_PM_SERR_DIAG_N		(1<<16) /* PhyRdy Change */
#define  SATA_PM_SERR_DIAG_I		(1<<17) /* Phy Internal Error */
#define  SATA_PM_SERR_DIAG_W		(1<<18) /* Comm Wake */
#define  SATA_PM_SERR_DIAG_B		(1<<19) /* 10B to 8B Decode Error */
#define  SATA_PM_SERR_DIAG_D		(1<<20) /* Disparity Error */
#define  SATA_PM_SERR_DIAG_C		(1<<21) /* CRC Error */
#define  SATA_PM_SERR_DIAG_H		(1<<22) /* Handshake Error */
#define  SATA_PM_SERR_DIAG_S		(1<<23) /* Link Sequence Error */
#define  SATA_PM_SERR_DIAG_T		(1<<24) /* Transport State Trans Err */
#define  SATA_PM_SERR_DIAG_F		(1<<25) /* Unknown FIS Type */
#define  SATA_PM_SERR_DIAG_X		(1<<26) /* Exchanged */

#define  SATA_PFMT_SERR	"\020" 	\
			"\033DIAG.X" "\032DIAG.F" "\031DIAG.T" "\030DIAG.S" \
			"\027DIAG.H" "\026DIAG.C" "\025DIAG.D" "\024DIAG.B" \
			"\023DIAG.W" "\022DIAG.I" "\021DIAG.N"		    \
			"\014ERR.E" "\013ERR.P" "\012ERR.C" "\011ERR.T"	    \
			"\002ERR.M" "\001ERR.I"

/*
 * AHCI port multiplier revision information SCR[1] (see ahci_pm_read)
 *
 * Rev 1.1 is the one that should support async notification.
 */
#define SATA_PMREV_PM1_0	0x00000002
#define SATA_PMREV_PM1_1	0x00000004
#define SATA_PFMT_PM_REV	"\20" "\003PM1.1" "\002PM1.0"

/*
 * GSCR[64] and GSCR[96] - Port Multiplier features available and features
 *			   enabled.
 */
#define SATA_PMREG_FEA		64
#define SATA_PMREG_FEAEN	96		/* (features enabled) */
#define SATA_PMFEA_BIST		0x00000001	/* BIST Support */
#define SATA_PMFEA_PMREQ	0x00000002	/* Can issue PMREQp to host */
#define SATA_PMFEA_DYNSSC	0x00000004	/* Dynamic SSC transmit enab */
#define SATA_PMFEA_ASYNCNOTIFY	0x00000008	/* Async notification */

#define SATA_PFMT_PM_FEA	"\20"			\
				"\004AsyncNotify"	\
				"\003DynamicSSC"	\
				"\002PMREQ"		\
				"\001BIST"

/*
 * Enable generation of async notify events for individual targets
 * via the PMEENA register.  Each bit in PMEINFO is a wire-or of all
 * SERROR bits for that target.  To enable a new notification event
 * the SERROR bits in PMSERROR_REGNO must be cleared.
 */
#define SATA_PMREG_EINFO	32		/* error info 16 ports */
#define SATA_PMREG_EEENA	33		/* error info enable 16 ports */

#define SATA_PMP_MAX_PORTS	16

#define SATA_PMP_CONTROL_PORT	0x0f
