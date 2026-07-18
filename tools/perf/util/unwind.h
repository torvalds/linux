/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UNWIND_H
#define __UNWIND_H

#include <stdint.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include "map_symbol.h"

struct maps;
struct option;
struct perf_sample;
struct thread;

struct unwind_entry {
	struct map_symbol ms;
	u64		  ip;
};

typedef int (*unwind_entry_cb_t)(struct unwind_entry *entry, void *arg);

int unwind__configure(const char *var, const char *value, void *cb);
int unwind__option(const struct option *opt, const char *arg, int unset);

/*
 * When best_effort is set, don't report errors and fail silently. This could
 * be expanded in the future to be more permissive about things other than
 * error messages.
 */
int unwind__get_entries(unwind_entry_cb_t cb, void *arg,
			struct thread *thread,
			struct perf_sample *data, int max_stack,
			bool best_effort);

#ifdef HAVE_LIBDW_SUPPORT
int libdw__get_entries(unwind_entry_cb_t cb, void *arg,
		       struct thread *thread,
		       struct perf_sample *data, int max_stack,
		       bool best_effort);
#else
#include "debug.h"
static inline int libdw__get_entries(unwind_entry_cb_t cb __maybe_unused, void *arg __maybe_unused,
				     struct thread *thread __maybe_unused,
				     struct perf_sample *data __maybe_unused,
				     int max_stack __maybe_unused,
				     bool best_effort __maybe_unused)
{
	pr_warning_once("Error: libdw dwarf unwinding not built into perf\n");
	return 0;
}
#endif

#ifdef HAVE_LIBUNWIND_SUPPORT
/* libunwind specific */
int libunwind__get_entries(unwind_entry_cb_t cb, void *arg,
			   struct thread *thread,
			   struct perf_sample *data, int max_stack,
			   bool best_effort);
int unwind__prepare_access(struct maps *maps, uint16_t e_machine);
void unwind__flush_access(struct maps *maps);
void unwind__finish_access(struct maps *maps);
#else
#include "debug.h"
static inline int libunwind__get_entries(unwind_entry_cb_t cb __maybe_unused,
					 void *arg __maybe_unused,
					 struct thread *thread __maybe_unused,
					 struct perf_sample *data __maybe_unused,
					 int max_stack __maybe_unused,
					 bool best_effort __maybe_unused)
{
	pr_warning_once("Error: libunwind dwarf unwinding not built into perf\n");
	return 0;
}

static inline int unwind__prepare_access(struct maps *maps __maybe_unused,
					 uint16_t e_machine __maybe_unused)
{
	return 0;
}

static inline void unwind__flush_access(struct maps *maps __maybe_unused) {}
static inline void unwind__finish_access(struct maps *maps __maybe_unused) {}
#endif

#endif /* __UNWIND_H */
