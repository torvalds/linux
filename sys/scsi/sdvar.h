/*	$OpenBSD: sdvar.h,v 1.52 2020/09/23 15:24:16 krw Exp $	*/
/*	$NetBSD: sdvar.h,v 1.7 1998/08/17 00:49:03 mycroft Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Originally written by Julian Elischer (julian@dialix.oz.au)
 * for TRW Financial Systems for use under the MACH(2.5) operating system.
 *
 * TRW Financial Systems, in accordance with their agreement with Carnegie
 * Mellon University, makes this software available to CMU to distribute
 * or use in any manner that they see fit as long as this message is kept with
 * the software. For this reason TFS also grants any other persons or
 * organisations permission to use or modify this software.
 *
 * TFS supplies this software to be publicly redistributed
 * on the understanding that TFS is not responsible for the correct
 * functioning of this software in any circumstances.
 *
 * Ported to run under 386BSD by Julian Elischer (julian@dialix.oz.au) Sept 1992
 */

#ifndef _SCSI_SDVAR_H
#define _SCSI_SDVAR_H

#ifdef _KERNEL
struct sd_softc {
	struct device		sc_dev;
	struct disk		sc_dk;
	struct bufq		sc_bufq;

	int			flags;
#define	SDF_DIRTY	0x20		/* disk is dirty; needs cache flush */
#define	SDF_DYING	0x40		/* dying, when deactivated */
#define	SDF_THIN	0x01		/* disk is thin provisioned */
	struct scsi_link	*sc_link; /* contains our targ, lun, etc. */
	struct disk_parms {
		u_int32_t	heads;		/* number of heads */
		u_int32_t	cyls;		/* number of cylinders */
		u_int32_t	sectors;	/* number of sectors/track */
		u_int32_t	secsize;	/* number of bytes/sector */
		u_int64_t	disksize;	/* total number sectors */
		u_int32_t	unmap_sectors;	/* maximum sectors/unmap */
		u_int32_t	unmap_descs;	/* maximum descriptors/unmap */
	} params;

	struct scsi_xshandler sc_xsh;
};
#endif /* _KERNEL */
#endif /* _SCSI_SDVAR_H */
