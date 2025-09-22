/*	$OpenBSD: disklabel.h,v 1.8 2010/04/25 06:15:17 deraadt Exp $	*/
/*	$NetBSD: disklabel.h,v 1.2 1998/08/22 14:55:28 mrg Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 */

/*
 * SunOS disk label layout (only relevant portions discovered here).
 * This describes the format typically found on SPARC systems, but not
 * that usually seen on SunOS/x86 and SunOS/amd64 systems.
 */

#define	SUN_DKMAGIC	55998

/* partition info */
struct sun_dkpart {
	u_int	sdkp_cyloffset;		/* starting cylinder */
	u_int	sdkp_nsectors;		/* number of sectors */
};

/* partition types */
struct sun_partinfo {
	u_short	spi_tag;		/* filesystem type */
	u_short	spi_flag;		/* flags */
};

/* some spi_tag values */
#define SPTAG_EMPTY		0x00
#define SPTAG_BOOT		0x01
#define SPTAG_SUNOS_ROOT	0x02
#define SPTAG_SUNOS_SWAP	0x03
#define SPTAG_SUNOS_USR		0x04
#define SPTAG_WHOLE_DISK	0x05
#define SPTAG_SUNOS_STAND	0x06
#define SPTAG_SUNOS_VAR		0x07
#define SPTAG_SUNOS_HOME	0x08
#define SPTAG_LINUX_SWAP	0x82
#define SPTAG_LINUX_EXT2	0x83

#define	SUNXPART	8
#define	SL_XPMAG	(0x199d1fe2+SUNXPART)
#define	SL_XPMAGTYP	(0x199d1fe2+SUNXPART+1)		/* contains types */

struct sun_disklabel {			/* total size = 512 bytes */
	char		sl_text[128];
	u_int		sl_xpsum;	/* additive cksum, [xl_xpmag,sl_xx1) */
	u_int		sl_xpmag;	/* "extended" magic number */
	struct sun_dkpart sl_xpart[SUNXPART];	/* "extended" partitions i..p */
	u_char		sl_types[MAXPARTITIONS];
	u_int8_t	sl_fragblock[MAXPARTITIONS];
	u_int16_t	sl_cpg[MAXPARTITIONS];
	u_char		sl_uid[8];	/* extension: unique disk label */
	char		sl_xxx1[292 - sizeof(u_int) - sizeof(u_int) -
			    (sizeof(struct sun_dkpart) * SUNXPART) -
			    sizeof(u_char) * MAXPARTITIONS -
			    sizeof(u_int8_t) * MAXPARTITIONS -
			    sizeof(u_int16_t) * MAXPARTITIONS -
			    sizeof(u_int64_t)];
	u_short sl_rpm;			/* rotational speed */
	u_short	sl_pcylinders;		/* number of physical cyls */
#define	sl_pcyl	sl_pcylinders		/* XXX: old sun3 */
	u_short sl_sparespercyl;	/* spare sectors per cylinder */
	char	sl_xxx3[4];
	u_short sl_interleave;		/* interleave factor */
	u_short	sl_ncylinders;		/* data cylinders */
	u_short	sl_acylinders;		/* alternate cylinders */
	u_short	sl_ntracks;		/* tracks per cylinder */
	u_short	sl_nsectors;		/* sectors per track */
	char	sl_xxx4[4];
	struct sun_dkpart sl_part[8];	/* partition layout */
	u_short	sl_magic;		/* == SUN_DKMAGIC */
	u_short	sl_cksum;		/* xor checksum of all shorts */
};

/* Sun standard fields, also used on Linux. */
struct sun_preamble {
	char	sl_text[128];
	u_int	sl_version;	/* label version */
	char	sl_volume[8];	/* short volume name */
	u_short	sl_nparts;	/* partition count */

	struct sun_partinfo sl_part[8];
};
