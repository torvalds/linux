/*	$OpenBSD: clockvar.h,v 1.5 2002/03/14 01:26:26 millert Exp $	*/
/*	$NetBSD: clockvar.h,v 1.2 1996/04/17 22:01:21 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
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

/*
 * Definitions for "cpu-independent" clock handling for the alpha.
 */

/*
 * clocktime structure:
 *
 * structure passed to TOY clocks when setting them.  broken out this
 * way, so that the time_t -> field conversion can be shared.
 */
struct clocktime {
	int	year;			/* year - 1900 */
	int	mon;			/* month (1 - 12) */
	int	day;			/* day (1 - 31) */
	int	hour;			/* hour (0 - 23) */
	int	min;			/* minute (0 - 59) */
	int	sec;			/* second (0 - 59) */
	int	dow;			/* day of week (0 - 6; 0 = Sunday) */
};

/*
 * clockfns structure:
 *
 * function switch used by chip-independent clock code, to access
 * chip-dependent routines.
 */
struct clockfns {
	void	(*cf_init)(struct device *);
	void	(*cf_get)(struct device *, time_t, struct clocktime *);
	void	(*cf_set)(struct device *, struct clocktime *);
};

void clockattach(struct device *, const struct clockfns *);
