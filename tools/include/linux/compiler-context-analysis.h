/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_COMPILER_CONTEXT_ANALYSIS_H
#define _TOOLS_LINUX_COMPILER_CONTEXT_ANALYSIS_H

/*
 * Macros and attributes for compiler-based static context analysis.
 * No-op stubs for tools.
 */

#define __guarded_by(...)
#define __pt_guarded_by(...)

#define context_lock_struct(name, ...)	struct __VA_ARGS__ name

#define __no_context_analysis
#define __context_unsafe(comment)
#define context_unsafe(...)		({ __VA_ARGS__; })
#define context_unsafe_alias(p)
#define disable_context_analysis()
#define enable_context_analysis()

#define __must_hold(...)
#define __must_not_hold(...)
#define __acquires(...)
#define __cond_acquires(ret, x)
#define __releases(...)
#define __acquire(x)			(void)0
#define __release(x)			(void)0

#define __must_hold_shared(...)
#define __acquires_shared(...)
#define __cond_acquires_shared(ret, x)
#define __releases_shared(...)
#define __acquire_shared(x)		(void)0
#define __release_shared(x)		(void)0

#define __acquire_ret(call, expr)	(call)
#define __acquire_shared_ret(call, expr) (call)
#define __acquires_ret
#define __acquires_shared_ret

#endif /* _TOOLS_LINUX_COMPILER_CONTEXT_ANALYSIS_H */
