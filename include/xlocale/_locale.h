/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by David Chisnall under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _XLOCALE_LOCALE_H
#define _XLOCALE_LOCALE_H

/* Bit shifting order of LC_*_MASK should match XLC_* and LC_* order. */
#define LC_COLLATE_MASK  (1<<0)
#define LC_CTYPE_MASK    (1<<1)
#define LC_MONETARY_MASK (1<<2)
#define LC_NUMERIC_MASK  (1<<3)
#define LC_TIME_MASK     (1<<4)
#define LC_MESSAGES_MASK (1<<5)
#define LC_ALL_MASK      (LC_COLLATE_MASK | LC_CTYPE_MASK | LC_MESSAGES_MASK | \
			  LC_MONETARY_MASK | LC_NUMERIC_MASK | LC_TIME_MASK)
#define LC_GLOBAL_LOCALE ((locale_t)-1)

#ifndef _LOCALE_T_DEFINED
#define _LOCALE_T_DEFINED
typedef struct	_xlocale *locale_t;
#endif

locale_t	 duplocale(locale_t base);
void		 freelocale(locale_t loc);
locale_t	 newlocale(int mask, const char *locale, locale_t base);
const char	*querylocale(int mask, locale_t loc);
locale_t	 uselocale(locale_t loc);

#endif /* _XLOCALE_LOCALE_H */
