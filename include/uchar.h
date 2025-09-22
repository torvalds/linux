/*	$OpenBSD: uchar.h,v 1.2 2023/09/05 23:16:01 schwarze Exp $	*/
/*
 * Written by Ingo Schwarze <schwarze@openbsd.org>
 * and placed in the public domain on March 19, 2022.
 */

#ifndef _UCHAR_H_
#define _UCHAR_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#ifndef _MBSTATE_T_DEFINED_
#define _MBSTATE_T_DEFINED_
typedef __mbstate_t	mbstate_t;
#endif

#ifndef _SIZE_T_DEFINED_
#define _SIZE_T_DEFINED_
typedef __size_t	size_t;
#endif

#define __STDC_UTF_16__	1
#define __STDC_UTF_32__	1

#if !defined(__cplusplus) || __cplusplus < 201103L
typedef __uint16_t	char16_t;
typedef __uint32_t	char32_t;
#endif

__BEGIN_DECLS
size_t	mbrtoc16(char16_t * __restrict, const char * __restrict, size_t,
	    mbstate_t * __restrict);
size_t	c16rtomb(char * __restrict, char16_t, mbstate_t * __restrict);
size_t	mbrtoc32(char32_t * __restrict, const char * __restrict, size_t,
	    mbstate_t * __restrict);
size_t	c32rtomb(char * __restrict, char32_t, mbstate_t * __restrict);
__END_DECLS

#endif /* !_UCHAR_H_ */
