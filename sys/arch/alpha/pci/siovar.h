/*	$OpenBSD: siovar.h,v 1.15 2014/02/18 19:37:33 miod Exp $	*/
/*	$NetBSD: siovar.h,v 1.5 1996/10/23 04:12:34 cgd Exp $	*/

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
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

#include <sys/evcount.h>

void	sio_intr_setup(pci_chipset_tag_t, bus_space_tag_t);
void	sio_intr_shutdown(void);
void	sio_iointr(void *framep, unsigned long vec);

const char *sio_intr_string(void *, int);
int	sio_intr_line(void *, int);
void	*sio_intr_establish(void *, int, int, int, int (*)(void *),
	    void *, const char *);
void	sio_intr_disestablish(void *, void *);
