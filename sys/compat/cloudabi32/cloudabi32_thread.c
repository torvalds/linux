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
#include <sys/systm.h>

#include <contrib/cloudabi/cloudabi32_types.h>

#include <compat/cloudabi32/cloudabi32_proto.h>
#include <compat/cloudabi32/cloudabi32_util.h>

struct thread_create_args {
	cloudabi32_threadattr_t attr;
	uint32_t tcb;
	lwpid_t tid;
};

static int
initialize_thread(struct thread *td, void *thunk)
{
	struct thread_create_args *args = thunk;

	/* Save the thread ID, so it can be returned. */
	args->tid = td->td_tid;

	/* Set up initial register contents. */
	return (cloudabi32_thread_setregs(td, &args->attr, args->tcb));
}

int
cloudabi32_sys_thread_create(struct thread *td,
    struct cloudabi32_sys_thread_create_args *uap)
{
	struct thread_create_args args;
	int error;

	error = copyin(uap->attr, &args.attr, sizeof(args.attr));
	if (error != 0)
		return (error);

	/* Remove some space on the top of the stack for the TCB. */
	args.tcb = rounddown(args.attr.stack + args.attr.stack_len -
	    sizeof(cloudabi32_tcb_t), _Alignof(cloudabi32_tcb_t));
	args.attr.stack_len = args.tcb - args.attr.stack;

	error = thread_create(td, NULL, initialize_thread, &args);
	if (error != 0)
		return (error);
	td->td_retval[0] = args.tid;
	return (0);
}
