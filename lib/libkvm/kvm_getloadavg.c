/*	$OpenBSD: kvm_getloadavg.c,v 1.10 2021/09/10 00:02:43 deraadt Exp $ */
/*	$NetBSD: kvm_getloadavg.c,v 1.2 1996/03/18 22:33:31 thorpej Exp $	*/

/*-
 * Copyright (c) 1993
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

#include <sys/signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <db.h>
#include <fcntl.h>
#include <limits.h>
#include <nlist.h>
#include <stdlib.h>
#include <kvm.h>

#include "kvm_private.h"

static struct nlist nl[] = {
	{ "_averunnable" },
#define	X_AVERUNNABLE	0
	{ "_fscale" },
#define	X_FSCALE	1
	{ "" },
};

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

/*
 * kvm_getloadavg() -- Get system load averages, from live or dead kernels.
 *
 * Put `nelem' samples into `loadavg' array.
 * Return number of samples retrieved, or -1 on error.
 */
int
kvm_getloadavg(kvm_t *kd, double loadavg[], int nelem)
{
	struct loadavg loadinfo;
	struct nlist *p;
	int fscale, i;

	if (ISALIVE(kd))
		return (getloadavg(loadavg, nelem));

	if (kvm_nlist(kd, nl) != 0) {
		for (p = nl; p->n_type != 0; ++p)
			;
		_kvm_err(kd, kd->program,
		    "%s: no such symbol", p->n_name);
		return (-1);
	}

	if (KREAD(kd, nl[X_AVERUNNABLE].n_value, &loadinfo)) {
		_kvm_err(kd, kd->program, "can't read averunnable");
		return (-1);
	}

	/*
	 * Old kernels have fscale separately; if not found assume
	 * running new format.
	 */
	if (!KREAD(kd, nl[X_FSCALE].n_value, &fscale))
		loadinfo.fscale = fscale;

	nelem = MINIMUM(nelem, sizeof(loadinfo.ldavg) / sizeof(fixpt_t));
	for (i = 0; i < nelem; i++)
		loadavg[i] = (double) loadinfo.ldavg[i] / loadinfo.fscale;
	return (nelem);
}
