/*	$OpenBSD: _null.h,v 1.2 2016/09/09 22:07:58 millert Exp $	*/

/*
 * Written by Todd C. Miller, September 9, 2016
 * Public domain.
 */

#ifndef NULL
#if !defined(__cplusplus)
#define	NULL	((void *)0)
#elif __cplusplus >= 201103L
#define	NULL	nullptr
#elif defined(__GNUG__)
#define	NULL	__null
#else
#define	NULL	0L
#endif
#endif
