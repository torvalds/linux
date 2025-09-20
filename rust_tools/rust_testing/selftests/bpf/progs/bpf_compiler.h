/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BPF_COMPILER_H__
#define __BPF_COMPILER_H__

#define DO_PRAGMA_(X) _Pragma(#X)

#if __clang__
#define __pragma_loop_unroll DO_PRAGMA_(clang loop unroll(enable))
#else
/* In GCC -funroll-loops, which is enabled with -O2, should have the
   same impact than the loop-unroll-enable pragma above.  */
#define __pragma_loop_unroll
#endif

#if __clang__
#define __pragma_loop_unroll_count(N) DO_PRAGMA_(clang loop unroll_count(N))
#else
#define __pragma_loop_unroll_count(N) DO_PRAGMA_(GCC unroll N)
#endif

#if __clang__
#define __pragma_loop_unroll_full DO_PRAGMA_(clang loop unroll(full))
#else
#define __pragma_loop_unroll_full DO_PRAGMA_(GCC unroll 65534)
#endif

#if __clang__
#define __pragma_loop_no_unroll DO_PRAGMA_(clang loop unroll(disable))
#else
#define __pragma_loop_no_unroll DO_PRAGMA_(GCC unroll 1)
#endif

#endif
