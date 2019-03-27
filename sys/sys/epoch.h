/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018, Matthew Macy <mmacy@freebsd.org>
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

#ifndef _SYS_EPOCH_H_
#define _SYS_EPOCH_H_

struct epoch_context {
	void   *data[2];
} __aligned(sizeof(void *));

typedef struct epoch_context *epoch_context_t;

#ifdef _KERNEL
#include <sys/lock.h>
#include <sys/pcpu.h>
#include <ck_epoch.h>

struct epoch;
typedef struct epoch *epoch_t;

#define EPOCH_PREEMPT 0x1
#define EPOCH_LOCKED 0x2

extern epoch_t global_epoch;
extern epoch_t global_epoch_preempt;

struct epoch_tracker {
#ifdef	EPOCH_TRACKER_DEBUG
#define	EPOCH_MAGIC0 0xFADECAFEF00DD00D
#define	EPOCH_MAGIC1 0xBADDBABEDEEDFEED
	uint64_t et_magic_pre;
#endif
	TAILQ_ENTRY(epoch_tracker) et_link;
	struct thread *et_td;
	ck_epoch_section_t et_section;
#ifdef	EPOCH_TRACKER_DEBUG
	uint64_t et_magic_post;
#endif
}  __aligned(sizeof(void *));
typedef struct epoch_tracker *epoch_tracker_t;

epoch_t	epoch_alloc(int flags);
void	epoch_free(epoch_t epoch);
void	epoch_wait(epoch_t epoch);
void	epoch_wait_preempt(epoch_t epoch);
void	epoch_call(epoch_t epoch, epoch_context_t ctx, void (*callback) (epoch_context_t));
int	in_epoch(epoch_t epoch);
int in_epoch_verbose(epoch_t epoch, int dump_onfail);
DPCPU_DECLARE(int, epoch_cb_count);
DPCPU_DECLARE(struct grouptask, epoch_cb_task);
#define EPOCH_MAGIC0 0xFADECAFEF00DD00D
#define EPOCH_MAGIC1 0xBADDBABEDEEDFEED

void epoch_enter_preempt(epoch_t epoch, epoch_tracker_t et);
void epoch_exit_preempt(epoch_t epoch, epoch_tracker_t et);
void epoch_enter(epoch_t epoch);
void epoch_exit(epoch_t epoch);

void epoch_thread_init(struct thread *);
void epoch_thread_fini(struct thread *);

#endif	/* _KERNEL */
#endif	/* _SYS_EPOCH_H_ */
