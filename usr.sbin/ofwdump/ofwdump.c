/*-
 * Copyright (c) 2002 by Thomas Moestl <tmm@FreeBSD.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/ofw/openfirm.h>
#include <dev/ofw/openfirmio.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <vis.h>

#include "ofw_util.h"

/* Constants controlling the layout of the output. */
#define	LVLINDENT	2
#define	NAMEINDENT	2
#define	DUMPINDENT	4
#define	CHARSPERLINE	60
#define	BYTESPERLINE	(CHARSPERLINE / 3)

static void	usage(void);
static void	ofw_indent(int);
static void	ofw_dump_properties(int, phandle_t, int, int, int);
static void	ofw_dump_property(int fd, phandle_t n, int level,
		    const char *prop, int raw, int str);
static void	ofw_dump(int, const char *, int, int, const char *, int, int);

static void
usage(void)
{

	fprintf(stderr,
	    "usage: ofwdump -a [-p | -P property] [-R | -S]\n"
	    "       ofwdump [-p | -P property] [-r] [-R | -S] [--] nodes\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	int opt, i, fd;
	int aflag, pflag, rflag, Rflag, Sflag;
	char *Parg;

	aflag = pflag = rflag = Rflag = Sflag = 0;
	Parg = NULL;
	while ((opt = getopt(argc, argv, "-aprP:RS")) != -1) {
		if (opt == '-')
			break;
		switch (opt) {
		case 'a':
			aflag = 1;
			rflag = 1;
			break;
		case 'p':
			if (Parg != NULL)
				usage();
			pflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 'P':
			if (pflag)
				usage();
			pflag = 1;
			Parg = optarg;
			break;
		case 'R':
			if (Sflag)
				usage();
			Rflag = 1;
			break;
		case 'S':
			if (Rflag)
				usage();
			Sflag = 1;
			break;
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	fd = ofw_open(O_RDONLY);
	if (aflag) {
		if (argc != 0)
			usage();
		ofw_dump(fd, NULL, rflag, pflag, Parg, Rflag, Sflag);
	} else {
		/*
		 * For the sake of scripts, usage() is not called here if
		 * argc == 0.
		 */
		for (i = 0; i < argc; i++)
			ofw_dump(fd, argv[i], rflag, pflag, Parg, Rflag, Sflag);
	}
	ofw_close(fd);
	return (EX_OK);
}

static void
ofw_indent(int level)
{
	int i;

	for (i = 0; i < level; i++)
		putchar(' ');
}

static void
ofw_dump_properties(int fd, phandle_t n, int level, int raw, int str)
{
	int nlen;
	char prop[32];

	for (nlen = ofw_firstprop(fd, n, prop, sizeof(prop)); nlen != 0;
	     nlen = ofw_nextprop(fd, n, prop, prop, sizeof(prop)))
		ofw_dump_property(fd, n, level, prop, raw, str);
}

static void
ofw_dump_property(int fd, phandle_t n, int level, const char *prop, int raw,
    int str)
{
	static void *pbuf = NULL;
	static char *visbuf = NULL;
	static char printbuf[CHARSPERLINE + 1];
	static int pblen = 0, vblen = 0;
	int len, i, j, max, vlen;

	len = ofw_getprop_alloc(fd, n, prop, &pbuf, &pblen, 1);
	if (len < 0)
		return;
	if (raw)
		write(STDOUT_FILENO, pbuf, len);
	else if (str)
		printf("%.*s\n", len, (char *)pbuf);
	else {
		ofw_indent(level * LVLINDENT + NAMEINDENT);
		printf("%s:\n", prop);
		/* Print in hex. */
		for (i = 0; i < len; i += BYTESPERLINE) {
			max = len - i;
			max = max > BYTESPERLINE ? BYTESPERLINE : max;
			ofw_indent(level * LVLINDENT + DUMPINDENT);
			for (j = 0; j < max; j++)
				printf("%02x ",
				    ((unsigned char *)pbuf)[i + j]);
			printf("\n");
		}
		/*
		 * strvis() and print if it looks like it is
		 * zero-terminated.
		 */
		if (((char *)pbuf)[len - 1] == '\0' &&
		    strlen(pbuf) == (unsigned)len - 1) {
			if (vblen < (len - 1) * 4 + 1) {
				if (visbuf != NULL)
					free(visbuf);
				vblen = (OFIOCMAXVALUE + len) * 4 + 1;
					if ((visbuf = malloc(vblen)) == NULL)
						err(EX_OSERR,
						    "malloc() failed");
			}
			vlen = strvis(visbuf, pbuf, VIS_TAB | VIS_NL);
			for (i = 0; i < vlen; i += CHARSPERLINE) {
				ofw_indent(level * LVLINDENT +
				    DUMPINDENT);
				strlcpy(printbuf, &visbuf[i],
				    sizeof(printbuf));
				printf("'%s'\n", printbuf);
			}
		}
	}
}

static void
ofw_dump_node(int fd, phandle_t n, int level, int rec, int prop,
    const char *pmatch, int raw, int str)
{
	static void *nbuf = NULL;
	static int nblen = 0;
	int plen;
	phandle_t c;

	if (!(raw || str)) {
		ofw_indent(level * LVLINDENT);
		printf("Node %#lx", (unsigned long)n);
		plen = ofw_getprop_alloc(fd, n, "name", &nbuf, &nblen, 1);
		if (plen > 0)
			printf(": %.*s\n", (int)plen, (char *)nbuf);
		else
			putchar('\n');
	}
	if (prop) {
		if (pmatch)
			ofw_dump_property(fd, n, level, pmatch, raw, str);
		else
			ofw_dump_properties(fd, n, level, raw, str);
	}
	if (rec) {
		for (c = ofw_child(fd, n); c != 0; c = ofw_peer(fd, c)) {
			ofw_dump_node(fd, c, level + 1, rec, prop, pmatch,
			    raw, str);
		}
	}
}

static void
ofw_dump(int fd, const char *start, int rec, int prop, const char *pmatch,
    int raw, int str)
{
	phandle_t n;

	n = start == NULL ? ofw_root(fd) : ofw_finddevice(fd, start);
	ofw_dump_node(fd, n, 0, rec, prop, pmatch, raw, str);
}
