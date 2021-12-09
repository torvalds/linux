// SPDX-License-Identifier: GPL-2.0
#include <Python.h>
#include <structmember.h>
#include <inttypes.h>
#include <poll.h>
#include <linux/err.h>
#include <perf/cpumap.h>
#include <traceevent/event-parse.h>
#include <perf/mmap.h>
#include "evlist.h"
#include "callchain.h"
#include "evsel.h"
#include "event.h"
#include "print_binary.h"
#include "thread_map.h"
#include "trace-event.h"
#include "mmap.h"
#include "stat.h"
#include "metricgroup.h"
#include "util/env.h"
#include <internal/lib.h>
#include "util.h"

#if PY_MAJOR_VERSION < 3
#define _PyUnicode_FromString(arg) \
  PyString_FromString(arg)
#define _PyUnicode_AsString(arg) \
  PyString_AsString(arg)
#define _PyUnicode_FromFormat(...) \
  PyString_FromFormat(__VA_ARGS__)
#define _PyLong_FromLong(arg) \
  PyInt_FromLong(arg)

#else

#define _PyUnicode_FromString(arg) \
  PyUnicode_FromString(arg)
#define _PyUnicode_FromFormat(...) \
  PyUnicode_FromFormat(__VA_ARGS__)
#define _PyLong_FromLong(arg) \
  PyLong_FromLong(arg)
#endif

#ifndef Py_TYPE
#define Py_TYPE(ob) (((PyObject*)(ob))->ob_type)
#endif

/*
 * Provide these two so that we don't have to link against callchain.c and
 * start dragging hist.c, etc.
 */
struct callchain_param callchain_param;

int parse_callchain_record(const char *arg __maybe_unused,
			   struct callchain_param *param __maybe_unused)
{
	return 0;
}

/*
 * Add this one here not to drag util/env.c
 */
struct perf_env perf_env;

/*
 * Add this one here not to drag util/stat-shadow.c
 */
void perf_stat__collect_metric_expr(struct evlist *evsel_list)
{
}

/*
 * This one is needed not to drag the PMU bandwagon, jevents generated
 * pmu_sys_event_tables, etc and evsel__find_pmu() is used so far just for
 * doing per PMU perf_event_attr.exclude_guest handling, not really needed, so
 * far, for the perf python binding known usecases, revisit if this become
 * necessary.
 */
struct perf_pmu *evsel__find_pmu(struct evsel *evsel __maybe_unused)
{
	return NULL;
}

/*
 * Add this one here not to drag util/metricgroup.c
 */
int metricgroup__copy_metric_events(struct evlist *evlist, struct cgroup *cgrp,
				    struct rblist *new_metric_events,
				    struct rblist *old_metric_events)
{
	return 0;
}

/*
 * XXX: All these evsel destructors need some better mechanism, like a linked
 * list of destructors registered when the relevant code indeed is used instead
 * of having more and more calls in perf_evsel__delete(). -- acme
 *
 * For now, add some more:
 *
 * Not to drag the BPF bandwagon...
 */
void bpf_counter__destroy(struct evsel *evsel);
int bpf_counter__install_pe(struct evsel *evsel, int cpu, int fd);
int bpf_counter__disable(struct evsel *evsel);

void bpf_counter__destroy(struct evsel *evsel __maybe_unused)
{
}

int bpf_counter__install_pe(struct evsel *evsel __maybe_unused, int cpu __maybe_unused, int fd __maybe_unused)
{
	return 0;
}

int bpf_counter__disable(struct evsel *evsel __maybe_unused)
{
	return 0;
}

/*
 * Support debug printing even though util/debug.c is not linked.  That means
 * implementing 'verbose' and 'eprintf'.
 */
int verbose;
int debug_peo_args;

int eprintf(int level, int var, const char *fmt, ...);

int eprintf(int level, int var, const char *fmt, ...)
{
	va_list args;
	int ret = 0;

	if (var >= level) {
		va_start(args, fmt);
		ret = vfprintf(stderr, fmt, args);
		va_end(args);
	}

	return ret;
}

/* Define PyVarObject_HEAD_INIT for python 2.5 */
#ifndef PyVarObject_HEAD_INIT
# define PyVarObject_HEAD_INIT(type, size) PyObject_HEAD_INIT(type) size,
#endif

#if PY_MAJOR_VERSION < 3
PyMODINIT_FUNC initperf(void);
#else
PyMODINIT_FUNC PyInit_perf(void);
#endif

#define member_def(type, member, ptype, help) \
	{ #member, ptype, \
	  offsetof(struct pyrf_event, event) + offsetof(struct type, member), \
	  0, help }

#define sample_member_def(name, member, ptype, help) \
	{ #name, ptype, \
	  offsetof(struct pyrf_event, sample) + offsetof(struct perf_sample, member), \
	  0, help }

struct pyrf_event {
	PyObject_HEAD
	struct evsel *evsel;
	struct perf_sample sample;
	union perf_event   event;
};

#define sample_members \
	sample_member_def(sample_ip, ip, T_ULONGLONG, "event type"),			 \
	sample_member_def(sample_pid, pid, T_INT, "event pid"),			 \
	sample_member_def(sample_tid, tid, T_INT, "event tid"),			 \
	sample_member_def(sample_time, time, T_ULONGLONG, "event timestamp"),		 \
	sample_member_def(sample_addr, addr, T_ULONGLONG, "event addr"),		 \
	sample_member_def(sample_id, id, T_ULONGLONG, "event id"),			 \
	sample_member_def(sample_stream_id, stream_id, T_ULONGLONG, "event stream id"), \
	sample_member_def(sample_period, period, T_ULONGLONG, "event period"),		 \
	sample_member_def(sample_cpu, cpu, T_UINT, "event cpu"),

static char pyrf_mmap_event__doc[] = PyDoc_STR("perf mmap event object.");

static PyMemberDef pyrf_mmap_event__members[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_event_header, misc, T_UINT, "event misc"),
	member_def(perf_record_mmap, pid, T_UINT, "event pid"),
	member_def(perf_record_mmap, tid, T_UINT, "event tid"),
	member_def(perf_record_mmap, start, T_ULONGLONG, "start of the map"),
	member_def(perf_record_mmap, len, T_ULONGLONG, "map length"),
	member_def(perf_record_mmap, pgoff, T_ULONGLONG, "page offset"),
	member_def(perf_record_mmap, filename, T_STRING_INPLACE, "backing store"),
	{ .name = NULL, },
};

static PyObject *pyrf_mmap_event__repr(struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;

	if (asprintf(&s, "{ type: mmap, pid: %u, tid: %u, start: %#" PRI_lx64 ", "
			 "length: %#" PRI_lx64 ", offset: %#" PRI_lx64 ", "
			 "filename: %s }",
		     pevent->event.mmap.pid, pevent->event.mmap.tid,
		     pevent->event.mmap.start, pevent->event.mmap.len,
		     pevent->event.mmap.pgoff, pevent->event.mmap.filename) < 0) {
		ret = PyErr_NoMemory();
	} else {
		ret = _PyUnicode_FromString(s);
		free(s);
	}
	return ret;
}

static PyTypeObject pyrf_mmap_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.mmap_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_mmap_event__doc,
	.tp_members	= pyrf_mmap_event__members,
	.tp_repr	= (reprfunc)pyrf_mmap_event__repr,
};

static char pyrf_task_event__doc[] = PyDoc_STR("perf task (fork/exit) event object.");

static PyMemberDef pyrf_task_event__members[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_fork, pid, T_UINT, "event pid"),
	member_def(perf_record_fork, ppid, T_UINT, "event ppid"),
	member_def(perf_record_fork, tid, T_UINT, "event tid"),
	member_def(perf_record_fork, ptid, T_UINT, "event ptid"),
	member_def(perf_record_fork, time, T_ULONGLONG, "timestamp"),
	{ .name = NULL, },
};

static PyObject *pyrf_task_event__repr(struct pyrf_event *pevent)
{
	return _PyUnicode_FromFormat("{ type: %s, pid: %u, ppid: %u, tid: %u, "
				   "ptid: %u, time: %" PRI_lu64 "}",
				   pevent->event.header.type == PERF_RECORD_FORK ? "fork" : "exit",
				   pevent->event.fork.pid,
				   pevent->event.fork.ppid,
				   pevent->event.fork.tid,
				   pevent->event.fork.ptid,
				   pevent->event.fork.time);
}

static PyTypeObject pyrf_task_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.task_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_task_event__doc,
	.tp_members	= pyrf_task_event__members,
	.tp_repr	= (reprfunc)pyrf_task_event__repr,
};

static char pyrf_comm_event__doc[] = PyDoc_STR("perf comm event object.");

static PyMemberDef pyrf_comm_event__members[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_comm, pid, T_UINT, "event pid"),
	member_def(perf_record_comm, tid, T_UINT, "event tid"),
	member_def(perf_record_comm, comm, T_STRING_INPLACE, "process name"),
	{ .name = NULL, },
};

static PyObject *pyrf_comm_event__repr(struct pyrf_event *pevent)
{
	return _PyUnicode_FromFormat("{ type: comm, pid: %u, tid: %u, comm: %s }",
				   pevent->event.comm.pid,
				   pevent->event.comm.tid,
				   pevent->event.comm.comm);
}

static PyTypeObject pyrf_comm_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.comm_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_comm_event__doc,
	.tp_members	= pyrf_comm_event__members,
	.tp_repr	= (reprfunc)pyrf_comm_event__repr,
};

static char pyrf_throttle_event__doc[] = PyDoc_STR("perf throttle event object.");

static PyMemberDef pyrf_throttle_event__members[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_throttle, time, T_ULONGLONG, "timestamp"),
	member_def(perf_record_throttle, id, T_ULONGLONG, "event id"),
	member_def(perf_record_throttle, stream_id, T_ULONGLONG, "event stream id"),
	{ .name = NULL, },
};

static PyObject *pyrf_throttle_event__repr(struct pyrf_event *pevent)
{
	struct perf_record_throttle *te = (struct perf_record_throttle *)(&pevent->event.header + 1);

	return _PyUnicode_FromFormat("{ type: %sthrottle, time: %" PRI_lu64 ", id: %" PRI_lu64
				   ", stream_id: %" PRI_lu64 " }",
				   pevent->event.header.type == PERF_RECORD_THROTTLE ? "" : "un",
				   te->time, te->id, te->stream_id);
}

static PyTypeObject pyrf_throttle_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.throttle_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_throttle_event__doc,
	.tp_members	= pyrf_throttle_event__members,
	.tp_repr	= (reprfunc)pyrf_throttle_event__repr,
};

static char pyrf_lost_event__doc[] = PyDoc_STR("perf lost event object.");

static PyMemberDef pyrf_lost_event__members[] = {
	sample_members
	member_def(perf_record_lost, id, T_ULONGLONG, "event id"),
	member_def(perf_record_lost, lost, T_ULONGLONG, "number of lost events"),
	{ .name = NULL, },
};

static PyObject *pyrf_lost_event__repr(struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;

	if (asprintf(&s, "{ type: lost, id: %#" PRI_lx64 ", "
			 "lost: %#" PRI_lx64 " }",
		     pevent->event.lost.id, pevent->event.lost.lost) < 0) {
		ret = PyErr_NoMemory();
	} else {
		ret = _PyUnicode_FromString(s);
		free(s);
	}
	return ret;
}

static PyTypeObject pyrf_lost_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.lost_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_lost_event__doc,
	.tp_members	= pyrf_lost_event__members,
	.tp_repr	= (reprfunc)pyrf_lost_event__repr,
};

static char pyrf_read_event__doc[] = PyDoc_STR("perf read event object.");

static PyMemberDef pyrf_read_event__members[] = {
	sample_members
	member_def(perf_record_read, pid, T_UINT, "event pid"),
	member_def(perf_record_read, tid, T_UINT, "event tid"),
	{ .name = NULL, },
};

static PyObject *pyrf_read_event__repr(struct pyrf_event *pevent)
{
	return _PyUnicode_FromFormat("{ type: read, pid: %u, tid: %u }",
				   pevent->event.read.pid,
				   pevent->event.read.tid);
	/*
 	 * FIXME: return the array of read values,
 	 * making this method useful ;-)
 	 */
}

static PyTypeObject pyrf_read_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.read_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_read_event__doc,
	.tp_members	= pyrf_read_event__members,
	.tp_repr	= (reprfunc)pyrf_read_event__repr,
};

static char pyrf_sample_event__doc[] = PyDoc_STR("perf sample event object.");

static PyMemberDef pyrf_sample_event__members[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	{ .name = NULL, },
};

static PyObject *pyrf_sample_event__repr(struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;

	if (asprintf(&s, "{ type: sample }") < 0) {
		ret = PyErr_NoMemory();
	} else {
		ret = _PyUnicode_FromString(s);
		free(s);
	}
	return ret;
}

static bool is_tracepoint(struct pyrf_event *pevent)
{
	return pevent->evsel->core.attr.type == PERF_TYPE_TRACEPOINT;
}

static PyObject*
tracepoint_field(struct pyrf_event *pe, struct tep_format_field *field)
{
	struct tep_handle *pevent = field->event->tep;
	void *data = pe->sample.raw_data;
	PyObject *ret = NULL;
	unsigned long long val;
	unsigned int offset, len;

	if (field->flags & TEP_FIELD_IS_ARRAY) {
		offset = field->offset;
		len    = field->size;
		if (field->flags & TEP_FIELD_IS_DYNAMIC) {
			val     = tep_read_number(pevent, data + offset, len);
			offset  = val;
			len     = offset >> 16;
			offset &= 0xffff;
		}
		if (field->flags & TEP_FIELD_IS_STRING &&
		    is_printable_array(data + offset, len)) {
			ret = _PyUnicode_FromString((char *)data + offset);
		} else {
			ret = PyByteArray_FromStringAndSize((const char *) data + offset, len);
			field->flags &= ~TEP_FIELD_IS_STRING;
		}
	} else {
		val = tep_read_number(pevent, data + field->offset,
				      field->size);
		if (field->flags & TEP_FIELD_IS_POINTER)
			ret = PyLong_FromUnsignedLong((unsigned long) val);
		else if (field->flags & TEP_FIELD_IS_SIGNED)
			ret = PyLong_FromLong((long) val);
		else
			ret = PyLong_FromUnsignedLong((unsigned long) val);
	}

	return ret;
}

static PyObject*
get_tracepoint_field(struct pyrf_event *pevent, PyObject *attr_name)
{
	const char *str = _PyUnicode_AsString(PyObject_Str(attr_name));
	struct evsel *evsel = pevent->evsel;
	struct tep_format_field *field;

	if (!evsel->tp_format) {
		struct tep_event *tp_format;

		tp_format = trace_event__tp_format_id(evsel->core.attr.config);
		if (!tp_format)
			return NULL;

		evsel->tp_format = tp_format;
	}

	field = tep_find_any_field(evsel->tp_format, str);
	if (!field)
		return NULL;

	return tracepoint_field(pevent, field);
}

static PyObject*
pyrf_sample_event__getattro(struct pyrf_event *pevent, PyObject *attr_name)
{
	PyObject *obj = NULL;

	if (is_tracepoint(pevent))
		obj = get_tracepoint_field(pevent, attr_name);

	return obj ?: PyObject_GenericGetAttr((PyObject *) pevent, attr_name);
}

static PyTypeObject pyrf_sample_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.sample_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_sample_event__doc,
	.tp_members	= pyrf_sample_event__members,
	.tp_repr	= (reprfunc)pyrf_sample_event__repr,
	.tp_getattro	= (getattrofunc) pyrf_sample_event__getattro,
};

static char pyrf_context_switch_event__doc[] = PyDoc_STR("perf context_switch event object.");

static PyMemberDef pyrf_context_switch_event__members[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_switch, next_prev_pid, T_UINT, "next/prev pid"),
	member_def(perf_record_switch, next_prev_tid, T_UINT, "next/prev tid"),
	{ .name = NULL, },
};

static PyObject *pyrf_context_switch_event__repr(struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;

	if (asprintf(&s, "{ type: context_switch, next_prev_pid: %u, next_prev_tid: %u, switch_out: %u }",
		     pevent->event.context_switch.next_prev_pid,
		     pevent->event.context_switch.next_prev_tid,
		     !!(pevent->event.header.misc & PERF_RECORD_MISC_SWITCH_OUT)) < 0) {
		ret = PyErr_NoMemory();
	} else {
		ret = _PyUnicode_FromString(s);
		free(s);
	}
	return ret;
}

static PyTypeObject pyrf_context_switch_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.context_switch_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_context_switch_event__doc,
	.tp_members	= pyrf_context_switch_event__members,
	.tp_repr	= (reprfunc)pyrf_context_switch_event__repr,
};

static int pyrf_event__setup_types(void)
{
	int err;
	pyrf_mmap_event__type.tp_new =
	pyrf_task_event__type.tp_new =
	pyrf_comm_event__type.tp_new =
	pyrf_lost_event__type.tp_new =
	pyrf_read_event__type.tp_new =
	pyrf_sample_event__type.tp_new =
	pyrf_context_switch_event__type.tp_new =
	pyrf_throttle_event__type.tp_new = PyType_GenericNew;
	err = PyType_Ready(&pyrf_mmap_event__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_lost_event__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_task_event__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_comm_event__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_throttle_event__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_read_event__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_sample_event__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_context_switch_event__type);
	if (err < 0)
		goto out;
out:
	return err;
}

static PyTypeObject *pyrf_event__type[] = {
	[PERF_RECORD_MMAP]	 = &pyrf_mmap_event__type,
	[PERF_RECORD_LOST]	 = &pyrf_lost_event__type,
	[PERF_RECORD_COMM]	 = &pyrf_comm_event__type,
	[PERF_RECORD_EXIT]	 = &pyrf_task_event__type,
	[PERF_RECORD_THROTTLE]	 = &pyrf_throttle_event__type,
	[PERF_RECORD_UNTHROTTLE] = &pyrf_throttle_event__type,
	[PERF_RECORD_FORK]	 = &pyrf_task_event__type,
	[PERF_RECORD_READ]	 = &pyrf_read_event__type,
	[PERF_RECORD_SAMPLE]	 = &pyrf_sample_event__type,
	[PERF_RECORD_SWITCH]	 = &pyrf_context_switch_event__type,
	[PERF_RECORD_SWITCH_CPU_WIDE]  = &pyrf_context_switch_event__type,
};

static PyObject *pyrf_event__new(union perf_event *event)
{
	struct pyrf_event *pevent;
	PyTypeObject *ptype;

	if ((event->header.type < PERF_RECORD_MMAP ||
	     event->header.type > PERF_RECORD_SAMPLE) &&
	    !(event->header.type == PERF_RECORD_SWITCH ||
	      event->header.type == PERF_RECORD_SWITCH_CPU_WIDE))
		return NULL;

	ptype = pyrf_event__type[event->header.type];
	pevent = PyObject_New(struct pyrf_event, ptype);
	if (pevent != NULL)
		memcpy(&pevent->event, event, event->header.size);
	return (PyObject *)pevent;
}

struct pyrf_cpu_map {
	PyObject_HEAD

	struct perf_cpu_map *cpus;
};

static int pyrf_cpu_map__init(struct pyrf_cpu_map *pcpus,
			      PyObject *args, PyObject *kwargs)
{
	static char *kwlist[] = { "cpustr", NULL };
	char *cpustr = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|s",
					 kwlist, &cpustr))
		return -1;

	pcpus->cpus = perf_cpu_map__new(cpustr);
	if (pcpus->cpus == NULL)
		return -1;
	return 0;
}

static void pyrf_cpu_map__delete(struct pyrf_cpu_map *pcpus)
{
	perf_cpu_map__put(pcpus->cpus);
	Py_TYPE(pcpus)->tp_free((PyObject*)pcpus);
}

static Py_ssize_t pyrf_cpu_map__length(PyObject *obj)
{
	struct pyrf_cpu_map *pcpus = (void *)obj;

	return pcpus->cpus->nr;
}

static PyObject *pyrf_cpu_map__item(PyObject *obj, Py_ssize_t i)
{
	struct pyrf_cpu_map *pcpus = (void *)obj;

	if (i >= pcpus->cpus->nr)
		return NULL;

	return Py_BuildValue("i", pcpus->cpus->map[i]);
}

static PySequenceMethods pyrf_cpu_map__sequence_methods = {
	.sq_length = pyrf_cpu_map__length,
	.sq_item   = pyrf_cpu_map__item,
};

static char pyrf_cpu_map__doc[] = PyDoc_STR("cpu map object.");

static PyTypeObject pyrf_cpu_map__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.cpu_map",
	.tp_basicsize	= sizeof(struct pyrf_cpu_map),
	.tp_dealloc	= (destructor)pyrf_cpu_map__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_cpu_map__doc,
	.tp_as_sequence	= &pyrf_cpu_map__sequence_methods,
	.tp_init	= (initproc)pyrf_cpu_map__init,
};

static int pyrf_cpu_map__setup_types(void)
{
	pyrf_cpu_map__type.tp_new = PyType_GenericNew;
	return PyType_Ready(&pyrf_cpu_map__type);
}

struct pyrf_thread_map {
	PyObject_HEAD

	struct perf_thread_map *threads;
};

static int pyrf_thread_map__init(struct pyrf_thread_map *pthreads,
				 PyObject *args, PyObject *kwargs)
{
	static char *kwlist[] = { "pid", "tid", "uid", NULL };
	int pid = -1, tid = -1, uid = UINT_MAX;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|iii",
					 kwlist, &pid, &tid, &uid))
		return -1;

	pthreads->threads = thread_map__new(pid, tid, uid);
	if (pthreads->threads == NULL)
		return -1;
	return 0;
}

static void pyrf_thread_map__delete(struct pyrf_thread_map *pthreads)
{
	perf_thread_map__put(pthreads->threads);
	Py_TYPE(pthreads)->tp_free((PyObject*)pthreads);
}

static Py_ssize_t pyrf_thread_map__length(PyObject *obj)
{
	struct pyrf_thread_map *pthreads = (void *)obj;

	return pthreads->threads->nr;
}

static PyObject *pyrf_thread_map__item(PyObject *obj, Py_ssize_t i)
{
	struct pyrf_thread_map *pthreads = (void *)obj;

	if (i >= pthreads->threads->nr)
		return NULL;

	return Py_BuildValue("i", pthreads->threads->map[i]);
}

static PySequenceMethods pyrf_thread_map__sequence_methods = {
	.sq_length = pyrf_thread_map__length,
	.sq_item   = pyrf_thread_map__item,
};

static char pyrf_thread_map__doc[] = PyDoc_STR("thread map object.");

static PyTypeObject pyrf_thread_map__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.thread_map",
	.tp_basicsize	= sizeof(struct pyrf_thread_map),
	.tp_dealloc	= (destructor)pyrf_thread_map__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_thread_map__doc,
	.tp_as_sequence	= &pyrf_thread_map__sequence_methods,
	.tp_init	= (initproc)pyrf_thread_map__init,
};

static int pyrf_thread_map__setup_types(void)
{
	pyrf_thread_map__type.tp_new = PyType_GenericNew;
	return PyType_Ready(&pyrf_thread_map__type);
}

struct pyrf_evsel {
	PyObject_HEAD

	struct evsel evsel;
};

static int pyrf_evsel__init(struct pyrf_evsel *pevsel,
			    PyObject *args, PyObject *kwargs)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
		.sample_type = PERF_SAMPLE_PERIOD | PERF_SAMPLE_TID,
	};
	static char *kwlist[] = {
		"type",
		"config",
		"sample_freq",
		"sample_period",
		"sample_type",
		"read_format",
		"disabled",
		"inherit",
		"pinned",
		"exclusive",
		"exclude_user",
		"exclude_kernel",
		"exclude_hv",
		"exclude_idle",
		"mmap",
		"context_switch",
		"comm",
		"freq",
		"inherit_stat",
		"enable_on_exec",
		"task",
		"watermark",
		"precise_ip",
		"mmap_data",
		"sample_id_all",
		"wakeup_events",
		"bp_type",
		"bp_addr",
		"bp_len",
		 NULL
	};
	u64 sample_period = 0;
	u32 disabled = 0,
	    inherit = 0,
	    pinned = 0,
	    exclusive = 0,
	    exclude_user = 0,
	    exclude_kernel = 0,
	    exclude_hv = 0,
	    exclude_idle = 0,
	    mmap = 0,
	    context_switch = 0,
	    comm = 0,
	    freq = 1,
	    inherit_stat = 0,
	    enable_on_exec = 0,
	    task = 0,
	    watermark = 0,
	    precise_ip = 0,
	    mmap_data = 0,
	    sample_id_all = 1;
	int idx = 0;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs,
					 "|iKiKKiiiiiiiiiiiiiiiiiiiiiiKK", kwlist,
					 &attr.type, &attr.config, &attr.sample_freq,
					 &sample_period, &attr.sample_type,
					 &attr.read_format, &disabled, &inherit,
					 &pinned, &exclusive, &exclude_user,
					 &exclude_kernel, &exclude_hv, &exclude_idle,
					 &mmap, &context_switch, &comm, &freq, &inherit_stat,
					 &enable_on_exec, &task, &watermark,
					 &precise_ip, &mmap_data, &sample_id_all,
					 &attr.wakeup_events, &attr.bp_type,
					 &attr.bp_addr, &attr.bp_len, &idx))
		return -1;

	/* union... */
	if (sample_period != 0) {
		if (attr.sample_freq != 0)
			return -1; /* FIXME: throw right exception */
		attr.sample_period = sample_period;
	}

	/* Bitfields */
	attr.disabled	    = disabled;
	attr.inherit	    = inherit;
	attr.pinned	    = pinned;
	attr.exclusive	    = exclusive;
	attr.exclude_user   = exclude_user;
	attr.exclude_kernel = exclude_kernel;
	attr.exclude_hv	    = exclude_hv;
	attr.exclude_idle   = exclude_idle;
	attr.mmap	    = mmap;
	attr.context_switch = context_switch;
	attr.comm	    = comm;
	attr.freq	    = freq;
	attr.inherit_stat   = inherit_stat;
	attr.enable_on_exec = enable_on_exec;
	attr.task	    = task;
	attr.watermark	    = watermark;
	attr.precise_ip	    = precise_ip;
	attr.mmap_data	    = mmap_data;
	attr.sample_id_all  = sample_id_all;
	attr.size	    = sizeof(attr);

	evsel__init(&pevsel->evsel, &attr, idx);
	return 0;
}

static void pyrf_evsel__delete(struct pyrf_evsel *pevsel)
{
	evsel__exit(&pevsel->evsel);
	Py_TYPE(pevsel)->tp_free((PyObject*)pevsel);
}

static PyObject *pyrf_evsel__open(struct pyrf_evsel *pevsel,
				  PyObject *args, PyObject *kwargs)
{
	struct evsel *evsel = &pevsel->evsel;
	struct perf_cpu_map *cpus = NULL;
	struct perf_thread_map *threads = NULL;
	PyObject *pcpus = NULL, *pthreads = NULL;
	int group = 0, inherit = 0;
	static char *kwlist[] = { "cpus", "threads", "group", "inherit", NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OOii", kwlist,
					 &pcpus, &pthreads, &group, &inherit))
		return NULL;

	if (pthreads != NULL)
		threads = ((struct pyrf_thread_map *)pthreads)->threads;

	if (pcpus != NULL)
		cpus = ((struct pyrf_cpu_map *)pcpus)->cpus;

	evsel->core.attr.inherit = inherit;
	/*
	 * This will group just the fds for this single evsel, to group
	 * multiple events, use evlist.open().
	 */
	if (evsel__open(evsel, cpus, threads) < 0) {
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyMethodDef pyrf_evsel__methods[] = {
	{
		.ml_name  = "open",
		.ml_meth  = (PyCFunction)pyrf_evsel__open,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("open the event selector file descriptor table.")
	},
	{ .ml_name = NULL, }
};

static char pyrf_evsel__doc[] = PyDoc_STR("perf event selector list object.");

static PyTypeObject pyrf_evsel__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.evsel",
	.tp_basicsize	= sizeof(struct pyrf_evsel),
	.tp_dealloc	= (destructor)pyrf_evsel__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_evsel__doc,
	.tp_methods	= pyrf_evsel__methods,
	.tp_init	= (initproc)pyrf_evsel__init,
};

static int pyrf_evsel__setup_types(void)
{
	pyrf_evsel__type.tp_new = PyType_GenericNew;
	return PyType_Ready(&pyrf_evsel__type);
}

struct pyrf_evlist {
	PyObject_HEAD

	struct evlist evlist;
};

static int pyrf_evlist__init(struct pyrf_evlist *pevlist,
			     PyObject *args, PyObject *kwargs __maybe_unused)
{
	PyObject *pcpus = NULL, *pthreads = NULL;
	struct perf_cpu_map *cpus;
	struct perf_thread_map *threads;

	if (!PyArg_ParseTuple(args, "OO", &pcpus, &pthreads))
		return -1;

	threads = ((struct pyrf_thread_map *)pthreads)->threads;
	cpus = ((struct pyrf_cpu_map *)pcpus)->cpus;
	evlist__init(&pevlist->evlist, cpus, threads);
	return 0;
}

static void pyrf_evlist__delete(struct pyrf_evlist *pevlist)
{
	evlist__exit(&pevlist->evlist);
	Py_TYPE(pevlist)->tp_free((PyObject*)pevlist);
}

static PyObject *pyrf_evlist__mmap(struct pyrf_evlist *pevlist,
				   PyObject *args, PyObject *kwargs)
{
	struct evlist *evlist = &pevlist->evlist;
	static char *kwlist[] = { "pages", "overwrite", NULL };
	int pages = 128, overwrite = false;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ii", kwlist,
					 &pages, &overwrite))
		return NULL;

	if (evlist__mmap(evlist, pages) < 0) {
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *pyrf_evlist__poll(struct pyrf_evlist *pevlist,
				   PyObject *args, PyObject *kwargs)
{
	struct evlist *evlist = &pevlist->evlist;
	static char *kwlist[] = { "timeout", NULL };
	int timeout = -1, n;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i", kwlist, &timeout))
		return NULL;

	n = evlist__poll(evlist, timeout);
	if (n < 0) {
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}

	return Py_BuildValue("i", n);
}

static PyObject *pyrf_evlist__get_pollfd(struct pyrf_evlist *pevlist,
					 PyObject *args __maybe_unused,
					 PyObject *kwargs __maybe_unused)
{
	struct evlist *evlist = &pevlist->evlist;
        PyObject *list = PyList_New(0);
	int i;

	for (i = 0; i < evlist->core.pollfd.nr; ++i) {
		PyObject *file;
#if PY_MAJOR_VERSION < 3
		FILE *fp = fdopen(evlist->core.pollfd.entries[i].fd, "r");

		if (fp == NULL)
			goto free_list;

		file = PyFile_FromFile(fp, "perf", "r", NULL);
#else
		file = PyFile_FromFd(evlist->core.pollfd.entries[i].fd, "perf", "r", -1,
				     NULL, NULL, NULL, 0);
#endif
		if (file == NULL)
			goto free_list;

		if (PyList_Append(list, file) != 0) {
			Py_DECREF(file);
			goto free_list;
		}

		Py_DECREF(file);
	}

	return list;
free_list:
	return PyErr_NoMemory();
}


static PyObject *pyrf_evlist__add(struct pyrf_evlist *pevlist,
				  PyObject *args,
				  PyObject *kwargs __maybe_unused)
{
	struct evlist *evlist = &pevlist->evlist;
	PyObject *pevsel;
	struct evsel *evsel;

	if (!PyArg_ParseTuple(args, "O", &pevsel))
		return NULL;

	Py_INCREF(pevsel);
	evsel = &((struct pyrf_evsel *)pevsel)->evsel;
	evsel->core.idx = evlist->core.nr_entries;
	evlist__add(evlist, evsel);

	return Py_BuildValue("i", evlist->core.nr_entries);
}

static struct mmap *get_md(struct evlist *evlist, int cpu)
{
	int i;

	for (i = 0; i < evlist->core.nr_mmaps; i++) {
		struct mmap *md = &evlist->mmap[i];

		if (md->core.cpu == cpu)
			return md;
	}

	return NULL;
}

static PyObject *pyrf_evlist__read_on_cpu(struct pyrf_evlist *pevlist,
					  PyObject *args, PyObject *kwargs)
{
	struct evlist *evlist = &pevlist->evlist;
	union perf_event *event;
	int sample_id_all = 1, cpu;
	static char *kwlist[] = { "cpu", "sample_id_all", NULL };
	struct mmap *md;
	int err;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|i", kwlist,
					 &cpu, &sample_id_all))
		return NULL;

	md = get_md(evlist, cpu);
	if (!md)
		return NULL;

	if (perf_mmap__read_init(&md->core) < 0)
		goto end;

	event = perf_mmap__read_event(&md->core);
	if (event != NULL) {
		PyObject *pyevent = pyrf_event__new(event);
		struct pyrf_event *pevent = (struct pyrf_event *)pyevent;
		struct evsel *evsel;

		if (pyevent == NULL)
			return PyErr_NoMemory();

		evsel = evlist__event2evsel(evlist, event);
		if (!evsel) {
			Py_INCREF(Py_None);
			return Py_None;
		}

		pevent->evsel = evsel;

		err = evsel__parse_sample(evsel, event, &pevent->sample);

		/* Consume the even only after we parsed it out. */
		perf_mmap__consume(&md->core);

		if (err)
			return PyErr_Format(PyExc_OSError,
					    "perf: can't parse sample, err=%d", err);
		return pyevent;
	}
end:
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *pyrf_evlist__open(struct pyrf_evlist *pevlist,
				   PyObject *args, PyObject *kwargs)
{
	struct evlist *evlist = &pevlist->evlist;
	int group = 0;
	static char *kwlist[] = { "group", NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OOii", kwlist, &group))
		return NULL;

	if (group)
		evlist__set_leader(evlist);

	if (evlist__open(evlist) < 0) {
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyMethodDef pyrf_evlist__methods[] = {
	{
		.ml_name  = "mmap",
		.ml_meth  = (PyCFunction)pyrf_evlist__mmap,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("mmap the file descriptor table.")
	},
	{
		.ml_name  = "open",
		.ml_meth  = (PyCFunction)pyrf_evlist__open,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("open the file descriptors.")
	},
	{
		.ml_name  = "poll",
		.ml_meth  = (PyCFunction)pyrf_evlist__poll,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("poll the file descriptor table.")
	},
	{
		.ml_name  = "get_pollfd",
		.ml_meth  = (PyCFunction)pyrf_evlist__get_pollfd,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("get the poll file descriptor table.")
	},
	{
		.ml_name  = "add",
		.ml_meth  = (PyCFunction)pyrf_evlist__add,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("adds an event selector to the list.")
	},
	{
		.ml_name  = "read_on_cpu",
		.ml_meth  = (PyCFunction)pyrf_evlist__read_on_cpu,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("reads an event.")
	},
	{ .ml_name = NULL, }
};

static Py_ssize_t pyrf_evlist__length(PyObject *obj)
{
	struct pyrf_evlist *pevlist = (void *)obj;

	return pevlist->evlist.core.nr_entries;
}

static PyObject *pyrf_evlist__item(PyObject *obj, Py_ssize_t i)
{
	struct pyrf_evlist *pevlist = (void *)obj;
	struct evsel *pos;

	if (i >= pevlist->evlist.core.nr_entries)
		return NULL;

	evlist__for_each_entry(&pevlist->evlist, pos) {
		if (i-- == 0)
			break;
	}

	return Py_BuildValue("O", container_of(pos, struct pyrf_evsel, evsel));
}

static PySequenceMethods pyrf_evlist__sequence_methods = {
	.sq_length = pyrf_evlist__length,
	.sq_item   = pyrf_evlist__item,
};

static char pyrf_evlist__doc[] = PyDoc_STR("perf event selector list object.");

static PyTypeObject pyrf_evlist__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.evlist",
	.tp_basicsize	= sizeof(struct pyrf_evlist),
	.tp_dealloc	= (destructor)pyrf_evlist__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_as_sequence	= &pyrf_evlist__sequence_methods,
	.tp_doc		= pyrf_evlist__doc,
	.tp_methods	= pyrf_evlist__methods,
	.tp_init	= (initproc)pyrf_evlist__init,
};

static int pyrf_evlist__setup_types(void)
{
	pyrf_evlist__type.tp_new = PyType_GenericNew;
	return PyType_Ready(&pyrf_evlist__type);
}

#define PERF_CONST(name) { #name, PERF_##name }

static struct {
	const char *name;
	int	    value;
} perf__constants[] = {
	PERF_CONST(TYPE_HARDWARE),
	PERF_CONST(TYPE_SOFTWARE),
	PERF_CONST(TYPE_TRACEPOINT),
	PERF_CONST(TYPE_HW_CACHE),
	PERF_CONST(TYPE_RAW),
	PERF_CONST(TYPE_BREAKPOINT),

	PERF_CONST(COUNT_HW_CPU_CYCLES),
	PERF_CONST(COUNT_HW_INSTRUCTIONS),
	PERF_CONST(COUNT_HW_CACHE_REFERENCES),
	PERF_CONST(COUNT_HW_CACHE_MISSES),
	PERF_CONST(COUNT_HW_BRANCH_INSTRUCTIONS),
	PERF_CONST(COUNT_HW_BRANCH_MISSES),
	PERF_CONST(COUNT_HW_BUS_CYCLES),
	PERF_CONST(COUNT_HW_CACHE_L1D),
	PERF_CONST(COUNT_HW_CACHE_L1I),
	PERF_CONST(COUNT_HW_CACHE_LL),
	PERF_CONST(COUNT_HW_CACHE_DTLB),
	PERF_CONST(COUNT_HW_CACHE_ITLB),
	PERF_CONST(COUNT_HW_CACHE_BPU),
	PERF_CONST(COUNT_HW_CACHE_OP_READ),
	PERF_CONST(COUNT_HW_CACHE_OP_WRITE),
	PERF_CONST(COUNT_HW_CACHE_OP_PREFETCH),
	PERF_CONST(COUNT_HW_CACHE_RESULT_ACCESS),
	PERF_CONST(COUNT_HW_CACHE_RESULT_MISS),

	PERF_CONST(COUNT_HW_STALLED_CYCLES_FRONTEND),
	PERF_CONST(COUNT_HW_STALLED_CYCLES_BACKEND),

	PERF_CONST(COUNT_SW_CPU_CLOCK),
	PERF_CONST(COUNT_SW_TASK_CLOCK),
	PERF_CONST(COUNT_SW_PAGE_FAULTS),
	PERF_CONST(COUNT_SW_CONTEXT_SWITCHES),
	PERF_CONST(COUNT_SW_CPU_MIGRATIONS),
	PERF_CONST(COUNT_SW_PAGE_FAULTS_MIN),
	PERF_CONST(COUNT_SW_PAGE_FAULTS_MAJ),
	PERF_CONST(COUNT_SW_ALIGNMENT_FAULTS),
	PERF_CONST(COUNT_SW_EMULATION_FAULTS),
	PERF_CONST(COUNT_SW_DUMMY),

	PERF_CONST(SAMPLE_IP),
	PERF_CONST(SAMPLE_TID),
	PERF_CONST(SAMPLE_TIME),
	PERF_CONST(SAMPLE_ADDR),
	PERF_CONST(SAMPLE_READ),
	PERF_CONST(SAMPLE_CALLCHAIN),
	PERF_CONST(SAMPLE_ID),
	PERF_CONST(SAMPLE_CPU),
	PERF_CONST(SAMPLE_PERIOD),
	PERF_CONST(SAMPLE_STREAM_ID),
	PERF_CONST(SAMPLE_RAW),

	PERF_CONST(FORMAT_TOTAL_TIME_ENABLED),
	PERF_CONST(FORMAT_TOTAL_TIME_RUNNING),
	PERF_CONST(FORMAT_ID),
	PERF_CONST(FORMAT_GROUP),

	PERF_CONST(RECORD_MMAP),
	PERF_CONST(RECORD_LOST),
	PERF_CONST(RECORD_COMM),
	PERF_CONST(RECORD_EXIT),
	PERF_CONST(RECORD_THROTTLE),
	PERF_CONST(RECORD_UNTHROTTLE),
	PERF_CONST(RECORD_FORK),
	PERF_CONST(RECORD_READ),
	PERF_CONST(RECORD_SAMPLE),
	PERF_CONST(RECORD_MMAP2),
	PERF_CONST(RECORD_AUX),
	PERF_CONST(RECORD_ITRACE_START),
	PERF_CONST(RECORD_LOST_SAMPLES),
	PERF_CONST(RECORD_SWITCH),
	PERF_CONST(RECORD_SWITCH_CPU_WIDE),

	PERF_CONST(RECORD_MISC_SWITCH_OUT),
	{ .name = NULL, },
};

static PyObject *pyrf__tracepoint(struct pyrf_evsel *pevsel,
				  PyObject *args, PyObject *kwargs)
{
	struct tep_event *tp_format;
	static char *kwlist[] = { "sys", "name", NULL };
	char *sys  = NULL;
	char *name = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ss", kwlist,
					 &sys, &name))
		return NULL;

	tp_format = trace_event__tp_format(sys, name);
	if (IS_ERR(tp_format))
		return _PyLong_FromLong(-1);

	return _PyLong_FromLong(tp_format->id);
}

static PyMethodDef perf__methods[] = {
	{
		.ml_name  = "tracepoint",
		.ml_meth  = (PyCFunction) pyrf__tracepoint,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("Get tracepoint config.")
	},
	{ .ml_name = NULL, }
};

#if PY_MAJOR_VERSION < 3
PyMODINIT_FUNC initperf(void)
#else
PyMODINIT_FUNC PyInit_perf(void)
#endif
{
	PyObject *obj;
	int i;
	PyObject *dict;
#if PY_MAJOR_VERSION < 3
	PyObject *module = Py_InitModule("perf", perf__methods);
#else
	static struct PyModuleDef moduledef = {
		PyModuleDef_HEAD_INIT,
		"perf",			/* m_name */
		"",			/* m_doc */
		-1,			/* m_size */
		perf__methods,		/* m_methods */
		NULL,			/* m_reload */
		NULL,			/* m_traverse */
		NULL,			/* m_clear */
		NULL,			/* m_free */
	};
	PyObject *module = PyModule_Create(&moduledef);
#endif

	if (module == NULL ||
	    pyrf_event__setup_types() < 0 ||
	    pyrf_evlist__setup_types() < 0 ||
	    pyrf_evsel__setup_types() < 0 ||
	    pyrf_thread_map__setup_types() < 0 ||
	    pyrf_cpu_map__setup_types() < 0)
#if PY_MAJOR_VERSION < 3
		return;
#else
		return module;
#endif

	/* The page_size is placed in util object. */
	page_size = sysconf(_SC_PAGE_SIZE);

	Py_INCREF(&pyrf_evlist__type);
	PyModule_AddObject(module, "evlist", (PyObject*)&pyrf_evlist__type);

	Py_INCREF(&pyrf_evsel__type);
	PyModule_AddObject(module, "evsel", (PyObject*)&pyrf_evsel__type);

	Py_INCREF(&pyrf_mmap_event__type);
	PyModule_AddObject(module, "mmap_event", (PyObject *)&pyrf_mmap_event__type);

	Py_INCREF(&pyrf_lost_event__type);
	PyModule_AddObject(module, "lost_event", (PyObject *)&pyrf_lost_event__type);

	Py_INCREF(&pyrf_comm_event__type);
	PyModule_AddObject(module, "comm_event", (PyObject *)&pyrf_comm_event__type);

	Py_INCREF(&pyrf_task_event__type);
	PyModule_AddObject(module, "task_event", (PyObject *)&pyrf_task_event__type);

	Py_INCREF(&pyrf_throttle_event__type);
	PyModule_AddObject(module, "throttle_event", (PyObject *)&pyrf_throttle_event__type);

	Py_INCREF(&pyrf_task_event__type);
	PyModule_AddObject(module, "task_event", (PyObject *)&pyrf_task_event__type);

	Py_INCREF(&pyrf_read_event__type);
	PyModule_AddObject(module, "read_event", (PyObject *)&pyrf_read_event__type);

	Py_INCREF(&pyrf_sample_event__type);
	PyModule_AddObject(module, "sample_event", (PyObject *)&pyrf_sample_event__type);

	Py_INCREF(&pyrf_context_switch_event__type);
	PyModule_AddObject(module, "switch_event", (PyObject *)&pyrf_context_switch_event__type);

	Py_INCREF(&pyrf_thread_map__type);
	PyModule_AddObject(module, "thread_map", (PyObject*)&pyrf_thread_map__type);

	Py_INCREF(&pyrf_cpu_map__type);
	PyModule_AddObject(module, "cpu_map", (PyObject*)&pyrf_cpu_map__type);

	dict = PyModule_GetDict(module);
	if (dict == NULL)
		goto error;

	for (i = 0; perf__constants[i].name != NULL; i++) {
		obj = _PyLong_FromLong(perf__constants[i].value);
		if (obj == NULL)
			goto error;
		PyDict_SetItemString(dict, perf__constants[i].name, obj);
		Py_DECREF(obj);
	}

error:
	if (PyErr_Occurred())
		PyErr_SetString(PyExc_ImportError, "perf: Init failed!");
#if PY_MAJOR_VERSION >= 3
	return module;
#endif
}

/*
 * Dummy, to avoid dragging all the test_attr infrastructure in the python
 * binding.
 */
void test_attr__open(struct perf_event_attr *attr, pid_t pid, int cpu,
                     int fd, int group_fd, unsigned long flags)
{
}
