/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)isctype.c	8.3 (Berkeley) 2/24/94";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>

#undef digittoint
int
digittoint(int c)
{
	return (__sbmaskrune(c, 0xFF));
}

#undef isalnum
int
isalnum(int c)
{
	return (__sbistype(c, _CTYPE_A|_CTYPE_N));
}

#undef isalpha
int
isalpha(int c)
{
	return (__sbistype(c, _CTYPE_A));
}

#undef isascii
int
isascii(int c)
{
	return ((c & ~0x7F) == 0);
}

#undef isblank
int
isblank(int c)
{
	return (__sbistype(c, _CTYPE_B));
}

#undef iscntrl
int
iscntrl(int c)
{
	return (__sbistype(c, _CTYPE_C));
}

#undef isdigit
int
isdigit(int c)
{
	return (__isctype(c, _CTYPE_D));
}

#undef isgraph
int
isgraph(int c)
{
	return (__sbistype(c, _CTYPE_G));
}

#undef ishexnumber 
int
ishexnumber(int c)
{
	return (__sbistype(c, _CTYPE_X));
}

#undef isideogram
int
isideogram(int c)
{
	return (__sbistype(c, _CTYPE_I));
}

#undef islower
int
islower(int c)
{
	return (__sbistype(c, _CTYPE_L));
}

#undef isnumber
int
isnumber(int c)
{
	return (__sbistype(c, _CTYPE_N));
}

#undef isphonogram	
int
isphonogram(int c)
{
	return (__sbistype(c, _CTYPE_Q));
}

#undef isprint
int
isprint(int c)
{
	return (__sbistype(c, _CTYPE_R));
}

#undef ispunct
int
ispunct(int c)
{
	return (__sbistype(c, _CTYPE_P));
}

#undef isrune
int
isrune(int c)
{
	return (__sbistype(c, 0xFFFFFF00L));
}

#undef isspace
int
isspace(int c)
{
	return (__sbistype(c, _CTYPE_S));
}

#undef isspecial
int
isspecial(int c)
{
	return (__sbistype(c, _CTYPE_T));
}

#undef isupper
int
isupper(int c)
{
	return (__sbistype(c, _CTYPE_U));
}

#undef isxdigit
int
isxdigit(int c)
{
	return (__isctype(c, _CTYPE_X));
}

#undef toascii
int
toascii(int c)
{
	return (c & 0x7F);
}

#undef tolower
int
tolower(int c)
{
	return (__sbtolower(c));
}

#undef toupper
int
toupper(int c)
{
	return (__sbtoupper(c));
}

