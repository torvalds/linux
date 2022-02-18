// SPDX-License-Identifier: GPL-2.0
#include <tracefs.h>
#include <stddef.h>

struct trace_instance {
	struct tracefs_instance		*inst;
	struct tep_handle		*tep;
	struct trace_seq		*seq;
};

int trace_instance_init(struct trace_instance *trace, char *tool_name);
int trace_instance_start(struct trace_instance *trace);
void trace_instance_destroy(struct trace_instance *trace);

struct trace_seq *get_trace_seq(void);
int enable_tracer_by_name(struct tracefs_instance *inst, const char *tracer_name);
void disable_tracer(struct tracefs_instance *inst);

int enable_osnoise(struct trace_instance *trace);
int enable_timerlat(struct trace_instance *trace);

struct tracefs_instance *create_instance(char *instance_name);
void destroy_instance(struct tracefs_instance *inst);

int save_trace_to_file(struct tracefs_instance *inst, const char *filename);
int collect_registered_events(struct tep_event *tep, struct tep_record *record,
			      int cpu, void *context);
