/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2008 Stanislav Sedov <stas@FreeBSD.org>
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

#ifndef _CPUCTL_H_
#define	_CPUCTL_H_

typedef struct {
	int		msr;	/* MSR to read */
	uint64_t	data;
} cpuctl_msr_args_t;

typedef struct {
	int		level;		/* CPUID level */
	uint32_t	data[4];
} cpuctl_cpuid_args_t;

typedef struct {
	int		level;		/* CPUID level */
	int		level_type;	/* CPUID level type */
	uint32_t	data[4];
} cpuctl_cpuid_count_args_t;

typedef struct {
	void	*data;
	size_t	size;
} cpuctl_update_args_t;

#define	CPUCTL_RDMSR	_IOWR('c', 1, cpuctl_msr_args_t)
#define	CPUCTL_WRMSR	_IOWR('c', 2, cpuctl_msr_args_t)
#define	CPUCTL_CPUID	_IOWR('c', 3, cpuctl_cpuid_args_t)
#define	CPUCTL_UPDATE	_IOWR('c', 4, cpuctl_update_args_t)
#define	CPUCTL_MSRSBIT	_IOWR('c', 5, cpuctl_msr_args_t)
#define	CPUCTL_MSRCBIT	_IOWR('c', 6, cpuctl_msr_args_t)
#define	CPUCTL_CPUID_COUNT _IOWR('c', 7, cpuctl_cpuid_count_args_t)
#define	CPUCTL_EVAL_CPU_FEATURES	_IO('c', 8)

#endif /* _CPUCTL_H_ */
