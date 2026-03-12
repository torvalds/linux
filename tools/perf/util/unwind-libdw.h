/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_UNWIND_LIBDW_H
#define __PERF_UNWIND_LIBDW_H

#include <stdint.h>
#include "unwind.h"

struct machine;
struct perf_sample;
struct thread;

#ifdef HAVE_LIBDW_SUPPORT

struct unwind_info {
	void			*dwfl;
	struct perf_sample      *sample;
	struct machine          *machine;
	struct thread           *thread;
	unwind_entry_cb_t	cb;
	void			*arg;
	int			max_stack;
	int			idx;
	uint32_t		e_flags;
	uint16_t		e_machine;
	bool			best_effort;
	struct unwind_entry	entries[];
};

void libdw__invalidate_dwfl(struct maps *maps, void *dwfl);
#endif

#endif /* __PERF_UNWIND_LIBDW_H */
