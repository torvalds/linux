/*	$OpenBSD: fdvar.h,v 1.3 2008/06/26 05:42:13 ray Exp $	*/
/*	$NetBSD: fdvar.h,v 1.12 2003/07/11 12:09:13 pk Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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

#define	FD_BSIZE(fd)	(128 << fd->sc_type->secsize)
#define	FDC_MAXIOSIZE	NBPG	/* XXX should be MAXBSIZE */

#define FDC_NSTATUS	10

struct fdcio {
	bus_space_handle_t	fdcio_handle;

	/*
	 * Interrupt state.
	 */
	int	fdcio_itask;
	int	fdcio_istatus;

	/*
	 * IO state.
	 */
	char	*fdcio_data;		/* pseudo-DMA data */
	int	fdcio_tc;		/* pseudo-DMA Terminal Count */
	u_char	fdcio_status[FDC_NSTATUS];	/* copy of registers */
	int	fdcio_nstat;		/* # of valid status bytes */
};

/* itask values */
#define FDC_ITASK_NONE		0	/* No HW interrupt expected */
#define FDC_ITASK_SENSEI	1	/* Do SENSEI on next HW interrupt */
#define FDC_ITASK_DMA		2	/* Do Pseudo-DMA */
#define FDC_ITASK_RESULT	3	/* Pick up command results */

/* istatus values */
#define FDC_ISTATUS_NONE	0	/* No status available */
#define FDC_ISTATUS_SPURIOUS	1	/* Spurious HW interrupt detected */
#define FDC_ISTATUS_ERROR	2	/* Operation completed abnormally */
#define FDC_ISTATUS_DONE	3	/* Operation completed normally */
