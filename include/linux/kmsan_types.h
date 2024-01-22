/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A minimal header declaring types added by KMSAN to existing kernel structs.
 *
 * Copyright (C) 2017-2022 Google LLC
 * Author: Alexander Potapenko <glider@google.com>
 *
 */
#ifndef _LINUX_KMSAN_TYPES_H
#define _LINUX_KMSAN_TYPES_H

#include <linux/types.h>

/* These constants are defined in the MSan LLVM instrumentation pass. */
#define KMSAN_RETVAL_SIZE 800
#define KMSAN_PARAM_SIZE 800

struct kmsan_context_state {
	char param_tls[KMSAN_PARAM_SIZE];
	char retval_tls[KMSAN_RETVAL_SIZE];
	char va_arg_tls[KMSAN_PARAM_SIZE];
	char va_arg_origin_tls[KMSAN_PARAM_SIZE];
	u64 va_arg_overflow_size_tls;
	char param_origin_tls[KMSAN_PARAM_SIZE];
	u32 retval_origin_tls;
};

#undef KMSAN_PARAM_SIZE
#undef KMSAN_RETVAL_SIZE

struct kmsan_ctx {
	struct kmsan_context_state cstate;
	int kmsan_in_runtime;
	bool allow_reporting;
};

#endif /* _LINUX_KMSAN_TYPES_H */
