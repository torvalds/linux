/* $OpenBSD: ast.c,v 1.8 2023/04/13 02:19:04 jsg Exp $ */
/*
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
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/signal.h>
#include <sys/syscall.h>
#include <sys/syscall_mi.h>
#include <machine/pcb.h>

#include <uvm/uvm_extern.h>

#include <machine/frame.h>

/*
 * Transform cpu ast to mi_ast
 */

void
ast(struct trapframe *tf)
{
	struct proc *p = curcpu()->ci_curproc;

	p->p_addr->u_pcb.pcb_tf = tf;

	refreshcreds(p);
	uvmexp.softs++;
	mi_ast(p, curcpu()->ci_want_resched);
	userret(p);
}
