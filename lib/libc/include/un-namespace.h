/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Daniel Eischen <deischen@FreeBSD.org>.
 * All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#ifndef _UN_NAMESPACE_H_
#define _UN_NAMESPACE_H_

#undef		accept
#undef		__acl_aclcheck_fd
#undef		__acl_delete_fd
#undef		__acl_get_fd
#undef		__acl_set_fd
#undef		bind
#undef		__cap_get_fd
#undef		__cap_set_fd
#undef		clock_nanosleep
#undef		close
#undef		connect
#undef		dup
#undef		dup2
#undef		execve
#undef		fcntl
#undef		flock
#undef		flockfile
#undef		fpathconf
#undef		fstat
#undef		fstatfs
#undef		fsync
#undef		funlockfile
#undef		getdirentries
#undef		getlogin
#undef		getpeername
#undef		getprogname
#undef		getsockname
#undef		getsockopt
#undef		ioctl
#undef		kevent
#undef		listen
#undef		nanosleep
#undef		open
#undef		openat
#undef		poll
#undef		pthread_atfork
#undef		pthread_attr_destroy
#undef		pthread_attr_get_np
#undef		pthread_attr_getaffinity_np
#undef		pthread_attr_getdetachstate
#undef		pthread_attr_getguardsize
#undef		pthread_attr_getinheritsched
#undef		pthread_attr_getschedparam
#undef		pthread_attr_getschedpolicy
#undef		pthread_attr_getscope
#undef		pthread_attr_getstack
#undef		pthread_attr_getstackaddr
#undef		pthread_attr_getstacksize
#undef		pthread_attr_init
#undef		pthread_attr_setaffinity_np
#undef		pthread_attr_setcreatesuspend_np
#undef		pthread_attr_setdetachstate
#undef		pthread_attr_setguardsize
#undef		pthread_attr_setinheritsched
#undef		pthread_attr_setschedparam
#undef		pthread_attr_setschedpolicy
#undef		pthread_attr_setscope
#undef		pthread_attr_setstack
#undef		pthread_attr_setstackaddr
#undef		pthread_attr_setstacksize
#undef		pthread_barrier_destroy
#undef		pthread_barrier_init
#undef		pthread_barrier_wait
#undef		pthread_barrierattr_destroy
#undef		pthread_barrierattr_getpshared
#undef		pthread_barrierattr_init
#undef		pthread_barrierattr_setpshared
#undef		pthread_cancel
#undef		pthread_cond_broadcast
#undef		pthread_cond_destroy
#undef		pthread_cond_init
#undef		pthread_cond_signal
#undef		pthread_cond_timedwait
#undef		pthread_cond_wait
#undef		pthread_condattr_destroy
#undef		pthread_condattr_getclock
#undef		pthread_condattr_getpshared
#undef		pthread_condattr_init
#undef		pthread_condattr_setclock
#undef		pthread_condattr_setpshared
#undef		pthread_create
#undef		pthread_detach
#undef		pthread_equal
#undef		pthread_exit
#undef		pthread_get_name_np
#undef		pthread_getaffinity_np
#undef		pthread_getconcurrency
#undef		pthread_getcpuclockid
#undef		pthread_getprio
#undef		pthread_getschedparam
#undef		pthread_getspecific
#undef		pthread_getthreadid_np
#undef		pthread_join
#undef		pthread_key_create
#undef		pthread_key_delete
#undef		pthread_kill
#undef		pthread_main_np
#undef		pthread_multi_np
#undef		pthread_mutex_destroy
#undef		pthread_mutex_getprioceiling
#undef		pthread_mutex_init
#undef		pthread_mutex_isowned_np
#undef		pthread_mutex_lock
#undef		pthread_mutex_setprioceiling
#undef		pthread_mutex_timedlock
#undef		pthread_mutex_trylock
#undef		pthread_mutex_unlock
#undef		pthread_mutexattr_destroy
#undef		pthread_mutexattr_getkind_np
#undef		pthread_mutexattr_getprioceiling
#undef		pthread_mutexattr_getprotocol
#undef		pthread_mutexattr_getpshared
#undef		pthread_mutexattr_gettype
#undef		pthread_mutexattr_init
#undef		pthread_mutexattr_setkind_np
#undef		pthread_mutexattr_setprioceiling
#undef		pthread_mutexattr_setprotocol
#undef		pthread_mutexattr_setpshared
#undef		pthread_mutexattr_settype
#undef		pthread_once
#undef		pthread_resume_all_np
#undef		pthread_resume_np
#undef		pthread_rwlock_destroy
#undef		pthread_rwlock_init
#undef		pthread_rwlock_rdlock
#undef		pthread_rwlock_timedrdlock
#undef		pthread_rwlock_timedwrlock
#undef		pthread_rwlock_tryrdlock
#undef		pthread_rwlock_trywrlock
#undef		pthread_rwlock_unlock
#undef		pthread_rwlock_wrlock
#undef		pthread_rwlockattr_destroy
#undef		pthread_rwlockattr_getpshared
#undef		pthread_rwlockattr_init
#undef		pthread_rwlockattr_setpshared
#undef		pthread_self
#undef		pthread_set_name_np
#undef		pthread_setaffinity_np
#undef		pthread_setcancelstate
#undef		pthread_setcanceltype
#undef		pthread_setconcurrency
#undef		pthread_setprio
#undef		pthread_setschedparam
#undef		pthread_setspecific
#undef		pthread_sigmask
#undef		pthread_single_np
#undef		pthread_spin_destroy
#undef		pthread_spin_init
#undef		pthread_spin_lock
#undef		pthread_spin_trylock
#undef		pthread_spin_unlock
#undef		pthread_suspend_all_np
#undef		pthread_suspend_np
#undef		pthread_switch_add_np
#undef		pthread_switch_delete_np
#undef		pthread_testcancel
#undef		pthread_timedjoin_np
#undef		pthread_yield
#undef		read
#undef		readv
#undef		recvfrom
#undef		recvmsg
#undef		recvmmsg
#undef		select
#undef		sem_close
#undef		sem_destroy
#undef		sem_getvalue
#undef		sem_init
#undef		sem_open
#undef		sem_post
#undef		sem_timedwait
#undef		sem_clockwait_np
#undef		sem_trywait
#undef		sem_unlink
#undef		sem_wait
#undef		sendmsg
#undef		sendmmsg
#undef		sendto
#undef		setsockopt
#undef		sigaction
#undef		sigprocmask
#undef		sigsuspend
#undef		socket
#undef		socketpair
#undef		usleep
#undef		wait4
#undef		wait6
#undef		waitpid
#undef		write
#undef		writev

#if 0
#undef		creat
#undef		fchflags
#undef		fchmod
#undef		ftrylockfile
#undef		msync
#undef		nfssvc
#undef		pause
#undef		sched_yield
#undef		sendfile
#undef		shutdown
#undef		sigaltstack
#undef		sigpending
#undef		sigreturn
#undef		sigsetmask
#undef		sleep
#undef		system
#undef		tcdrain
#undef		wait
#endif	/* 0 */

#ifdef _SIGNAL_H_
int     	_sigaction(int, const struct sigaction *, struct sigaction *);
#endif

#ifdef _SYS_EVENT_H_
int		_kevent(int, const struct kevent *, int, struct kevent *,
		    int, const struct timespec *);
#endif

#ifdef _SYS_FCNTL_H_
int		_flock(int, int);
#endif

#undef		err
#undef		warn
#undef		nsdispatch

#endif	/* _UN_NAMESPACE_H_ */
