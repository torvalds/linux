/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1987, 1991, 1993
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1987, 1991, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)wc.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/capsicum.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <capsicum_helpers.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include <libxo/xo.h>

#include <libcasper.h>
#include <casper/cap_fileargs.h>

static fileargs_t *fa;
static uintmax_t tlinect, twordct, tcharct, tlongline;
static int doline, doword, dochar, domulti, dolongline;
static volatile sig_atomic_t siginfo;
static xo_handle_t *stderr_handle;

static void	show_cnt(const char *file, uintmax_t linect, uintmax_t wordct,
		    uintmax_t charct, uintmax_t llct);
static int	cnt(const char *);
static void	usage(void);

static void
siginfo_handler(int sig __unused)
{

	siginfo = 1;
}

static void
reset_siginfo(void)
{

	signal(SIGINFO, SIG_DFL);
	siginfo = 0;
}

int
main(int argc, char *argv[])
{
	int ch, errors, total;
	cap_rights_t rights;

	(void) setlocale(LC_CTYPE, "");

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		return (argc);

	while ((ch = getopt(argc, argv, "clmwL")) != -1)
		switch((char)ch) {
		case 'l':
			doline = 1;
			break;
		case 'w':
			doword = 1;
			break;
		case 'c':
			dochar = 1;
			domulti = 0;
			break;
		case 'L':
			dolongline = 1;
			break;
		case 'm':
			domulti = 1;
			dochar = 0;
			break;
		case '?':
		default:
			usage();
		}
	argv += optind;
	argc -= optind;

	(void)signal(SIGINFO, siginfo_handler);

	fa = fileargs_init(argc, argv, O_RDONLY, 0,
	    cap_rights_init(&rights, CAP_READ, CAP_FSTAT));
	if (fa == NULL) {
		xo_warn("Unable to init casper");
		exit(1);
	}

	caph_cache_catpages();
	if (caph_limit_stdio() < 0) {
		xo_warn("Unable to limit stdio");
		fileargs_free(fa);
		exit(1);
	}

	if (caph_enter_casper() < 0) {
		xo_warn("Unable to enter capability mode");
		fileargs_free(fa);
		exit(1);
	}

	/* Wc's flags are on by default. */
	if (doline + doword + dochar + domulti + dolongline == 0)
		doline = doword = dochar = 1;

	stderr_handle = xo_create_to_file(stderr, XO_STYLE_TEXT, 0);
	xo_open_container("wc");
	xo_open_list("file");

	errors = 0;
	total = 0;
	if (!*argv) {
	 	xo_open_instance("file");
		if (cnt((char *)NULL) != 0)
			++errors;
	 	xo_close_instance("file");
	} else {
		do {
	 		xo_open_instance("file");
			if (cnt(*argv) != 0)
				++errors;
	 		xo_close_instance("file");
			++total;
		} while(*++argv);
	}

	xo_close_list("file");

	if (total > 1) {
		xo_open_container("total");
		show_cnt("total", tlinect, twordct, tcharct, tlongline);
		xo_close_container("total");
	}

	fileargs_free(fa);
	xo_close_container("wc");
	xo_finish();
	exit(errors == 0 ? 0 : 1);
}

static void
show_cnt(const char *file, uintmax_t linect, uintmax_t wordct,
    uintmax_t charct, uintmax_t llct)
{
	xo_handle_t *xop;

	if (!siginfo)
		xop = NULL;
	else {
		xop = stderr_handle;
		siginfo = 0;
	}

	if (doline)
		xo_emit_h(xop, " {:lines/%7ju/%ju}", linect);
	if (doword)
		xo_emit_h(xop, " {:words/%7ju/%ju}", wordct);
	if (dochar || domulti)
		xo_emit_h(xop, " {:characters/%7ju/%ju}", charct);
	if (dolongline)
		xo_emit_h(xop, " {:long-lines/%7ju/%ju}", llct);
	if (file != NULL)
		xo_emit_h(xop, " {:filename/%s}\n", file);
	else
		xo_emit_h(xop, "\n");
}

static int
cnt(const char *file)
{
	struct stat sb;
	uintmax_t linect, wordct, charct, llct, tmpll;
	int fd, len, warned;
	size_t clen;
	short gotsp;
	u_char *p;
	u_char buf[MAXBSIZE];
	wchar_t wch;
	mbstate_t mbs;

	linect = wordct = charct = llct = tmpll = 0;
	if (file == NULL)
		fd = STDIN_FILENO;
	else if ((fd = fileargs_open(fa, file)) < 0) {
		xo_warn("%s: open", file);
		return (1);
	}
	if (doword || (domulti && MB_CUR_MAX != 1))
		goto word;
	/*
	 * If all we need is the number of characters and it's a regular file,
	 * just stat it.
	 */
	if (doline == 0 && dolongline == 0) {
		if (fstat(fd, &sb)) {
			xo_warn("%s: fstat", file);
			(void)close(fd);
			return (1);
		}
		if (S_ISREG(sb.st_mode)) {
			reset_siginfo();
			charct = sb.st_size;
			show_cnt(file, linect, wordct, charct, llct);
			tcharct += charct;
			(void)close(fd);
			return (0);
		}
	}
	/*
	 * For files we can't stat, or if we need line counting, slurp the
	 * file.  Line counting is split out because it's a lot faster to get
	 * lines than to get words, since the word count requires locale
	 * handling.
	 */
	while ((len = read(fd, buf, MAXBSIZE))) {
		if (len == -1) {
			xo_warn("%s: read", file);
			(void)close(fd);
			return (1);
		}
		if (siginfo)
			show_cnt(file, linect, wordct, charct, llct);
		charct += len;
		if (doline || dolongline) {
			for (p = buf; len--; ++p)
				if (*p == '\n') {
					if (tmpll > llct)
						llct = tmpll;
					tmpll = 0;
					++linect;
				} else
					tmpll++;
		}
	}
	reset_siginfo();
	if (doline)
		tlinect += linect;
	if (dochar)
		tcharct += charct;
	if (dolongline && llct > tlongline)
		tlongline = llct;
	show_cnt(file, linect, wordct, charct, llct);
	(void)close(fd);
	return (0);

	/* Do it the hard way... */
word:	gotsp = 1;
	warned = 0;
	memset(&mbs, 0, sizeof(mbs));
	while ((len = read(fd, buf, MAXBSIZE)) != 0) {
		if (len == -1) {
			xo_warn("%s: read", file != NULL ? file : "stdin");
			(void)close(fd);
			return (1);
		}
		p = buf;
		while (len > 0) {
			if (siginfo)
				show_cnt(file, linect, wordct, charct, llct);
			if (!domulti || MB_CUR_MAX == 1) {
				clen = 1;
				wch = (unsigned char)*p;
			} else if ((clen = mbrtowc(&wch, p, len, &mbs)) ==
			    (size_t)-1) {
				if (!warned) {
					errno = EILSEQ;
					xo_warn("%s",
					    file != NULL ? file : "stdin");
					warned = 1;
				}
				memset(&mbs, 0, sizeof(mbs));
				clen = 1;
				wch = (unsigned char)*p;
			} else if (clen == (size_t)-2)
				break;
			else if (clen == 0)
				clen = 1;
			charct++;
			if (wch != L'\n')
				tmpll++;
			len -= clen;
			p += clen;
			if (wch == L'\n') {
				if (tmpll > llct)
					llct = tmpll;
				tmpll = 0;
				++linect;
			}
			if (iswspace(wch))
				gotsp = 1;
			else if (gotsp) {
				gotsp = 0;
				++wordct;
			}
		}
	}
	reset_siginfo();
	if (domulti && MB_CUR_MAX > 1)
		if (mbrtowc(NULL, NULL, 0, &mbs) == (size_t)-1 && !warned)
			xo_warn("%s", file != NULL ? file : "stdin");
	if (doline)
		tlinect += linect;
	if (doword)
		twordct += wordct;
	if (dochar || domulti)
		tcharct += charct;
	if (dolongline && llct > tlongline)
		tlongline = llct;
	show_cnt(file, linect, wordct, charct, llct);
	(void)close(fd);
	return (0);
}

static void
usage(void)
{
	xo_error("usage: wc [-Lclmw] [file ...]\n");
	exit(1);
}
