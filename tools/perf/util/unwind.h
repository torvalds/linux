#ifndef __UNWIND_H
#define __UNWIND_H

#include <linux/types.h>
#include "event.h"
#include "symbol.h"
#include "thread.h"

struct unwind_entry {
	struct map	*map;
	struct symbol	*sym;
	u64		ip;
};

typedef int (*unwind_entry_cb_t)(struct unwind_entry *entry, void *arg);

#ifdef HAVE_DWARF_UNWIND_SUPPORT
int unwind__get_entries(unwind_entry_cb_t cb, void *arg,
			struct machine *machine,
			struct thread *thread,
			struct perf_sample *data, int max_stack);
/* libunwind specific */
#ifdef HAVE_LIBUNWIND_SUPPORT
int libunwind__arch_reg_id(int regnum);
int unwind__prepare_access(struct thread *thread);
void unwind__finish_access(struct thread *thread);
#else
static inline int unwind__prepare_access(struct thread *thread __maybe_unused)
{
	return 0;
}

static inline void unwind__finish_access(struct thread *thread __maybe_unused) {}
#endif
#else
static inline int
unwind__get_entries(unwind_entry_cb_t cb __maybe_unused,
		    void *arg __maybe_unused,
		    struct machine *machine __maybe_unused,
		    struct thread *thread __maybe_unused,
		    struct perf_sample *data __maybe_unused,
		    int max_stack __maybe_unused)
{
	return 0;
}

static inline int unwind__prepare_access(struct thread *thread __maybe_unused)
{
	return 0;
}

static inline void unwind__finish_access(struct thread *thread __maybe_unused) {}
#endif /* HAVE_DWARF_UNWIND_SUPPORT */
#endif /* __UNWIND_H */
