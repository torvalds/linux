/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Konstantin Belousov <kib@FreeBSD.org>
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

#include <sys/elf.h>
#include <sys/time.h>
#include <sys/vdso.h>
#include <errno.h>
#include <time.h>
#include <machine/atomic.h>
#include "libc_private.h"

static int
tc_delta(const struct vdso_timehands *th, u_int *delta)
{
	int error;
	u_int tc;

	error = __vdso_gettc(th, &tc);
	if (error == 0)
		*delta = (tc - th->th_offset_count) & th->th_counter_mask;
	return (error);
}

/*
 * Calculate the absolute or boot-relative time from the
 * machine-specific fast timecounter and the published timehands
 * structure read from the shared page.
 *
 * The lockless reading scheme is similar to the one used to read the
 * in-kernel timehands, see sys/kern/kern_tc.c:binuptime().  This code
 * is based on the kernel implementation.
 */
static int
binuptime(struct bintime *bt, struct vdso_timekeep *tk, int abs)
{
	struct vdso_timehands *th;
	uint32_t curr, gen;
	u_int delta;
	int error;

	do {
		if (!tk->tk_enabled)
			return (ENOSYS);

		curr = atomic_load_acq_32(&tk->tk_current);
		th = &tk->tk_th[curr];
		gen = atomic_load_acq_32(&th->th_gen);
		*bt = th->th_offset;
		error = tc_delta(th, &delta);
		if (error == EAGAIN)
			continue;
		if (error != 0)
			return (error);
		bintime_addx(bt, th->th_scale * delta);
		if (abs)
			bintime_add(bt, &th->th_boottime);

		/*
		 * Ensure that the load of th_offset is completed
		 * before the load of th_gen.
		 */
		atomic_thread_fence_acq();
	} while (curr != tk->tk_current || gen == 0 || gen != th->th_gen);
	return (0);
}

static struct vdso_timekeep *tk;

#pragma weak __vdso_gettimeofday
int
__vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	struct bintime bt;
	int error;

	if (tz != NULL)
		return (ENOSYS);
	if (tk == NULL) {
		error = __vdso_gettimekeep(&tk);
		if (error != 0 || tk == NULL)
			return (ENOSYS);
	}
	if (tk->tk_ver != VDSO_TK_VER_CURR)
		return (ENOSYS);
	error = binuptime(&bt, tk, 1);
	if (error != 0)
		return (error);
	bintime2timeval(&bt, tv);
	return (0);
}

#pragma weak __vdso_clock_gettime
int
__vdso_clock_gettime(clockid_t clock_id, struct timespec *ts)
{
	struct bintime bt;
	int abs, error;

	if (tk == NULL) {
		error = _elf_aux_info(AT_TIMEKEEP, &tk, sizeof(tk));
		if (error != 0 || tk == NULL)
			return (ENOSYS);
	}
	if (tk->tk_ver != VDSO_TK_VER_CURR)
		return (ENOSYS);
	switch (clock_id) {
	case CLOCK_REALTIME:
	case CLOCK_REALTIME_PRECISE:
	case CLOCK_REALTIME_FAST:
	case CLOCK_SECOND:
		abs = 1;
		break;
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_PRECISE:
	case CLOCK_MONOTONIC_FAST:
	case CLOCK_UPTIME:
	case CLOCK_UPTIME_PRECISE:
	case CLOCK_UPTIME_FAST:
		abs = 0;
		break;
	default:
		return (ENOSYS);
	}
	error = binuptime(&bt, tk, abs);
	if (error != 0)
		return (error);
	bintime2timespec(&bt, ts);
	if (clock_id == CLOCK_SECOND)
		ts->tv_nsec = 0;
	return (0);
}
