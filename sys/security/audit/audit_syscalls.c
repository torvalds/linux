/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999-2009 Apple Inc.
 * Copyright (c) 2016, 2018 Robert N. M. Watson
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
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/jail.h>

#include <bsm/audit.h>
#include <bsm/audit_kevents.h>

#include <security/audit/audit.h>
#include <security/audit/audit_private.h>
#include <security/mac/mac_framework.h>

#ifdef AUDIT

/*
 * System call to allow a user space application to submit a BSM audit record
 * to the kernel for inclusion in the audit log.  This function does little
 * verification on the audit record that is submitted.
 *
 * XXXAUDIT: Audit preselection for user records does not currently work,
 * since we pre-select only based on the AUE_audit event type, not the event
 * type submitted as part of the user audit data.
 */
/* ARGSUSED */
int
sys_audit(struct thread *td, struct audit_args *uap)
{
	int error;
	void * rec;
	struct kaudit_record *ar;

	if (jailed(td->td_ucred))
		return (ENOSYS);
	error = priv_check(td, PRIV_AUDIT_SUBMIT);
	if (error)
		return (error);

	if ((uap->length <= 0) || (uap->length > audit_qctrl.aq_bufsz))
		return (EINVAL);

	ar = currecord();

	/*
	 * If there's no current audit record (audit() itself not audited)
	 * commit the user audit record.
	 */
	if (ar == NULL) {

		/*
		 * This is not very efficient; we're required to allocate a
		 * complete kernel audit record just so the user record can
		 * tag along.
		 *
		 * XXXAUDIT: Maybe AUE_AUDIT in the system call context and
		 * special pre-select handling?
		 */
		td->td_ar = audit_new(AUE_NULL, td);
		if (td->td_ar == NULL)
			return (ENOTSUP);
		td->td_pflags |= TDP_AUDITREC;
		ar = td->td_ar;
	}

	if (uap->length > MAX_AUDIT_RECORD_SIZE)
		return (EINVAL);

	rec = malloc(uap->length, M_AUDITDATA, M_WAITOK);

	error = copyin(uap->record, rec, uap->length);
	if (error)
		goto free_out;

	/* Verify the record. */
	if (bsm_rec_verify(rec) == 0) {
		error = EINVAL;
		goto free_out;
	}

#ifdef MAC
	error = mac_system_check_audit(td->td_ucred, rec, uap->length);
	if (error)
		goto free_out;
#endif

	/*
	 * Attach the user audit record to the kernel audit record.  Because
	 * this system call is an auditable event, we will write the user
	 * record along with the record for this audit event.
	 *
	 * XXXAUDIT: KASSERT appropriate starting values of k_udata, k_ulen,
	 * k_ar_commit & AR_COMMIT_USER?
	 */
	ar->k_udata = rec;
	ar->k_ulen  = uap->length;
	ar->k_ar_commit |= AR_COMMIT_USER;

	/*
	 * Currently we assume that all preselection has been performed in
	 * userspace.  We unconditionally set these masks so that the records
	 * get committed both to the trail and pipe.  In the future we will
	 * want to setup kernel based preselection.
	 */
	ar->k_ar_commit |= (AR_PRESELECT_USER_TRAIL | AR_PRESELECT_USER_PIPE);
	return (0);

free_out:
	/*
	 * audit_syscall_exit() will free the audit record on the thread even
	 * if we allocated it above.
	 */
	free(rec, M_AUDITDATA);
	return (error);
}

/*
 *  System call to manipulate auditing.
 */
/* ARGSUSED */
int
sys_auditon(struct thread *td, struct auditon_args *uap)
{
	struct ucred *cred, *newcred, *oldcred;
	int error;
	union auditon_udata udata;
	struct proc *tp;

	if (jailed(td->td_ucred))
		return (ENOSYS);
	AUDIT_ARG_CMD(uap->cmd);

#ifdef MAC
	error = mac_system_check_auditon(td->td_ucred, uap->cmd);
	if (error)
		return (error);
#endif

	error = priv_check(td, PRIV_AUDIT_CONTROL);
	if (error)
		return (error);

	if ((uap->length <= 0) || (uap->length > sizeof(union auditon_udata)))
		return (EINVAL);

	memset((void *)&udata, 0, sizeof(udata));

	/*
	 * Some of the GET commands use the arguments too.
	 */
	switch (uap->cmd) {
	case A_SETPOLICY:
	case A_OLDSETPOLICY:
	case A_SETKMASK:
	case A_SETQCTRL:
	case A_OLDSETQCTRL:
	case A_SETSTAT:
	case A_SETUMASK:
	case A_SETSMASK:
	case A_SETCOND:
	case A_OLDSETCOND:
	case A_SETCLASS:
	case A_SETEVENT:
	case A_SETPMASK:
	case A_SETFSIZE:
	case A_SETKAUDIT:
	case A_GETCLASS:
	case A_GETEVENT:
	case A_GETPINFO:
	case A_GETPINFO_ADDR:
	case A_SENDTRIGGER:
		error = copyin(uap->data, (void *)&udata, uap->length);
		if (error)
			return (error);
		AUDIT_ARG_AUDITON(&udata);
		break;
	}

	/*
	 * XXXAUDIT: Locking?
	 */
	switch (uap->cmd) {
	case A_OLDGETPOLICY:
	case A_GETPOLICY:
		if (uap->length == sizeof(udata.au_policy64)) {
			if (!audit_fail_stop)
				udata.au_policy64 |= AUDIT_CNT;
			if (audit_panic_on_write_fail)
				udata.au_policy64 |= AUDIT_AHLT;
			if (audit_argv)
				udata.au_policy64 |= AUDIT_ARGV;
			if (audit_arge)
				udata.au_policy64 |= AUDIT_ARGE;
			break;
		}
		if (uap->length != sizeof(udata.au_policy))
			return (EINVAL);
		if (!audit_fail_stop)
			udata.au_policy |= AUDIT_CNT;
		if (audit_panic_on_write_fail)
			udata.au_policy |= AUDIT_AHLT;
		if (audit_argv)
			udata.au_policy |= AUDIT_ARGV;
		if (audit_arge)
			udata.au_policy |= AUDIT_ARGE;
		break;

	case A_OLDSETPOLICY:
	case A_SETPOLICY:
		if (uap->length == sizeof(udata.au_policy64)) {
			if (udata.au_policy & ~(AUDIT_CNT|AUDIT_AHLT|
			    AUDIT_ARGV|AUDIT_ARGE))
				return (EINVAL);
			audit_fail_stop = ((udata.au_policy64 & AUDIT_CNT) ==
			    0);
			audit_panic_on_write_fail = (udata.au_policy64 &
			    AUDIT_AHLT);
			audit_argv = (udata.au_policy64 & AUDIT_ARGV);
			audit_arge = (udata.au_policy64 & AUDIT_ARGE);
			break;
		}
		if (uap->length != sizeof(udata.au_policy))
			return (EINVAL);
		if (udata.au_policy & ~(AUDIT_CNT|AUDIT_AHLT|AUDIT_ARGV|
		    AUDIT_ARGE))
			return (EINVAL);
		/*
		 * XXX - Need to wake up waiters if the policy relaxes?
		 */
		audit_fail_stop = ((udata.au_policy & AUDIT_CNT) == 0);
		audit_panic_on_write_fail = (udata.au_policy & AUDIT_AHLT);
		audit_argv = (udata.au_policy & AUDIT_ARGV);
		audit_arge = (udata.au_policy & AUDIT_ARGE);
		break;

	case A_GETKMASK:
		if (uap->length != sizeof(udata.au_mask))
			return (EINVAL);
		udata.au_mask = audit_nae_mask;
		break;

	case A_SETKMASK:
		if (uap->length != sizeof(udata.au_mask))
			return (EINVAL);
		audit_nae_mask = udata.au_mask;
		break;

	case A_OLDGETQCTRL:
	case A_GETQCTRL:
		if (uap->length == sizeof(udata.au_qctrl64)) {
			udata.au_qctrl64.aq64_hiwater =
			    (u_int64_t)audit_qctrl.aq_hiwater;
			udata.au_qctrl64.aq64_lowater =
			    (u_int64_t)audit_qctrl.aq_lowater;
			udata.au_qctrl64.aq64_bufsz =
			    (u_int64_t)audit_qctrl.aq_bufsz;
			udata.au_qctrl64.aq64_minfree =
			    (u_int64_t)audit_qctrl.aq_minfree;
			break;
		}
		if (uap->length != sizeof(udata.au_qctrl))
			return (EINVAL);
		udata.au_qctrl = audit_qctrl;
		break;

	case A_OLDSETQCTRL:
	case A_SETQCTRL:
		if (uap->length == sizeof(udata.au_qctrl64)) {
			/* NB: aq64_minfree is unsigned unlike aq_minfree. */
			if ((udata.au_qctrl64.aq64_hiwater > AQ_MAXHIGH) ||
			    (udata.au_qctrl64.aq64_lowater >=
			    udata.au_qctrl.aq_hiwater) ||
			    (udata.au_qctrl64.aq64_bufsz > AQ_MAXBUFSZ) ||
			    (udata.au_qctrl64.aq64_minfree > 100))
				return (EINVAL);
			audit_qctrl.aq_hiwater =
			    (int)udata.au_qctrl64.aq64_hiwater;
			audit_qctrl.aq_lowater =
			    (int)udata.au_qctrl64.aq64_lowater;
			audit_qctrl.aq_bufsz =
			    (int)udata.au_qctrl64.aq64_bufsz;
			audit_qctrl.aq_minfree =
			    (int)udata.au_qctrl64.aq64_minfree;
			audit_qctrl.aq_delay = -1;	/* Not used. */
			break;
		}
		if (uap->length != sizeof(udata.au_qctrl))
			return (EINVAL);
		if ((udata.au_qctrl.aq_hiwater > AQ_MAXHIGH) ||
		    (udata.au_qctrl.aq_lowater >= udata.au_qctrl.aq_hiwater) ||
		    (udata.au_qctrl.aq_bufsz > AQ_MAXBUFSZ) ||
		    (udata.au_qctrl.aq_minfree < 0) ||
		    (udata.au_qctrl.aq_minfree > 100))
			return (EINVAL);

		audit_qctrl = udata.au_qctrl;
		/* XXX The queue delay value isn't used with the kernel. */
		audit_qctrl.aq_delay = -1;
		break;

	case A_GETCWD:
		return (ENOSYS);
		break;

	case A_GETCAR:
		return (ENOSYS);
		break;

	case A_GETSTAT:
		return (ENOSYS);
		break;

	case A_SETSTAT:
		return (ENOSYS);
		break;

	case A_SETUMASK:
		return (ENOSYS);
		break;

	case A_SETSMASK:
		return (ENOSYS);
		break;

	case A_OLDGETCOND:
	case A_GETCOND:
		if (uap->length == sizeof(udata.au_cond64)) {
			if (audit_trail_enabled && !audit_trail_suspended)
				udata.au_cond64 = AUC_AUDITING;
			else
				udata.au_cond64 = AUC_NOAUDIT;
			break;
		}
		if (uap->length != sizeof(udata.au_cond))
			return (EINVAL);
		if (audit_trail_enabled && !audit_trail_suspended)
			udata.au_cond = AUC_AUDITING;
		else
			udata.au_cond = AUC_NOAUDIT;
		break;

	case A_OLDSETCOND:
	case A_SETCOND:
		if (uap->length == sizeof(udata.au_cond64)) {
			if (udata.au_cond64 == AUC_NOAUDIT)
				audit_trail_suspended = 1;
			if (udata.au_cond64 == AUC_AUDITING)
				audit_trail_suspended = 0;
			if (udata.au_cond64 == AUC_DISABLED) {
				audit_trail_suspended = 1;
				audit_shutdown(NULL, 0);
			}
			audit_syscalls_enabled_update();
			break;
		}
		if (uap->length != sizeof(udata.au_cond))
			return (EINVAL);
		if (udata.au_cond == AUC_NOAUDIT)
			audit_trail_suspended = 1;
		if (udata.au_cond == AUC_AUDITING)
			audit_trail_suspended = 0;
		if (udata.au_cond == AUC_DISABLED) {
			audit_trail_suspended = 1;
			audit_shutdown(NULL, 0);
		}
		audit_syscalls_enabled_update();
		break;

	case A_GETCLASS:
		if (uap->length != sizeof(udata.au_evclass))
			return (EINVAL);
		udata.au_evclass.ec_class = au_event_class(
		    udata.au_evclass.ec_number);
		break;

	case A_GETEVENT:
		if (uap->length != sizeof(udata.au_evname))
			return (EINVAL);
		error = au_event_name(udata.au_evname.en_number,
		    udata.au_evname.en_name);
		if (error != 0)
			return (error);
		break;

	case A_SETCLASS:
		if (uap->length != sizeof(udata.au_evclass))
			return (EINVAL);
		au_evclassmap_insert(udata.au_evclass.ec_number,
		    udata.au_evclass.ec_class);
		break;

	case A_SETEVENT:
		if (uap->length != sizeof(udata.au_evname))
			return (EINVAL);

		/* Ensure nul termination from userspace. */
		udata.au_evname.en_name[sizeof(udata.au_evname.en_name) - 1]
		    = 0;
		au_evnamemap_insert(udata.au_evname.en_number,
		    udata.au_evname.en_name);
		break;

	case A_GETPINFO:
		if (uap->length != sizeof(udata.au_aupinfo))
			return (EINVAL);
		if (udata.au_aupinfo.ap_pid < 1)
			return (ESRCH);
		if ((tp = pfind(udata.au_aupinfo.ap_pid)) == NULL)
			return (ESRCH);
		if ((error = p_cansee(td, tp)) != 0) {
			PROC_UNLOCK(tp);
			return (error);
		}
		cred = tp->p_ucred;
		if (cred->cr_audit.ai_termid.at_type == AU_IPv6) {
			PROC_UNLOCK(tp);
			return (EINVAL);
		}
		udata.au_aupinfo.ap_auid = cred->cr_audit.ai_auid;
		udata.au_aupinfo.ap_mask.am_success =
		    cred->cr_audit.ai_mask.am_success;
		udata.au_aupinfo.ap_mask.am_failure =
		    cred->cr_audit.ai_mask.am_failure;
		udata.au_aupinfo.ap_termid.machine =
		    cred->cr_audit.ai_termid.at_addr[0];
		udata.au_aupinfo.ap_termid.port =
		    (dev_t)cred->cr_audit.ai_termid.at_port;
		udata.au_aupinfo.ap_asid = cred->cr_audit.ai_asid;
		PROC_UNLOCK(tp);
		break;

	case A_SETPMASK:
		if (uap->length != sizeof(udata.au_aupinfo))
			return (EINVAL);
		if (udata.au_aupinfo.ap_pid < 1)
			return (ESRCH);
		newcred = crget();
		if ((tp = pfind(udata.au_aupinfo.ap_pid)) == NULL) {
			crfree(newcred);
			return (ESRCH);
		}
		if ((error = p_cansee(td, tp)) != 0) {
			PROC_UNLOCK(tp);
			crfree(newcred);
			return (error);
		}
		oldcred = tp->p_ucred;
		crcopy(newcred, oldcred);
		newcred->cr_audit.ai_mask.am_success =
		    udata.au_aupinfo.ap_mask.am_success;
		newcred->cr_audit.ai_mask.am_failure =
		    udata.au_aupinfo.ap_mask.am_failure;
		proc_set_cred(tp, newcred);
		PROC_UNLOCK(tp);
		crfree(oldcred);
		break;

	case A_SETFSIZE:
		if (uap->length != sizeof(udata.au_fstat))
			return (EINVAL);
		if ((udata.au_fstat.af_filesz != 0) &&
		   (udata.au_fstat.af_filesz < MIN_AUDIT_FILE_SIZE))
			return (EINVAL);
		audit_fstat.af_filesz = udata.au_fstat.af_filesz;
		break;

	case A_GETFSIZE:
		if (uap->length != sizeof(udata.au_fstat))
			return (EINVAL);
		udata.au_fstat.af_filesz = audit_fstat.af_filesz;
		udata.au_fstat.af_currsz = audit_fstat.af_currsz;
		break;

	case A_GETPINFO_ADDR:
		if (uap->length != sizeof(udata.au_aupinfo_addr))
			return (EINVAL);
		if (udata.au_aupinfo_addr.ap_pid < 1)
			return (ESRCH);
		if ((tp = pfind(udata.au_aupinfo_addr.ap_pid)) == NULL)
			return (ESRCH);
		cred = tp->p_ucred;
		udata.au_aupinfo_addr.ap_auid = cred->cr_audit.ai_auid;
		udata.au_aupinfo_addr.ap_mask.am_success =
		    cred->cr_audit.ai_mask.am_success;
		udata.au_aupinfo_addr.ap_mask.am_failure =
		    cred->cr_audit.ai_mask.am_failure;
		udata.au_aupinfo_addr.ap_termid = cred->cr_audit.ai_termid;
		udata.au_aupinfo_addr.ap_asid = cred->cr_audit.ai_asid;
		PROC_UNLOCK(tp);
		break;

	case A_GETKAUDIT:
		if (uap->length != sizeof(udata.au_kau_info))
			return (EINVAL);
		audit_get_kinfo(&udata.au_kau_info);
		break;

	case A_SETKAUDIT:
		if (uap->length != sizeof(udata.au_kau_info))
			return (EINVAL);
		if (udata.au_kau_info.ai_termid.at_type != AU_IPv4 &&
		    udata.au_kau_info.ai_termid.at_type != AU_IPv6)
			return (EINVAL);
		audit_set_kinfo(&udata.au_kau_info);
		break;

	case A_SENDTRIGGER:
		if (uap->length != sizeof(udata.au_trigger))
			return (EINVAL);
		if ((udata.au_trigger < AUDIT_TRIGGER_MIN) ||
		    (udata.au_trigger > AUDIT_TRIGGER_MAX))
			return (EINVAL);
		return (audit_send_trigger(udata.au_trigger));

	default:
		return (EINVAL);
	}

	/*
	 * Copy data back to userspace for the GET comands.
	 */
	switch (uap->cmd) {
	case A_GETPOLICY:
	case A_OLDGETPOLICY:
	case A_GETKMASK:
	case A_GETQCTRL:
	case A_OLDGETQCTRL:
	case A_GETCWD:
	case A_GETCAR:
	case A_GETSTAT:
	case A_GETCOND:
	case A_OLDGETCOND:
	case A_GETCLASS:
	case A_GETPINFO:
	case A_GETFSIZE:
	case A_GETPINFO_ADDR:
	case A_GETKAUDIT:
		error = copyout((void *)&udata, uap->data, uap->length);
		if (error)
			return (error);
		break;
	}

	return (0);
}

/*
 * System calls to manage the user audit information.
 */
/* ARGSUSED */
int
sys_getauid(struct thread *td, struct getauid_args *uap)
{
	int error;

	if (jailed(td->td_ucred))
		return (ENOSYS);
	error = priv_check(td, PRIV_AUDIT_GETAUDIT);
	if (error)
		return (error);
	return (copyout(&td->td_ucred->cr_audit.ai_auid, uap->auid,
	    sizeof(td->td_ucred->cr_audit.ai_auid)));
}

/* ARGSUSED */
int
sys_setauid(struct thread *td, struct setauid_args *uap)
{
	struct ucred *newcred, *oldcred;
	au_id_t id;
	int error;

	if (jailed(td->td_ucred))
		return (ENOSYS);
	error = copyin(uap->auid, &id, sizeof(id));
	if (error)
		return (error);
	audit_arg_auid(id);
	newcred = crget();
	PROC_LOCK(td->td_proc);
	oldcred = td->td_proc->p_ucred;
	crcopy(newcred, oldcred);
#ifdef MAC
	error = mac_cred_check_setauid(oldcred, id);
	if (error)
		goto fail;
#endif
	error = priv_check_cred(oldcred, PRIV_AUDIT_SETAUDIT);
	if (error)
		goto fail;
	newcred->cr_audit.ai_auid = id;
	proc_set_cred(td->td_proc, newcred);
	PROC_UNLOCK(td->td_proc);
	crfree(oldcred);
	return (0);
fail:
	PROC_UNLOCK(td->td_proc);
	crfree(newcred);
	return (error);
}

/*
 * System calls to get and set process audit information.
 */
/* ARGSUSED */
int
sys_getaudit(struct thread *td, struct getaudit_args *uap)
{
	struct auditinfo ai;
	struct ucred *cred;
	int error;

	cred = td->td_ucred;
	if (jailed(cred))
		return (ENOSYS);
	error = priv_check(td, PRIV_AUDIT_GETAUDIT);
	if (error)
		return (error);
	if (cred->cr_audit.ai_termid.at_type == AU_IPv6)
		return (E2BIG);
	bzero(&ai, sizeof(ai));
	ai.ai_auid = cred->cr_audit.ai_auid;
	ai.ai_mask = cred->cr_audit.ai_mask;
	ai.ai_asid = cred->cr_audit.ai_asid;
	ai.ai_termid.machine = cred->cr_audit.ai_termid.at_addr[0];
	ai.ai_termid.port = cred->cr_audit.ai_termid.at_port;
	return (copyout(&ai, uap->auditinfo, sizeof(ai)));
}

/* ARGSUSED */
int
sys_setaudit(struct thread *td, struct setaudit_args *uap)
{
	struct ucred *newcred, *oldcred;
	struct auditinfo ai;
	int error;

	if (jailed(td->td_ucred))
		return (ENOSYS);
	error = copyin(uap->auditinfo, &ai, sizeof(ai));
	if (error)
		return (error);
	audit_arg_auditinfo(&ai);
	newcred = crget();
	PROC_LOCK(td->td_proc);
	oldcred = td->td_proc->p_ucred;
	crcopy(newcred, oldcred);
#ifdef MAC
	error = mac_cred_check_setaudit(oldcred, &ai);
	if (error)
		goto fail;
#endif
	error = priv_check_cred(oldcred, PRIV_AUDIT_SETAUDIT);
	if (error)
		goto fail;
	bzero(&newcred->cr_audit, sizeof(newcred->cr_audit));
	newcred->cr_audit.ai_auid = ai.ai_auid;
	newcred->cr_audit.ai_mask = ai.ai_mask;
	newcred->cr_audit.ai_asid = ai.ai_asid;
	newcred->cr_audit.ai_termid.at_addr[0] = ai.ai_termid.machine;
	newcred->cr_audit.ai_termid.at_port = ai.ai_termid.port;
	newcred->cr_audit.ai_termid.at_type = AU_IPv4;
	proc_set_cred(td->td_proc, newcred);
	PROC_UNLOCK(td->td_proc);
	crfree(oldcred);
	return (0);
fail:
	PROC_UNLOCK(td->td_proc);
	crfree(newcred);
	return (error);
}

/* ARGSUSED */
int
sys_getaudit_addr(struct thread *td, struct getaudit_addr_args *uap)
{
	int error;

	if (jailed(td->td_ucred))
		return (ENOSYS);
	if (uap->length < sizeof(*uap->auditinfo_addr))
		return (EOVERFLOW);
	error = priv_check(td, PRIV_AUDIT_GETAUDIT);
	if (error)
		return (error);
	return (copyout(&td->td_ucred->cr_audit, uap->auditinfo_addr,
	    sizeof(*uap->auditinfo_addr)));
}

/* ARGSUSED */
int
sys_setaudit_addr(struct thread *td, struct setaudit_addr_args *uap)
{
	struct ucred *newcred, *oldcred;
	struct auditinfo_addr aia;
	int error;

	if (jailed(td->td_ucred))
		return (ENOSYS);
	error = copyin(uap->auditinfo_addr, &aia, sizeof(aia));
	if (error)
		return (error);
	audit_arg_auditinfo_addr(&aia);
	if (aia.ai_termid.at_type != AU_IPv6 &&
	    aia.ai_termid.at_type != AU_IPv4)
		return (EINVAL);
	newcred = crget();
	PROC_LOCK(td->td_proc);	
	oldcred = td->td_proc->p_ucred;
	crcopy(newcred, oldcred);
#ifdef MAC
	error = mac_cred_check_setaudit_addr(oldcred, &aia);
	if (error)
		goto fail;
#endif
	error = priv_check_cred(oldcred, PRIV_AUDIT_SETAUDIT);
	if (error)
		goto fail;
	newcred->cr_audit = aia;
	proc_set_cred(td->td_proc, newcred);
	PROC_UNLOCK(td->td_proc);
	crfree(oldcred);
	return (0);
fail:
	PROC_UNLOCK(td->td_proc);
	crfree(newcred);
	return (error);
}

/*
 * Syscall to manage audit files.
 */
/* ARGSUSED */
int
sys_auditctl(struct thread *td, struct auditctl_args *uap)
{
	struct nameidata nd;
	struct ucred *cred;
	struct vnode *vp;
	int error = 0;
	int flags;

	if (jailed(td->td_ucred))
		return (ENOSYS);
	error = priv_check(td, PRIV_AUDIT_CONTROL);
	if (error)
		return (error);

	vp = NULL;
	cred = NULL;

	/*
	 * If a path is specified, open the replacement vnode, perform
	 * validity checks, and grab another reference to the current
	 * credential.
	 *
	 * On Darwin, a NULL path argument is also used to disable audit.
	 */
	if (uap->path == NULL)
		return (EINVAL);

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | AUDITVNODE1,
	    UIO_USERSPACE, uap->path, td);
	flags = AUDIT_OPEN_FLAGS;
	error = vn_open(&nd, &flags, 0, NULL);
	if (error)
		return (error);
	vp = nd.ni_vp;
#ifdef MAC
	error = mac_system_check_auditctl(td->td_ucred, vp);
	VOP_UNLOCK(vp, 0);
	if (error) {
		vn_close(vp, AUDIT_CLOSE_FLAGS, td->td_ucred, td);
		return (error);
	}
#else
	VOP_UNLOCK(vp, 0);
#endif
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (vp->v_type != VREG) {
		vn_close(vp, AUDIT_CLOSE_FLAGS, td->td_ucred, td);
		return (EINVAL);
	}
	cred = td->td_ucred;
	crhold(cred);

	/*
	 * XXXAUDIT: Should audit_trail_suspended actually be cleared by
	 * audit_worker?
	 */
	audit_trail_suspended = 0;
	audit_syscalls_enabled_update();

	audit_rotate_vnode(cred, vp);

	return (error);
}

#else /* !AUDIT */

int
sys_audit(struct thread *td, struct audit_args *uap)
{

	return (ENOSYS);
}

int
sys_auditon(struct thread *td, struct auditon_args *uap)
{

	return (ENOSYS);
}

int
sys_getauid(struct thread *td, struct getauid_args *uap)
{

	return (ENOSYS);
}

int
sys_setauid(struct thread *td, struct setauid_args *uap)
{

	return (ENOSYS);
}

int
sys_getaudit(struct thread *td, struct getaudit_args *uap)
{

	return (ENOSYS);
}

int
sys_setaudit(struct thread *td, struct setaudit_args *uap)
{

	return (ENOSYS);
}

int
sys_getaudit_addr(struct thread *td, struct getaudit_addr_args *uap)
{

	return (ENOSYS);
}

int
sys_setaudit_addr(struct thread *td, struct setaudit_addr_args *uap)
{

	return (ENOSYS);
}

int
sys_auditctl(struct thread *td, struct auditctl_args *uap)
{

	return (ENOSYS);
}
#endif /* AUDIT */
