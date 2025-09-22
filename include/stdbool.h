/* $OpenBSD: stdbool.h,v 1.7 2015/09/04 23:47:09 daniel Exp $ */

/*
 * Written by Marc Espie, September 25, 1999
 * Public domain.
 */

#ifndef	_STDBOOL_H_
#define	_STDBOOL_H_	

#ifndef __cplusplus

#if defined(__GNUC__) || \
	(defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901)
/* Support for C99: type _Bool is already built-in. */
#define false	0
#define true	1

#else
/* `_Bool' type must promote to `int' or `unsigned int'. */
typedef enum {
	false = 0,
	true = 1
} _Bool;

/* And those constants must also be available as macros. */
#define	false	false
#define	true	true

#endif

/* User visible type `bool' is provided as a macro which may be redefined */
#define bool _Bool

#else /* __cplusplus */
#define _Bool 	bool
#define bool 	bool
#define false 	false
#define true 	true
#endif /* __cplusplus */

/* Inform that everything is fine */
#define __bool_true_false_are_defined 1

#endif /* _STDBOOL_H_ */
