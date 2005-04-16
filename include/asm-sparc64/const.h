/* const.h: Macros for dealing with constants.  */

#ifndef _SPARC64_CONST_H
#define _SPARC64_CONST_H

/* Some constant macros are used in both assembler and
 * C code.  Therefore we cannot annotate them always with
 * 'UL' and other type specificers unilaterally.  We
 * use the following macros to deal with this.
 */

#ifdef __ASSEMBLY__
#define _AC(X,Y)	X
#else
#define _AC(X,Y)	(X##Y)
#endif


#endif /* !(_SPARC64_CONST_H) */
