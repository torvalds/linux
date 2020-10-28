// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#define barrier_var(var) asm volatile("" : "=r"(var) : "0"(var))
#define UNROLL
#define INLINE __always_inline
#include "profiler.inc.h"
