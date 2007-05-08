/* const.h: Macros for dealing with constants.  */

#ifndef _X86_64_CONST_H
#define _X86_64_CONST_H

/* Some constant macros are used in both assembler and
 * C code.  Therefore we cannot annotate them always with
 * 'UL' and other type specificers unilaterally.  We
 * use the following macros to deal with this.
 */

#ifdef __ASSEMBLY__
#define _AC(X,Y)	X
#else
#define __AC(X,Y)	(X##Y)
#define _AC(X,Y)	__AC(X,Y)
#endif


#endif /* !(_X86_64_CONST_H) */
