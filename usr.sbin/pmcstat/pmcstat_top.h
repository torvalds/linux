/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Fabien Thomas
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
 * $FreeBSD$
 */

#ifndef	_PMCSTAT_TOP_H_
#define	_PMCSTAT_TOP_H_

/* Return the ncurses attributes for the given value. */
#define PMCSTAT_ATTRPERCENT(b) 					\
    ((b) > 10.0 ? (args.pa_topcolor ? COLOR_PAIR(1) : A_BOLD) :	\
    ((b) >  5.0 ? (args.pa_topcolor ? COLOR_PAIR(2) : 0) : 	\
    ((b) >  2.5 ? (args.pa_topcolor ? COLOR_PAIR(3) : 0) : 0)))

/* Print to the default ncurse windows if on a terminal or to the file. */
#define PMCSTAT_PRINTW(...) do {			\
	if (args.pa_toptty)				\
		printw(__VA_ARGS__);			\
	else						\
		fprintf(args.pa_printfile, __VA_ARGS__);\
} while (0)

/* If ncurses mode active set attributes. */
#define PMCSTAT_ATTRON(b) do {				\
	if (args.pa_toptty)				\
		attron(b);				\
} while (0)

/* If ncurses mode active unset attributes. */
#define PMCSTAT_ATTROFF(b) do {				\
	if (args.pa_toptty)				\
		attroff(b);				\
} while (0)

/* Erase screen and set cursor to top left. */
#define PMCSTAT_PRINTBEGIN() do {			\
	if (args.pa_toptty)				\
		clear();				\
} while (0)

/* Flush buffer to backend. */
#define PMCSTAT_PRINTEND() do {				\
	if (!args.pa_toptty) {				\
		PMCSTAT_PRINTW("---\n");		\
		fflush(args.pa_printfile);		\
	} else						\
		refresh();				\
} while (0)

/* Function prototypes */

#endif	/* _PMCSTAT_TOP_H_ */
