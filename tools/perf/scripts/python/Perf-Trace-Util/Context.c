/*
 * Context.c.  Python interfaces for perf script.
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
#include "../../../perf.h"
#include "../../../util/trace-event.h"

PyMODINIT_FUNC initperf_trace_context(void);

static PyObject *perf_trace_context_common_pc(PyObject *self, PyObject *args)
{
	static struct scripting_context *scripting_context;
	PyObject *context;
	int retval;

	if (!PyArg_ParseTuple(args, "O", &context))
		return NULL;

	scripting_context = PyCObject_AsVoidPtr(context);
	retval = common_pc(scripting_context);

	return Py_BuildValue("i", retval);
}

static PyObject *perf_trace_context_common_flags(PyObject *self,
						 PyObject *args)
{
	static struct scripting_context *scripting_context;
	PyObject *context;
	int retval;

	if (!PyArg_ParseTuple(args, "O", &context))
		return NULL;

	scripting_context = PyCObject_AsVoidPtr(context);
	retval = common_flags(scripting_context);

	return Py_BuildValue("i", retval);
}

static PyObject *perf_trace_context_common_lock_depth(PyObject *self,
						      PyObject *args)
{
	static struct scripting_context *scripting_context;
	PyObject *context;
	int retval;

	if (!PyArg_ParseTuple(args, "O", &context))
		return NULL;

	scripting_context = PyCObject_AsVoidPtr(context);
	retval = common_lock_depth(scripting_context);

	return Py_BuildValue("i", retval);
}

static PyMethodDef ContextMethods[] = {
	{ "common_pc", perf_trace_context_common_pc, METH_VARARGS,
	  "Get the common preempt count event field value."},
	{ "common_flags", perf_trace_context_common_flags, METH_VARARGS,
	  "Get the common flags event field value."},
	{ "common_lock_depth", perf_trace_context_common_lock_depth,
	  METH_VARARGS,	"Get the common lock depth event field value."},
	{ NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC initperf_trace_context(void)
{
	(void) Py_InitModule("perf_trace_context", ContextMethods);
}
