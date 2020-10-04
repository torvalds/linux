// SPDX-License-Identifier: GPL-2.0
#ifndef __TRACE_SYNTH_H
#define __TRACE_SYNTH_H

#include "trace_dynevent.h"

#define SYNTH_SYSTEM		"synthetic"
#define SYNTH_FIELDS_MAX	32

#define STR_VAR_LEN_MAX		MAX_FILTER_STR_VAL /* must be multiple of sizeof(u64) */

struct synth_field {
	char *type;
	char *name;
	size_t size;
	unsigned int offset;
	bool is_signed;
	bool is_string;
};

struct synth_event {
	struct dyn_event			devent;
	int					ref;
	char					*name;
	struct synth_field			**fields;
	unsigned int				n_fields;
	unsigned int				n_u64;
	struct trace_event_class		class;
	struct trace_event_call			call;
	struct tracepoint			*tp;
	struct module				*mod;
};

extern struct synth_event *find_synth_event(const char *name);

#endif /* __TRACE_SYNTH_H */
