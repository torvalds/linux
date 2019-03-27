/*-
 * Copyright (c) 2015 Nuxi, https://nuxi.nl/
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/filedesc.h>
#include <sys/imgact.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/syscallsubr.h>
#include <sys/unistd.h>

#include <contrib/cloudabi/cloudabi_types_common.h>

#include <compat/cloudabi/cloudabi_proto.h>

int
cloudabi_sys_proc_exec(struct thread *td,
    struct cloudabi_sys_proc_exec_args *uap)
{
	struct image_args args;
	struct vmspace *oldvmspace;
	int error;

	error = pre_execve(td, &oldvmspace);
	if (error != 0)
		return (error);
	error = exec_copyin_data_fds(td, &args, uap->data, uap->data_len,
	    uap->fds, uap->fds_len);
	if (error == 0) {
		args.fd = uap->fd;
		error = kern_execve(td, &args, NULL);
	}
	post_execve(td, error, oldvmspace);
	return (error);
}

int
cloudabi_sys_proc_exit(struct thread *td,
    struct cloudabi_sys_proc_exit_args *uap)
{

	exit1(td, uap->rval, 0);
	/* NOTREACHED */
}

int
cloudabi_sys_proc_fork(struct thread *td,
    struct cloudabi_sys_proc_fork_args *uap)
{
	struct fork_req fr;
	struct filecaps fcaps = {};
	int error, fd;

	cap_rights_init(&fcaps.fc_rights, CAP_FSTAT, CAP_EVENT);
	bzero(&fr, sizeof(fr));
	fr.fr_flags = RFFDG | RFPROC | RFPROCDESC;
	fr.fr_pd_fd = &fd;
	fr.fr_pd_fcaps = &fcaps;
	error = fork1(td, &fr);
	if (error != 0)
		return (error);
	/* Return the file descriptor to the parent process. */
	td->td_retval[0] = fd;
	return (0);
}

int
cloudabi_sys_proc_raise(struct thread *td,
    struct cloudabi_sys_proc_raise_args *uap)
{
	static const int signals[] = {
		[CLOUDABI_SIGABRT] = SIGABRT,
		[CLOUDABI_SIGALRM] = SIGALRM,
		[CLOUDABI_SIGBUS] = SIGBUS,
		[CLOUDABI_SIGCHLD] = SIGCHLD,
		[CLOUDABI_SIGCONT] = SIGCONT,
		[CLOUDABI_SIGFPE] = SIGFPE,
		[CLOUDABI_SIGHUP] = SIGHUP,
		[CLOUDABI_SIGILL] = SIGILL,
		[CLOUDABI_SIGINT] = SIGINT,
		[CLOUDABI_SIGKILL] = SIGKILL,
		[CLOUDABI_SIGPIPE] = SIGPIPE,
		[CLOUDABI_SIGQUIT] = SIGQUIT,
		[CLOUDABI_SIGSEGV] = SIGSEGV,
		[CLOUDABI_SIGSTOP] = SIGSTOP,
		[CLOUDABI_SIGSYS] = SIGSYS,
		[CLOUDABI_SIGTERM] = SIGTERM,
		[CLOUDABI_SIGTRAP] = SIGTRAP,
		[CLOUDABI_SIGTSTP] = SIGTSTP,
		[CLOUDABI_SIGTTIN] = SIGTTIN,
		[CLOUDABI_SIGTTOU] = SIGTTOU,
		[CLOUDABI_SIGURG] = SIGURG,
		[CLOUDABI_SIGUSR1] = SIGUSR1,
		[CLOUDABI_SIGUSR2] = SIGUSR2,
		[CLOUDABI_SIGVTALRM] = SIGVTALRM,
		[CLOUDABI_SIGXCPU] = SIGXCPU,
		[CLOUDABI_SIGXFSZ] = SIGXFSZ,
	};
	ksiginfo_t ksi;
	struct proc *p;

	if (uap->sig >= nitems(signals) || signals[uap->sig] == 0) {
		/* Invalid signal, or the null signal. */
		return (uap->sig == 0 ? 0 : EINVAL);
	}

	p = td->td_proc;
	ksiginfo_init(&ksi);
	ksi.ksi_signo = signals[uap->sig];
	ksi.ksi_code = SI_USER;
	ksi.ksi_pid = p->p_pid;
	ksi.ksi_uid = td->td_ucred->cr_ruid;
	PROC_LOCK(p);
	pksignal(p, ksi.ksi_signo, &ksi);
	PROC_UNLOCK(p);
	return (0);
}

MODULE_VERSION(cloudabi, 1);
