/*
 * Copyright (c) 2014 The FreeBSD Foundation.
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include "libc_private.h"

#define	SLOT(a, b) \
	[INTERPOS_##a] = (interpos_func_t)b
interpos_func_t __libc_interposing[INTERPOS_MAX] = {
	SLOT(accept, __sys_accept),
	SLOT(accept4, __sys_accept4),
	SLOT(aio_suspend, __sys_aio_suspend),
	SLOT(close, __sys_close),
	SLOT(connect, __sys_connect),
	SLOT(fcntl, __sys_fcntl),
	SLOT(fsync, __sys_fsync),
	SLOT(fork, __sys_fork),
	SLOT(msync, __sys_msync),
	SLOT(nanosleep, __sys_nanosleep),
	SLOT(openat, __sys_openat),
	SLOT(poll, __sys_poll),
	SLOT(pselect, __sys_pselect),
	SLOT(read, __sys_read),
	SLOT(readv, __sys_readv),
	SLOT(recvfrom, __sys_recvfrom),
	SLOT(recvmsg, __sys_recvmsg),
	SLOT(select, __sys_select),
	SLOT(sendmsg, __sys_sendmsg),
	SLOT(sendto, __sys_sendto),
	SLOT(setcontext, __sys_setcontext),
	SLOT(sigaction, __sys_sigaction),
	SLOT(sigprocmask, __sys_sigprocmask),
	SLOT(sigsuspend, __sys_sigsuspend),
	SLOT(sigwait, __libc_sigwait),
	SLOT(sigtimedwait, __sys_sigtimedwait),
	SLOT(sigwaitinfo, __sys_sigwaitinfo),
	SLOT(swapcontext, __sys_swapcontext),
	SLOT(system, __libc_system),
	SLOT(tcdrain, __libc_tcdrain),
	SLOT(wait4, __sys_wait4),
	SLOT(write, __sys_write),
	SLOT(writev, __sys_writev),
	SLOT(_pthread_mutex_init_calloc_cb, _pthread_mutex_init_calloc_cb_stub),
	SLOT(spinlock, __libc_spinlock_stub),
	SLOT(spinunlock, __libc_spinunlock_stub),
	SLOT(kevent, __sys_kevent),
	SLOT(wait6, __sys_wait6),
	SLOT(ppoll, __sys_ppoll),
	SLOT(map_stacks_exec, __libc_map_stacks_exec),
	SLOT(fdatasync, __sys_fdatasync),
	SLOT(clock_nanosleep, __sys_clock_nanosleep),
};
#undef SLOT

interpos_func_t *
__libc_interposing_slot(int interposno)
{

	return (&__libc_interposing[interposno]);
}
