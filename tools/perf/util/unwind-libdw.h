/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_UNWIND_LIBDW_H
#define __PERF_UNWIND_LIBDW_H

#include <elfutils/libdwfl.h>
#include "unwind.h"

struct machine;
struct perf_sample;
struct thread;

bool libdw_set_initial_registers_mips(Dwfl_Thread *thread, void *arg);
bool libdw_set_initial_registers_s390(Dwfl_Thread *thread, void *arg);

struct unwind_info {
	Dwfl			*dwfl;
	struct perf_sample      *sample;
	struct machine          *machine;
	struct thread           *thread;
	unwind_entry_cb_t	cb;
	void			*arg;
	int			max_stack;
	int			idx;
	uint16_t		e_machine;
	bool			best_effort;
	struct unwind_entry	entries[];
};

#endif /* __PERF_UNWIND_LIBDW_H */
