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
#include <stdbool.h>
#include <errno.h>
#include <linux/bitmap.h>

#include "../../perf.h"
#include "../debug.h"
#include "../callchain.h"
#include "../evsel.h"
#include "../util.h"
#include "../event.h"
#include "../thread.h"
#include "../comm.h"
#include "../machine.h"
#include "../db-export.h"
#include "../thread-stack.h"
#include "../trace-event.h"
#include "../machine.h"

PyMODINIT_FUNC initperf_trace_context(void);

#define TRACE_EVENT_TYPE_MAX				\
	((1 << (sizeof(unsigned short) * 8)) - 1)

static DECLARE_BITMAP(events_defined, TRACE_EVENT_TYPE_MAX);

#define MAX_FIELDS	64
#define N_COMMON_FIELDS	7

extern struct scripting_context *scripting_context;

static char *cur_field_name;
static int zero_flag_atom;

static PyObject *main_module, *main_dict;

struct tables {
	struct db_export	dbe;
	PyObject		*evsel_handler;
	PyObject		*machine_handler;
	PyObject		*thread_handler;
	PyObject		*comm_handler;
	PyObject		*comm_thread_handler;
	PyObject		*dso_handler;
	PyObject		*symbol_handler;
	PyObject		*branch_type_handler;
	PyObject		*sample_handler;
	PyObject		*call_path_handler;
	PyObject		*call_return_handler;
	bool			db_export_mode;
};

static struct tables tables_global;

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

static PyObject *get_handler(const char *handler_name)
{
	PyObject *handler;

	handler = PyDict_GetItemString(main_dict, handler_name);
	if (handler && !PyCallable_Check(handler))
		return NULL;
	return handler;
}

static void call_object(PyObject *handler, PyObject *args, const char *die_msg)
{
	PyObject *retval;

	retval = PyObject_CallObject(handler, args);
	if (retval == NULL)
		handler_call_die(die_msg);
	Py_DECREF(retval);
}

static void try_call_object(const char *handler_name, PyObject *args)
{
	PyObject *handler;

	handler = get_handler(handler_name);
	if (handler)
		call_object(handler, args, handler_name);
}

static void define_value(enum print_arg_type field_type,
			 const char *ev_name,
			 const char *field_name,
			 const char *field_value,
			 const char *field_str)
{
	const char *handler_name = "define_flag_value";
	PyObject *t;
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

	try_call_object(handler_name, t);

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
	PyObject *t;
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

	try_call_object(handler_name, t);

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
	case PRINT_INT_ARRAY:
		define_event_symbols(event, ev_name, args->int_array.field);
		define_event_symbols(event, ev_name, args->int_array.count);
		define_event_symbols(event, ev_name, args->int_array.el_size);
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
	case PRINT_DYNAMIC_ARRAY_LEN:
	case PRINT_FUNC:
	case PRINT_BITMASK:
		/* we should warn... */
		return;
	}

	if (args->next)
		define_event_symbols(event, ev_name, args->next);
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

	if (thread__resolve_callchain(al->thread, evsel,
				      sample, NULL, NULL,
				      scripting_max_stack) != 0) {
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
				      struct addr_location *al)
{
	struct event_format *event = evsel->tp_format;
	PyObject *handler, *context, *t, *obj, *callchain;
	PyObject *dict = NULL;
	static char handler_name[256];
	struct format_field *field;
	unsigned long s, ns;
	unsigned n = 0;
	int pid;
	int cpu = sample->cpu;
	void *data = sample->raw_data;
	unsigned long long nsecs = sample->time;
	const char *comm = thread__comm_str(al->thread);

	t = PyTuple_New(MAX_FIELDS);
	if (!t)
		Py_FatalError("couldn't create Python tuple");

	if (!event)
		die("ug! no event found for type %d", (int)evsel->attr.config);

	pid = raw_field_value(event, "common_pid", data);

	sprintf(handler_name, "%s__%s", event->system, event->name);

	if (!test_and_set_bit(event->id, events_defined))
		define_event_symbols(event, handler_name, event->print_fmt.args);

	handler = get_handler(handler_name);
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
		call_object(handler, t, handler_name);
	} else {
		try_call_object("trace_unhandled", t);
		Py_DECREF(dict);
	}

	Py_DECREF(t);
}

static PyObject *tuple_new(unsigned int sz)
{
	PyObject *t;

	t = PyTuple_New(sz);
	if (!t)
		Py_FatalError("couldn't create Python tuple");
	return t;
}

static int tuple_set_u64(PyObject *t, unsigned int pos, u64 val)
{
#if BITS_PER_LONG == 64
	return PyTuple_SetItem(t, pos, PyInt_FromLong(val));
#endif
#if BITS_PER_LONG == 32
	return PyTuple_SetItem(t, pos, PyLong_FromLongLong(val));
#endif
}

static int tuple_set_s32(PyObject *t, unsigned int pos, s32 val)
{
	return PyTuple_SetItem(t, pos, PyInt_FromLong(val));
}

static int tuple_set_string(PyObject *t, unsigned int pos, const char *s)
{
	return PyTuple_SetItem(t, pos, PyString_FromString(s));
}

static int python_export_evsel(struct db_export *dbe, struct perf_evsel *evsel)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(2);

	tuple_set_u64(t, 0, evsel->db_id);
	tuple_set_string(t, 1, perf_evsel__name(evsel));

	call_object(tables->evsel_handler, t, "evsel_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_machine(struct db_export *dbe,
				 struct machine *machine)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(3);

	tuple_set_u64(t, 0, machine->db_id);
	tuple_set_s32(t, 1, machine->pid);
	tuple_set_string(t, 2, machine->root_dir ? machine->root_dir : "");

	call_object(tables->machine_handler, t, "machine_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_thread(struct db_export *dbe, struct thread *thread,
				u64 main_thread_db_id, struct machine *machine)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(5);

	tuple_set_u64(t, 0, thread->db_id);
	tuple_set_u64(t, 1, machine->db_id);
	tuple_set_u64(t, 2, main_thread_db_id);
	tuple_set_s32(t, 3, thread->pid_);
	tuple_set_s32(t, 4, thread->tid);

	call_object(tables->thread_handler, t, "thread_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_comm(struct db_export *dbe, struct comm *comm)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(2);

	tuple_set_u64(t, 0, comm->db_id);
	tuple_set_string(t, 1, comm__str(comm));

	call_object(tables->comm_handler, t, "comm_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_comm_thread(struct db_export *dbe, u64 db_id,
				     struct comm *comm, struct thread *thread)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(3);

	tuple_set_u64(t, 0, db_id);
	tuple_set_u64(t, 1, comm->db_id);
	tuple_set_u64(t, 2, thread->db_id);

	call_object(tables->comm_thread_handler, t, "comm_thread_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_dso(struct db_export *dbe, struct dso *dso,
			     struct machine *machine)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];
	PyObject *t;

	build_id__sprintf(dso->build_id, sizeof(dso->build_id), sbuild_id);

	t = tuple_new(5);

	tuple_set_u64(t, 0, dso->db_id);
	tuple_set_u64(t, 1, machine->db_id);
	tuple_set_string(t, 2, dso->short_name);
	tuple_set_string(t, 3, dso->long_name);
	tuple_set_string(t, 4, sbuild_id);

	call_object(tables->dso_handler, t, "dso_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_symbol(struct db_export *dbe, struct symbol *sym,
				struct dso *dso)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	u64 *sym_db_id = symbol__priv(sym);
	PyObject *t;

	t = tuple_new(6);

	tuple_set_u64(t, 0, *sym_db_id);
	tuple_set_u64(t, 1, dso->db_id);
	tuple_set_u64(t, 2, sym->start);
	tuple_set_u64(t, 3, sym->end);
	tuple_set_s32(t, 4, sym->binding);
	tuple_set_string(t, 5, sym->name);

	call_object(tables->symbol_handler, t, "symbol_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_branch_type(struct db_export *dbe, u32 branch_type,
				     const char *name)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(2);

	tuple_set_s32(t, 0, branch_type);
	tuple_set_string(t, 1, name);

	call_object(tables->branch_type_handler, t, "branch_type_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_sample(struct db_export *dbe,
				struct export_sample *es)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(21);

	tuple_set_u64(t, 0, es->db_id);
	tuple_set_u64(t, 1, es->evsel->db_id);
	tuple_set_u64(t, 2, es->al->machine->db_id);
	tuple_set_u64(t, 3, es->al->thread->db_id);
	tuple_set_u64(t, 4, es->comm_db_id);
	tuple_set_u64(t, 5, es->dso_db_id);
	tuple_set_u64(t, 6, es->sym_db_id);
	tuple_set_u64(t, 7, es->offset);
	tuple_set_u64(t, 8, es->sample->ip);
	tuple_set_u64(t, 9, es->sample->time);
	tuple_set_s32(t, 10, es->sample->cpu);
	tuple_set_u64(t, 11, es->addr_dso_db_id);
	tuple_set_u64(t, 12, es->addr_sym_db_id);
	tuple_set_u64(t, 13, es->addr_offset);
	tuple_set_u64(t, 14, es->sample->addr);
	tuple_set_u64(t, 15, es->sample->period);
	tuple_set_u64(t, 16, es->sample->weight);
	tuple_set_u64(t, 17, es->sample->transaction);
	tuple_set_u64(t, 18, es->sample->data_src);
	tuple_set_s32(t, 19, es->sample->flags & PERF_BRANCH_MASK);
	tuple_set_s32(t, 20, !!(es->sample->flags & PERF_IP_FLAG_IN_TX));

	call_object(tables->sample_handler, t, "sample_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_call_path(struct db_export *dbe, struct call_path *cp)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;
	u64 parent_db_id, sym_db_id;

	parent_db_id = cp->parent ? cp->parent->db_id : 0;
	sym_db_id = cp->sym ? *(u64 *)symbol__priv(cp->sym) : 0;

	t = tuple_new(4);

	tuple_set_u64(t, 0, cp->db_id);
	tuple_set_u64(t, 1, parent_db_id);
	tuple_set_u64(t, 2, sym_db_id);
	tuple_set_u64(t, 3, cp->ip);

	call_object(tables->call_path_handler, t, "call_path_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_call_return(struct db_export *dbe,
				     struct call_return *cr)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	u64 comm_db_id = cr->comm ? cr->comm->db_id : 0;
	PyObject *t;

	t = tuple_new(11);

	tuple_set_u64(t, 0, cr->db_id);
	tuple_set_u64(t, 1, cr->thread->db_id);
	tuple_set_u64(t, 2, comm_db_id);
	tuple_set_u64(t, 3, cr->cp->db_id);
	tuple_set_u64(t, 4, cr->call_time);
	tuple_set_u64(t, 5, cr->return_time);
	tuple_set_u64(t, 6, cr->branch_count);
	tuple_set_u64(t, 7, cr->call_ref);
	tuple_set_u64(t, 8, cr->return_ref);
	tuple_set_u64(t, 9, cr->cp->parent->db_id);
	tuple_set_s32(t, 10, cr->flags);

	call_object(tables->call_return_handler, t, "call_return_table");

	Py_DECREF(t);

	return 0;
}

static int python_process_call_return(struct call_return *cr, void *data)
{
	struct db_export *dbe = data;

	return db_export__call_return(dbe, cr);
}

static void python_process_general_event(struct perf_sample *sample,
					 struct perf_evsel *evsel,
					 struct addr_location *al)
{
	PyObject *handler, *t, *dict, *callchain, *dict_sample;
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

	handler = get_handler(handler_name);
	if (!handler)
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
			PyString_FromString(thread__comm_str(al->thread)));
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

	call_object(handler, t, handler_name);
exit:
	Py_DECREF(dict);
	Py_DECREF(t);
}

static void python_process_event(union perf_event *event,
				 struct perf_sample *sample,
				 struct perf_evsel *evsel,
				 struct addr_location *al)
{
	struct tables *tables = &tables_global;

	switch (evsel->attr.type) {
	case PERF_TYPE_TRACEPOINT:
		python_process_tracepoint(sample, evsel, al);
		break;
	/* Reserve for future process_hw/sw/raw APIs */
	default:
		if (tables->db_export_mode)
			db_export__sample(&tables->dbe, event, sample, evsel, al);
		else
			python_process_general_event(sample, evsel, al);
	}
}

static int run_start_sub(void)
{
	main_module = PyImport_AddModule("__main__");
	if (main_module == NULL)
		return -1;
	Py_INCREF(main_module);

	main_dict = PyModule_GetDict(main_module);
	if (main_dict == NULL)
		goto error;
	Py_INCREF(main_dict);

	try_call_object("trace_begin", NULL);

	return 0;

error:
	Py_XDECREF(main_dict);
	Py_XDECREF(main_module);
	return -1;
}

#define SET_TABLE_HANDLER_(name, handler_name, table_name) do {		\
	tables->handler_name = get_handler(#table_name);		\
	if (tables->handler_name)					\
		tables->dbe.export_ ## name = python_export_ ## name;	\
} while (0)

#define SET_TABLE_HANDLER(name) \
	SET_TABLE_HANDLER_(name, name ## _handler, name ## _table)

static void set_table_handlers(struct tables *tables)
{
	const char *perf_db_export_mode = "perf_db_export_mode";
	const char *perf_db_export_calls = "perf_db_export_calls";
	PyObject *db_export_mode, *db_export_calls;
	bool export_calls = false;
	int ret;

	memset(tables, 0, sizeof(struct tables));
	if (db_export__init(&tables->dbe))
		Py_FatalError("failed to initialize export");

	db_export_mode = PyDict_GetItemString(main_dict, perf_db_export_mode);
	if (!db_export_mode)
		return;

	ret = PyObject_IsTrue(db_export_mode);
	if (ret == -1)
		handler_call_die(perf_db_export_mode);
	if (!ret)
		return;

	tables->dbe.crp = NULL;
	db_export_calls = PyDict_GetItemString(main_dict, perf_db_export_calls);
	if (db_export_calls) {
		ret = PyObject_IsTrue(db_export_calls);
		if (ret == -1)
			handler_call_die(perf_db_export_calls);
		export_calls = !!ret;
	}

	if (export_calls) {
		tables->dbe.crp =
			call_return_processor__new(python_process_call_return,
						   &tables->dbe);
		if (!tables->dbe.crp)
			Py_FatalError("failed to create calls processor");
	}

	tables->db_export_mode = true;
	/*
	 * Reserve per symbol space for symbol->db_id via symbol__priv()
	 */
	symbol_conf.priv_size = sizeof(u64);

	SET_TABLE_HANDLER(evsel);
	SET_TABLE_HANDLER(machine);
	SET_TABLE_HANDLER(thread);
	SET_TABLE_HANDLER(comm);
	SET_TABLE_HANDLER(comm_thread);
	SET_TABLE_HANDLER(dso);
	SET_TABLE_HANDLER(symbol);
	SET_TABLE_HANDLER(branch_type);
	SET_TABLE_HANDLER(sample);
	SET_TABLE_HANDLER(call_path);
	SET_TABLE_HANDLER(call_return);
}

/*
 * Start trace script
 */
static int python_start_script(const char *script, int argc, const char **argv)
{
	struct tables *tables = &tables_global;
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

	set_table_handlers(tables);

	if (tables->db_export_mode) {
		err = db_export__branch_types(&tables->dbe);
		if (err)
			goto error;
	}

	return err;
error:
	Py_Finalize();
	free(command_line);

	return err;
}

static int python_flush_script(void)
{
	struct tables *tables = &tables_global;

	return db_export__flush(&tables->dbe);
}

/*
 * Stop trace script
 */
static int python_stop_script(void)
{
	struct tables *tables = &tables_global;

	try_call_object("trace_end", NULL);

	db_export__exit(&tables->dbe);

	Py_XDECREF(main_dict);
	Py_XDECREF(main_module);
	Py_Finalize();

	return 0;
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
	.flush_script = python_flush_script,
	.stop_script = python_stop_script,
	.process_event = python_process_event,
	.generate_script = python_generate_script,
};
