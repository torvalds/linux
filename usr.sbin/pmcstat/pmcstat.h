/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2007, Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
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

#ifndef	_PMCSTAT_H_
#define	_PMCSTAT_H_

#include <libpmcstat.h>

#define	DEFAULT_WAIT_INTERVAL		5.0
#define	DEFAULT_DISPLAY_HEIGHT		256		/* file virtual height */
#define	DEFAULT_DISPLAY_WIDTH		1024		/* file virtual width */
#define	DEFAULT_BUFFER_SIZE		4096
#define	DEFAULT_CALLGRAPH_DEPTH		16

#define	PRINT_HEADER_PREFIX		"# "

#define	PMCSTAT_DEFAULT_NW_HOST		"localhost"
#define	PMCSTAT_DEFAULT_NW_PORT		"9000"

#define	PMCSTAT_LDD_COMMAND		"/usr/bin/ldd"

#define	PMCSTAT_PRINT_ENTRY(T,...) do {					\
		(void) fprintf(args.pa_printfile, "%-9s", T);		\
		(void) fprintf(args.pa_printfile, " "  __VA_ARGS__);	\
		(void) fprintf(args.pa_printfile, "\n");		\
	} while (0)

#define PMCSTAT_PL_NONE		0
#define PMCSTAT_PL_CALLGRAPH	1
#define PMCSTAT_PL_GPROF	2
#define PMCSTAT_PL_ANNOTATE	3
#define PMCSTAT_PL_CALLTREE	4
#define PMCSTAT_PL_ANNOTATE_CG	5

#define PMCSTAT_TOP_DELTA 	0
#define PMCSTAT_TOP_ACCUM	1

extern int pmcstat_displayheight;	/* current terminal height */
extern int pmcstat_displaywidth;	/* current terminal width */
extern struct pmcstat_args args;	/* command line args */

/* Function prototypes */
void	pmcstat_cleanup(void);
void	pmcstat_find_targets(const char *_arg);
void	pmcstat_kill_process(void);
void	pmcstat_print_counters(void);
void	pmcstat_print_headers(void);
void	pmcstat_print_pmcs(void);
void	pmcstat_show_usage(void);
void	pmcstat_start_pmcs(void);
int	pmcstat_process_log(void);
int	pmcstat_keypress_log(void);
void	pmcstat_display_log(void);
void	pmcstat_pluginconfigure_log(char *_opt);
void	pmcstat_topexit(void);

void pmcstat_log_shutdown_logging(void);
void pmcstat_log_initialize_logging(void);
#endif	/* _PMCSTAT_H_ */
