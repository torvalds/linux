/*	$OpenBSD: pass3.c,v 1.18 2015/01/20 18:22:21 deraadt Exp $	*/
/*	$NetBSD: pass3.c,v 1.8 1995/03/18 14:55:54 cgd Exp $	*/

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

#include <sys/time.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>
#include <stdio.h>
#include "fsck.h"
#include "extern.h"

static int info_pos;

static int
pass3_info(char *buf, size_t buflen)
{
	return (snprintf(buf, buflen, "phase 3, directory %d/%ld",
	    info_pos, inplast) > 0);
}

void
pass3(void)
{
	struct inoinfo **inpp, *inp, *pinp;
	ino_t orphan;
	int loopcnt;

	info_fn = pass3_info;
	for (inpp = &inpsort[inplast - 1]; inpp >= inpsort; inpp--) {
		info_pos++;
		inp = *inpp;
		if (inp->i_number == ROOTINO ||
		    (inp->i_parent != 0 && GET_ISTATE(inp->i_number) != DSTATE))
			continue;
		if (GET_ISTATE(inp->i_number) == DCLEAR)
			continue;
		for (loopcnt = 0; ; loopcnt++) {
			orphan = inp->i_number;
			if (inp->i_parent == 0 ||
			    GET_ISTATE(inp->i_parent) != DSTATE ||
			    loopcnt > numdirs)
				break;
			inp = getinoinfo(inp->i_parent);
		}
		if (linkup(orphan, inp->i_dotdot)) {
			inp->i_parent = inp->i_dotdot = lfdir;
			ILNCOUNT(lfdir)--;
			pinp = getinoinfo(inp->i_parent);
			inp->i_sibling = pinp->i_child;
			pinp->i_child = inp;
			SET_ISTATE(orphan, GET_ISTATE(inp->i_parent));
		}
		propagate(orphan);
	}
	info_fn = NULL;
}
