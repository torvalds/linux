// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Context.c.  Python interfaces for perf script.
 *
 * Copyright (C) 2010 Tom Zanussi <tzanussi@gmail.com>
 */

#include <Python.h>
#include "../../../util/trace-event.h"
#include "../../../util/event.h"
#include "../../../util/symbol.h"
#include "../../../util/thread.h"
#include "../../../util/maps.h"

#if PY_MAJOR_VERSION < 3
#define _PyCapsule_GetPointer(arg1, arg2) \
  PyCObject_AsVoidPtr(arg1)
#define _PyBytes_FromStringAndSize(arg1, arg2) \
  PyString_FromStringAndSize((arg1), (arg2))

PyMODINIT_FUNC initperf_trace_context(void);
#else
#define _PyCapsule_GetPointer(arg1, arg2) \
  PyCapsule_GetPointer((arg1), (arg2))
#define _PyBytes_FromStringAndSize(arg1, arg2) \
  PyBytes_FromStringAndSize((arg1), (arg2))

PyMODINIT_FUNC PyInit_perf_trace_context(void);
#endif

static struct scripting_context *get_scripting_context(PyObject *args)
{
	PyObject *context;

	if (!PyArg_ParseTuple(args, "O", &context))
		return NULL;

	return _PyCapsule_GetPointer(context, NULL);
}

static PyObject *perf_trace_context_common_pc(PyObject *obj, PyObject *args)
{
	struct scripting_context *c = get_scripting_context(args);

	if (!c)
		return NULL;

	return Py_BuildValue("i", common_pc(c));
}

static PyObject *perf_trace_context_common_flags(PyObject *obj,
						 PyObject *args)
{
	struct scripting_context *c = get_scripting_context(args);

	if (!c)
		return NULL;

	return Py_BuildValue("i", common_flags(c));
}

static PyObject *perf_trace_context_common_lock_depth(PyObject *obj,
						      PyObject *args)
{
	struct scripting_context *c = get_scripting_context(args);

	if (!c)
		return NULL;

	return Py_BuildValue("i", common_lock_depth(c));
}

static PyObject *perf_sample_insn(PyObject *obj, PyObject *args)
{
	struct scripting_context *c = get_scripting_context(args);

	if (!c)
		return NULL;

	if (c->sample->ip && !c->sample->insn_len &&
	    c->al->thread->maps && c->al->thread->maps->machine)
		script_fetch_insn(c->sample, c->al->thread, c->al->thread->maps->machine);

	if (!c->sample->insn_len)
		Py_RETURN_NONE; /* N.B. This is a return statement */

	return _PyBytes_FromStringAndSize(c->sample->insn, c->sample->insn_len);
}

static PyMethodDef ContextMethods[] = {
	{ "common_pc", perf_trace_context_common_pc, METH_VARARGS,
	  "Get the common preempt count event field value."},
	{ "common_flags", perf_trace_context_common_flags, METH_VARARGS,
	  "Get the common flags event field value."},
	{ "common_lock_depth", perf_trace_context_common_lock_depth,
	  METH_VARARGS,	"Get the common lock depth event field value."},
	{ "perf_sample_insn", perf_sample_insn,
	  METH_VARARGS,	"Get the machine code instruction."},
	{ NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION < 3
PyMODINIT_FUNC initperf_trace_context(void)
{
	(void) Py_InitModule("perf_trace_context", ContextMethods);
}
#else
PyMODINIT_FUNC PyInit_perf_trace_context(void)
{
	static struct PyModuleDef moduledef = {
		PyModuleDef_HEAD_INIT,
		"perf_trace_context",	/* m_name */
		"",			/* m_doc */
		-1,			/* m_size */
		ContextMethods,		/* m_methods */
		NULL,			/* m_reload */
		NULL,			/* m_traverse */
		NULL,			/* m_clear */
		NULL,			/* m_free */
	};
	PyObject *mod;

	mod = PyModule_Create(&moduledef);
	/* Add perf_script_context to the module so it can be imported */
	PyObject_SetAttrString(mod, "perf_script_context", Py_None);

	return mod;
}
#endif
