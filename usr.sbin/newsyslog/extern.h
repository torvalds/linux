/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * Copyright (c) 2003  - Garance Alistair Drosehn <gad@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the FreeBSD Project.
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * $FreeBSD$
 * ------+---------+---------+---------+---------+---------+---------+---------*
 */

#include <sys/cdefs.h>
#include <time.h>

#define	PTM_PARSE_ISO8601	0x0001	/* Parse ISO-standard format */
#define	PTM_PARSE_DWM		0x0002	/* Parse Day-Week-Month format */
#define	PTM_PARSE_MATCHDOM	0x0004	/* If the user specifies a day-of-month,
					 * then the result should be a month
					 * which actually has that day.  Eg:
					 * the user requests "day 31" when
					 * the present month is February. */

struct ptime_data;

/* Some global variables from newsyslog.c which might be of interest */
extern int	 dbg_at_times;		/* cmdline debugging option */
extern int	 noaction;		/* command-line option */
extern int	 verbose;		/* command-line option */
extern struct ptime_data *dbg_timenow;

__BEGIN_DECLS
struct ptime_data *ptime_init(const struct ptime_data *_optsrc);
int		 ptime_adjust4dst(struct ptime_data *_ptime, const struct
		    ptime_data *_dstsrc);
int		 ptime_free(struct ptime_data *_ptime);
int		 ptime_relparse(struct ptime_data *_ptime, int _parseopts,
		    time_t _basetime, const char *_str);
const char	*ptimeget_ctime(const struct ptime_data *_ptime);
char		*ptimeget_ctime_rfc5424(const struct ptime_data *_ptime,
		    char *timebuf, size_t bufsize);
double		 ptimeget_diff(const struct ptime_data *_minuend,
		    const struct ptime_data *_subtrahend);
time_t		 ptimeget_secs(const struct ptime_data *_ptime);
int		 ptimeset_nxtime(struct ptime_data *_ptime);
int		 ptimeset_time(struct ptime_data *_ptime, time_t _secs);
__END_DECLS
