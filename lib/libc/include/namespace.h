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

#ifndef _NAMESPACE_H_
#define _NAMESPACE_H_

/*
 * Adjust names so that headers declare "hidden" names.
 *
 * README: When modifying this file don't forget to make the appropriate
 *         changes in un-namespace.h!!!
 */

/*
 * ISO C (C90) section.  Most names in libc aren't in ISO C, so they
 * should be here.  Most aren't here...
 */
#define		err				_err
#define		warn				_warn
#define		nsdispatch			_nsdispatch

/*
 * Prototypes for syscalls/functions that need to be overridden
 * in libc_r/libpthread.
 */
#define		accept				_accept
#define		__acl_aclcheck_fd		___acl_aclcheck_fd
#define		__acl_delete_fd			___acl_delete_fd
#define		__acl_get_fd			___acl_get_fd
#define		__acl_set_fd			___acl_set_fd
#define		bind				_bind
#define		__cap_get_fd			___cap_get_fd
#define		__cap_set_fd			___cap_set_fd
#define		clock_nanosleep			_clock_nanosleep
#define		close				_close
#define		connect				_connect
#define		dup				_dup
#define		dup2				_dup2
#define		execve				_execve
#define		fcntl				_fcntl
/*#define		flock				_flock */
#define		flockfile			_flockfile
#define		fpathconf			_fpathconf
#define		fstat				_fstat
#define		fstatfs				_fstatfs
#define		fsync				_fsync
#define		funlockfile			_funlockfile
#define		getdirentries			_getdirentries
#define		getlogin			_getlogin
#define		getpeername			_getpeername
#define		getprogname			_getprogname
#define		getsockname			_getsockname
#define		getsockopt			_getsockopt
#define		ioctl				_ioctl
/* #define		kevent				_kevent */
#define		listen				_listen
#define		nanosleep			_nanosleep
#define		open				_open
#define		openat				_openat
#define		poll				_poll
#define		pthread_atfork			_pthread_atfork
#define		pthread_attr_destroy		_pthread_attr_destroy
#define		pthread_attr_get_np		_pthread_attr_get_np
#define		pthread_attr_getaffinity_np	_pthread_attr_getaffinity_np
#define		pthread_attr_getdetachstate	_pthread_attr_getdetachstate
#define		pthread_attr_getguardsize	_pthread_attr_getguardsize
#define		pthread_attr_getinheritsched	_pthread_attr_getinheritsched
#define		pthread_attr_getschedparam	_pthread_attr_getschedparam
#define		pthread_attr_getschedpolicy	_pthread_attr_getschedpolicy
#define		pthread_attr_getscope		_pthread_attr_getscope
#define		pthread_attr_getstack		_pthread_attr_getstack
#define		pthread_attr_getstackaddr	_pthread_attr_getstackaddr
#define		pthread_attr_getstacksize	_pthread_attr_getstacksize
#define		pthread_attr_init		_pthread_attr_init
#define		pthread_attr_setaffinity_np	_pthread_attr_setaffinity_np
#define		pthread_attr_setcreatesuspend_np _pthread_attr_setcreatesuspend_np
#define		pthread_attr_setdetachstate	_pthread_attr_setdetachstate
#define		pthread_attr_setguardsize	_pthread_attr_setguardsize
#define		pthread_attr_setinheritsched	_pthread_attr_setinheritsched
#define		pthread_attr_setschedparam	_pthread_attr_setschedparam
#define		pthread_attr_setschedpolicy	_pthread_attr_setschedpolicy
#define		pthread_attr_setscope		_pthread_attr_setscope
#define		pthread_attr_setstack		_pthread_attr_setstack
#define		pthread_attr_setstackaddr	_pthread_attr_setstackaddr
#define		pthread_attr_setstacksize	_pthread_attr_setstacksize
#define		pthread_barrier_destroy		_pthread_barrier_destroy
#define		pthread_barrier_init		_pthread_barrier_init
#define		pthread_barrier_wait		_pthread_barrier_wait
#define		pthread_barrierattr_destroy	_pthread_barrierattr_destroy
#define		pthread_barrierattr_getpshared	_pthread_barrierattr_getpshared
#define		pthread_barrierattr_init	_pthread_barrierattr_init
#define		pthread_barrierattr_setpshared	_pthread_barrierattr_setpshared
#define		pthread_cancel			_pthread_cancel
#define		pthread_cond_broadcast		_pthread_cond_broadcast
#define		pthread_cond_destroy		_pthread_cond_destroy
#define		pthread_cond_init		_pthread_cond_init
#define		pthread_cond_signal		_pthread_cond_signal
#define		pthread_cond_timedwait		_pthread_cond_timedwait
#define		pthread_cond_wait		_pthread_cond_wait
#define		pthread_condattr_destroy	_pthread_condattr_destroy
#define		pthread_condattr_getclock	_pthread_condattr_getclock
#define		pthread_condattr_getpshared	_pthread_condattr_getpshared
#define		pthread_condattr_init		_pthread_condattr_init
#define		pthread_condattr_setclock	_pthread_condattr_setclock
#define		pthread_condattr_setpshared	_pthread_condattr_setpshared
#define		pthread_create			_pthread_create
#define		pthread_detach			_pthread_detach
#define		pthread_equal			_pthread_equal
#define		pthread_exit			_pthread_exit
#define		pthread_get_name_np		_pthread_get_name_np
#define		pthread_getaffinity_np		_pthread_getaffinity_np
#define		pthread_getconcurrency		_pthread_getconcurrency
#define		pthread_getcpuclockid		_pthread_getcpuclockid
#define		pthread_getprio			_pthread_getprio
#define		pthread_getschedparam		_pthread_getschedparam
#define		pthread_getspecific		_pthread_getspecific
#define		pthread_getthreadid_np		_pthread_getthreadid_np
#define		pthread_join			_pthread_join
#define		pthread_key_create		_pthread_key_create
#define		pthread_key_delete		_pthread_key_delete
#define		pthread_kill			_pthread_kill
#define		pthread_main_np			_pthread_main_np
#define		pthread_multi_np		_pthread_multi_np
#define		pthread_mutex_destroy		_pthread_mutex_destroy
#define		pthread_mutex_getprioceiling	_pthread_mutex_getprioceiling
#define		pthread_mutex_init		_pthread_mutex_init
#define		pthread_mutex_isowned_np	_pthread_mutex_isowned_np
#define		pthread_mutex_lock		_pthread_mutex_lock
#define		pthread_mutex_setprioceiling	_pthread_mutex_setprioceiling
#define		pthread_mutex_timedlock		_pthread_mutex_timedlock
#define		pthread_mutex_trylock		_pthread_mutex_trylock
#define		pthread_mutex_unlock		_pthread_mutex_unlock
#define		pthread_mutexattr_destroy	_pthread_mutexattr_destroy
#define		pthread_mutexattr_getkind_np	_pthread_mutexattr_getkind_np
#define		pthread_mutexattr_getprioceiling _pthread_mutexattr_getprioceiling
#define		pthread_mutexattr_getprotocol	_pthread_mutexattr_getprotocol
#define		pthread_mutexattr_getpshared	_pthread_mutexattr_getpshared
#define		pthread_mutexattr_gettype	_pthread_mutexattr_gettype
#define		pthread_mutexattr_init		_pthread_mutexattr_init
#define		pthread_mutexattr_setkind_np	_pthread_mutexattr_setkind_np
#define		pthread_mutexattr_setprioceiling _pthread_mutexattr_setprioceiling
#define		pthread_mutexattr_setprotocol	_pthread_mutexattr_setprotocol
#define		pthread_mutexattr_setpshared	_pthread_mutexattr_setpshared
#define		pthread_mutexattr_settype	_pthread_mutexattr_settype
#define		pthread_once			_pthread_once
#define		pthread_resume_all_np		_pthread_resume_all_np
#define		pthread_resume_np		_pthread_resume_np
#define		pthread_rwlock_destroy		_pthread_rwlock_destroy
#define		pthread_rwlock_init		_pthread_rwlock_init
#define		pthread_rwlock_rdlock		_pthread_rwlock_rdlock
#define		pthread_rwlock_timedrdlock	_pthread_rwlock_timedrdlock
#define		pthread_rwlock_timedwrlock	_pthread_rwlock_timedwrlock
#define		pthread_rwlock_tryrdlock	_pthread_rwlock_tryrdlock
#define		pthread_rwlock_trywrlock	_pthread_rwlock_trywrlock
#define		pthread_rwlock_unlock		_pthread_rwlock_unlock
#define		pthread_rwlock_wrlock		_pthread_rwlock_wrlock
#define		pthread_rwlockattr_destroy	_pthread_rwlockattr_destroy
#define		pthread_rwlockattr_getpshared	_pthread_rwlockattr_getpshared
#define		pthread_rwlockattr_init		_pthread_rwlockattr_init
#define		pthread_rwlockattr_setpshared	_pthread_rwlockattr_setpshared
#define		pthread_self			_pthread_self
#define		pthread_set_name_np		_pthread_set_name_np
#define		pthread_setaffinity_np		_pthread_setaffinity_np
#define		pthread_setcancelstate		_pthread_setcancelstate
#define		pthread_setcanceltype		_pthread_setcanceltype
#define		pthread_setconcurrency		_pthread_setconcurrency
#define		pthread_setprio			_pthread_setprio
#define		pthread_setschedparam		_pthread_setschedparam
#define		pthread_setspecific		_pthread_setspecific
#define		pthread_sigmask			_pthread_sigmask
#define		pthread_single_np		_pthread_single_np
#define		pthread_spin_destroy		_pthread_spin_destroy
#define		pthread_spin_init		_pthread_spin_init
#define		pthread_spin_lock		_pthread_spin_lock
#define		pthread_spin_trylock		_pthread_spin_trylock
#define		pthread_spin_unlock		_pthread_spin_unlock
#define		pthread_suspend_all_np		_pthread_suspend_all_np
#define		pthread_suspend_np		_pthread_suspend_np
#define		pthread_switch_add_np		_pthread_switch_add_np
#define		pthread_switch_delete_np	_pthread_switch_delete_np
#define		pthread_testcancel		_pthread_testcancel
#define		pthread_timedjoin_np		_pthread_timedjoin_np
#define		pthread_yield			_pthread_yield
#define		read				_read
#define		readv				_readv
#define		recvfrom			_recvfrom
#define		recvmsg				_recvmsg
#define		recvmmsg			_recvmmsg
#define		select				_select
#define		sem_close			_sem_close
#define		sem_destroy			_sem_destroy
#define		sem_getvalue			_sem_getvalue
#define		sem_init			_sem_init
#define		sem_open			_sem_open
#define		sem_post			_sem_post
#define		sem_timedwait			_sem_timedwait
#define		sem_clockwait_np		_sem_clockwait_np
#define		sem_trywait			_sem_trywait
#define		sem_unlink			_sem_unlink
#define		sem_wait			_sem_wait
#define		sendmsg				_sendmsg
#define		sendmmsg			_sendmmsg
#define		sendto				_sendto
#define		setsockopt			_setsockopt
/*#define		sigaction			_sigaction*/
#define		sigprocmask			_sigprocmask
#define		sigsuspend			_sigsuspend
#define		socket				_socket
#define		socketpair			_socketpair
#define		usleep				_usleep
#define		wait4				_wait4
#define		wait6				_wait6
#define		waitpid				_waitpid
#define		write				_write
#define		writev				_writev


/*
 * Other hidden syscalls/functions that libc_r needs to override
 * but are not used internally by libc.
 *
 * XXX - When modifying libc to use one of the following, remove
 * the prototype from below and place it in the list above.
 */
#if 0
#define		creat				_creat
#define		fchflags			_fchflags
#define		fchmod				_fchmod
#define		ftrylockfile			_ftrylockfile
#define		msync				_msync
#define		nfssvc				_nfssvc
#define		pause				_pause
#define		sched_yield			_sched_yield
#define		sendfile			_sendfile
#define		shutdown			_shutdown
#define		sigaltstack			_sigaltstack
#define		sigpending			_sigpending
#define		sigreturn			_sigreturn
#define		sigsetmask			_sigsetmask
#define		sleep				_sleep
#define		system				_system
#define		tcdrain				_tcdrain
#define		wait				_wait
#endif

#endif /* _NAMESPACE_H_ */
