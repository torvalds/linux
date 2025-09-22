/*	$OpenBSD: cd9660_util.c,v 1.11 2021/03/05 07:01:36 jsg Exp $	*/
/*	$NetBSD: cd9660_util.c,v 1.12 1997/01/24 00:27:33 cgd Exp $	*/

/*-
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp). Joliet support was added by
 * Joachim Kuebart (joki@kuebart.stuttgart.netsurf.de).
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
 *	@(#)cd9660_util.c	8.3 (Berkeley) 12/5/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/resourcevar.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/dirent.h>

#include <isofs/cd9660/iso.h>

/*
 * XXX: limited support for loading of Unicode
 * conversion routine as a kld at a run-time.
 * Should be removed when native Unicode kernel
 * interfaces have been introduced.
 */
u_char (*cd9660_wchar2char)(u_int32_t wchar) = NULL;

/*
 * Get one character out of an iso filename
 * Obey joliet_level
 * Return number of bytes consumed
 */
int
isochar(const u_char *isofn, const u_char *isoend, int joliet_level, u_char *c)
{
      *c = *isofn++;
      if (joliet_level == 0 || isofn == isoend)
              /* (00) and (01) are one byte in Joliet, too */
              return 1;

      /* No Unicode support yet :-( */
      switch (*c) {
      default:
              *c = '?';
              break;
      case '\0':
              *c = *isofn;
              break;
      }

      /* XXX: if Unicode conversion routine is loaded then use it */
      if (cd9660_wchar2char != NULL)
	      *c = cd9660_wchar2char((*(isofn - 1) << 8) | *isofn);

      return 2;
}

/*
 * translate and compare a filename
 * returns (fn - isofn)
 * Note: Version number plus ';' may be omitted.
 */
int
isofncmp(const u_char *fn, int fnlen, const u_char *isofn, int isolen,
    int joliet_level)
{
	int i, j;
	u_char c;
	const u_char *fnend = fn + fnlen, *isoend = isofn + isolen;
	
	for (; fn != fnend; fn++) {
		if (isofn == isoend)
			return *fn;
		isofn += isochar(isofn, isoend, joliet_level, &c);
		if (c == ';') {
			if (*fn++ != ';')
				return fn[-1];
			for (i = 0; fn != fnend; i = i * 10 + *fn++ - '0') {
				if (*fn < '0' || *fn > '9') {
					return -1;
				}
			}
			for (j = 0; isofn != isoend; j = j * 10 + c - '0')
				isofn += isochar(isofn, isoend,
				    joliet_level, &c);
			return i - j;
		}
		if (((u_char) c) != *fn) {
			if (c >= 'A' && c <= 'Z') {
				if (c + ('a' - 'A') != *fn) {
					if (*fn >= 'a' && *fn <= 'z')
						return *fn - ('a' - 'A') - c;
					else
						return *fn - c;
				}
			} else
				return *fn - c;
		}
	}
	if (isofn != isoend) {
		isofn += isochar(isofn, isoend, joliet_level, &c);
		switch (c) {
		default:
			return -c;
		case '.':
			if (isofn != isoend) {
				isochar(isofn, isoend, joliet_level, &c);
				if (c == ';')
					return 0;
			}
			return -1;
		case ';':
			return 0;
		}
	}
	return 0;
}

/*
 * translate a filename of length > 0
 */
void
isofntrans(u_char *infn, int infnlen, u_char *outfn, u_short *outfnlen,
    int original, int assoc, int joliet_level)
{
	int fnidx = 0;
	u_char c, d = '\0', *infnend = infn + infnlen;
	
	if (assoc) {
		*outfn++ = ASSOCCHAR;
		fnidx++;
	}
	for (; infn != infnend; fnidx++) {
		infn += isochar(infn, infnend, joliet_level, &c);
		
		if (!original && c == ';') {
			fnidx -= (d == '.');
			break;
		} else
			*outfn++ = c;
		d = c;
	}
	*outfnlen = fnidx;
}
