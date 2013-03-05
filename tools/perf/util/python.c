#include <Python.h>
#include <structmember.h>
#include <inttypes.h>
#include <poll.h>
#include "evlist.h"
#include "evsel.h"
#include "event.h"
#include "cpumap.h"
#include "thread_map.h"

/* Define PyVarObject_HEAD_INIT for python 2.5 */
#ifndef PyVarObject_HEAD_INIT
# define PyVarObject_HEAD_INIT(type, size) PyObject_HEAD_INIT(type) size,
#endif

struct throttle_event {
	struct perf_event_header header;
	u64			 time;
	u64			 id;
	u64			 stream_id;
};

PyMODINIT_FUNC initperf(void);

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
	member_def(mmap_event, pid, T_UINT, "event pid"),
	member_def(mmap_event, tid, T_UINT, "event tid"),
	member_def(mmap_event, start, T_ULONGLONG, "start of the map"),
	member_def(mmap_event, len, T_ULONGLONG, "map length"),
	member_def(mmap_event, pgoff, T_ULONGLONG, "page offset"),
	member_def(mmap_event, filename, T_STRING_INPLACE, "backing store"),
	{ .name = NULL, },
};

static PyObject *pyrf_mmap_event__repr(struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;

	if (asprintf(&s, "{ type: mmap, pid: %u, tid: %u, start: %#" PRIx64 ", "
			 "length: %#" PRIx64 ", offset: %#" PRIx64 ", "
			 "filename: %s }",
		     pevent->event.mmap.pid, pevent->event.mmap.tid,
		     pevent->event.mmap.start, pevent->event.mmap.len,
		     pevent->event.mmap.pgoff, pevent->event.mmap.filename) < 0) {
		ret = PyErr_NoMemory();
	} else {
		ret = PyString_FromString(s);
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
	member_def(fork_event, pid, T_UINT, "event pid"),
	member_def(fork_event, ppid, T_UINT, "event ppid"),
	member_def(fork_event, tid, T_UINT, "event tid"),
	member_def(fork_event, ptid, T_UINT, "event ptid"),
	member_def(fork_event, time, T_ULONGLONG, "timestamp"),
	{ .name = NULL, },
};

static PyObject *pyrf_task_event__repr(struct pyrf_event *pevent)
{
	return PyString_FromFormat("{ type: %s, pid: %u, ppid: %u, tid: %u, "
				   "ptid: %u, time: %" PRIu64 "}",
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
	member_def(comm_event, pid, T_UINT, "event pid"),
	member_def(comm_event, tid, T_UINT, "event tid"),
	member_def(comm_event, comm, T_STRING_INPLACE, "process name"),
	{ .name = NULL, },
};

static PyObject *pyrf_comm_event__repr(struct pyrf_event *pevent)
{
	return PyString_FromFormat("{ type: comm, pid: %u, tid: %u, comm: %s }",
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
	member_def(throttle_event, time, T_ULONGLONG, "timestamp"),
	member_def(throttle_event, id, T_ULONGLONG, "event id"),
	member_def(throttle_event, stream_id, T_ULONGLONG, "event stream id"),
	{ .name = NULL, },
};

static PyObject *pyrf_throttle_event__repr(struct pyrf_event *pevent)
{
	struct throttle_event *te = (struct throttle_event *)(&pevent->event.header + 1);

	return PyString_FromFormat("{ type: %sthrottle, time: %" PRIu64 ", id: %" PRIu64
				   ", stream_id: %" PRIu64 " }",
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
	member_def(lost_event, id, T_ULONGLONG, "event id"),
	member_def(lost_event, lost, T_ULONGLONG, "number of lost events"),
	{ .name = NULL, },
};

static PyObject *pyrf_lost_event__repr(struct pyrf_event *pevent)
{
	PyObject *ret;
	char *s;

	if (asprintf(&s, "{ type: lost, id: %#" PRIx64 ", "
			 "lost: %#" PRIx64 " }",
		     pevent->event.lost.id, pevent->event.lost.lost) < 0) {
		ret = PyErr_NoMemory();
	} else {
		ret = PyString_FromString(s);
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
	member_def(read_event, pid, T_UINT, "event pid"),
	member_def(read_event, tid, T_UINT, "event tid"),
	{ .name = NULL, },
};

static PyObject *pyrf_read_event__repr(struct pyrf_event *pevent)
{
	return PyString_FromFormat("{ type: read, pid: %u, tid: %u }",
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
		ret = PyString_FromString(s);
		free(s);
	}
	return ret;
}

static PyTypeObject pyrf_sample_event__type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name	= "perf.sample_event",
	.tp_basicsize	= sizeof(struct pyrf_event),
	.tp_flags	= Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	.tp_doc		= pyrf_sample_event__doc,
	.tp_members	= pyrf_sample_event__members,
	.tp_repr	= (reprfunc)pyrf_sample_event__repr,
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
};

static PyObject *pyrf_event__new(union perf_event *event)
{
	struct pyrf_event *pevent;
	PyTypeObject *ptype;

	if (event->header.type < PERF_RECORD_MMAP ||
	    event->header.type > PERF_RECORD_SAMPLE)
		return NULL;

	ptype = pyrf_event__type[event->header.type];
	pevent = PyObject_New(struct pyrf_event, ptype);
	if (pevent != NULL)
		memcpy(&pevent->event, event, event->header.size);
	return (PyObject *)pevent;
}

struct pyrf_cpu_map {
	PyObject_HEAD

	struct cpu_map *cpus;
};

static int pyrf_cpu_map__init(struct pyrf_cpu_map *pcpus,
			      PyObject *args, PyObject *kwargs)
{
	static char *kwlist[] = { "cpustr", NULL };
	char *cpustr = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|s",
					 kwlist, &cpustr))
		return -1;

	pcpus->cpus = cpu_map__new(cpustr);
	if (pcpus->cpus == NULL)
		return -1;
	return 0;
}

static void pyrf_cpu_map__delete(struct pyrf_cpu_map *pcpus)
{
	cpu_map__delete(pcpus->cpus);
	pcpus->ob_type->tp_free((PyObject*)pcpus);
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

	struct thread_map *threads;
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
	thread_map__delete(pthreads->threads);
	pthreads->ob_type->tp_free((PyObject*)pthreads);
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

	struct perf_evsel evsel;
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
					 "|iKiKKiiiiiiiiiiiiiiiiiiiiiKK", kwlist,
					 &attr.type, &attr.config, &attr.sample_freq,
					 &sample_period, &attr.sample_type,
					 &attr.read_format, &disabled, &inherit,
					 &pinned, &exclusive, &exclude_user,
					 &exclude_kernel, &exclude_hv, &exclude_idle,
					 &mmap, &comm, &freq, &inherit_stat,
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
	attr.comm	    = comm;
	attr.freq	    = freq;
	attr.inherit_stat   = inherit_stat;
	attr.enable_on_exec = enable_on_exec;
	attr.task	    = task;
	attr.watermark	    = watermark;
	attr.precise_ip	    = precise_ip;
	attr.mmap_data	    = mmap_data;
	attr.sample_id_all  = sample_id_all;

	perf_evsel__init(&pevsel->evsel, &attr, idx);
	return 0;
}

static void pyrf_evsel__delete(struct pyrf_evsel *pevsel)
{
	perf_evsel__exit(&pevsel->evsel);
	pevsel->ob_type->tp_free((PyObject*)pevsel);
}

static PyObject *pyrf_evsel__open(struct pyrf_evsel *pevsel,
				  PyObject *args, PyObject *kwargs)
{
	struct perf_evsel *evsel = &pevsel->evsel;
	struct cpu_map *cpus = NULL;
	struct thread_map *threads = NULL;
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

	evsel->attr.inherit = inherit;
	/*
	 * This will group just the fds for this single evsel, to group
	 * multiple events, use evlist.open().
	 */
	if (perf_evsel__open(evsel, cpus, threads) < 0) {
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

	struct perf_evlist evlist;
};

static int pyrf_evlist__init(struct pyrf_evlist *pevlist,
			     PyObject *args, PyObject *kwargs __maybe_unused)
{
	PyObject *pcpus = NULL, *pthreads = NULL;
	struct cpu_map *cpus;
	struct thread_map *threads;

	if (!PyArg_ParseTuple(args, "OO", &pcpus, &pthreads))
		return -1;

	threads = ((struct pyrf_thread_map *)pthreads)->threads;
	cpus = ((struct pyrf_cpu_map *)pcpus)->cpus;
	perf_evlist__init(&pevlist->evlist, cpus, threads);
	return 0;
}

static void pyrf_evlist__delete(struct pyrf_evlist *pevlist)
{
	perf_evlist__exit(&pevlist->evlist);
	pevlist->ob_type->tp_free((PyObject*)pevlist);
}

static PyObject *pyrf_evlist__mmap(struct pyrf_evlist *pevlist,
				   PyObject *args, PyObject *kwargs)
{
	struct perf_evlist *evlist = &pevlist->evlist;
	static char *kwlist[] = { "pages", "overwrite", NULL };
	int pages = 128, overwrite = false;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ii", kwlist,
					 &pages, &overwrite))
		return NULL;

	if (perf_evlist__mmap(evlist, pages, overwrite) < 0) {
		PyErr_SetFromErrno(PyExc_OSError);
		return NULL;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *pyrf_evlist__poll(struct pyrf_evlist *pevlist,
				   PyObject *args, PyObject *kwargs)
{
	struct perf_evlist *evlist = &pevlist->evlist;
	static char *kwlist[] = { "timeout", NULL };
	int timeout = -1, n;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i", kwlist, &timeout))
		return NULL;

	n = poll(evlist->pollfd, evlist->nr_fds, timeout);
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
	struct perf_evlist *evlist = &pevlist->evlist;
        PyObject *list = PyList_New(0);
	int i;

	for (i = 0; i < evlist->nr_fds; ++i) {
		PyObject *file;
		FILE *fp = fdopen(evlist->pollfd[i].fd, "r");

		if (fp == NULL)
			goto free_list;

		file = PyFile_FromFile(fp, "perf", "r", NULL);
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
	struct perf_evlist *evlist = &pevlist->evlist;
	PyObject *pevsel;
	struct perf_evsel *evsel;

	if (!PyArg_ParseTuple(args, "O", &pevsel))
		return NULL;

	Py_INCREF(pevsel);
	evsel = &((struct pyrf_evsel *)pevsel)->evsel;
	evsel->idx = evlist->nr_entries;
	perf_evlist__add(evlist, evsel);

	return Py_BuildValue("i", evlist->nr_entries);
}

static PyObject *pyrf_evlist__read_on_cpu(struct pyrf_evlist *pevlist,
					  PyObject *args, PyObject *kwargs)
{
	struct perf_evlist *evlist = &pevlist->evlist;
	union perf_event *event;
	int sample_id_all = 1, cpu;
	static char *kwlist[] = { "cpu", "sample_id_all", NULL };
	int err;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|i", kwlist,
					 &cpu, &sample_id_all))
		return NULL;

	event = perf_evlist__mmap_read(evlist, cpu);
	if (event != NULL) {
		PyObject *pyevent = pyrf_event__new(event);
		struct pyrf_event *pevent = (struct pyrf_event *)pyevent;

		if (pyevent == NULL)
			return PyErr_NoMemory();

		err = perf_evlist__parse_sample(evlist, event, &pevent->sample);
		if (err)
			return PyErr_Format(PyExc_OSError,
					    "perf: can't parse sample, err=%d", err);
		return pyevent;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *pyrf_evlist__open(struct pyrf_evlist *pevlist,
				   PyObject *args, PyObject *kwargs)
{
	struct perf_evlist *evlist = &pevlist->evlist;
	int group = 0;
	static char *kwlist[] = { "group", NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OOii", kwlist, &group))
		return NULL;

	if (group)
		perf_evlist__set_leader(evlist);

	if (perf_evlist__open(evlist) < 0) {
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

	return pevlist->evlist.nr_entries;
}

static PyObject *pyrf_evlist__item(PyObject *obj, Py_ssize_t i)
{
	struct pyrf_evlist *pevlist = (void *)obj;
	struct perf_evsel *pos;

	if (i >= pevlist->evlist.nr_entries)
		return NULL;

	list_for_each_entry(pos, &pevlist->evlist.entries, node)
		if (i-- == 0)
			break;

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

static struct {
	const char *name;
	int	    value;
} perf__constants[] = {
	{ "TYPE_HARDWARE",   PERF_TYPE_HARDWARE },
	{ "TYPE_SOFTWARE",   PERF_TYPE_SOFTWARE },
	{ "TYPE_TRACEPOINT", PERF_TYPE_TRACEPOINT },
	{ "TYPE_HW_CACHE",   PERF_TYPE_HW_CACHE },
	{ "TYPE_RAW",	     PERF_TYPE_RAW },
	{ "TYPE_BREAKPOINT", PERF_TYPE_BREAKPOINT },

	{ "COUNT_HW_CPU_CYCLES",	  PERF_COUNT_HW_CPU_CYCLES },
	{ "COUNT_HW_INSTRUCTIONS",	  PERF_COUNT_HW_INSTRUCTIONS },
	{ "COUNT_HW_CACHE_REFERENCES",	  PERF_COUNT_HW_CACHE_REFERENCES },
	{ "COUNT_HW_CACHE_MISSES",	  PERF_COUNT_HW_CACHE_MISSES },
	{ "COUNT_HW_BRANCH_INSTRUCTIONS", PERF_COUNT_HW_BRANCH_INSTRUCTIONS },
	{ "COUNT_HW_BRANCH_MISSES",	  PERF_COUNT_HW_BRANCH_MISSES },
	{ "COUNT_HW_BUS_CYCLES",	  PERF_COUNT_HW_BUS_CYCLES },
	{ "COUNT_HW_CACHE_L1D",		  PERF_COUNT_HW_CACHE_L1D },
	{ "COUNT_HW_CACHE_L1I",		  PERF_COUNT_HW_CACHE_L1I },
	{ "COUNT_HW_CACHE_LL",	  	  PERF_COUNT_HW_CACHE_LL },
	{ "COUNT_HW_CACHE_DTLB",	  PERF_COUNT_HW_CACHE_DTLB },
	{ "COUNT_HW_CACHE_ITLB",	  PERF_COUNT_HW_CACHE_ITLB },
	{ "COUNT_HW_CACHE_BPU",		  PERF_COUNT_HW_CACHE_BPU },
	{ "COUNT_HW_CACHE_OP_READ",	  PERF_COUNT_HW_CACHE_OP_READ },
	{ "COUNT_HW_CACHE_OP_WRITE",	  PERF_COUNT_HW_CACHE_OP_WRITE },
	{ "COUNT_HW_CACHE_OP_PREFETCH",	  PERF_COUNT_HW_CACHE_OP_PREFETCH },
	{ "COUNT_HW_CACHE_RESULT_ACCESS", PERF_COUNT_HW_CACHE_RESULT_ACCESS },
	{ "COUNT_HW_CACHE_RESULT_MISS",   PERF_COUNT_HW_CACHE_RESULT_MISS },

	{ "COUNT_HW_STALLED_CYCLES_FRONTEND",	  PERF_COUNT_HW_STALLED_CYCLES_FRONTEND },
	{ "COUNT_HW_STALLED_CYCLES_BACKEND",	  PERF_COUNT_HW_STALLED_CYCLES_BACKEND },

	{ "COUNT_SW_CPU_CLOCK",	       PERF_COUNT_SW_CPU_CLOCK },
	{ "COUNT_SW_TASK_CLOCK",       PERF_COUNT_SW_TASK_CLOCK },
	{ "COUNT_SW_PAGE_FAULTS",      PERF_COUNT_SW_PAGE_FAULTS },
	{ "COUNT_SW_CONTEXT_SWITCHES", PERF_COUNT_SW_CONTEXT_SWITCHES },
	{ "COUNT_SW_CPU_MIGRATIONS",   PERF_COUNT_SW_CPU_MIGRATIONS },
	{ "COUNT_SW_PAGE_FAULTS_MIN",  PERF_COUNT_SW_PAGE_FAULTS_MIN },
	{ "COUNT_SW_PAGE_FAULTS_MAJ",  PERF_COUNT_SW_PAGE_FAULTS_MAJ },
	{ "COUNT_SW_ALIGNMENT_FAULTS", PERF_COUNT_SW_ALIGNMENT_FAULTS },
	{ "COUNT_SW_EMULATION_FAULTS", PERF_COUNT_SW_EMULATION_FAULTS },

	{ "SAMPLE_IP",	      PERF_SAMPLE_IP },
	{ "SAMPLE_TID",	      PERF_SAMPLE_TID },
	{ "SAMPLE_TIME",      PERF_SAMPLE_TIME },
	{ "SAMPLE_ADDR",      PERF_SAMPLE_ADDR },
	{ "SAMPLE_READ",      PERF_SAMPLE_READ },
	{ "SAMPLE_CALLCHAIN", PERF_SAMPLE_CALLCHAIN },
	{ "SAMPLE_ID",	      PERF_SAMPLE_ID },
	{ "SAMPLE_CPU",	      PERF_SAMPLE_CPU },
	{ "SAMPLE_PERIOD",    PERF_SAMPLE_PERIOD },
	{ "SAMPLE_STREAM_ID", PERF_SAMPLE_STREAM_ID },
	{ "SAMPLE_RAW",	      PERF_SAMPLE_RAW },

	{ "FORMAT_TOTAL_TIME_ENABLED", PERF_FORMAT_TOTAL_TIME_ENABLED },
	{ "FORMAT_TOTAL_TIME_RUNNING", PERF_FORMAT_TOTAL_TIME_RUNNING },
	{ "FORMAT_ID",		       PERF_FORMAT_ID },
	{ "FORMAT_GROUP",	       PERF_FORMAT_GROUP },

	{ "RECORD_MMAP",       PERF_RECORD_MMAP },
	{ "RECORD_LOST",       PERF_RECORD_LOST },
	{ "RECORD_COMM",       PERF_RECORD_COMM },
	{ "RECORD_EXIT",       PERF_RECORD_EXIT },
	{ "RECORD_THROTTLE",   PERF_RECORD_THROTTLE },
	{ "RECORD_UNTHROTTLE", PERF_RECORD_UNTHROTTLE },
	{ "RECORD_FORK",       PERF_RECORD_FORK },
	{ "RECORD_READ",       PERF_RECORD_READ },
	{ "RECORD_SAMPLE",     PERF_RECORD_SAMPLE },
	{ .name = NULL, },
};

static PyMethodDef perf__methods[] = {
	{ .ml_name = NULL, }
};

PyMODINIT_FUNC initperf(void)
{
	PyObject *obj;
	int i;
	PyObject *dict, *module = Py_InitModule("perf", perf__methods);

	if (module == NULL ||
	    pyrf_event__setup_types() < 0 ||
	    pyrf_evlist__setup_types() < 0 ||
	    pyrf_evsel__setup_types() < 0 ||
	    pyrf_thread_map__setup_types() < 0 ||
	    pyrf_cpu_map__setup_types() < 0)
		return;

	page_size = sysconf(_SC_PAGE_SIZE);

	Py_INCREF(&pyrf_evlist__type);
	PyModule_AddObject(module, "evlist", (PyObject*)&pyrf_evlist__type);

	Py_INCREF(&pyrf_evsel__type);
	PyModule_AddObject(module, "evsel", (PyObject*)&pyrf_evsel__type);

	Py_INCREF(&pyrf_thread_map__type);
	PyModule_AddObject(module, "thread_map", (PyObject*)&pyrf_thread_map__type);

	Py_INCREF(&pyrf_cpu_map__type);
	PyModule_AddObject(module, "cpu_map", (PyObject*)&pyrf_cpu_map__type);

	dict = PyModule_GetDict(module);
	if (dict == NULL)
		goto error;

	for (i = 0; perf__constants[i].name != NULL; i++) {
		obj = PyInt_FromLong(perf__constants[i].value);
		if (obj == NULL)
			goto error;
		PyDict_SetItemString(dict, perf__constants[i].name, obj);
		Py_DECREF(obj);
	}

error:
	if (PyErr_Occurred())
		PyErr_SetString(PyExc_ImportError, "perf: Init failed!");
}

/*
 * Dummy, to avoid dragging all the test_attr infrastructure in the python
 * binding.
 */
void test_attr__open(struct perf_event_attr *attr, pid_t pid, int cpu,
                     int fd, int group_fd, unsigned long flags)
{
}
