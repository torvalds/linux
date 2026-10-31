// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/kernel.h>
#include <event-parse.h>
#include <api/fs/tracing_path.h>
#include <api/fs/fs.h>
#include "trace-event.h"
#include "machine.h"

/*
 * Global trace_event object used by trace_event__tp_format. It caches the
 * tracepoint formats of the running kernel for the lifetime of the command
 * and is released by trace_event__exit.
 */
static struct trace_event tevent;
static bool tevent_initialized;

int trace_event__init(struct trace_event *t)
{
	struct tep_handle *pevent;

	trace_event__cleanup(t);

	pevent = tep_alloc();

	if (pevent) {
		t->plugin_list = tep_load_plugins(pevent);
		t->pevent  = pevent;
	}

	return pevent ? 0 : -1;
}

static int trace_event__init2(void)
{
	int be = tep_is_bigendian();
	struct tep_handle *pevent;

	if (trace_event__init(&tevent))
		return -1;

	pevent = tevent.pevent;
	tep_set_flag(pevent, TEP_NSEC_OUTPUT);
	tep_set_file_bigendian(pevent, be);
	tep_set_local_bigendian(pevent, be);
	tevent_initialized = true;
	return 0;
}

int trace_event__register_resolver(struct machine *machine,
				   tep_func_resolver_t *func)
{
	if (!tevent_initialized && trace_event__init2())
		return -1;

	return tep_set_function_resolver(tevent.pevent, func, machine);
}

void trace_event__cleanup(struct trace_event *t)
{
	if (!t->pevent)
		return;

	tep_unload_plugins(t->plugin_list, t->pevent);
	tep_free(t->pevent);
	t->pevent = NULL;
	t->plugin_list = NULL;
}

/*
 * Release the global trace_event. Called once the command is done, when the
 * tep_event pointers handed out by trace_event__tp_format are no longer in
 * use.
 */
void trace_event__exit(void)
{
	if (!tevent_initialized)
		return;

	trace_event__cleanup(&tevent);
	tevent_initialized = false;
}

/*
 * Returns NULL and sets errno on failure.
 */
static struct tep_event*
tp_format(const char *sys, const char *name)
{
	struct tep_handle *pevent = tevent.pevent;
	struct tep_event *event;
	char *tp_dir;
	char path[PATH_MAX];
	size_t size;
	char *data;
	int err;

	/*
	 * Each parse adds an event to the tep handle that can only be freed
	 * by freeing the whole handle, so re-reading a format file both
	 * repeats the work and grows the handle with a duplicate. Reuse the
	 * event if it was already parsed.
	 */
	event = tep_find_event_by_name(pevent, sys, name);
	if (event)
		return event;

	tp_dir = get_events_file(sys);
	if (!tp_dir) {
		errno = ENOMEM;
		return NULL;
	}

	scnprintf(path, PATH_MAX, "%s/%s/format", tp_dir, name);
	put_events_file(tp_dir);

	err = filename__read_str(path, &data, &size);
	if (err) {
		errno = -err;
		return NULL;
	}

	event = NULL;
	err = tep_parse_format(pevent, &event, data, size, sys);

	free(data);

	/*
	 * A parse failure leaves no event behind, report it rather than
	 * letting a NULL be mistaken for a successfully parsed format.
	 */
	if (err != TEP_ERRNO__SUCCESS || !event) {
		errno = EINVAL;
		return NULL;
	}

	return event;
}

/*
 * Returns NULL and sets errno on failure.
 */
struct tep_event*
trace_event__tp_format(const char *sys, const char *name)
{
	if (!tevent_initialized && trace_event__init2()) {
		errno = ENOMEM;
		return NULL;
	}

	return tp_format(sys, name);
}

/*
 * Returns NULL and sets errno on failure.
 */
struct tep_event *trace_event__tp_format_id(int id)
{
	struct tep_event *event;

	if (!tevent_initialized && trace_event__init2()) {
		errno = ENOMEM;
		return NULL;
	}

	event = tep_find_event(tevent.pevent, id);
	if (!event)
		errno = ENOENT;

	return event;
}
