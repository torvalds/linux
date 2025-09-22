/*	$OpenBSD: cdefs.h,v 1.4 2013/03/28 17:30:45 martynas Exp $	*/

#ifndef	_ARM_CDEFS_H_
#define	_ARM_CDEFS_H_

#define __strong_alias(alias,sym)					\
	__asm__(".global " __STRING(alias) " ; " __STRING(alias)	\
	    " = " __STRING(sym))
#define __weak_alias(alias,sym)						\
	__asm__(".weak " __STRING(alias) " ; " __STRING(alias)		\
	    " = " __STRING(sym))
#define	__warn_references(sym,msg)					\
	__asm__(".section .gnu.warning." __STRING(sym)			\
	    " ; .ascii \"" msg "\" ; .text")

#endif /* !_ARM_CDEFS_H_ */
