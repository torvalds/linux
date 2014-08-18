#ifndef __PERF_UNWIND_LIBDW_H
#define __PERF_UNWIND_LIBDW_H

#include <elfutils/libdwfl.h>
#include "event.h"
#include "thread.h"
#include "unwind.h"

bool libdw__arch_set_initial_registers(Dwfl_Thread *thread, void *arg);

struct unwind_info {
	Dwfl			*dwfl;
	struct perf_sample      *sample;
	struct machine          *machine;
	struct thread           *thread;
	unwind_entry_cb_t	cb;
	void			*arg;
	int			max_stack;
};

#endif /* __PERF_UNWIND_LIBDW_H */
