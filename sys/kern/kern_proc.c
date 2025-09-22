/*	$OpenBSD: kern_proc.c,v 1.103 2025/06/02 12:29:50 claudio Exp $	*/
/*	$NetBSD: kern_proc.c,v 1.14 1996/02/09 18:59:41 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
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
 *
 *	@(#)kern_proc.c	8.4 (Berkeley) 1/4/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/wait.h>
#include <sys/rwlock.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/signalvar.h>
#include <sys/pool.h>
#include <sys/vnode.h>

/*
 *  Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	U	uidinfolk
 */

struct rwlock uidinfolk;
#define	UIHASH(uid)	(&uihashtbl[(uid) & uihash])
LIST_HEAD(uihashhead, uidinfo) *uihashtbl;	/* [U] */
u_long uihash;				/* [I] size of hash table - 1 */

/*
 * Other process lists
 */
struct tidhashhead *tidhashtbl;
u_long tidhash;
struct pidhashhead *pidhashtbl;
u_long pidhash;
struct pgrphashhead *pgrphashtbl;
u_long pgrphash;
struct processlist allprocess;
struct processlist zombprocess;
struct proclist allproc;

struct pool proc_pool;
struct pool process_pool;
struct pool rusage_pool;
struct pool ucred_pool;
struct pool pgrp_pool;
struct pool session_pool;

void	pgdelete(struct pgrp *);
void	fixjobc(struct process *, struct pgrp *, int);

static void orphanpg(struct pgrp *);
#ifdef DEBUG
void pgrpdump(void);
#endif

/*
 * Initialize global process hashing structures.
 */
void
procinit(void)
{
	LIST_INIT(&allprocess);
	LIST_INIT(&zombprocess);
	LIST_INIT(&allproc);

	rw_init(&uidinfolk, "uidinfo");

	tidhashtbl = hashinit(maxthread / 4, M_PROC, M_NOWAIT, &tidhash);
	pidhashtbl = hashinit(maxprocess / 4, M_PROC, M_NOWAIT, &pidhash);
	pgrphashtbl = hashinit(maxprocess / 4, M_PROC, M_NOWAIT, &pgrphash);
	uihashtbl = hashinit(maxprocess / 16, M_PROC, M_NOWAIT, &uihash);
	if (!tidhashtbl || !pidhashtbl || !pgrphashtbl || !uihashtbl)
		panic("procinit: malloc");

	pool_init(&proc_pool, sizeof(struct proc), 0, IPL_NONE,
	    PR_WAITOK, "procpl", NULL);
	pool_init(&process_pool, sizeof(struct process), 0, IPL_NONE,
	    PR_WAITOK, "processpl", NULL);
	pool_init(&rusage_pool, sizeof(struct rusage), 0, IPL_NONE,
	    PR_WAITOK, "zombiepl", NULL);
	pool_init(&ucred_pool, sizeof(struct ucred), 0, IPL_MPFLOOR,
	    0, "ucredpl", NULL);
	pool_init(&pgrp_pool, sizeof(struct pgrp), 0, IPL_NONE,
	    PR_WAITOK, "pgrppl", NULL);
	pool_init(&session_pool, sizeof(struct session), 0, IPL_NONE,
	    PR_WAITOK, "sessionpl", NULL);
}

/*
 * This returns with `uidinfolk' held: caller must call uid_release()
 * after making whatever change they needed.
 */
struct uidinfo *
uid_find(uid_t uid)
{
	struct uidinfo *uip, *nuip;
	struct uihashhead *uipp;

	uipp = UIHASH(uid);
	rw_enter_write(&uidinfolk);
	LIST_FOREACH(uip, uipp, ui_hash)
		if (uip->ui_uid == uid)
			break;
	if (uip)
		return (uip);
	rw_exit_write(&uidinfolk);
	nuip = malloc(sizeof(*nuip), M_PROC, M_WAITOK|M_ZERO);
	rw_enter_write(&uidinfolk);
	LIST_FOREACH(uip, uipp, ui_hash)
		if (uip->ui_uid == uid)
			break;
	if (uip) {
		free(nuip, M_PROC, sizeof(*nuip));
		return (uip);
	}
	nuip->ui_uid = uid;
	LIST_INSERT_HEAD(uipp, nuip, ui_hash);

	return (nuip);
}

void
uid_release(struct uidinfo *uip)
{
	rw_exit_write(&uidinfolk);
}

/*
 * Change the count associated with number of threads
 * a given user is using.
 */
int
chgproccnt(uid_t uid, int diff)
{
	struct uidinfo *uip;
	long count;

	uip = uid_find(uid);
	count = (uip->ui_proccnt += diff);
	uid_release(uip);
	if (count < 0)
		panic("chgproccnt: procs < 0");
	return count;
}

/*
 * Is pr an inferior of parent?
 */
int
inferior(struct process *pr, struct process *parent)
{

	for (; pr != parent; pr = pr->ps_pptr)
		if (pr->ps_pid == 0 || pr->ps_pid == 1)
			return (0);
	return (1);
}

/*
 * Locate a proc (thread) by number
 */
struct proc *
tfind(pid_t tid)
{
	struct proc *p;

	LIST_FOREACH(p, TIDHASH(tid), p_hash)
		if (p->p_tid == tid)
			return (p);
	return (NULL);
}

/*
 * Locate a thread by userspace id, from a given process.
 */
struct proc *
tfind_user(pid_t tid, struct process *pr)
{
	struct proc *p;

	if (tid < THREAD_PID_OFFSET)
		return NULL;
	p = tfind(tid - THREAD_PID_OFFSET);

	/* verify we found a thread in the correct process */
	if (p != NULL && p->p_p != pr)
		p = NULL;
	return p;
}

/*
 * Locate a process by number
 */
struct process *
prfind(pid_t pid)
{
	struct process *pr;

	LIST_FOREACH(pr, PIDHASH(pid), ps_hash)
		if (pr->ps_pid == pid)
			return (pr);
	return (NULL);
}

/*
 * Locate a process group by number
 */
struct pgrp *
pgfind(pid_t pgid)
{
	struct pgrp *pgrp;

	LIST_FOREACH(pgrp, PGRPHASH(pgid), pg_hash)
		if (pgrp->pg_id == pgid)
			return (pgrp);
	return (NULL);
}

/*
 * Locate a zombie process
 */
struct process *
zombiefind(pid_t pid)
{
	struct process *pr;

	LIST_FOREACH(pr, &zombprocess, ps_list)
		if (pr->ps_pid == pid)
			return (pr);
	return (NULL);
}

/*
 * Move process to a new process group.  If a session is provided
 * then it's a new session to contain this process group; otherwise
 * the process is staying within its existing session.
 */
void
enternewpgrp(struct process *pr, struct pgrp *pgrp, struct session *newsess)
{
#ifdef DIAGNOSTIC
	if (SESS_LEADER(pr))
		panic("%s: session leader attempted setpgrp", __func__);
#endif

	if (newsess != NULL) {
		/*
		 * New session.  Initialize it completely
		 */
		timeout_set(&newsess->s_verauthto, zapverauth, newsess);
		newsess->s_leader = pr;
		newsess->s_count = 1;
		newsess->s_ttyvp = NULL;
		newsess->s_ttyp = NULL;
		memcpy(newsess->s_login, pr->ps_session->s_login,
		    sizeof(newsess->s_login));
		atomic_clearbits_int(&pr->ps_flags, PS_CONTROLT);
		pgrp->pg_session = newsess;
#ifdef DIAGNOSTIC
		if (pr != curproc->p_p)
			panic("%s: mksession but not curproc", __func__);
#endif
	} else {
		pgrp->pg_session = pr->ps_session;
		pgrp->pg_session->s_count++;
	}
	pgrp->pg_id = pr->ps_pid;
	LIST_INIT(&pgrp->pg_members);
	LIST_INIT(&pgrp->pg_sigiolst);
	LIST_INSERT_HEAD(PGRPHASH(pr->ps_pid), pgrp, pg_hash);
	pgrp->pg_jobc = 0;

	enterthispgrp(pr, pgrp);
}

/*
 * move process to an existing process group
 */
void
enterthispgrp(struct process *pr, struct pgrp *pgrp)
{
	struct pgrp *savepgrp = pr->ps_pgrp;

	/*
	 * Adjust eligibility of affected pgrps to participate in job control.
	 * Increment eligibility counts before decrementing, otherwise we
	 * could reach 0 spuriously during the first call.
	 */
	fixjobc(pr, pgrp, 1);
	fixjobc(pr, savepgrp, 0);

	LIST_REMOVE(pr, ps_pglist);
	mtx_enter(&pr->ps_mtx);
	pr->ps_pgrp = pgrp;
	mtx_leave(&pr->ps_mtx);
	LIST_INSERT_HEAD(&pgrp->pg_members, pr, ps_pglist);
	if (LIST_EMPTY(&savepgrp->pg_members))
		pgdelete(savepgrp);
}

/*
 * remove process from process group
 */
void
leavepgrp(struct process *pr)
{

	if (pr->ps_session->s_verauthppid == pr->ps_pid)
		zapverauth(pr->ps_session);
	LIST_REMOVE(pr, ps_pglist);
	if (LIST_EMPTY(&pr->ps_pgrp->pg_members))
		pgdelete(pr->ps_pgrp);
	mtx_enter(&pr->ps_mtx);
	pr->ps_pgrp = NULL;
	mtx_leave(&pr->ps_mtx);
}

/*
 * delete a process group
 */
void
pgdelete(struct pgrp *pgrp)
{
	sigio_freelist(&pgrp->pg_sigiolst);

	if (pgrp->pg_session->s_ttyp != NULL && 
	    pgrp->pg_session->s_ttyp->t_pgrp == pgrp)
		pgrp->pg_session->s_ttyp->t_pgrp = NULL;
	LIST_REMOVE(pgrp, pg_hash);
	SESSRELE(pgrp->pg_session);
	pool_put(&pgrp_pool, pgrp);
}

void
zapverauth(void *v)
{
	struct session *sess = v;
	sess->s_verauthuid = 0;
	sess->s_verauthppid = 0;
}

/*
 * Adjust pgrp jobc counters when specified process changes process group.
 * We count the number of processes in each process group that "qualify"
 * the group for terminal job control (those with a parent in a different
 * process group of the same session).  If that count reaches zero, the
 * process group becomes orphaned.  Check both the specified process'
 * process group and that of its children.
 * entering == 0 => pr is leaving specified group.
 * entering == 1 => pr is entering specified group.
 * XXX need proctree lock
 */
void
fixjobc(struct process *pr, struct pgrp *pgrp, int entering)
{
	struct pgrp *hispgrp;
	struct session *mysession = pgrp->pg_session;

	/*
	 * Check pr's parent to see whether pr qualifies its own process
	 * group; if so, adjust count for pr's process group.
	 */
	if ((hispgrp = pr->ps_pptr->ps_pgrp) != pgrp &&
	    hispgrp->pg_session == mysession) {
		if (entering)
			pgrp->pg_jobc++;
		else if (--pgrp->pg_jobc == 0)
			orphanpg(pgrp);
	}

	/*
	 * Check this process' children to see whether they qualify
	 * their process groups; if so, adjust counts for children's
	 * process groups.
	 */
	LIST_FOREACH(pr, &pr->ps_children, ps_sibling)
		if ((hispgrp = pr->ps_pgrp) != pgrp &&
		    hispgrp->pg_session == mysession &&
		    (pr->ps_flags & PS_ZOMBIE) == 0) {
			if (entering)
				hispgrp->pg_jobc++;
			else if (--hispgrp->pg_jobc == 0)
				orphanpg(hispgrp);
		}
}

void
killjobc(struct process *pr)
{
	if (SESS_LEADER(pr)) {
		struct session *sp = pr->ps_session;

		if (sp->s_ttyvp) {
			struct vnode *ovp;

			/*
			 * Controlling process.
			 * Signal foreground pgrp,
			 * drain controlling terminal
			 * and revoke access to controlling terminal.
			 */
			if (sp->s_ttyp->t_session == sp) {
				if (sp->s_ttyp->t_pgrp)
					pgsignal(sp->s_ttyp->t_pgrp, SIGHUP, 1);
				ttywait(sp->s_ttyp);
				/*
				 * The tty could have been revoked
				 * if we blocked.
				 */
				if (sp->s_ttyvp)
					VOP_REVOKE(sp->s_ttyvp, REVOKEALL);
			}
			ovp = sp->s_ttyvp;
			sp->s_ttyvp = NULL;
			if (ovp)
				vrele(ovp);
			/*
			 * s_ttyp is not zero'd; we use this to
			 * indicate that the session once had a
			 * controlling terminal.  (for logging and
			 * informational purposes)
			 */
		}
		sp->s_leader = NULL;
	}
	fixjobc(pr, pr->ps_pgrp, 0);
}

/* 
 * A process group has become orphaned;
 * if there are any stopped processes in the group,
 * hang-up all process in that group.
 */
static void
orphanpg(struct pgrp *pg)
{
	struct process *pr;

	LIST_FOREACH(pr, &pg->pg_members, ps_pglist) {
		if (pr->ps_flags & PS_STOPPED) {
			LIST_FOREACH(pr, &pg->pg_members, ps_pglist) {
				prsignal(pr, SIGHUP);
				prsignal(pr, SIGCONT);
			}
			return;
		}
	}
}

#ifdef DDB
void 
proc_printit(struct proc *p, const char *modif,
    int (*pr)(const char *, ...) __attribute__((__format__(__kprintf__,1,2))))
{
	static const char *const pstat[] = {
		"idle", "run", "sleep", "stop", "zombie", "dead", "onproc"
	};
	char pstbuf[5];
	const char *pst = pstbuf;


	if (p->p_stat < 1 || p->p_stat > sizeof(pstat) / sizeof(pstat[0]))
		snprintf(pstbuf, sizeof(pstbuf), "%d", p->p_stat);
	else
		pst = pstat[(int)p->p_stat - 1];

	(*pr)("PROC (%s) tid=%d pid=%d tcnt=%d stat=%s\n", p->p_p->ps_comm,
	    p->p_tid, p->p_p->ps_pid, p->p_p->ps_threadcnt, pst);
	(*pr)("    flags process=%b proc=%b\n",
	    p->p_p->ps_flags, PS_BITS, p->p_flag, P_BITS);
	(*pr)("    runpri=%u, usrpri=%u, slppri=%u, nice=%d\n",
	    p->p_runpri, p->p_usrpri, p->p_slppri, p->p_p->ps_nice);
	(*pr)("    wchan=%p, wmesg=%s, ps_single=%p scnt=%d ecnt=%d\n",
	    p->p_wchan, (p->p_wchan && p->p_wmesg) ?  p->p_wmesg : "",
	    p->p_p->ps_single, p->p_p->ps_suspendcnt, p->p_p->ps_exitcnt);
	(*pr)("    forw=%p, list=%p,%p\n",
	    TAILQ_NEXT(p, p_runq), p->p_list.le_next, p->p_list.le_prev);
	(*pr)("    process=%p user=%p, vmspace=%p\n",
	    p->p_p, p->p_addr, p->p_vmspace);
	(*pr)("    estcpu=%u, cpticks=%d, pctcpu=%u.%u, "
	    "user=%llu, sys=%llu, intr=%llu\n",
	    p->p_estcpu, p->p_cpticks, p->p_pctcpu / 100, p->p_pctcpu % 100,
	    p->p_tu.tu_uticks, p->p_tu.tu_sticks, p->p_tu.tu_iticks);
}
#include <machine/db_machdep.h>

#include <ddb/db_output.h>

void
db_kill_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct process *pr;
	struct proc *p;

	pr = prfind(addr);
	if (pr == NULL) {
		db_printf("%ld: No such process", addr);
		return;
	}

	p = TAILQ_FIRST(&pr->ps_threads);

	/* Send uncatchable SIGABRT for coredump */
	sigabort(p);
}

void
db_show_all_procs(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	char *mode;
	int skipzomb = 0;
	int has_kernel_lock = 0;
	struct proc *p;
	struct process *pr, *ppr;

	if (modif[0] == 0)
		modif[0] = 'n';			/* default == normal mode */

	mode = "mawno";
	while (*mode && *mode != modif[0])
		mode++;
	if (*mode == 0 || *mode == 'm') {
		db_printf("usage: show all procs [/a] [/n] [/w]\n");
		db_printf("\t/a == show process address info\n");
		db_printf("\t/n == show normal process info [default]\n");
		db_printf("\t/w == show process pgrp/wait info\n");
		db_printf("\t/o == show normal info for non-idle SONPROC\n");
		return;
	}

	pr = LIST_FIRST(&allprocess);

	switch (*mode) {

	case 'a':
		db_printf("    TID  %-9s  %18s  %18s  %18s\n",
		    "COMMAND", "STRUCT PROC *", "UAREA *", "VMSPACE/VM_MAP");
		break;
	case 'n':
		db_printf("   PID  %6s  %5s  %5s  S  %10s  %-12s  %-15s\n",
		    "TID", "PPID", "UID", "FLAGS", "WAIT", "COMMAND");
		break;
	case 'w':
		db_printf("    TID  %-15s  %-5s  %18s  %s\n",
		    "COMMAND", "PGRP", "WAIT-CHANNEL", "WAIT-MSG");
		break;
	case 'o':
		skipzomb = 1;
		db_printf("    TID  %5s  %5s  %10s %10s  %3s  %-30s\n",
		    "PID", "UID", "PRFLAGS", "PFLAGS", "CPU", "COMMAND");
		break;
	}

	while (pr != NULL) {
		ppr = pr->ps_pptr;

		TAILQ_FOREACH(p, &pr->ps_threads, p_thr_link) {
#ifdef MULTIPROCESSOR
			if (p->p_cpu != NULL &&
			    __mp_lock_held(&kernel_lock, p->p_cpu))
				has_kernel_lock = 1;
			else
				has_kernel_lock = 0;
#endif
			if (p->p_stat) {
				if (*mode == 'o') {
					if (p->p_stat != SONPROC)
						continue;
					if (p->p_cpu != NULL && p->p_cpu->
					    ci_schedstate.spc_idleproc == p)
						continue;
				}

				if (*mode == 'n') {
					db_printf("%c%5d  ", (p == curproc ?
					    '*' : ' '), pr->ps_pid);
				} else {
					db_printf("%c%6d  ", (p == curproc ?
					    '*' : ' '), p->p_tid);
				}

				switch (*mode) {

				case 'a':
					db_printf("%-9.9s  %18p  %18p  %18p\n",
					    pr->ps_comm, p, p->p_addr, p->p_vmspace);
					break;

				case 'n':
					db_printf("%6d  %5d  %5d  %d  %#10x  "
					    "%-12.12s  %-15s\n",
					    p->p_tid, ppr ? ppr->ps_pid : -1,
					    pr->ps_ucred->cr_ruid, p->p_stat,
					    p->p_flag | pr->ps_flags,
					    (p->p_wchan && p->p_wmesg) ?
						p->p_wmesg : "", pr->ps_comm);
					break;

				case 'w':
					db_printf("%-15s  %-5d  %18p  %s\n",
					    pr->ps_comm, (pr->ps_pgrp ?
						pr->ps_pgrp->pg_id : -1),
					    p->p_wchan,
					    (p->p_wchan && p->p_wmesg) ?
						p->p_wmesg : "");
					break;

				case 'o':
					db_printf("%5d  %5d  %#10x %#10x  %3d"
					    "%c %-31s\n",
					    pr->ps_pid, pr->ps_ucred->cr_ruid,
					    pr->ps_flags, p->p_flag,
					    CPU_INFO_UNIT(p->p_cpu),
					    has_kernel_lock ? 'K' : ' ',
					    pr->ps_comm);
					break;

				}
			}
		}
		pr = LIST_NEXT(pr, ps_list);
		if (pr == NULL && skipzomb == 0) {
			skipzomb = 1;
			pr = LIST_FIRST(&zombprocess);
		}
	}
}
#endif

#ifdef DEBUG
void
pgrpdump(void)
{
	struct pgrp *pgrp;
	struct process *pr;
	int i;

	for (i = 0; i <= pgrphash; i++) {
		if (!LIST_EMPTY(&pgrphashtbl[i])) {
			printf("\tindx %d\n", i);
			LIST_FOREACH(pgrp, &pgrphashtbl[i], pg_hash) {
				printf("\tpgrp %p, pgid %d, sess %p, sesscnt %d, mem %p\n",
				    pgrp, pgrp->pg_id, pgrp->pg_session,
				    pgrp->pg_session->s_count,
				    LIST_FIRST(&pgrp->pg_members));
				LIST_FOREACH(pr, &pgrp->pg_members, ps_pglist) {
					printf("\t\tpid %d addr %p pgrp %p\n", 
					    pr->ps_pid, pr, pr->ps_pgrp);
				}
			}
		}
	}
}
#endif /* DEBUG */
