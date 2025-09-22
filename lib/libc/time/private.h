/*	$OpenBSD: private.h,v 1.39 2019/05/12 12:49:52 schwarze Exp $	*/
#ifndef PRIVATE_H

#define PRIVATE_H

/*
** This file is in the public domain, so clarified as of
** 1996-06-05 by Arthur David Olson.
*/

/* OpenBSD defaults */
#define PCTS			1
#define ALL_STATE		1
#define STD_INSPIRED		1

/*
** This header is for use ONLY with the time conversion code.
** There is no guarantee that it will remain unchanged,
** or that it will remain at all.
** Do NOT copy it to any system include directory.
** Thank you!
*/

#define GRANDPARENTED	"Local time zone must be set--see zic manual page"

/*
** Nested includes
*/

#include <limits.h>	/* for CHAR_BIT et al. */
#include <time.h>

/*
** Finally, some convenience items.
*/

#ifndef TRUE
#define TRUE	1
#endif /* !defined TRUE */

#ifndef FALSE
#define FALSE	0
#endif /* !defined FALSE */

#ifndef TYPE_BIT
#define TYPE_BIT(type)	(sizeof (type) * CHAR_BIT)
#endif /* !defined TYPE_BIT */

#ifndef TYPE_SIGNED
#define TYPE_SIGNED(type) (((type) -1) < 0)
#endif /* !defined TYPE_SIGNED */

#ifndef INT_STRLEN_MAXIMUM
/*
** 302 / 1000 is log10(2.0) rounded up.
** Subtract one for the sign bit if the type is signed;
** add one for integer division truncation;
** add one more for a minus sign if the type is signed.
*/
#define INT_STRLEN_MAXIMUM(type) \
	((TYPE_BIT(type) - TYPE_SIGNED(type)) * 302 / 1000 + \
	1 + TYPE_SIGNED(type))
#endif /* !defined INT_STRLEN_MAXIMUM */

#define _(msgid) msgid

#ifndef TZ_DOMAIN
#define TZ_DOMAIN "tz"
#endif /* !defined TZ_DOMAIN */

#ifndef YEARSPERREPEAT
#define YEARSPERREPEAT		400	/* years before a Gregorian repeat */
#endif /* !defined YEARSPERREPEAT */

/*
** The Gregorian year averages 365.2425 days, which is 31556952 seconds.
*/

#ifndef AVGSECSPERYEAR
#define AVGSECSPERYEAR		31556952L
#endif /* !defined AVGSECSPERYEAR */

#ifndef SECSPERREPEAT
#define SECSPERREPEAT		((int_fast64_t) YEARSPERREPEAT * (int_fast64_t) AVGSECSPERYEAR)
#endif /* !defined SECSPERREPEAT */

#ifndef SECSPERREPEAT_BITS
#define SECSPERREPEAT_BITS	34	/* ceil(log2(SECSPERREPEAT)) */
#endif /* !defined SECSPERREPEAT_BITS */

#endif /* !defined PRIVATE_H */
