/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2009 Marvell Semiconductor, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#ifndef _MWL_DIAG_H_
#define	_MWL_DIAG_H_
/*
 * Diagnostic interface.  This is an open-ended interface that
 * is opaque to applications.  Diagnostic programs use this to
 * retrieve internal data structures, etc.  There is no guarantee
 * that calling conventions for calls other than MWL_DIAG_REVS
 * are stable between HAL releases; a diagnostic application must
 * use the HAL revision information to deal with ABI/API differences.
 *
 * NB: do not renumber these, certain codes are publicly used.
 */
enum {
	MWL_DIAG_CMD_REVS	= 0,	/* MAC/PHY/Radio revs */
	MWL_DIAG_CMD_REGS	= 1,	/* Registers */
	MWL_DIAG_CMD_HOSTCMD	= 2,	/* issue arbitrary cmd */
	MWL_DIAG_CMD_FWLOAD	= 3,	/* load firmware */
};

/*
 * Device revision information.
 */
typedef struct {
	uint16_t	mh_devid;		/* PCI device ID */
	uint16_t	mh_subvendorid;		/* PCI subvendor ID */
	uint16_t	mh_macRev;		/* MAC revision */
	uint16_t	mh_phyRev;		/* PHY revision */
} MWL_DIAG_REVS;

typedef struct {
	uint16_t	start;		/* first register */
	uint16_t	end;		/* ending register or zero */
} MWL_DIAG_REGRANGE;

/*
 * Registers are mapped into virtual banks; the hal converts
 * r/w operations through the diag api to host cmds as required.
 *
 * NB: register offsets are 16-bits and we need to avoid real
 *     register mappings in BAR1.
 */
#define	MWL_DIAG_BASE_MAC	0xa000
#define	MWL_DIAG_ISMAC(r) \
	(MWL_DIAG_BASE_MAC <= (r) && (r) < (MWL_DIAG_BASE_MAC+0x1000))
#define	MWL_DIAG_BASE_BB	0xe000
#define	MWL_DIAG_ISBB(r) \
	(MWL_DIAG_BASE_BB <= (r) && (r) < (MWL_DIAG_BASE_BB+0x1000))
#define	MWL_DIAG_BASE_RF	0xf000
#define	MWL_DIAG_ISRF(r) \
	(MWL_DIAG_BASE_RF <= (r) && (r) < (MWL_DIAG_BASE_RF+0x1000))

/*
 * Firmware download
 */
typedef struct {
	uint32_t	opmode;			/* operating mode */
	uint32_t	signature;		/* f/w ready signature */
	char		name[1];		/* variable length pathname */
} MWL_DIAG_FWLOAD;

struct mwl_diag {
	char	md_name[IFNAMSIZ];	/* if name, e.g. "mv0" */
	uint16_t md_id;
#define	MWL_DIAG_DYN	0x8000		/* allocate buffer in caller */
#define	MWL_DIAG_IN	0x4000		/* copy in parameters */
#define	MWL_DIAG_OUT	0x0000		/* copy out results (always) */
#define	MWL_DIAG_ID	0x0fff
	uint16_t md_in_size;		/* pack to fit, yech */
	void *	md_in_data;
	void *	md_out_data;
	u_int	md_out_size;

};
#define	SIOCGMVDIAG	_IOWR('i', 138, struct mwl_diag)
#define	SIOCGMVRESET	_IOW('i', 139, struct mwl_diag)
#endif /* _MWL_DIAG_H_ */
