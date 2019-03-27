/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
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

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1991, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static const char sccsid[] = "@(#)dmesg.c	8.1 (Berkeley) 6/5/93";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/msgbuf.h>
#include <sys/sysctl.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <locale.h>
#include <nlist.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>
#include <sys/syslog.h>

static struct nlist nl[] = {
#define	X_MSGBUF	0
	{ "_msgbufp", 0, 0, 0, 0 },
	{ NULL, 0, 0, 0, 0 },
};

void usage(void) __dead2;

#define	KREAD(addr, var) \
	kvm_read(kd, addr, &var, sizeof(var)) != sizeof(var)

int
main(int argc, char *argv[])
{
	struct msgbuf *bufp, cur;
	char *bp, *ep, *memf, *nextp, *nlistf, *p, *q, *visbp;
	kvm_t *kd;
	size_t buflen, bufpos;
	long pri;
	int ch, clear;
	bool all;

	all = false;
	clear = false;
	(void) setlocale(LC_CTYPE, "");
	memf = nlistf = NULL;
	while ((ch = getopt(argc, argv, "acM:N:")) != -1)
		switch(ch) {
		case 'a':
			all = true;
			break;
		case 'c':
			clear = true;
			break;
		case 'M':
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	if (argc != 0)
		usage();

	if (memf == NULL) {
		/*
		 * Running kernel.  Use sysctl.  This gives an unwrapped buffer
		 * as a side effect.  Remove nulterm (if present) so the value
		 * returned by sysctl is formatted as the rest of the code
		 * expects (the same as the value read from a core file below).
		 */
		if (sysctlbyname("kern.msgbuf", NULL, &buflen, NULL, 0) == -1)
			err(1, "sysctl kern.msgbuf");
		/* Allocate extra room for growth between the sysctl calls. */
		buflen += buflen/8;
		/* Allocate more than sysctl sees, for room to append \n\0. */
		if ((bp = malloc(buflen + 2)) == NULL)
			errx(1, "malloc failed");
		if (sysctlbyname("kern.msgbuf", bp, &buflen, NULL, 0) == -1)
			err(1, "sysctl kern.msgbuf");
		if (buflen > 0 && bp[buflen - 1] == '\0')
			buflen--;
		if (clear)
			if (sysctlbyname("kern.msgbuf_clear", NULL, NULL, &clear, sizeof(int)))
				err(1, "sysctl kern.msgbuf_clear");
	} else {
		/* Read in kernel message buffer and do sanity checks. */
		kd = kvm_open(nlistf, memf, NULL, O_RDONLY, "dmesg");
		if (kd == NULL)
			exit (1);
		if (kvm_nlist(kd, nl) == -1)
			errx(1, "kvm_nlist: %s", kvm_geterr(kd));
		if (nl[X_MSGBUF].n_type == 0)
			errx(1, "%s: msgbufp not found",
			    nlistf ? nlistf : "namelist");
		if (KREAD(nl[X_MSGBUF].n_value, bufp) || KREAD((long)bufp, cur))
			errx(1, "kvm_read: %s", kvm_geterr(kd));
		if (cur.msg_magic != MSG_MAGIC)
			errx(1, "kernel message buffer has different magic "
			    "number");
		if ((bp = malloc(cur.msg_size + 2)) == NULL)
			errx(1, "malloc failed");

		/* Unwrap the circular buffer to start from the oldest data. */
		bufpos = MSGBUF_SEQ_TO_POS(&cur, cur.msg_wseq);
		if (kvm_read(kd, (long)&cur.msg_ptr[bufpos], bp,
		    cur.msg_size - bufpos) != (ssize_t)(cur.msg_size - bufpos))
			errx(1, "kvm_read: %s", kvm_geterr(kd));
		if (bufpos != 0 && kvm_read(kd, (long)cur.msg_ptr,
		    &bp[cur.msg_size - bufpos], bufpos) != (ssize_t)bufpos)
			errx(1, "kvm_read: %s", kvm_geterr(kd));
		kvm_close(kd);
		buflen = cur.msg_size;
	}

	/*
	 * Ensure that the buffer ends with a newline and a \0 to avoid
	 * complications below.  We left space above.
	 */
	if (buflen == 0 || bp[buflen - 1] != '\n')
		bp[buflen++] = '\n';
	bp[buflen] = '\0';

	if ((visbp = malloc(4 * buflen + 1)) == NULL)
		errx(1, "malloc failed");

	/*
	 * The message buffer is circular, but has been unwrapped so that
	 * the oldest data comes first.  The data will be preceded by \0's
	 * if the message buffer was not full.
	 */
	p = bp;
	ep = &bp[buflen];
	if (*p == '\0') {
		/* Strip leading \0's */
		while (*p == '\0')
			p++;
	} else if (!all) {
		/* Skip the first line, since it is probably incomplete. */
		p = memchr(p, '\n', ep - p);
		p++;
	}
	for (; p < ep; p = nextp) {
		nextp = memchr(p, '\n', ep - p);
		nextp++;

		/* Skip ^<[0-9]+> syslog sequences. */
		if (*p == '<' && isdigit(*(p+1))) {
			errno = 0;
			pri = strtol(p + 1, &q, 10);
			if (*q == '>' && pri >= 0 && pri < INT_MAX &&
			    errno == 0) {
				if (LOG_FAC(pri) != LOG_KERN && !all)
					continue;
				p = q + 1;
			}
		}

		(void)strvisx(visbp, p, nextp - p, 0);
		(void)printf("%s", visbp);
	}
	exit(0);
}

void
usage(void)
{
	fprintf(stderr, "usage: dmesg [-ac] [-M core [-N system]]\n");
	exit(1);
}
