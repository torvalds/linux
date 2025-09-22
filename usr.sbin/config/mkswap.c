/*	$OpenBSD: mkswap.c,v 1.21 2024/10/30 07:28:17 jsg Exp $	*/
/*	$NetBSD: mkswap.c,v 1.5 1996/08/31 20:58:27 mycroft Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratories.
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
 *	from: @(#)mkswap.c	8.1 (Berkeley) 6/6/93
 */

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "sem.h"

dev_t nodev = (dev_t)-1;

static int mkoneswap(struct config *);

/*
 * Make the various swap*.c files.  Nothing to do for generic swap.
 */
int
mkswap(void)
{
	struct config *cf;

	for (cf = allcf; cf != NULL; cf = cf->cf_next)
		if (cf->cf_root != NULL && mkoneswap(cf))
			return (1);
	return (0);
}

static char *
mkdevstr(dev_t d)
{
	static char buf[32];

	if (d == nodev)
		(void)snprintf(buf, sizeof buf, "NODEV");
	else
		(void)snprintf(buf, sizeof buf, "makedev(%u, %u)",
		    major(d), minor(d));
	return buf;
}

static int
mkoneswap(struct config *cf)
{
	char fname[200], *mountroot;
	struct nvlist *nv;
	FILE *fp;

	(void)snprintf(fname, sizeof fname, "swap%s.c", cf->cf_name);
	if ((fp = fopen(fname, "w")) == NULL) {
		warn("cannot write %s", fname);
		return (1);
	}
	if (fputs("\
#include <sys/param.h>\n\
#include <sys/systm.h>\n\n", fp) == EOF)
		goto wrerror;
	nv = cf->cf_root;
	if (fprintf(fp, "dev_t\trootdev = %s;\t/* %s */\n",
	    mkdevstr(nv->nv_int), nv->nv_str) < 0)
		goto wrerror;
	nv = cf->cf_dump;
	if (fprintf(fp, "dev_t\tdumpdev = %s;\t/* %s */\n",
	    mkdevstr(nv->nv_int), nv->nv_str) < 0)
		goto wrerror;
	if (fputs("\ndev_t\tswdevt[] = {\n", fp) == EOF)
		goto wrerror;
	for (nv = cf->cf_swap; nv != NULL; nv = nv->nv_next)
		if (fprintf(fp, "\t%s,\t/* %s */\n",
		    mkdevstr(nv->nv_int), nv->nv_str) < 0)
			goto wrerror;
	if (fputs("\tNODEV\n};\n\n", fp) == EOF)
		goto wrerror;
	mountroot =
	    cf->cf_root->nv_str == s_nfs ? "nfs_mountroot" : "dk_mountroot";
	if (fprintf(fp, "int (*mountroot)(void) = %s;\n", mountroot) < 0)
		goto wrerror;

	if (fclose(fp)) {
		fp = NULL;
		goto wrerror;
	}
	return (0);
wrerror:
	warn("error writing %s", fname);
	if (fp != NULL)
		(void)fclose(fp);
	/* (void)unlink(fname); */
	return (1);
}
