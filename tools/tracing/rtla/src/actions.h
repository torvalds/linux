/* SPDX-License-Identifier: GPL-2.0 */
#include <tracefs.h>
#include <stdbool.h>

enum action_type {
	ACTION_NONE = 0,
	ACTION_TRACE_OUTPUT,
	ACTION_SIGNAL,
	ACTION_SHELL,
	ACTION_CONTINUE,
	ACTION_FIELD_N
};

struct action {
	enum action_type type;
	union {
		struct {
			/* For ACTION_TRACE_OUTPUT */
			char *trace_output;
		};
		struct {
			/* For ACTION_SIGNAL */
			int signal;
			int pid;
		};
		struct {
			/* For ACTION_SHELL */
			char *command;
		};
	};
};

static const int action_default_size = 8;

struct actions {
	struct action *list;
	int len, size;
	bool present[ACTION_FIELD_N];
	bool continue_flag;

	/* External dependencies */
	struct tracefs_instance *trace_output_inst;
};

void actions_init(struct actions *self);
void actions_destroy(struct actions *self);
int actions_add_trace_output(struct actions *self, const char *trace_output);
int actions_add_signal(struct actions *self, int signal, int pid);
int actions_add_shell(struct actions *self, const char *command);
int actions_add_continue(struct actions *self);
int actions_parse(struct actions *self, const char *trigger, const char *tracefn);
int actions_perform(struct actions *self);
