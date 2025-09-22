/*	$OpenBSD: ioctl_fd.h,v 1.2 2011/03/23 16:54:37 pirofti Exp $	*/
/*	from: ioctl_fd.h,v 1.4 1995/06/29 03:49:32 jtk Exp 	*/

/*
 * Copyright (C) 1992-1994 by Joerg Wunsch, Dresden
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * From: Id: ioctl_fd.h,v 1.7 1994/10/30 19:17:39 joerg Exp
 */

#ifndef _MACHINE_IOCTL_FD_H_
#define _MACHINE_IOCTL_FD_H_

#define FD_FORMAT_VERSION 110	/* used to validate before formatting */
#define FD_MAX_NSEC 36		/* highest known number of spt - allow for */
				/* 2.88 MB drives */

struct fd_formb {
	int format_version;	/* == FD_FORMAT_VERSION */
	int cyl, head;
	int transfer_rate;	/* fdreg.h: FDC_???KBPS */

	union {
		struct fd_form_data {
			/*
			 * DO NOT CHANGE THE LAYOUT OF THIS STRUCTS
			 * it is hardware-dependant since it exactly
			 * matches the byte sequence to write to FDC
			 * during its `format track' operation
			 */
			u_char secshift; /* 0 -> 128, ...; usually 2 -> 512 */
			u_char nsecs;	/* must be <= FD_MAX_NSEC */
			u_char gaplen;	/* GAP 3 length; usually 84 */
			u_char fillbyte; /* usually 0xf6 */
			struct fd_idfield_data {
				/*
				 * data to write into id fields;
				 * for obscure formats, they mustn't match
				 * the real values (but mostly do)
				 */
				u_char cylno;	/* 0 thru 79 (or 39) */
				u_char headno;	/* 0, or 1 */
				u_char secno;	/* starting at 1! */
				u_char secsize;	/* usually 2 */
			} idfields[FD_MAX_NSEC]; /* 0 <= idx < nsecs used */
		} structured;
		u_char raw[1];	/* to have continuous indexed access */
	} format_info;
};

/* make life easier */
# define fd_formb_secshift   format_info.structured.secshift
# define fd_formb_nsecs      format_info.structured.nsecs
# define fd_formb_gaplen     format_info.structured.gaplen
# define fd_formb_fillbyte   format_info.structured.fillbyte
/* these data must be filled in for(i = 0; i < fd_formb_nsecs; i++) */
# define fd_formb_cylno(i)   format_info.structured.idfields[i].cylno
# define fd_formb_headno(i)  format_info.structured.idfields[i].headno
# define fd_formb_secno(i)   format_info.structured.idfields[i].secno
# define fd_formb_secsize(i) format_info.structured.idfields[i].secsize

/*
 * Floppies come in various flavors, e.g., 1.2MB vs 1.44MB; here is how
 * we tell them apart.
 */
struct fd_type {
	int	sectrac;	/* sectors per track */
	int	heads;		/* number of heads */
	int	seccyl;		/* sectors per cylinder */
	int	secsize;	/* size code for sectors */
	int	datalen;	/* data len when secsize = 0 */
	int	steprate;	/* step rate and head unload time */
	int	gap1;		/* gap len between sectors */
	int	gap2;		/* formatting gap */
	int	tracks;		/* total num of tracks */
	int	size;		/* size of disk in sectors */
	int	step;		/* steps per cylinder */
	int	rate;		/* transfer speed code */
	char	*name;
};


#define FD_FORM   _IOW('F', 61, struct fd_formb) /* format a track */
#define FD_GTYPE  _IOR('F', 62, struct fd_type)  /* get drive type */
#define FD_STYPE  _IOW('F', 63, struct fd_type)  /* set drive type */

#define FD_GOPTS  _IOR('F', 64, int) /* drive options, see below */
#define FD_SOPTS  _IOW('F', 65, int)

#define FDOPT_NORETRY 0x0001	/* no retries on failure (cleared on close) */
#define FDOPT_SILENT  0x0002

/*
 * The following definitions duplicate those in sys/i386/isa/fdreg.h
 * They are here since their values are to be used in the above
 * structure when formatting a floppy. For very obvious reasons, both
 * definitions must match ;-)
 */
#ifndef FDC_500KBPS
#define	FDC_500KBPS	0x00	/* 500KBPS MFM drive transfer rate */
#define	FDC_300KBPS	0x01	/* 300KBPS MFM drive transfer rate */
#define	FDC_250KBPS	0x02	/* 250KBPS MFM drive transfer rate */
#define	FDC_125KBPS	0x03	/* 125KBPS FM drive transfer rate */
				/* for some controllers 1MPBS instead */
#endif /* FDC_500KBPS */


#endif /* !_MACHINE_IOCTL_FD_H__ */
