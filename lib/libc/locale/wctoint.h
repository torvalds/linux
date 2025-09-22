/*	$OpenBSD: wctoint.h,v 1.3 2022/08/29 02:58:13 jsg Exp $	*/
/* $NetBSD: __wctoint.h,v 1.1 2001/09/28 11:25:37 yamt Exp $ */

/*-
 * Copyright (c)2001 Citrus Project,
 * All rights reserved.
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
 *	$Citrus: xpg4dl/FreeBSD/lib/libc/locale/__wctoint.h,v 1.1 2001/09/21 13:52:32 yamt Exp $
 */


static inline int
wctoint(wchar_t wc)
{
	int n;

	switch (wc) {
	case L'0': n = 0; break;
	case L'1': n = 1; break;
	case L'2': n = 2; break;
	case L'3': n = 3; break;
	case L'4': n = 4; break;
	case L'5': n = 5; break;
	case L'6': n = 6; break;
	case L'7': n = 7; break;
	case L'8': n = 8; break;
	case L'9': n = 9; break;
	case L'A': case L'a': n = 10; break;
	case L'B': case L'b': n = 11; break;
	case L'C': case L'c': n = 12; break;
	case L'D': case L'd': n = 13; break;
	case L'E': case L'e': n = 14; break;
	case L'F': case L'f': n = 15; break;
	case L'G': case L'g': n = 16; break;
	case L'H': case L'h': n = 17; break;
	case L'I': case L'i': n = 18; break;
	case L'J': case L'j': n = 19; break;
	case L'K': case L'k': n = 20; break;
	case L'L': case L'l': n = 21; break;
	case L'M': case L'm': n = 22; break;
	case L'N': case L'n': n = 23; break;
	case L'O': case L'o': n = 24; break;
	case L'P': case L'p': n = 25; break;
	case L'Q': case L'q': n = 26; break;
	case L'R': case L'r': n = 27; break;
	case L'S': case L's': n = 28; break;
	case L'T': case L't': n = 29; break;
	case L'U': case L'u': n = 30; break;
	case L'V': case L'v': n = 31; break;
	case L'W': case L'w': n = 32; break;
	case L'X': case L'x': n = 33; break;
	case L'Y': case L'y': n = 34; break;
	case L'Z': case L'z': n = 35; break;
	default: n = -1; break; /* error */
	}

	return n;
}
