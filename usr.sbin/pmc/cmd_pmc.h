/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018, Matthew Macy
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
 *
 */
#ifndef _CMD_PMC_H_
#define _CMD_PMC_H_

#define	DEFAULT_DISPLAY_HEIGHT		256	/* file virtual height */
#define	DEFAULT_DISPLAY_WIDTH		1024	/* file virtual width */

extern int pmc_displayheight;
extern int pmc_displaywidth;
extern int pmc_kq;
extern struct pmcstat_args pmc_args;

typedef int (*cmd_disp_t)(int, char **);

#if defined(__cplusplus)
extern "C" {
#endif
	int	cmd_pmc_stat(int, char **);
	int	cmd_pmc_filter(int, char **);
	int	cmd_pmc_stat_system(int, char **);
	int	cmd_pmc_list_events(int, char **);
	int	cmd_pmc_summary(int, char **);
#if defined(__cplusplus)
};
#endif
int	pmc_util_get_pid(struct pmcstat_args *);
void	pmc_util_start_pmcs(struct pmcstat_args *);
void	pmc_util_cleanup(struct pmcstat_args *);
void	pmc_util_shutdown_logging(struct pmcstat_args *args);
void	pmc_util_kill_process(struct pmcstat_args *args);

#endif
