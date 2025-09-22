/*	$OpenBSD: ieeefp.h,v 1.4 2009/09/27 21:23:55 martynas Exp $	*/

/* 
 * Written by J.T. Conklin, Apr 6, 1995
 * Public domain.
 */

#ifndef _IEEEFP_H_
#define _IEEEFP_H_

#include <sys/cdefs.h>
#include <machine/ieeefp.h>

__BEGIN_DECLS
extern fp_rnd    fpgetround(void);
extern fp_rnd    fpsetround(fp_rnd);
extern fp_except fpgetmask(void);
extern fp_except fpsetmask(fp_except);
extern fp_except fpgetsticky(void);
extern fp_except fpsetsticky(fp_except);
__END_DECLS

#endif /* _IEEEFP_H_ */
