/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Tejun Heo <tj@kernel.org> */
#pragma once

/*
 * Constants shared across the BPF headers, the lib and the schedulers.
 * Freestanding so that scheduler interface headers consumed by bindgen can
 * include it. Rust code cannot, so rust/scx_arena/scx_arena/src/lib.rs mirrors
 * the values. Keep them in sync.
 */
enum scx_const_defs {
	/* mirrors the kernel's per-arch L1_CACHE_SHIFT */
#if defined(__TARGET_ARCH_s390) || defined(__s390x__)
	SCX_CACHELINE_SIZE	= 256,
#elif defined(__TARGET_ARCH_powerpc) || defined(__powerpc64__)
	SCX_CACHELINE_SIZE	= 128,
#else
	SCX_CACHELINE_SIZE	= 64,
#endif
};
