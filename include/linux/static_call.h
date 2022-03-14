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
 *   DEFINE_STATIC_CALL_NULL(name, typename);
 *   DEFINE_STATIC_CALL_RET0(name, typename);
 *
 *   __static_call_return0;
 *
 *   static_call(name)(args...);
 *   static_call_cond(name)(args...);
 *   static_call_update(name, func);
 *   static_call_query(name);
 *
 *   EXPORT_STATIC_CALL{,_TRAMP}{,_GPL}()
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
 *
 *
 * Notes on NULL function pointers:
 *
 *   Static_call()s support NULL functions, with many of the caveats that
 *   regular function pointers have.
 *
 *   Clearly calling a NULL function pointer is 'BAD', so too for
 *   static_call()s (although when HAVE_STATIC_CALL it might not be immediately
 *   fatal). A NULL static_call can be the result of:
 *
 *     DECLARE_STATIC_CALL_NULL(my_static_call, void (*)(int));
 *
 *   which is equivalent to declaring a NULL function pointer with just a
 *   typename:
 *
 *     void (*my_func_ptr)(int arg1) = NULL;
 *
 *   or using static_call_update() with a NULL function. In both cases the
 *   HAVE_STATIC_CALL implementation will patch the trampoline with a RET
 *   instruction, instead of an immediate tail-call JMP. HAVE_STATIC_CALL_INLINE
 *   architectures can patch the trampoline call to a NOP.
 *
 *   In all cases, any argument evaluation is unconditional. Unlike a regular
 *   conditional function pointer call:
 *
 *     if (my_func_ptr)
 *         my_func_ptr(arg1)
 *
 *   where the argument evaludation also depends on the pointer value.
 *
 *   When calling a static_call that can be NULL, use:
 *
 *     static_call_cond(name)(arg1);
 *
 *   which will include the required value tests to avoid NULL-pointer
 *   dereferences.
 *
 *   To query which function is currently set to be called, use:
 *
 *   func = static_call_query(name);
 *
 *
 * DEFINE_STATIC_CALL_RET0 / __static_call_return0:
 *
 *   Just like how DEFINE_STATIC_CALL_NULL() / static_call_cond() optimize the
 *   conditional void function call, DEFINE_STATIC_CALL_RET0 /
 *   __static_call_return0 optimize the do nothing return 0 function.
 *
 *   This feature is strictly UB per the C standard (since it casts a function
 *   pointer to a different signature) and relies on the architecture ABI to
 *   make things work. In particular it relies on Caller Stack-cleanup and the
 *   whole return register being clobbered for short return values. All normal
 *   CDECL style ABIs conform.
 *
 *   In particular the x86_64 implementation replaces the 5 byte CALL
 *   instruction at the callsite with a 5 byte clear of the RAX register,
 *   completely eliding any function call overhead.
 *
 *   Notably argument setup is unconditional.
 *
 *
 * EXPORT_STATIC_CALL() vs EXPORT_STATIC_CALL_TRAMP():
 *
 *   The difference is that the _TRAMP variant tries to only export the
 *   trampoline with the result that a module can use static_call{,_cond}() but
 *   not static_call_update().
 *
 */

#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/static_call_types.h>

#ifdef CONFIG_HAVE_STATIC_CALL
#include <asm/static_call.h>

/*
 * Either @site or @tramp can be NULL.
 */
extern void arch_static_call_transform(void *site, void *tramp, void *func, bool tail);

#define STATIC_CALL_TRAMP_ADDR(name) &STATIC_CALL_TRAMP(name)

#else
#define STATIC_CALL_TRAMP_ADDR(name) NULL
#endif

#define static_call_update(name, func)					\
({									\
	typeof(&STATIC_CALL_TRAMP(name)) __F = (func);			\
	__static_call_update(&STATIC_CALL_KEY(name),			\
			     STATIC_CALL_TRAMP_ADDR(name), __F);	\
})

#define static_call_query(name) (READ_ONCE(STATIC_CALL_KEY(name).func))

#ifdef CONFIG_HAVE_STATIC_CALL_INLINE

extern int __init static_call_init(void);

struct static_call_mod {
	struct static_call_mod *next;
	struct module *mod; /* for vmlinux, mod == NULL */
	struct static_call_site *sites;
};

/* For finding the key associated with a trampoline */
struct static_call_tramp_key {
	s32 tramp;
	s32 key;
};

extern void __static_call_update(struct static_call_key *key, void *tramp, void *func);
extern int static_call_mod_init(struct module *mod);
extern int static_call_text_reserved(void *start, void *end);

extern long __static_call_return0(void);

#define __DEFINE_STATIC_CALL(name, _func, _func_init)			\
	DECLARE_STATIC_CALL(name, _func);				\
	struct static_call_key STATIC_CALL_KEY(name) = {		\
		.func = _func_init,					\
		.type = 1,						\
	};								\
	ARCH_DEFINE_STATIC_CALL_TRAMP(name, _func_init)

#define DEFINE_STATIC_CALL_NULL(name, _func)				\
	DECLARE_STATIC_CALL(name, _func);				\
	struct static_call_key STATIC_CALL_KEY(name) = {		\
		.func = NULL,						\
		.type = 1,						\
	};								\
	ARCH_DEFINE_STATIC_CALL_NULL_TRAMP(name)

#define DEFINE_STATIC_CALL_RET0(name, _func)				\
	DECLARE_STATIC_CALL(name, _func);				\
	struct static_call_key STATIC_CALL_KEY(name) = {		\
		.func = __static_call_return0,				\
		.type = 1,						\
	};								\
	ARCH_DEFINE_STATIC_CALL_RET0_TRAMP(name)

#define static_call_cond(name)	(void)__static_call(name)

#define EXPORT_STATIC_CALL(name)					\
	EXPORT_SYMBOL(STATIC_CALL_KEY(name));				\
	EXPORT_SYMBOL(STATIC_CALL_TRAMP(name))
#define EXPORT_STATIC_CALL_GPL(name)					\
	EXPORT_SYMBOL_GPL(STATIC_CALL_KEY(name));			\
	EXPORT_SYMBOL_GPL(STATIC_CALL_TRAMP(name))

/* Leave the key unexported, so modules can't change static call targets: */
#define EXPORT_STATIC_CALL_TRAMP(name)					\
	EXPORT_SYMBOL(STATIC_CALL_TRAMP(name));				\
	ARCH_ADD_TRAMP_KEY(name)
#define EXPORT_STATIC_CALL_TRAMP_GPL(name)				\
	EXPORT_SYMBOL_GPL(STATIC_CALL_TRAMP(name));			\
	ARCH_ADD_TRAMP_KEY(name)

#elif defined(CONFIG_HAVE_STATIC_CALL)

static inline int static_call_init(void) { return 0; }

#define __DEFINE_STATIC_CALL(name, _func, _func_init)			\
	DECLARE_STATIC_CALL(name, _func);				\
	struct static_call_key STATIC_CALL_KEY(name) = {		\
		.func = _func_init,					\
	};								\
	ARCH_DEFINE_STATIC_CALL_TRAMP(name, _func_init)

#define DEFINE_STATIC_CALL_NULL(name, _func)				\
	DECLARE_STATIC_CALL(name, _func);				\
	struct static_call_key STATIC_CALL_KEY(name) = {		\
		.func = NULL,						\
	};								\
	ARCH_DEFINE_STATIC_CALL_NULL_TRAMP(name)

#define DEFINE_STATIC_CALL_RET0(name, _func)				\
	DECLARE_STATIC_CALL(name, _func);				\
	struct static_call_key STATIC_CALL_KEY(name) = {		\
		.func = __static_call_return0,				\
	};								\
	ARCH_DEFINE_STATIC_CALL_RET0_TRAMP(name)

#define static_call_cond(name)	(void)__static_call(name)

static inline
void __static_call_update(struct static_call_key *key, void *tramp, void *func)
{
	cpus_read_lock();
	WRITE_ONCE(key->func, func);
	arch_static_call_transform(NULL, tramp, func, false);
	cpus_read_unlock();
}

static inline int static_call_text_reserved(void *start, void *end)
{
	return 0;
}

extern long __static_call_return0(void);

#define EXPORT_STATIC_CALL(name)					\
	EXPORT_SYMBOL(STATIC_CALL_KEY(name));				\
	EXPORT_SYMBOL(STATIC_CALL_TRAMP(name))
#define EXPORT_STATIC_CALL_GPL(name)					\
	EXPORT_SYMBOL_GPL(STATIC_CALL_KEY(name));			\
	EXPORT_SYMBOL_GPL(STATIC_CALL_TRAMP(name))

/* Leave the key unexported, so modules can't change static call targets: */
#define EXPORT_STATIC_CALL_TRAMP(name)					\
	EXPORT_SYMBOL(STATIC_CALL_TRAMP(name))
#define EXPORT_STATIC_CALL_TRAMP_GPL(name)				\
	EXPORT_SYMBOL_GPL(STATIC_CALL_TRAMP(name))

#else /* Generic implementation */

static inline int static_call_init(void) { return 0; }

static inline long __static_call_return0(void)
{
	return 0;
}

#define __DEFINE_STATIC_CALL(name, _func, _func_init)			\
	DECLARE_STATIC_CALL(name, _func);				\
	struct static_call_key STATIC_CALL_KEY(name) = {		\
		.func = _func_init,					\
	}

#define DEFINE_STATIC_CALL_NULL(name, _func)				\
	DECLARE_STATIC_CALL(name, _func);				\
	struct static_call_key STATIC_CALL_KEY(name) = {		\
		.func = NULL,						\
	}

#define DEFINE_STATIC_CALL_RET0(name, _func)				\
	__DEFINE_STATIC_CALL(name, _func, __static_call_return0)

static inline void __static_call_nop(void) { }

/*
 * This horrific hack takes care of two things:
 *
 *  - it ensures the compiler will only load the function pointer ONCE,
 *    which avoids a reload race.
 *
 *  - it ensures the argument evaluation is unconditional, similar
 *    to the HAVE_STATIC_CALL variant.
 *
 * Sadly current GCC/Clang (10 for both) do not optimize this properly
 * and will emit an indirect call for the NULL case :-(
 */
#define __static_call_cond(name)					\
({									\
	void *func = READ_ONCE(STATIC_CALL_KEY(name).func);		\
	if (!func)							\
		func = &__static_call_nop;				\
	(typeof(STATIC_CALL_TRAMP(name))*)func;				\
})

#define static_call_cond(name)	(void)__static_call_cond(name)

static inline
void __static_call_update(struct static_call_key *key, void *tramp, void *func)
{
	WRITE_ONCE(key->func, func);
}

static inline int static_call_text_reserved(void *start, void *end)
{
	return 0;
}

#define EXPORT_STATIC_CALL(name)	EXPORT_SYMBOL(STATIC_CALL_KEY(name))
#define EXPORT_STATIC_CALL_GPL(name)	EXPORT_SYMBOL_GPL(STATIC_CALL_KEY(name))

#endif /* CONFIG_HAVE_STATIC_CALL */

#define DEFINE_STATIC_CALL(name, _func)					\
	__DEFINE_STATIC_CALL(name, _func, _func)

#endif /* _LINUX_STATIC_CALL_H */
