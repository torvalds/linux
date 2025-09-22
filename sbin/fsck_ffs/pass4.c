/*	$OpenBSD: pass4.c,v 1.26 2020/06/20 07:49:04 otto Exp $	*/
/*	$NetBSD: pass4.c,v 1.11 1996/09/27 22:45:17 christos Exp $	*/

/*
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#include <sys/param.h>	/* isset clrbit */
#include <sys/time.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsutil.h"
#include "fsck.h"
#include "extern.h"

static ino_t info_inumber;

static int
pass4_info(char *buf, size_t buflen)
{
	return (snprintf(buf, buflen, "phase 4, inode %llu/%llu",
	    (unsigned long long)info_inumber,
	    (unsigned long long)lastino) > 0);
}

void
pass4(void)
{
	ino_t inumber;
	struct zlncnt *zlnp;
	union dinode *dp;
	struct inodesc idesc;
	int n, i;
	u_int c;

	memset(&idesc, 0, sizeof(struct inodesc));
	idesc.id_type = ADDR;
	idesc.id_func = pass4check;
	info_fn = pass4_info;
	for (c = 0; c < sblock.fs_ncg; c++) {
		inumber = c * sblock.fs_ipg;
		for (i = 0; i < inostathead[c].il_numalloced; i++, inumber++) {
			if (inumber < ROOTINO)
				continue;
			idesc.id_number = inumber;
			switch (GET_ISTATE(inumber)) {

			case FSTATE:
			case DFOUND:
				n = ILNCOUNT(inumber);
				if (n) {
					adjust(&idesc, (short)n);
					break;
				}
				for (zlnp = zlnhead; zlnp; zlnp = zlnp->next)
					if (zlnp->zlncnt == inumber) {
						zlnp->zlncnt = zlnhead->zlncnt;
						zlnp = zlnhead;
						zlnhead = zlnhead->next;
						free((char *)zlnp);
						clri(&idesc, "UNREF", 1);
						break;
					}
				break;

			case DSTATE:
				clri(&idesc, "UNREF", 1);
				break;

			case DCLEAR:
				dp = ginode(inumber);
				if (DIP(dp, di_size) == 0) {
					clri(&idesc, "ZERO LENGTH", 1);
					break;
				}
				/* FALLTHROUGH */
			case FCLEAR:
				clri(&idesc, "BAD/DUP", 1);
				break;

			case USTATE:
				break;

			default:
				errexit("BAD STATE %d FOR INODE I=%llu\n",
				    GET_ISTATE(inumber),
				    (unsigned long long)inumber);
			}
		}
	}
	info_fn = NULL;
}

int
pass4check(struct inodesc *idesc)
{
	struct dups *dlp;
	int nfrags, res = KEEPON;
	daddr_t blkno = idesc->id_blkno;

	for (nfrags = idesc->id_numfrags; nfrags > 0; blkno++, nfrags--) {
		if (chkrange(blkno, 1)) {
			res = SKIP;
		} else if (testbmap(blkno)) {
			for (dlp = duplist; dlp; dlp = dlp->next) {
				if (dlp->dup != blkno)
					continue;
				dlp->dup = duplist->dup;
				dlp = duplist;
				duplist = duplist->next;
				free(dlp);
				break;
			}
			if (dlp == 0) {
				clrbmap(blkno);
				n_blks--;
			}
		}
	}
	return (res);
}
