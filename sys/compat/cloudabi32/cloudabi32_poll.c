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
#include <sys/proc.h>
#include <sys/syscallsubr.h>

#include <contrib/cloudabi/cloudabi32_types.h>

#include <compat/cloudabi/cloudabi_util.h>

#include <compat/cloudabi32/cloudabi32_proto.h>
#include <compat/cloudabi32/cloudabi32_util.h>

/* Converts a FreeBSD signal number to a CloudABI signal number. */
static cloudabi_signal_t
convert_signal(int sig)
{
	static const cloudabi_signal_t signals[] = {
		[SIGABRT]	= CLOUDABI_SIGABRT,
		[SIGALRM]	= CLOUDABI_SIGALRM,
		[SIGBUS]	= CLOUDABI_SIGBUS,
		[SIGCHLD]	= CLOUDABI_SIGCHLD,
		[SIGCONT]	= CLOUDABI_SIGCONT,
		[SIGFPE]	= CLOUDABI_SIGFPE,
		[SIGHUP]	= CLOUDABI_SIGHUP,
		[SIGILL]	= CLOUDABI_SIGILL,
		[SIGINT]	= CLOUDABI_SIGINT,
		[SIGKILL]	= CLOUDABI_SIGKILL,
		[SIGPIPE]	= CLOUDABI_SIGPIPE,
		[SIGQUIT]	= CLOUDABI_SIGQUIT,
		[SIGSEGV]	= CLOUDABI_SIGSEGV,
		[SIGSTOP]	= CLOUDABI_SIGSTOP,
		[SIGSYS]	= CLOUDABI_SIGSYS,
		[SIGTERM]	= CLOUDABI_SIGTERM,
		[SIGTRAP]	= CLOUDABI_SIGTRAP,
		[SIGTSTP]	= CLOUDABI_SIGTSTP,
		[SIGTTIN]	= CLOUDABI_SIGTTIN,
		[SIGTTOU]	= CLOUDABI_SIGTTOU,
		[SIGURG]	= CLOUDABI_SIGURG,
		[SIGUSR1]	= CLOUDABI_SIGUSR1,
		[SIGUSR2]	= CLOUDABI_SIGUSR2,
		[SIGVTALRM]	= CLOUDABI_SIGVTALRM,
		[SIGXCPU]	= CLOUDABI_SIGXCPU,
		[SIGXFSZ]	= CLOUDABI_SIGXFSZ,
	};

	/* Convert unknown signals to SIGABRT. */
	if (sig < 0 || sig >= nitems(signals) || signals[sig] == 0)
		return (SIGABRT);
	return (signals[sig]);
}

struct cloudabi32_kevent_args {
	const cloudabi32_subscription_t *in;
	cloudabi_event_t *out;
};

/* Converts CloudABI's subscription objects to FreeBSD's struct kevent. */
static int
cloudabi32_kevent_copyin(void *arg, struct kevent *kevp, int count)
{
	cloudabi32_subscription_t sub;
	struct cloudabi32_kevent_args *args;
	cloudabi_timestamp_t ts;
	int error;

	args = arg;
	while (count-- > 0) {
		/* TODO(ed): Copy in multiple entries at once. */
		error = copyin(args->in++, &sub, sizeof(sub));
		if (error != 0)
			return (error);

		memset(kevp, 0, sizeof(*kevp));
		kevp->udata = TO_PTR(sub.userdata);
		switch (sub.type) {
		case CLOUDABI_EVENTTYPE_CLOCK:
			kevp->filter = EVFILT_TIMER;
			kevp->ident = sub.clock.identifier;
			kevp->fflags = NOTE_NSECONDS;
			if ((sub.clock.flags &
			    CLOUDABI_SUBSCRIPTION_CLOCK_ABSTIME) != 0 &&
			    sub.clock.timeout > 0) {
				/* Convert absolute timestamp to a relative. */
				error = cloudabi_clock_time_get(curthread,
				    sub.clock.clock_id, &ts);
				if (error != 0)
					return (error);
				ts = ts > sub.clock.timeout ? 0 :
				    sub.clock.timeout - ts;
			} else {
				/* Relative timestamp. */
				ts = sub.clock.timeout;
			}
			kevp->data = ts > INTPTR_MAX ? INTPTR_MAX : ts;
			break;
		case CLOUDABI_EVENTTYPE_FD_READ:
			kevp->filter = EVFILT_READ;
			kevp->ident = sub.fd_readwrite.fd;
			kevp->fflags = NOTE_FILE_POLL;
			break;
		case CLOUDABI_EVENTTYPE_FD_WRITE:
			kevp->filter = EVFILT_WRITE;
			kevp->ident = sub.fd_readwrite.fd;
			break;
		case CLOUDABI_EVENTTYPE_PROC_TERMINATE:
			kevp->filter = EVFILT_PROCDESC;
			kevp->ident = sub.proc_terminate.fd;
			kevp->fflags = NOTE_EXIT;
			break;
		}
		kevp->flags = EV_ADD | EV_ONESHOT;
		++kevp;
	}
	return (0);
}

/* Converts FreeBSD's struct kevent to CloudABI's event objects. */
static int
cloudabi32_kevent_copyout(void *arg, struct kevent *kevp, int count)
{
	cloudabi_event_t ev;
	struct cloudabi32_kevent_args *args;
	int error;

	args = arg;
	while (count-- > 0) {
		/* Convert fields that should always be present. */
		memset(&ev, 0, sizeof(ev));
		ev.userdata = (uintptr_t)kevp->udata;
		switch (kevp->filter) {
		case EVFILT_TIMER:
			ev.type = CLOUDABI_EVENTTYPE_CLOCK;
			break;
		case EVFILT_READ:
			ev.type = CLOUDABI_EVENTTYPE_FD_READ;
			break;
		case EVFILT_WRITE:
			ev.type = CLOUDABI_EVENTTYPE_FD_WRITE;
			break;
		case EVFILT_PROCDESC:
			ev.type = CLOUDABI_EVENTTYPE_PROC_TERMINATE;
			break;
		}

		if ((kevp->flags & EV_ERROR) == 0) {
			/* Success. */
			switch (kevp->filter) {
			case EVFILT_READ:
			case EVFILT_WRITE:
				ev.fd_readwrite.nbytes = kevp->data;
				if ((kevp->flags & EV_EOF) != 0) {
					ev.fd_readwrite.flags |=
					    CLOUDABI_EVENT_FD_READWRITE_HANGUP;
				}
				break;
			case EVFILT_PROCDESC:
				if (WIFSIGNALED(kevp->data)) {
					/* Process got signalled. */
					ev.proc_terminate.signal =
					   convert_signal(WTERMSIG(kevp->data));
					ev.proc_terminate.exitcode = 0;
				} else {
					/* Process exited. */
					ev.proc_terminate.signal = 0;
					ev.proc_terminate.exitcode =
					    WEXITSTATUS(kevp->data);
				}
				break;
			}
		} else {
			/* Error. */
			ev.error = cloudabi_convert_errno(kevp->data);
		}
		++kevp;

		/* TODO(ed): Copy out multiple entries at once. */
		error = copyout(&ev, args->out++, sizeof(ev));
		if (error != 0)
			return (error);
	}
	return (0);
}

int
cloudabi32_sys_poll(struct thread *td, struct cloudabi32_sys_poll_args *uap)
{
	struct cloudabi32_kevent_args args = {
		.in	= uap->in,
		.out	= uap->out,
	};
	struct kevent_copyops copyops = {
		.k_copyin	= cloudabi32_kevent_copyin,
		.k_copyout	= cloudabi32_kevent_copyout,
		.arg		= &args,
	};

	/*
	 * Bandaid to support CloudABI futex constructs that are not
	 * implemented through FreeBSD's kqueue().
	 */
	if (uap->nsubscriptions == 1) {
		cloudabi32_subscription_t sub;
		cloudabi_event_t ev = {};
		int error;

		error = copyin(uap->in, &sub, sizeof(sub));
		if (error != 0)
			return (error);
		ev.userdata = sub.userdata;
		ev.type = sub.type;
		if (sub.type == CLOUDABI_EVENTTYPE_CONDVAR) {
			/* Wait on a condition variable. */
			ev.error = cloudabi_convert_errno(
			    cloudabi_futex_condvar_wait(
			        td, TO_PTR(sub.condvar.condvar),
			        sub.condvar.condvar_scope,
			        TO_PTR(sub.condvar.lock),
			        sub.condvar.lock_scope,
			        CLOUDABI_CLOCK_MONOTONIC, UINT64_MAX, 0, true));
			td->td_retval[0] = 1;
			return (copyout(&ev, uap->out, sizeof(ev)));
		} else if (sub.type == CLOUDABI_EVENTTYPE_LOCK_RDLOCK) {
			/* Acquire a read lock. */
			ev.error = cloudabi_convert_errno(
			    cloudabi_futex_lock_rdlock(
			        td, TO_PTR(sub.lock.lock),
			        sub.lock.lock_scope, CLOUDABI_CLOCK_MONOTONIC,
			        UINT64_MAX, 0, true));
			td->td_retval[0] = 1;
			return (copyout(&ev, uap->out, sizeof(ev)));
		} else if (sub.type == CLOUDABI_EVENTTYPE_LOCK_WRLOCK) {
			/* Acquire a write lock. */
			ev.error = cloudabi_convert_errno(
			    cloudabi_futex_lock_wrlock(
			        td, TO_PTR(sub.lock.lock),
			        sub.lock.lock_scope, CLOUDABI_CLOCK_MONOTONIC,
			        UINT64_MAX, 0, true));
			td->td_retval[0] = 1;
			return (copyout(&ev, uap->out, sizeof(ev)));
		}
	} else if (uap->nsubscriptions == 2) {
		cloudabi32_subscription_t sub[2];
		cloudabi_event_t ev[2] = {};
		int error;

		error = copyin(uap->in, &sub, sizeof(sub));
		if (error != 0)
			return (error);
		ev[0].userdata = sub[0].userdata;
		ev[0].type = sub[0].type;
		ev[1].userdata = sub[1].userdata;
		ev[1].type = sub[1].type;
		if (sub[0].type == CLOUDABI_EVENTTYPE_CONDVAR &&
		    sub[1].type == CLOUDABI_EVENTTYPE_CLOCK) {
			/* Wait for a condition variable with timeout. */
			error = cloudabi_futex_condvar_wait(
			    td, TO_PTR(sub[0].condvar.condvar),
			    sub[0].condvar.condvar_scope,
			    TO_PTR(sub[0].condvar.lock),
			    sub[0].condvar.lock_scope, sub[1].clock.clock_id,
			    sub[1].clock.timeout, sub[1].clock.precision,
			    (sub[1].clock.flags &
			    CLOUDABI_SUBSCRIPTION_CLOCK_ABSTIME) != 0);
			if (error == ETIMEDOUT) {
				td->td_retval[0] = 1;
				return (copyout(&ev[1], uap->out,
				    sizeof(ev[1])));
			}

			ev[0].error = cloudabi_convert_errno(error);
			td->td_retval[0] = 1;
			return (copyout(&ev[0], uap->out, sizeof(ev[0])));
		} else if (sub[0].type == CLOUDABI_EVENTTYPE_LOCK_RDLOCK &&
		    sub[1].type == CLOUDABI_EVENTTYPE_CLOCK) {
			/* Acquire a read lock with a timeout. */
			error = cloudabi_futex_lock_rdlock(
			    td, TO_PTR(sub[0].lock.lock),
			    sub[0].lock.lock_scope, sub[1].clock.clock_id,
			    sub[1].clock.timeout, sub[1].clock.precision,
			    (sub[1].clock.flags &
			    CLOUDABI_SUBSCRIPTION_CLOCK_ABSTIME) != 0);
			if (error == ETIMEDOUT) {
				td->td_retval[0] = 1;
				return (copyout(&ev[1], uap->out,
				    sizeof(ev[1])));
			}

			ev[0].error = cloudabi_convert_errno(error);
			td->td_retval[0] = 1;
			return (copyout(&ev[0], uap->out, sizeof(ev[0])));
		} else if (sub[0].type == CLOUDABI_EVENTTYPE_LOCK_WRLOCK &&
		    sub[1].type == CLOUDABI_EVENTTYPE_CLOCK) {
			/* Acquire a write lock with a timeout. */
			error = cloudabi_futex_lock_wrlock(
			    td, TO_PTR(sub[0].lock.lock),
			    sub[0].lock.lock_scope, sub[1].clock.clock_id,
			    sub[1].clock.timeout, sub[1].clock.precision,
			    (sub[1].clock.flags &
			    CLOUDABI_SUBSCRIPTION_CLOCK_ABSTIME) != 0);
			if (error == ETIMEDOUT) {
				td->td_retval[0] = 1;
				return (copyout(&ev[1], uap->out,
				    sizeof(ev[1])));
			}

			ev[0].error = cloudabi_convert_errno(error);
			td->td_retval[0] = 1;
			return (copyout(&ev[0], uap->out, sizeof(ev[0])));
		}
	}

	return (kern_kevent_anonymous(td, uap->nsubscriptions, &copyops));
}
