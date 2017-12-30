// SPDX-License-Identifier: GPL-2.0
/*
 * perf_hooks.c
 *
 * Copyright (C) 2016 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2016 Huawei Inc.
 */

#include <errno.h>
#include <stdlib.h>
#include <setjmp.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include "util/util.h"
#include "util/debug.h"
#include "util/perf-hooks.h"

static sigjmp_buf jmpbuf;
static const struct perf_hook_desc *current_perf_hook;

void perf_hooks__invoke(const struct perf_hook_desc *desc)
{
	if (!(desc && desc->p_hook_func && *desc->p_hook_func))
		return;

	if (sigsetjmp(jmpbuf, 1)) {
		pr_warning("Fatal error (SEGFAULT) in perf hook '%s'\n",
			   desc->hook_name);
		*(current_perf_hook->p_hook_func) = NULL;
	} else {
		current_perf_hook = desc;
		(**desc->p_hook_func)(desc->hook_ctx);
	}
	current_perf_hook = NULL;
}

void perf_hooks__recover(void)
{
	if (current_perf_hook)
		siglongjmp(jmpbuf, 1);
}

#define PERF_HOOK(name)					\
perf_hook_func_t __perf_hook_func_##name = NULL;	\
struct perf_hook_desc __perf_hook_desc_##name =		\
	{.hook_name = #name,				\
	 .p_hook_func = &__perf_hook_func_##name,	\
	 .hook_ctx = NULL};
#include "perf-hooks-list.h"
#undef PERF_HOOK

#define PERF_HOOK(name)		\
	&__perf_hook_desc_##name,

static struct perf_hook_desc *perf_hooks[] = {
#include "perf-hooks-list.h"
};
#undef PERF_HOOK

int perf_hooks__set_hook(const char *hook_name,
			 perf_hook_func_t hook_func,
			 void *hook_ctx)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(perf_hooks); i++) {
		if (strcmp(hook_name, perf_hooks[i]->hook_name) != 0)
			continue;

		if (*(perf_hooks[i]->p_hook_func))
			pr_warning("Overwrite existing hook: %s\n", hook_name);
		*(perf_hooks[i]->p_hook_func) = hook_func;
		perf_hooks[i]->hook_ctx = hook_ctx;
		return 0;
	}
	return -ENOENT;
}

perf_hook_func_t perf_hooks__get_hook(const char *hook_name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(perf_hooks); i++) {
		if (strcmp(hook_name, perf_hooks[i]->hook_name) != 0)
			continue;

		return *(perf_hooks[i]->p_hook_func);
	}
	return ERR_PTR(-ENOENT);
}
