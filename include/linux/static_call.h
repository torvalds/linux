/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STATIC_CALL_H
#define _LINUX_STATIC_CALL_H

/*
 * Static call support
 *
 * Static calls use code patching to hard-code function pointers into direct
 * branch instructions. They give the flexibility of function pointers, but
 * with improved performance. This is especially important for cases where
 * retpolines would otherwise be used, as retpolines can significantly impact
 * performance.
 *
 *
 * API overview:
 *
 *   DECLARE_STATIC_CALL(name, func);
 *   DEFINE_STATIC_CALL(name, func);
 *   static_call(name)(args...);
 *   static_call_update(name, func);
 *
 * Usage example:
 *
 *   # Start with the following functions (with identical prototypes):
 *   int func_a(int arg1, int arg2);
 *   int func_b(int arg1, int arg2);
 *
 *   # Define a 'my_name' reference, associated with func_a() by default
 *   DEFINE_STATIC_CALL(my_name, func_a);
 *
 *   # Call func_a()
 *   static_call(my_name)(arg1, arg2);
 *
 *   # Update 'my_name' to point to func_b()
 *   static_call_update(my_name, &func_b);
 *
 *   # Call func_b()
 *   static_call(my_name)(arg1, arg2);
 *
 *
 * Implementation details:
 *
 *   This requires some arch-specific code (CONFIG_HAVE_STATIC_CALL).
 *   Otherwise basic indirect calls are used (with function pointers).
 *
 *   Each static_call() site calls into a trampoline associated with the name.
 *   The trampoline has a direct branch to the default function.  Updates to a
 *   name will modify the trampoline's branch destination.
 *
 *   If the arch has CONFIG_HAVE_STATIC_CALL_INLINE, then the call sites
 *   themselves will be patched at runtime to call the functions directly,
 *   rather than calling through the trampoline.  This requires objtool or a
 *   compiler plugin to detect all the static_call() sites and annotate them
 *   in the .static_call_sites section.
 */

#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/static_call_types.h>

#ifdef CONFIG_HAVE_STATIC_CALL
#include <asm/static_call.h>

/*
 * Either @site or @tramp can be NULL.
 */
extern void arch_static_call_transform(void *site, void *tramp, void *func);

#define STATIC_CALL_TRAMP_ADDR(name) &STATIC_CALL_TRAMP(name)

/*
 * __ADDRESSABLE() is used to ensure the key symbol doesn't get stripped from
 * the symbol table so that objtool can reference it when it generates the
 * .static_call_sites section.
 */
#define __static_call(name)						\
({									\
	__ADDRESSABLE(STATIC_CALL_KEY(name));				\
	&STATIC_CALL_TRAMP(name);					\
})

#else
#define STATIC_CALL_TRAMP_ADDR(name) NULL
#endif


#define DECLARE_STATIC_CALL(name, func)					\
	extern struct static_call_key STATIC_CALL_KEY(name);		\
	extern typeof(func) STATIC_CALL_TRAMP(name);

#define static_call_update(name, func)					\
({									\
	BUILD_BUG_ON(!__same_type(*(func), STATIC_CALL_TRAMP(name)));	\
	__static_call_update(&STATIC_CALL_KEY(name),			\
			     STATIC_CALL_TRAMP_ADDR(name), func);	\
})

#if defined(CONFIG_HAVE_STATIC_CALL)

struct static_call_key {
	void *func;
};

#define DEFINE_STATIC_CALL(name, _func)					\
	DECLARE_STATIC_CALL(name, _func);				\
	struct static_call_key STATIC_CALL_KEY(name) = {		\
		.func = _func,						\
	};								\
	ARCH_DEFINE_STATIC_CALL_TRAMP(name, _func)

#define static_call(name)	__static_call(name)

static inline
void __static_call_update(struct static_call_key *key, void *tramp, void *func)
{
	cpus_read_lock();
	WRITE_ONCE(key->func, func);
	arch_static_call_transform(NULL, tramp, func);
	cpus_read_unlock();
}

#define EXPORT_STATIC_CALL(name)					\
	EXPORT_SYMBOL(STATIC_CALL_KEY(name));				\
	EXPORT_SYMBOL(STATIC_CALL_TRAMP(name))

#define EXPORT_STATIC_CALL_GPL(name)					\
	EXPORT_SYMBOL_GPL(STATIC_CALL_KEY(name));			\
	EXPORT_SYMBOL_GPL(STATIC_CALL_TRAMP(name))

#else /* Generic implementation */

struct static_call_key {
	void *func;
};

#define DEFINE_STATIC_CALL(name, _func)					\
	DECLARE_STATIC_CALL(name, _func);				\
	struct static_call_key STATIC_CALL_KEY(name) = {		\
		.func = _func,						\
	}

#define static_call(name)						\
	((typeof(STATIC_CALL_TRAMP(name))*)(STATIC_CALL_KEY(name).func))

static inline
void __static_call_update(struct static_call_key *key, void *tramp, void *func)
{
	WRITE_ONCE(key->func, func);
}

#define EXPORT_STATIC_CALL(name)	EXPORT_SYMBOL(STATIC_CALL_KEY(name))
#define EXPORT_STATIC_CALL_GPL(name)	EXPORT_SYMBOL_GPL(STATIC_CALL_KEY(name))

#endif /* CONFIG_HAVE_STATIC_CALL */

#endif /* _LINUX_STATIC_CALL_H */
