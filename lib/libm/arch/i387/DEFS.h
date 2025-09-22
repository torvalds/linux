/*	$OpenBSD: DEFS.h,v 1.1 2016/09/12 19:47:02 guenther Exp $	*/

/*
 * Written by Philip Guenther <guenther@openbsd.org>
 */

#include <machine/asm.h>

/*
 * We define a hidden alias with the prefix "_libm_" for each global symbol
 * that may be used internally.  By referencing _libm_x instead of x, other
 * parts of libm prevent overriding by the application and avoid unnecessary
 * relocations.
 */
#define _HIDDEN(x)		_libm_##x
#define _HIDDEN_ALIAS(x,y)			\
	STRONG_ALIAS(_HIDDEN(x),y);		\
	.hidden _HIDDEN(x)
#define _HIDDEN_FALIAS(x,y)			\
	_HIDDEN_ALIAS(x,y);			\
	.type _HIDDEN(x),@function

/*
 * For functions implemented in ASM that are used internally
 *   END_STD(x)	Like DEF_STD() in C; for standard/reserved C names
 *   END_NONSTD(x)	Like DEF_NONSTD() in C; for non-ISO C names
 */
#define	END_STD(x)	END(x); _HIDDEN_FALIAS(x,x); END(_HIDDEN(x))
#define	END_NONSTD(x)	END_STD(x); .weak x
