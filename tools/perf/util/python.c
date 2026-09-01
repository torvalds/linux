// SPDX-License-Identifier: GPL-2.0
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <inttypes.h>
#include <string.h>

#include <linux/err.h>
#include <poll.h>
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
#include "dso.h"
#include "dwarf-regs.h"
#include "event.h"
#include "branch.h"
#include "evlist.h"
#include "evsel.h"
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

#define member_def(type, member, ptype, help) \
	{ #member, ptype, \
	  offsetof(struct pyrf_event, event) + offsetof(struct type, member), \
	  0, help }

#define sample_member_def(name, member, ptype, help) \
	{ #name, ptype, \
	  offsetof(struct pyrf_event, sample) + offsetof(struct perf_sample, member), \
	  0, help }

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
	/** @sample: The parsed sample from the event. */
	struct perf_sample sample;
	/** @al: The address location from machine__resolve, lazily computed. */
	struct addr_location al;
	/** @al_resolved: True when machine__resolve been called. */
	bool al_resolved;
	/** @callchain: Resolved callchain, eagerly computed if requested. */
	PyObject *callchain;
	/** @brstack: Resolved branch stack, eagerly computed if requested. */
	PyObject *brstack;
	/** @event: The underlying perf_event that may be in a file or ring buffer. */
	union perf_event event;
};

#define sample_members \
	sample_member_def(sample_pid, pid, T_INT, "event pid"),			 \
	sample_member_def(sample_tid, tid, T_INT, "event tid"),			 \
	sample_member_def(sample_time, time, T_ULONGLONG, "event timestamp"),		 \
	sample_member_def(sample_id, id, T_ULONGLONG, "event id"),			 \
	sample_member_def(sample_stream_id, stream_id, T_ULONGLONG, "event stream id"), \
	sample_member_def(sample_period, period, T_ULONGLONG, "event period"),		 \
	sample_member_def(sample_cpu, cpu, T_UINT, "event cpu"),

static PyObject *pyrf_event__get_evsel(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (!pevent->sample.evsel)
		Py_RETURN_NONE;

	return pyrf_evsel__from_evsel(pevent->sample.evsel);
}

static PyGetSetDef pyrf_event__getset[] = {
	{
		.name = "evsel",
		.get = pyrf_event__get_evsel,
		.set = NULL,
		.doc = "tracking event.",
	},
	{ .name = NULL, },
};

static void pyrf_event__delete(struct pyrf_event *pevent)
{
	if (pevent->al_resolved)
		addr_location__exit(&pevent->al);
	Py_XDECREF(pevent->callchain);
	Py_XDECREF(pevent->brstack);
	perf_sample__exit(&pevent->sample);
	Py_TYPE(pevent)->tp_free((PyObject *)pevent);
}

static const char pyrf_mmap_event__doc[] = PyDoc_STR("perf mmap event object.");

static PyMemberDef pyrf_mmap_event__members[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_event_header, misc, T_USHORT, "event misc"),
	member_def(perf_record_mmap, pid, T_UINT, "event pid"),
	member_def(perf_record_mmap, tid, T_UINT, "event tid"),
	member_def(perf_record_mmap, start, T_ULONGLONG, "start of the map"),
	member_def(perf_record_mmap, len, T_ULONGLONG, "map length"),
	member_def(perf_record_mmap, pgoff, T_ULONGLONG, "page offset"),
	member_def(perf_record_mmap, filename, T_STRING_INPLACE, "backing store"),
	{ .name = NULL, },
};

static PyObject *pyrf_mmap_event__repr(const struct pyrf_event *pevent)
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
	.tp_members	= pyrf_mmap_event__members,
	.tp_getset	= pyrf_event__getset,
	.tp_repr	= (reprfunc)pyrf_mmap_event__repr,
};

static const char pyrf_mmap2_event__doc[] = PyDoc_STR("perf mmap2 event object.");

static PyObject *pyrf_mmap2_event__get_maj(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (pevent->event.header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLong(pevent->event.mmap2.maj);
}

static PyObject *pyrf_mmap2_event__get_min(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (pevent->event.header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLong(pevent->event.mmap2.min);
}

static PyObject *pyrf_mmap2_event__get_ino(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (pevent->event.header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLongLong(pevent->event.mmap2.ino);
}

static PyObject *pyrf_mmap2_event__get_ino_generation(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (pevent->event.header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID)
		Py_RETURN_NONE;

	return PyLong_FromUnsignedLongLong(pevent->event.mmap2.ino_generation);
}

static PyObject *pyrf_mmap2_event__get_build_id(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

	if (!(pevent->event.header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID))
		Py_RETURN_NONE;

	int size = pevent->event.mmap2.build_id_size;

	if (size > 20)
		size = 20;

	return PyBytes_FromStringAndSize((const char *)pevent->event.mmap2.build_id, size);
}

static PyGetSetDef pyrf_mmap2_event__getset[] = {
	{
		.name = "evsel",
		.get = pyrf_event__get_evsel,
		.set = NULL,
		.doc = "tracking event.",
	},
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

static PyMemberDef pyrf_mmap2_event__members[] = {
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
	member_def(perf_record_mmap2, filename, T_STRING_INPLACE, "backing store"),
	{ .name = NULL, },
};

static PyObject *pyrf_mmap2_event__repr(const struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;

	if (asprintf(&s, "{ type: mmap2, pid: %u, tid: %u, start: %#" PRI_lx64 ", length: %#" PRI_lx64 ", offset: %#" PRI_lx64 ", flags: %#x, prot: %#x, filename: %s }",
		     pevent->event.mmap2.pid, pevent->event.mmap2.tid,
		     pevent->event.mmap2.start, pevent->event.mmap2.len,
		     pevent->event.mmap2.pgoff, pevent->event.mmap2.flags,
		     pevent->event.mmap2.prot, pevent->event.mmap2.filename) < 0)
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
	.tp_members	= pyrf_mmap2_event__members,
	.tp_getset	= pyrf_mmap2_event__getset,
	.tp_repr	= (reprfunc)pyrf_mmap2_event__repr,
};

static const char pyrf_task_event__doc[] = PyDoc_STR("perf task (fork/exit) event object.");

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

static PyObject *pyrf_task_event__repr(const struct pyrf_event *pevent)
{
	return PyUnicode_FromFormat("{ type: %s, pid: %u, ppid: %u, tid: %u, "
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
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_task_event__doc,
	.tp_members	= pyrf_task_event__members,
	.tp_getset	= pyrf_event__getset,
	.tp_repr	= (reprfunc)pyrf_task_event__repr,
};

static const char pyrf_comm_event__doc[] = PyDoc_STR("perf comm event object.");

static PyMemberDef pyrf_comm_event__members[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_comm, pid, T_UINT, "event pid"),
	member_def(perf_record_comm, tid, T_UINT, "event tid"),
	member_def(perf_record_comm, comm, T_STRING_INPLACE, "process name"),
	{ .name = NULL, },
};

static PyObject *pyrf_comm_event__repr(const struct pyrf_event *pevent)
{
	return PyUnicode_FromFormat("{ type: comm, pid: %u, tid: %u, comm: %s }",
				   pevent->event.comm.pid,
				   pevent->event.comm.tid,
				   pevent->event.comm.comm);
}

static PyTypeObject pyrf_comm_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.comm_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_comm_event__doc,
	.tp_members	= pyrf_comm_event__members,
	.tp_getset	= pyrf_event__getset,
	.tp_repr	= (reprfunc)pyrf_comm_event__repr,
};

static const char pyrf_throttle_event__doc[] = PyDoc_STR("perf throttle event object.");

static PyMemberDef pyrf_throttle_event__members[] = {
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
		(&pevent->event.header + 1);

	return PyUnicode_FromFormat("{ type: %sthrottle, time: %" PRI_lu64 ", id: %" PRI_lu64
				   ", stream_id: %" PRI_lu64 " }",
				   pevent->event.header.type == PERF_RECORD_THROTTLE ? "" : "un",
				   te->time, te->id, te->stream_id);
}

static PyTypeObject pyrf_throttle_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.throttle_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_throttle_event__doc,
	.tp_members	= pyrf_throttle_event__members,
	.tp_getset	= pyrf_event__getset,
	.tp_repr	= (reprfunc)pyrf_throttle_event__repr,
};

static const char pyrf_lost_event__doc[] = PyDoc_STR("perf lost event object.");

static PyMemberDef pyrf_lost_event__members[] = {
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
		     pevent->event.lost.id, pevent->event.lost.lost) < 0) {
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
	.tp_members	= pyrf_lost_event__members,
	.tp_getset	= pyrf_event__getset,
	.tp_repr	= (reprfunc)pyrf_lost_event__repr,
};

static const char pyrf_stat_event__doc[] = PyDoc_STR("perf stat event object.");

static PyMemberDef pyrf_stat_event__members[] = {
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
	return PyUnicode_FromFormat(
		"{ type: stat, id: %llu, cpu: %u, thread: %u, val: %llu, ena: %llu, run: %llu }",
		pevent->event.stat.id,
		pevent->event.stat.cpu,
		pevent->event.stat.thread,
		pevent->event.stat.val,
		pevent->event.stat.ena,
		pevent->event.stat.run);
}

static PyTypeObject pyrf_stat_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.stat_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_new		= PyType_GenericNew,
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_stat_event__doc,
	.tp_members	= pyrf_stat_event__members,
	.tp_getset	= pyrf_event__getset,
	.tp_repr	= (reprfunc)pyrf_stat_event__repr,
};

static const char pyrf_stat_round_event__doc[] = PyDoc_STR("perf stat round event object.");

static PyMemberDef pyrf_stat_round_event__members[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	{ .name = "stat_round_type", .type = T_ULONGLONG,
	  .offset = offsetof(struct pyrf_event, event) + offsetof(struct perf_record_stat_round, type),
	  .doc = "round type" },
	member_def(perf_record_stat_round, time, T_ULONGLONG, "round time"),
	{ .name = NULL, },
};

static PyObject *pyrf_stat_round_event__repr(const struct pyrf_event *pevent)
{
	return PyUnicode_FromFormat("{ type: stat_round, type: %llu, time: %llu }",
				   pevent->event.stat_round.type,
				   pevent->event.stat_round.time);
}

static PyTypeObject pyrf_stat_round_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.stat_round_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_new		= PyType_GenericNew,
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_stat_round_event__doc,
	.tp_members	= pyrf_stat_round_event__members,
	.tp_getset	= pyrf_event__getset,
	.tp_repr	= (reprfunc)pyrf_stat_round_event__repr,
};

static const char pyrf_read_event__doc[] = PyDoc_STR("perf read event object.");

static PyMemberDef pyrf_read_event__members[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_read, pid, T_UINT, "event pid"),
	member_def(perf_record_read, tid, T_UINT, "event tid"),
	{ .name = NULL, },
};

static PyObject *pyrf_read_event__repr(const struct pyrf_event *pevent)
{
	return PyUnicode_FromFormat("{ type: read, pid: %u, tid: %u }",
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
	.tp_dealloc	= (destructor)pyrf_event__delete,
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_read_event__doc,
	.tp_members	= pyrf_read_event__members,
	.tp_getset	= pyrf_event__getset,
	.tp_repr	= (reprfunc)pyrf_read_event__repr,
};

static const char pyrf_sample_event__doc[] = PyDoc_STR("perf sample event object.");

static PyMemberDef pyrf_sample_event__members[] = {
	sample_members
	sample_member_def(sample_ip, ip, T_ULONGLONG, "event ip"),
	sample_member_def(sample_addr, addr, T_ULONGLONG, "event addr"),
	sample_member_def(sample_phys_addr, phys_addr, T_ULONGLONG, "event physical addr"),
	sample_member_def(sample_weight, weight, T_ULONGLONG, "event weight"),
	sample_member_def(sample_data_src, data_src, T_ULONGLONG, "event data source"),
	sample_member_def(sample_insn_count, insn_cnt, T_ULONGLONG, "event instruction count"),
	sample_member_def(sample_cyc_count, cyc_cnt, T_ULONGLONG, "event cycle count"),
	member_def(perf_event_header, type, T_UINT, "event type"),
	{ .name = NULL, },
};

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
	if (!pevent->sample.evsel)
		return false;
	return pevent->sample.evsel->core.attr.type == PERF_TYPE_TRACEPOINT;
}

static PyObject*
tracepoint_field(const struct pyrf_event *pe, struct tep_format_field *field)
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
	struct evsel *evsel = pevent->sample.evsel;
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
	struct evsel *evsel = pevent->sample.evsel;
	struct evlist *evlist = evsel ? evsel->evlist : NULL;
	struct perf_session *session = evlist ? evlist__session(evlist) : NULL;

	if (pevent->al_resolved)
		return 0;

	if (!session)
		return -1;

	addr_location__init(&pevent->al);
	if (machine__resolve(&session->machines.host, &pevent->al, &pevent->sample) < 0) {
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
	if (pevent->event.header.type != PERF_RECORD_SAMPLE)
		Py_RETURN_NONE;

	return PyBytes_FromStringAndSize((const char *)pevent->sample.raw_data,
					 pevent->sample.raw_size);
}

static PyObject *pyrf_sample_event__srccode(PyObject *self, PyObject *args)
{
	struct pyrf_event *pevent = (void *)self;
	u64 addr = pevent->sample.ip;
	char *srcfile = NULL;
	char *srccode = NULL;
	unsigned int line = 0;
	int len = 0;
	PyObject *result;
	struct addr_location al;

	if (!PyArg_ParseTuple(args, "|K", &addr))
		return NULL;

	if (pyrf_sample_event__resolve_al(pevent) < 0)
		Py_RETURN_NONE;

	if (addr != pevent->sample.ip) {
		addr_location__init(&al);
		thread__find_symbol_fb(pevent->al.thread, pevent->sample.cpumode, addr, &al);
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
	struct thread *thread;
	struct machine *machine;

	if (pyrf_sample_event__resolve_al(pevent) < 0)
		Py_RETURN_NONE;

	thread = pevent->al.thread;

	if (!thread || !thread__maps(thread))
		Py_RETURN_NONE;

	machine = maps__machine(thread__maps(thread));
	if (!machine)
		Py_RETURN_NONE;

	if (pevent->sample.ip && !pevent->sample.insn_len)
		perf_sample__fetch_insn(&pevent->sample, thread, machine);

	if (!pevent->sample.insn_len)
		Py_RETURN_NONE;

	return PyBytes_FromStringAndSize((const char *)pevent->sample.insn,
					 pevent->sample.insn_len);
}

struct pyrf_callchain_node {
	PyObject_HEAD
	u64 ip;
	struct map *map;
	struct symbol *sym;
};

static void pyrf_callchain_node__delete(struct pyrf_callchain_node *pnode)
{
	map__put(pnode->map);
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

static PyGetSetDef pyrf_callchain_node__getset[] = {
	{ .name = "ip",     .get = (getter)pyrf_callchain_node__get_ip, },
	{ .name = "symbol", .get = (getter)pyrf_callchain_node__get_symbol, },
	{ .name = "dso",    .get = (getter)pyrf_callchain_node__get_dso, },
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
};

struct pyrf_callchain {
	PyObject_HEAD
	struct pyrf_callchain_frame *frames;
	u64 nr_frames;
};

static void pyrf_callchain__delete(struct pyrf_callchain *pchain)
{
	if (pchain->frames) {
		for (u64 i = 0; i < pchain->nr_frames; i++)
			map__put(pchain->frames[i].map);
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

static PyObject *pyrf_sample_event__get_callchain(PyObject *self, void *closure __maybe_unused)
{
	struct pyrf_event *pevent = (void *)self;

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

static PyGetSetDef pyrf_sample_event__getset[] = {
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
		.name = "evsel",
		.get = pyrf_event__get_evsel,
		.set = NULL,
		.doc = "tracking event.",
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
	.tp_members	= pyrf_sample_event__members,
	.tp_getset	= pyrf_sample_event__getset,
	.tp_methods	= pyrf_sample_event__methods,
	.tp_repr	= (reprfunc)pyrf_sample_event__repr,
	.tp_getattro	= (getattrofunc) pyrf_sample_event__getattro,
};

static const char pyrf_context_switch_event__doc[] = PyDoc_STR("perf context_switch event object.");

static PyMemberDef pyrf_context_switch_event__members[] = {
	sample_members
	member_def(perf_event_header, type, T_UINT, "event type"),
	member_def(perf_record_switch, next_prev_pid, T_UINT, "next/prev pid"),
	member_def(perf_record_switch, next_prev_tid, T_UINT, "next/prev tid"),
	{ .name = NULL, },
};

static PyObject *pyrf_context_switch_event__repr(const struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;

	if (asprintf(&s, "{ type: context_switch, next_prev_pid: %u, next_prev_tid: %u, switch_out: %u }",
		     pevent->event.context_switch.next_prev_pid,
		     pevent->event.context_switch.next_prev_tid,
		     !!(pevent->event.header.misc & PERF_RECORD_MISC_SWITCH_OUT)) < 0) {
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
	.tp_members	= pyrf_context_switch_event__members,
	.tp_getset	= pyrf_event__getset,
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

static PyObject *pyrf_event__new(const union perf_event *event, struct evsel *evsel,
				 struct perf_session *session,
				 struct machine *machine)
{
	struct pyrf_event *pevent;
	struct perf_sample *sample;
	int err;
	u32 min_size;
	bool needs_swap;

	if (!machine)
		machine = session ? &session->machines.host : NULL;

	if (event->header.type >= ARRAY_SIZE(pyrf_event__type) ||
	    pyrf_event__type[event->header.type] == NULL) {
		return PyErr_Format(PyExc_TypeError, "Unexpected header type %u",
			     event->header.type);
	}

	if (perf_event__too_small(event, &min_size)) {
		return PyErr_Format(PyExc_ValueError, "Event size %u too small for type %u",
				    event->header.size, event->header.type);
	}

	size_t copy_size = event->header.size;

	if (copy_size > sizeof(pevent->event)) {
		return PyErr_Format(PyExc_TypeError, "Unexpected event size: %zd < %zu",
				    sizeof(pevent->event), copy_size);
	}

	pevent = PyObject_New(struct pyrf_event, pyrf_event__type[event->header.type]);
	if (pevent == NULL)
		return PyErr_NoMemory();

	/* Copy the event for memory safety and initialize variables. */
	memcpy(&pevent->event, event, copy_size);
	if (copy_size < sizeof(pevent->event))
		memset((char *)&pevent->event + copy_size, 0, sizeof(pevent->event) - copy_size);

	if (event->header.type == PERF_RECORD_MMAP2)
		pevent->event.mmap2.filename[sizeof(pevent->event.mmap2.filename) - 1] = '\0';

	perf_sample__init(&pevent->sample, /*all=*/true);
	pevent->callchain = NULL;
	pevent->brstack = NULL;
	pevent->al_resolved = false;
	addr_location__init(&pevent->al);

	if (!evsel)
		return (PyObject *)pevent;

	/* Parse the sample again so that pointers are within the copied event. */
	needs_swap = evsel->needs_swap;

	evsel->needs_swap = false;
	err = evsel__parse_sample(evsel, &pevent->event, &pevent->sample);
	evsel->needs_swap = needs_swap;
	if (err < 0) {
		Py_DECREF(pevent);
		return PyErr_Format(PyExc_OSError,
				    "perf: can't parse sample, err=%d", err);
	}
	sample = &pevent->sample;
	if (machine && sample->callchain) {
		struct addr_location al;
		struct callchain_cursor *cursor;
		u64 i;
		struct pyrf_callchain *pchain;

		addr_location__init(&al);
		if (machine__resolve(machine, &al, sample) >= 0) {
			cursor = get_tls_callchain_cursor();
			if (thread__resolve_callchain(al.thread, cursor, sample,
						      NULL, NULL, PERF_MAX_STACK_DEPTH) == 0) {
				callchain_cursor_commit(cursor);

				pchain = PyObject_New(struct pyrf_callchain, &pyrf_callchain__type);
				if (!pchain) {
					addr_location__exit(&al);
					Py_DECREF(pevent);
					return NULL;
				}
				pchain->nr_frames = cursor->nr;
				pchain->frames = calloc(pchain->nr_frames,
							sizeof(*pchain->frames));
				if (!pchain->frames) {
					Py_DECREF(pchain);
					addr_location__exit(&al);
					Py_DECREF(pevent);
					return PyErr_NoMemory();
				}
				struct callchain_cursor_node *node;

				for (i = 0; i < pchain->nr_frames; i++) {
					node = callchain_cursor_current(cursor);
					pchain->frames[i].ip = node->ip;
					pchain->frames[i].map =
						map__get(node->ms.map);
					pchain->frames[i].sym = node->ms.sym;
					callchain_cursor_advance(cursor);
				}
				pevent->callchain = (PyObject *)pchain;
			}
			addr_location__exit(&al);
		}
	}
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
	struct perf_counts_values *old_count, *new_count;
	struct pyrf_counts_values *count_values;

	CHECK_INITIALIZED(evsel, "evsel");

	if (!PyArg_ParseTuple(args, "ii", &cpu, &thread))
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
		char *n = strdup(evsel__metric_id(cur));

		if (!n)
			return -ENOMEM;

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
		ret = expr__add_id_val_source_count(pctx, n, val, source_count);
		if (ret)
			return ret;
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
	struct rb_node *node;
	struct metric_expr *mexp = NULL;
	struct expr_parse_ctx *pctx;
	double result = 0;
	struct evsel *metric_evsel = NULL;

	CHECK_INITIALIZED(pevlist->evlist, "evlist");

	if (!PyArg_ParseTuple(args, "sii", &metric, &cpu, &thread))
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


static PyObject *pyrf_evlist__add(struct pyrf_evlist *pevlist,
				  PyObject *args,
				  PyObject *kwargs __maybe_unused)
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
	pyevent = pyrf_event__new(event, evsel, evlist__session(evlist), /*machine=*/NULL);
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
		.ml_flags = METH_VARARGS | METH_KEYWORDS,
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
	{ .name = NULL, },
};

static PyObject *pyrf__tracepoint(struct pyrf_evsel *pevsel,
				  PyObject *args, PyObject *kwargs)
{
	static char *kwlist[] = { "sys", "name", NULL };
	char *sys  = NULL;
	char *name = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ss", kwlist,
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
		PyErr_SetFromErrno(PyExc_OSError);
		evlist__put(evlist);
		return NULL;
	}
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
	const char *str;

	CHECK_INITIALIZED(pthread->thread, "perf.thread");

	str = thread__comm_str(pthread->thread);

	if (!str)
		Py_RETURN_NONE;

	return PyUnicode_FromString(str);
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
	struct pyrf_data *pdata;
	PyObject *sample;
	PyObject *stat;
};

static int pyrf_session_tool__sample(const struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct machine *machine)
{
	struct pyrf_session *psession = container_of(tool, struct pyrf_session, tool);
	PyObject *pyevent = pyrf_event__new(event, sample->evsel, psession->session, machine);
	PyObject *ret;

	if (pyevent == NULL)
		return -ENOMEM;

	ret = PyObject_CallFunction(psession->sample, "O", pyevent);
	if (!ret) {
		Py_DECREF(pyevent);
		return -1;
	}
	Py_DECREF(ret);
	Py_DECREF(pyevent);
	return 0;
}

static int pyrf_session_tool__stat(const struct perf_tool *tool,
				   struct perf_session *session,
				   union perf_event *event)
{
	struct pyrf_session *psession = container_of(tool, struct pyrf_session, tool);
	struct evsel *evsel = evlist__id2evsel(session->evlist, event->stat.id);
	PyObject *pyevent = pyrf_event__new(event, /*evsel=*/NULL, psession->session,
					    /*machine=*/NULL);
	const char *name = evsel ? evsel__name(evsel) : "unknown";
	PyObject *ret;

	if (pyevent == NULL)
		return -ENOMEM;

	ret = PyObject_CallFunction(psession->stat, "Oz", pyevent, name);
	if (!ret) {
		Py_DECREF(pyevent);
		return -1;
	}
	Py_DECREF(ret);
	Py_DECREF(pyevent);
	return 0;
}

static int pyrf_session_tool__stat_round(const struct perf_tool *tool,
					 struct perf_session *session __maybe_unused,
					 union perf_event *event)
{
	struct pyrf_session *psession = container_of(tool, struct pyrf_session, tool);
	PyObject *pyevent = pyrf_event__new(event, /*evsel=*/NULL, psession->session,
					    /*machine=*/NULL);
	PyObject *ret;

	if (pyevent == NULL)
		return -ENOMEM;

	ret = PyObject_CallFunction(psession->stat, "Oz", pyevent, NULL);
	if (!ret) {
		Py_DECREF(pyevent);
		return -1;
	}
	Py_DECREF(ret);
	Py_DECREF(pyevent);
	return 0;
}

static PyObject *pyrf_session__find_thread(struct pyrf_session *psession, PyObject *args)
{
	struct machine *machine;
	struct thread *thread = NULL;
	PyObject *result;
	int pid;

	CHECK_INITIALIZED(psession->session, "session");

	if (!PyArg_ParseTuple(args, "i", &pid))
		return NULL;

	machine = &psession->session->machines.host;
	thread = machine__find_thread(machine, pid, pid);

	if (!thread) {
		machine = perf_session__find_machine(psession->session, pid);
		if (machine)
			thread = machine__find_thread(machine, pid, pid);
	}

	if (!thread) {
		PyErr_Format(PyExc_TypeError, "Failed to find thread %d", pid);
		return NULL;
	}
	result = pyrf_thread__from_thread(thread);
	thread__put(thread);
	return result;
}

static PyObject *pyrf_session__new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
	struct pyrf_data *pdata;
	PyObject *sample = NULL, *stat = NULL;
	static char *kwlist[] = { "data", "sample", "stat", NULL };
	struct pyrf_session *psession;
	struct perf_session *session;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!|OO", kwlist, &pyrf_data__type, &pdata,
					 &sample, &stat))
		return NULL;

	psession = PyObject_New(struct pyrf_session, type);
	if (!psession)
		return NULL;

	psession->session = NULL;
	psession->sample = NULL;
	psession->stat = NULL;
	psession->pdata = NULL;

	Py_INCREF(pdata);
	psession->pdata = pdata;

	perf_tool__init(&psession->tool, /*ordered_events=*/true);
	psession->tool.ordering_requires_timestamps = true;

	#define ADD_TOOL(name)						\
	do {								\
		if (name) {						\
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
	ADD_TOOL(stat);
	#undef ADD_TOOL

	if (stat)
		psession->tool.stat_round = pyrf_session_tool__stat_round;


	psession->tool.comm		= perf_event__process_comm;
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
	psession->tool.feature		= perf_event__process_feature;

	session = perf_session__new(&pdata->data, &psession->tool);
	if (IS_ERR(session)) {
		PyErr_Format(PyExc_IOError, "failed to create session: %ld", PTR_ERR(session));
		goto err_out;
	}
	psession->session = session;

	symbol_conf.use_callchain = true;
	symbol_conf.show_kernel_path = true;
	symbol_conf.inline_name = false;
	if (symbol__init(perf_session__env(session)) < 0) {
		PyErr_SetString(PyExc_OSError, "perf: symbol__init failed");
		goto err_out;
	}



	return (PyObject *)psession;
err_out:
	Py_DECREF(psession);
	return NULL;
}

static void pyrf_session__delete(struct pyrf_session *psession)
{
	perf_session__delete(psession->session);
	Py_XDECREF(psession->pdata);
	Py_XDECREF(psession->sample);
	Py_XDECREF(psession->stat);
	Py_TYPE(psession)->tp_free((PyObject *)psession);
}

static PyObject *pyrf_session__find_thread_events(struct pyrf_session *psession)
{
	int err;

	CHECK_INITIALIZED(psession->session, "session");

	err = perf_session__process_events(psession->session);

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
	if (!psession->session) {
		PyErr_SetString(PyExc_ValueError, "session not initialized");
		return NULL;
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

static PyObject *pyrf__syscall_name(PyObject *self, PyObject *args, PyObject *kwargs)
{
	const char *name;
	int id;
	int elf_machine = EM_HOST;
	static char *kwlist[] = { "id", "elf_machine", NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|$i", kwlist, &id, &elf_machine))
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
	static char *kwlist[] = { "name", "elf_machine", NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|$i", kwlist, &name, &elf_machine))
		return NULL;

	id = syscalltbl__id(elf_machine, name);
	if (id < 0) {
		PyErr_Format(PyExc_ValueError, "Failed to find syscall %s", name);
		return NULL;
	}
	return PyLong_FromLong(id);
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

	if (pyrf_event__setup_types() < 0 ||
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

	Py_INCREF(&pyrf_task_event__type);
	PyModule_AddObject(module, "task_event", (PyObject *)&pyrf_task_event__type);

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
