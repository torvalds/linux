/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2007 Joseph Koshy
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

#ifndef	_PMCLOG_H_
#define	_PMCLOG_H_

#include <sys/cdefs.h>
#include <sys/pmclog.h>

enum pmclog_state {
	PMCLOG_OK,
	PMCLOG_EOF,
	PMCLOG_REQUIRE_DATA,
	PMCLOG_ERROR
};

struct pmclog_ev_callchain {
	uint32_t	pl_pid;
	uint32_t	pl_tid;
	uint32_t	pl_pmcid;
	uint32_t	pl_cpuflags;
	uint32_t	pl_cpuflags2;
	uint32_t	pl_npc;
	uintfptr_t	pl_pc[PMC_CALLCHAIN_DEPTH_MAX];
};

struct pmclog_ev_dropnotify {
};

struct pmclog_ev_closelog {
};

struct pmclog_ev_initialize {
	uint32_t	pl_version;
	uint32_t	pl_arch;
	uint64_t	pl_tsc_freq;
	struct timespec pl_ts;
	char		pl_cpuid[PATH_MAX];
};

struct pmclog_ev_map_in {
	pid_t		pl_pid;
	uintfptr_t	pl_start;
	char		pl_pathname[PATH_MAX];
};

struct pmclog_ev_map_out {
	pid_t		pl_pid;
	uintfptr_t	pl_start;
	uintfptr_t	pl_end;
};

struct pmclog_ev_pcsample {
	uintfptr_t	pl_pc;
	pid_t		pl_pid;
	pid_t		pl_tid;
	pmc_id_t	pl_pmcid;
	uint32_t	pl_flags;
	uint32_t	pl_usermode;
};

struct pmclog_ev_pmcallocate {
	const char *	pl_evname;
	uint64_t	pl_rate;
	uint32_t	pl_event;
	uint32_t	pl_flags;
	pmc_id_t	pl_pmcid;
};

struct pmclog_ev_pmcallocatedyn {
	char 		pl_evname[PMC_NAME_MAX];
	uint32_t	pl_event;
	uint32_t	pl_flags;
	pmc_id_t	pl_pmcid;
};

struct pmclog_ev_pmcattach {
	pmc_id_t	pl_pmcid;
	pid_t		pl_pid;
	char		pl_pathname[PATH_MAX];
};

struct pmclog_ev_pmcdetach {
	pmc_id_t	pl_pmcid;
	pid_t		pl_pid;
};

struct pmclog_ev_proccsw {
	pid_t		pl_pid;
	pid_t		pl_tid;
	pmc_id_t	pl_pmcid;
	pmc_value_t	pl_value;
};

struct pmclog_ev_proccreate {
	pid_t		pl_pid;
	uint32_t	pl_flags;
	char		pl_pcomm[MAXCOMLEN+1];
};

struct pmclog_ev_procexec {
	pid_t		pl_pid;
	pmc_id_t	pl_pmcid;
	uintfptr_t	pl_entryaddr;
	char		pl_pathname[PATH_MAX];
};

struct pmclog_ev_procexit {
	uint32_t	pl_pid;
	pmc_id_t	pl_pmcid;
	pmc_value_t	pl_value;
};

struct pmclog_ev_procfork {
	pid_t		pl_oldpid;
	pid_t		pl_newpid;
};

struct pmclog_ev_sysexit {
	pid_t		pl_pid;
};

struct pmclog_ev_threadcreate {
	pid_t		pl_tid;
	pid_t		pl_pid;
	uint32_t	pl_flags;
	char		pl_tdname[MAXCOMLEN+1];
};

struct pmclog_ev_threadexit {
	pid_t		pl_tid;
};

struct pmclog_ev_userdata {
	uint32_t	pl_userdata;
};

struct pmclog_ev {
	enum pmclog_state pl_state;	/* state after 'get_event()' */
	off_t		  pl_offset;	/* byte offset in stream */
	size_t		  pl_count;	/* count of records so far */
	struct timespec   pl_ts;	/* log entry timestamp */
	enum pmclog_type  pl_type;	/* type of log entry */
	void		 *pl_data;
	int		  pl_len;
	union { 			/* log entry data */
		struct pmclog_ev_callchain	pl_cc;
		struct pmclog_ev_closelog	pl_cl;
		struct pmclog_ev_dropnotify	pl_dn;
		struct pmclog_ev_initialize	pl_i;
		struct pmclog_ev_map_in		pl_mi;
		struct pmclog_ev_map_out	pl_mo;
		struct pmclog_ev_pmcallocate	pl_a;
		struct pmclog_ev_pmcallocatedyn	pl_ad;
		struct pmclog_ev_pmcattach	pl_t;
		struct pmclog_ev_pmcdetach	pl_d;
		struct pmclog_ev_proccsw	pl_c;
		struct pmclog_ev_proccreate	pl_pc;
		struct pmclog_ev_procexec	pl_x;
		struct pmclog_ev_procexit	pl_e;
		struct pmclog_ev_procfork	pl_f;
		struct pmclog_ev_sysexit	pl_se;
		struct pmclog_ev_threadcreate	pl_tc;
		struct pmclog_ev_threadexit	pl_te;
		struct pmclog_ev_userdata	pl_u;
	} pl_u;
};

enum pmclog_parser_state {
	PL_STATE_NEW_RECORD,		/* in-between records */
	PL_STATE_EXPECTING_HEADER,	/* header being read */
	PL_STATE_PARTIAL_RECORD,	/* header present but not the record */
	PL_STATE_ERROR			/* parsing error encountered */
};

struct pmclog_parse_state {
	enum pmclog_parser_state ps_state;
	enum pmc_cputype	ps_arch;	/* log file architecture */
	uint32_t		ps_version;	/* hwpmc version */
	int			ps_initialized;	/* whether initialized */
	int			ps_count;	/* count of records processed */
	off_t			ps_offset;	/* stream byte offset */
	union pmclog_entry	ps_saved;	/* saved partial log entry */
	int			ps_svcount;	/* #bytes saved */
	int			ps_fd;		/* active fd or -1 */
	char			*ps_buffer;	/* scratch buffer if fd != -1 */
	char			*ps_data;	/* current parse pointer */
	char			*ps_cpuid;	/* log cpuid */
	size_t			ps_len;		/* length of buffered data */
};

#define	PMCLOG_FD_NONE				(-1)

__BEGIN_DECLS
void	*pmclog_open(int _fd);
int	pmclog_feed(void *_cookie, char *_data, int _len);
int	pmclog_read(void *_cookie, struct pmclog_ev *_ev);
void	pmclog_close(void *_cookie);
__END_DECLS

#endif

