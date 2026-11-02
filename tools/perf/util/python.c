// SPDX-License-Identifier: GPL-2.0
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <elf.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <linux/err.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <internal/lib.h>
#include <perf/cpumap.h>
#include <perf/mmap.h>
#include <structmember.h>

#include "addr_location.h"
#include "build-id.h"
#include "callchain.h"
#include "comm.h"
#include "config.h"
#include "counts.h"
#include "data.h"
#include "debug.h"
#include "call-path.h"
#include "db-export.h"
#include "thread-stack.h"
#include "dso.h"
#include "dwarf-regs.h"
#include "event.h"
#include "env.h"
#include "branch.h"
#include "evlist.h"
#include "evsel.h"
#include "symbol.h"
#include "expr.h"
#include "map.h"
#include "metricgroup.h"
#include "mmap.h"
#include "pmus.h"
#include "print_binary.h"
#include "record.h"
#include "sample.h"
#include "session.h"
#include "srccode.h"
#include "srcline.h"
#include "strbuf.h"
#include "strlist.h"
#include "symbol.h"
#include "stat.h"
#include "header.h"
#include "trace/beauty/syscalltbl.h"
#include "thread.h"
#include "thread_map.h"
#include "tool.h"
#include "tp_pmu.h"
#include "trace-event.h"

#ifdef HAVE_LIBTRACEEVENT
#include <event-parse.h>
#endif

PyMODINIT_FUNC PyInit_perf(void);

static PyObject *pyrf_evsel__from_evsel(struct evsel *evsel);

#define CHECK_INITIALIZED(ptr, msg) \
	do { \
		if (!(ptr)) { \
			PyErr_SetString(PyExc_ValueError, msg " not initialized"); \
			return NULL; \
		} \
	} while (0)

#define CHECK_INITIALIZED_INT(ptr, msg) \
	do { \
		if (!(ptr)) { \
			PyErr_SetString(PyExc_ValueError, msg " not initialized"); \
			return -1; \
		} \
	} while (0)

struct pyrf_event {
	PyObject_HEAD
	/** @sample: The parsed sample, pointing to @sample_storage or a borrowed sample. */
	struct perf_sample *sample;
	/** @sample_storage: Storage for @sample when owned by this object. */
	struct perf_sample sample_storage;
	/** @evsel: The evsel associated with the event. */
	struct evsel *evsel;
	/** @machine: The machine associated with the sample/event. */
	struct machine *machine;
	/** @al: The address location from machine__resolve, lazily computed. */
	struct addr_location al;
	/** @al_resolved: True when machine__resolve been called. */
	bool al_resolved;
	/** @callchain: Resolved callchain, lazily computed. */
	PyObject *callchain;
	/** @brstack: Resolved branch stack, eagerly computed if requested. */
	PyObject *brstack;
	/** @event: The underlying perf_event, pointing to @event_copy or a borrowed event. */
	const union perf_event *event;
	/** @event_copy: Copy of @event when owned by this object. */
	union perf_event *event_copy;
};

static const union perf_event zero_event;

static PyObject *pyrf_event__get_member(PyObject *self, void *closure)
{
	struct pyrf_event *pevent = (void *)self;
	const union perf_event *event = pevent->event ?: &zero_event;

	return PyMember_GetOne((const char *)event, closure);
}

static PyObject *pyrf_event__get_sample_member(PyObject *self, void *closure)
{
	struct pyrf_event *pevent = (void *)self;
	const struct perf_sample *sample = pevent->sample ?: &pevent->sample_storage;

	return PyMember_GetOne((const char *)sample, closure);
}

#define named_member_def(field, type, member, ptype, help) \
	{ .name = #field, .get = pyrf_event__get_member, .doc = help, \
	  .closure = &(struct PyMemberDef){ #field, ptype, \
					    offsetof(struct type, member), READONLY, help } }

#define member_def(type, member, ptype, help) \
	named_member_def(member, type, member, ptype, help)

struct pyrf_string_member {
	size_t offset;
	size_t maxlen;
};

static size_t pyrf_event__str_len(const struct pyrf_event *pevent, size_t offset, size_t maxlen)
{
	const char *base = (const char *)(pevent->event ?: &zero_event);
	size_t event_size = pevent->event ? pevent->event->header.size : 0;

	if (event_size <= offset)
		return 0;
	return strnlen(base + offset, min(event_size - offset, maxlen));
}

static PyObject *pyrf_event__get_string_member(PyObject *self, void *closure)
{
	struct pyrf_event *pevent = (void *)self;
	const struct pyrf_string_member *sm = closure;
	const char *base = (const char *)(pevent->event ?: &zero_event);

	return PyUnicode_FromStringAndSize(base + sm->offset,
					   pyrf_event__str_len(pevent, sm->offset, sm->maxlen));
}

#define string_member_def(type, member, help) \
	{ .name = #member, .get = pyrf_event__get_string_member, .doc = help, \
	  .closure = &(struct pyrf_string_member){ offsetof(struct type, member), \
						   sizeof(((struct type *)0)->member) } }

#define sample_member_def(field, member, ptype, help) \
	{ .name = #field, .get = pyrf_event__get_sample_member, .doc = help, \
	  .closure = &(struct PyMemberDef){ #field, ptype, \
					    offsetof(struct perf_sample, member), READONLY, help } }

static PyObject *pyrf_event__get_evsel(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (!pevent->sample || !pevent->sample->evsel)
		Py_RETURN_NONE;

	return pyrf_evsel__from_evsel(pevent->sample->evsel);
}

#define sample_members \
	{ .name = "evsel", .get = pyrf_event__get_evsel, .doc = "tracking event." },	 \
	sample_member_def(sample_pid, pid, T_INT, "event pid"),			 \
	sample_member_def(sample_tid, tid, T_INT, "event tid"),			 \
	sample_member_def(sample_time, time, T_ULONGLONG, "event timestamp"),		 \
	sample_member_def(sample_id, id, T_ULONGLONG, "event id"),			 \
	sample_member_def(sample_stream_id, stream_id, T_ULONGLONG, "event stream id"), \
	sample_member_def(machine_pid, machine_pid, T_UINT, "event machine pid"),	 \
	sample_member_def(vcpu, vcpu, T_UINT, "event vcpu"),				 \
	sample_member_def(sample_period, period, T_ULONGLONG, "event period"),		 \
	sample_member_def(sample_cpu, cpu, T_UINT, "event cpu"),

static void pyrf_event__delete(struct pyrf_event *pevent)
{
	if (pevent->al_resolved)
		addr_location__exit(&pevent->al);
	Py_XDECREF(pevent->callchain);
	Py_XDECREF(pevent->brstack);
	perf_sample__exit(&pevent->sample_storage);
	evsel__put(pevent->evsel);
	free(pevent->event_copy);
	Py_TYPE(pevent)->tp_free((PyObject *)pevent);
}

static const char pyrf_mmap_event__doc[] = PyDoc_STR("perf mmap event object.");

static PyGetSetDef pyrf_mmap_event__getset[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_event_header, misc, T_USHORT, "event misc"),
	member_def(perf_record_mmap, pid, T_UINT, "event pid"),
	member_def(perf_record_mmap, tid, T_UINT, "event tid"),
	member_def(perf_record_mmap, start, T_ULONGLONG, "start of the map"),
	member_def(perf_record_mmap, len, T_ULONGLONG, "map length"),
	member_def(perf_record_mmap, pgoff, T_ULONGLONG, "page offset"),
	string_member_def(perf_record_mmap, filename, "backing store"),
	{ .name = NULL, },
};

static PyObject *pyrf_mmap_event__repr(const struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;
	int fn_len = pyrf_event__str_len(pevent, offsetof(struct perf_record_mmap, filename),
					 sizeof(pevent->event->mmap.filename));

	if (asprintf(&s, "{ type: mmap, pid: %u, tid: %u, start: %#" PRI_lx64 ", "
			 "length: %#" PRI_lx64 ", offset: %#" PRI_lx64 ", "
			 "filename: %.*s }",
		     pevent->event->mmap.pid, pevent->event->mmap.tid,
		     pevent->event->mmap.start, pevent->event->mmap.len,
		     pevent->event->mmap.pgoff, fn_len, pevent->event->mmap.filename) < 0) {
		ret = PyErr_NoMemory();
	} else {
		ret = PyUnicode_FromString(s);
		free(s);
	}
	return ret;
}

static PyTypeObject pyrf_mmap_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.mmap_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_mmap_event__doc,
	.tp_getset	= pyrf_mmap_event__getset,
	.tp_repr	= (reprfunc)pyrf_mmap_event__repr,
};

static const char pyrf_mmap2_event__doc[] = PyDoc_STR("perf mmap2 event object.");

static PyObject *pyrf_mmap2_event__get_maj(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (pevent->event->header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLong(pevent->event->mmap2.maj);
}

static PyObject *pyrf_mmap2_event__get_min(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (pevent->event->header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLong(pevent->event->mmap2.min);
}

static PyObject *pyrf_mmap2_event__get_ino(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (pevent->event->header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLongLong(pevent->event->mmap2.ino);
}

static PyObject *pyrf_mmap2_event__get_ino_generation(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (pevent->event->header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLongLong(pevent->event->mmap2.ino_generation);
}

static PyObject *pyrf_mmap2_event__get_build_id(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (!(pevent->event->header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID))
		Py_RETURN_NONE;

	int size = pevent->event->mmap2.build_id_size;

	if (size > 20)
		size = 20;

	return PyBytes_FromStringAndSize((const char *)pevent->event->mmap2.build_id, size);
}

static PyGetSetDef pyrf_mmap2_event__getset[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_event_header, misc, T_USHORT, "event misc"),
	member_def(perf_record_mmap2, pid, T_UINT, "event pid"),
	member_def(perf_record_mmap2, tid, T_UINT, "event tid"),
	member_def(perf_record_mmap2, start, T_ULONGLONG, "start of the map"),
	member_def(perf_record_mmap2, len, T_ULONGLONG, "map length"),
	member_def(perf_record_mmap2, pgoff, T_ULONGLONG, "page offset"),
	member_def(perf_record_mmap2, prot, T_UINT, "protection"),
	member_def(perf_record_mmap2, flags, T_UINT, "flags"),
	string_member_def(perf_record_mmap2, filename, "backing store"),
	{
		.name = "maj",
		.get = pyrf_mmap2_event__get_maj,
		.set = NULL,
		.doc = "major number.",
	},
	{
		.name = "min",
		.get = pyrf_mmap2_event__get_min,
		.set = NULL,
		.doc = "minor number.",
	},
	{
		.name = "ino",
		.get = pyrf_mmap2_event__get_ino,
		.set = NULL,
		.doc = "inode number.",
	},
	{
		.name = "ino_generation",
		.get = pyrf_mmap2_event__get_ino_generation,
		.set = NULL,
		.doc = "inode generation.",
	},
	{
		.name = "build_id",
		.get = pyrf_mmap2_event__get_build_id,
		.set = NULL,
		.doc = "binary build ID.",
	},
	{ .name = NULL, },
};

static PyObject *pyrf_mmap2_event__repr(const struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;
	int fn_len = pyrf_event__str_len(pevent, offsetof(struct perf_record_mmap2, filename),
					 sizeof(pevent->event->mmap2.filename));

	if (asprintf(&s, "{ type: mmap2, pid: %u, tid: %u, start: %#" PRI_lx64 ", length: %#" PRI_lx64 ", offset: %#" PRI_lx64 ", flags: %#x, prot: %#x, filename: %.*s }",
		     pevent->event->mmap2.pid, pevent->event->mmap2.tid,
		     pevent->event->mmap2.start, pevent->event->mmap2.len,
		     pevent->event->mmap2.pgoff, pevent->event->mmap2.flags,
		     pevent->event->mmap2.prot, fn_len, pevent->event->mmap2.filename) < 0)
		return PyErr_NoMemory();

	ret = PyUnicode_FromString(s);
	free(s);
	return ret;
}

static PyTypeObject pyrf_mmap2_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.mmap2_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_mmap2_event__doc,
	.tp_getset	= pyrf_mmap2_event__getset,
	.tp_repr	= (reprfunc)pyrf_mmap2_event__repr,
};

static const char pyrf_task_event__doc[] = PyDoc_STR("perf task (fork/exit) event object.");

static PyGetSetDef pyrf_task_event__getset[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_fork, pid, T_UINT, "event pid"),
	member_def(perf_record_fork, ppid, T_UINT, "event ppid"),
	member_def(perf_record_fork, tid, T_UINT, "event tid"),
	member_def(perf_record_fork, ptid, T_UINT, "event ptid"),
	member_def(perf_record_fork, time, T_ULONGLONG, "timestamp"),
	{ .name = NULL, },
};

static PyObject *pyrf_task_event__repr(const struct pyrf_event *pevent)
{
	return PyUnicode_FromFormat("{ type: %s, pid: %u, ppid: %u, tid: %u, "
				   "ptid: %u, time: %" PRI_lu64 "}",
				   pevent->event->header.type == PERF_RECORD_FORK ? "fork" : "exit",
				   pevent->event->fork.pid,
				   pevent->event->fork.ppid,
				   pevent->event->fork.tid,
				   pevent->event->fork.ptid,
				   pevent->event->fork.time);
}

static PyTypeObject pyrf_task_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.task_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_task_event__doc,
	.tp_getset	= pyrf_task_event__getset,
	.tp_repr	= (reprfunc)pyrf_task_event__repr,
};

static const char pyrf_comm_event__doc[] = PyDoc_STR("perf comm event object.");

static PyGetSetDef pyrf_comm_event__getset[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_comm, pid, T_UINT, "event pid"),
	member_def(perf_record_comm, tid, T_UINT, "event tid"),
	string_member_def(perf_record_comm, comm, "process name"),
	{ .name = NULL, },
};

static PyObject *pyrf_comm_event__repr(const struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;
	int comm_len = pyrf_event__str_len(pevent, offsetof(struct perf_record_comm, comm),
					   sizeof(pevent->event->comm.comm));

	if (asprintf(&s, "{ type: comm, pid: %u, tid: %u, comm: %.*s }",
		     pevent->event->comm.pid, pevent->event->comm.tid,
		     comm_len, pevent->event->comm.comm) < 0)
		return PyErr_NoMemory();

	ret = PyUnicode_FromString(s);
	free(s);
	return ret;
}

static PyTypeObject pyrf_comm_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.comm_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_comm_event__doc,
	.tp_getset	= pyrf_comm_event__getset,
	.tp_repr	= (reprfunc)pyrf_comm_event__repr,
};

static const char pyrf_throttle_event__doc[] = PyDoc_STR("perf throttle event object.");

static PyGetSetDef pyrf_throttle_event__getset[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_throttle, time, T_ULONGLONG, "timestamp"),
	member_def(perf_record_throttle, id, T_ULONGLONG, "event id"),
	member_def(perf_record_throttle, stream_id, T_ULONGLONG, "event stream id"),
	{ .name = NULL, },
};

static PyObject *pyrf_throttle_event__repr(const struct pyrf_event *pevent)
{
	const struct perf_record_throttle *te = (const struct perf_record_throttle *)
		(&pevent->event->header + 1);

	return PyUnicode_FromFormat("{ type: %sthrottle, time: %" PRI_lu64 ", id: %" PRI_lu64
				   ", stream_id: %" PRI_lu64 " }",
				   pevent->event->header.type == PERF_RECORD_THROTTLE ? "" : "un",
				   te->time, te->id, te->stream_id);
}

static PyTypeObject pyrf_throttle_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.throttle_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_throttle_event__doc,
	.tp_getset	= pyrf_throttle_event__getset,
	.tp_repr	= (reprfunc)pyrf_throttle_event__repr,
};

static const char pyrf_lost_event__doc[] = PyDoc_STR("perf lost event object.");

static PyGetSetDef pyrf_lost_event__getset[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_lost, id, T_ULONGLONG, "event id"),
	member_def(perf_record_lost, lost, T_ULONGLONG, "number of lost events"),
	{ .name = NULL, },
};

static PyObject *pyrf_lost_event__repr(const struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;

	if (asprintf(&s, "{ type: lost, id: %#" PRI_lx64 ", "
			 "lost: %#" PRI_lx64 " }",
		     pevent->event->lost.id, pevent->event->lost.lost) < 0) {
		ret = PyErr_NoMemory();
	} else {
		ret = PyUnicode_FromString(s);
		free(s);
	}
	return ret;
}

static PyTypeObject pyrf_lost_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.lost_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_lost_event__doc,
	.tp_getset	= pyrf_lost_event__getset,
	.tp_repr	= (reprfunc)pyrf_lost_event__repr,
};

static const char pyrf_stat_event__doc[] = PyDoc_STR("perf stat event object.");

static PyGetSetDef pyrf_stat_event__getset[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_stat, id, T_ULONGLONG, "event id"),
	member_def(perf_record_stat, cpu, T_UINT, "event cpu"),
	member_def(perf_record_stat, thread, T_UINT, "event thread"),
	member_def(perf_record_stat, val, T_ULONGLONG, "counter value"),
	member_def(perf_record_stat, ena, T_ULONGLONG, "enabled time"),
	member_def(perf_record_stat, run, T_ULONGLONG, "running time"),
	{ .name = NULL, },
};

static PyObject *pyrf_stat_event__repr(const struct pyrf_event *pevent)
{
	const union perf_event *event = pevent->event ?: &zero_event;

	return PyUnicode_FromFormat(
		"{ type: stat, id: %llu, cpu: %u, thread: %u, val: %llu, ena: %llu, run: %llu }",
		event->stat.id,
		event->stat.cpu,
		event->stat.thread,
		event->stat.val,
		event->stat.ena,
		event->stat.run);
}

static PyTypeObject pyrf_stat_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.stat_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_new		= PyType_GenericNew,
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_stat_event__doc,
	.tp_getset	= pyrf_stat_event__getset,
	.tp_repr	= (reprfunc)pyrf_stat_event__repr,
};

static const char pyrf_stat_round_event__doc[] = PyDoc_STR("perf stat round event object.");

static PyGetSetDef pyrf_stat_round_event__getset[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	named_member_def(stat_round_type, perf_record_stat_round, type, T_ULONGLONG, "round type"),
	member_def(perf_record_stat_round, time, T_ULONGLONG, "round time"),
	{ .name = NULL, },
};

static PyObject *pyrf_stat_round_event__repr(const struct pyrf_event *pevent)
{
	const union perf_event *event = pevent->event ?: &zero_event;

	return PyUnicode_FromFormat("{ type: stat_round, type: %llu, time: %llu }",
				   event->stat_round.type,
				   event->stat_round.time);
}

static PyTypeObject pyrf_stat_round_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.stat_round_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_new		= PyType_GenericNew,
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_stat_round_event__doc,
	.tp_getset	= pyrf_stat_round_event__getset,
	.tp_repr	= (reprfunc)pyrf_stat_round_event__repr,
};

static const char pyrf_read_event__doc[] = PyDoc_STR("perf read event object.");

static PyGetSetDef pyrf_read_event__getset[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_read, pid, T_UINT, "event pid"),
	member_def(perf_record_read, tid, T_UINT, "event tid"),
	{ .name = NULL, },
};

static PyObject *pyrf_read_event__repr(const struct pyrf_event *pevent)
{
	return PyUnicode_FromFormat("{ type: read, pid: %u, tid: %u }",
				   pevent->event->read.pid,
				   pevent->event->read.tid);
	/*
 	 * FIXME: return the array of read values,
 	 * making this method useful ;-)
 	 */
}

static PyTypeObject pyrf_read_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.read_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_read_event__doc,
	.tp_getset	= pyrf_read_event__getset,
	.tp_repr	= (reprfunc)pyrf_read_event__repr,
};

static const char pyrf_sample_event__doc[] = PyDoc_STR("perf sample event object.");

static PyObject *pyrf_sample_event__repr(const struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;

	if (asprintf(&s, "{ type: sample }") < 0) {
		ret = PyErr_NoMemory();
	} else {
		ret = PyUnicode_FromString(s);
		free(s);
	}
	return ret;
}

#ifdef HAVE_LIBTRACEEVENT
static bool is_tracepoint(const struct pyrf_event *pevent)
{
	struct evsel *evsel = pevent->sample->evsel ?: pevent->evsel;

	if (!evsel)
		return false;
	return evsel->core.attr.type == PERF_TYPE_TRACEPOINT;
}

static PyObject*
tracepoint_field(const struct pyrf_event *pe, struct tep_format_field *field)
{
	struct tep_handle *pevent = field->event->tep;
	void *data = pe->sample->raw_data;
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
			if (tep_field_is_relative(field->flags))
				offset += field->offset + field->size;
		}
		if (field->flags & TEP_FIELD_IS_STRING &&
		    is_printable_array(data + offset, len)) {
			ret = PyUnicode_FromString((char *)data + offset);
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
	struct evsel *evsel = pevent->sample->evsel ?: pevent->evsel;
	struct tep_event *tp_format = evsel__tp_format(evsel);
	struct tep_format_field *field;

	if (IS_ERR_OR_NULL(tp_format))
		return NULL;

	PyObject *obj = PyObject_Str(attr_name);
	if (obj == NULL)
		return NULL;

	const char *str = PyUnicode_AsUTF8(obj);
	if (str == NULL) {
		Py_DECREF(obj);
		return NULL;
	}

	field = tep_find_any_field(tp_format, str);
	Py_DECREF(obj);
	return field ? tracepoint_field(pevent, field) : NULL;
}
#endif /* HAVE_LIBTRACEEVENT */

static int pyrf_sample_event__resolve_al(struct pyrf_event *pevent)
{
	if (pevent->al_resolved)
		return 0;

	/*
	 * Use pevent->machine, which pyrf_event__new() initializes while the
	 * session callback is active and pyrf_event__copy() clears after
	 * resolving pevent->al if a reference is retained beyond the callback.
	 */
	if (!pevent->machine)
		return -1;

	addr_location__init(&pevent->al);
	if (machine__resolve(pevent->machine, &pevent->al, pevent->sample) < 0) {
		addr_location__exit(&pevent->al);
		return -1;
	}

	pevent->al_resolved = true;
	return 0;
}

static PyObject *pyrf_sample_event__get_dso(struct pyrf_event *pevent,
					    void *closure __maybe_unused)
{
	if (pyrf_sample_event__resolve_al(pevent) < 0 || !pevent->al.map)
		Py_RETURN_NONE;

	return PyUnicode_FromString(dso__name(map__dso(pevent->al.map)));
}

static PyObject *pyrf_sample_event__get_dso_long_name(struct pyrf_event *pevent,
						      void *closure __maybe_unused)
{
	if (pyrf_sample_event__resolve_al(pevent) < 0 || !pevent->al.map)
		Py_RETURN_NONE;

	return PyUnicode_FromString(dso__long_name(map__dso(pevent->al.map)));
}

static PyObject *pyrf_sample_event__get_dso_bid(struct pyrf_event *pevent,
						void *closure __maybe_unused)
{
	char sbuild_id[SBUILD_ID_SIZE];

	if (pyrf_sample_event__resolve_al(pevent) < 0 || !pevent->al.map)
		Py_RETURN_NONE;

	build_id__snprintf(dso__bid(map__dso(pevent->al.map)), sbuild_id, sizeof(sbuild_id));
	return PyUnicode_FromString(sbuild_id);
}

static PyObject *pyrf_sample_event__get_map_start(struct pyrf_event *pevent,
						  void *closure __maybe_unused)
{
	if (pyrf_sample_event__resolve_al(pevent) < 0 || !pevent->al.map)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLong(map__start(pevent->al.map));
}

static PyObject *pyrf_sample_event__get_map_end(struct pyrf_event *pevent,
						void *closure __maybe_unused)
{
	if (pyrf_sample_event__resolve_al(pevent) < 0 || !pevent->al.map)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLong(map__end(pevent->al.map));
}

static PyObject *pyrf_sample_event__get_map_pgoff(struct pyrf_event *pevent,
						  void *closure __maybe_unused)
{
	if (pyrf_sample_event__resolve_al(pevent) < 0 || !pevent->al.map)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLongLong(map__pgoff(pevent->al.map));
}

static PyObject *pyrf_sample_event__get_symbol(struct pyrf_event *pevent,
					       void *closure __maybe_unused)
{
	if (pyrf_sample_event__resolve_al(pevent) < 0 || !pevent->al.sym)
		Py_RETURN_NONE;

	return PyUnicode_FromString(pevent->al.sym->name);
}

static PyObject *pyrf_sample_event__get_sym_start(struct pyrf_event *pevent,
						  void *closure __maybe_unused)
{
	if (pyrf_sample_event__resolve_al(pevent) < 0 || !pevent->al.sym)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLongLong(pevent->al.sym->start);
}

static PyObject *pyrf_sample_event__get_sym_offset(struct pyrf_event *pevent,
						   void *closure __maybe_unused)
{
	if (pyrf_sample_event__resolve_al(pevent) < 0 || !pevent->al.sym)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLongLong(pevent->al.addr - pevent->al.sym->start);
}

static PyObject *pyrf_sample_event__get_sym_end(struct pyrf_event *pevent,
						void *closure __maybe_unused)
{
	if (pyrf_sample_event__resolve_al(pevent) < 0 || !pevent->al.sym)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLongLong(pevent->al.sym->end);
}

static PyObject *pyrf_sample_event__get_raw_buf(struct pyrf_event *pevent,
						void *closure __maybe_unused)
{
	if (pevent->event->header.type != PERF_RECORD_SAMPLE)
		Py_RETURN_NONE;

	return PyBytes_FromStringAndSize((const char *)pevent->sample->raw_data,
					 pevent->sample->raw_size);
}

static PyObject *pyrf_sample_event__srccode(PyObject *self, PyObject *args)
{
	struct pyrf_event *pevent = (void *)self;
	u64 addr = pevent->sample->ip;
	char *srcfile = NULL;
	char *srccode = NULL;
	unsigned int line = 0;
	int len = 0;
	PyObject *result;
	struct addr_location al;

	/*
	 * addr defaults to pevent->sample->ip when omitted, and when
	 * addr != pevent->sample->ip a local addr_location is resolved so
	 * callers can inspect callchain/branch addresses without mutating
	 * the sample's cached pevent->al.
	 */
	if (!PyArg_ParseTuple(args, "|K", &addr))
		return NULL;

	if (pyrf_sample_event__resolve_al(pevent) < 0)
		Py_RETURN_NONE;

	if (addr != pevent->sample->ip) {
		addr_location__init(&al);
		thread__find_symbol_fb(pevent->al.thread, pevent->sample->cpumode, addr, &al);
	} else {
		addr_location__init(&al);
		al.thread = thread__get(pevent->al.thread);
		al.map = map__get(pevent->al.map);
		al.sym = pevent->al.sym;
		al.addr = pevent->al.addr;
	}

	if (al.map) {
		struct dso *dso = map__dso(al.map);

		if (dso) {
			srcfile = get_srcline_split(dso, map__rip_2objdump(al.map, addr),
						    &line);
		}
	}
	addr_location__exit(&al);

	if (srcfile) {
		/*
		 * PyUnicode_FromStringAndSize(line, len) (via Py_BuildValue "s#")
		 * must copy the source line before free_srccode() frees the
		 * buffer, and because the line returned by find_sourceline() is
		 * bounded by len rather than NUL-terminated.
		 */
		srccode = find_sourceline(srcfile, line, &len);
		result = Py_BuildValue("(sIs#)", srcfile, line, srccode, (Py_ssize_t)len);
		free(srcfile);
	} else {
		result = Py_BuildValue("(sIs#)", NULL, 0, NULL, (Py_ssize_t)0);
	}

	return result;
}

static PyObject *pyrf_sample_event__insn(PyObject *self, PyObject *args __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (pevent->sample->ip && !pevent->sample->insn_len) {
		if (pyrf_sample_event__resolve_al(pevent) < 0 ||
		    !pevent->al.thread || !pevent->machine)
			Py_RETURN_NONE;

		perf_sample__fetch_insn(pevent->sample, pevent->al.thread,
					pevent->machine);
	}

	if (!pevent->sample->insn_len)
		Py_RETURN_NONE;

	return PyBytes_FromStringAndSize((const char *)pevent->sample->insn,
					 pevent->sample->insn_len);
}

struct pyrf_callchain_node {
	PyObject_HEAD
	u64 ip;
	struct map *map;
	struct symbol *sym;
	char *sym_name;
};

static void pyrf_callchain_node__delete(struct pyrf_callchain_node *pnode)
{
	map__put(pnode->map);
	free(pnode->sym_name);
	Py_TYPE(pnode)->tp_free((PyObject *)pnode);
}

static PyObject *pyrf_callchain_node__get_ip(struct pyrf_callchain_node *pnode,
					     void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLongLong(pnode->ip);
}

static PyObject *pyrf_callchain_node__get_symbol(struct pyrf_callchain_node *pnode,
						 void *closure __maybe_unused)
{
	if (pnode->sym_name)
		return PyUnicode_FromString(pnode->sym_name);
	if (pnode->sym)
		return PyUnicode_FromString(pnode->sym->name);
	return PyUnicode_FromString("[unknown]");
}

static PyObject *pyrf_callchain_node__get_dso(struct pyrf_callchain_node *pnode,
					      void *closure __maybe_unused)
{
	const char *dsoname = "[unknown]";

	if (pnode->map) {
		struct dso *dso = map__dso(pnode->map);

		if (dso) {
			if (symbol_conf.show_kernel_path && dso__long_name(dso))
				dsoname = dso__long_name(dso);
			else
				dsoname = dso__name(dso);
		}
	}
	return PyUnicode_FromString(dsoname);
}

static PyObject *pyrf_callchain_node__get_sym_offset(struct pyrf_callchain_node *pnode,
						     void *closure __maybe_unused)
{
	u64 addr;

	if (!pnode->sym)
		Py_RETURN_NONE;

	addr = pnode->map ? map__map_ip(pnode->map, pnode->ip) : pnode->ip;
	return PyLong_FromUnsignedLongLong(addr - pnode->sym->start);
}

static PyGetSetDef pyrf_callchain_node__getset[] = {
	{ .name = "ip",         .get = (getter)pyrf_callchain_node__get_ip, },
	{ .name = "symbol",     .get = (getter)pyrf_callchain_node__get_symbol, },
	{ .name = "dso",        .get = (getter)pyrf_callchain_node__get_dso, },
	{ .name = "sym_offset", .get = (getter)pyrf_callchain_node__get_sym_offset, },
	{ .name = NULL, },
};

static PyTypeObject pyrf_callchain_node__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.callchain_node",
	.tp_basicsize	= sizeof(struct pyrf_callchain_node),
	.tp_dealloc	= (destructor)pyrf_callchain_node__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= "perf callchain node object.",
	.tp_getset	= pyrf_callchain_node__getset,
};

struct pyrf_callchain_frame {
	u64 ip;
	struct map *map;
	struct symbol *sym;
	char *sym_name;
};

struct pyrf_callchain {
	PyObject_HEAD
	struct pyrf_callchain_frame *frames;
	u64 nr_frames;
};

static void pyrf_callchain__delete(struct pyrf_callchain *pchain)
{
	if (pchain->frames) {
		for (u64 i = 0; i < pchain->nr_frames; i++) {
			map__put(pchain->frames[i].map);
			free(pchain->frames[i].sym_name);
		}
		free(pchain->frames);
	}
	Py_TYPE(pchain)->tp_free((PyObject *)pchain);
}

static Py_ssize_t pyrf_callchain__length(PyObject *obj)
{
	struct pyrf_callchain *pchain = (void *)obj;

	return pchain->nr_frames;
}

static PyObject *pyrf_callchain__item(PyObject *obj, Py_ssize_t i)
{
	struct pyrf_callchain *pchain = (void *)obj;
	struct pyrf_callchain_node *pnode;

	if (i < 0 || i >= (Py_ssize_t)pchain->nr_frames) {
		PyErr_SetString(PyExc_IndexError, "Index out of range");
		return NULL;
	}

	pnode = PyObject_New(struct pyrf_callchain_node, &pyrf_callchain_node__type);
	if (!pnode)
		return NULL;

	pnode->ip = pchain->frames[i].ip;
	pnode->map = map__get(pchain->frames[i].map);
	pnode->sym = pchain->frames[i].sym;
	pnode->sym_name = pchain->frames[i].sym_name ? strdup(pchain->frames[i].sym_name) : NULL;

	return (PyObject *)pnode;
}

static PySequenceMethods pyrf_callchain__sequence_methods = {
	.sq_length = pyrf_callchain__length,
	.sq_item   = pyrf_callchain__item,
};

static PyTypeObject pyrf_callchain__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.callchain",
	.tp_basicsize	= sizeof(struct pyrf_callchain),
	.tp_dealloc	= (destructor)pyrf_callchain__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= "perf callchain object.",
	.tp_as_sequence	= &pyrf_callchain__sequence_methods,
};

static int pyrf_sample_event__resolve_callchain(struct pyrf_event *pevent)
{
	struct callchain_cursor *cursor;
	struct pyrf_callchain *pchain;
	struct callchain_cursor_node *node;

	if (pevent->callchain || !pevent->sample->callchain || !pevent->machine)
		return 0;

	if (pyrf_sample_event__resolve_al(pevent) < 0)
		return 0;

	cursor = get_tls_callchain_cursor();
	if (thread__resolve_callchain(pevent->al.thread, cursor, pevent->sample,
				      NULL, NULL, PERF_MAX_STACK_DEPTH) != 0)
		return 0;

	callchain_cursor_commit(cursor);
	pchain = PyObject_New(struct pyrf_callchain, &pyrf_callchain__type);
	if (!pchain)
		return -ENOMEM;

	pchain->nr_frames = cursor->nr;
	pchain->frames = calloc(pchain->nr_frames, sizeof(*pchain->frames));
	if (!pchain->frames) {
		Py_DECREF(pchain);
		PyErr_NoMemory();
		return -ENOMEM;
	}
	for (u64 i = 0; i < pchain->nr_frames; i++) {
		node = callchain_cursor_current(cursor);
		pchain->frames[i].ip = node->ip;
		pchain->frames[i].map = map__get(node->ms.map);
		pchain->frames[i].sym = node->ms.sym;
		callchain_cursor_advance(cursor);
	}
	pevent->callchain = (PyObject *)pchain;
	return 0;
}

static PyObject *pyrf_sample_event__get_callchain(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (pyrf_sample_event__resolve_callchain(pevent) < 0)
		return NULL;

	if (!pevent->callchain)
		Py_RETURN_NONE;

	Py_INCREF(pevent->callchain);
	return pevent->callchain;
}

struct pyrf_branch_entry {
	PyObject_HEAD
	u64 from;
	u64 to;
	struct branch_flags flags;
};

static void pyrf_branch_entry__delete(struct pyrf_branch_entry *pentry)
{
	Py_TYPE(pentry)->tp_free((PyObject *)pentry);
}

static PyObject *pyrf_branch_entry__get_from(struct pyrf_branch_entry *pentry,
					     void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLongLong(pentry->from);
}

static PyObject *pyrf_branch_entry__get_to(struct pyrf_branch_entry *pentry,
					   void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLongLong(pentry->to);
}

static PyObject *pyrf_branch_entry__get_mispred(struct pyrf_branch_entry *pentry,
						void *closure __maybe_unused)
{
	return PyBool_FromLong(pentry->flags.mispred);
}

static PyObject *pyrf_branch_entry__get_predicted(struct pyrf_branch_entry *pentry,
						  void *closure __maybe_unused)
{
	return PyBool_FromLong(pentry->flags.predicted);
}

static PyObject *pyrf_branch_entry__get_in_tx(struct pyrf_branch_entry *pentry,
					      void *closure __maybe_unused)
{
	return PyBool_FromLong(pentry->flags.in_tx);
}

static PyObject *pyrf_branch_entry__get_abort(struct pyrf_branch_entry *pentry,
					      void *closure __maybe_unused)
{
	return PyBool_FromLong(pentry->flags.abort);
}

static PyObject *pyrf_branch_entry__get_cycles(struct pyrf_branch_entry *pentry,
					       void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLongLong(pentry->flags.cycles);
}

static PyObject *pyrf_branch_entry__get_type(struct pyrf_branch_entry *pentry,
					     void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLongLong((unsigned long long)pentry->flags.type);
}

static PyGetSetDef pyrf_branch_entry__getset[] = {
	{ .name = "from_ip",      .get = (getter)pyrf_branch_entry__get_from, },
	{ .name = "to_ip",        .get = (getter)pyrf_branch_entry__get_to, },
	{ .name = "mispred",   .get = (getter)pyrf_branch_entry__get_mispred, },
	{ .name = "predicted", .get = (getter)pyrf_branch_entry__get_predicted, },
	{ .name = "in_tx",     .get = (getter)pyrf_branch_entry__get_in_tx, },
	{ .name = "abort",     .get = (getter)pyrf_branch_entry__get_abort, },
	{ .name = "cycles",    .get = (getter)pyrf_branch_entry__get_cycles, },
	{ .name = "type",      .get = (getter)pyrf_branch_entry__get_type, },
	{ .name = NULL, },
};

static PyTypeObject pyrf_branch_entry__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.branch_entry",
	.tp_basicsize	= sizeof(struct pyrf_branch_entry),
	.tp_dealloc	= (destructor)pyrf_branch_entry__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= "perf branch entry object.",
	.tp_getset	= pyrf_branch_entry__getset,
};

struct pyrf_branch_stack {
	PyObject_HEAD
	struct branch_entry *entries;
	u64 nr;
};

static void pyrf_branch_stack__delete(struct pyrf_branch_stack *pstack)
{
	free(pstack->entries);
	Py_TYPE(pstack)->tp_free((PyObject *)pstack);
}

static Py_ssize_t pyrf_branch_stack__length(PyObject *obj)
{
	struct pyrf_branch_stack *pstack = (void *)obj;

	return pstack->nr;
}

static PyObject *pyrf_branch_stack__item(PyObject *obj, Py_ssize_t i)
{
	struct pyrf_branch_stack *pstack = (void *)obj;
	struct pyrf_branch_entry *pentry;

	if (i < 0 || i >= (Py_ssize_t)pstack->nr) {
		PyErr_SetString(PyExc_IndexError, "Index out of range");
		return NULL;
	}

	pentry = PyObject_New(struct pyrf_branch_entry, &pyrf_branch_entry__type);
	if (!pentry)
		return NULL;

	pentry->from = pstack->entries[i].from;
	pentry->to = pstack->entries[i].to;
	pentry->flags = pstack->entries[i].flags;

	return (PyObject *)pentry;
}

static PySequenceMethods pyrf_branch_stack__sequence_methods = {
	.sq_length = pyrf_branch_stack__length,
	.sq_item   = pyrf_branch_stack__item,
};

static PyTypeObject pyrf_branch_stack__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.branch_stack",
	.tp_basicsize	= sizeof(struct pyrf_branch_stack),
	.tp_dealloc	= (destructor)pyrf_branch_stack__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= "perf branch stack object.",
	.tp_as_sequence	= &pyrf_branch_stack__sequence_methods,
};

static PyObject *pyrf_sample_event__get_brstack(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (!pevent->brstack)
		Py_RETURN_NONE;

	Py_INCREF(pevent->brstack);
	return pevent->brstack;
}

static PyObject*
pyrf_sample_event__getattro(struct pyrf_event *pevent, PyObject *attr_name)
{
	PyObject *obj = NULL;

#ifdef HAVE_LIBTRACEEVENT
	if (is_tracepoint(pevent))
		obj = get_tracepoint_field(pevent, attr_name);
#endif

	return obj ?: PyObject_GenericGetAttr((PyObject *) pevent, attr_name);
}

static int pyrf_sample_event__resolve_addr_al(struct pyrf_event *pevent,
					      struct addr_location *addr_al)
{
	addr_location__init(addr_al);
	if (pyrf_sample_event__resolve_al(pevent) < 0 || !pevent->al.thread)
		return -1;

	thread__find_symbol_fb(pevent->al.thread, pevent->sample->cpumode,
			       pevent->sample->addr, addr_al);
	return 0;
}

static PyObject *pyrf_sample_event__get_addr_dso(struct pyrf_event *pevent,
						 void *closure __maybe_unused)
{
	struct addr_location addr_al;
	PyObject *ret = Py_None;

	if (pyrf_sample_event__resolve_addr_al(pevent, &addr_al) == 0 && addr_al.map)
		ret = PyUnicode_FromString(dso__name(map__dso(addr_al.map)));
	else
		Py_INCREF(Py_None);

	addr_location__exit(&addr_al);
	return ret;
}

static PyObject *pyrf_sample_event__get_addr_symbol(struct pyrf_event *pevent,
						    void *closure __maybe_unused)
{
	struct addr_location addr_al;
	PyObject *ret = Py_None;

	if (pyrf_sample_event__resolve_addr_al(pevent, &addr_al) == 0 && addr_al.sym)
		ret = PyUnicode_FromString(addr_al.sym->name);
	else
		Py_INCREF(Py_None);

	addr_location__exit(&addr_al);
	return ret;
}

static PyObject *pyrf_sample_event__get_addr_sym_offset(struct pyrf_event *pevent,
							void *closure __maybe_unused)
{
	struct addr_location addr_al;
	PyObject *ret = Py_None;

	if (pyrf_sample_event__resolve_addr_al(pevent, &addr_al) == 0 && addr_al.sym)
		ret = PyLong_FromUnsignedLongLong(addr_al.addr - addr_al.sym->start);
	else
		Py_INCREF(Py_None);

	addr_location__exit(&addr_al);
	return ret;
}

static PyObject *pyrf_sample_event__get_branch_type(struct pyrf_event *pevent,
						    void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLong(pevent->sample->flags & PERF_BRANCH_MASK);
}

static PyObject *pyrf_sample_event__get_in_tx(struct pyrf_event *pevent,
					      void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLong(!!(pevent->sample->flags & PERF_IP_FLAG_IN_TX));
}

static PyObject *pyrf_sample_event__get_transaction(struct pyrf_event *pevent,
						    void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLongLong(pevent->sample->transaction);
}

static PyGetSetDef pyrf_sample_event__getset[] = {
	sample_members
	sample_member_def(sample_ip, ip, T_ULONGLONG, "event ip"),
	sample_member_def(sample_addr, addr, T_ULONGLONG, "event addr"),
	sample_member_def(sample_phys_addr, phys_addr, T_ULONGLONG, "event physical addr"),
	sample_member_def(sample_weight, weight, T_ULONGLONG, "event weight"),
	sample_member_def(sample_data_src, data_src, T_ULONGLONG, "event data source"),
	sample_member_def(sample_insn_count, insn_cnt, T_ULONGLONG, "event instruction count"),
	sample_member_def(sample_cyc_count, cyc_cnt, T_ULONGLONG, "event cycle count"),
	sample_member_def(flags, flags, T_UINT, "event flags"),
	member_def(perf_event_header, type, T_UINT, "event type"),
	{
		.name = "addr_dso",
		.get = (getter)pyrf_sample_event__get_addr_dso,
		.doc = "event destination dso.",
	},
	{
		.name = "addr_symbol",
		.get = (getter)pyrf_sample_event__get_addr_symbol,
		.doc = "event destination symbol.",
	},
	{
		.name = "addr_sym_offset",
		.get = (getter)pyrf_sample_event__get_addr_sym_offset,
		.doc = "event destination symbol offset.",
	},
	{
		.name = "branch_type",
		.get = (getter)pyrf_sample_event__get_branch_type,
		.doc = "branch type.",
	},
	{
		.name = "in_tx",
		.get = (getter)pyrf_sample_event__get_in_tx,
		.doc = "in transaction flag.",
	},
	{
		.name = "transaction",
		.get = (getter)pyrf_sample_event__get_transaction,
		.doc = "transaction execution.",
	},

	{
		.name = "callchain",
		.get = pyrf_sample_event__get_callchain,
		.set = NULL,
		.doc = "event callchain.",
	},
	{
		.name = "brstack",
		.get = pyrf_sample_event__get_brstack,
		.set = NULL,
		.doc = "event branch stack.",
	},
	{
		.name = "raw_buf",
		.get = (getter)pyrf_sample_event__get_raw_buf,
		.set = NULL,
		.doc = "event raw buffer.",
	},
	{
		.name = "dso",
		.get = (getter)pyrf_sample_event__get_dso,
		.set = NULL,
		.doc = "event dso short name.",
	},
	{
		.name = "dso_long_name",
		.get = (getter)pyrf_sample_event__get_dso_long_name,
		.set = NULL,
		.doc = "event dso long name.",
	},
	{
		.name = "dso_bid",
		.get = (getter)pyrf_sample_event__get_dso_bid,
		.set = NULL,
		.doc = "event dso build id.",
	},
	{
		.name = "map_start",
		.get = (getter)pyrf_sample_event__get_map_start,
		.set = NULL,
		.doc = "event map start address.",
	},
	{
		.name = "map_end",
		.get = (getter)pyrf_sample_event__get_map_end,
		.set = NULL,
		.doc = "event map end address.",
	},
	{
		.name = "map_pgoff",
		.get = (getter)pyrf_sample_event__get_map_pgoff,
		.set = NULL,
		.doc = "event map page offset.",
	},
	{
		.name = "sym_offset",
		.get = (getter)pyrf_sample_event__get_sym_offset,
		.set = NULL,
		.doc = "event symbol offset.",
	},
	{
		.name = "symbol",
		.get = (getter)pyrf_sample_event__get_symbol,
		.set = NULL,
		.doc = "event symbol name.",
	},
	{
		.name = "sym_start",
		.get = (getter)pyrf_sample_event__get_sym_start,
		.set = NULL,
		.doc = "event symbol start address.",
	},
	{
		.name = "sym_end",
		.get = (getter)pyrf_sample_event__get_sym_end,
		.set = NULL,
		.doc = "event symbol end address.",
	},
	{ .name = NULL, },
};

static PyMethodDef pyrf_sample_event__methods[] = {
	{
		.ml_name  = "srccode",
		.ml_meth  = (PyCFunction)pyrf_sample_event__srccode,
		.ml_flags = METH_VARARGS,
		.ml_doc	  = PyDoc_STR("Get source code for an address.")
	},
	{
		.ml_name  = "insn",
		.ml_meth  = (PyCFunction)pyrf_sample_event__insn,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("Get instruction bytes for a sample.")
	},
	{ .ml_name = NULL, }
};

static PyTypeObject pyrf_sample_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.sample_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_sample_event__doc,
	.tp_getset	= pyrf_sample_event__getset,
	.tp_methods	= pyrf_sample_event__methods,
	.tp_repr	= (reprfunc)pyrf_sample_event__repr,
	.tp_getattro	= (getattrofunc) pyrf_sample_event__getattro,
};

static const char pyrf_context_switch_event__doc[] = PyDoc_STR("perf context_switch event object.");

static PyObject *pyrf_context_switch_event__get_next_prev_pid(const struct pyrf_event *pevent,
							      void *closure __maybe_unused)
{
	if (pevent->event->header.type == PERF_RECORD_SWITCH_CPU_WIDE)
		return PyLong_FromUnsignedLong(pevent->event->context_switch.next_prev_pid);
	Py_RETURN_NONE;
}

static PyObject *pyrf_context_switch_event__get_next_prev_tid(const struct pyrf_event *pevent,
							      void *closure __maybe_unused)
{
	if (pevent->event->header.type == PERF_RECORD_SWITCH_CPU_WIDE)
		return PyLong_FromUnsignedLong(pevent->event->context_switch.next_prev_tid);
	Py_RETURN_NONE;
}

static PyGetSetDef pyrf_context_switch_event__getset[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_event_header, misc, T_USHORT, "event misc"),
	{
		.name = "next_prev_pid",
		.get = (getter)pyrf_context_switch_event__get_next_prev_pid,
		.set = NULL,
		.doc = "next/prev pid for CPU-wide switch, or None.",
	},
	{
		.name = "next_prev_tid",
		.get = (getter)pyrf_context_switch_event__get_next_prev_tid,
		.set = NULL,
		.doc = "next/prev tid for CPU-wide switch, or None.",
	},
	{ .name = NULL, },
};

static PyObject *pyrf_context_switch_event__repr(const struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;
	int res;

	if (pevent->event->header.type == PERF_RECORD_SWITCH_CPU_WIDE) {
		res = asprintf(&s,
			"{ type: context_switch, next_prev_pid: %u, next_prev_tid: %u, switch_out: %u }",
			pevent->event->context_switch.next_prev_pid,
			pevent->event->context_switch.next_prev_tid,
			!!(pevent->event->header.misc & PERF_RECORD_MISC_SWITCH_OUT));
	} else {
		res = asprintf(&s, "{ type: context_switch, switch_out: %u }",
			       !!(pevent->event->header.misc & PERF_RECORD_MISC_SWITCH_OUT));
	}
	if (res < 0) {
		ret = PyErr_NoMemory();
	} else {
		ret = PyUnicode_FromString(s);
		free(s);
	}
	return ret;
}

static PyTypeObject pyrf_context_switch_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.context_switch_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_context_switch_event__doc,
	.tp_getset	= pyrf_context_switch_event__getset,
	.tp_repr	= (reprfunc)pyrf_context_switch_event__repr,
};

static int pyrf_event__setup_types(void)
{
	int err;

	err = PyType_Ready(&pyrf_mmap_event__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_mmap2_event__type);
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
	err = PyType_Ready(&pyrf_stat_event__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_stat_round_event__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_callchain_node__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_callchain__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_branch_entry__type);
	if (err < 0)
		goto out;
	err = PyType_Ready(&pyrf_branch_stack__type);
	if (err < 0)
		goto out;
out:
	return err;
}

static PyTypeObject *pyrf_event__type[] = {
	[PERF_RECORD_MMAP]	 = &pyrf_mmap_event__type,
	[PERF_RECORD_MMAP2]	 = &pyrf_mmap2_event__type,
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
	[PERF_RECORD_STAT]	 = &pyrf_stat_event__type,
	[PERF_RECORD_STAT_ROUND] = &pyrf_stat_round_event__type,
};

static int pyrf_event__copy(struct pyrf_event *pevent)
{
	const union perf_event *orig_event = pevent->event;
	const struct perf_sample *orig_sample = pevent->sample;
	size_t copy_size = orig_event->header.size;
	int err = -EINVAL;

	/*
	 * If a session callback retained a reference to pevent, resolve its
	 * address location and callchain now while pevent->machine is still
	 * live and thread->maps reflects the point in time of the sample.
	 */
	if (pevent->machine && pevent->evsel) {
		pyrf_sample_event__resolve_al(pevent);
		if (pyrf_sample_event__resolve_callchain(pevent) < 0) {
			pevent->machine = NULL;
			if (!pevent->event_copy) {
				pevent->event = &zero_event;
				pevent->sample = &pevent->sample_storage;
			}
			return -ENOMEM;
		}
	}
	pevent->machine = NULL;

	if (pevent->event_copy)
		return 0;

	/*
	 * Clear borrowed pointers immediately so that even if copying fails,
	 * pevent does not retain dangling pointers to the caller's stack,
	 * ring buffer, or session.
	 */
	pevent->event = &zero_event;
	pevent->sample = &pevent->sample_storage;

	/*
	 * Allocate an extra zero byte so string fields such as mmap/mmap2
	 * filenames or comm strings are always NUL-terminated.
	 */
	pevent->event_copy = calloc(1, copy_size + 1);
	if (!pevent->event_copy) {
		PyErr_NoMemory();
		return -ENOMEM;
	}

	memcpy(pevent->event_copy, orig_event, copy_size);
	pevent->event = pevent->event_copy;

	if (pevent->evsel) {
		bool needs_swap = pevent->evsel->needs_swap;

		pevent->evsel->needs_swap = false;
		err = evsel__parse_sample(pevent->evsel, pevent->event_copy,
					  &pevent->sample_storage);
		pevent->evsel->needs_swap = needs_swap;
	}

	if (err == 0) {
		if (orig_sample != &pevent->sample_storage) {
			if (orig_sample->id)
				pevent->sample_storage.id = orig_sample->id;
			if (orig_sample->period)
				pevent->sample_storage.period = orig_sample->period;
			if (orig_sample->machine_pid)
				pevent->sample_storage.machine_pid =
					orig_sample->machine_pid;
			if (orig_sample->vcpu != (u32)-1)
				pevent->sample_storage.vcpu = orig_sample->vcpu;
			if (orig_sample->insn_len && !pevent->sample_storage.insn_len) {
				memcpy(pevent->sample_storage.insn, orig_sample->insn,
				       sizeof(pevent->sample_storage.insn));
				pevent->sample_storage.insn_len = orig_sample->insn_len;
			}
		}
		pevent->sample = &pevent->sample_storage;
	} else if (orig_sample != &pevent->sample_storage) {
		/*
		 * Synthesized events (e.g. Intel PT itrace) or events without
		 * an evsel may not have raw sample buffers for
		 * evsel__parse_sample(); copy the scalar fields from the
		 * borrowed sample.
		 */
		perf_sample__exit(&pevent->sample_storage);
		pevent->sample_storage = *orig_sample;
		if (pevent->sample_storage.evsel)
			pevent->sample_storage.evsel =
				evsel__get(pevent->sample_storage.evsel);
		pevent->sample_storage.callchain = NULL;
		pevent->sample_storage.merged_callchain = false;
		pevent->sample_storage.raw_data = NULL;
		pevent->sample_storage.raw_size = 0;
		pevent->sample_storage.branch_stack = NULL;
		pevent->sample_storage.user_regs = NULL;
		pevent->sample_storage.intr_regs = NULL;
		pevent->sample_storage.user_stack.size = 0;
		pevent->sample_storage.user_stack.data = NULL;
		pevent->sample_storage.aux_sample.size = 0;
		pevent->sample_storage.aux_sample.data = NULL;
		pevent->sample = &pevent->sample_storage;
	} else if (pevent->evsel) {
		PyErr_Format(PyExc_OSError, "perf: can't parse sample, err=%d", err);
		return err;
	}
	return 0;
}

static PyObject *pyrf_event__new(const union perf_event *event, struct evsel *evsel,
				 struct perf_session *session, struct machine *machine,
				 struct perf_sample *sample_arg)
{
	struct pyrf_event *pevent;
	struct perf_sample *sample;
	u32 min_size;

	if (event->header.type >= ARRAY_SIZE(pyrf_event__type) ||
	    pyrf_event__type[event->header.type] == NULL) {
		return PyErr_Format(PyExc_TypeError, "Unexpected header type %u",
			     event->header.type);
	}

	if (perf_event__too_small(event, &min_size)) {
		return PyErr_Format(PyExc_ValueError, "Event size %u too small for type %u",
				    event->header.size, event->header.type);
	}

	if (event->header.size > sizeof(union perf_event)) {
		return PyErr_Format(PyExc_TypeError, "Unexpected event size: %zu < %u",
				    sizeof(union perf_event), event->header.size);
	}

	pevent = PyObject_New(struct pyrf_event, pyrf_event__type[event->header.type]);
	if (pevent == NULL)
		return PyErr_NoMemory();

	perf_sample__init(&pevent->sample_storage, /*all=*/true);
	pevent->sample = sample_arg ?: &pevent->sample_storage;
	pevent->evsel = evsel ? evsel__get(evsel) : NULL;
	pevent->machine = NULL;
	pevent->callchain = NULL;
	pevent->brstack = NULL;
	pevent->al_resolved = false;
	addr_location__init(&pevent->al);
	pevent->event = event;
	pevent->event_copy = NULL;

	/*
	 * When called outside a session callback (e.g. pyrf_evlist__read_on_cpu)
	 * or without a pre-parsed sample for an evsel, copy the event and parse
	 * its sample into sample_storage immediately.
	 */
	if ((!session || (evsel && !sample_arg)) && pyrf_event__copy(pevent) < 0) {
		Py_DECREF(pevent);
		return NULL;
	}

	if (!evsel)
		return (PyObject *)pevent;

	sample = pevent->sample;
	if (session && session->evlist && perf_guest && sample->id) {
		struct perf_sample_id *sid = evlist__id2sid(session->evlist, sample->id);

		if (sid) {
			sample->machine_pid = sid->machine_pid;
			sample->vcpu = sid->vcpu.cpu;
		}
	}
	if (!machine && session) {
		machine = sample->machine_pid ?
			machines__find(&session->machines, sample->machine_pid) :
			&session->machines.host;
		if (!machine)
			machine = &session->machines.host;
	}
	pevent->machine = machine;
	if (machine && machine->pid > 0 && !sample->machine_pid)
		sample->machine_pid = machine->pid;
	if (sample->branch_stack) {
		struct branch_stack *bs = sample->branch_stack;
		struct branch_entry *entries = perf_sample__branch_entries(sample);
		struct pyrf_branch_stack *pstack;

		pstack = PyObject_New(struct pyrf_branch_stack, &pyrf_branch_stack__type);
		if (!pstack) {
			Py_DECREF(pevent);
			return NULL;
		}
		pstack->nr = bs->nr;
		pstack->entries = calloc(bs->nr, sizeof(struct branch_entry));
		if (!pstack->entries) {
			Py_DECREF(pstack);
			Py_DECREF(pevent);
			return PyErr_NoMemory();
		}
		memcpy(pstack->entries, entries,
		       bs->nr * sizeof(struct branch_entry));
		pevent->brstack = (PyObject *)pstack;
	}
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

	return perf_cpu_map__nr(pcpus->cpus);
}

static PyObject *pyrf_cpu_map__item(PyObject *obj, Py_ssize_t i)
{
	struct pyrf_cpu_map *pcpus = (void *)obj;

	if (i >= perf_cpu_map__nr(pcpus->cpus)) {
		PyErr_SetString(PyExc_IndexError, "Index out of range");
		return NULL;
	}

	return Py_BuildValue("i", perf_cpu_map__cpu(pcpus->cpus, i).cpu);
}

static PySequenceMethods pyrf_cpu_map__sequence_methods = {
	.sq_length = pyrf_cpu_map__length,
	.sq_item   = pyrf_cpu_map__item,
};

static const char pyrf_cpu_map__doc[] = PyDoc_STR("cpu map object.");

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
	static char *kwlist[] = { "pid", "tid", NULL };
	int pid = -1, tid = -1;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ii",
					 kwlist, &pid, &tid))
		return -1;

	pthreads->threads = thread_map__new(pid, tid);
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

	return perf_thread_map__nr(pthreads->threads);
}

static PyObject *pyrf_thread_map__item(PyObject *obj, Py_ssize_t i)
{
	struct pyrf_thread_map *pthreads = (void *)obj;

	if (i >= perf_thread_map__nr(pthreads->threads)) {
		PyErr_SetString(PyExc_IndexError, "Index out of range");
		return NULL;
	}

	return Py_BuildValue("i", perf_thread_map__pid(pthreads->threads, i));
}

static PySequenceMethods pyrf_thread_map__sequence_methods = {
	.sq_length = pyrf_thread_map__length,
	.sq_item   = pyrf_thread_map__item,
};

static const char pyrf_thread_map__doc[] = PyDoc_STR("thread map object.");

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

/**
 * A python wrapper for perf_pmus that are globally owned by the pmus.c code.
 */
struct pyrf_pmu {
	PyObject_HEAD

	struct perf_pmu *pmu;
};

static void pyrf_pmu__delete(struct pyrf_pmu *ppmu)
{
	Py_TYPE(ppmu)->tp_free((PyObject *)ppmu);
}

static PyObject *pyrf_pmu__name(PyObject *self)
{
	struct pyrf_pmu *ppmu = (void *)self;

	CHECK_INITIALIZED(ppmu->pmu, "pmu");
	return PyUnicode_FromString(ppmu->pmu->name);
}

static bool add_to_dict(PyObject *dict, const char *key, const char *value)
{
	PyObject *pkey, *pvalue;
	bool ret;

	if (value == NULL)
		return true;

	pkey = PyUnicode_FromString(key);
	pvalue = PyUnicode_FromString(value);

	ret = pkey && pvalue && PyDict_SetItem(dict, pkey, pvalue) == 0;
	Py_XDECREF(pkey);
	Py_XDECREF(pvalue);
	return ret;
}

static int pyrf_pmu__events_cb(void *state, struct pmu_event_info *info)
{
	PyObject *py_list = state;
	PyObject *dict = PyDict_New();

	if (!dict)
		return -ENOMEM;

	if (!add_to_dict(dict, "name", info->name) ||
	    !add_to_dict(dict, "alias", info->alias) ||
	    !add_to_dict(dict, "scale_unit", info->scale_unit) ||
	    !add_to_dict(dict, "desc", info->desc) ||
	    !add_to_dict(dict, "long_desc", info->long_desc) ||
	    !add_to_dict(dict, "encoding_desc", info->encoding_desc) ||
	    !add_to_dict(dict, "topic", info->topic) ||
	    !add_to_dict(dict, "event_type_desc", info->event_type_desc) ||
	    !add_to_dict(dict, "str", info->str) ||
	    !add_to_dict(dict, "deprecated", info->deprecated ? "deprecated" : NULL) ||
	    PyList_Append(py_list, dict) != 0) {
		Py_DECREF(dict);
		return -ENOMEM;
	}
	Py_DECREF(dict);
	return 0;
}

static PyObject *pyrf_pmu__events(PyObject *self)
{
	struct pyrf_pmu *ppmu = (void *)self;
	PyObject *py_list;
	int ret;

	CHECK_INITIALIZED(ppmu->pmu, "pmu");

	py_list = PyList_New(0);
	if (!py_list)
		return NULL;

	ret = perf_pmu__for_each_event(ppmu->pmu,
				       /*skip_duplicate_pmus=*/false,
				       py_list,
				       pyrf_pmu__events_cb);
	if (ret) {
		Py_DECREF(py_list);
		errno = -ret;
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}
	return py_list;
}

static PyObject *pyrf_pmu__repr(PyObject *self)
{
	struct pyrf_pmu *ppmu = (void *)self;

	CHECK_INITIALIZED(ppmu->pmu, "pmu");
	return PyUnicode_FromFormat("pmu(%s)", ppmu->pmu->name);
}

static const char pyrf_pmu__doc[] = PyDoc_STR("perf Performance Monitoring Unit (PMU) object.");

static PyMethodDef pyrf_pmu__methods[] = {
	{
		.ml_name  = "events",
		.ml_meth  = (PyCFunction)pyrf_pmu__events,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("Returns a sequence of events encoded as a dictionaries.")
	},
	{
		.ml_name  = "name",
		.ml_meth  = (PyCFunction)pyrf_pmu__name,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("Name of the PMU including suffixes.")
	},
	{ .ml_name = NULL, }
};

/** The python type for a perf.pmu. */
static PyTypeObject pyrf_pmu__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.pmu",
	.tp_basicsize	= sizeof(struct pyrf_pmu),
	.tp_dealloc	= (destructor)pyrf_pmu__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_pmu__doc,
	.tp_methods	= pyrf_pmu__methods,
	.tp_str         = pyrf_pmu__name,
	.tp_repr        = pyrf_pmu__repr,
};

static int pyrf_pmu__setup_types(void)
{
	pyrf_pmu__type.tp_new = PyType_GenericNew;
	return PyType_Ready(&pyrf_pmu__type);
}

/** A python iterator for pmus that has no equivalent in the C code. */
struct pyrf_pmu_iterator {
	PyObject_HEAD
	struct perf_pmu *pmu;
};

static void pyrf_pmu_iterator__dealloc(struct pyrf_pmu_iterator *self)
{
	Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyObject *pyrf_pmu_iterator__new(PyTypeObject *type, PyObject *args __maybe_unused,
					PyObject *kwds __maybe_unused)
{
	struct pyrf_pmu_iterator *itr = (void *)type->tp_alloc(type, 0);

	if (itr != NULL)
		itr->pmu = perf_pmus__scan(/*pmu=*/NULL);

	return (PyObject *) itr;
}

static PyObject *pyrf_pmu_iterator__iter(PyObject *self)
{
	Py_INCREF(self);
	return self;
}

static PyObject *pyrf_pmu_iterator__iternext(PyObject *self)
{
	struct pyrf_pmu_iterator *itr = (void *)self;
	struct pyrf_pmu *ppmu;

	if (itr->pmu == NULL) {
		PyErr_SetNone(PyExc_StopIteration);
		return NULL;
	}
	// Create object to return.
	ppmu = PyObject_New(struct pyrf_pmu, &pyrf_pmu__type);
	if (ppmu) {
		ppmu->pmu = itr->pmu;
		// Advance iterator.
		itr->pmu = perf_pmus__scan(itr->pmu);
	}
	return (PyObject *)ppmu;
}

/** The python type for the PMU iterator. */
static PyTypeObject pyrf_pmu_iterator__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "pmus.iterator",
	.tp_doc = "Iterator for the pmus string sequence.",
	.tp_basicsize = sizeof(struct pyrf_pmu_iterator),
	.tp_itemsize = 0,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_new = pyrf_pmu_iterator__new,
	.tp_dealloc = (destructor) pyrf_pmu_iterator__dealloc,
	.tp_iter = pyrf_pmu_iterator__iter,
	.tp_iternext = pyrf_pmu_iterator__iternext,
};

static int pyrf_pmu_iterator__setup_types(void)
{
	return PyType_Ready(&pyrf_pmu_iterator__type);
}

static PyObject *pyrf__pmus(PyObject *self, PyObject *args)
{
	// Calling the class creates an instance of the iterator.
	return PyObject_CallObject((PyObject *) &pyrf_pmu_iterator__type, /*args=*/NULL);
}

struct pyrf_counts_values {
	PyObject_HEAD

	struct perf_counts_values values;
};

static const char pyrf_counts_values__doc[] = PyDoc_STR("perf counts values object.");

static void pyrf_counts_values__delete(struct pyrf_counts_values *pcounts_values)
{
	Py_TYPE(pcounts_values)->tp_free((PyObject *)pcounts_values);
}

#define counts_values_member_def(member, ptype, help) \
	{ #member, ptype, \
	  offsetof(struct pyrf_counts_values, values.member), \
	  0, help }

static PyMemberDef pyrf_counts_values_members[] = {
	counts_values_member_def(val, T_ULONGLONG, "Value of event"),
	counts_values_member_def(ena, T_ULONGLONG, "Time for which enabled"),
	counts_values_member_def(run, T_ULONGLONG, "Time for which running"),
	counts_values_member_def(id, T_ULONGLONG, "Unique ID for an event"),
	counts_values_member_def(lost, T_ULONGLONG, "Num of lost samples"),
	{ .name = NULL, },
};

static PyObject *pyrf_counts_values_get_values(struct pyrf_counts_values *self, void *closure)
{
	PyObject *vals = PyList_New(5);

	if (!vals)
		return NULL;
	for (int i = 0; i < 5; i++) {
		PyObject *val = PyLong_FromUnsignedLongLong(self->values.values[i]);

		if (!val) {
			Py_DECREF(vals);
			return NULL;
		}
		PyList_SetItem(vals, i, val);
	}

	return vals;
}

static int pyrf_counts_values_set_values(struct pyrf_counts_values *self, PyObject *list,
					 void *closure)
{
	Py_ssize_t size;
	PyObject *item = NULL;

	if (list == NULL) {
		PyErr_SetString(PyExc_TypeError, "cannot delete attribute");
		return -1;
	}

	if (!PyList_Check(list)) {
		PyErr_SetString(PyExc_TypeError, "Value assigned must be a list");
		return -1;
	}

	size = PyList_Size(list);
	if (size != 5) {
		PyErr_SetString(PyExc_ValueError, "List must have exactly 5 entries");
		return -1;
	}

	for (Py_ssize_t i = 0; i < size; i++) {
		unsigned long long val;

		item = PyList_GetItem(list, i);
		if (!PyLong_Check(item)) {
			PyErr_SetString(PyExc_TypeError, "List members should be numbers");
			return -1;
		}
		val = PyLong_AsUnsignedLongLong(item);
		if (val == (unsigned long long)-1 && PyErr_Occurred())
			return -1;
		self->values.values[i] = val;
	}

	return 0;
}

static PyGetSetDef pyrf_counts_values_getset[] = {
	{"values", (getter)pyrf_counts_values_get_values, (setter)pyrf_counts_values_set_values,
		"Name field", NULL},
	{ .name = NULL, },
};

static PyTypeObject pyrf_counts_values__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.counts_values",
	.tp_basicsize	= sizeof(struct pyrf_counts_values),
	.tp_dealloc	= (destructor)pyrf_counts_values__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_counts_values__doc,
	.tp_members	= pyrf_counts_values_members,
	.tp_getset	= pyrf_counts_values_getset,
};

static int pyrf_counts_values__setup_types(void)
{
	pyrf_counts_values__type.tp_new = PyType_GenericNew;
	return PyType_Ready(&pyrf_counts_values__type);
}

struct pyrf_evsel {
	PyObject_HEAD

	struct evsel *evsel;
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
		"idx",
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
	    sample_id_all = 1,
	    idx = 0;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs,
					 "|iKiKKiiiiiiiiiiiiiiiiiiiiiiKKi", kwlist,
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

	evsel__put(pevsel->evsel);
	pevsel->evsel = evsel__new(&attr);
	if (!pevsel->evsel) {
		PyErr_NoMemory();
		return -1;
	}
	return 0;
}

static void pyrf_evsel__delete(struct pyrf_evsel *pevsel)
{
	evsel__put(pevsel->evsel);
	Py_TYPE(pevsel)->tp_free((PyObject*)pevsel);
}

static PyObject *pyrf_evsel__open(struct pyrf_evsel *pevsel,
				  PyObject *args, PyObject *kwargs)
{
	struct evsel *evsel = pevsel->evsel;
	struct perf_cpu_map *cpus = NULL;
	struct perf_thread_map *threads = NULL;
	PyObject *pcpus = NULL, *pthreads = NULL;
	int group = 0, inherit = 0;
	static char *kwlist[] = { "cpus", "threads", "group", "inherit", NULL };

	CHECK_INITIALIZED(evsel, "evsel");

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OOii", kwlist,
					 &pcpus, &pthreads, &group, &inherit))
		return NULL;

	if (pthreads != NULL && pthreads != Py_None) {
		if (!PyObject_TypeCheck(pthreads, &pyrf_thread_map__type)) {
			PyErr_SetString(PyExc_TypeError, "threads must be a thread_map");
			return NULL;
		}
		threads = ((struct pyrf_thread_map *)pthreads)->threads;
	}

	if (pcpus != NULL && pcpus != Py_None) {
		if (!PyObject_TypeCheck(pcpus, &pyrf_cpu_map__type)) {
			PyErr_SetString(PyExc_TypeError, "cpus must be a cpu_map");
			return NULL;
		}
		cpus = ((struct pyrf_cpu_map *)pcpus)->cpus;
	}

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

static PyObject *pyrf_evsel__cpus(struct pyrf_evsel *pevsel)
{
	struct pyrf_cpu_map *pcpu_map;

	CHECK_INITIALIZED(pevsel->evsel, "evsel");

	pcpu_map = PyObject_New(struct pyrf_cpu_map, &pyrf_cpu_map__type);
	if (pcpu_map)
		pcpu_map->cpus = perf_cpu_map__get(pevsel->evsel->core.cpus);

	return (PyObject *)pcpu_map;
}

static PyObject *pyrf_evsel__threads(struct pyrf_evsel *pevsel)
{
	struct pyrf_thread_map *pthread_map;

	CHECK_INITIALIZED(pevsel->evsel, "evsel");

	pthread_map = PyObject_New(struct pyrf_thread_map, &pyrf_thread_map__type);
	if (pthread_map)
		pthread_map->threads = perf_thread_map__get(pevsel->evsel->core.threads);

	return (PyObject *)pthread_map;
}

/*
 * Ensure evsel's counts and prev_raw_counts are allocated, the latter
 * used by tool PMUs to compute the cumulative count as expected by
 * stat's process_counter_values.
 */
static int evsel__ensure_counts(struct evsel *evsel)
{
	int nthreads, ncpus;

	if (evsel->counts != NULL)
		return 0;

	nthreads = perf_thread_map__nr(evsel->core.threads);
	ncpus = perf_cpu_map__nr(evsel->core.cpus);

	evsel->counts = perf_counts__new(ncpus, nthreads);
	if (evsel->counts == NULL)
		return -ENOMEM;

	evsel->prev_raw_counts = perf_counts__new(ncpus, nthreads);
	if (evsel->prev_raw_counts == NULL)
		return -ENOMEM;

	return 0;
}

static PyObject *pyrf_evsel__read(struct pyrf_evsel *pevsel,
				  PyObject *args, PyObject *kwargs)
{
	struct evsel *evsel = pevsel->evsel;
	int cpu = 0, cpu_idx, thread = 0, thread_idx;
	static char *kwlist[] = { "cpu", "thread", NULL };
	struct perf_counts_values *old_count, *new_count;
	struct pyrf_counts_values *count_values;

	CHECK_INITIALIZED(evsel, "evsel");

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ii", kwlist, &cpu, &thread))
		return NULL;

	cpu_idx = perf_cpu_map__idx(evsel->core.cpus, (struct perf_cpu){.cpu = cpu});
	if (cpu_idx < 0) {
		PyErr_Format(PyExc_TypeError, "CPU %d is not part of evsel's CPUs", cpu);
		return NULL;
	}
	thread_idx = perf_thread_map__idx(evsel->core.threads, thread);
	if (thread_idx < 0) {
		PyErr_Format(PyExc_TypeError, "Thread %d is not part of evsel's threads",
			     thread);
		return NULL;
	}

	if (evsel__ensure_counts(evsel))
		return PyErr_NoMemory();

	count_values = PyObject_New(struct pyrf_counts_values, &pyrf_counts_values__type);
	if (!count_values)
		return NULL;

	/* Set up pointers to the old and newly read counter values. */
	old_count = perf_counts(evsel->prev_raw_counts, cpu_idx, thread_idx);
	new_count = perf_counts(evsel->counts, cpu_idx, thread_idx);
	/* Update the value in evsel->counts. */
	evsel__read_counter(evsel, cpu_idx, thread_idx);
	/* Copy the value and turn it into the delta from old_count. */
	count_values->values = *new_count;
	count_values->values.val -= old_count->val;
	count_values->values.ena -= old_count->ena;
	count_values->values.run -= old_count->run;
	/* Save the new count over the old_count for the next read. */
	*old_count = *new_count;
	return (PyObject *)count_values;
}

static PyObject *pyrf_evsel__str(PyObject *self)
{
	struct pyrf_evsel *pevsel = (void *)self;
	struct evsel *evsel = pevsel->evsel;

	if (!evsel)
		return PyUnicode_FromString("evsel(uninitialized)");

	return PyUnicode_FromFormat("evsel(%s)", evsel__name(evsel));
}

static PyMethodDef pyrf_evsel__methods[] = {
	{
		.ml_name  = "open",
		.ml_meth  = (PyCFunction)pyrf_evsel__open,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("open the event selector file descriptor table.")
	},
	{
		.ml_name  = "cpus",
		.ml_meth  = (PyCFunction)pyrf_evsel__cpus,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("CPUs the event is to be used with.")
	},
	{
		.ml_name  = "threads",
		.ml_meth  = (PyCFunction)pyrf_evsel__threads,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("threads the event is to be used with.")
	},
	{
		.ml_name  = "read",
		.ml_meth  = (PyCFunction)pyrf_evsel__read,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("read counters")
	},
	{ .ml_name = NULL, }
};

static PyObject *pyrf_evsel__get_tracking(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;

	CHECK_INITIALIZED(pevsel->evsel, "evsel");

	if (pevsel->evsel->tracking)
		Py_RETURN_TRUE;
	else
		Py_RETURN_FALSE;
}

static int pyrf_evsel__set_tracking(PyObject *self, PyObject *val, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;
	int is_true;

	CHECK_INITIALIZED_INT(pevsel->evsel, "evsel");

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError, "cannot delete attribute");
		return -1;
	}

	is_true = PyObject_IsTrue(val);
	if (is_true < 0)
		return -1;

	pevsel->evsel->tracking = is_true;
	return 0;
}

static int pyrf_evsel__set_attr_config(PyObject *self, PyObject *val, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;
	unsigned long long new_val;

	CHECK_INITIALIZED_INT(pevsel->evsel, "evsel");

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError, "cannot delete attribute");
		return -1;
	}

	new_val = PyLong_AsUnsignedLongLong(val);
	if (PyErr_Occurred())
		return -1;

	pevsel->evsel->core.attr.config = new_val;
	return 0;
}

static PyObject *pyrf_evsel__get_attr_config(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;

	CHECK_INITIALIZED(pevsel->evsel, "evsel");

	return PyLong_FromUnsignedLongLong(pevsel->evsel->core.attr.config);
}

static int pyrf_evsel__set_attr_read_format(PyObject *self, PyObject *val, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;
	unsigned long long new_val;

	CHECK_INITIALIZED_INT(pevsel->evsel, "evsel");

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError, "cannot delete attribute");
		return -1;
	}

	new_val = PyLong_AsUnsignedLongLong(val);
	if (PyErr_Occurred())
		return -1;

	pevsel->evsel->core.attr.read_format = new_val;
	return 0;
}

static PyObject *pyrf_evsel__get_attr_read_format(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;

	CHECK_INITIALIZED(pevsel->evsel, "evsel");

	return PyLong_FromUnsignedLongLong(pevsel->evsel->core.attr.read_format);
}

static int pyrf_evsel__set_attr_sample_period(PyObject *self, PyObject *val, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;
	unsigned long long new_val;

	CHECK_INITIALIZED_INT(pevsel->evsel, "evsel");

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError, "cannot delete attribute");
		return -1;
	}

	new_val = PyLong_AsUnsignedLongLong(val);
	if (PyErr_Occurred())
		return -1;

	pevsel->evsel->core.attr.sample_period = new_val;
	return 0;
}

static PyObject *pyrf_evsel__get_attr_sample_period(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;

	CHECK_INITIALIZED(pevsel->evsel, "evsel");

	return PyLong_FromUnsignedLongLong(pevsel->evsel->core.attr.sample_period);
}

static int pyrf_evsel__set_attr_sample_type(PyObject *self, PyObject *val, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;
	unsigned long long new_val;

	CHECK_INITIALIZED_INT(pevsel->evsel, "evsel");

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError, "cannot delete attribute");
		return -1;
	}

	new_val = PyLong_AsUnsignedLongLong(val);
	if (PyErr_Occurred())
		return -1;

	pevsel->evsel->core.attr.sample_type = new_val;
	return 0;
}

static PyObject *pyrf_evsel__get_attr_sample_type(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;

	CHECK_INITIALIZED(pevsel->evsel, "evsel");

	return PyLong_FromUnsignedLongLong(pevsel->evsel->core.attr.sample_type);
}

static PyObject *pyrf_evsel__get_attr_size(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;

	CHECK_INITIALIZED(pevsel->evsel, "evsel");

	return PyLong_FromUnsignedLong(pevsel->evsel->core.attr.size);
}

static int pyrf_evsel__set_attr_type(PyObject *self, PyObject *val, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;
	unsigned long new_val;

	CHECK_INITIALIZED_INT(pevsel->evsel, "evsel");

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError, "cannot delete attribute");
		return -1;
	}

	new_val = PyLong_AsUnsignedLong(val);
	if (PyErr_Occurred())
		return -1;

	pevsel->evsel->core.attr.type = new_val;
	return 0;
}

static PyObject *pyrf_evsel__get_attr_type(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;

	CHECK_INITIALIZED(pevsel->evsel, "evsel");

	return PyLong_FromUnsignedLong(pevsel->evsel->core.attr.type);
}

static int pyrf_evsel__set_attr_wakeup_events(PyObject *self, PyObject *val, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;
	unsigned long new_val;

	CHECK_INITIALIZED_INT(pevsel->evsel, "evsel");

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError, "cannot delete attribute");
		return -1;
	}

	new_val = PyLong_AsUnsignedLong(val);
	if (PyErr_Occurred())
		return -1;

	pevsel->evsel->core.attr.wakeup_events = new_val;
	return 0;
}

static PyObject *pyrf_evsel__get_attr_wakeup_events(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_evsel *pevsel = (void *)self;

	CHECK_INITIALIZED(pevsel->evsel, "evsel");

	return PyLong_FromUnsignedLong(pevsel->evsel->core.attr.wakeup_events);
}

static PyObject *pyrf_evsel__get_ids(struct pyrf_evsel *pevsel, void *closure __maybe_unused)
{
	struct evsel *evsel;
	PyObject *list;

	CHECK_INITIALIZED(pevsel->evsel, "evsel");

	evsel = pevsel->evsel;
	list = PyList_New(0);

	if (!list)
		return NULL;

	for (u32 i = 0; i < evsel->core.ids; i++) {
		PyObject *id = PyLong_FromUnsignedLongLong(evsel->core.id[i]);
		int ret;

		if (!id) {
			Py_DECREF(list);
			return NULL;
		}
		ret = PyList_Append(list, id);
		Py_DECREF(id);
		if (ret < 0) {
			Py_DECREF(list);
			return NULL;
		}
	}

	return list;
}

static PyGetSetDef pyrf_evsel__getset[] = {
	{
		.name = "ids",
		.get = (getter)pyrf_evsel__get_ids,
		.set = NULL,
		.doc = "event IDs.",
	},
	{
		.name = "tracking",
		.get = pyrf_evsel__get_tracking,
		.set = pyrf_evsel__set_tracking,
		.doc = "tracking event.",
	},
	{
		.name = "config",
		.get = pyrf_evsel__get_attr_config,
		.set = pyrf_evsel__set_attr_config,
		.doc = "attribute config.",
	},
	{
		.name = "read_format",
		.get = pyrf_evsel__get_attr_read_format,
		.set = pyrf_evsel__set_attr_read_format,
		.doc = "attribute read_format.",
	},
	{
		.name = "sample_period",
		.get = pyrf_evsel__get_attr_sample_period,
		.set = pyrf_evsel__set_attr_sample_period,
		.doc = "attribute sample_period.",
	},
	{
		.name = "sample_type",
		.get = pyrf_evsel__get_attr_sample_type,
		.set = pyrf_evsel__set_attr_sample_type,
		.doc = "attribute sample_type.",
	},
	{
		.name = "size",
		.get = pyrf_evsel__get_attr_size,
		.doc = "attribute size.",
	},
	{
		.name = "type",
		.get = pyrf_evsel__get_attr_type,
		.set = pyrf_evsel__set_attr_type,
		.doc = "attribute type.",
	},
	{
		.name = "wakeup_events",
		.get = pyrf_evsel__get_attr_wakeup_events,
		.set = pyrf_evsel__set_attr_wakeup_events,
		.doc = "attribute wakeup_events.",
	},
	{ .name = NULL},
};

static const char pyrf_evsel__doc[] = PyDoc_STR("perf event selector list object.");

static PyObject *pyrf_evsel__getattro(struct pyrf_evsel *pevsel, PyObject *attr_name)
{
	if (!pevsel->evsel) {
		PyErr_SetString(PyExc_ValueError, "evsel not initialized");
		return NULL;
	}
	return PyObject_GenericGetAttr((PyObject *) pevsel, attr_name);
}

static int pyrf_evsel__setattro(struct pyrf_evsel *pevsel, PyObject *attr_name, PyObject *value)
{
	if (!pevsel->evsel) {
		PyErr_SetString(PyExc_ValueError, "evsel not initialized");
		return -1;
	}
	return PyObject_GenericSetAttr((PyObject *) pevsel, attr_name, value);
}

static PyTypeObject pyrf_evsel__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.evsel",
	.tp_basicsize	= sizeof(struct pyrf_evsel),
	.tp_dealloc	= (destructor)pyrf_evsel__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_evsel__doc,
	.tp_getset	= pyrf_evsel__getset,
	.tp_methods	= pyrf_evsel__methods,
	.tp_init	= (initproc)pyrf_evsel__init,
	.tp_str         = pyrf_evsel__str,
	.tp_repr        = pyrf_evsel__str,
	.tp_getattro	= (getattrofunc) pyrf_evsel__getattro,
	.tp_setattro	= (setattrofunc) pyrf_evsel__setattro,
};

static PyObject *pyrf_evsel__new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
	struct pyrf_evsel *pevsel;

	pevsel = (struct pyrf_evsel *)PyType_GenericNew(type, args, kwargs);
	if (pevsel)
		pevsel->evsel = NULL;
	return (PyObject *)pevsel;
}

static int pyrf_evsel__setup_types(void)
{
	pyrf_evsel__type.tp_new = pyrf_evsel__new;
	return PyType_Ready(&pyrf_evsel__type);
}

struct pyrf_evlist {
	PyObject_HEAD

	struct evlist *evlist;
};

static int pyrf_evlist__init(struct pyrf_evlist *pevlist,
			     PyObject *args, PyObject *kwargs __maybe_unused)
{
	PyObject *pcpus = NULL, *pthreads = NULL;
	struct perf_cpu_map *cpus;
	struct perf_thread_map *threads;

	if (!PyArg_ParseTuple(args, "O!O!",
			      &pyrf_cpu_map__type, &pcpus,
			      &pyrf_thread_map__type, &pthreads))
		return -1;

	evlist__put(pevlist->evlist);
	pevlist->evlist = evlist__new();
	if (!pevlist->evlist) {
		PyErr_NoMemory();
		return -1;
	}
	threads = ((struct pyrf_thread_map *)pthreads)->threads;
	cpus = ((struct pyrf_cpu_map *)pcpus)->cpus;
	perf_evlist__set_maps(evlist__core(pevlist->evlist), cpus, threads);

	return 0;
}

static void pyrf_evlist__delete(struct pyrf_evlist *pevlist)
{
	evlist__put(pevlist->evlist);
	Py_TYPE(pevlist)->tp_free((PyObject*)pevlist);
}

static PyObject *pyrf_evlist__all_cpus(struct pyrf_evlist *pevlist)
{
	struct pyrf_cpu_map *pcpu_map;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	pcpu_map = PyObject_New(struct pyrf_cpu_map, &pyrf_cpu_map__type);
	if (pcpu_map)
		pcpu_map->cpus = perf_cpu_map__get(evlist__core(pevlist->evlist)->all_cpus);

	return (PyObject *)pcpu_map;
}

static PyObject *pyrf_evlist__metrics(struct pyrf_evlist *pevlist)
{
	PyObject *list;
	struct rb_node *node;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	list = PyList_New(/*len=*/0);
	if (!list)
		return NULL;

	for (node = rb_first_cached(&evlist__metric_events(pevlist->evlist)->entries); node;
	     node = rb_next(node)) {
		struct metric_event *me = container_of(node, struct metric_event, nd);
		struct list_head *pos;

		list_for_each(pos, &me->head) {
			struct metric_expr *expr = container_of(pos, struct metric_expr, nd);
			PyObject *str = PyUnicode_FromString(expr->metric_name);

			if (!str || PyList_Append(list, str) != 0) {
				Py_DECREF(list);
				return NULL;
			}
			Py_DECREF(str);
		}
	}
	return list;
}

static int prepare_metric(const struct metric_expr *mexp,
			  const struct evsel *evsel,
			  struct expr_parse_ctx *pctx,
			  int cpu_idx, int thread_idx)
{
	struct evsel * const *metric_events = mexp->metric_events;
	struct metric_ref *metric_refs = mexp->metric_refs;

	for (int i = 0; metric_events[i]; i++) {
		struct evsel *cur = metric_events[i];
		double val, ena, run;
		int ret, source_count = 0;
		struct perf_counts_values *old_count, *new_count;

		/*
		 * If there are multiple uncore PMUs and we're not reading the
		 * leader's stats, determine the stats for the appropriate
		 * uncore PMU.
		 */
		if (evsel && evsel->metric_leader &&
		    evsel->pmu != evsel->metric_leader->pmu &&
		    cur->pmu == evsel->metric_leader->pmu) {
			struct evsel *pos;

			evlist__for_each_entry(evsel->evlist, pos) {
				if (pos->pmu != evsel->pmu)
					continue;
				if (pos->metric_leader != cur)
					continue;
				cur = pos;
				source_count = 1;
				break;
			}
		}

		if (source_count == 0)
			source_count = evsel__source_count(cur);

		ret = evsel__ensure_counts(cur);
		if (ret)
			return ret;

		/* Set up pointers to the old and newly read counter values. */
		old_count = perf_counts(cur->prev_raw_counts, cpu_idx, thread_idx);
		new_count = perf_counts(cur->counts, cpu_idx, thread_idx);
		/* Update the value in cur->counts. */
		evsel__read_counter(cur, cpu_idx, thread_idx);

		val = new_count->val - old_count->val;
		ena = new_count->ena - old_count->ena;
		run = new_count->run - old_count->run;

		if (ena != run && run != 0)
			val = val * ena / run;
		char *n = strdup(evsel__metric_id(cur));

		if (!n)
			return -ENOMEM;
		ret = expr__add_id_val_source_count(pctx, n, val, source_count);
		if (ret) {
			free(n);
			return ret;
		}
	}

	for (int i = 0; metric_refs && metric_refs[i].metric_name; i++) {
		int ret = expr__add_ref(pctx, &metric_refs[i]);

		if (ret)
			return ret;
	}

	return 0;
}

static PyObject *pyrf_evlist__compute_metric(struct pyrf_evlist *pevlist,
					     PyObject *args, PyObject *kwargs)
{
	int ret, cpu = 0, cpu_idx = 0, thread = 0, thread_idx = 0;
	const char *metric;
	static char *kwlist[] = { "metric", "cpu", "thread", NULL };
	struct rb_node *node;
	struct metric_expr *mexp = NULL;
	struct expr_parse_ctx *pctx;
	double result = 0;
	struct evsel *metric_evsel = NULL;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sii", kwlist, &metric, &cpu, &thread))
		return NULL;

	for (node = rb_first_cached(&evlist__metric_events(pevlist->evlist)->entries);
	     mexp == NULL && node;
	     node = rb_next(node)) {
		struct metric_event *me = container_of(node, struct metric_event, nd);
		struct list_head *pos;

		list_for_each(pos, &me->head) {
			struct metric_expr *e = container_of(pos, struct metric_expr, nd);
			struct evsel *pos2;

			if (strcmp(e->metric_name, metric))
				continue;

			if (e->metric_events[0] == NULL)
				continue;

			evlist__for_each_entry(pevlist->evlist, pos2) {
				if (pos2->metric_leader != e->metric_events[0])
					continue;
				cpu_idx = perf_cpu_map__idx(pos2->core.cpus,
							    (struct perf_cpu){.cpu = cpu});
				if (cpu_idx < 0)
					continue;

				thread_idx = perf_thread_map__idx(pos2->core.threads, thread);
				if (thread_idx < 0)
					continue;
				metric_evsel = pos2;
				mexp = e;
				goto done;
			}
		}
	}
done:
	if (!mexp) {
		PyErr_Format(PyExc_TypeError, "Unknown metric '%s' for CPU '%d' and thread '%d'",
			     metric, cpu, thread);
		return NULL;
	}

	pctx = expr__ctx_new();
	if (!pctx)
		return PyErr_NoMemory();

	ret = prepare_metric(mexp, metric_evsel, pctx, cpu_idx, thread_idx);
	if (ret) {
		expr__ctx_free(pctx);
		errno = -ret;
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}
	if (expr__parse(&result, pctx, mexp->metric_expr))
		result = 0.0;

	expr__ctx_free(pctx);
	return PyFloat_FromDouble(result);
}

static PyObject *pyrf_evlist__mmap(struct pyrf_evlist *pevlist,
				   PyObject *args, PyObject *kwargs)
{
	struct evlist *evlist;
	static char *kwlist[] = { "pages", "overwrite", NULL };
	int pages = 128, overwrite = false;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	evlist = pevlist->evlist;
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ii", kwlist,
					 &pages, &overwrite))
		return NULL;

	if (evlist__do_mmap(evlist, pages) < 0) {
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *pyrf_evlist__poll(struct pyrf_evlist *pevlist,
				   PyObject *args, PyObject *kwargs)
{
	struct evlist *evlist;
	static char *kwlist[] = { "timeout", NULL };
	int timeout = -1, n;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	evlist = pevlist->evlist;
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
	struct evlist *evlist;
	PyObject *list;
	int i;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	evlist = pevlist->evlist;
	list = PyList_New(0);
	if (!list)
		return NULL;

	for (i = 0; i < evlist__core(evlist)->pollfd.nr; ++i) {
		PyObject *file;
		file = PyFile_FromFd(evlist__core(evlist)->pollfd.entries[i].fd, "perf", "r", -1,
				     NULL, NULL, NULL, 0);
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
	Py_XDECREF(list);
	return PyErr_NoMemory();
}

static PyObject *pyrf_evlist__add(struct pyrf_evlist *pevlist, PyObject *args)
{
	struct evlist *evlist;
	PyObject *pevsel;
	struct evsel *evsel;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	evlist = pevlist->evlist;
	if (!PyArg_ParseTuple(args, "O!", &pyrf_evsel__type, &pevsel))
		return NULL;

	CHECK_INITIALIZED(((struct pyrf_evsel *)pevsel)->evsel, "evsel");

	evsel = ((struct pyrf_evsel *)pevsel)->evsel;
	CHECK_INITIALIZED(evsel, "evsel");

	evsel->core.idx = evlist__nr_entries(evlist);
	evlist__add(evlist, evsel__get(evsel));

	return Py_BuildValue("i", evlist__nr_entries(evlist));
}

static struct mmap *get_md(struct evlist *evlist, int cpu)
{
	int i;

	for (i = 0; i < evlist__core(evlist)->nr_mmaps; i++) {
		struct mmap *md = &evlist__mmap(evlist)[i];

		if (md->core.cpu.cpu == cpu)
			return md;
	}

	return NULL;
}

static PyObject *pyrf_evlist__read_on_cpu(struct pyrf_evlist *pevlist,
					  PyObject *args, PyObject *kwargs)
{
	struct evlist *evlist;
	union perf_event *event;
	struct evsel *evsel;
	int sample_id_all = 1, cpu;
	static char *kwlist[] = { "cpu", "sample_id_all", NULL };
	struct mmap *md;
	PyObject *pyevent;
	int err;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	evlist = pevlist->evlist;
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|i", kwlist,
					 &cpu, &sample_id_all))
		return NULL;

	md = get_md(evlist, cpu);
	if (!md)
		return PyErr_Format(PyExc_TypeError, "Unknown CPU '%d'", cpu);

	err = perf_mmap__read_init(&md->core);
	if (err < 0) {
		if (err == -EAGAIN)
			Py_RETURN_NONE;
		return PyErr_Format(PyExc_OSError,
				    "perf: error mmap read init, err=%d", err);
	}

	event = perf_mmap__read_event(&md->core);
	if (event == NULL)
		Py_RETURN_NONE;

	evsel = evlist__event2evsel(evlist, event);
	if (!evsel) {
		/* Unknown evsel. */
		perf_mmap__consume(&md->core);
		Py_RETURN_NONE;
	}
	pyevent = pyrf_event__new(event, evsel, evlist__session(evlist), /*machine=*/NULL, NULL);
	perf_mmap__consume(&md->core);
	if (pyevent == NULL)
		return PyErr_Occurred() ? NULL : PyErr_NoMemory();

	return pyevent;
}

static PyObject *pyrf_evlist__open(struct pyrf_evlist *pevlist,
				   PyObject *args, PyObject *kwargs)
{
	struct evlist *evlist;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	evlist = pevlist->evlist;
	if (evlist__open(evlist) < 0) {
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *pyrf_evlist__close(struct pyrf_evlist *pevlist)
{
	struct evlist *evlist;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	evlist = pevlist->evlist;
	evlist__close(evlist);

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *pyrf_evlist__config(struct pyrf_evlist *pevlist)
{
	struct record_opts opts = {
		.sample_time	     = true,
		.mmap_pages	     = UINT_MAX,
		.user_freq	     = UINT_MAX,
		.user_interval	     = ULLONG_MAX,
		.freq		     = 4000,
		.target		     = {
			.uses_mmap   = true,
			.default_per_cpu = true,
		},
		.nr_threads_synthesize = 1,
		.ctl_fd              = -1,
		.ctl_fd_ack          = -1,
		.no_buffering        = true,
		.no_inherit          = true,
	};
	struct evlist *evlist;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	evlist = pevlist->evlist;
	evlist__config(evlist, &opts, &callchain_param);
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *pyrf_evlist__disable(struct pyrf_evlist *pevlist)
{
	CHECK_INITIALIZED(pevlist->evlist, "evlist");
	evlist__disable(pevlist->evlist);
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *pyrf_evlist__enable(struct pyrf_evlist *pevlist)
{
	CHECK_INITIALIZED(pevlist->evlist, "evlist");
	evlist__enable(pevlist->evlist);
	Py_INCREF(Py_None);
	return Py_None;
}

static PyMethodDef pyrf_evlist__methods[] = {
	{
		.ml_name  = "all_cpus",
		.ml_meth  = (PyCFunction)pyrf_evlist__all_cpus,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("CPU map union of all evsel CPU maps.")
	},
	{
		.ml_name  = "metrics",
		.ml_meth  = (PyCFunction)pyrf_evlist__metrics,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("List of metric names within the evlist.")
	},
	{
		.ml_name  = "compute_metric",
		.ml_meth  = (PyCFunction)pyrf_evlist__compute_metric,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("compute metric for given name, cpu and thread")
	},
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
		.ml_name  = "close",
		.ml_meth  = (PyCFunction)pyrf_evlist__close,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("close the file descriptors.")
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
		.ml_flags = METH_VARARGS,
		.ml_doc	  = PyDoc_STR("adds an event selector to the list.")
	},
	{
		.ml_name  = "read_on_cpu",
		.ml_meth  = (PyCFunction)pyrf_evlist__read_on_cpu,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("reads an event.")
	},
	{
		.ml_name  = "config",
		.ml_meth  = (PyCFunction)pyrf_evlist__config,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("Apply default record options to the evlist.")
	},
	{
		.ml_name  = "disable",
		.ml_meth  = (PyCFunction)pyrf_evlist__disable,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("Disable the evsels in the evlist.")
	},
	{
		.ml_name  = "enable",
		.ml_meth  = (PyCFunction)pyrf_evlist__enable,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("Enable the evsels in the evlist.")
	},
	{ .ml_name = NULL, }
};

static Py_ssize_t pyrf_evlist__length(PyObject *obj)
{
	struct pyrf_evlist *pevlist = (void *)obj;

	if (!pevlist->evlist)
		return 0;

	return evlist__nr_entries(pevlist->evlist);
}

static PyObject *pyrf_evsel__from_evsel(struct evsel *evsel)
{
	struct pyrf_evsel *pevsel = PyObject_New(struct pyrf_evsel, &pyrf_evsel__type);

	if (!pevsel)
		return NULL;

	pevsel->evsel = evsel__get(evsel);
	return (PyObject *)pevsel;
}

static PyObject *pyrf_evlist__item(PyObject *obj, Py_ssize_t i)
{
	struct pyrf_evlist *pevlist = (void *)obj;
	struct evsel *pos;

	if (!pevlist->evlist || i >= evlist__nr_entries(pevlist->evlist)) {
		PyErr_SetString(PyExc_IndexError, "Index out of range");
		return NULL;
	}

	evlist__for_each_entry(pevlist->evlist, pos) {
		if (i-- == 0)
			break;
	}
	return pyrf_evsel__from_evsel(pos);
}

static PyObject *pyrf_evlist__str(PyObject *self)
{
	struct pyrf_evlist *pevlist = (void *)self;
	struct evsel *pos;
	struct strbuf sb = STRBUF_INIT;
	bool first = true;
	PyObject *result;

	if (!pevlist->evlist)
		return PyUnicode_FromString("evlist(uninitialized)");

	strbuf_addstr(&sb, "evlist([");
	evlist__for_each_entry(pevlist->evlist, pos) {
		if (!first)
			strbuf_addch(&sb, ',');
		strbuf_addstr(&sb, evsel__name(pos));
		first = false;
	}
	strbuf_addstr(&sb, "])");
	result = PyUnicode_FromString(sb.buf);
	strbuf_release(&sb);
	return result;
}

static PySequenceMethods pyrf_evlist__sequence_methods = {
	.sq_length = pyrf_evlist__length,
	.sq_item   = pyrf_evlist__item,
};

static const char pyrf_evlist__doc[] = PyDoc_STR("perf event selector list object.");

static PyObject *pyrf_evlist__getattro(struct pyrf_evlist *pevlist, PyObject *attr_name)
{
	if (!pevlist->evlist) {
		PyErr_SetString(PyExc_ValueError, "evlist not initialized");
		return NULL;
	}
	return PyObject_GenericGetAttr((PyObject *) pevlist, attr_name);
}

static int pyrf_evlist__setattro(struct pyrf_evlist *pevlist, PyObject *attr_name, PyObject *value)
{
	if (!pevlist->evlist) {
		PyErr_SetString(PyExc_ValueError, "evlist not initialized");
		return -1;
	}
	return PyObject_GenericSetAttr((PyObject *) pevlist, attr_name, value);
}

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
	.tp_repr        = pyrf_evlist__str,
	.tp_str         = pyrf_evlist__str,
	.tp_getattro	= (getattrofunc) pyrf_evlist__getattro,
	.tp_setattro	= (setattrofunc) pyrf_evlist__setattro,
};

static PyObject *pyrf_evlist__new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
	struct pyrf_evlist *pevlist;

	pevlist = (struct pyrf_evlist *)PyType_GenericNew(type, args, kwargs);
	if (pevlist)
		pevlist->evlist = NULL;
	return (PyObject *)pevlist;
}

static int pyrf_evlist__setup_types(void)
{
	pyrf_evlist__type.tp_new = pyrf_evlist__new;
	return PyType_Ready(&pyrf_evlist__type);
}

#define PERF_CONST(name) { #name, PERF_##name }

struct perf_constant {
	const char *name;
	int	    value;
};

static const struct perf_constant perf__constants[] = {
	PERF_CONST(TYPE_HARDWARE),
	PERF_CONST(TYPE_SOFTWARE),
	PERF_CONST(TYPE_TRACEPOINT),
	PERF_CONST(TYPE_HW_CACHE),
	PERF_CONST(TYPE_RAW),
	PERF_CONST(TYPE_BREAKPOINT),

	PERF_CONST(COUNT_HW_CPU_CYCLES),
	PERF_CONST(COUNT_HW_REF_CPU_CYCLES),
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
	PERF_CONST(RECORD_STAT),
	PERF_CONST(RECORD_STAT_ROUND),

	PERF_CONST(RECORD_MISC_SWITCH_OUT),

	/* Values for the flags of a perf.call_return, see thread-stack.h. */
	{ "CALL_RETURN_NO_CALL",	CALL_RETURN_NO_CALL },
	{ "CALL_RETURN_NO_RETURN",	CALL_RETURN_NO_RETURN },
	{ "CALL_RETURN_NON_CALL",	CALL_RETURN_NON_CALL },
	{ .name = NULL, },
};

static PyObject *pyrf__tracepoint(struct pyrf_evsel *pevsel,
				  PyObject *args, PyObject *kwargs)
{
	static char *kwlist[] = { "sys", "name", NULL };
	char *sys  = NULL;
	char *name = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ss", kwlist,
					 &sys, &name))
		return NULL;

	return PyLong_FromLong(tp_pmu__id(sys, name));
}

static PyObject *pyrf_evlist__from_evlist(struct evlist *evlist)
{
	struct pyrf_evlist *pevlist = PyObject_New(struct pyrf_evlist, &pyrf_evlist__type);

	if (!pevlist)
		return NULL;

	pevlist->evlist = evlist__get(evlist);
	return (PyObject *)pevlist;
}

static PyObject *pyrf__parse_events(PyObject *self, PyObject *args)
{
	const char *input;
	struct evlist *evlist = evlist__new();
	struct parse_events_error err;
	PyObject *result;
	PyObject *pcpus = NULL, *pthreads = NULL;
	struct perf_cpu_map *cpus;
	struct perf_thread_map *threads;

	if (!evlist)
		return PyErr_NoMemory();

	if (!PyArg_ParseTuple(args, "s|OO", &input, &pcpus, &pthreads)) {
		evlist__put(evlist);
		return NULL;
	}

	if (pthreads && pthreads != Py_None &&
	    !PyObject_TypeCheck(pthreads, &pyrf_thread_map__type)) {
		PyErr_SetString(PyExc_TypeError, "threads must be a perf.thread_map or None");
		evlist__put(evlist);
		return NULL;
	}

	if (pcpus && pcpus != Py_None &&
	    !PyObject_TypeCheck(pcpus, &pyrf_cpu_map__type)) {
		PyErr_SetString(PyExc_TypeError, "cpus must be a perf.cpu_map or None");
		evlist__put(evlist);
		return NULL;
	}

	threads = (pthreads && pthreads != Py_None) ?
			((struct pyrf_thread_map *)pthreads)->threads : NULL;
	cpus = (pcpus && pcpus != Py_None) ?
			((struct pyrf_cpu_map *)pcpus)->cpus : NULL;

	parse_events_error__init(&err);
	perf_evlist__set_maps(evlist__core(evlist), cpus, threads);
	if (parse_events(evlist, input, &err)) {
		parse_events_error__print(&err, input);
		parse_events_error__exit(&err);
		PyErr_SetFromErrno(PyExc_OSError);
		evlist__put(evlist);
		return NULL;
	}
	parse_events_error__exit(&err);
	result = pyrf_evlist__from_evlist(evlist);
	evlist__put(evlist);
	return result;
}

static PyObject *pyrf__parse_metrics(PyObject *self, PyObject *args)
{
	const char *input, *pmu = NULL;
	struct evlist *evlist = evlist__new();
	PyObject *result;
	PyObject *pcpus = NULL, *pthreads = NULL;
	struct perf_cpu_map *cpus;
	struct perf_thread_map *threads;
	int ret;

	if (!evlist)
		return PyErr_NoMemory();

	if (!PyArg_ParseTuple(args, "s|sOO", &input, &pmu, &pcpus, &pthreads)) {
		evlist__put(evlist);
		return NULL;
	}

	if (pthreads && pthreads != Py_None &&
	    !PyObject_TypeCheck(pthreads, &pyrf_thread_map__type)) {
		PyErr_SetString(PyExc_TypeError, "threads must be a perf.thread_map or None");
		evlist__put(evlist);
		return NULL;
	}

	if (pcpus && pcpus != Py_None &&
	    !PyObject_TypeCheck(pcpus, &pyrf_cpu_map__type)) {
		PyErr_SetString(PyExc_TypeError, "cpus must be a perf.cpu_map or None");
		evlist__put(evlist);
		return NULL;
	}

	threads = (pthreads && pthreads != Py_None) ?
			((struct pyrf_thread_map *)pthreads)->threads : NULL;
	cpus = (pcpus && pcpus != Py_None) ?
			((struct pyrf_cpu_map *)pcpus)->cpus : NULL;

	perf_evlist__set_maps(evlist__core(evlist), cpus, threads);
	ret = metricgroup__parse_groups(evlist, pmu ?: "all",
					/*cputype_filter=*/false, input,
					/*metric_no_group=*/ false,
					/*metric_no_merge=*/ false,
					/*metric_no_threshold=*/ true,
					/*user_requested_cpu_list=*/ NULL,
					/*system_wide=*/true,
					/*hardware_aware_grouping=*/ false);
	if (ret) {
		evlist__put(evlist);
		errno = -ret;
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}
	result = pyrf_evlist__from_evlist(evlist);
	evlist__put(evlist);
	return result;
}

static PyObject *pyrf__metrics_groups(const struct pmu_metric *pm)
{
	PyObject *groups = PyList_New(/*len=*/0);
	const char *mg = pm->metric_group;

	if (!groups)
		return NULL;

	while (mg) {
		PyObject *val = NULL;
		const char *sep = strchr(mg, ';');
		size_t len = sep ? (size_t)(sep - mg) : strlen(mg);

		if (len > 0) {
			val = PyUnicode_FromStringAndSize(mg, len);
			if (val)
				PyList_Append(groups, val);

			Py_XDECREF(val);
		}
		mg = sep ? sep + 1 : NULL;
	}
	return groups;
}

static int pyrf__metrics_cb(const struct pmu_metric *pm,
			    const struct pmu_metrics_table *table __maybe_unused,
			    void *vdata)
{
	PyObject *py_list = vdata;
	PyObject *dict = PyDict_New();
	PyObject *key = dict ? PyUnicode_FromString("MetricGroup") : NULL;
	PyObject *value = key ? pyrf__metrics_groups(pm) : NULL;

	if (!value || PyDict_SetItem(dict, key, value) != 0) {
		Py_XDECREF(key);
		Py_XDECREF(value);
		Py_XDECREF(dict);
		return -ENOMEM;
	}
	Py_DECREF(key);
	Py_DECREF(value);

	if (!add_to_dict(dict, "MetricName", pm->metric_name) ||
	    !add_to_dict(dict, "PMU", pm->pmu) ||
	    !add_to_dict(dict, "MetricExpr", pm->metric_expr) ||
	    !add_to_dict(dict, "MetricThreshold", pm->metric_threshold) ||
	    !add_to_dict(dict, "ScaleUnit", pm->unit) ||
	    !add_to_dict(dict, "Compat", pm->compat) ||
	    !add_to_dict(dict, "BriefDescription", pm->desc) ||
	    !add_to_dict(dict, "PublicDescription", pm->long_desc) ||
	    PyList_Append(py_list, dict) != 0) {
		Py_DECREF(dict);
		return -ENOMEM;
	}
	Py_DECREF(dict);
	return 0;
}

static PyObject *pyrf__metrics(PyObject *self, PyObject *args)
{
	const struct pmu_metrics_table *table = pmu_metrics_table__find();
	PyObject *list = PyList_New(/*len=*/0);
	int ret;

	if (!list)
		return NULL;

	ret = pmu_metrics_table__for_each_metric(table, pyrf__metrics_cb, list);
	if (!ret)
		ret = pmu_for_each_sys_metric(pyrf__metrics_cb, list);

	if (ret) {
		Py_DECREF(list);
		errno = -ret;
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}
	return list;
}

struct pyrf_data {
	PyObject_HEAD

	struct perf_data data;
};

static int pyrf_data__init(struct pyrf_data *pdata, PyObject *args, PyObject *kwargs)
{
	static char *kwlist[] = { "path", "fd", NULL };
	char *path = NULL;
	int fd = -1;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|si", kwlist, &path, &fd))
		return -1;

	if (pdata->data.open)
		perf_data__close(&pdata->data);
	free((char *)pdata->data.path);
	memset(&pdata->data, 0, sizeof(pdata->data));

	if (fd != -1) {
		struct stat st;

		if (fstat(fd, &st) < 0 || !S_ISFIFO(st.st_mode)) {
			PyErr_SetString(PyExc_ValueError,
					"fd argument is only supported for pipes");
			return -1;
		}
		if (!path)
			path = "-";
		else if (strcmp(path, "-") != 0) {
			PyErr_SetString(PyExc_ValueError,
					"path must be '-' when fd is provided");
			return -1;
		}
		fd = dup(fd);
		if (fd < 0) {
			PyErr_SetFromErrno(PyExc_OSError);
			return -1;
		}
	} else if (path && strcmp(path, "-") == 0) {
		fd = dup(0);
		if (fd < 0) {
			PyErr_SetFromErrno(PyExc_OSError);
			return -1;
		}
	}

	if (!path)
		path = "perf.data";

	pdata->data.path = strdup(path);
	if (!pdata->data.path) {
		if (fd != -1)
			close(fd);
		PyErr_NoMemory();
		return -1;
	}

	pdata->data.mode = PERF_DATA_MODE_READ;
	pdata->data.file.fd = fd;
	if (perf_data__open(&pdata->data) < 0) {
		if (fd >= 0) {
			close(fd);
			pdata->data.file.fd = -1;
		}
		PyErr_Format(PyExc_IOError, "Failed to open perf data: %s",
			     pdata->data.path ? pdata->data.path : "perf.data");
		return -1;
	}
	return 0;
}

static void pyrf_data__delete(struct pyrf_data *pdata)
{
	perf_data__close(&pdata->data);
	free((char *)pdata->data.path);
	Py_TYPE(pdata)->tp_free((PyObject *)pdata);
}

static PyObject *pyrf_data__str(PyObject *self)
{
	const struct pyrf_data *pdata = (const struct pyrf_data *)self;

	if (!pdata->data.path)
		return PyUnicode_FromString("[uninitialized]");
	return PyUnicode_FromString(pdata->data.path);
}

static PyObject *pyrf_data__new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
	struct pyrf_data *pdata;

	pdata = (struct pyrf_data *)PyType_GenericNew(type, args, kwargs);
	if (pdata)
		memset(&pdata->data, 0, sizeof(pdata->data));
	return (PyObject *)pdata;
}

static const char pyrf_data__doc[] = PyDoc_STR("perf data file object.");

static PyTypeObject pyrf_data__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.data",
	.tp_basicsize	= sizeof(struct pyrf_data),
	.tp_dealloc	= (destructor)pyrf_data__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_data__doc,
	.tp_init	= (initproc)pyrf_data__init,
	.tp_repr	= pyrf_data__str,
	.tp_str		= pyrf_data__str,
};

static int pyrf_data__setup_types(void)
{
	pyrf_data__type.tp_new = pyrf_data__new;
	return PyType_Ready(&pyrf_data__type);
}

struct pyrf_thread {
	PyObject_HEAD

	struct thread *thread;
};

static void pyrf_thread__delete(struct pyrf_thread *pthread)
{
	thread__put(pthread->thread);
	Py_TYPE(pthread)->tp_free((PyObject *)pthread);
}

static PyObject *pyrf_thread__comm(PyObject *obj)
{
	struct pyrf_thread *pthread = (void *)obj;
	struct thread *thread = pthread->thread;
	bool has_comm = false;
	char *str = NULL;
	PyObject *ret;

	CHECK_INITIALIZED(thread, "perf.thread");

	/*
	 * Copy the comm string under comm_lock so a concurrent PERF_RECORD_COMM
	 * in perf_session__process_events() (which runs with the GIL released)
	 * cannot free the old comm_str via comm__override() while it is being
	 * read, and drop comm_lock before calling into the Python allocator.
	 */
	down_read(thread__comm_lock(thread));
	if (!list_empty(thread__comm_list(thread))) {
		const struct comm *comm;

		comm = list_first_entry(thread__comm_list(thread), struct comm, list);
		if (comm && comm__str(comm)) {
			has_comm = true;
			str = strdup(comm__str(comm));
		}
	}
	up_read(thread__comm_lock(thread));

	if (!has_comm)
		Py_RETURN_NONE;
	if (!str)
		return PyErr_NoMemory();
	ret = PyUnicode_FromString(str);
	free(str);
	return ret;
}

static PyMethodDef pyrf_thread__methods[] = {
	{
		.ml_name  = "comm",
		.ml_meth  = (PyCFunction)pyrf_thread__comm,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("Comm(and) associated with this thread.")
	},
	{ .ml_name = NULL, }
};

static PyObject *pyrf_thread__get_pid(struct pyrf_thread *pthread, void *closure __maybe_unused)
{
	CHECK_INITIALIZED(pthread->thread, "thread");
	return PyLong_FromLong(thread__pid(pthread->thread));
}

static PyObject *pyrf_thread__get_tid(struct pyrf_thread *pthread, void *closure __maybe_unused)
{
	CHECK_INITIALIZED(pthread->thread, "thread");
	return PyLong_FromLong(thread__tid(pthread->thread));
}

static PyObject *pyrf_thread__get_ppid(struct pyrf_thread *pthread, void *closure __maybe_unused)
{
	CHECK_INITIALIZED(pthread->thread, "thread");
	return PyLong_FromLong(thread__ppid(pthread->thread));
}

static PyObject *pyrf_thread__get_cpu(struct pyrf_thread *pthread, void *closure __maybe_unused)
{
	CHECK_INITIALIZED(pthread->thread, "thread");
	return PyLong_FromLong(thread__cpu(pthread->thread));
}

static PyGetSetDef pyrf_thread__getset[] = {
	{ .name = "pid", .get = (getter)pyrf_thread__get_pid, .doc = "process ID" },
	{ .name = "tid", .get = (getter)pyrf_thread__get_tid, .doc = "thread ID" },
	{ .name = "ppid", .get = (getter)pyrf_thread__get_ppid, .doc = "parent process ID" },
	{ .name = "cpu", .get = (getter)pyrf_thread__get_cpu, .doc = "cpu number" },
	{ .name = NULL }
};

static const char pyrf_thread__doc[] = PyDoc_STR("perf thread object.");

static PyTypeObject pyrf_thread__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.thread",
	.tp_basicsize	= sizeof(struct pyrf_thread),
	.tp_dealloc	= (destructor)pyrf_thread__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_methods	= pyrf_thread__methods,
	.tp_getset	= pyrf_thread__getset,
	.tp_doc		= pyrf_thread__doc,
};

static int pyrf_thread__setup_types(void)
{
	return PyType_Ready(&pyrf_thread__type);
}

static PyObject *pyrf_thread__from_thread(struct thread *thread)
{
	struct pyrf_thread *pthread = PyObject_New(struct pyrf_thread, &pyrf_thread__type);

	if (!pthread)
		return NULL;

	pthread->thread = thread__get(thread);
	return (PyObject *)pthread;
}

struct pyrf_session {
	PyObject_HEAD

	struct perf_session *session;
	struct perf_tool tool;
	struct itrace_synth_opts itrace_opts;
	struct pyrf_data *pdata;
	PyObject *sample;
	PyObject *stat;
	PyObject *context_switch;
	PyObject *call_return;
	struct call_return_processor *crp;
	/**
	 * @call_return_last_db_id: Counter used to give each perf.call_return a
	 * unique db_id. A call's db_id may be allocated before the call
	 * returns, so the identifiers cannot be generated by the Python script.
	 */
	u64 call_return_last_db_id;
	u64 sample_last_db_id;
	/**
	 * @callback_raised: A python callback raised an exception and requested
	 * that event processing stops, see pyrf_session__callback_raised().
	 */
	bool callback_raised;
	unsigned int callback_depth;
	unsigned long callback_thread;
	char *vmlinux_name;
	char *kallsyms_name;
	char *symfs;
};

struct pyrf_call_return {
	PyObject_HEAD
	struct call_return cr;
	/**
	 * @call_path: perf.callchain of the call path, ordered from the
	 * outermost caller to the callee. Resolved eagerly as the underlying
	 * struct call_path is owned by the call return processor.
	 */
	PyObject *call_path;
	char *comm;
	pid_t machine_pid;
	pid_t pid;
	pid_t tid;
};

static PyObject *pyrf_call_return__repr(struct pyrf_call_return *pcr)
{
	return PyUnicode_FromString("{ type: call_return }");
}

static PyObject *pyrf_call_return__get_comm(struct pyrf_call_return *pcr,
				       void *closure __maybe_unused)
{
	if (pcr->comm)
		return PyUnicode_FromString(pcr->comm);
	Py_RETURN_NONE;
}

static PyObject *pyrf_call_return__get_machine_pid(struct pyrf_call_return *pcr,
						   void *closure __maybe_unused)
{
	return PyLong_FromLong(pcr->machine_pid);
}

static PyObject *pyrf_call_return__get_pid(struct pyrf_call_return *pcr,
					   void *closure __maybe_unused)
{
	if (pcr->pid != -1)
		return PyLong_FromLong(pcr->pid);
	Py_RETURN_NONE;
}

static PyObject *pyrf_call_return__get_tid(struct pyrf_call_return *pcr,
				       void *closure __maybe_unused)
{
	if (pcr->tid != -1)
		return PyLong_FromLong(pcr->tid);
	Py_RETURN_NONE;
}

static PyObject *pyrf_call_return__get_call_time(struct pyrf_call_return *pcr,
				       void *closure __maybe_unused)
{
	struct call_return *cr = &pcr->cr;

	return PyLong_FromUnsignedLongLong(cr->call_time);
}

static PyObject *pyrf_call_return__get_return_time(struct pyrf_call_return *pcr,
				       void *closure __maybe_unused)
{
	struct call_return *cr = &pcr->cr;

	return PyLong_FromUnsignedLongLong(cr->return_time);
}

static PyObject *pyrf_call_return__get_branch_count(struct pyrf_call_return *pcr,
				       void *closure __maybe_unused)
{
	struct call_return *cr = &pcr->cr;

	return PyLong_FromUnsignedLongLong(cr->branch_count);
}

static PyObject *pyrf_call_return__get_call_ref(struct pyrf_call_return *pcr,
				       void *closure __maybe_unused)
{
	struct call_return *cr = &pcr->cr;

	return PyLong_FromUnsignedLongLong(cr->call_ref);
}

static PyObject *pyrf_call_return__get_return_ref(struct pyrf_call_return *pcr,
				       void *closure __maybe_unused)
{
	struct call_return *cr = &pcr->cr;

	return PyLong_FromUnsignedLongLong(cr->return_ref);
}

static PyObject *pyrf_call_return__get_flags(struct pyrf_call_return *pcr,
				       void *closure __maybe_unused)
{
	struct call_return *cr = &pcr->cr;

	return PyLong_FromLong(cr->flags);
}

static PyObject *pyrf_call_return__get_call_path(struct pyrf_call_return *pcr,
				       void *closure __maybe_unused)
{
	PyObject *call_path = pcr->call_path ?: Py_None;

	Py_INCREF(call_path);
	return call_path;
}

static PyObject *pyrf_call_return__get_parent_id(struct pyrf_call_return *pcr,
						 void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLongLong(pcr->cr.parent_db_id);
}

static PyObject *pyrf_call_return__get_insn_count(struct pyrf_call_return *pcr,
						  void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLongLong(pcr->cr.insn_count);
}

static PyObject *pyrf_call_return__get_cyc_count(struct pyrf_call_return *pcr,
						 void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLongLong(pcr->cr.cyc_count);
}

static PyObject *pyrf_call_return__get_db_id(struct pyrf_call_return *pcr,
					     void *closure __maybe_unused)
{
	return PyLong_FromUnsignedLongLong(pcr->cr.db_id);
}

static PyGetSetDef pyrf_call_return__getset[] = {
	{
		.name = "db_id",
		.get = (getter)pyrf_call_return__get_db_id,
		.doc = "Unique identifier of this call return.",
	},
	{
		.name = "parent_id",
		.get = (getter)pyrf_call_return__get_parent_id,
		.doc = "db_id of the calling call return, 0 if there is none.",
	},
	{
		.name = "insn_count",
		.get = (getter)pyrf_call_return__get_insn_count,
		.doc = "Instructions executed between the call and the return.",
	},
	{
		.name = "cyc_count",
		.get = (getter)pyrf_call_return__get_cyc_count,
		.doc = "Cycles elapsed between the call and the return.",
	},
	{
		.name = "comm",
		.get = (getter)pyrf_call_return__get_comm,
		.doc = "Thread comm at the time of the call/return, or None.",
	},
	{
		.name = "machine_pid",
		.get = (getter)pyrf_call_return__get_machine_pid,
		.doc = "Machine PID (0 for host).",
	},
	{
		.name = "pid",
		.get = (getter)pyrf_call_return__get_pid,
		.doc = "Process ID of the thread, or None.",
	},
	{
		.name = "tid",
		.get = (getter)pyrf_call_return__get_tid,
		.doc = "Thread identifier, None if unknown.",
	},
	{
		.name = "call_time",
		.get = (getter)pyrf_call_return__get_call_time,
		.doc = "Timestamp of the call.",
	},
	{
		.name = "return_time",
		.get = (getter)pyrf_call_return__get_return_time,
		.doc = "Timestamp of the return.",
	},
	{
		.name = "branch_count",
		.get = (getter)pyrf_call_return__get_branch_count,
		.doc = "Branches taken between the call and the return.",
	},
	{
		.name = "call_ref",
		.get = (getter)pyrf_call_return__get_call_ref,
		.doc = "Reference to the sample of the call.",
	},
	{
		.name = "return_ref",
		.get = (getter)pyrf_call_return__get_return_ref,
		.doc = "Reference to the sample of the return.",
	},
	{
		.name = "flags",
		.get = (getter)pyrf_call_return__get_flags,
		.doc = "Bitmask of perf.CALL_RETURN_* values.",
	},
	{
		.name = "call_path",
		.get = (getter)pyrf_call_return__get_call_path,
		.doc = "perf.callchain from outermost caller to callee.",
	},
	{ .name = NULL, },
};

static void pyrf_call_return__delete(struct pyrf_call_return *pevent)
{
	Py_XDECREF(pevent->call_path);
	free(pevent->comm);
	Py_TYPE(pevent)->tp_free((PyObject *)pevent);
}

static PyTypeObject pyrf_call_return__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.call_return",
	.tp_basicsize	= sizeof(struct pyrf_call_return),
	.tp_dealloc	= (destructor)pyrf_call_return__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= "perf call_return object.",
	.tp_getset	= pyrf_call_return__getset,
	.tp_repr	= (reprfunc)pyrf_call_return__repr,
};

/*
 * Turn a struct call_path, that is a leaf with parent pointers up to the
 * outermost caller, into a perf.callchain ordered from the outermost caller to
 * the leaf. The struct call_path is owned by the call return processor and so
 * must be resolved before returning to Python.
 */
static PyObject *pyrf_call_path__to_callchain(const struct call_path *cp,
					      struct thread *thread)
{
	struct pyrf_callchain *pchain;
	const struct call_path *pos;
	u64 nr_frames = 0;

	/* The root call path is a sentinel with no symbol, skip it. */
	for (pos = cp; pos && pos->parent; pos = pos->parent)
		nr_frames++;

	pchain = PyObject_New(struct pyrf_callchain, &pyrf_callchain__type);
	if (!pchain)
		return NULL;

	pchain->nr_frames = nr_frames;
	pchain->frames = nr_frames ? calloc(nr_frames, sizeof(*pchain->frames)) : NULL;
	if (nr_frames && !pchain->frames) {
		pchain->nr_frames = 0;
		Py_DECREF(pchain);
		return PyErr_NoMemory();
	}
	/* Fill in reverse so that the outermost caller is first. */
	for (pos = cp; nr_frames > 0; pos = pos->parent) {
		struct pyrf_callchain_frame *frame = &pchain->frames[--nr_frames];

		frame->ip = pos->ip;
		frame->sym = NULL;
		frame->sym_name = pos->sym ? strdup(pos->sym->name) : NULL;
		frame->map = NULL;
		if (thread) {
			struct addr_location al;
			u8 cpumode = pos->in_kernel ? PERF_RECORD_MISC_KERNEL
						    : PERF_RECORD_MISC_USER;

			addr_location__init(&al);
			thread__find_map(thread, cpumode, pos->ip, &al);
			frame->map = map__get(al.map);
			if (frame->map)
				frame->sym = pos->sym;
			addr_location__exit(&al);
		}
	}
	return (PyObject *)pchain;
}

static PyObject *pyrf_call_return__new(struct call_return *cr)
{
	struct pyrf_call_return *pevent;

	pevent = PyObject_New(struct pyrf_call_return, &pyrf_call_return__type);
	if (!pevent)
		return NULL;

	pevent->cr = *cr;
	/* Avoid lifetime issues with thread by caching tid/pid/machine_pid */
	if (cr->thread) {
		struct maps *maps = thread__maps(cr->thread);

		pevent->machine_pid = maps && maps__machine(maps) ? maps__machine(maps)->pid : 0;
		pevent->pid = thread__pid(cr->thread);
		pevent->tid = thread__tid(cr->thread);
	} else {
		pevent->machine_pid = 0;
		pevent->pid = -1;
		pevent->tid = -1;
	}
	pevent->cr.thread = NULL;
	pevent->comm = cr->comm ? strdup(comm__str(cr->comm)) : NULL;
	pevent->call_path = NULL;
	if (cr->cp) {
		pevent->call_path = pyrf_call_path__to_callchain(cr->cp, cr->thread);
		if (!pevent->call_path) {
			Py_DECREF(pevent);
			return NULL;
		}
	}
	return (PyObject *)pevent;
}

/*
 * Number of times a python callback has requested that event processing
 * stops, see pyrf_session__callback_raised().
 */
static unsigned int pyrf_session__stop_requests;

/*
 * A python callback raised an exception.
 *
 * Returning an error to the session code would cause reader__read_event() to
 * print a "processing failed for event of type: ..." error to stderr. This is
 * noise, and in the case of a TUI corrupts the display, when a callback raises
 * to deliberately stop processing (e.g. cancelling a background load) and the
 * error message is redundant with the python exception that is raised anyway.
 *
 * Instead request that event processing stops by setting session_done and
 * return success. The exception remains pending (PyErr_Occurred()) and
 * pyrf_session__find_thread_events() returns NULL so python raises it.
 *
 * Note, session_done is a process-global rather than per-session. Were
 * multiple sessions processing events concurrently in different python
 * threads then an exception in one would stop all of them. Only the session
 * whose callback raised clears session_done, and the other sessions detect
 * the stop request, using pyrf_session__stop_requests, and raise an error
 * rather than silently dropping events.
 */
static int pyrf_session__callback_raised(struct pyrf_session *psession)
{
	psession->callback_raised = true;
	pyrf_session__stop_requests++;
	session_done = 1;
	return 0;
}

/*
 * session_done stops the reader loops, the ordered_events and deferred sample
 * flushes, and auxtrace__flush_events(), but some callbacks may still happen
 * after a callback has raised. For example, a single sample may generate
 * multiple call_return callbacks, and perf_session__flush_thread_stacks()
 * delivers call returns for the remaining stack entries (skipping it would
 * only defer those callbacks until the threads are deleted). Calling into
 * python with an exception pending is an error, and the events would be
 * discarded anyway, so callbacks check this and skip calling python.
 */
static bool pyrf_session__exception_pending(void)
{
	return PyErr_Occurred() != NULL;
}

static int __pyrf_session__call_return_process(struct call_return *cr,
					       u64 *parent_db_id,
					       struct pyrf_session *psession)
{
	PyObject *pyevent, *ret;

	if (!psession->call_return || pyrf_session__exception_pending())
		return 0;

	if (!cr->db_id)
		cr->db_id = ++psession->call_return_last_db_id;

	if (parent_db_id) {
		if (!*parent_db_id)
			*parent_db_id = ++psession->call_return_last_db_id;
		cr->parent_db_id = *parent_db_id;
	}

	pyevent = pyrf_call_return__new(cr);
	if (!pyevent)
		return -1;

	ret = PyObject_CallFunctionObjArgs(psession->call_return, pyevent, NULL);
	Py_DECREF(pyevent);
	if (!ret)
		return pyrf_session__callback_raised(psession);
	Py_DECREF(ret);
	return 0;
}

static PyGILState_STATE pyrf_session__callback_enter(struct pyrf_session *psession)
{
	PyGILState_STATE gstate = PyGILState_Ensure();

	psession->callback_thread = PyThread_get_thread_ident();
	psession->callback_depth++;
	return gstate;
}

static void pyrf_session__callback_leave(struct pyrf_session *psession,
					 PyGILState_STATE gstate)
{
	psession->callback_depth--;
	PyGILState_Release(gstate);
}

/*
 * Call returns may be processed during a sample callback, or when thread
 * stacks are flushed, so acquire the GIL, see pyrf_session_tool__sample().
 */
static int pyrf_session__call_return_process(struct call_return *cr,
					     u64 *parent_db_id,
					     void *data)
{
	struct pyrf_session *psession = data;
	PyGILState_STATE gstate = pyrf_session__callback_enter(psession);
	int ret = __pyrf_session__call_return_process(cr, parent_db_id, psession);

	pyrf_session__callback_leave(psession, gstate);
	return ret;
}

static int pyrf_session__call_event(struct pyrf_session *psession, PyObject *callback,
				    PyObject *pyevent, const char *extra_arg, bool has_extra_arg)
{
	PyObject *ret;
	int err = 0;

	if (has_extra_arg)
		ret = PyObject_CallFunction(callback, "Oz", pyevent, extra_arg);
	else
		ret = PyObject_CallFunctionObjArgs(callback, pyevent, NULL);

	Py_XDECREF(ret);
	if (Py_REFCNT(pyevent) > 1) {
		PyObject *exc_type = NULL, *exc_value = NULL, *exc_tb = NULL;

		/*
		 * The Python callback retained a reference to the event
		 * beyond the callback's return (or in an exception frame);
		 * copy the underlying event and sample before process_events
		 * reuses or unmaps its buffer.
		 */
		if (!ret)
			PyErr_Fetch(&exc_type, &exc_value, &exc_tb);
		err = pyrf_event__copy((struct pyrf_event *)pyevent);
		if (!ret) {
			PyErr_Clear();
			PyErr_Restore(exc_type, exc_value, exc_tb);
		}
	}
	Py_DECREF(pyevent);
	if (!ret || err < 0)
		return pyrf_session__callback_raised(psession);
	return 0;
}

static int __pyrf_session_tool__sample(const struct perf_tool *tool,
				       union perf_event *event,
				       struct perf_sample *sample,
				       struct machine *machine)
{
	struct pyrf_session *psession = container_of(tool, struct pyrf_session, tool);
	u64 sample_db_id = ++psession->sample_last_db_id;

	if (pyrf_session__exception_pending())
		return 0;

	if (psession->crp) {
		struct addr_location al, addr_al;
		struct thread *thread = NULL;

		addr_location__init(&al);
		addr_location__init(&addr_al);

		if (machine__resolve(machine, &al, sample) >= 0) {
			thread = al.thread;
			if (sample->flags & (PERF_IP_FLAG_CALL |
					     PERF_IP_FLAG_TRACE_BEGIN |
					     PERF_IP_FLAG_RETURN |
					     PERF_IP_FLAG_BRANCH |
					     PERF_IP_FLAG_TRACE_END)) {
				thread__resolve(thread, &addr_al, sample);
			}

			int err;

			err = thread_stack__process(thread, thread__comm(thread), sample,
					      &al, &addr_al, sample_db_id, psession->crp);
			if (err) {
				addr_location__exit(&addr_al);
				addr_location__exit(&al);
				return err;
			}
		}

		addr_location__exit(&addr_al);
		addr_location__exit(&al);

		/* A call_return callback may have raised. */
		if (pyrf_session__exception_pending())
			return 0;
	}

	if (psession->sample) {
		PyObject *pyevent = pyrf_event__new(event, sample->evsel,
						    psession->session,
						    machine, sample);

		if (pyevent == NULL)
			return -ENOMEM;

		return pyrf_session__call_event(psession, psession->sample, pyevent,
						/*extra_arg=*/NULL, /*has_extra_arg=*/false);
	}
	return 0;
}

static int __pyrf_session_tool__context_switch(const struct perf_tool *tool,
					       union perf_event *event,
					       struct perf_sample *sample,
					       struct machine *machine)
{
	struct pyrf_session *psession = container_of(tool, struct pyrf_session, tool);
	PyObject *pyevent;

	if (pyrf_session__exception_pending()) {
		/*
		 * Skip calling python, but keep the machine's thread state
		 * consistent as perf_event__process_switch() would have been
		 * called had no python callback been registered.
		 */
		return perf_event__process_switch(tool, event, sample, machine);
	}

	pyevent = pyrf_event__new(event, sample->evsel, psession->session, machine, sample);
	if (perf_event__process_switch(tool, event, sample, machine) < 0) {
		Py_XDECREF(pyevent);
		return -1;
	}

	if (pyevent == NULL)
		return -ENOMEM;

	return pyrf_session__call_event(psession, psession->context_switch, pyevent,
					/*extra_arg=*/NULL, /*has_extra_arg=*/false);
}

static int __pyrf_session_tool__stat(const struct perf_tool *tool,
				     struct perf_session *session,
				     union perf_event *event)
{
	struct pyrf_session *psession = container_of(tool, struct pyrf_session, tool);
	struct evsel *evsel = evlist__id2evsel(session->evlist, event->stat.id);
	const char *name = evsel ? evsel__name(evsel) : "unknown";
	PyObject *pyevent;

	if (pyrf_session__exception_pending())
		return 0;

	pyevent = pyrf_event__new(event, /*evsel=*/NULL, psession->session,
				  /*machine=*/NULL, NULL);
	if (pyevent == NULL)
		return -ENOMEM;

	return pyrf_session__call_event(psession, psession->stat, pyevent,
					name, /*has_extra_arg=*/true);
}

static int __pyrf_session_tool__stat_round(const struct perf_tool *tool,
					   struct perf_session *session __maybe_unused,
					   union perf_event *event)
{
	struct pyrf_session *psession = container_of(tool, struct pyrf_session, tool);
	PyObject *pyevent;

	if (pyrf_session__exception_pending())
		return 0;

	pyevent = pyrf_event__new(event, /*evsel=*/NULL, psession->session,
				  /*machine=*/NULL, NULL);
	if (pyevent == NULL)
		return -ENOMEM;

	return pyrf_session__call_event(psession, psession->stat, pyevent,
					/*extra_arg=*/NULL, /*has_extra_arg=*/true);
}

/*
 * The session currently inside perf_session__new() or
 * perf_session__process_events() with the GIL released. Because symbol_conf
 * and session_done are process-globals and perf_session is not thread-safe,
 * only one session may be active at a time. Modified with the GIL held.
 */
static struct pyrf_session *pyrf_session__active;

static bool pyrf_session__check_not_busy(struct pyrf_session *psession)
{
	if (pyrf_session__active == psession &&
	    (!psession->callback_depth ||
	     psession->callback_thread != PyThread_get_thread_ident())) {
		PyErr_SetString(PyExc_RuntimeError,
				"perf.session is busy processing events in another thread");
		return false;
	}
	return true;
}

/*
 * The GIL is released while events are processed, see
 * pyrf_session__find_thread_events(), so that other python threads can run,
 * such as a UI thread while a session reads from a pipe that may block. The
 * tool callbacks that use python acquire the GIL, PyGILState_Ensure() is
 * reentrant so nested callbacks, like call returns during a sample, work.
 */

static int pyrf_session_tool__sample(const struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct machine *machine)
{
	struct pyrf_session *psession = container_of(tool, struct pyrf_session, tool);
	PyGILState_STATE gstate = pyrf_session__callback_enter(psession);
	int ret = __pyrf_session_tool__sample(tool, event, sample, machine);

	pyrf_session__callback_leave(psession, gstate);
	return ret;
}

static int pyrf_session_tool__context_switch(const struct perf_tool *tool,
					     union perf_event *event,
					     struct perf_sample *sample,
					     struct machine *machine)
{
	struct pyrf_session *psession = container_of(tool, struct pyrf_session, tool);
	PyGILState_STATE gstate = pyrf_session__callback_enter(psession);
	int ret = __pyrf_session_tool__context_switch(tool, event, sample, machine);

	pyrf_session__callback_leave(psession, gstate);
	return ret;
}

static int pyrf_session_tool__stat(const struct perf_tool *tool,
				   struct perf_session *session,
				   union perf_event *event)
{
	struct pyrf_session *psession = container_of(tool, struct pyrf_session, tool);
	PyGILState_STATE gstate = pyrf_session__callback_enter(psession);
	int ret = __pyrf_session_tool__stat(tool, session, event);

	pyrf_session__callback_leave(psession, gstate);
	return ret;
}

static int pyrf_session_tool__stat_round(const struct perf_tool *tool,
					 struct perf_session *session,
					 union perf_event *event)
{
	struct pyrf_session *psession = container_of(tool, struct pyrf_session, tool);
	PyGILState_STATE gstate = pyrf_session__callback_enter(psession);
	int ret = __pyrf_session_tool__stat_round(tool, session, event);

	pyrf_session__callback_leave(psession, gstate);
	return ret;
}

static PyObject *pyrf_session__find_thread(struct pyrf_session *psession, PyObject *args)
{
	struct machine *machine;
	struct thread *thread = NULL;
	PyObject *result;
	int pid, tid = -1;

	CHECK_INITIALIZED(psession->session, "session");
	if (!pyrf_session__check_not_busy(psession))
		return NULL;

	if (!PyArg_ParseTuple(args, "i|i", &pid, &tid))
		return NULL;

	if (tid == -1) {
		tid = pid;
		pid = -1;
	}

	/*
	 * When host lookup fails, guest machines (default_guest and the guests
	 * rb_root) are searched.
	 */
	machine = &psession->session->machines.host;
	thread = machine__find_thread(machine, pid, tid);

	if (!thread) {
		machine = perf_session__find_machine(psession->session, pid != -1 ? pid : tid);
		if (machine)
			thread = machine__find_thread(machine, pid, tid);
		if (!thread) {
			machine = machines__find(&psession->session->machines,
						 DEFAULT_GUEST_KERNEL_ID);
			if (machine)
				thread = machine__find_thread(machine, pid, tid);
		}
	}

	/* Return None rather than raising TypeError when a PID/TID is not known. */
	if (!thread)
		Py_RETURN_NONE;
	result = pyrf_thread__from_thread(thread);
	thread__put(thread);
	return result;
}

static PyObject *pyrf_session__new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
	struct pyrf_data *pdata;
	PyObject *sample = NULL, *stat = NULL, *context_switch = NULL;
	PyObject *call_return = NULL;
	char *itrace_str = NULL;
	char *vmlinux_str = NULL, *kallsyms_str = NULL, *symfs_str = NULL;
	static char *kwlist[] = { "data", "sample", "stat", "context_switch",
				  "call_return", "itrace", "vmlinux", "kallsyms",
				  "symfs", NULL };
	struct pyrf_session *psession;
	struct perf_session *session;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!|OOOOzzzz", kwlist,
					 &pyrf_data__type, &pdata,
					 &sample, &stat, &context_switch,
					 &call_return, &itrace_str,
					 &vmlinux_str, &kallsyms_str, &symfs_str))
		return NULL;

	psession = PyObject_New(struct pyrf_session, type);
	if (!psession)
		return NULL;

	/* PyObject_New doesn't zero the memory, initialize every member. */
	psession->session = NULL;
	psession->sample = NULL;
	psession->stat = NULL;
	psession->context_switch = NULL;
	psession->call_return = NULL;
	psession->crp = NULL;
	psession->call_return_last_db_id = 0;
	psession->sample_last_db_id = 0;
	psession->callback_raised = false;
	psession->callback_depth = 0;
	psession->callback_thread = 0;
	psession->vmlinux_name = NULL;
	psession->kallsyms_name = NULL;
	psession->symfs = NULL;
	psession->pdata = NULL;
	memset(&psession->itrace_opts, 0, sizeof(psession->itrace_opts));

	Py_INCREF(pdata);
	psession->pdata = pdata;

	perf_tool__init(&psession->tool, /*ordered_events=*/true);
	psession->tool.ordering_requires_timestamps = true;

	#define ADD_TOOL(name)						\
	do {								\
		if (name && name != Py_None) {				\
			if (!PyCallable_Check(name)) {			\
				PyErr_SetString(PyExc_TypeError, #name " must be callable"); \
				goto err_out;				\
			}						\
			psession->tool.name = pyrf_session_tool__##name; \
			Py_INCREF(name);				\
			psession->name = name;				\
		}							\
	} while (0)

	ADD_TOOL(sample);
	if (call_return && call_return != Py_None && (!sample || sample == Py_None))
		psession->tool.sample = pyrf_session_tool__sample;
	ADD_TOOL(stat);
	ADD_TOOL(context_switch);

	if (call_return && call_return != Py_None) {
		if (!PyCallable_Check(call_return)) {
			PyErr_SetString(PyExc_TypeError, "call_return must be callable");
			Py_DECREF(psession);
			return NULL;
		}
		Py_INCREF(call_return);
		psession->call_return = call_return;
		psession->crp = call_return_processor__new(pyrf_session__call_return_process,
							   psession);
		if (!psession->crp) {
			PyErr_SetString(PyExc_MemoryError,
					"Failed to allocate call_return_processor");
			Py_DECREF(psession);
			return NULL;
		}
	}
	#undef ADD_TOOL

	if (stat && stat != Py_None)
		psession->tool.stat_round = pyrf_session_tool__stat_round;

	psession->tool.comm		= perf_event__process_comm;
	psession->tool.auxtrace_info = perf_event__process_auxtrace_info;
	psession->tool.auxtrace = perf_event__process_auxtrace;
	psession->tool.auxtrace_error = perf_event__process_auxtrace_error;
	psession->tool.id_index	= perf_event__process_id_index;
	psession->tool.mmap		= perf_event__process_mmap;
	psession->tool.mmap2            = perf_event__process_mmap2;
	psession->tool.namespaces       = perf_event__process_namespaces;
	psession->tool.cgroup           = perf_event__process_cgroup;
	psession->tool.exit             = perf_event__process_exit;
	psession->tool.fork             = perf_event__process_fork;
	psession->tool.ksymbol          = perf_event__process_ksymbol;
	psession->tool.text_poke        = perf_event__process_text_poke;
	psession->tool.build_id         = perf_event__process_build_id;
	psession->tool.attr		= perf_event__process_attr;
	psession->tool.event_update	= perf_event__process_event_update;
	psession->tool.feature		= perf_event__process_feature;
#ifdef HAVE_LIBTRACEEVENT
	psession->tool.tracing_data	= perf_event__process_tracing_data;
#endif

	if (!vmlinux_str)
		vmlinux_str = getenv("PERF_SYMBOL_VMLINUX");
	if (vmlinux_str && vmlinux_str[0]) {
		psession->vmlinux_name = strdup(vmlinux_str);
		if (!psession->vmlinux_name) {
			PyErr_NoMemory();
			goto err_out;
		}
	}

	if (!kallsyms_str)
		kallsyms_str = getenv("PERF_SYMBOL_KALLSYMS");
	if (kallsyms_str && kallsyms_str[0]) {
		psession->kallsyms_name = strdup(kallsyms_str);
		if (!psession->kallsyms_name) {
			PyErr_NoMemory();
			goto err_out;
		}
	}

	if (!symfs_str)
		symfs_str = getenv("PERF_SYMBOL_SYMFS");
	if (symfs_str && symfs_str[0]) {
		psession->symfs = strdup(symfs_str);
		if (!psession->symfs) {
			PyErr_NoMemory();
			goto err_out;
		}
	}

	if (!itrace_str)
		itrace_str = getenv("PERF_ITRACE");
	if (itrace_str) {
		memset(&psession->itrace_opts, 0, sizeof(psession->itrace_opts));
		if (itrace_do_parse_synth_opts(&psession->itrace_opts, itrace_str, 0) < 0) {
			PyErr_SetString(PyExc_ValueError, "Failed to parse itrace options");
			goto err_out;
		}
	}

	if (pyrf_session__active) {
		PyErr_SetString(PyExc_RuntimeError,
				"Another perf.session is currently active");
		goto err_out;
	}

	symbol_conf.vmlinux_name = psession->vmlinux_name;
	symbol_conf.kallsyms_name = psession->kallsyms_name;
	symbol_conf.symfs = psession->symfs ?: "";
	symbol_conf.use_callchain = true;
	symbol_conf.show_kernel_path = true;
	symbol_conf.inline_name = false;

	/* Reading the header of a pipe may block, let other python threads run. */
	pyrf_session__active = psession;
	Py_BEGIN_ALLOW_THREADS
	session = perf_session__new(&pdata->data, &psession->tool);
	Py_END_ALLOW_THREADS
	pyrf_session__active = NULL;
	if (!psession->kallsyms_name && symbol_conf.kallsyms_name)
		psession->kallsyms_name = (char *)symbol_conf.kallsyms_name;
	if (IS_ERR(session)) {
		PyErr_Format(PyExc_IOError, "failed to create session: %ld", PTR_ERR(session));
		goto err_out;
	}
	psession->session = session;
	psession->session->itrace_synth_opts = &psession->itrace_opts;
	if (symbol__init(perf_session__env(session)) < 0) {
		PyErr_SetString(PyExc_OSError, "perf: symbol__init failed");
		goto err_out;
	}
	if (psession->symfs && !symbol_conf.symfs[0])
		zfree(&psession->symfs);

	return (PyObject *)psession;
err_out:
	Py_DECREF(psession);
	return NULL;
}

static void pyrf_session__delete(struct pyrf_session *psession)
{
	perf_session__delete(psession->session);
	if (!pyrf_session__active) {
		if (symbol_conf.vmlinux_name == psession->vmlinux_name)
			symbol_conf.vmlinux_name = NULL;
		if (symbol_conf.kallsyms_name == psession->kallsyms_name)
			symbol_conf.kallsyms_name = NULL;
		if (symbol_conf.symfs == psession->symfs)
			symbol_conf.symfs = "";
	}
	free(psession->vmlinux_name);
	free(psession->kallsyms_name);
	free(psession->symfs);
	free(psession->itrace_opts.vm_tm_corr_args);
	free(psession->itrace_opts.cpu_bitmap);
	free(psession->itrace_opts.ptime_range);
	Py_XDECREF(psession->pdata);
	Py_XDECREF(psession->sample);
	Py_XDECREF(psession->stat);
	Py_XDECREF(psession->context_switch);
	Py_XDECREF(psession->call_return);
	if (psession->crp)
		call_return_processor__free(psession->crp);
	Py_TYPE(psession)->tp_free((PyObject *)psession);
}

static PyObject *pyrf_session__find_thread_events(struct pyrf_session *psession)
{
	unsigned int stop_requests = pyrf_session__stop_requests;
	int err;

	CHECK_INITIALIZED(psession->session, "session");
	if (pyrf_session__active) {
		PyErr_SetString(PyExc_RuntimeError,
				"Another perf.session is currently active");
		return NULL;
	}

	symbol_conf.vmlinux_name = psession->vmlinux_name;
	symbol_conf.kallsyms_name = psession->kallsyms_name;
	symbol_conf.symfs = psession->symfs ?: "";

	psession->callback_raised = false;
	/* Callbacks that use python acquire the GIL, see pyrf_session_tool__sample(). */
	pyrf_session__active = psession;
	Py_BEGIN_ALLOW_THREADS
	err = perf_session__process_events(psession->session);
	Py_END_ALLOW_THREADS
	pyrf_session__active = NULL;
	if (psession->callback_raised) {
		/*
		 * Clear the early stop requested by this session's callback, in
		 * pyrf_session__callback_raised(), so that later sessions process
		 * all their events. That function is the only writer of
		 * session_done in the python module, so resetting it here after
		 * processing is sufficient and there is no need to also reset it
		 * before processing.
		 */
		session_done = 0;
	} else if (stop_requests != pyrf_session__stop_requests && !PyErr_Occurred()) {
		/*
		 * A callback of a session being processed concurrently, in
		 * another thread, raised and set session_done which may have
		 * stopped processing this session's events early. Raise an error
		 * rather than silently dropping events.
		 */
		PyErr_SetString(PyExc_RuntimeError,
				"Processing events may have stopped early as a callback of a concurrently processed session raised an exception");
		return NULL;
	}

	if (PyErr_Occurred())
		return NULL;

	if (err < 0) {
		PyErr_Format(PyExc_OSError, "Process events failed: %d", err);
		return NULL;
	}

	Py_RETURN_NONE;
}

static PyMethodDef pyrf_session__methods[] = {
	{
		.ml_name  = "process_events",
		.ml_meth  = (PyCFunction)pyrf_session__find_thread_events,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("Iterate and process events.")
	},
	{
		.ml_name  = "find_thread",
		.ml_meth  = (PyCFunction)pyrf_session__find_thread,
		.ml_flags = METH_VARARGS,
		.ml_doc	  = PyDoc_STR("Returns the thread associated with a pid.")
	},
	{ .ml_name = NULL, }
};

static const char pyrf_session__doc[] = PyDoc_STR("perf session object.");

static PyObject *pyrf_session__getattro(struct pyrf_session *psession, PyObject *attr_name)
{
	const char *name_str = PyUnicode_AsUTF8(attr_name);

	if (!name_str)
		return NULL;
	if (!psession->session) {
		PyErr_SetString(PyExc_ValueError, "session not initialized");
		return NULL;
	}
	if (!pyrf_session__check_not_busy(psession))
		return NULL;
	if (!strcmp(name_str, "e_machine"))
		return PyLong_FromLong(perf_session__e_machine(psession->session,
							       /*e_flags=*/NULL));

	if (!strcmp(name_str, "is_64_bit")) {
		/*
		 * Prefer the machine of the recorded threads over the perf_env,
		 * as in pipe mode the perf_env may not be populated yet and
		 * perf_env__kernel_is_64_bit would cache the host's bitness.
		 */
		switch (perf_session__e_machine(psession->session, /*e_flags=*/NULL)) {
		case EM_AARCH64:
		case EM_ALPHA:
		case EM_IA_64:
		case EM_LOONGARCH:
		case EM_PPC64:
		case EM_SPARCV9:
		case EM_X86_64:
			Py_RETURN_TRUE;
		case EM_386:
		case EM_ARM:
		case EM_PPC:
		case EM_SPARC:
			Py_RETURN_FALSE;
		default:
			/*
			 * Machines like EM_MIPS, EM_PARISC, EM_RISCV and
			 * EM_S390 are used for both word sizes, fall back on
			 * the recorded arch string if available.
			 */
			break;
		}
		{
			struct perf_env *env = perf_session__env(psession->session);

			if (env && env->arch)
				return PyBool_FromLong(perf_env__kernel_is_64_bit(env) == 1);
			return PyBool_FromLong(sizeof(void *) == 8);
		}
	}
	return PyObject_GenericGetAttr((PyObject *) psession, attr_name);
}

static int pyrf_session__setattro(struct pyrf_session *psession, PyObject *attr_name,
				  PyObject *value)
{
	if (!psession->session) {
		PyErr_SetString(PyExc_ValueError, "session not initialized");
		return -1;
	}
	if (!pyrf_session__check_not_busy(psession))
		return -1;
	return PyObject_GenericSetAttr((PyObject *) psession, attr_name, value);
}

static PyTypeObject pyrf_session__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.session",
	.tp_basicsize	= sizeof(struct pyrf_session),
	.tp_dealloc	= (destructor)pyrf_session__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_methods	= pyrf_session__methods,
	.tp_doc		= pyrf_session__doc,
	.tp_new		= pyrf_session__new,
	.tp_getattro	= (getattrofunc) pyrf_session__getattro,
	.tp_setattro	= (setattrofunc) pyrf_session__setattro,
};

static int pyrf_session__setup_types(void)
{
	return PyType_Ready(&pyrf_session__type);
}

static int pyrf__parse_elf_machine(PyObject *obj, int *elf_machine)
{
	long val;

	if (!obj || obj == Py_None) {
		*elf_machine = EM_HOST;
		return 0;
	}
	val = PyLong_AsLong(obj);
	if (val == -1 && PyErr_Occurred())
		return -1;
	if (val <= 0 || val > UINT16_MAX)
		*elf_machine = EM_HOST;
	else
		*elf_machine = (int)val;
	return 0;
}

static PyObject *pyrf__syscall_name(PyObject *self, PyObject *args, PyObject *kwargs)
{
	const char *name;
	int id;
	int elf_machine = EM_HOST;
	PyObject *elf_machine_obj = NULL;
	static char *kwlist[] = { "id", "elf_machine", NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|O", kwlist, &id, &elf_machine_obj))
		return NULL;
	if (pyrf__parse_elf_machine(elf_machine_obj, &elf_machine) < 0)
		return NULL;

	name = syscalltbl__name(elf_machine, id);
	if (!name)
		Py_RETURN_NONE;
	return PyUnicode_FromString(name);
}

static PyObject *pyrf__syscall_id(PyObject *self, PyObject *args, PyObject *kwargs)
{
	const char *name;
	int id;
	int elf_machine = EM_HOST;
	PyObject *elf_machine_obj = NULL;
	static char *kwlist[] = { "name", "elf_machine", NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|O", kwlist, &name, &elf_machine_obj))
		return NULL;
	if (pyrf__parse_elf_machine(elf_machine_obj, &elf_machine) < 0)
		return NULL;

	id = syscalltbl__id(elf_machine, name);
	if (id < 0) {
		PyErr_Format(PyExc_ValueError, "Failed to find syscall %s", name);
		return NULL;
	}
	return PyLong_FromLong(id);
}

static PyObject *pyrf__arch_strerrno(PyObject *self, PyObject *args, PyObject *kwargs)
{
	const char *name;
	int err;
	int elf_machine = EM_HOST;
	PyObject *elf_machine_obj = NULL;
	static char *kwlist[] = { "err", "elf_machine", NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|O", kwlist, &err, &elf_machine_obj))
		return NULL;
	if (pyrf__parse_elf_machine(elf_machine_obj, &elf_machine) < 0)
		return NULL;

	if (err == INT_MIN)
		Py_RETURN_NONE;
	if (err < 0)
		err = -err;

	name = perf_env__arch_strerrno((uint16_t)elf_machine, err);
	if (!name || !strcmp(name, "(unknown)"))
		Py_RETURN_NONE;
	return PyUnicode_FromString(name);
}

static PyObject *pyrf__config_get(PyObject *self, PyObject *args)
{
	const char *config_name, *val;

	if (!PyArg_ParseTuple(args, "s", &config_name))
		return NULL;

	val = perf_config_get(config_name);
	if (!val)
		Py_RETURN_NONE;
	return PyUnicode_FromString(val);
}

static PyMethodDef perf__methods[] = {
	{
		.ml_name  = "config_get",
		.ml_meth  = (PyCFunction) pyrf__config_get,
		.ml_flags = METH_VARARGS,
		.ml_doc	  = PyDoc_STR("Get a perf config value.")
	},
	{
		.ml_name  = "metrics",
		.ml_meth  = (PyCFunction) pyrf__metrics,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR(
			"Returns a list of metrics represented as string values in dictionaries.")
	},
	{
		.ml_name  = "tracepoint",
		.ml_meth  = (PyCFunction) pyrf__tracepoint,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("Get tracepoint config.")
	},
	{
		.ml_name  = "parse_events",
		.ml_meth  = (PyCFunction) pyrf__parse_events,
		.ml_flags = METH_VARARGS,
		.ml_doc	  = PyDoc_STR("Parse a string of events and return an evlist.")
	},
	{
		.ml_name  = "parse_metrics",
		.ml_meth  = (PyCFunction) pyrf__parse_metrics,
		.ml_flags = METH_VARARGS,
		.ml_doc	  = PyDoc_STR(
			"Parse a string of metrics or metric groups and return an evlist.")
	},
	{
		.ml_name  = "pmus",
		.ml_meth  = (PyCFunction) pyrf__pmus,
		.ml_flags = METH_NOARGS,
		.ml_doc	  = PyDoc_STR("Returns a sequence of pmus.")
	},
	{
		.ml_name  = "syscall_name",
		.ml_meth  = (PyCFunction) pyrf__syscall_name,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("Turns a syscall number to a string.")
	},
	{
		.ml_name  = "syscall_id",
		.ml_meth  = (PyCFunction) pyrf__syscall_id,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("Turns a syscall name to a number.")
	},
	{
		.ml_name  = "arch_strerrno",
		.ml_meth  = (PyCFunction) pyrf__arch_strerrno,
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
		.ml_doc	  = PyDoc_STR("Turns an errno number to a string.")
	},
	{ .ml_name = NULL, }
};

PyMODINIT_FUNC PyInit_perf(void)
{
	PyObject *obj;
	int i;
	PyObject *dict;
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

	if (module == NULL)
		return NULL;

	if (PyType_Ready(&pyrf_call_return__type) < 0 ||
	    pyrf_event__setup_types() < 0 ||
	    pyrf_evlist__setup_types() < 0 ||
	    pyrf_evsel__setup_types() < 0 ||
	    pyrf_thread_map__setup_types() < 0 ||
	    pyrf_cpu_map__setup_types() < 0 ||
	    pyrf_pmu_iterator__setup_types() < 0 ||
	    pyrf_pmu__setup_types() < 0 ||
	    pyrf_counts_values__setup_types() < 0 ||
	    pyrf_data__setup_types() < 0 ||
	    pyrf_session__setup_types() < 0 ||
	    pyrf_thread__setup_types() < 0) {
		Py_DECREF(module);
		return NULL;
	}

	/* The page_size is placed in util object. */
	page_size = sysconf(_SC_PAGE_SIZE);
	perf_debug_setup();

	Py_INCREF(&pyrf_evlist__type);
	PyModule_AddObject(module, "evlist", (PyObject *)&pyrf_evlist__type);

	Py_INCREF(&pyrf_evsel__type);
	PyModule_AddObject(module, "evsel", (PyObject *)&pyrf_evsel__type);

	Py_INCREF(&pyrf_thread__type);
	PyModule_AddObject(module, "thread", (PyObject *)&pyrf_thread__type);

	Py_INCREF(&pyrf_callchain__type);
	PyModule_AddObject(module, "callchain", (PyObject *)&pyrf_callchain__type);

	Py_INCREF(&pyrf_callchain_node__type);
	PyModule_AddObject(module, "callchain_node", (PyObject *)&pyrf_callchain_node__type);

	Py_INCREF(&pyrf_mmap_event__type);
	PyModule_AddObject(module, "mmap_event", (PyObject *)&pyrf_mmap_event__type);

	Py_INCREF(&pyrf_mmap2_event__type);
	PyModule_AddObject(module, "mmap2_event", (PyObject *)&pyrf_mmap2_event__type);

	Py_INCREF(&pyrf_lost_event__type);
	PyModule_AddObject(module, "lost_event", (PyObject *)&pyrf_lost_event__type);

	Py_INCREF(&pyrf_comm_event__type);
	PyModule_AddObject(module, "comm_event", (PyObject *)&pyrf_comm_event__type);

	Py_INCREF(&pyrf_task_event__type);
	PyModule_AddObject(module, "task_event", (PyObject *)&pyrf_task_event__type);

	Py_INCREF(&pyrf_throttle_event__type);
	PyModule_AddObject(module, "throttle_event", (PyObject *)&pyrf_throttle_event__type);

	Py_INCREF(&pyrf_read_event__type);
	PyModule_AddObject(module, "read_event", (PyObject *)&pyrf_read_event__type);

	Py_INCREF(&pyrf_sample_event__type);
	PyModule_AddObject(module, "sample_event", (PyObject *)&pyrf_sample_event__type);

	Py_INCREF(&pyrf_context_switch_event__type);
	PyModule_AddObject(module, "switch_event", (PyObject *)&pyrf_context_switch_event__type);

	Py_INCREF(&pyrf_stat_event__type);
	PyModule_AddObject(module, "stat_event", (PyObject *)&pyrf_stat_event__type);

	Py_INCREF(&pyrf_stat_round_event__type);
	PyModule_AddObject(module, "stat_round_event", (PyObject *)&pyrf_stat_round_event__type);

	Py_INCREF(&pyrf_thread_map__type);
	PyModule_AddObject(module, "thread_map", (PyObject*)&pyrf_thread_map__type);

	Py_INCREF(&pyrf_cpu_map__type);
	PyModule_AddObject(module, "cpu_map", (PyObject*)&pyrf_cpu_map__type);

	Py_INCREF(&pyrf_counts_values__type);
	PyModule_AddObject(module, "counts_values", (PyObject *)&pyrf_counts_values__type);

	Py_INCREF(&pyrf_data__type);
	PyModule_AddObject(module, "data", (PyObject *)&pyrf_data__type);

	Py_INCREF(&pyrf_session__type);
	PyModule_AddObject(module, "session", (PyObject *)&pyrf_session__type);
	Py_INCREF(&pyrf_call_return__type);
	PyModule_AddObject(module, "call_return", (PyObject *)&pyrf_call_return__type);

	Py_INCREF(&pyrf_branch_entry__type);
	if (PyModule_AddObject(module, "branch_entry", (PyObject *)&pyrf_branch_entry__type) < 0) {
		Py_DECREF(&pyrf_branch_entry__type);
		goto error;
	}

	Py_INCREF(&pyrf_branch_stack__type);
	if (PyModule_AddObject(module, "branch_stack", (PyObject *)&pyrf_branch_stack__type) < 0) {
		Py_DECREF(&pyrf_branch_stack__type);
		goto error;
	}

	dict = PyModule_GetDict(module);
	if (dict == NULL)
		goto error;

	for (i = 0; perf__constants[i].name != NULL; i++) {
		obj = PyLong_FromLong(perf__constants[i].value);
		if (obj == NULL)
			goto error;
		PyDict_SetItemString(dict, perf__constants[i].name, obj);
		Py_DECREF(obj);
	}

error:
	if (PyErr_Occurred()) {
		Py_XDECREF(module);
		return NULL;
	}
	return module;
}
