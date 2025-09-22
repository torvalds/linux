/*	$OpenBSD: syscall.c,v 1.19 2024/01/11 19:16:27 miod Exp $	*/

/*
 * Copyright (c) 2020 Brian Bamsch <bbamsch@google.com>
 * Copyright (c) 2015 Dale Rahn <drahn@dalerahn.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/syscall_mi.h>

#include <uvm/uvm_extern.h>

#include <machine/syscall.h>

static __inline struct trapframe *
process_frame(struct proc *p)
{
	return p->p_addr->u_pcb.pcb_tf;
}

void
svc_handler(trapframe_t *frame)
{
	struct proc *p = curproc;
	const struct sysent *callp = sysent;
	int code, error = ENOSYS;
	register_t *args, rval[2];

	uvmexp.syscalls++;

	code = frame->tf_t[0];
	if (code <= 0 || code >= SYS_MAXSYSCALL)
		goto bad;

	callp += code;

	args = &frame->tf_a[0];

	rval[0] = 0;
	rval[1] = 0;

	error = mi_syscall(p, code, callp, args, rval);

	switch (error) {
	case 0:
		frame->tf_a[0] = rval[0];
		frame->tf_t[0] = 0;		/* syscall succeeded */
		break;
	case ERESTART:
		frame->tf_sepc -= 4;		/* prev instruction */
		break;
	case EJUSTRETURN:
		break;
	default:
	bad:
		frame->tf_a[0] = error;
		frame->tf_t[0] = 1;		/* syscall error */
		break;
	}

	mi_syscall_return(p, code, error, rval);
}

void
child_return(void *arg)
{
	struct proc *p = arg;
	struct trapframe *frame = process_frame(p);

	frame->tf_a[0] = 0;
	frame->tf_t[0] = 0;			/* no error */

	KERNEL_UNLOCK();

	mi_child_return(p);
}
