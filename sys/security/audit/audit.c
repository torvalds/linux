/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999-2005 Apple Inc.
 * Copyright (c) 2006-2007, 2016-2018 Robert N. M. Watson
 * All rights reserved.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/fcntl.h>
#include <sys/ipc.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/ucred.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/unistd.h>
#include <sys/vnode.h>

#include <bsm/audit.h>
#include <bsm/audit_internal.h>
#include <bsm/audit_kevents.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>

#include <security/audit/audit.h>
#include <security/audit/audit_private.h>

#include <vm/uma.h>

FEATURE(audit, "BSM audit support");

static uma_zone_t	audit_record_zone;
static MALLOC_DEFINE(M_AUDITCRED, "audit_cred", "Audit cred storage");
MALLOC_DEFINE(M_AUDITDATA, "audit_data", "Audit data storage");
MALLOC_DEFINE(M_AUDITPATH, "audit_path", "Audit path storage");
MALLOC_DEFINE(M_AUDITTEXT, "audit_text", "Audit text storage");
MALLOC_DEFINE(M_AUDITGIDSET, "audit_gidset", "Audit GID set storage");

static SYSCTL_NODE(_security, OID_AUTO, audit, CTLFLAG_RW, 0,
    "TrustedBSD audit controls");

/*
 * Audit control settings that are set/read by system calls and are hence
 * non-static.
 *
 * Define the audit control flags.
 */
int			audit_trail_enabled;
int			audit_trail_suspended;
#ifdef KDTRACE_HOOKS
u_int			audit_dtrace_enabled;
#endif
bool __read_frequently	audit_syscalls_enabled;

/*
 * Flags controlling behavior in low storage situations.  Should we panic if
 * a write fails?  Should we fail stop if we're out of disk space?
 */
int			audit_panic_on_write_fail;
int			audit_fail_stop;
int			audit_argv;
int			audit_arge;

/*
 * Are we currently "failing stop" due to out of disk space?
 */
int			audit_in_failure;

/*
 * Global audit statistics.
 */
struct audit_fstat	audit_fstat;

/*
 * Preselection mask for non-attributable events.
 */
struct au_mask		audit_nae_mask;

/*
 * Mutex to protect global variables shared between various threads and
 * processes.
 */
struct mtx		audit_mtx;

/*
 * Queue of audit records ready for delivery to disk.  We insert new records
 * at the tail, and remove records from the head.  Also, a count of the
 * number of records used for checking queue depth.  In addition, a counter
 * of records that we have allocated but are not yet in the queue, which is
 * needed to estimate the total size of the combined set of records
 * outstanding in the system.
 */
struct kaudit_queue	audit_q;
int			audit_q_len;
int			audit_pre_q_len;

/*
 * Audit queue control settings (minimum free, low/high water marks, etc.)
 */
struct au_qctrl		audit_qctrl;

/*
 * Condition variable to signal to the worker that it has work to do: either
 * new records are in the queue, or a log replacement is taking place.
 */
struct cv		audit_worker_cv;

/*
 * Condition variable to flag when crossing the low watermark, meaning that
 * threads blocked due to hitting the high watermark can wake up and continue
 * to commit records.
 */
struct cv		audit_watermark_cv;

/*
 * Condition variable for  auditing threads wait on when in fail-stop mode.
 * Threads wait on this CV forever (and ever), never seeing the light of day
 * again.
 */
static struct cv	audit_fail_cv;

/*
 * Optional DTrace audit provider support: function pointers for preselection
 * and commit events.
 */
#ifdef KDTRACE_HOOKS
void	*(*dtaudit_hook_preselect)(au_id_t auid, au_event_t event,
	    au_class_t class);
int	(*dtaudit_hook_commit)(struct kaudit_record *kar, au_id_t auid,
	    au_event_t event, au_class_t class, int sorf);
void	(*dtaudit_hook_bsm)(struct kaudit_record *kar, au_id_t auid,
	    au_event_t event, au_class_t class, int sorf,
	    void *bsm_data, size_t bsm_lenlen);
#endif

/*
 * Kernel audit information.  This will store the current audit address
 * or host information that the kernel will use when it's generating
 * audit records.  This data is modified by the A_GET{SET}KAUDIT auditon(2)
 * command.
 */
static struct auditinfo_addr	audit_kinfo;
static struct rwlock		audit_kinfo_lock;

#define	KINFO_LOCK_INIT()	rw_init(&audit_kinfo_lock, \
				    "audit_kinfo_lock")
#define	KINFO_RLOCK()		rw_rlock(&audit_kinfo_lock)
#define	KINFO_WLOCK()		rw_wlock(&audit_kinfo_lock)
#define	KINFO_RUNLOCK()		rw_runlock(&audit_kinfo_lock)
#define	KINFO_WUNLOCK()		rw_wunlock(&audit_kinfo_lock)

/*
 * Check various policies to see if we should enable system-call audit hooks.
 * Note that despite the mutex being held, we want to assign a value exactly
 * once, as checks of the flag are performed lock-free for performance
 * reasons.  The mutex is used to get a consistent snapshot of policy state --
 * e.g., safely accessing the two audit_trail flags.
 */
void
audit_syscalls_enabled_update(void)
{

	mtx_lock(&audit_mtx);
#ifdef KDTRACE_HOOKS
	if (audit_dtrace_enabled)
		audit_syscalls_enabled = true;
	else {
#endif
		if (audit_trail_enabled && !audit_trail_suspended)
			audit_syscalls_enabled = true;
		else
			audit_syscalls_enabled = false;
#ifdef KDTRACE_HOOKS
	}
#endif
	mtx_unlock(&audit_mtx);
}

void
audit_set_kinfo(struct auditinfo_addr *ak)
{

	KASSERT(ak->ai_termid.at_type == AU_IPv4 ||
	    ak->ai_termid.at_type == AU_IPv6,
	    ("audit_set_kinfo: invalid address type"));

	KINFO_WLOCK();
	audit_kinfo = *ak;
	KINFO_WUNLOCK();
}

void
audit_get_kinfo(struct auditinfo_addr *ak)
{

	KASSERT(audit_kinfo.ai_termid.at_type == AU_IPv4 ||
	    audit_kinfo.ai_termid.at_type == AU_IPv6,
	    ("audit_set_kinfo: invalid address type"));

	KINFO_RLOCK();
	*ak = audit_kinfo;
	KINFO_RUNLOCK();
}

/*
 * Construct an audit record for the passed thread.
 */
static int
audit_record_ctor(void *mem, int size, void *arg, int flags)
{
	struct kaudit_record *ar;
	struct thread *td;
	struct ucred *cred;
	struct prison *pr;

	KASSERT(sizeof(*ar) == size, ("audit_record_ctor: wrong size"));

	td = arg;
	ar = mem;
	bzero(ar, sizeof(*ar));
	ar->k_ar.ar_magic = AUDIT_RECORD_MAGIC;
	nanotime(&ar->k_ar.ar_starttime);

	/*
	 * Export the subject credential.
	 */
	cred = td->td_ucred;
	cru2x(cred, &ar->k_ar.ar_subj_cred);
	ar->k_ar.ar_subj_ruid = cred->cr_ruid;
	ar->k_ar.ar_subj_rgid = cred->cr_rgid;
	ar->k_ar.ar_subj_egid = cred->cr_groups[0];
	ar->k_ar.ar_subj_auid = cred->cr_audit.ai_auid;
	ar->k_ar.ar_subj_asid = cred->cr_audit.ai_asid;
	ar->k_ar.ar_subj_pid = td->td_proc->p_pid;
	ar->k_ar.ar_subj_amask = cred->cr_audit.ai_mask;
	ar->k_ar.ar_subj_term_addr = cred->cr_audit.ai_termid;
	/*
	 * If this process is jailed, make sure we capture the name of the
	 * jail so we can use it to generate a zonename token when we covert
	 * this record to BSM.
	 */
	if (jailed(cred)) {
		pr = cred->cr_prison;
		(void) strlcpy(ar->k_ar.ar_jailname, pr->pr_name,
		    sizeof(ar->k_ar.ar_jailname));
	} else
		ar->k_ar.ar_jailname[0] = '\0';
	return (0);
}

static void
audit_record_dtor(void *mem, int size, void *arg)
{
	struct kaudit_record *ar;

	KASSERT(sizeof(*ar) == size, ("audit_record_dtor: wrong size"));

	ar = mem;
	if (ar->k_ar.ar_arg_upath1 != NULL)
		free(ar->k_ar.ar_arg_upath1, M_AUDITPATH);
	if (ar->k_ar.ar_arg_upath2 != NULL)
		free(ar->k_ar.ar_arg_upath2, M_AUDITPATH);
	if (ar->k_ar.ar_arg_text != NULL)
		free(ar->k_ar.ar_arg_text, M_AUDITTEXT);
	if (ar->k_udata != NULL)
		free(ar->k_udata, M_AUDITDATA);
	if (ar->k_ar.ar_arg_argv != NULL)
		free(ar->k_ar.ar_arg_argv, M_AUDITTEXT);
	if (ar->k_ar.ar_arg_envv != NULL)
		free(ar->k_ar.ar_arg_envv, M_AUDITTEXT);
	if (ar->k_ar.ar_arg_groups.gidset != NULL)
		free(ar->k_ar.ar_arg_groups.gidset, M_AUDITGIDSET);
}

/*
 * Initialize the Audit subsystem: configuration state, work queue,
 * synchronization primitives, worker thread, and trigger device node.  Also
 * call into the BSM assembly code to initialize it.
 */
static void
audit_init(void)
{

	audit_trail_enabled = 0;
	audit_trail_suspended = 0;
	audit_syscalls_enabled = false;
	audit_panic_on_write_fail = 0;
	audit_fail_stop = 0;
	audit_in_failure = 0;
	audit_argv = 0;
	audit_arge = 0;

	audit_fstat.af_filesz = 0;	/* '0' means unset, unbounded. */
	audit_fstat.af_currsz = 0;
	audit_nae_mask.am_success = 0;
	audit_nae_mask.am_failure = 0;

	TAILQ_INIT(&audit_q);
	audit_q_len = 0;
	audit_pre_q_len = 0;
	audit_qctrl.aq_hiwater = AQ_HIWATER;
	audit_qctrl.aq_lowater = AQ_LOWATER;
	audit_qctrl.aq_bufsz = AQ_BUFSZ;
	audit_qctrl.aq_minfree = AU_FS_MINFREE;

	audit_kinfo.ai_termid.at_type = AU_IPv4;
	audit_kinfo.ai_termid.at_addr[0] = INADDR_ANY;

	mtx_init(&audit_mtx, "audit_mtx", NULL, MTX_DEF);
	KINFO_LOCK_INIT();
	cv_init(&audit_worker_cv, "audit_worker_cv");
	cv_init(&audit_watermark_cv, "audit_watermark_cv");
	cv_init(&audit_fail_cv, "audit_fail_cv");

	audit_record_zone = uma_zcreate("audit_record",
	    sizeof(struct kaudit_record), audit_record_ctor,
	    audit_record_dtor, NULL, NULL, UMA_ALIGN_PTR, 0);

	/* First initialisation of audit_syscalls_enabled. */
	audit_syscalls_enabled_update();

	/* Initialize the BSM audit subsystem. */
	kau_init();

	audit_trigger_init();

	/* Register shutdown handler. */
	EVENTHANDLER_REGISTER(shutdown_pre_sync, audit_shutdown, NULL,
	    SHUTDOWN_PRI_FIRST);

	/* Start audit worker thread. */
	audit_worker_init();
}

SYSINIT(audit_init, SI_SUB_AUDIT, SI_ORDER_FIRST, audit_init, NULL);

/*
 * Drain the audit queue and close the log at shutdown.  Note that this can
 * be called both from the system shutdown path and also from audit
 * configuration syscalls, so 'arg' and 'howto' are ignored.
 *
 * XXXRW: In FreeBSD 7.x and 8.x, this fails to wait for the record queue to
 * drain before returning, which could lead to lost records on shutdown.
 */
void
audit_shutdown(void *arg, int howto)
{

	audit_rotate_vnode(NULL, NULL);
}

/*
 * Return the current thread's audit record, if any.
 */
struct kaudit_record *
currecord(void)
{

	return (curthread->td_ar);
}

/*
 * XXXAUDIT: Shouldn't there be logic here to sleep waiting on available
 * pre_q space, suspending the system call until there is room?
 */
struct kaudit_record *
audit_new(int event, struct thread *td)
{
	struct kaudit_record *ar;

	/*
	 * Note: the number of outstanding uncommitted audit records is
	 * limited to the number of concurrent threads servicing system calls
	 * in the kernel.
	 */
	ar = uma_zalloc_arg(audit_record_zone, td, M_WAITOK);
	ar->k_ar.ar_event = event;

	mtx_lock(&audit_mtx);
	audit_pre_q_len++;
	mtx_unlock(&audit_mtx);

	return (ar);
}

void
audit_free(struct kaudit_record *ar)
{

	uma_zfree(audit_record_zone, ar);
}

void
audit_commit(struct kaudit_record *ar, int error, int retval)
{
	au_event_t event;
	au_class_t class;
	au_id_t auid;
	int sorf;
	struct au_mask *aumask;

	if (ar == NULL)
		return;

	ar->k_ar.ar_errno = error;
	ar->k_ar.ar_retval = retval;
	nanotime(&ar->k_ar.ar_endtime);

	/*
	 * Decide whether to commit the audit record by checking the error
	 * value from the system call and using the appropriate audit mask.
	 */
	if (ar->k_ar.ar_subj_auid == AU_DEFAUDITID)
		aumask = &audit_nae_mask;
	else
		aumask = &ar->k_ar.ar_subj_amask;

	if (error)
		sorf = AU_PRS_FAILURE;
	else
		sorf = AU_PRS_SUCCESS;

	/*
	 * syscalls.master sometimes contains a prototype event number, which
	 * we will transform into a more specific event number now that we
	 * have more complete information gathered during the system call.
	 */
	switch(ar->k_ar.ar_event) {
	case AUE_OPEN_RWTC:
		ar->k_ar.ar_event = audit_flags_and_error_to_openevent(
		    ar->k_ar.ar_arg_fflags, error);
		break;

	case AUE_OPENAT_RWTC:
		ar->k_ar.ar_event = audit_flags_and_error_to_openatevent(
		    ar->k_ar.ar_arg_fflags, error);
		break;

	case AUE_SYSCTL:
		ar->k_ar.ar_event = audit_ctlname_to_sysctlevent(
		    ar->k_ar.ar_arg_ctlname, ar->k_ar.ar_valid_arg);
		break;

	case AUE_AUDITON:
		/* Convert the auditon() command to an event. */
		ar->k_ar.ar_event = auditon_command_event(ar->k_ar.ar_arg_cmd);
		break;

	case AUE_MSGSYS:
		if (ARG_IS_VALID(ar, ARG_SVIPC_WHICH))
			ar->k_ar.ar_event =
			    audit_msgsys_to_event(ar->k_ar.ar_arg_svipc_which);
		break;

	case AUE_SEMSYS:
		if (ARG_IS_VALID(ar, ARG_SVIPC_WHICH))
			ar->k_ar.ar_event =
			    audit_semsys_to_event(ar->k_ar.ar_arg_svipc_which);
		break;

	case AUE_SHMSYS:
		if (ARG_IS_VALID(ar, ARG_SVIPC_WHICH))
			ar->k_ar.ar_event =
			    audit_shmsys_to_event(ar->k_ar.ar_arg_svipc_which);
		break;
	}

	auid = ar->k_ar.ar_subj_auid;
	event = ar->k_ar.ar_event;
	class = au_event_class(event);

	ar->k_ar_commit |= AR_COMMIT_KERNEL;
	if (au_preselect(event, class, aumask, sorf) != 0)
		ar->k_ar_commit |= AR_PRESELECT_TRAIL;
	if (audit_pipe_preselect(auid, event, class, sorf,
	    ar->k_ar_commit & AR_PRESELECT_TRAIL) != 0)
		ar->k_ar_commit |= AR_PRESELECT_PIPE;
#ifdef KDTRACE_HOOKS
	/*
	 * Expose the audit record to DTrace, both to allow the "commit" probe
	 * to fire if it's desirable, and also to allow a decision to be made
	 * about later firing with BSM in the audit worker.
	 */
	if (dtaudit_hook_commit != NULL) {
		if (dtaudit_hook_commit(ar, auid, event, class, sorf) != 0)
			ar->k_ar_commit |= AR_PRESELECT_DTRACE;
	}
#endif

	if ((ar->k_ar_commit & (AR_PRESELECT_TRAIL | AR_PRESELECT_PIPE |
	    AR_PRESELECT_USER_TRAIL | AR_PRESELECT_USER_PIPE |
	    AR_PRESELECT_DTRACE)) == 0) {
		mtx_lock(&audit_mtx);
		audit_pre_q_len--;
		mtx_unlock(&audit_mtx);
		audit_free(ar);
		return;
	}

	/*
	 * Note: it could be that some records initiated while audit was
	 * enabled should still be committed?
	 *
	 * NB: The check here is not for audit_syscalls because any
	 * DTrace-related obligations have been fulfilled above -- we're just
	 * down to the trail and pipes now.
	 */
	mtx_lock(&audit_mtx);
	if (audit_trail_suspended || !audit_trail_enabled) {
		audit_pre_q_len--;
		mtx_unlock(&audit_mtx);
		audit_free(ar);
		return;
	}

	/*
	 * Constrain the number of committed audit records based on the
	 * configurable parameter.
	 */
	while (audit_q_len >= audit_qctrl.aq_hiwater)
		cv_wait(&audit_watermark_cv, &audit_mtx);

	TAILQ_INSERT_TAIL(&audit_q, ar, k_q);
	audit_q_len++;
	audit_pre_q_len--;
	cv_signal(&audit_worker_cv);
	mtx_unlock(&audit_mtx);
}

/*
 * audit_syscall_enter() is called on entry to each system call.  It is
 * responsible for deciding whether or not to audit the call (preselection),
 * and if so, allocating a per-thread audit record.  audit_new() will fill in
 * basic thread/credential properties.
 *
 * This function will be entered only if audit_syscalls_enabled was set in the
 * macro wrapper for this function.  It could be cleared by the time this
 * function runs, but that is an acceptable race.
 */
void
audit_syscall_enter(unsigned short code, struct thread *td)
{
	struct au_mask *aumask;
#ifdef KDTRACE_HOOKS
	void *dtaudit_state;
#endif
	au_class_t class;
	au_event_t event;
	au_id_t auid;
	int record_needed;

	KASSERT(td->td_ar == NULL, ("audit_syscall_enter: td->td_ar != NULL"));
	KASSERT((td->td_pflags & TDP_AUDITREC) == 0,
	    ("audit_syscall_enter: TDP_AUDITREC set"));

	/*
	 * In FreeBSD, each ABI has its own system call table, and hence
	 * mapping of system call codes to audit events.  Convert the code to
	 * an audit event identifier using the process system call table
	 * reference.  In Darwin, there's only one, so we use the global
	 * symbol for the system call table.  No audit record is generated
	 * for bad system calls, as no operation has been performed.
	 */
	if (code >= td->td_proc->p_sysent->sv_size)
		return;

	event = td->td_proc->p_sysent->sv_table[code].sy_auevent;
	if (event == AUE_NULL)
		return;

	/*
	 * Check which audit mask to use; either the kernel non-attributable
	 * event mask or the process audit mask.
	 */
	auid = td->td_ucred->cr_audit.ai_auid;
	if (auid == AU_DEFAUDITID)
		aumask = &audit_nae_mask;
	else
		aumask = &td->td_ucred->cr_audit.ai_mask;

	/*
	 * Determine whether trail or pipe preselection would like an audit
	 * record allocated for this system call.
	 */
	class = au_event_class(event);
	if (au_preselect(event, class, aumask, AU_PRS_BOTH)) {
		/*
		 * If we're out of space and need to suspend unprivileged
		 * processes, do that here rather than trying to allocate
		 * another audit record.
		 *
		 * Note: we might wish to be able to continue here in the
		 * future, if the system recovers.  That should be possible
		 * by means of checking the condition in a loop around
		 * cv_wait().  It might be desirable to reevaluate whether an
		 * audit record is still required for this event by
		 * re-calling au_preselect().
		 */
		if (audit_in_failure &&
		    priv_check(td, PRIV_AUDIT_FAILSTOP) != 0) {
			cv_wait(&audit_fail_cv, &audit_mtx);
			panic("audit_failing_stop: thread continued");
		}
		record_needed = 1;
	} else if (audit_pipe_preselect(auid, event, class, AU_PRS_BOTH, 0)) {
		record_needed = 1;
	} else {
		record_needed = 0;
	}

	/*
	 * After audit trails and pipes have made their policy choices, DTrace
	 * may request that records be generated as well.  This is a slightly
	 * complex affair, as the DTrace audit provider needs the audit
	 * framework to maintain some state on the audit record, which has not
	 * been allocated at the point where the decision has to be made.
	 * This hook must run even if we are not changing the decision, as
	 * DTrace may want to stick event state onto a record we were going to
	 * produce due to the trail or pipes.  The event state returned by the
	 * DTrace provider must be safe without locks held between here and
	 * below -- i.e., dtaudit_state must must refer to stable memory.
	 */
#ifdef KDTRACE_HOOKS
	dtaudit_state = NULL;
        if (dtaudit_hook_preselect != NULL) {
		dtaudit_state = dtaudit_hook_preselect(auid, event, class);
		if (dtaudit_state != NULL)
			record_needed = 1;
	}
#endif

	/*
	 * If a record is required, allocate it and attach it to the thread
	 * for use throughout the system call.  Also attach DTrace state if
	 * required.
	 *
	 * XXXRW: If we decide to reference count the evname_elem underlying
	 * dtaudit_state, we will need to free here if no record is allocated
	 * or allocatable.
	 */
	if (record_needed) {
		td->td_ar = audit_new(event, td);
		if (td->td_ar != NULL) {
			td->td_pflags |= TDP_AUDITREC;
#ifdef KDTRACE_HOOKS
			td->td_ar->k_dtaudit_state = dtaudit_state;
#endif
		}
	} else
		td->td_ar = NULL;
}

/*
 * audit_syscall_exit() is called from the return of every system call, or in
 * the event of exit1(), during the execution of exit1().  It is responsible
 * for committing the audit record, if any, along with return condition.
 */
void
audit_syscall_exit(int error, struct thread *td)
{
	int retval;

	/*
	 * Commit the audit record as desired; once we pass the record into
	 * audit_commit(), the memory is owned by the audit subsystem.  The
	 * return value from the system call is stored on the user thread.
	 * If there was an error, the return value is set to -1, imitating
	 * the behavior of the cerror routine.
	 */
	if (error)
		retval = -1;
	else
		retval = td->td_retval[0];

	audit_commit(td->td_ar, error, retval);
	td->td_ar = NULL;
	td->td_pflags &= ~TDP_AUDITREC;
}

void
audit_cred_copy(struct ucred *src, struct ucred *dest)
{

	bcopy(&src->cr_audit, &dest->cr_audit, sizeof(dest->cr_audit));
}

void
audit_cred_destroy(struct ucred *cred)
{

}

void
audit_cred_init(struct ucred *cred)
{

	bzero(&cred->cr_audit, sizeof(cred->cr_audit));
}

/*
 * Initialize audit information for the first kernel process (proc 0) and for
 * the first user process (init).
 */
void
audit_cred_kproc0(struct ucred *cred)
{

	cred->cr_audit.ai_auid = AU_DEFAUDITID;
	cred->cr_audit.ai_termid.at_type = AU_IPv4;
}

void
audit_cred_proc1(struct ucred *cred)
{

	cred->cr_audit.ai_auid = AU_DEFAUDITID;
	cred->cr_audit.ai_termid.at_type = AU_IPv4;
}

void
audit_thread_alloc(struct thread *td)
{

	td->td_ar = NULL;
}

void
audit_thread_free(struct thread *td)
{

	KASSERT(td->td_ar == NULL, ("audit_thread_free: td_ar != NULL"));
	KASSERT((td->td_pflags & TDP_AUDITREC) == 0,
	    ("audit_thread_free: TDP_AUDITREC set"));
}

void
audit_proc_coredump(struct thread *td, char *path, int errcode)
{
	struct kaudit_record *ar;
	struct au_mask *aumask;
	struct ucred *cred;
	au_class_t class;
	int ret, sorf;
	char **pathp;
	au_id_t auid;

	ret = 0;

	/*
	 * Make sure we are using the correct preselection mask.
	 */
	cred = td->td_ucred;
	auid = cred->cr_audit.ai_auid;
	if (auid == AU_DEFAUDITID)
		aumask = &audit_nae_mask;
	else
		aumask = &cred->cr_audit.ai_mask;
	/*
	 * It's possible for coredump(9) generation to fail.  Make sure that
	 * we handle this case correctly for preselection.
	 */
	if (errcode != 0)
		sorf = AU_PRS_FAILURE;
	else
		sorf = AU_PRS_SUCCESS;
	class = au_event_class(AUE_CORE);
	if (au_preselect(AUE_CORE, class, aumask, sorf) == 0 &&
	    audit_pipe_preselect(auid, AUE_CORE, class, sorf, 0) == 0)
		return;

	/*
	 * If we are interested in seeing this audit record, allocate it.
	 * Where possible coredump records should contain a pathname and arg32
	 * (signal) tokens.
	 */
	ar = audit_new(AUE_CORE, td);
	if (ar == NULL)
		return;
	if (path != NULL) {
		pathp = &ar->k_ar.ar_arg_upath1;
		*pathp = malloc(MAXPATHLEN, M_AUDITPATH, M_WAITOK);
		audit_canon_path(td, AT_FDCWD, path, *pathp);
		ARG_SET_VALID(ar, ARG_UPATH1);
	}
	ar->k_ar.ar_arg_signum = td->td_proc->p_sig;
	ARG_SET_VALID(ar, ARG_SIGNUM);
	if (errcode != 0)
		ret = 1;
	audit_commit(ar, errcode, ret);
}
