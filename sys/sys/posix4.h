#ifndef _P1003_1B_P1003_1B_H_
#define _P1003_1B_P1003_1B_H_
/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996, 1997, 1998
 *	HD Associates, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by HD Associates, Inc
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY HD ASSOCIATES AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL HD ASSOCIATES OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/sched.h>

/* Generate syscall stubs for when something is optionally
 * loadable as a module.  References "syscall_not_present". 
 * XXX Good candidate for sys/syscall.h
 */
struct proc;
struct nosys_args;
extern int syscall_not_present(struct thread *, const char *, struct nosys_args *);

#define SYSCALL_NOT_PRESENT_GEN(SC) \
int sys_ ## SC (struct thread *td, struct SC##_args *uap) \
{ \
	return syscall_not_present(td, #SC , (struct nosys_args *)uap); \
}


MALLOC_DECLARE(M_P31B);

int p31b_proc(struct proc *, pid_t, struct proc **);

void p31b_setcfg(int, int);
int p31b_getcfg(int);
int p31b_iscfg(int);
void p31b_unsetcfg(int);

#ifdef _KPOSIX_PRIORITY_SCHEDULING

/* 
 * KSCHED_OP_RW is a vector of read/write flags for each entry indexed
 * by the enum ksched_op.
 *
 * 1 means you need write access, 0 means read is sufficient.
 */

enum ksched_op {

#define KSCHED_OP_RW { 1, 0, 1, 0, 0, 0, 0, 0 }

	SCHED_SETPARAM,
	SCHED_GETPARAM,
	SCHED_SETSCHEDULER,
	SCHED_GETSCHEDULER,
	SCHED_YIELD,
	SCHED_GET_PRIORITY_MAX,
	SCHED_GET_PRIORITY_MIN,
	SCHED_RR_GET_INTERVAL,
	SCHED_OP_MAX
};

struct ksched;

int ksched_attach(struct ksched **);
int ksched_detach(struct ksched *);

int ksched_setparam(struct ksched *,
	struct thread *, const struct sched_param *);
int ksched_getparam(struct ksched *,
	struct thread *, struct sched_param *);

int ksched_setscheduler(struct ksched *,
	struct thread *, int, const struct sched_param *);
int ksched_getscheduler(struct ksched *, struct thread *, int *);

int ksched_yield(struct ksched *);

int ksched_get_priority_max(struct ksched *, int, int *);
int ksched_get_priority_min(struct ksched *, int, int *);

int ksched_rr_get_interval(struct ksched *,
	struct thread *, struct timespec *);

#endif /* _KPOSIX_PRIORITY_SCHEDULING */

#endif /* _P1003_1B_P1003_1B_H_ */
