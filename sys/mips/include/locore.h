/* $NetBSD: locore.h,v 1.78 2007/10/17 19:55:36 garbled Exp $ */

/*
 * Copyright 1996 The Board of Trustees of The Leland Stanford
 * Junior University. All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  Stanford University
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $FreeBSD$
 */

/*
 * Jump table for MIPS cpu locore functions that are implemented
 * differently on different generations, or instruction-level
 * archtecture (ISA) level, the Mips family.
 *
 * We currently provide support for MIPS I and MIPS III.
 */

#ifndef _MIPS_LOCORE_H
#define	_MIPS_LOCORE_H

#include <machine/cpufunc.h>
#include <machine/cpuregs.h>
#include <machine/frame.h>
#include <machine/md_var.h>

/*
 * CPU identification, from PRID register.
 */

#define MIPS_PRID_REV(x)	(((x) >>  0) & 0x00ff)
#define MIPS_PRID_IMPL(x)	(((x) >>  8) & 0x00ff)

/* pre-MIPS32/64 */
#define MIPS_PRID_RSVD(x)	(((x) >> 16) & 0xffff)
#define MIPS_PRID_REV_MIN(x)	((MIPS_PRID_REV(x) >> 0) & 0x0f)
#define MIPS_PRID_REV_MAJ(x)	((MIPS_PRID_REV(x) >> 4) & 0x0f)

/* MIPS32/64 */
#define	MIPS_PRID_CID(x)	(((x) >> 16) & 0x00ff)	/* Company ID */
#define	MIPS_PRID_CID_PREHISTORIC	0x00	/* Not MIPS32/64 */
#define	MIPS_PRID_CID_MTI		0x01	/* MIPS Technologies, Inc. */
#define	MIPS_PRID_CID_BROADCOM		0x02	/* Broadcom */
#define	MIPS_PRID_CID_ALCHEMY		0x03	/* Alchemy Semiconductor */
#define	MIPS_PRID_CID_SIBYTE		0x04	/* SiByte */
#define	MIPS_PRID_CID_SANDCRAFT		0x05	/* SandCraft */
#define	MIPS_PRID_CID_PHILIPS		0x06	/* Philips */
#define	MIPS_PRID_CID_TOSHIBA		0x07	/* Toshiba */
#define	MIPS_PRID_CID_LSI		0x08	/* LSI */
				/*	0x09	unannounced */
				/*	0x0a	unannounced */
#define	MIPS_PRID_CID_LEXRA		0x0b	/* Lexra */
#define	MIPS_PRID_CID_RMI		0x0c	/* RMI */
#define	MIPS_PRID_CID_CAVIUM		0x0d	/* Cavium */
#define	MIPS_PRID_CID_INGENIC		0xe1	/* Ingenic */
#define	MIPS_PRID_CID_INGENIC2		0xd1	/* Ingenic */

#define	MIPS_PRID_COPTS(x)	(((x) >> 24) & 0x00ff)	/* Company Options */

#endif	/* _MIPS_LOCORE_H */
