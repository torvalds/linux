/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Borman at Krystal Technologies.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)runetype.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD$
 */

#ifndef	_RUNETYPE_H_
#define	_RUNETYPE_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#define	_CACHED_RUNES	(1 <<8 )	/* Must be a power of 2 */
#define	_CRMASK		(~(_CACHED_RUNES - 1))

/*
 * The lower 8 bits of runetype[] contain the digit value of the rune.
 */
typedef struct {
	__rune_t	__min;		/* First rune of the range */
	__rune_t	__max;		/* Last rune (inclusive) of the range */
	__rune_t	__map;		/* What first maps to in maps */
	unsigned long	*__types;	/* Array of types in range */
} _RuneEntry;

typedef struct {
	int		__nranges;	/* Number of ranges stored */
	_RuneEntry	*__ranges;	/* Pointer to the ranges */
} _RuneRange;

typedef struct {
	char		__magic[8];	/* Magic saying what version we are */
	char		__encoding[32];	/* ASCII name of this encoding */

	__rune_t	(*__sgetrune)(const char *, __size_t, char const **);
	int		(*__sputrune)(__rune_t, char *, __size_t, char **);
	__rune_t	__invalid_rune;

	unsigned long	__runetype[_CACHED_RUNES];
	__rune_t	__maplower[_CACHED_RUNES];
	__rune_t	__mapupper[_CACHED_RUNES];

	/*
	 * The following are to deal with Runes larger than _CACHED_RUNES - 1.
	 * Their data is actually contiguous with this structure so as to make
	 * it easier to read/write from/to disk.
	 */
	_RuneRange	__runetype_ext;
	_RuneRange	__maplower_ext;
	_RuneRange	__mapupper_ext;

	void		*__variable;	/* Data which depends on the encoding */
	int		__variable_len;	/* how long that data is */
} _RuneLocale;

#define	_RUNE_MAGIC_1	"RuneMagi"	/* Indicates version 0 of RuneLocale */
__BEGIN_DECLS
extern const _RuneLocale _DefaultRuneLocale;
extern const _RuneLocale *_CurrentRuneLocale;
#if defined(__NO_TLS) || defined(__RUNETYPE_INTERNAL)
extern const _RuneLocale *__getCurrentRuneLocale(void);
#else
extern _Thread_local const _RuneLocale *_ThreadRuneLocale;
static __inline const _RuneLocale *__getCurrentRuneLocale(void)
{

	if (_ThreadRuneLocale) 
		return _ThreadRuneLocale;
	return _CurrentRuneLocale;
}
#endif /* __NO_TLS || __RUNETYPE_INTERNAL */
#define _CurrentRuneLocale (__getCurrentRuneLocale())
__END_DECLS

#endif	/* !_RUNETYPE_H_ */
