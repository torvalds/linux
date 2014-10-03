/*
 * trace-event-python.  Feed trace events to an embedded Python interpreter.
 *
 * Copyright (C) 2010 Tom Zanussi <tzanussi@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <Python.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../../perf.h"
#include "../debug.h"
#include "../evsel.h"
#include "../util.h"
#include "../event.h"
#include "../thread.h"
#include "../trace-event.h"
#include "../machine.h"

PyMODINIT_FUNC initperf_trace_context(void);

#define FTRACE_MAX_EVENT				\
	((1 << (sizeof(unsigned short) * 8)) - 1)

struct event_format *events[FTRACE_MAX_EVENT];

#define MAX_FIELDS	64
#define N_COMMON_FIELDS	7

extern struct scripting_context *scripting_context;

static char *cur_field_name;
static int zero_flag_atom;

static PyObject *main_module, *main_dict;

static void handler_call_die(const char *handler_name) NORETURN;
static void handler_call_die(const char *handler_name)
{
	PyErr_Print();
	Py_FatalError("problem in Python trace event handler");
	// Py_FatalError does not return
	// but we have to make the compiler happy
	abort();
}

/*
 * Insert val into into the dictionary and decrement the reference counter.
 * This is necessary for dictionaries since PyDict_SetItemString() does not 
 * steal a reference, as opposed to PyTuple_SetItem().
 */
static void pydict_set_item_string_decref(PyObject *dict, const char *key, PyObject *val)
{
	PyDict_SetItemString(dict, key, val);
	Py_DECREF(val);
}

static void define_value(enum print_arg_type field_type,
			 const char *ev_name,
			 const char *field_name,
			 const char *field_value,
			 const char *field_str)
{
	const char *handler_name = "define_flag_value";
	PyObject *handler, *t, *retval;
	unsigned long long value;
	unsigned n = 0;

	if (field_type == PRINT_SYMBOL)
		handler_name = "define_symbolic_value";

	t = PyTuple_New(4);
	if (!t)
		Py_FatalError("couldn't create Python tuple");

	value = eval_flag(field_value);

	PyTuple_SetItem(t, n++, PyString_FromString(ev_name));
	PyTuple_SetItem(t, n++, PyString_FromString(field_name));
	PyTuple_SetItem(t, n++, PyInt_FromLong(value));
	PyTuple_SetItem(t, n++, PyString_FromString(field_str));

	handler = PyDict_GetItemString(main_dict, handler_name);
	if (handler && PyCallable_Check(handler)) {
		retval = PyObject_CallObject(handler, t);
		if (retval == NULL)
			handler_call_die(handler_name);
		Py_DECREF(retval);
	}

	Py_DECREF(t);
}

static void define_values(enum print_arg_type field_type,
			  struct print_flag_sym *field,
			  const char *ev_name,
			  const char *field_name)
{
	define_value(field_type, ev_name, field_name, field->value,
		     field->str);

	if (field->next)
		define_values(field_type, field->next, ev_name, field_name);
}

static void define_field(enum print_arg_type field_type,
			 const char *ev_name,
			 const char *field_name,
			 const char *delim)
{
	const char *handler_name = "define_flag_field";
	PyObject *handler, *t, *retval;
	unsigned n = 0;

	if (field_type == PRINT_SYMBOL)
		handler_name = "define_symbolic_field";

	if (field_type == PRINT_FLAGS)
		t = PyTuple_New(3);
	else
		t = PyTuple_New(2);
	if (!t)
		Py_FatalError("couldn't create Python tuple");

	PyTuple_SetItem(t, n++, PyString_FromString(ev_name));
	PyTuple_SetItem(t, n++, PyString_FromString(field_name));
	if (field_type == PRINT_FLAGS)
		PyTuple_SetItem(t, n++, PyString_FromString(delim));

	handler = PyDict_GetItemString(main_dict, handler_name);
	if (handler && PyCallable_Check(handler)) {
		retval = PyObject_CallObject(handler, t);
		if (retval == NULL)
			handler_call_die(handler_name);
		Py_DECREF(retval);
	}

	Py_DECREF(t);
}

static void define_event_symbols(struct event_format *event,
				 const char *ev_name,
				 struct print_arg *args)
{
	switch (args->type) {
	case PRINT_NULL:
		break;
	case PRINT_ATOM:
		define_value(PRINT_FLAGS, ev_name, cur_field_name, "0",
			     args->atom.atom);
		zero_flag_atom = 0;
		break;
	case PRINT_FIELD:
		free(cur_field_name);
		cur_field_name = strdup(args->field.name);
		break;
	case PRINT_FLAGS:
		define_event_symbols(event, ev_name, args->flags.field);
		define_field(PRINT_FLAGS, ev_name, cur_field_name,
			     args->flags.delim);
		define_values(PRINT_FLAGS, args->flags.flags, ev_name,
			      cur_field_name);
		break;
	case PRINT_SYMBOL:
		define_event_symbols(event, ev_name, args->symbol.field);
		define_field(PRINT_SYMBOL, ev_name, cur_field_name, NULL);
		define_values(PRINT_SYMBOL, args->symbol.symbols, ev_name,
			      cur_field_name);
		break;
	case PRINT_HEX:
		define_event_symbols(event, ev_name, args->hex.field);
		define_event_symbols(event, ev_name, args->hex.size);
		break;
	case PRINT_STRING:
		break;
	case PRINT_TYPE:
		define_event_symbols(event, ev_name, args->typecast.item);
		break;
	case PRINT_OP:
		if (strcmp(args->op.op, ":") == 0)
			zero_flag_atom = 1;
		define_event_symbols(event, ev_name, args->op.left);
		define_event_symbols(event, ev_name, args->op.right);
		break;
	default:
		/* gcc warns for these? */
	case PRINT_BSTRING:
	case PRINT_DYNAMIC_ARRAY:
	case PRINT_FUNC:
	case PRINT_BITMASK:
		/* we should warn... */
		return;
	}

	if (args->next)
		define_event_symbols(event, ev_name, args->next);
}

static inline struct event_format *find_cache_event(struct perf_evsel *evsel)
{
	static char ev_name[256];
	struct event_format *event;
	int type = evsel->attr.config;

	/*
 	 * XXX: Do we really need to cache this since now we have evsel->tp_format
 	 * cached already? Need to re-read this "cache" routine that as well calls
 	 * define_event_symbols() :-\
 	 */
	if (events[type])
		return events[type];

	events[type] = event = evsel->tp_format;
	if (!event)
		return NULL;

	sprintf(ev_name, "%s__%s", event->system, event->name);

	define_event_symbols(event, ev_name, event->print_fmt.args);

	return event;
}

static PyObject *get_field_numeric_entry(struct event_format *event,
		struct format_field *field, void *data)
{
	bool is_array = field->flags & FIELD_IS_ARRAY;
	PyObject *obj, *list = NULL;
	unsigned long long val;
	unsigned int item_size, n_items, i;

	if (is_array) {
		list = PyList_New(field->arraylen);
		item_size = field->size / field->arraylen;
		n_items = field->arraylen;
	} else {
		item_size = field->size;
		n_items = 1;
	}

	for (i = 0; i < n_items; i++) {

		val = read_size(event, data + field->offset + i * item_size,
				item_size);
		if (field->flags & FIELD_IS_SIGNED) {
			if ((long long)val >= LONG_MIN &&
					(long long)val <= LONG_MAX)
				obj = PyInt_FromLong(val);
			else
				obj = PyLong_FromLongLong(val);
		} else {
			if (val <= LONG_MAX)
				obj = PyInt_FromLong(val);
			else
				obj = PyLong_FromUnsignedLongLong(val);
		}
		if (is_array)
			PyList_SET_ITEM(list, i, obj);
	}
	if (is_array)
		obj = list;
	return obj;
}


static PyObject *python_process_callchain(struct perf_sample *sample,
					 struct perf_evsel *evsel,
					 struct addr_location *al)
{
	PyObject *pylist;

	pylist = PyList_New(0);
	if (!pylist)
		Py_FatalError("couldn't create Python list");

	if (!symbol_conf.use_callchain || !sample->callchain)
		goto exit;

	if (machine__resolve_callchain(al->machine, evsel, al->thread,
					   sample, NULL, NULL,
					   PERF_MAX_STACK_DEPTH) != 0) {
		pr_err("Failed to resolve callchain. Skipping\n");
		goto exit;
	}
	callchain_cursor_commit(&callchain_cursor);


	while (1) {
		PyObject *pyelem;
		struct callchain_cursor_node *node;
		node = callchain_cursor_current(&callchain_cursor);
		if (!node)
			break;

		pyelem = PyDict_New();
		if (!pyelem)
			Py_FatalError("couldn't create Python dictionary");


		pydict_set_item_string_decref(pyelem, "ip",
				PyLong_FromUnsignedLongLong(node->ip));

		if (node->sym) {
			PyObject *pysym  = PyDict_New();
			if (!pysym)
				Py_FatalError("couldn't create Python dictionary");
			pydict_set_item_string_decref(pysym, "start",
					PyLong_FromUnsignedLongLong(node->sym->start));
			pydict_set_item_string_decref(pysym, "end",
					PyLong_FromUnsignedLongLong(node->sym->end));
			pydict_set_item_string_decref(pysym, "binding",
					PyInt_FromLong(node->sym->binding));
			pydict_set_item_string_decref(pysym, "name",
					PyString_FromStringAndSize(node->sym->name,
							node->sym->namelen));
			pydict_set_item_string_decref(pyelem, "sym", pysym);
		}

		if (node->map) {
			struct map *map = node->map;
			const char *dsoname = "[unknown]";
			if (map && map->dso && (map->dso->name || map->dso->long_name)) {
				if (symbol_conf.show_kernel_path && map->dso->long_name)
					dsoname = map->dso->long_name;
				else if (map->dso->name)
					dsoname = map->dso->name;
			}
			pydict_set_item_string_decref(pyelem, "dso",
					PyString_FromString(dsoname));
		}

		callchain_cursor_advance(&callchain_cursor);
		PyList_Append(pylist, pyelem);
		Py_DECREF(pyelem);
	}

exit:
	return pylist;
}


static void python_process_tracepoint(struct perf_sample *sample,
				      struct perf_evsel *evsel,
				      struct thread *thread,
				      struct addr_location *al)
{
	PyObject *handler, *retval, *context, *t, *obj, *callchain;
	PyObject *dict = NULL;
	static char handler_name[256];
	struct format_field *field;
	unsigned long s, ns;
	struct event_format *event;
	unsigned n = 0;
	int pid;
	int cpu = sample->cpu;
	void *data = sample->raw_data;
	unsigned long long nsecs = sample->time;
	const char *comm = thread__comm_str(thread);

	t = PyTuple_New(MAX_FIELDS);
	if (!t)
		Py_FatalError("couldn't create Python tuple");

	event = find_cache_event(evsel);
	if (!event)
		die("ug! no event found for type %d", (int)evsel->attr.config);

	pid = raw_field_value(event, "common_pid", data);

	sprintf(handler_name, "%s__%s", event->system, event->name);

	handler = PyDict_GetItemString(main_dict, handler_name);
	if (handler && !PyCallable_Check(handler))
		handler = NULL;
	if (!handler) {
		dict = PyDict_New();
		if (!dict)
			Py_FatalError("couldn't create Python dict");
	}
	s = nsecs / NSECS_PER_SEC;
	ns = nsecs - s * NSECS_PER_SEC;

	scripting_context->event_data = data;
	scripting_context->pevent = evsel->tp_format->pevent;

	context = PyCObject_FromVoidPtr(scripting_context, NULL);

	PyTuple_SetItem(t, n++, PyString_FromString(handler_name));
	PyTuple_SetItem(t, n++, context);

	/* ip unwinding */
	callchain = python_process_callchain(sample, evsel, al);

	if (handler) {
		PyTuple_SetItem(t, n++, PyInt_FromLong(cpu));
		PyTuple_SetItem(t, n++, PyInt_FromLong(s));
		PyTuple_SetItem(t, n++, PyInt_FromLong(ns));
		PyTuple_SetItem(t, n++, PyInt_FromLong(pid));
		PyTuple_SetItem(t, n++, PyString_FromString(comm));
		PyTuple_SetItem(t, n++, callchain);
	} else {
		pydict_set_item_string_decref(dict, "common_cpu", PyInt_FromLong(cpu));
		pydict_set_item_string_decref(dict, "common_s", PyInt_FromLong(s));
		pydict_set_item_string_decref(dict, "common_ns", PyInt_FromLong(ns));
		pydict_set_item_string_decref(dict, "common_pid", PyInt_FromLong(pid));
		pydict_set_item_string_decref(dict, "common_comm", PyString_FromString(comm));
		pydict_set_item_string_decref(dict, "common_callchain", callchain);
	}
	for (field = event->format.fields; field; field = field->next) {
		if (field->flags & FIELD_IS_STRING) {
			int offset;
			if (field->flags & FIELD_IS_DYNAMIC) {
				offset = *(int *)(data + field->offset);
				offset &= 0xffff;
			} else
				offset = field->offset;
			obj = PyString_FromString((char *)data + offset);
		} else { /* FIELD_IS_NUMERIC */
			obj = get_field_numeric_entry(event, field, data);
		}
		if (handler)
			PyTuple_SetItem(t, n++, obj);
		else
			pydict_set_item_string_decref(dict, field->name, obj);

	}

	if (!handler)
		PyTuple_SetItem(t, n++, dict);

	if (_PyTuple_Resize(&t, n) == -1)
		Py_FatalError("error resizing Python tuple");

	if (handler) {
		retval = PyObject_CallObject(handler, t);
		if (retval == NULL)
			handler_call_die(handler_name);
		Py_DECREF(retval);
	} else {
		handler = PyDict_GetItemString(main_dict, "trace_unhandled");
		if (handler && PyCallable_Check(handler)) {

			retval = PyObject_CallObject(handler, t);
			if (retval == NULL)
				handler_call_die("trace_unhandled");
			Py_DECREF(retval);
		}
		Py_DECREF(dict);
	}

	Py_DECREF(t);
}

static void python_process_general_event(struct perf_sample *sample,
					 struct perf_evsel *evsel,
					 struct thread *thread,
					 struct addr_location *al)
{
	PyObject *handler, *retval, *t, *dict, *callchain, *dict_sample;
	static char handler_name[64];
	unsigned n = 0;

	/*
	 * Use the MAX_FIELDS to make the function expandable, though
	 * currently there is only one item for the tuple.
	 */
	t = PyTuple_New(MAX_FIELDS);
	if (!t)
		Py_FatalError("couldn't create Python tuple");

	dict = PyDict_New();
	if (!dict)
		Py_FatalError("couldn't create Python dictionary");

	dict_sample = PyDict_New();
	if (!dict_sample)
		Py_FatalError("couldn't create Python dictionary");

	snprintf(handler_name, sizeof(handler_name), "%s", "process_event");

	handler = PyDict_GetItemString(main_dict, handler_name);
	if (!handler || !PyCallable_Check(handler))
		goto exit;

	pydict_set_item_string_decref(dict, "ev_name", PyString_FromString(perf_evsel__name(evsel)));
	pydict_set_item_string_decref(dict, "attr", PyString_FromStringAndSize(
			(const char *)&evsel->attr, sizeof(evsel->attr)));

	pydict_set_item_string_decref(dict_sample, "pid",
			PyInt_FromLong(sample->pid));
	pydict_set_item_string_decref(dict_sample, "tid",
			PyInt_FromLong(sample->tid));
	pydict_set_item_string_decref(dict_sample, "cpu",
			PyInt_FromLong(sample->cpu));
	pydict_set_item_string_decref(dict_sample, "ip",
			PyLong_FromUnsignedLongLong(sample->ip));
	pydict_set_item_string_decref(dict_sample, "time",
			PyLong_FromUnsignedLongLong(sample->time));
	pydict_set_item_string_decref(dict_sample, "period",
			PyLong_FromUnsignedLongLong(sample->period));
	pydict_set_item_string_decref(dict, "sample", dict_sample);

	pydict_set_item_string_decref(dict, "raw_buf", PyString_FromStringAndSize(
			(const char *)sample->raw_data, sample->raw_size));
	pydict_set_item_string_decref(dict, "comm",
			PyString_FromString(thread__comm_str(thread)));
	if (al->map) {
		pydict_set_item_string_decref(dict, "dso",
			PyString_FromString(al->map->dso->name));
	}
	if (al->sym) {
		pydict_set_item_string_decref(dict, "symbol",
			PyString_FromString(al->sym->name));
	}

	/* ip unwinding */
	callchain = python_process_callchain(sample, evsel, al);
	pydict_set_item_string_decref(dict, "callchain", callchain);

	PyTuple_SetItem(t, n++, dict);
	if (_PyTuple_Resize(&t, n) == -1)
		Py_FatalError("error resizing Python tuple");

	retval = PyObject_CallObject(handler, t);
	if (retval == NULL)
		handler_call_die(handler_name);
	Py_DECREF(retval);
exit:
	Py_DECREF(dict);
	Py_DECREF(t);
}

static void python_process_event(union perf_event *event __maybe_unused,
				 struct perf_sample *sample,
				 struct perf_evsel *evsel,
				 struct thread *thread,
				 struct addr_location *al)
{
	switch (evsel->attr.type) {
	case PERF_TYPE_TRACEPOINT:
		python_process_tracepoint(sample, evsel, thread, al);
		break;
	/* Reserve for future process_hw/sw/raw APIs */
	default:
		python_process_general_event(sample, evsel, thread, al);
	}
}

static int run_start_sub(void)
{
	PyObject *handler, *retval;
	int err = 0;

	main_module = PyImport_AddModule("__main__");
	if (main_module == NULL)
		return -1;
	Py_INCREF(main_module);

	main_dict = PyModule_GetDict(main_module);
	if (main_dict == NULL) {
		err = -1;
		goto error;
	}
	Py_INCREF(main_dict);

	handler = PyDict_GetItemString(main_dict, "trace_begin");
	if (handler == NULL || !PyCallable_Check(handler))
		goto out;

	retval = PyObject_CallObject(handler, NULL);
	if (retval == NULL)
		handler_call_die("trace_begin");

	Py_DECREF(retval);
	return err;
error:
	Py_XDECREF(main_dict);
	Py_XDECREF(main_module);
out:
	return err;
}

/*
 * Start trace script
 */
static int python_start_script(const char *script, int argc, const char **argv)
{
	const char **command_line;
	char buf[PATH_MAX];
	int i, err = 0;
	FILE *fp;

	command_line = malloc((argc + 1) * sizeof(const char *));
	command_line[0] = script;
	for (i = 1; i < argc + 1; i++)
		command_line[i] = argv[i - 1];

	Py_Initialize();

	initperf_trace_context();

	PySys_SetArgv(argc + 1, (char **)command_line);

	fp = fopen(script, "r");
	if (!fp) {
		sprintf(buf, "Can't open python script \"%s\"", script);
		perror(buf);
		err = -1;
		goto error;
	}

	err = PyRun_SimpleFile(fp, script);
	if (err) {
		fprintf(stderr, "Error running python script %s\n", script);
		goto error;
	}

	err = run_start_sub();
	if (err) {
		fprintf(stderr, "Error starting python script %s\n", script);
		goto error;
	}

	free(command_line);

	return err;
error:
	Py_Finalize();
	free(command_line);

	return err;
}

/*
 * Stop trace script
 */
static int python_stop_script(void)
{
	PyObject *handler, *retval;
	int err = 0;

	handler = PyDict_GetItemString(main_dict, "trace_end");
	if (handler == NULL || !PyCallable_Check(handler))
		goto out;

	retval = PyObject_CallObject(handler, NULL);
	if (retval == NULL)
		handler_call_die("trace_end");
	Py_DECREF(retval);
out:
	Py_XDECREF(main_dict);
	Py_XDECREF(main_module);
	Py_Finalize();

	return err;
}

static int python_generate_script(struct pevent *pevent, const char *outfile)
{
	struct event_format *event = NULL;
	struct format_field *f;
	char fname[PATH_MAX];
	int not_first, count;
	FILE *ofp;

	sprintf(fname, "%s.py", outfile);
	ofp = fopen(fname, "w");
	if (ofp == NULL) {
		fprintf(stderr, "couldn't open %s\n", fname);
		return -1;
	}
	fprintf(ofp, "# perf script event handlers, "
		"generated by perf script -g python\n");

	fprintf(ofp, "# Licensed under the terms of the GNU GPL"
		" License version 2\n\n");

	fprintf(ofp, "# The common_* event handler fields are the most useful "
		"fields common to\n");

	fprintf(ofp, "# all events.  They don't necessarily correspond to "
		"the 'common_*' fields\n");

	fprintf(ofp, "# in the format files.  Those fields not available as "
		"handler params can\n");

	fprintf(ofp, "# be retrieved using Python functions of the form "
		"common_*(context).\n");

	fprintf(ofp, "# See the perf-trace-python Documentation for the list "
		"of available functions.\n\n");

	fprintf(ofp, "import os\n");
	fprintf(ofp, "import sys\n\n");

	fprintf(ofp, "sys.path.append(os.environ['PERF_EXEC_PATH'] + \\\n");
	fprintf(ofp, "\t'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')\n");
	fprintf(ofp, "\nfrom perf_trace_context import *\n");
	fprintf(ofp, "from Core import *\n\n\n");

	fprintf(ofp, "def trace_begin():\n");
	fprintf(ofp, "\tprint \"in trace_begin\"\n\n");

	fprintf(ofp, "def trace_end():\n");
	fprintf(ofp, "\tprint \"in trace_end\"\n\n");

	while ((event = trace_find_next_event(pevent, event))) {
		fprintf(ofp, "def %s__%s(", event->system, event->name);
		fprintf(ofp, "event_name, ");
		fprintf(ofp, "context, ");
		fprintf(ofp, "common_cpu,\n");
		fprintf(ofp, "\tcommon_secs, ");
		fprintf(ofp, "common_nsecs, ");
		fprintf(ofp, "common_pid, ");
		fprintf(ofp, "common_comm,\n\t");
		fprintf(ofp, "common_callchain, ");

		not_first = 0;
		count = 0;

		for (f = event->format.fields; f; f = f->next) {
			if (not_first++)
				fprintf(ofp, ", ");
			if (++count % 5 == 0)
				fprintf(ofp, "\n\t");

			fprintf(ofp, "%s", f->name);
		}
		fprintf(ofp, "):\n");

		fprintf(ofp, "\t\tprint_header(event_name, common_cpu, "
			"common_secs, common_nsecs,\n\t\t\t"
			"common_pid, common_comm)\n\n");

		fprintf(ofp, "\t\tprint \"");

		not_first = 0;
		count = 0;

		for (f = event->format.fields; f; f = f->next) {
			if (not_first++)
				fprintf(ofp, ", ");
			if (count && count % 3 == 0) {
				fprintf(ofp, "\" \\\n\t\t\"");
			}
			count++;

			fprintf(ofp, "%s=", f->name);
			if (f->flags & FIELD_IS_STRING ||
			    f->flags & FIELD_IS_FLAG ||
			    f->flags & FIELD_IS_ARRAY ||
			    f->flags & FIELD_IS_SYMBOLIC)
				fprintf(ofp, "%%s");
			else if (f->flags & FIELD_IS_SIGNED)
				fprintf(ofp, "%%d");
			else
				fprintf(ofp, "%%u");
		}

		fprintf(ofp, "\" %% \\\n\t\t(");

		not_first = 0;
		count = 0;

		for (f = event->format.fields; f; f = f->next) {
			if (not_first++)
				fprintf(ofp, ", ");

			if (++count % 5 == 0)
				fprintf(ofp, "\n\t\t");

			if (f->flags & FIELD_IS_FLAG) {
				if ((count - 1) % 5 != 0) {
					fprintf(ofp, "\n\t\t");
					count = 4;
				}
				fprintf(ofp, "flag_str(\"");
				fprintf(ofp, "%s__%s\", ", event->system,
					event->name);
				fprintf(ofp, "\"%s\", %s)", f->name,
					f->name);
			} else if (f->flags & FIELD_IS_SYMBOLIC) {
				if ((count - 1) % 5 != 0) {
					fprintf(ofp, "\n\t\t");
					count = 4;
				}
				fprintf(ofp, "symbol_str(\"");
				fprintf(ofp, "%s__%s\", ", event->system,
					event->name);
				fprintf(ofp, "\"%s\", %s)", f->name,
					f->name);
			} else
				fprintf(ofp, "%s", f->name);
		}

		fprintf(ofp, ")\n\n");

		fprintf(ofp, "\t\tfor node in common_callchain:");
		fprintf(ofp, "\n\t\t\tif 'sym' in node:");
		fprintf(ofp, "\n\t\t\t\tprint \"\\t[%%x] %%s\" %% (node['ip'], node['sym']['name'])");
		fprintf(ofp, "\n\t\t\telse:");
		fprintf(ofp, "\n\t\t\t\tprint \"\t[%%x]\" %% (node['ip'])\n\n");
		fprintf(ofp, "\t\tprint \"\\n\"\n\n");

	}

	fprintf(ofp, "def trace_unhandled(event_name, context, "
		"event_fields_dict):\n");

	fprintf(ofp, "\t\tprint ' '.join(['%%s=%%s'%%(k,str(v))"
		"for k,v in sorted(event_fields_dict.items())])\n\n");

	fprintf(ofp, "def print_header("
		"event_name, cpu, secs, nsecs, pid, comm):\n"
		"\tprint \"%%-20s %%5u %%05u.%%09u %%8u %%-20s \" %% \\\n\t"
		"(event_name, cpu, secs, nsecs, pid, comm),\n");

	fclose(ofp);

	fprintf(stderr, "generated Python script: %s\n", fname);

	return 0;
}

struct scripting_ops python_scripting_ops = {
	.name = "Python",
	.start_script = python_start_script,
	.stop_script = python_stop_script,
	.process_event = python_process_event,
	.generate_script = python_generate_script,
};
