/*
 *   Copyright (C) International Business Machines Corp., 2001
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _H_ENDIAN24
#define	_H_ENDIAN24

/*
 *	endian24.h:
 *
 * Endian conversion for 24-byte data
 *
 */
#define __swab24(x) \
({ \
	__u32 __x = (x); \
	((__u32)( \
		((__x & (__u32)0x000000ffUL) << 16) | \
		 (__x & (__u32)0x0000ff00UL)	    | \
		((__x & (__u32)0x00ff0000UL) >> 16) )); \
})

#if (defined(__KERNEL__) && defined(__LITTLE_ENDIAN)) || (defined(__BYTE_ORDER) && (__BYTE_ORDER == __LITTLE_ENDIAN))
	#define __cpu_to_le24(x) ((__u32)(x))
	#define __le24_to_cpu(x) ((__u32)(x))
#else
	#define __cpu_to_le24(x) __swab24(x)
	#define __le24_to_cpu(x) __swab24(x)
#endif

#ifdef __KERNEL__
	#define cpu_to_le24 __cpu_to_le24
	#define le24_to_cpu __le24_to_cpu
#endif

#endif				/* !_H_ENDIAN24 */
