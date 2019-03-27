/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2004,2005 Joerg Wunsch
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)sun_disklabel.h	8.1 (Berkeley) 6/11/93
 *	$NetBSD: disklabel.h,v 1.2 1998/08/22 14:55:28 mrg Exp $
 *
 * $FreeBSD$ 
 */

#ifndef _SYS_SUN_DISKLABEL_H_
#define	_SYS_SUN_DISKLABEL_H_

/*
 * SunOS/Solaris disk label layout (partial).
 * 
 * Suns disk label format contains a lot of historical baggage which we 
 * ignore entirely.  The structure below contains the relevant bits and the
 * _enc/_dec functions encode/decode only these fields.
 */

#define	SUN_DKMAGIC	55998
#define	SUN_NPART	8
#define	SUN_RAWPART	2
#define	SUN_SIZE	512
#define	SUN_VTOC_VERSION 1
#define	SUN_VTOC_SANE	0x600DDEEE /* SVR4-compatible VTOC is "sane". */
#define	SUN_VOLNAME_LEN	8
/*
 * XXX: I am actually not sure if this should be "16 sectors" or "8192 bytes".
 * XXX: Considering that Sun went to the effort of getting 512 byte compatible
 * XXX: CDROM drives produced my guess is that Sun computers stand little or
 * XXX: even no chance of running, much less booting on !=512 byte media.
 * XXX: Define this is in terms of bytes since that is easier for us.
 */
#define	SUN_BOOTSIZE	8192

/* partition info */
struct sun_dkpart {
	u_int32_t	sdkp_cyloffset;		/* starting cylinder */
	u_int32_t	sdkp_nsectors;		/* number of sectors */
};

struct sun_vtoc_info {
	u_int16_t	svtoc_tag;		/* partition tag */
	u_int16_t	svtoc_flag;		/* partition flags */
};

/* known partition tag values */
#define	VTOC_UNASSIGNED	0x00
#define	VTOC_BOOT	0x01
#define	VTOC_ROOT	0x02
#define	VTOC_SWAP	0x03
#define	VTOC_USR	0x04
#define	VTOC_BACKUP	0x05	/* "c" partition, covers entire disk */
#define	VTOC_STAND	0x06
#define	VTOC_VAR	0x07
#define	VTOC_HOME	0x08
#define	VTOC_ALTSCTR	0x09	/* alternate sector partition */
#define	VTOC_CACHE	0x0a	/* Solaris cachefs partition */
#define	VTOC_VXVM_PUB	0x0e	/* VxVM public region */
#define	VTOC_VXVM_PRIV	0x0f	/* VxVM private region */

/* VTOC partition flags */
#define	VTOC_UNMNT	0x01	/* unmountable partition */
#define	VTOC_RONLY	0x10	/* partition is read/only */

struct sun_disklabel {
	char		sl_text[128];

	/* SVR4 VTOC information */
	u_int32_t	sl_vtoc_vers;		/* == SUN_VTOC_VERSION */
	char		sl_vtoc_volname[SUN_VOLNAME_LEN];
	u_int16_t	sl_vtoc_nparts;		/* == SUN_NPART */
	struct sun_vtoc_info sl_vtoc_map[SUN_NPART]; /* partition tag/flag */
	u_int32_t	sl_vtoc_sane;		/* == SUN_VTOC_SANE */

	/* Sun label information */
	u_int16_t	sl_rpm;			/* rotational speed */
	u_int16_t	sl_pcylinders;		/* number of physical cyls */
	u_int16_t	sl_sparespercyl;	/* spare sectors per cylinder */
	u_int16_t	sl_interleave;		/* interleave factor */
	u_int16_t	sl_ncylinders;		/* data cylinders */
	u_int16_t	sl_acylinders;		/* alternate cylinders */
	u_int16_t	sl_ntracks;		/* tracks per cylinder */
	u_int16_t	sl_nsectors;		/* sectors per track */
	struct sun_dkpart sl_part[SUN_NPART];	/* partition layout */
	u_int16_t	sl_magic;		/* == SUN_DKMAGIC */
};

int sunlabel_dec(void const *pp, struct sun_disklabel *sl);
void sunlabel_enc(void *pp, struct sun_disklabel *sl);

#endif /* _SYS_SUN_DISKLABEL_H_ */
