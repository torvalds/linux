/*-
 * Copyright (c) 2015 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2015 Xin LI <delphij@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/capsicum.h>
#include <sys/types.h>
#include <sys/sbuf.h>

#include <capsicum_helpers.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <xlocale.h>

typedef enum {
	/* state	condition to transit to next state */
	INIT,		/* '$' */
	DELIM_SEEN,	/* letter */
	KEYWORD,	/* punctuation mark */
	PUNC_SEEN,	/* ':' -> _SVN; space -> TEXT */
	PUNC_SEEN_SVN,	/* space */
	TEXT
} analyzer_states;

static int
scan(FILE *fp, const char *name, bool quiet)
{
	int c;
	bool hasid = false;
	bool subversion = false;
	analyzer_states state = INIT;
	struct sbuf *id = sbuf_new_auto();
	locale_t l;

	l = newlocale(LC_ALL_MASK, "C", NULL);

	if (name != NULL)
		printf("%s:\n", name);

	while ((c = fgetc(fp)) != EOF) {
		switch (state) {
		case INIT:
			if (c == '$') {
				/* Transit to DELIM_SEEN if we see $ */
				state = DELIM_SEEN;
			} else {
				/* Otherwise, stay in INIT state */
				continue;
			}
			break;
		case DELIM_SEEN:
			if (isalpha_l(c, l)) {
				/* Transit to KEYWORD if we see letter */
				sbuf_clear(id);
				sbuf_putc(id, '$');
				sbuf_putc(id, c);
				state = KEYWORD;

				continue;
			} else if (c == '$') {
				/* Or, stay in DELIM_SEEN if more $ */
				continue;
			} else {
				/* Otherwise, transit back to INIT */
				state = INIT;
			}
			break;
		case KEYWORD:
			sbuf_putc(id, c);

			if (isalpha_l(c, l)) {
				/*
				 * Stay in KEYWORD if additional letter is seen
				 */
				continue;
			} else if (c == ':') {
				/*
				 * See ':' for the first time, transit to
				 * PUNC_SEEN.
				 */
				state = PUNC_SEEN;
				subversion = false;
			} else if (c == '$') {
				/*
				 * Incomplete ident.  Go back to DELIM_SEEN
				 * state because we see a '$' which could be
				 * the beginning of a keyword.
				 */
				state = DELIM_SEEN;
			} else {
				/*
				 * Go back to INIT state otherwise.
				 */
				state = INIT;
			}
			break;
		case PUNC_SEEN:
		case PUNC_SEEN_SVN:
			sbuf_putc(id, c);

			switch (c) {
			case ':':
				/*
				 * If we see '::' (seen : in PUNC_SEEN),
				 * activate subversion treatment and transit
				 * to PUNC_SEEN_SVN state.
				 *
				 * If more than two :'s were seen, the ident
				 * is invalid and we would therefore go back
				 * to INIT state.
				 */
				if (state == PUNC_SEEN) {
					state = PUNC_SEEN_SVN;
					subversion = true;
				} else {
					state = INIT;
				}
				break;
			case ' ':
				/*
				 * A space after ':' or '::' indicates we are at the
				 * last component of potential ident.
				 */
				state = TEXT;
				break;
			default:
				/* All other characters are invalid */
				state = INIT;
				break;
			}
			break;
		case TEXT:
			sbuf_putc(id, c);

			if (iscntrl_l(c, l)) {
				/* Control characters are not allowed in this state */
				state = INIT;
			} else if (c == '$') {
				sbuf_finish(id);
				/*
				 * valid ident should end with a space.
				 *
				 * subversion extension uses '#' to indicate that
				 * the keyword expansion have exceeded the fixed
				 * width, so it is also permitted if we are in
				 * subversion mode.  No length check is enforced
				 * because GNU RCS ident(1) does not do it either.
				 */
				c = sbuf_data(id)[sbuf_len(id) - 2];
				if (c == ' ' || (subversion && c == '#')) {
					printf("     %s\n", sbuf_data(id));
					hasid = true;
				}
				state = INIT;
			}
			/* Other characters: stay in the state */
			break;
		}
	}
	sbuf_delete(id);
	freelocale(l);

	if (!hasid) {
		if (!quiet)
			fprintf(stderr, "%s warning: no id keywords in %s\n",
			    getprogname(), name ? name : "standard input");

		return (EXIT_FAILURE);
	}

	return (EXIT_SUCCESS);
}

int
main(int argc, char **argv)
{
	bool quiet = false;
	int ch, i, *fds, fd;
	int ret = EXIT_SUCCESS;
	size_t nfds;
	FILE *fp;

	while ((ch = getopt(argc, argv, "qV")) != -1) {
		switch (ch) {
		case 'q':
			quiet = true;
			break;
		case 'V':
			/* Do nothing, compat with GNU rcs's ident */
			return (EXIT_SUCCESS);
		default:
			errx(EXIT_FAILURE, "usage: %s [-q] [-V] [file...]",
			    getprogname());
		}
	}

	argc -= optind;
	argv += optind;

	if (caph_limit_stdio() < 0)
		err(EXIT_FAILURE, "unable to limit stdio");

	if (argc == 0) {
		nfds = 1;
		fds = malloc(sizeof(*fds));
		if (fds == NULL)
			err(EXIT_FAILURE, "unable to allocate fds array");
		fds[0] = STDIN_FILENO;
	} else {
		nfds = argc;
		fds = malloc(sizeof(*fds) * nfds);
		if (fds == NULL)
			err(EXIT_FAILURE, "unable to allocate fds array");

		for (i = 0; i < argc; i++) {
			fds[i] = fd = open(argv[i], O_RDONLY);
			if (fd < 0) {
				warn("%s", argv[i]);
				ret = EXIT_FAILURE;
				continue;
			}
			if (caph_limit_stream(fd, CAPH_READ) < 0)
				err(EXIT_FAILURE,
				    "unable to limit fcntls/rights for %s",
				    argv[i]);
		}
	}

	/* Enter Capsicum sandbox. */
	if (caph_enter() < 0)
		err(EXIT_FAILURE, "unable to enter capability mode");

	for (i = 0; i < (int)nfds; i++) {
		if (fds[i] < 0)
			continue;

		fp = fdopen(fds[i], "r");
		if (fp == NULL) {
			warn("%s", argv[i]);
			ret = EXIT_FAILURE;
			continue;
		}
		if (scan(fp, argc == 0 ? NULL : argv[i], quiet) != EXIT_SUCCESS)
			ret = EXIT_FAILURE;
		fclose(fp);
	}

	return (ret);
}
