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
#include <sys/sched.h>
#include <sys/syscallsubr.h>
#include <sys/umtx.h>

#include <contrib/cloudabi/cloudabi_types_common.h>

#include <compat/cloudabi/cloudabi_proto.h>

int
cloudabi_sys_thread_exit(struct thread *td,
    struct cloudabi_sys_thread_exit_args *uap)
{
	struct cloudabi_sys_lock_unlock_args cloudabi_sys_lock_unlock_args = {
		.lock = uap->lock,
		.scope = uap->scope,
	};

	umtx_thread_exit(td);

        /* Wake up joining thread. */
	cloudabi_sys_lock_unlock(td, &cloudabi_sys_lock_unlock_args);

        /*
	 * Attempt to terminate the thread. Terminate the process if
	 * it's the last thread.
	 */
	kern_thr_exit(td);
	exit1(td, 0, 0);
	/* NOTREACHED */
}

int
cloudabi_sys_thread_yield(struct thread *td,
    struct cloudabi_sys_thread_yield_args *uap)
{

	sched_relinquish(td);
	return (0);
}
