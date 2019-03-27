/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef	_MACHINE_INTR_MACHDEP_H_
#define	_MACHINE_INTR_MACHDEP_H_

#define	IRSR_BUSY	(1 << 5)

#define	PIL_MAX		(1 << 4)
#define	IV_MAX		(1 << 11)

#define	IR_FREE		(PIL_MAX * 2)

#define	IH_SHIFT	PTR_SHIFT
#define	IQE_SHIFT	5
#define	IV_SHIFT	6

#define	PIL_LOW		1	/* stray interrupts */
#define	PIL_PREEMPT	2	/* preempt idle thread CPU IPI */
#define	PIL_ITHREAD	3	/* interrupts that use ithreads */
#define	PIL_RENDEZVOUS	4	/* SMP rendezvous IPI */
#define	PIL_AST		5	/* asynchronous trap IPI */
#define	PIL_HARDCLOCK	6	/* hardclock broadcast */
#define	PIL_FILTER	11	/* filter interrupts */
#define	PIL_BRIDGE	12	/* bridge interrupts */
#define	PIL_STOP	13	/* stop CPU IPI */
#define	PIL_TICK	14	/* tick interrupts */

#ifndef LOCORE

#define	INTR_BRIDGE	INTR_MD1

struct trapframe;

typedef	void ih_func_t(struct trapframe *);
typedef	void iv_func_t(void *);

struct intr_request {
	struct	intr_request *ir_next;
	iv_func_t *ir_func;
	void	*ir_arg;
	u_int	ir_vec;
	u_int	ir_pri;
};

struct intr_controller {
	void	(*ic_enable)(void *);
	void	(*ic_disable)(void *);
	void	(*ic_assign)(void *);
	void	(*ic_clear)(void *);
};

struct intr_vector {
	iv_func_t *iv_func;
	void	*iv_arg;
	const struct	intr_controller *iv_ic;
	void	*iv_icarg;
	struct	intr_event *iv_event;
	u_int	iv_pri;
	u_int	iv_vec;
	u_int	iv_mid;
	u_int	iv_refcnt;
	u_int	iv_pad[2];
};

extern ih_func_t *intr_handlers[];
extern struct intr_vector intr_vectors[];

#ifdef SMP
void	intr_add_cpu(u_int cpu);
#endif
int	intr_bind(int vec, u_char cpu);
int	intr_describe(int vec, void *ih, const char *descr);
void	intr_setup(int level, ih_func_t *ihf, int pri, iv_func_t *ivf,
	    void *iva);
void	intr_init1(void);
void	intr_init2(void);
int	intr_controller_register(int vec, const struct intr_controller *ic,
	    void *icarg);
int	inthand_add(const char *name, int vec, int (*filt)(void *),
	    void (*handler)(void *), void *arg, int flags, void **cookiep);
int	inthand_remove(int vec, void *cookie);

ih_func_t intr_fast;

#endif /* !LOCORE */

#endif /* !_MACHINE_INTR_MACHDEP_H_ */
