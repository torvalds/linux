/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2012 Konstantin Belousov <kib@FreeBSD.ORG>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_VDSO_H
#define	_SYS_VDSO_H

#include <sys/types.h>
#include <machine/vdso.h>

struct vdso_timehands {
	uint32_t	th_algo;
	uint32_t	th_gen;
	uint64_t	th_scale;
	uint32_t 	th_offset_count;
	uint32_t	th_counter_mask;
	struct bintime	th_offset;
	struct bintime	th_boottime;
	VDSO_TIMEHANDS_MD
};

struct vdso_timekeep {
	uint32_t	tk_ver;
	uint32_t	tk_enabled;
	uint32_t	tk_current;
	struct vdso_timehands	tk_th[];
};

#define	VDSO_TK_CURRENT_BUSY	0xffffffff
#define	VDSO_TK_VER_1		0x1
#define	VDSO_TK_VER_CURR	VDSO_TK_VER_1
#define	VDSO_TH_ALGO_1		0x1
#define	VDSO_TH_ALGO_2		0x2
#define	VDSO_TH_ALGO_3		0x3
#define	VDSO_TH_ALGO_4		0x4

#ifndef _KERNEL

struct timespec;
struct timeval;
struct timezone;

int __vdso_clock_gettime(clockid_t clock_id, struct timespec *ts);
int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz);
int __vdso_gettc(const struct vdso_timehands *vdso_th, u_int *tc);
int __vdso_gettimekeep(struct vdso_timekeep **tk);

#endif

#ifdef _KERNEL

struct timecounter;

struct vdso_sv_tk {
	int		sv_timekeep_off;
	int		sv_timekeep_curr;
	uint32_t	sv_timekeep_gen;
};

void timekeep_push_vdso(void);

uint32_t tc_fill_vdso_timehands(struct vdso_timehands *vdso_th);

/*
 * The cpu_fill_vdso_timehands() function should fill MD-part of the
 * struct vdso_timehands, which is both machine- and
 * timecounter-depended. The return value should be 1 if fast
 * userspace timecounter is enabled by hardware, and 0 otherwise. The
 * global sysctl enable override is handled by machine-independed code
 * after cpu_fill_vdso_timehands() call is made.
 */
uint32_t cpu_fill_vdso_timehands(struct vdso_timehands *vdso_th,
    struct timecounter *tc);

struct vdso_sv_tk *alloc_sv_tk(void);

#define	VDSO_TH_NUM	4

#ifdef COMPAT_FREEBSD32
struct bintime32 {
	uint32_t	sec;
	uint32_t	frac[2];
};

struct vdso_timehands32 {
	uint32_t	th_algo;
	uint32_t	th_gen;
	uint32_t	th_scale[2];
	uint32_t 	th_offset_count;
	uint32_t	th_counter_mask;
	struct bintime32	th_offset;
	struct bintime32	th_boottime;
	VDSO_TIMEHANDS_MD32
};

struct vdso_timekeep32 {
	uint32_t	tk_ver;
	uint32_t	tk_enabled;
	uint32_t	tk_current;
	struct vdso_timehands32	tk_th[];
};

uint32_t tc_fill_vdso_timehands32(struct vdso_timehands32 *vdso_th32);
uint32_t cpu_fill_vdso_timehands32(struct vdso_timehands32 *vdso_th32,
    struct timecounter *tc);
struct vdso_sv_tk *alloc_sv_tk_compat32(void);

#endif
#endif

#endif
