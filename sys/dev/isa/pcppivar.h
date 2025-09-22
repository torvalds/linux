/* $OpenBSD: pcppivar.h,v 1.3 2002/03/14 01:26:56 millert Exp $ */
/* $NetBSD: pcppivar.h,v 1.1 1998/04/15 20:26:18 drochner Exp $ */

/*
 * Copyright (c) 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

typedef void *pcppi_tag_t;

struct pcppi_attach_args {
	pcppi_tag_t pa_cookie;
};

#define PCPPI_BELL_SLEEP	0x01	/* synchronous; sleep for complete */
#define PCPPI_BELL_POLL		0x02	/* synchronous; poll for complete */

void pcppi_bell(pcppi_tag_t, int, int, int);
