/*	$OpenBSD: mcclockvar.h,v 1.4 2002/03/14 01:26:26 millert Exp $	*/
/*	$NetBSD: mcclockvar.h,v 1.2 1996/04/17 22:22:38 cgd Exp $	*/

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

struct mcclock_softc {
	struct device sc_dev;
	const struct mcclock_busfns *sc_busfns;
};

struct mcclock_busfns {
	void    (*mc_bf_write)(struct mcclock_softc *, u_int, u_int);
	u_int   (*mc_bf_read)(struct mcclock_softc *, u_int);
};

void	mcclock_attach(struct mcclock_softc *,
	    const struct mcclock_busfns *);
