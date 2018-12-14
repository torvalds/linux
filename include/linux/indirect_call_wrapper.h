/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_INDIRECT_CALL_WRAPPER_H
#define _LINUX_INDIRECT_CALL_WRAPPER_H

#ifdef CONFIG_RETPOLINE

/*
 * INDIRECT_CALL_$NR - wrapper for indirect calls with $NR known builtin
 *  @f: function pointer
 *  @f$NR: builtin functions names, up to $NR of them
 *  @__VA_ARGS__: arguments for @f
 *
 * Avoid retpoline overhead for known builtin, checking @f vs each of them and
 * eventually invoking directly the builtin function. The functions are check
 * in the given order. Fallback to the indirect call.
 */
#define INDIRECT_CALL_1(f, f1, ...)					\
	({								\
		likely(f == f1) ? f1(__VA_ARGS__) : f(__VA_ARGS__);	\
	})
#define INDIRECT_CALL_2(f, f2, f1, ...)					\
	({								\
		likely(f == f2) ? f2(__VA_ARGS__) :			\
				  INDIRECT_CALL_1(f, f1, __VA_ARGS__);	\
	})

#define INDIRECT_CALLABLE_DECLARE(f)	f
#define INDIRECT_CALLABLE_SCOPE

#else
#define INDIRECT_CALL_1(f, name, ...) f(__VA_ARGS__)
#define INDIRECT_CALL_2(f, name, ...) f(__VA_ARGS__)
#define INDIRECT_CALLABLE_DECLARE(f)
#define INDIRECT_CALLABLE_SCOPE		static
#endif

/*
 * We can use INDIRECT_CALL_$NR for ipv6 related functions only if ipv6 is
 * builtin, this macro simplify dealing with indirect calls with only ipv4/ipv6
 * alternatives
 */
#if IS_BUILTIN(CONFIG_IPV6)
#define INDIRECT_CALL_INET(f, f2, f1, ...) \
	INDIRECT_CALL_2(f, f2, f1, __VA_ARGS__)
#elif IS_ENABLED(CONFIG_INET)
#define INDIRECT_CALL_INET(f, f2, f1, ...) INDIRECT_CALL_1(f, f1, __VA_ARGS__)
#else
#define INDIRECT_CALL_INET(f, f2, f1, ...) f(__VA_ARGS__)
#endif

#endif
