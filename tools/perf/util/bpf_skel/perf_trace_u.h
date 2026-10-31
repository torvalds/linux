/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (c) 2025 Google */

/* This file will be shared between BPF and userspace. */

#ifndef __PERF_TRACE_U_H
#define __PERF_TRACE_U_H

enum syscall_trace_type {
	SYSCALL_TRACE_ENTER = 0,
	SYSCALL_TRACE_EXIT,
};

#endif /* __PERF_TRACE_U_H */
