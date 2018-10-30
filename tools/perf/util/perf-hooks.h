/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_UTIL_PERF_HOOKS_H
#define PERF_UTIL_PERF_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*perf_hook_func_t)(void *ctx);
struct perf_hook_desc {
	const char * const hook_name;
	perf_hook_func_t * const p_hook_func;
	void *hook_ctx;
};

extern void perf_hooks__invoke(const struct perf_hook_desc *);
extern void perf_hooks__recover(void);

#define PERF_HOOK(name)					\
extern struct perf_hook_desc __perf_hook_desc_##name;	\
static inline void perf_hooks__invoke_##name(void)	\
{ 							\
	perf_hooks__invoke(&__perf_hook_desc_##name);	\
}

#include "perf-hooks-list.h"
#undef PERF_HOOK

extern int
perf_hooks__set_hook(const char *hook_name,
		     perf_hook_func_t hook_func,
		     void *hook_ctx);

extern perf_hook_func_t
perf_hooks__get_hook(const char *hook_name);

#ifdef __cplusplus
}
#endif
#endif
