/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999-2008 Apple Inc.
 * Copyright (c) 2006-2008, 2016, 2018 Robert N. M. Watson
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
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/sx.h>
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

#include <machine/stdarg.h>

/*
 * Worker thread that will schedule disk I/O, etc.
 */
static struct proc		*audit_thread;

/*
 * audit_cred and audit_vp are the stored credential and vnode to use for
 * active audit trail.  They are protected by the audit worker lock, which
 * will be held across all I/O and all rotation to prevent them from being
 * replaced (rotated) while in use.  The audit_file_rotate_wait flag is set
 * when the kernel has delivered a trigger to auditd to rotate the trail, and
 * is cleared when the next rotation takes place.  It is also protected by
 * the audit worker lock.
 */
static int		 audit_file_rotate_wait;
static struct ucred	*audit_cred;
static struct vnode	*audit_vp;
static off_t		 audit_size;
static struct sx	 audit_worker_lock;

#define	AUDIT_WORKER_LOCK_INIT()	sx_init(&audit_worker_lock, \
					    "audit_worker_lock");
#define	AUDIT_WORKER_LOCK_ASSERT()	sx_assert(&audit_worker_lock, \
					    SA_XLOCKED)
#define	AUDIT_WORKER_LOCK()		sx_xlock(&audit_worker_lock)
#define	AUDIT_WORKER_UNLOCK()		sx_xunlock(&audit_worker_lock)

static void
audit_worker_sync_vp(struct vnode *vp, struct mount *mp, const char *fmt, ...)
{
	struct mount *mp1;
	int error;
	va_list va;

	va_start(va, fmt);
	error = vn_start_write(vp, &mp1, 0);
	if (error == 0) {
		VOP_LOCK(vp, LK_EXCLUSIVE | LK_RETRY);
		(void)VOP_FSYNC(vp, MNT_WAIT, curthread);
		VOP_UNLOCK(vp, 0);
		vn_finished_write(mp1);
	}
	vfs_unbusy(mp);
	vpanic(fmt, va);
	va_end(va);
}

/*
 * Write an audit record to a file, performed as the last stage after both
 * preselection and BSM conversion.  Both space management and write failures
 * are handled in this function.
 *
 * No attempt is made to deal with possible failure to deliver a trigger to
 * the audit daemon, since the message is asynchronous anyway.
 */
static void
audit_record_write(struct vnode *vp, struct ucred *cred, void *data,
    size_t len)
{
	static struct timeval last_lowspace_trigger;
	static struct timeval last_fail;
	static int cur_lowspace_trigger;
	struct statfs *mnt_stat;
	struct mount *mp;
	int error;
	static int cur_fail;
	long temp;

	AUDIT_WORKER_LOCK_ASSERT();

	if (vp == NULL)
		return;

	mp = vp->v_mount;
	if (mp == NULL) {
		error = EINVAL;
		goto fail;
	}
	error = vfs_busy(mp, 0);
	if (error != 0) {
		mp = NULL;
		goto fail;
	}
	mnt_stat = &mp->mnt_stat;

	/*
	 * First, gather statistics on the audit log file and file system so
	 * that we know how we're doing on space.  Consider failure of these
	 * operations to indicate a future inability to write to the file.
	 */
	error = VFS_STATFS(mp, mnt_stat);
	if (error != 0)
		goto fail;

	/*
	 * We handle four different space-related limits:
	 *
	 * - A fixed (hard) limit on the minimum free blocks we require on
	 *   the file system, and results in record loss, a trigger, and
	 *   possible fail stop due to violating invariants.
	 *
	 * - An administrative (soft) limit, which when fallen below, results
	 *   in the kernel notifying the audit daemon of low space.
	 *
	 * - An audit trail size limit, which when gone above, results in the
	 *   kernel notifying the audit daemon that rotation is desired.
	 *
	 * - The total depth of the kernel audit record exceeding free space,
	 *   which can lead to possible fail stop (with drain), in order to
	 *   prevent violating invariants.  Failure here doesn't halt
	 *   immediately, but prevents new records from being generated.
	 *
	 * Possibly, the last of these should be handled differently, always
	 * allowing a full queue to be lost, rather than trying to prevent
	 * loss.
	 *
	 * First, handle the hard limit, which generates a trigger and may
	 * fail stop.  This is handled in the same manner as ENOSPC from
	 * VOP_WRITE, and results in record loss.
	 */
	if (mnt_stat->f_bfree < AUDIT_HARD_LIMIT_FREE_BLOCKS) {
		error = ENOSPC;
		goto fail_enospc;
	}

	/*
	 * Second, handle falling below the soft limit, if defined; we send
	 * the daemon a trigger and continue processing the record.  Triggers
	 * are limited to 1/sec.
	 */
	if (audit_qctrl.aq_minfree != 0) {
		temp = mnt_stat->f_blocks / (100 / audit_qctrl.aq_minfree);
		if (mnt_stat->f_bfree < temp) {
			if (ppsratecheck(&last_lowspace_trigger,
			    &cur_lowspace_trigger, 1)) {
				(void)audit_send_trigger(
				    AUDIT_TRIGGER_LOW_SPACE);
				printf("Warning: disk space low (< %d%% free) "
				    "on audit log file-system\n",
				    audit_qctrl.aq_minfree);
			}
		}
	}

	/*
	 * If the current file is getting full, generate a rotation trigger
	 * to the daemon.  This is only approximate, which is fine as more
	 * records may be generated before the daemon rotates the file.
	 */
	if (audit_fstat.af_filesz != 0 &&
	    audit_size >= audit_fstat.af_filesz * (audit_file_rotate_wait + 1)) {
		AUDIT_WORKER_LOCK_ASSERT();

		audit_file_rotate_wait++;
		(void)audit_send_trigger(AUDIT_TRIGGER_ROTATE_KERNEL);
	}

	/*
	 * If the estimated amount of audit data in the audit event queue
	 * (plus records allocated but not yet queued) has reached the amount
	 * of free space on the disk, then we need to go into an audit fail
	 * stop state, in which we do not permit the allocation/committing of
	 * any new audit records.  We continue to process records but don't
	 * allow any activities that might generate new records.  In the
	 * future, we might want to detect when space is available again and
	 * allow operation to continue, but this behavior is sufficient to
	 * meet fail stop requirements in CAPP.
	 */
	if (audit_fail_stop) {
		if ((unsigned long)((audit_q_len + audit_pre_q_len + 1) *
		    MAX_AUDIT_RECORD_SIZE) / mnt_stat->f_bsize >=
		    (unsigned long)(mnt_stat->f_bfree)) {
			if (ppsratecheck(&last_fail, &cur_fail, 1))
				printf("audit_record_write: free space "
				    "below size of audit queue, failing "
				    "stop\n");
			audit_in_failure = 1;
		} else if (audit_in_failure) {
			/*
			 * Note: if we want to handle recovery, this is the
			 * spot to do it: unset audit_in_failure, and issue a
			 * wakeup on the cv.
			 */
		}
	}

	error = vn_rdwr(UIO_WRITE, vp, data, len, (off_t)0, UIO_SYSSPACE,
	    IO_APPEND|IO_UNIT, cred, NULL, NULL, curthread);
	if (error == ENOSPC)
		goto fail_enospc;
	else if (error)
		goto fail;
	AUDIT_WORKER_LOCK_ASSERT();
	audit_size += len;

	/*
	 * Catch completion of a queue drain here; if we're draining and the
	 * queue is now empty, fail stop.  That audit_fail_stop is implicitly
	 * true, since audit_in_failure can only be set of audit_fail_stop is
	 * set.
	 *
	 * Note: if we handle recovery from audit_in_failure, then we need to
	 * make panic here conditional.
	 */
	if (audit_in_failure) {
		if (audit_q_len == 0 && audit_pre_q_len == 0) {
			audit_worker_sync_vp(vp, mp,
			    "Audit store overflow; record queue drained.");
		}
	}

	vfs_unbusy(mp);
	return;

fail_enospc:
	/*
	 * ENOSPC is considered a special case with respect to failures, as
	 * this can reflect either our preemptive detection of insufficient
	 * space, or ENOSPC returned by the vnode write call.
	 */
	if (audit_fail_stop) {
		audit_worker_sync_vp(vp, mp,
		    "Audit log space exhausted and fail-stop set.");
	}
	(void)audit_send_trigger(AUDIT_TRIGGER_NO_SPACE);
	audit_trail_suspended = 1;
	audit_syscalls_enabled_update();

	/* FALLTHROUGH */
fail:
	/*
	 * We have failed to write to the file, so the current record is
	 * lost, which may require an immediate system halt.
	 */
	if (audit_panic_on_write_fail) {
		audit_worker_sync_vp(vp, mp,
		    "audit_worker: write error %d\n", error);
	} else if (ppsratecheck(&last_fail, &cur_fail, 1))
		printf("audit_worker: write error %d\n", error);
	if (mp != NULL)
		vfs_unbusy(mp);
}

/*
 * Given a kernel audit record, process as required.  Kernel audit records
 * are converted to one, or possibly two, BSM records, depending on whether
 * there is a user audit record present also.  Kernel records need be
 * converted to BSM before they can be written out.  Both types will be
 * written to disk, and audit pipes.
 */
static void
audit_worker_process_record(struct kaudit_record *ar)
{
	struct au_record *bsm;
	au_class_t class;
	au_event_t event;
	au_id_t auid;
	int error, sorf;
	int locked;

	/*
	 * We hold the audit worker lock over both writes, if there are two,
	 * so that the two records won't be split across a rotation and end
	 * up in two different trail files.
	 */
	if (((ar->k_ar_commit & AR_COMMIT_USER) &&
	    (ar->k_ar_commit & AR_PRESELECT_USER_TRAIL)) ||
	    (ar->k_ar_commit & AR_PRESELECT_TRAIL)) {
		AUDIT_WORKER_LOCK();
		locked = 1;
	} else
		locked = 0;

	/*
	 * First, handle the user record, if any: commit to the system trail
	 * and audit pipes as selected.
	 */
	if ((ar->k_ar_commit & AR_COMMIT_USER) &&
	    (ar->k_ar_commit & AR_PRESELECT_USER_TRAIL)) {
		AUDIT_WORKER_LOCK_ASSERT();
		audit_record_write(audit_vp, audit_cred, ar->k_udata,
		    ar->k_ulen);
	}

	if ((ar->k_ar_commit & AR_COMMIT_USER) &&
	    (ar->k_ar_commit & AR_PRESELECT_USER_PIPE))
		audit_pipe_submit_user(ar->k_udata, ar->k_ulen);

	if (!(ar->k_ar_commit & AR_COMMIT_KERNEL) ||
	    ((ar->k_ar_commit & AR_PRESELECT_PIPE) == 0 &&
	    (ar->k_ar_commit & AR_PRESELECT_TRAIL) == 0 &&
	    (ar->k_ar_commit & AR_PRESELECT_DTRACE) == 0))
		goto out;

	auid = ar->k_ar.ar_subj_auid;
	event = ar->k_ar.ar_event;
	class = au_event_class(event);
	if (ar->k_ar.ar_errno == 0)
		sorf = AU_PRS_SUCCESS;
	else
		sorf = AU_PRS_FAILURE;

	error = kaudit_to_bsm(ar, &bsm);
	switch (error) {
	case BSM_NOAUDIT:
		goto out;

	case BSM_FAILURE:
		printf("audit_worker_process_record: BSM_FAILURE\n");
		goto out;

	case BSM_SUCCESS:
		break;

	default:
		panic("kaudit_to_bsm returned %d", error);
	}

	if (ar->k_ar_commit & AR_PRESELECT_TRAIL) {
		AUDIT_WORKER_LOCK_ASSERT();
		audit_record_write(audit_vp, audit_cred, bsm->data, bsm->len);
	}

	if (ar->k_ar_commit & AR_PRESELECT_PIPE)
		audit_pipe_submit(auid, event, class, sorf,
		    ar->k_ar_commit & AR_PRESELECT_TRAIL, bsm->data,
		    bsm->len);

#ifdef KDTRACE_HOOKS
	/*
	 * Version of the dtaudit commit hook that accepts BSM.
	 */
	if (ar->k_ar_commit & AR_PRESELECT_DTRACE) {
		if (dtaudit_hook_bsm != NULL)
			dtaudit_hook_bsm(ar, auid, event, class, sorf,
			    bsm->data, bsm->len);
	}
#endif

	kau_free(bsm);
out:
	if (locked)
		AUDIT_WORKER_UNLOCK();
}

/*
 * The audit_worker thread is responsible for watching the event queue,
 * dequeueing records, converting them to BSM format, and committing them to
 * disk.  In order to minimize lock thrashing, records are dequeued in sets
 * to a thread-local work queue.
 *
 * Note: this means that the effect bound on the size of the pending record
 * queue is 2x the length of the global queue.
 */
static void
audit_worker(void *arg)
{
	struct kaudit_queue ar_worklist;
	struct kaudit_record *ar;
	int lowater_signal;

	TAILQ_INIT(&ar_worklist);
	mtx_lock(&audit_mtx);
	while (1) {
		mtx_assert(&audit_mtx, MA_OWNED);

		/*
		 * Wait for a record.
		 */
		while (TAILQ_EMPTY(&audit_q))
			cv_wait(&audit_worker_cv, &audit_mtx);

		/*
		 * If there are records in the global audit record queue,
		 * transfer them to a thread-local queue and process them
		 * one by one.  If we cross the low watermark threshold,
		 * signal any waiting processes that they may wake up and
		 * continue generating records.
		 */
		lowater_signal = 0;
		while ((ar = TAILQ_FIRST(&audit_q))) {
			TAILQ_REMOVE(&audit_q, ar, k_q);
			audit_q_len--;
			if (audit_q_len == audit_qctrl.aq_lowater)
				lowater_signal++;
			TAILQ_INSERT_TAIL(&ar_worklist, ar, k_q);
		}
		if (lowater_signal)
			cv_broadcast(&audit_watermark_cv);

		mtx_unlock(&audit_mtx);
		while ((ar = TAILQ_FIRST(&ar_worklist))) {
			TAILQ_REMOVE(&ar_worklist, ar, k_q);
			audit_worker_process_record(ar);
			audit_free(ar);
		}
		mtx_lock(&audit_mtx);
	}
}

/*
 * audit_rotate_vnode() is called by a user or kernel thread to configure or
 * de-configure auditing on a vnode.  The arguments are the replacement
 * credential (referenced) and vnode (referenced and opened) to substitute
 * for the current credential and vnode, if any.  If either is set to NULL,
 * both should be NULL, and this is used to indicate that audit is being
 * disabled.  Any previous cred/vnode will be closed and freed.  We re-enable
 * generating rotation requests to auditd.
 */
void
audit_rotate_vnode(struct ucred *cred, struct vnode *vp)
{
	struct ucred *old_audit_cred;
	struct vnode *old_audit_vp;
	struct vattr vattr;

	KASSERT((cred != NULL && vp != NULL) || (cred == NULL && vp == NULL),
	    ("audit_rotate_vnode: cred %p vp %p", cred, vp));

	if (vp != NULL) {
		vn_lock(vp, LK_SHARED | LK_RETRY);
		if (VOP_GETATTR(vp, &vattr, cred) != 0)
			vattr.va_size = 0;
		VOP_UNLOCK(vp, 0);
	} else {
		vattr.va_size = 0;
	}

	/*
	 * Rotate the vnode/cred, and clear the rotate flag so that we will
	 * send a rotate trigger if the new file fills.
	 */
	AUDIT_WORKER_LOCK();
	old_audit_cred = audit_cred;
	old_audit_vp = audit_vp;
	audit_cred = cred;
	audit_vp = vp;
	audit_size = vattr.va_size;
	audit_file_rotate_wait = 0;
	audit_trail_enabled = (audit_vp != NULL);
	audit_syscalls_enabled_update();
	AUDIT_WORKER_UNLOCK();

	/*
	 * If there was an old vnode/credential, close and free.
	 */
	if (old_audit_vp != NULL) {
		vn_close(old_audit_vp, AUDIT_CLOSE_FLAGS, old_audit_cred,
		    curthread);
		crfree(old_audit_cred);
	}
}

void
audit_worker_init(void)
{
	int error;

	AUDIT_WORKER_LOCK_INIT();
	error = kproc_create(audit_worker, NULL, &audit_thread, RFHIGHPID,
	    0, "audit");
	if (error)
		panic("audit_worker_init: kproc_create returned %d", error);
}
