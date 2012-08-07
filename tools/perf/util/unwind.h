#ifndef __UNWIND_H
#define __UNWIND_H

#include "types.h"
#include "event.h"
#include "symbol.h"

struct unwind_entry {
	struct map	*map;
	struct symbol	*sym;
	u64		ip;
};

typedef int (*unwind_entry_cb_t)(struct unwind_entry *entry, void *arg);

#ifndef NO_LIBUNWIND_SUPPORT
int unwind__get_entries(unwind_entry_cb_t cb, void *arg,
			struct machine *machine,
			struct thread *thread,
			u64 sample_uregs,
			struct perf_sample *data);
int unwind__arch_reg_id(int regnum);
#else
static inline int
unwind__get_entries(unwind_entry_cb_t cb __used, void *arg __used,
		    struct machine *machine __used,
		    struct thread *thread __used,
		    u64 sample_uregs __used,
		    struct perf_sample *data __used)
{
	return 0;
}
#endif /* NO_LIBUNWIND_SUPPORT */
#endif /* __UNWIND_H */
