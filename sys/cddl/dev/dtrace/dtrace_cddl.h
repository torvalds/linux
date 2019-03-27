/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 *
 */

#ifndef _DTRACE_CDDL_H_
#define	_DTRACE_CDDL_H_

#include <sys/proc.h>

#define LOCK_LEVEL	10

/*
 * Kernel DTrace extension to 'struct proc' for FreeBSD.
 */
typedef struct kdtrace_proc {
	int		p_dtrace_probes;	/* Are there probes for this proc? */
	u_int64_t	p_dtrace_count;		/* Number of DTrace tracepoints */
	void		*p_dtrace_helpers;	/* DTrace helpers, if any */
	int		p_dtrace_model;
	uint64_t	p_fasttrap_tp_gen;	/* Tracepoint hash table gen */
} kdtrace_proc_t;

/*
 * Kernel DTrace extension to 'struct thread' for FreeBSD.
 */
typedef struct kdtrace_thread {
	u_int8_t	td_dtrace_stop;	/* Indicates a DTrace-desired stop */
	u_int8_t	td_dtrace_sig;	/* Signal sent via DTrace's raise() */
	u_int8_t	td_dtrace_inprobe; /* Are we in a probe? */
	u_int		td_predcache;	/* DTrace predicate cache */
	u_int64_t	td_dtrace_vtime; /* DTrace virtual time */
	u_int64_t	td_dtrace_start; /* DTrace slice start time */

	union __tdu {
		struct __tds {
			u_int8_t	_td_dtrace_on;
					/* Hit a fasttrap tracepoint. */
			u_int8_t	_td_dtrace_step;
					/* About to return to kernel. */
			u_int8_t	_td_dtrace_ret;
					/* Handling a return probe. */
			u_int8_t	_td_dtrace_ast;
					/* Saved ast flag. */
#ifdef __amd64__
			u_int8_t	_td_dtrace_reg;
#endif
		} _tds;
		u_long	_td_dtrace_ft;	/* Bitwise or of these flags. */
	} _tdu;
#define	td_dtrace_ft	_tdu._td_dtrace_ft
#define	td_dtrace_on	_tdu._tds._td_dtrace_on
#define	td_dtrace_step	_tdu._tds._td_dtrace_step
#define	td_dtrace_ret	_tdu._tds._td_dtrace_ret
#define	td_dtrace_ast	_tdu._tds._td_dtrace_ast
#define	td_dtrace_reg	_tdu._tds._td_dtrace_reg

	uintptr_t	td_dtrace_pc;	/* DTrace saved pc from fasttrap. */
	uintptr_t	td_dtrace_npc;	/* DTrace next pc from fasttrap. */
	uintptr_t	td_dtrace_scrpc;
					/* DTrace per-thread scratch location. */
	uintptr_t	td_dtrace_astpc;
					/* DTrace return sequence location. */
#ifdef __amd64__
	uintptr_t	td_dtrace_regv;
#endif
	u_int64_t	td_hrtime;	/* Last time on cpu. */
	void		*td_dtrace_sscr; /* Saved scratch space location. */
	void		*td_systrace_args; /* syscall probe arguments. */
	uint64_t	td_fasttrap_tp_gen; /* Tracepoint hash table gen. */
} kdtrace_thread_t;

/*
 * Definitions to reference fields in the FreeBSD DTrace structures defined
 * above using the names of fields in similar structures in Solaris. Note 
 * that the separation on FreeBSD is a licensing constraint designed to
 * keep the GENERIC kernel BSD licensed.
 */
#define	t_dtrace_vtime	td_dtrace->td_dtrace_vtime
#define	t_dtrace_start	td_dtrace->td_dtrace_start
#define	t_dtrace_stop	td_dtrace->td_dtrace_stop
#define	t_dtrace_sig	td_dtrace->td_dtrace_sig
#define	t_dtrace_inprobe	td_dtrace->td_dtrace_inprobe
#define	t_predcache	td_dtrace->td_predcache
#define	t_dtrace_ft	td_dtrace->td_dtrace_ft
#define	t_dtrace_on	td_dtrace->td_dtrace_on
#define	t_dtrace_step	td_dtrace->td_dtrace_step
#define	t_dtrace_ret	td_dtrace->td_dtrace_ret
#define	t_dtrace_ast	td_dtrace->td_dtrace_ast
#define	t_dtrace_reg	td_dtrace->td_dtrace_reg
#define	t_dtrace_pc	td_dtrace->td_dtrace_pc
#define	t_dtrace_npc	td_dtrace->td_dtrace_npc
#define	t_dtrace_scrpc	td_dtrace->td_dtrace_scrpc
#define	t_dtrace_astpc	td_dtrace->td_dtrace_astpc
#define	t_dtrace_regv	td_dtrace->td_dtrace_regv
#define	t_dtrace_sscr	td_dtrace->td_dtrace_sscr
#define	t_dtrace_systrace_args	td_dtrace->td_systrace_args
#define	t_fasttrap_tp_gen	td_dtrace->td_fasttrap_tp_gen
#define	p_dtrace_helpers	p_dtrace->p_dtrace_helpers
#define	p_dtrace_count	p_dtrace->p_dtrace_count
#define	p_dtrace_probes	p_dtrace->p_dtrace_probes
#define	p_model		p_dtrace->p_dtrace_model
#define	p_fasttrap_tp_gen	p_dtrace->p_fasttrap_tp_gen

#define	DATAMODEL_NATIVE	0
#ifdef __amd64__
#define	DATAMODEL_LP64		0
#define	DATAMODEL_ILP32		1
#else
#define	DATAMODEL_LP64		1
#define	DATAMODEL_ILP32		0
#endif

/*
 * Definitions for fields in struct proc which are named differently in FreeBSD.
 */
#define	p_cred		p_ucred
#define	p_parent	p_pptr

/*
 * Definitions for fields in struct thread which are named differently in FreeBSD.
 */
#define	t_procp		td_proc
#define	t_tid		td_tid
#define	t_did		td_tid
#define	t_cred		td_ucred


int priv_policy(const cred_t *, int, boolean_t, int, const char *);
boolean_t priv_policy_only(const cred_t *, int, boolean_t);
boolean_t priv_policy_choice(const cred_t *, int, boolean_t);

/*
 * Test privilege. Audit success or failure, allow privilege debugging.
 * Returns 0 for success, err for failure.
 */
#define	PRIV_POLICY(cred, priv, all, err, reason) \
		priv_policy((cred), (priv), (all), (err), (reason))

/*
 * Test privilege. Audit success only, no privilege debugging.
 * Returns 1 for success, and 0 for failure.
 */
#define	PRIV_POLICY_CHOICE(cred, priv, all) \
		priv_policy_choice((cred), (priv), (all))

/*
 * Test privilege. No priv_debugging, no auditing.
 * Returns 1 for success, and 0 for failure.
 */

#define	PRIV_POLICY_ONLY(cred, priv, all) \
		priv_policy_only((cred), (priv), (all))

#endif	/* !_DTRACE_CDDL_H_ */
