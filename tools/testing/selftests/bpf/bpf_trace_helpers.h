/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BPF_TRACE_HELPERS_H
#define __BPF_TRACE_HELPERS_H

#include "bpf_helpers.h"

#define __BPF_MAP_0(i, m, v, ...) v
#define __BPF_MAP_1(i, m, v, t, a, ...) m(t, a, ctx[i])
#define __BPF_MAP_2(i, m, v, t, a, ...) m(t, a, ctx[i]), __BPF_MAP_1(i+1, m, v, __VA_ARGS__)
#define __BPF_MAP_3(i, m, v, t, a, ...) m(t, a, ctx[i]), __BPF_MAP_2(i+1, m, v, __VA_ARGS__)
#define __BPF_MAP_4(i, m, v, t, a, ...) m(t, a, ctx[i]), __BPF_MAP_3(i+1, m, v, __VA_ARGS__)
#define __BPF_MAP_5(i, m, v, t, a, ...) m(t, a, ctx[i]), __BPF_MAP_4(i+1, m, v, __VA_ARGS__)
#define __BPF_MAP_6(i, m, v, t, a, ...) m(t, a, ctx[i]), __BPF_MAP_5(i+1, m, v, __VA_ARGS__)
#define __BPF_MAP_7(i, m, v, t, a, ...) m(t, a, ctx[i]), __BPF_MAP_6(i+1, m, v, __VA_ARGS__)
#define __BPF_MAP_8(i, m, v, t, a, ...) m(t, a, ctx[i]), __BPF_MAP_7(i+1, m, v, __VA_ARGS__)
#define __BPF_MAP_9(i, m, v, t, a, ...) m(t, a, ctx[i]), __BPF_MAP_8(i+1, m, v, __VA_ARGS__)
#define __BPF_MAP_10(i, m, v, t, a, ...) m(t, a, ctx[i]), __BPF_MAP_9(i+1, m, v, __VA_ARGS__)
#define __BPF_MAP_11(i, m, v, t, a, ...) m(t, a, ctx[i]), __BPF_MAP_10(i+1, m, v, __VA_ARGS__)
#define __BPF_MAP_12(i, m, v, t, a, ...) m(t, a, ctx[i]), __BPF_MAP_11(i+1, m, v, __VA_ARGS__)
#define __BPF_MAP(n, ...) __BPF_MAP_##n(0, __VA_ARGS__)

/* BPF sizeof(void *) is always 8, so no need to cast to long first
 * for ptr to avoid compiler warning.
 */
#define __BPF_CAST(t, a, ctx) (t) ctx
#define __BPF_V void
#define __BPF_N

#define __BPF_DECL_ARGS(t, a, ctx) t a

#define BPF_TRACE_x(x, sec_name, fname, ret_type, ...)			\
static __always_inline ret_type						\
____##fname(__BPF_MAP(x, __BPF_DECL_ARGS, __BPF_V, __VA_ARGS__));	\
									\
SEC(sec_name)								\
ret_type fname(__u64 *ctx)						\
{									\
	return ____##fname(__BPF_MAP(x, __BPF_CAST, __BPF_N, __VA_ARGS__));\
}									\
									\
static __always_inline							\
ret_type ____##fname(__BPF_MAP(x, __BPF_DECL_ARGS, __BPF_V, __VA_ARGS__))

#define BPF_TRACE_0(sec, fname, ...)  BPF_TRACE_x(0, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_1(sec, fname, ...)  BPF_TRACE_x(1, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_2(sec, fname, ...)  BPF_TRACE_x(2, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_3(sec, fname, ...)  BPF_TRACE_x(3, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_4(sec, fname, ...)  BPF_TRACE_x(4, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_5(sec, fname, ...)  BPF_TRACE_x(5, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_6(sec, fname, ...)  BPF_TRACE_x(6, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_7(sec, fname, ...)  BPF_TRACE_x(7, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_8(sec, fname, ...)  BPF_TRACE_x(8, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_9(sec, fname, ...)  BPF_TRACE_x(9, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_10(sec, fname, ...)  BPF_TRACE_x(10, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_11(sec, fname, ...)  BPF_TRACE_x(11, sec, fname, int, __VA_ARGS__)
#define BPF_TRACE_12(sec, fname, ...)  BPF_TRACE_x(12, sec, fname, int, __VA_ARGS__)

#endif
