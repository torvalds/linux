/*	$OpenBSD: uchar.h,v 1.1 2023/08/20 15:02:51 schwarze Exp $	*/
/*
 * Written by Ingo Schwarze <schwarze@openbsd.org>
 * and placed in the public domain on March 19, 2022.
 */

#ifndef _LIBC_UCHAR_H_
#define _LIBC_UCHAR_H_

#include_next <uchar.h>

PROTO_STD_DEPRECATED(c16rtomb);
PROTO_STD_DEPRECATED(c32rtomb);
PROTO_STD_DEPRECATED(mbrtoc16);
PROTO_STD_DEPRECATED(mbrtoc32);

#endif /* !_LIBC_UCHAR_H_ */
