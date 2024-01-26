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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <linux/bitmap.h>
#include <linux/compiler.h>
#include <linux/time64.h>
#ifdef HAVE_LIBTRACEEVENT
#include <traceevent/event-parse.h>
#endif

#include "../build-id.h"
#include "../counts.h"
#include "../debug.h"
#include "../dso.h"
#include "../callchain.h"
#include "../env.h"
#include "../evsel.h"
#include "../event.h"
#include "../thread.h"
#include "../comm.h"
#include "../machine.h"
#include "../db-export.h"
#include "../thread-stack.h"
#include "../trace-event.h"
#include "../call-path.h"
#include "map.h"
#include "symbol.h"
#include "thread_map.h"
#include "print_binary.h"
#include "stat.h"
#include "mem-events.h"
#include "util/perf_regs.h"

#if PY_MAJOR_VERSION < 3
#define _PyUnicode_FromString(arg) \
  PyString_FromString(arg)
#define _PyUnicode_FromStringAndSize(arg1, arg2) \
  PyString_FromStringAndSize((arg1), (arg2))
#define _PyBytes_FromStringAndSize(arg1, arg2) \
  PyString_FromStringAndSize((arg1), (arg2))
#define _PyLong_FromLong(arg) \
  PyInt_FromLong(arg)
#define _PyLong_AsLong(arg) \
  PyInt_AsLong(arg)
#define _PyCapsule_New(arg1, arg2, arg3) \
  PyCObject_FromVoidPtr((arg1), (arg2))

PyMODINIT_FUNC initperf_trace_context(void);
#else
#define _PyUnicode_FromString(arg) \
  PyUnicode_FromString(arg)
#define _PyUnicode_FromStringAndSize(arg1, arg2) \
  PyUnicode_FromStringAndSize((arg1), (arg2))
#define _PyBytes_FromStringAndSize(arg1, arg2) \
  PyBytes_FromStringAndSize((arg1), (arg2))
#define _PyLong_FromLong(arg) \
  PyLong_FromLong(arg)
#define _PyLong_AsLong(arg) \
  PyLong_AsLong(arg)
#define _PyCapsule_New(arg1, arg2, arg3) \
  PyCapsule_New((arg1), (arg2), (arg3))

PyMODINIT_FUNC PyInit_perf_trace_context(void);
#endif

#ifdef HAVE_LIBTRACEEVENT
#define TRACE_EVENT_TYPE_MAX				\
	((1 << (sizeof(unsigned short) * 8)) - 1)

#define N_COMMON_FIELDS	7

static char *cur_field_name;
static int zero_flag_atom;
#endif

#define MAX_FIELDS	64

extern struct scripting_context *scripting_context;

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
	PyObject		*synth_handler;
	PyObject		*context_switch_handler;
	bool			db_export_mode;
};

static struct tables tables_global;

static void handler_call_die(const char *handler_name) __noreturn;
static void handler_call_die(const char *handler_name)
{
	PyErr_Print();
	Py_FatalError("problem in Python trace event handler");
	// Py_FatalError does not return
	// but we have to make the compiler happy
	abort();
}

/*
 * Insert val into the dictionary and decrement the reference counter.
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

#ifdef HAVE_LIBTRACEEVENT
static int get_argument_count(PyObject *handler)
{
	int arg_count = 0;

	/*
	 * The attribute for the code object is func_code in Python 2,
	 * whereas it is __code__ in Python 3.0+.
	 */
	PyObject *code_obj = PyObject_GetAttrString(handler,
		"func_code");
	if (PyErr_Occurred()) {
		PyErr_Clear();
		code_obj = PyObject_GetAttrString(handler,
			"__code__");
	}
	PyErr_Clear();
	if (code_obj) {
		PyObject *arg_count_obj = PyObject_GetAttrString(code_obj,
			"co_argcount");
		if (arg_count_obj) {
			arg_count = (int) _PyLong_AsLong(arg_count_obj);
			Py_DECREF(arg_count_obj);
		}
		Py_DECREF(code_obj);
	}
	return arg_count;
}

static void define_value(enum tep_print_arg_type field_type,
			 const char *ev_name,
			 const char *field_name,
			 const char *field_value,
			 const char *field_str)
{
	const char *handler_name = "define_flag_value";
	PyObject *t;
	unsigned long long value;
	unsigned n = 0;

	if (field_type == TEP_PRINT_SYMBOL)
		handler_name = "define_symbolic_value";

	t = PyTuple_New(4);
	if (!t)
		Py_FatalError("couldn't create Python tuple");

	value = eval_flag(field_value);

	PyTuple_SetItem(t, n++, _PyUnicode_FromString(ev_name));
	PyTuple_SetItem(t, n++, _PyUnicode_FromString(field_name));
	PyTuple_SetItem(t, n++, _PyLong_FromLong(value));
	PyTuple_SetItem(t, n++, _PyUnicode_FromString(field_str));

	try_call_object(handler_name, t);

	Py_DECREF(t);
}

static void define_values(enum tep_print_arg_type field_type,
			  struct tep_print_flag_sym *field,
			  const char *ev_name,
			  const char *field_name)
{
	define_value(field_type, ev_name, field_name, field->value,
		     field->str);

	if (field->next)
		define_values(field_type, field->next, ev_name, field_name);
}

static void define_field(enum tep_print_arg_type field_type,
			 const char *ev_name,
			 const char *field_name,
			 const char *delim)
{
	const char *handler_name = "define_flag_field";
	PyObject *t;
	unsigned n = 0;

	if (field_type == TEP_PRINT_SYMBOL)
		handler_name = "define_symbolic_field";

	if (field_type == TEP_PRINT_FLAGS)
		t = PyTuple_New(3);
	else
		t = PyTuple_New(2);
	if (!t)
		Py_FatalError("couldn't create Python tuple");

	PyTuple_SetItem(t, n++, _PyUnicode_FromString(ev_name));
	PyTuple_SetItem(t, n++, _PyUnicode_FromString(field_name));
	if (field_type == TEP_PRINT_FLAGS)
		PyTuple_SetItem(t, n++, _PyUnicode_FromString(delim));

	try_call_object(handler_name, t);

	Py_DECREF(t);
}

static void define_event_symbols(struct tep_event *event,
				 const char *ev_name,
				 struct tep_print_arg *args)
{
	if (args == NULL)
		return;

	switch (args->type) {
	case TEP_PRINT_NULL:
		break;
	case TEP_PRINT_ATOM:
		define_value(TEP_PRINT_FLAGS, ev_name, cur_field_name, "0",
			     args->atom.atom);
		zero_flag_atom = 0;
		break;
	case TEP_PRINT_FIELD:
		free(cur_field_name);
		cur_field_name = strdup(args->field.name);
		break;
	case TEP_PRINT_FLAGS:
		define_event_symbols(event, ev_name, args->flags.field);
		define_field(TEP_PRINT_FLAGS, ev_name, cur_field_name,
			     args->flags.delim);
		define_values(TEP_PRINT_FLAGS, args->flags.flags, ev_name,
			      cur_field_name);
		break;
	case TEP_PRINT_SYMBOL:
		define_event_symbols(event, ev_name, args->symbol.field);
		define_field(TEP_PRINT_SYMBOL, ev_name, cur_field_name, NULL);
		define_values(TEP_PRINT_SYMBOL, args->symbol.symbols, ev_name,
			      cur_field_name);
		break;
	case TEP_PRINT_HEX:
	case TEP_PRINT_HEX_STR:
		define_event_symbols(event, ev_name, args->hex.field);
		define_event_symbols(event, ev_name, args->hex.size);
		break;
	case TEP_PRINT_INT_ARRAY:
		define_event_symbols(event, ev_name, args->int_array.field);
		define_event_symbols(event, ev_name, args->int_array.count);
		define_event_symbols(event, ev_name, args->int_array.el_size);
		break;
	case TEP_PRINT_STRING:
		break;
	case TEP_PRINT_TYPE:
		define_event_symbols(event, ev_name, args->typecast.item);
		break;
	case TEP_PRINT_OP:
		if (strcmp(args->op.op, ":") == 0)
			zero_flag_atom = 1;
		define_event_symbols(event, ev_name, args->op.left);
		define_event_symbols(event, ev_name, args->op.right);
		break;
	default:
		/* gcc warns for these? */
	case TEP_PRINT_BSTRING:
	case TEP_PRINT_DYNAMIC_ARRAY:
	case TEP_PRINT_DYNAMIC_ARRAY_LEN:
	case TEP_PRINT_FUNC:
	case TEP_PRINT_BITMASK:
		/* we should warn... */
		return;
	}

	if (args->next)
		define_event_symbols(event, ev_name, args->next);
}

static PyObject *get_field_numeric_entry(struct tep_event *event,
		struct tep_format_field *field, void *data)
{
	bool is_array = field->flags & TEP_FIELD_IS_ARRAY;
	PyObject *obj = NULL, *list = NULL;
	unsigned long long val;
	unsigned int item_size, n_items, i;

	if (is_array) {
		list = PyList_New(field->arraylen);
		if (!list)
			Py_FatalError("couldn't create Python list");
		item_size = field->size / field->arraylen;
		n_items = field->arraylen;
	} else {
		item_size = field->size;
		n_items = 1;
	}

	for (i = 0; i < n_items; i++) {

		val = read_size(event, data + field->offset + i * item_size,
				item_size);
		if (field->flags & TEP_FIELD_IS_SIGNED) {
			if ((long long)val >= LONG_MIN &&
					(long long)val <= LONG_MAX)
				obj = _PyLong_FromLong(val);
			else
				obj = PyLong_FromLongLong(val);
		} else {
			if (val <= LONG_MAX)
				obj = _PyLong_FromLong(val);
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
#endif

static const char *get_dsoname(struct map *map)
{
	const char *dsoname = "[unknown]";
	struct dso *dso = map ? map__dso(map) : NULL;

	if (dso) {
		if (symbol_conf.show_kernel_path && dso->long_name)
			dsoname = dso->long_name;
		else
			dsoname = dso->name;
	}

	return dsoname;
}

static unsigned long get_offset(struct symbol *sym, struct addr_location *al)
{
	unsigned long offset;

	if (al->addr < sym->end)
		offset = al->addr - sym->start;
	else
		offset = al->addr - map__start(al->map) - sym->start;

	return offset;
}

static PyObject *python_process_callchain(struct perf_sample *sample,
					 struct evsel *evsel,
					 struct addr_location *al)
{
	PyObject *pylist;
	struct callchain_cursor *cursor;

	pylist = PyList_New(0);
	if (!pylist)
		Py_FatalError("couldn't create Python list");

	if (!symbol_conf.use_callchain || !sample->callchain)
		goto exit;

	cursor = get_tls_callchain_cursor();
	if (thread__resolve_callchain(al->thread, cursor, evsel,
				      sample, NULL, NULL,
				      scripting_max_stack) != 0) {
		pr_err("Failed to resolve callchain. Skipping\n");
		goto exit;
	}
	callchain_cursor_commit(cursor);


	while (1) {
		PyObject *pyelem;
		struct callchain_cursor_node *node;
		node = callchain_cursor_current(cursor);
		if (!node)
			break;

		pyelem = PyDict_New();
		if (!pyelem)
			Py_FatalError("couldn't create Python dictionary");


		pydict_set_item_string_decref(pyelem, "ip",
				PyLong_FromUnsignedLongLong(node->ip));

		if (node->ms.sym) {
			PyObject *pysym  = PyDict_New();
			if (!pysym)
				Py_FatalError("couldn't create Python dictionary");
			pydict_set_item_string_decref(pysym, "start",
					PyLong_FromUnsignedLongLong(node->ms.sym->start));
			pydict_set_item_string_decref(pysym, "end",
					PyLong_FromUnsignedLongLong(node->ms.sym->end));
			pydict_set_item_string_decref(pysym, "binding",
					_PyLong_FromLong(node->ms.sym->binding));
			pydict_set_item_string_decref(pysym, "name",
					_PyUnicode_FromStringAndSize(node->ms.sym->name,
							node->ms.sym->namelen));
			pydict_set_item_string_decref(pyelem, "sym", pysym);

			if (node->ms.map) {
				struct map *map = node->ms.map;
				struct addr_location node_al;
				unsigned long offset;

				addr_location__init(&node_al);
				node_al.addr = map__map_ip(map, node->ip);
				node_al.map  = map__get(map);
				offset = get_offset(node->ms.sym, &node_al);
				addr_location__exit(&node_al);

				pydict_set_item_string_decref(
					pyelem, "sym_off",
					PyLong_FromUnsignedLongLong(offset));
			}
			if (node->srcline && strcmp(":0", node->srcline)) {
				pydict_set_item_string_decref(
					pyelem, "sym_srcline",
					_PyUnicode_FromString(node->srcline));
			}
		}

		if (node->ms.map) {
			const char *dsoname = get_dsoname(node->ms.map);

			pydict_set_item_string_decref(pyelem, "dso",
					_PyUnicode_FromString(dsoname));
		}

		callchain_cursor_advance(cursor);
		PyList_Append(pylist, pyelem);
		Py_DECREF(pyelem);
	}

exit:
	return pylist;
}

static PyObject *python_process_brstack(struct perf_sample *sample,
					struct thread *thread)
{
	struct branch_stack *br = sample->branch_stack;
	struct branch_entry *entries = perf_sample__branch_entries(sample);
	PyObject *pylist;
	u64 i;

	pylist = PyList_New(0);
	if (!pylist)
		Py_FatalError("couldn't create Python list");

	if (!(br && br->nr))
		goto exit;

	for (i = 0; i < br->nr; i++) {
		PyObject *pyelem;
		struct addr_location al;
		const char *dsoname;

		pyelem = PyDict_New();
		if (!pyelem)
			Py_FatalError("couldn't create Python dictionary");

		pydict_set_item_string_decref(pyelem, "from",
		    PyLong_FromUnsignedLongLong(entries[i].from));
		pydict_set_item_string_decref(pyelem, "to",
		    PyLong_FromUnsignedLongLong(entries[i].to));
		pydict_set_item_string_decref(pyelem, "mispred",
		    PyBool_FromLong(entries[i].flags.mispred));
		pydict_set_item_string_decref(pyelem, "predicted",
		    PyBool_FromLong(entries[i].flags.predicted));
		pydict_set_item_string_decref(pyelem, "in_tx",
		    PyBool_FromLong(entries[i].flags.in_tx));
		pydict_set_item_string_decref(pyelem, "abort",
		    PyBool_FromLong(entries[i].flags.abort));
		pydict_set_item_string_decref(pyelem, "cycles",
		    PyLong_FromUnsignedLongLong(entries[i].flags.cycles));

		addr_location__init(&al);
		thread__find_map_fb(thread, sample->cpumode,
				    entries[i].from, &al);
		dsoname = get_dsoname(al.map);
		pydict_set_item_string_decref(pyelem, "from_dsoname",
					      _PyUnicode_FromString(dsoname));

		thread__find_map_fb(thread, sample->cpumode,
				    entries[i].to, &al);
		dsoname = get_dsoname(al.map);
		pydict_set_item_string_decref(pyelem, "to_dsoname",
					      _PyUnicode_FromString(dsoname));

		addr_location__exit(&al);
		PyList_Append(pylist, pyelem);
		Py_DECREF(pyelem);
	}

exit:
	return pylist;
}

static int get_symoff(struct symbol *sym, struct addr_location *al,
		      bool print_off, char *bf, int size)
{
	unsigned long offset;

	if (!sym || !sym->name[0])
		return scnprintf(bf, size, "%s", "[unknown]");

	if (!print_off)
		return scnprintf(bf, size, "%s", sym->name);

	offset = get_offset(sym, al);

	return scnprintf(bf, size, "%s+0x%x", sym->name, offset);
}

static int get_br_mspred(struct branch_flags *flags, char *bf, int size)
{
	if (!flags->mispred  && !flags->predicted)
		return scnprintf(bf, size, "%s", "-");

	if (flags->mispred)
		return scnprintf(bf, size, "%s", "M");

	return scnprintf(bf, size, "%s", "P");
}

static PyObject *python_process_brstacksym(struct perf_sample *sample,
					   struct thread *thread)
{
	struct branch_stack *br = sample->branch_stack;
	struct branch_entry *entries = perf_sample__branch_entries(sample);
	PyObject *pylist;
	u64 i;
	char bf[512];

	pylist = PyList_New(0);
	if (!pylist)
		Py_FatalError("couldn't create Python list");

	if (!(br && br->nr))
		goto exit;

	for (i = 0; i < br->nr; i++) {
		PyObject *pyelem;
		struct addr_location al;

		addr_location__init(&al);
		pyelem = PyDict_New();
		if (!pyelem)
			Py_FatalError("couldn't create Python dictionary");

		thread__find_symbol_fb(thread, sample->cpumode,
				       entries[i].from, &al);
		get_symoff(al.sym, &al, true, bf, sizeof(bf));
		pydict_set_item_string_decref(pyelem, "from",
					      _PyUnicode_FromString(bf));

		thread__find_symbol_fb(thread, sample->cpumode,
				       entries[i].to, &al);
		get_symoff(al.sym, &al, true, bf, sizeof(bf));
		pydict_set_item_string_decref(pyelem, "to",
					      _PyUnicode_FromString(bf));

		get_br_mspred(&entries[i].flags, bf, sizeof(bf));
		pydict_set_item_string_decref(pyelem, "pred",
					      _PyUnicode_FromString(bf));

		if (entries[i].flags.in_tx) {
			pydict_set_item_string_decref(pyelem, "in_tx",
					      _PyUnicode_FromString("X"));
		} else {
			pydict_set_item_string_decref(pyelem, "in_tx",
					      _PyUnicode_FromString("-"));
		}

		if (entries[i].flags.abort) {
			pydict_set_item_string_decref(pyelem, "abort",
					      _PyUnicode_FromString("A"));
		} else {
			pydict_set_item_string_decref(pyelem, "abort",
					      _PyUnicode_FromString("-"));
		}

		PyList_Append(pylist, pyelem);
		Py_DECREF(pyelem);
		addr_location__exit(&al);
	}

exit:
	return pylist;
}

static PyObject *get_sample_value_as_tuple(struct sample_read_value *value,
					   u64 read_format)
{
	PyObject *t;

	t = PyTuple_New(3);
	if (!t)
		Py_FatalError("couldn't create Python tuple");
	PyTuple_SetItem(t, 0, PyLong_FromUnsignedLongLong(value->id));
	PyTuple_SetItem(t, 1, PyLong_FromUnsignedLongLong(value->value));
	if (read_format & PERF_FORMAT_LOST)
		PyTuple_SetItem(t, 2, PyLong_FromUnsignedLongLong(value->lost));

	return t;
}

static void set_sample_read_in_dict(PyObject *dict_sample,
					 struct perf_sample *sample,
					 struct evsel *evsel)
{
	u64 read_format = evsel->core.attr.read_format;
	PyObject *values;
	unsigned int i;

	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
		pydict_set_item_string_decref(dict_sample, "time_enabled",
			PyLong_FromUnsignedLongLong(sample->read.time_enabled));
	}

	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
		pydict_set_item_string_decref(dict_sample, "time_running",
			PyLong_FromUnsignedLongLong(sample->read.time_running));
	}

	if (read_format & PERF_FORMAT_GROUP)
		values = PyList_New(sample->read.group.nr);
	else
		values = PyList_New(1);

	if (!values)
		Py_FatalError("couldn't create Python list");

	if (read_format & PERF_FORMAT_GROUP) {
		struct sample_read_value *v = sample->read.group.values;

		i = 0;
		sample_read_group__for_each(v, sample->read.group.nr, read_format) {
			PyObject *t = get_sample_value_as_tuple(v, read_format);
			PyList_SET_ITEM(values, i, t);
			i++;
		}
	} else {
		PyObject *t = get_sample_value_as_tuple(&sample->read.one,
							read_format);
		PyList_SET_ITEM(values, 0, t);
	}
	pydict_set_item_string_decref(dict_sample, "values", values);
}

static void set_sample_datasrc_in_dict(PyObject *dict,
				       struct perf_sample *sample)
{
	struct mem_info mi = { .data_src.val = sample->data_src };
	char decode[100];

	pydict_set_item_string_decref(dict, "datasrc",
			PyLong_FromUnsignedLongLong(sample->data_src));

	perf_script__meminfo_scnprintf(decode, 100, &mi);

	pydict_set_item_string_decref(dict, "datasrc_decode",
			_PyUnicode_FromString(decode));
}

static void regs_map(struct regs_dump *regs, uint64_t mask, const char *arch, char *bf, int size)
{
	unsigned int i = 0, r;
	int printed = 0;

	bf[0] = 0;

	if (size <= 0)
		return;

	if (!regs || !regs->regs)
		return;

	for_each_set_bit(r, (unsigned long *) &mask, sizeof(mask) * 8) {
		u64 val = regs->regs[i++];

		printed += scnprintf(bf + printed, size - printed,
				     "%5s:0x%" PRIx64 " ",
				     perf_reg_name(r, arch), val);
	}
}

static int set_regs_in_dict(PyObject *dict,
			     struct perf_sample *sample,
			     struct evsel *evsel)
{
	struct perf_event_attr *attr = &evsel->core.attr;
	const char *arch = perf_env__arch(evsel__env(evsel));

	/*
	 * Here value 28 is a constant size which can be used to print
	 * one register value and its corresponds to:
	 * 16 chars is to specify 64 bit register in hexadecimal.
	 * 2 chars is for appending "0x" to the hexadecimal value and
	 * 10 chars is for register name.
	 */
	int size = __sw_hweight64(attr->sample_regs_intr) * 28;
	char *bf = malloc(size);
	if (!bf)
		return -1;

	regs_map(&sample->intr_regs, attr->sample_regs_intr, arch, bf, size);

	pydict_set_item_string_decref(dict, "iregs",
			_PyUnicode_FromString(bf));

	regs_map(&sample->user_regs, attr->sample_regs_user, arch, bf, size);

	pydict_set_item_string_decref(dict, "uregs",
			_PyUnicode_FromString(bf));
	free(bf);

	return 0;
}

static void set_sym_in_dict(PyObject *dict, struct addr_location *al,
			    const char *dso_field, const char *dso_bid_field,
			    const char *dso_map_start, const char *dso_map_end,
			    const char *sym_field, const char *symoff_field)
{
	char sbuild_id[SBUILD_ID_SIZE];

	if (al->map) {
		struct dso *dso = map__dso(al->map);

		pydict_set_item_string_decref(dict, dso_field, _PyUnicode_FromString(dso->name));
		build_id__sprintf(&dso->bid, sbuild_id);
		pydict_set_item_string_decref(dict, dso_bid_field,
			_PyUnicode_FromString(sbuild_id));
		pydict_set_item_string_decref(dict, dso_map_start,
			PyLong_FromUnsignedLong(map__start(al->map)));
		pydict_set_item_string_decref(dict, dso_map_end,
			PyLong_FromUnsignedLong(map__end(al->map)));
	}
	if (al->sym) {
		pydict_set_item_string_decref(dict, sym_field,
			_PyUnicode_FromString(al->sym->name));
		pydict_set_item_string_decref(dict, symoff_field,
			PyLong_FromUnsignedLong(get_offset(al->sym, al)));
	}
}

static void set_sample_flags(PyObject *dict, u32 flags)
{
	const char *ch = PERF_IP_FLAG_CHARS;
	char *p, str[33];

	for (p = str; *ch; ch++, flags >>= 1) {
		if (flags & 1)
			*p++ = *ch;
	}
	*p = 0;
	pydict_set_item_string_decref(dict, "flags", _PyUnicode_FromString(str));
}

static void python_process_sample_flags(struct perf_sample *sample, PyObject *dict_sample)
{
	char flags_disp[SAMPLE_FLAGS_BUF_SIZE];

	set_sample_flags(dict_sample, sample->flags);
	perf_sample__sprintf_flags(sample->flags, flags_disp, sizeof(flags_disp));
	pydict_set_item_string_decref(dict_sample, "flags_disp",
		_PyUnicode_FromString(flags_disp));
}

static PyObject *get_perf_sample_dict(struct perf_sample *sample,
					 struct evsel *evsel,
					 struct addr_location *al,
					 struct addr_location *addr_al,
					 PyObject *callchain)
{
	PyObject *dict, *dict_sample, *brstack, *brstacksym;

	dict = PyDict_New();
	if (!dict)
		Py_FatalError("couldn't create Python dictionary");

	dict_sample = PyDict_New();
	if (!dict_sample)
		Py_FatalError("couldn't create Python dictionary");

	pydict_set_item_string_decref(dict, "ev_name", _PyUnicode_FromString(evsel__name(evsel)));
	pydict_set_item_string_decref(dict, "attr", _PyBytes_FromStringAndSize((const char *)&evsel->core.attr, sizeof(evsel->core.attr)));

	pydict_set_item_string_decref(dict_sample, "pid",
			_PyLong_FromLong(sample->pid));
	pydict_set_item_string_decref(dict_sample, "tid",
			_PyLong_FromLong(sample->tid));
	pydict_set_item_string_decref(dict_sample, "cpu",
			_PyLong_FromLong(sample->cpu));
	pydict_set_item_string_decref(dict_sample, "ip",
			PyLong_FromUnsignedLongLong(sample->ip));
	pydict_set_item_string_decref(dict_sample, "time",
			PyLong_FromUnsignedLongLong(sample->time));
	pydict_set_item_string_decref(dict_sample, "period",
			PyLong_FromUnsignedLongLong(sample->period));
	pydict_set_item_string_decref(dict_sample, "phys_addr",
			PyLong_FromUnsignedLongLong(sample->phys_addr));
	pydict_set_item_string_decref(dict_sample, "addr",
			PyLong_FromUnsignedLongLong(sample->addr));
	set_sample_read_in_dict(dict_sample, sample, evsel);
	pydict_set_item_string_decref(dict_sample, "weight",
			PyLong_FromUnsignedLongLong(sample->weight));
	pydict_set_item_string_decref(dict_sample, "transaction",
			PyLong_FromUnsignedLongLong(sample->transaction));
	set_sample_datasrc_in_dict(dict_sample, sample);
	pydict_set_item_string_decref(dict, "sample", dict_sample);

	pydict_set_item_string_decref(dict, "raw_buf", _PyBytes_FromStringAndSize(
			(const char *)sample->raw_data, sample->raw_size));
	pydict_set_item_string_decref(dict, "comm",
			_PyUnicode_FromString(thread__comm_str(al->thread)));
	set_sym_in_dict(dict, al, "dso", "dso_bid", "dso_map_start", "dso_map_end",
			"symbol", "symoff");

	pydict_set_item_string_decref(dict, "callchain", callchain);

	brstack = python_process_brstack(sample, al->thread);
	pydict_set_item_string_decref(dict, "brstack", brstack);

	brstacksym = python_process_brstacksym(sample, al->thread);
	pydict_set_item_string_decref(dict, "brstacksym", brstacksym);

	if (sample->machine_pid) {
		pydict_set_item_string_decref(dict_sample, "machine_pid",
				_PyLong_FromLong(sample->machine_pid));
		pydict_set_item_string_decref(dict_sample, "vcpu",
				_PyLong_FromLong(sample->vcpu));
	}

	pydict_set_item_string_decref(dict_sample, "cpumode",
			_PyLong_FromLong((unsigned long)sample->cpumode));

	if (addr_al) {
		pydict_set_item_string_decref(dict_sample, "addr_correlates_sym",
			PyBool_FromLong(1));
		set_sym_in_dict(dict_sample, addr_al, "addr_dso", "addr_dso_bid",
				"addr_dso_map_start", "addr_dso_map_end",
				"addr_symbol", "addr_symoff");
	}

	if (sample->flags)
		python_process_sample_flags(sample, dict_sample);

	/* Instructions per cycle (IPC) */
	if (sample->insn_cnt && sample->cyc_cnt) {
		pydict_set_item_string_decref(dict_sample, "insn_cnt",
			PyLong_FromUnsignedLongLong(sample->insn_cnt));
		pydict_set_item_string_decref(dict_sample, "cyc_cnt",
			PyLong_FromUnsignedLongLong(sample->cyc_cnt));
	}

	if (set_regs_in_dict(dict, sample, evsel))
		Py_FatalError("Failed to setting regs in dict");

	return dict;
}

#ifdef HAVE_LIBTRACEEVENT
static void python_process_tracepoint(struct perf_sample *sample,
				      struct evsel *evsel,
				      struct addr_location *al,
				      struct addr_location *addr_al)
{
	struct tep_event *event = evsel->tp_format;
	PyObject *handler, *context, *t, *obj = NULL, *callchain;
	PyObject *dict = NULL, *all_entries_dict = NULL;
	static char handler_name[256];
	struct tep_format_field *field;
	unsigned long s, ns;
	unsigned n = 0;
	int pid;
	int cpu = sample->cpu;
	void *data = sample->raw_data;
	unsigned long long nsecs = sample->time;
	const char *comm = thread__comm_str(al->thread);
	const char *default_handler_name = "trace_unhandled";
	DECLARE_BITMAP(events_defined, TRACE_EVENT_TYPE_MAX);

	bitmap_zero(events_defined, TRACE_EVENT_TYPE_MAX);

	if (!event) {
		snprintf(handler_name, sizeof(handler_name),
			 "ug! no event found for type %" PRIu64, (u64)evsel->core.attr.config);
		Py_FatalError(handler_name);
	}

	pid = raw_field_value(event, "common_pid", data);

	sprintf(handler_name, "%s__%s", event->system, event->name);

	if (!__test_and_set_bit(event->id, events_defined))
		define_event_symbols(event, handler_name, event->print_fmt.args);

	handler = get_handler(handler_name);
	if (!handler) {
		handler = get_handler(default_handler_name);
		if (!handler)
			return;
		dict = PyDict_New();
		if (!dict)
			Py_FatalError("couldn't create Python dict");
	}

	t = PyTuple_New(MAX_FIELDS);
	if (!t)
		Py_FatalError("couldn't create Python tuple");


	s = nsecs / NSEC_PER_SEC;
	ns = nsecs - s * NSEC_PER_SEC;

	context = _PyCapsule_New(scripting_context, NULL, NULL);

	PyTuple_SetItem(t, n++, _PyUnicode_FromString(handler_name));
	PyTuple_SetItem(t, n++, context);

	/* ip unwinding */
	callchain = python_process_callchain(sample, evsel, al);
	/* Need an additional reference for the perf_sample dict */
	Py_INCREF(callchain);

	if (!dict) {
		PyTuple_SetItem(t, n++, _PyLong_FromLong(cpu));
		PyTuple_SetItem(t, n++, _PyLong_FromLong(s));
		PyTuple_SetItem(t, n++, _PyLong_FromLong(ns));
		PyTuple_SetItem(t, n++, _PyLong_FromLong(pid));
		PyTuple_SetItem(t, n++, _PyUnicode_FromString(comm));
		PyTuple_SetItem(t, n++, callchain);
	} else {
		pydict_set_item_string_decref(dict, "common_cpu", _PyLong_FromLong(cpu));
		pydict_set_item_string_decref(dict, "common_s", _PyLong_FromLong(s));
		pydict_set_item_string_decref(dict, "common_ns", _PyLong_FromLong(ns));
		pydict_set_item_string_decref(dict, "common_pid", _PyLong_FromLong(pid));
		pydict_set_item_string_decref(dict, "common_comm", _PyUnicode_FromString(comm));
		pydict_set_item_string_decref(dict, "common_callchain", callchain);
	}
	for (field = event->format.fields; field; field = field->next) {
		unsigned int offset, len;
		unsigned long long val;

		if (field->flags & TEP_FIELD_IS_ARRAY) {
			offset = field->offset;
			len    = field->size;
			if (field->flags & TEP_FIELD_IS_DYNAMIC) {
				val     = tep_read_number(scripting_context->pevent,
							  data + offset, len);
				offset  = val;
				len     = offset >> 16;
				offset &= 0xffff;
				if (tep_field_is_relative(field->flags))
					offset += field->offset + field->size;
			}
			if (field->flags & TEP_FIELD_IS_STRING &&
			    is_printable_array(data + offset, len)) {
				obj = _PyUnicode_FromString((char *) data + offset);
			} else {
				obj = PyByteArray_FromStringAndSize((const char *) data + offset, len);
				field->flags &= ~TEP_FIELD_IS_STRING;
			}
		} else { /* FIELD_IS_NUMERIC */
			obj = get_field_numeric_entry(event, field, data);
		}
		if (!dict)
			PyTuple_SetItem(t, n++, obj);
		else
			pydict_set_item_string_decref(dict, field->name, obj);

	}

	if (dict)
		PyTuple_SetItem(t, n++, dict);

	if (get_argument_count(handler) == (int) n + 1) {
		all_entries_dict = get_perf_sample_dict(sample, evsel, al, addr_al,
			callchain);
		PyTuple_SetItem(t, n++,	all_entries_dict);
	} else {
		Py_DECREF(callchain);
	}

	if (_PyTuple_Resize(&t, n) == -1)
		Py_FatalError("error resizing Python tuple");

	if (!dict)
		call_object(handler, t, handler_name);
	else
		call_object(handler, t, default_handler_name);

	Py_DECREF(t);
}
#else
static void python_process_tracepoint(struct perf_sample *sample __maybe_unused,
				      struct evsel *evsel __maybe_unused,
				      struct addr_location *al __maybe_unused,
				      struct addr_location *addr_al __maybe_unused)
{
	fprintf(stderr, "Tracepoint events are not supported because "
			"perf is not linked with libtraceevent.\n");
}
#endif

static PyObject *tuple_new(unsigned int sz)
{
	PyObject *t;

	t = PyTuple_New(sz);
	if (!t)
		Py_FatalError("couldn't create Python tuple");
	return t;
}

static int tuple_set_s64(PyObject *t, unsigned int pos, s64 val)
{
#if BITS_PER_LONG == 64
	return PyTuple_SetItem(t, pos, _PyLong_FromLong(val));
#endif
#if BITS_PER_LONG == 32
	return PyTuple_SetItem(t, pos, PyLong_FromLongLong(val));
#endif
}

/*
 * Databases support only signed 64-bit numbers, so even though we are
 * exporting a u64, it must be as s64.
 */
#define tuple_set_d64 tuple_set_s64

static int tuple_set_u64(PyObject *t, unsigned int pos, u64 val)
{
#if BITS_PER_LONG == 64
	return PyTuple_SetItem(t, pos, PyLong_FromUnsignedLong(val));
#endif
#if BITS_PER_LONG == 32
	return PyTuple_SetItem(t, pos, PyLong_FromUnsignedLongLong(val));
#endif
}

static int tuple_set_u32(PyObject *t, unsigned int pos, u32 val)
{
	return PyTuple_SetItem(t, pos, PyLong_FromUnsignedLong(val));
}

static int tuple_set_s32(PyObject *t, unsigned int pos, s32 val)
{
	return PyTuple_SetItem(t, pos, _PyLong_FromLong(val));
}

static int tuple_set_bool(PyObject *t, unsigned int pos, bool val)
{
	return PyTuple_SetItem(t, pos, PyBool_FromLong(val));
}

static int tuple_set_string(PyObject *t, unsigned int pos, const char *s)
{
	return PyTuple_SetItem(t, pos, _PyUnicode_FromString(s));
}

static int tuple_set_bytes(PyObject *t, unsigned int pos, void *bytes,
			   unsigned int sz)
{
	return PyTuple_SetItem(t, pos, _PyBytes_FromStringAndSize(bytes, sz));
}

static int python_export_evsel(struct db_export *dbe, struct evsel *evsel)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(2);

	tuple_set_d64(t, 0, evsel->db_id);
	tuple_set_string(t, 1, evsel__name(evsel));

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

	tuple_set_d64(t, 0, machine->db_id);
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

	tuple_set_d64(t, 0, thread__db_id(thread));
	tuple_set_d64(t, 1, machine->db_id);
	tuple_set_d64(t, 2, main_thread_db_id);
	tuple_set_s32(t, 3, thread__pid(thread));
	tuple_set_s32(t, 4, thread__tid(thread));

	call_object(tables->thread_handler, t, "thread_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_comm(struct db_export *dbe, struct comm *comm,
			      struct thread *thread)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(5);

	tuple_set_d64(t, 0, comm->db_id);
	tuple_set_string(t, 1, comm__str(comm));
	tuple_set_d64(t, 2, thread__db_id(thread));
	tuple_set_d64(t, 3, comm->start);
	tuple_set_s32(t, 4, comm->exec);

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

	tuple_set_d64(t, 0, db_id);
	tuple_set_d64(t, 1, comm->db_id);
	tuple_set_d64(t, 2, thread__db_id(thread));

	call_object(tables->comm_thread_handler, t, "comm_thread_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_dso(struct db_export *dbe, struct dso *dso,
			     struct machine *machine)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	char sbuild_id[SBUILD_ID_SIZE];
	PyObject *t;

	build_id__sprintf(&dso->bid, sbuild_id);

	t = tuple_new(5);

	tuple_set_d64(t, 0, dso->db_id);
	tuple_set_d64(t, 1, machine->db_id);
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

	tuple_set_d64(t, 0, *sym_db_id);
	tuple_set_d64(t, 1, dso->db_id);
	tuple_set_d64(t, 2, sym->start);
	tuple_set_d64(t, 3, sym->end);
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

static void python_export_sample_table(struct db_export *dbe,
				       struct export_sample *es)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(25);

	tuple_set_d64(t, 0, es->db_id);
	tuple_set_d64(t, 1, es->evsel->db_id);
	tuple_set_d64(t, 2, maps__machine(es->al->maps)->db_id);
	tuple_set_d64(t, 3, thread__db_id(es->al->thread));
	tuple_set_d64(t, 4, es->comm_db_id);
	tuple_set_d64(t, 5, es->dso_db_id);
	tuple_set_d64(t, 6, es->sym_db_id);
	tuple_set_d64(t, 7, es->offset);
	tuple_set_d64(t, 8, es->sample->ip);
	tuple_set_d64(t, 9, es->sample->time);
	tuple_set_s32(t, 10, es->sample->cpu);
	tuple_set_d64(t, 11, es->addr_dso_db_id);
	tuple_set_d64(t, 12, es->addr_sym_db_id);
	tuple_set_d64(t, 13, es->addr_offset);
	tuple_set_d64(t, 14, es->sample->addr);
	tuple_set_d64(t, 15, es->sample->period);
	tuple_set_d64(t, 16, es->sample->weight);
	tuple_set_d64(t, 17, es->sample->transaction);
	tuple_set_d64(t, 18, es->sample->data_src);
	tuple_set_s32(t, 19, es->sample->flags & PERF_BRANCH_MASK);
	tuple_set_s32(t, 20, !!(es->sample->flags & PERF_IP_FLAG_IN_TX));
	tuple_set_d64(t, 21, es->call_path_id);
	tuple_set_d64(t, 22, es->sample->insn_cnt);
	tuple_set_d64(t, 23, es->sample->cyc_cnt);
	tuple_set_s32(t, 24, es->sample->flags);

	call_object(tables->sample_handler, t, "sample_table");

	Py_DECREF(t);
}

static void python_export_synth(struct db_export *dbe, struct export_sample *es)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(3);

	tuple_set_d64(t, 0, es->db_id);
	tuple_set_d64(t, 1, es->evsel->core.attr.config);
	tuple_set_bytes(t, 2, es->sample->raw_data, es->sample->raw_size);

	call_object(tables->synth_handler, t, "synth_data");

	Py_DECREF(t);
}

static int python_export_sample(struct db_export *dbe,
				struct export_sample *es)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);

	python_export_sample_table(dbe, es);

	if (es->evsel->core.attr.type == PERF_TYPE_SYNTH && tables->synth_handler)
		python_export_synth(dbe, es);

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

	tuple_set_d64(t, 0, cp->db_id);
	tuple_set_d64(t, 1, parent_db_id);
	tuple_set_d64(t, 2, sym_db_id);
	tuple_set_d64(t, 3, cp->ip);

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

	t = tuple_new(14);

	tuple_set_d64(t, 0, cr->db_id);
	tuple_set_d64(t, 1, thread__db_id(cr->thread));
	tuple_set_d64(t, 2, comm_db_id);
	tuple_set_d64(t, 3, cr->cp->db_id);
	tuple_set_d64(t, 4, cr->call_time);
	tuple_set_d64(t, 5, cr->return_time);
	tuple_set_d64(t, 6, cr->branch_count);
	tuple_set_d64(t, 7, cr->call_ref);
	tuple_set_d64(t, 8, cr->return_ref);
	tuple_set_d64(t, 9, cr->cp->parent->db_id);
	tuple_set_s32(t, 10, cr->flags);
	tuple_set_d64(t, 11, cr->parent_db_id);
	tuple_set_d64(t, 12, cr->insn_count);
	tuple_set_d64(t, 13, cr->cyc_count);

	call_object(tables->call_return_handler, t, "call_return_table");

	Py_DECREF(t);

	return 0;
}

static int python_export_context_switch(struct db_export *dbe, u64 db_id,
					struct machine *machine,
					struct perf_sample *sample,
					u64 th_out_id, u64 comm_out_id,
					u64 th_in_id, u64 comm_in_id, int flags)
{
	struct tables *tables = container_of(dbe, struct tables, dbe);
	PyObject *t;

	t = tuple_new(9);

	tuple_set_d64(t, 0, db_id);
	tuple_set_d64(t, 1, machine->db_id);
	tuple_set_d64(t, 2, sample->time);
	tuple_set_s32(t, 3, sample->cpu);
	tuple_set_d64(t, 4, th_out_id);
	tuple_set_d64(t, 5, comm_out_id);
	tuple_set_d64(t, 6, th_in_id);
	tuple_set_d64(t, 7, comm_in_id);
	tuple_set_s32(t, 8, flags);

	call_object(tables->context_switch_handler, t, "context_switch");

	Py_DECREF(t);

	return 0;
}

static int python_process_call_return(struct call_return *cr, u64 *parent_db_id,
				      void *data)
{
	struct db_export *dbe = data;

	return db_export__call_return(dbe, cr, parent_db_id);
}

static void python_process_general_event(struct perf_sample *sample,
					 struct evsel *evsel,
					 struct addr_location *al,
					 struct addr_location *addr_al)
{
	PyObject *handler, *t, *dict, *callchain;
	static char handler_name[64];
	unsigned n = 0;

	snprintf(handler_name, sizeof(handler_name), "%s", "process_event");

	handler = get_handler(handler_name);
	if (!handler)
		return;

	/*
	 * Use the MAX_FIELDS to make the function expandable, though
	 * currently there is only one item for the tuple.
	 */
	t = PyTuple_New(MAX_FIELDS);
	if (!t)
		Py_FatalError("couldn't create Python tuple");

	/* ip unwinding */
	callchain = python_process_callchain(sample, evsel, al);
	dict = get_perf_sample_dict(sample, evsel, al, addr_al, callchain);

	PyTuple_SetItem(t, n++, dict);
	if (_PyTuple_Resize(&t, n) == -1)
		Py_FatalError("error resizing Python tuple");

	call_object(handler, t, handler_name);

	Py_DECREF(t);
}

static void python_process_event(union perf_event *event,
				 struct perf_sample *sample,
				 struct evsel *evsel,
				 struct addr_location *al,
				 struct addr_location *addr_al)
{
	struct tables *tables = &tables_global;

	scripting_context__update(scripting_context, event, sample, evsel, al, addr_al);

	switch (evsel->core.attr.type) {
	case PERF_TYPE_TRACEPOINT:
		python_process_tracepoint(sample, evsel, al, addr_al);
		break;
	/* Reserve for future process_hw/sw/raw APIs */
	default:
		if (tables->db_export_mode)
			db_export__sample(&tables->dbe, event, sample, evsel, al, addr_al);
		else
			python_process_general_event(sample, evsel, al, addr_al);
	}
}

static void python_process_throttle(union perf_event *event,
				    struct perf_sample *sample,
				    struct machine *machine)
{
	const char *handler_name;
	PyObject *handler, *t;

	if (event->header.type == PERF_RECORD_THROTTLE)
		handler_name = "throttle";
	else
		handler_name = "unthrottle";
	handler = get_handler(handler_name);
	if (!handler)
		return;

	t = tuple_new(6);
	if (!t)
		return;

	tuple_set_u64(t, 0, event->throttle.time);
	tuple_set_u64(t, 1, event->throttle.id);
	tuple_set_u64(t, 2, event->throttle.stream_id);
	tuple_set_s32(t, 3, sample->cpu);
	tuple_set_s32(t, 4, sample->pid);
	tuple_set_s32(t, 5, sample->tid);

	call_object(handler, t, handler_name);

	Py_DECREF(t);
}

static void python_do_process_switch(union perf_event *event,
				     struct perf_sample *sample,
				     struct machine *machine)
{
	const char *handler_name = "context_switch";
	bool out = event->header.misc & PERF_RECORD_MISC_SWITCH_OUT;
	bool out_preempt = out && (event->header.misc & PERF_RECORD_MISC_SWITCH_OUT_PREEMPT);
	pid_t np_pid = -1, np_tid = -1;
	PyObject *handler, *t;

	handler = get_handler(handler_name);
	if (!handler)
		return;

	if (event->header.type == PERF_RECORD_SWITCH_CPU_WIDE) {
		np_pid = event->context_switch.next_prev_pid;
		np_tid = event->context_switch.next_prev_tid;
	}

	t = tuple_new(11);
	if (!t)
		return;

	tuple_set_u64(t, 0, sample->time);
	tuple_set_s32(t, 1, sample->cpu);
	tuple_set_s32(t, 2, sample->pid);
	tuple_set_s32(t, 3, sample->tid);
	tuple_set_s32(t, 4, np_pid);
	tuple_set_s32(t, 5, np_tid);
	tuple_set_s32(t, 6, machine->pid);
	tuple_set_bool(t, 7, out);
	tuple_set_bool(t, 8, out_preempt);
	tuple_set_s32(t, 9, sample->machine_pid);
	tuple_set_s32(t, 10, sample->vcpu);

	call_object(handler, t, handler_name);

	Py_DECREF(t);
}

static void python_process_switch(union perf_event *event,
				  struct perf_sample *sample,
				  struct machine *machine)
{
	struct tables *tables = &tables_global;

	if (tables->db_export_mode)
		db_export__switch(&tables->dbe, event, sample, machine);
	else
		python_do_process_switch(event, sample, machine);
}

static void python_process_auxtrace_error(struct perf_session *session __maybe_unused,
					  union perf_event *event)
{
	struct perf_record_auxtrace_error *e = &event->auxtrace_error;
	u8 cpumode = e->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	const char *handler_name = "auxtrace_error";
	unsigned long long tm = e->time;
	const char *msg = e->msg;
	PyObject *handler, *t;

	handler = get_handler(handler_name);
	if (!handler)
		return;

	if (!e->fmt) {
		tm = 0;
		msg = (const char *)&e->time;
	}

	t = tuple_new(11);

	tuple_set_u32(t, 0, e->type);
	tuple_set_u32(t, 1, e->code);
	tuple_set_s32(t, 2, e->cpu);
	tuple_set_s32(t, 3, e->pid);
	tuple_set_s32(t, 4, e->tid);
	tuple_set_u64(t, 5, e->ip);
	tuple_set_u64(t, 6, tm);
	tuple_set_string(t, 7, msg);
	tuple_set_u32(t, 8, cpumode);
	tuple_set_s32(t, 9, e->machine_pid);
	tuple_set_s32(t, 10, e->vcpu);

	call_object(handler, t, handler_name);

	Py_DECREF(t);
}

static void get_handler_name(char *str, size_t size,
			     struct evsel *evsel)
{
	char *p = str;

	scnprintf(str, size, "stat__%s", evsel__name(evsel));

	while ((p = strchr(p, ':'))) {
		*p = '_';
		p++;
	}
}

static void
process_stat(struct evsel *counter, struct perf_cpu cpu, int thread, u64 tstamp,
	     struct perf_counts_values *count)
{
	PyObject *handler, *t;
	static char handler_name[256];
	int n = 0;

	t = PyTuple_New(MAX_FIELDS);
	if (!t)
		Py_FatalError("couldn't create Python tuple");

	get_handler_name(handler_name, sizeof(handler_name),
			 counter);

	handler = get_handler(handler_name);
	if (!handler) {
		pr_debug("can't find python handler %s\n", handler_name);
		return;
	}

	PyTuple_SetItem(t, n++, _PyLong_FromLong(cpu.cpu));
	PyTuple_SetItem(t, n++, _PyLong_FromLong(thread));

	tuple_set_u64(t, n++, tstamp);
	tuple_set_u64(t, n++, count->val);
	tuple_set_u64(t, n++, count->ena);
	tuple_set_u64(t, n++, count->run);

	if (_PyTuple_Resize(&t, n) == -1)
		Py_FatalError("error resizing Python tuple");

	call_object(handler, t, handler_name);

	Py_DECREF(t);
}

static void python_process_stat(struct perf_stat_config *config,
				struct evsel *counter, u64 tstamp)
{
	struct perf_thread_map *threads = counter->core.threads;
	struct perf_cpu_map *cpus = counter->core.cpus;
	int cpu, thread;

	for (thread = 0; thread < perf_thread_map__nr(threads); thread++) {
		for (cpu = 0; cpu < perf_cpu_map__nr(cpus); cpu++) {
			process_stat(counter, perf_cpu_map__cpu(cpus, cpu),
				     perf_thread_map__pid(threads, thread), tstamp,
				     perf_counts(counter->counts, cpu, thread));
		}
	}
}

static void python_process_stat_interval(u64 tstamp)
{
	PyObject *handler, *t;
	static const char handler_name[] = "stat__interval";
	int n = 0;

	t = PyTuple_New(MAX_FIELDS);
	if (!t)
		Py_FatalError("couldn't create Python tuple");

	handler = get_handler(handler_name);
	if (!handler) {
		pr_debug("can't find python handler %s\n", handler_name);
		return;
	}

	tuple_set_u64(t, n++, tstamp);

	if (_PyTuple_Resize(&t, n) == -1)
		Py_FatalError("error resizing Python tuple");

	call_object(handler, t, handler_name);

	Py_DECREF(t);
}

static int perf_script_context_init(void)
{
	PyObject *perf_script_context;
	PyObject *perf_trace_context;
	PyObject *dict;
	int ret;

	perf_trace_context = PyImport_AddModule("perf_trace_context");
	if (!perf_trace_context)
		return -1;
	dict = PyModule_GetDict(perf_trace_context);
	if (!dict)
		return -1;

	perf_script_context = _PyCapsule_New(scripting_context, NULL, NULL);
	if (!perf_script_context)
		return -1;

	ret = PyDict_SetItemString(dict, "perf_script_context", perf_script_context);
	if (!ret)
		ret = PyDict_SetItemString(main_dict, "perf_script_context", perf_script_context);
	Py_DECREF(perf_script_context);
	return ret;
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

	if (perf_script_context_init())
		goto error;

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
	const char *perf_db_export_callchains = "perf_db_export_callchains";
	PyObject *db_export_mode, *db_export_calls, *db_export_callchains;
	bool export_calls = false;
	bool export_callchains = false;
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

	/* handle export calls */
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

	/* handle export callchains */
	tables->dbe.cpr = NULL;
	db_export_callchains = PyDict_GetItemString(main_dict,
						    perf_db_export_callchains);
	if (db_export_callchains) {
		ret = PyObject_IsTrue(db_export_callchains);
		if (ret == -1)
			handler_call_die(perf_db_export_callchains);
		export_callchains = !!ret;
	}

	if (export_callchains) {
		/*
		 * Attempt to use the call path root from the call return
		 * processor, if the call return processor is in use. Otherwise,
		 * we allocate a new call path root. This prevents exporting
		 * duplicate call path ids when both are in use simultaneously.
		 */
		if (tables->dbe.crp)
			tables->dbe.cpr = tables->dbe.crp->cpr;
		else
			tables->dbe.cpr = call_path_root__new();

		if (!tables->dbe.cpr)
			Py_FatalError("failed to create call path root");
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
	SET_TABLE_HANDLER(context_switch);

	/*
	 * Synthesized events are samples but with architecture-specific data
	 * stored in sample->raw_data. They are exported via
	 * python_export_sample() and consequently do not need a separate export
	 * callback.
	 */
	tables->synth_handler = get_handler("synth_data");
}

#if PY_MAJOR_VERSION < 3
static void _free_command_line(const char **command_line, int num)
{
	free(command_line);
}
#else
static void _free_command_line(wchar_t **command_line, int num)
{
	int i;
	for (i = 0; i < num; i++)
		PyMem_RawFree(command_line[i]);
	free(command_line);
}
#endif


/*
 * Start trace script
 */
static int python_start_script(const char *script, int argc, const char **argv,
			       struct perf_session *session)
{
	struct tables *tables = &tables_global;
#if PY_MAJOR_VERSION < 3
	const char **command_line;
#else
	wchar_t **command_line;
#endif
	/*
	 * Use a non-const name variable to cope with python 2.6's
	 * PyImport_AppendInittab prototype
	 */
	char buf[PATH_MAX], name[19] = "perf_trace_context";
	int i, err = 0;
	FILE *fp;

	scripting_context->session = session;
#if PY_MAJOR_VERSION < 3
	command_line = malloc((argc + 1) * sizeof(const char *));
	if (!command_line)
		return -1;

	command_line[0] = script;
	for (i = 1; i < argc + 1; i++)
		command_line[i] = argv[i - 1];
	PyImport_AppendInittab(name, initperf_trace_context);
#else
	command_line = malloc((argc + 1) * sizeof(wchar_t *));
	if (!command_line)
		return -1;

	command_line[0] = Py_DecodeLocale(script, NULL);
	for (i = 1; i < argc + 1; i++)
		command_line[i] = Py_DecodeLocale(argv[i - 1], NULL);
	PyImport_AppendInittab(name, PyInit_perf_trace_context);
#endif
	Py_Initialize();

#if PY_MAJOR_VERSION < 3
	PySys_SetArgv(argc + 1, (char **)command_line);
#else
	PySys_SetArgv(argc + 1, command_line);
#endif

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

	set_table_handlers(tables);

	if (tables->db_export_mode) {
		err = db_export__branch_types(&tables->dbe);
		if (err)
			goto error;
	}

	_free_command_line(command_line, argc + 1);

	return err;
error:
	Py_Finalize();
	_free_command_line(command_line, argc + 1);

	return err;
}

static int python_flush_script(void)
{
	return 0;
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

#ifdef HAVE_LIBTRACEEVENT
static int python_generate_script(struct tep_handle *pevent, const char *outfile)
{
	int i, not_first, count, nr_events;
	struct tep_event **all_events;
	struct tep_event *event = NULL;
	struct tep_format_field *f;
	char fname[PATH_MAX];
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

	fprintf(ofp, "# See the perf-script-python Documentation for the list "
		"of available functions.\n\n");

	fprintf(ofp, "from __future__ import print_function\n\n");
	fprintf(ofp, "import os\n");
	fprintf(ofp, "import sys\n\n");

	fprintf(ofp, "sys.path.append(os.environ['PERF_EXEC_PATH'] + \\\n");
	fprintf(ofp, "\t'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')\n");
	fprintf(ofp, "\nfrom perf_trace_context import *\n");
	fprintf(ofp, "from Core import *\n\n\n");

	fprintf(ofp, "def trace_begin():\n");
	fprintf(ofp, "\tprint(\"in trace_begin\")\n\n");

	fprintf(ofp, "def trace_end():\n");
	fprintf(ofp, "\tprint(\"in trace_end\")\n\n");

	nr_events = tep_get_events_count(pevent);
	all_events = tep_list_events(pevent, TEP_EVENT_SORT_ID);

	for (i = 0; all_events && i < nr_events; i++) {
		event = all_events[i];
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
		if (not_first++)
			fprintf(ofp, ", ");
		if (++count % 5 == 0)
			fprintf(ofp, "\n\t\t");
		fprintf(ofp, "perf_sample_dict");

		fprintf(ofp, "):\n");

		fprintf(ofp, "\t\tprint_header(event_name, common_cpu, "
			"common_secs, common_nsecs,\n\t\t\t"
			"common_pid, common_comm)\n\n");

		fprintf(ofp, "\t\tprint(\"");

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
			if (f->flags & TEP_FIELD_IS_STRING ||
			    f->flags & TEP_FIELD_IS_FLAG ||
			    f->flags & TEP_FIELD_IS_ARRAY ||
			    f->flags & TEP_FIELD_IS_SYMBOLIC)
				fprintf(ofp, "%%s");
			else if (f->flags & TEP_FIELD_IS_SIGNED)
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

			if (f->flags & TEP_FIELD_IS_FLAG) {
				if ((count - 1) % 5 != 0) {
					fprintf(ofp, "\n\t\t");
					count = 4;
				}
				fprintf(ofp, "flag_str(\"");
				fprintf(ofp, "%s__%s\", ", event->system,
					event->name);
				fprintf(ofp, "\"%s\", %s)", f->name,
					f->name);
			} else if (f->flags & TEP_FIELD_IS_SYMBOLIC) {
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

		fprintf(ofp, "))\n\n");

		fprintf(ofp, "\t\tprint('Sample: {'+"
			"get_dict_as_string(perf_sample_dict['sample'], ', ')+'}')\n\n");

		fprintf(ofp, "\t\tfor node in common_callchain:");
		fprintf(ofp, "\n\t\t\tif 'sym' in node:");
		fprintf(ofp, "\n\t\t\t\tprint(\"\t[%%x] %%s%%s%%s%%s\" %% (");
		fprintf(ofp, "\n\t\t\t\t\tnode['ip'], node['sym']['name'],");
		fprintf(ofp, "\n\t\t\t\t\t\"+0x{:x}\".format(node['sym_off']) if 'sym_off' in node else \"\",");
		fprintf(ofp, "\n\t\t\t\t\t\" ({})\".format(node['dso'])  if 'dso' in node else \"\",");
		fprintf(ofp, "\n\t\t\t\t\t\" \" + node['sym_srcline'] if 'sym_srcline' in node else \"\"))");
		fprintf(ofp, "\n\t\t\telse:");
		fprintf(ofp, "\n\t\t\t\tprint(\"\t[%%x]\" %% (node['ip']))\n\n");
		fprintf(ofp, "\t\tprint()\n\n");

	}

	fprintf(ofp, "def trace_unhandled(event_name, context, "
		"event_fields_dict, perf_sample_dict):\n");

	fprintf(ofp, "\t\tprint(get_dict_as_string(event_fields_dict))\n");
	fprintf(ofp, "\t\tprint('Sample: {'+"
		"get_dict_as_string(perf_sample_dict['sample'], ', ')+'}')\n\n");

	fprintf(ofp, "def print_header("
		"event_name, cpu, secs, nsecs, pid, comm):\n"
		"\tprint(\"%%-20s %%5u %%05u.%%09u %%8u %%-20s \" %% \\\n\t"
		"(event_name, cpu, secs, nsecs, pid, comm), end=\"\")\n\n");

	fprintf(ofp, "def get_dict_as_string(a_dict, delimiter=' '):\n"
		"\treturn delimiter.join"
		"(['%%s=%%s'%%(k,str(v))for k,v in sorted(a_dict.items())])\n");

	fclose(ofp);

	fprintf(stderr, "generated Python script: %s\n", fname);

	return 0;
}
#else
static int python_generate_script(struct tep_handle *pevent __maybe_unused,
				  const char *outfile __maybe_unused)
{
	fprintf(stderr, "Generating Python perf-script is not supported."
		"  Install libtraceevent and rebuild perf to enable it.\n"
		"For example:\n  # apt install libtraceevent-dev (ubuntu)"
		"\n  # yum install libtraceevent-devel (Fedora)"
		"\n  etc.\n");
	return -1;
}
#endif

struct scripting_ops python_scripting_ops = {
	.name			= "Python",
	.dirname		= "python",
	.start_script		= python_start_script,
	.flush_script		= python_flush_script,
	.stop_script		= python_stop_script,
	.process_event		= python_process_event,
	.process_switch		= python_process_switch,
	.process_auxtrace_error	= python_process_auxtrace_error,
	.process_stat		= python_process_stat,
	.process_stat_interval	= python_process_stat_interval,
	.process_throttle	= python_process_throttle,
	.generate_script	= python_generate_script,
};
