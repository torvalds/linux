/*-
 * Copyright (c) 2014, 2015 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <Python.h>

#include "bus.h"
#include "busdma.h"

static PyObject *
bus_read_1(PyObject *self, PyObject *args)
{
	long ofs;
	int rid;
	uint8_t val;

	if (!PyArg_ParseTuple(args, "il", &rid, &ofs))
		return (NULL);
	if (!bs_read(rid, ofs, &val, sizeof(val))) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	return (Py_BuildValue("B", val));
}

static PyObject *
bus_read_2(PyObject *self, PyObject *args)
{
	long ofs;
	int rid;
	uint16_t val;

	if (!PyArg_ParseTuple(args, "il", &rid, &ofs))
		return (NULL);
	if (!bs_read(rid, ofs, &val, sizeof(val))) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	return (Py_BuildValue("H", val));
}

static PyObject *
bus_read_4(PyObject *self, PyObject *args)
{
	long ofs;
	int rid;
	uint32_t val;

	if (!PyArg_ParseTuple(args, "il", &rid, &ofs))
		return (NULL);
	if (!bs_read(rid, ofs, &val, sizeof(val))) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	return (Py_BuildValue("I", val));
}

static PyObject *
bus_write_1(PyObject *self, PyObject *args)
{
	long ofs;
	int rid;
	uint8_t val;

	if (!PyArg_ParseTuple(args, "ilB", &rid, &ofs, &val))
		return (NULL);
	if (!bs_write(rid, ofs, &val, sizeof(val))) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	Py_RETURN_NONE;
}

static PyObject *
bus_write_2(PyObject *self, PyObject *args)
{
	long ofs;
	int rid;
	uint16_t val;

	if (!PyArg_ParseTuple(args, "ilH", &rid, &ofs, &val))
		return (NULL);
	if (!bs_write(rid, ofs, &val, sizeof(val))) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	Py_RETURN_NONE;
}

static PyObject *
bus_write_4(PyObject *self, PyObject *args)
{
	long ofs;
	int rid;
	uint32_t val;

	if (!PyArg_ParseTuple(args, "ilI", &rid, &ofs, &val))
		return (NULL);
	if (!bs_write(rid, ofs, &val, sizeof(val))) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	Py_RETURN_NONE;
}

static PyObject *
bus_map(PyObject *self, PyObject *args)
{
	char *dev, *resource;
	int rid;

	if (!PyArg_ParseTuple(args, "ss", &dev, &resource))
		return (NULL);
	rid = bs_map(dev, resource);
	if (rid == -1) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	return (Py_BuildValue("i", rid));
}

static PyObject *
bus_unmap(PyObject *self, PyObject *args)
{
	int rid;

	if (!PyArg_ParseTuple(args, "i", &rid))
		return (NULL);
	if (!bs_unmap(rid)) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	Py_RETURN_NONE;
}

static PyObject *
bus_subregion(PyObject *self, PyObject *args)
{
	long ofs, sz;
	int rid0, rid;

	if (!PyArg_ParseTuple(args, "ill", &rid0, &ofs, &sz))
		return (NULL);
	rid = bs_subregion(rid0, ofs, sz);
	if (rid == -1) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	return (Py_BuildValue("i", rid));
}

static PyObject *
busdma_tag_create(PyObject *self, PyObject *args)
{
	char *dev;
	u_long align, bndry, maxaddr, maxsz, maxsegsz;
	u_int nsegs, datarate, flags;
	int tid;

	if (!PyArg_ParseTuple(args, "skkkkIkII", &dev, &align, &bndry,
	    &maxaddr, &maxsz, &nsegs, &maxsegsz, &datarate, &flags))
		return (NULL);
	tid = bd_tag_create(dev, align, bndry, maxaddr, maxsz, nsegs,
	    maxsegsz, datarate, flags);
	if (tid == -1) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	return (Py_BuildValue("i", tid));
}

static PyObject *
busdma_tag_derive(PyObject *self, PyObject *args)
{
	u_long align, bndry, maxaddr, maxsz, maxsegsz;
	u_int nsegs, datarate, flags;
	int ptid, tid;
 
	if (!PyArg_ParseTuple(args, "ikkkkIkII", &ptid, &align, &bndry,
	    &maxaddr, &maxsz, &nsegs, &maxsegsz, &datarate, &flags))
		return (NULL);
	tid = bd_tag_derive(ptid, align, bndry, maxaddr, maxsz, nsegs,
	    maxsegsz, datarate, flags);
	if (tid == -1) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	return (Py_BuildValue("i", tid));
}

static PyObject *
busdma_tag_destroy(PyObject *self, PyObject *args)
{
	int error, tid;
 
	if (!PyArg_ParseTuple(args, "i", &tid))
		return (NULL);
	error = bd_tag_destroy(tid);
	if (error) {
		PyErr_SetString(PyExc_IOError, strerror(error));
		return (NULL);
	}
	Py_RETURN_NONE;
}

static PyObject *
busdma_md_create(PyObject *self, PyObject *args)
{
	u_int flags;
	int error, mdid, tid;
 
	if (!PyArg_ParseTuple(args, "iI", &tid, &flags))
		return (NULL);
	mdid = bd_md_create(tid, flags);
	if (mdid == -1) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	return (Py_BuildValue("i", mdid));
}

static PyObject *
busdma_md_destroy(PyObject *self, PyObject *args)
{
	int error, mdid;

	if (!PyArg_ParseTuple(args, "i", &mdid))
		return (NULL);
	error = bd_md_destroy(mdid);
	if (error) {
		PyErr_SetString(PyExc_IOError, strerror(error));
		return (NULL);
	}
	Py_RETURN_NONE;
}

static PyObject *
busdma_md_load(PyObject *self, PyObject *args)
{
	void *buf;
	u_long len;
	u_int flags;
	int error, mdid;

	if (!PyArg_ParseTuple(args, "iwkI", &mdid, &buf, &len, &flags))
		return (NULL);
	error = bd_md_load(mdid, buf, len, flags);
	if (error) {
		PyErr_SetString(PyExc_IOError, strerror(error));
		return (NULL);
	}
	Py_RETURN_NONE;
}

static PyObject *
busdma_md_unload(PyObject *self, PyObject *args)
{
	int error, mdid;

	if (!PyArg_ParseTuple(args, "i", &mdid))
		return (NULL);
	error = bd_md_unload(mdid);
	if (error) {
		PyErr_SetString(PyExc_IOError, strerror(error));
		return (NULL);
	}
	Py_RETURN_NONE;
}

static PyObject *
busdma_mem_alloc(PyObject *self, PyObject *args)
{
	u_int flags;
	int mdid, tid;

	if (!PyArg_ParseTuple(args, "iI", &tid, &flags))
		return (NULL);
	mdid = bd_mem_alloc(tid, flags);
	if (mdid == -1) {
		PyErr_SetString(PyExc_IOError, strerror(errno));
		return (NULL);
	}
	return (Py_BuildValue("i", mdid));
}

static PyObject *
busdma_mem_free(PyObject *self, PyObject *args)
{
	int error, mdid;

	if (!PyArg_ParseTuple(args, "i", &mdid))
		return (NULL);
	error = bd_mem_free(mdid);
	if (error) {
		PyErr_SetString(PyExc_IOError, strerror(error));
		return (NULL);
	}
	Py_RETURN_NONE;
}

static PyObject *
busdma_md_first_seg(PyObject *self, PyObject *args)
{
	int error, mdid, sid, what;

	if (!PyArg_ParseTuple(args, "ii", &mdid, &what))
		return (NULL);
	sid = bd_md_first_seg(mdid, what);
	if (sid == -1)
		Py_RETURN_NONE;
	return (Py_BuildValue("i", sid));
}

static PyObject *
busdma_md_next_seg(PyObject *self, PyObject *args)
{
	int error, mdid, sid;

	if (!PyArg_ParseTuple(args, "ii", &mdid, &sid))
		return (NULL);
	sid = bd_md_next_seg(mdid, sid);
	if (sid == -1)
		Py_RETURN_NONE;
	return (Py_BuildValue("i", sid));
}

static PyObject *
busdma_seg_get_addr(PyObject *self, PyObject *args)
{
	u_long addr;
	int error, sid;

	if (!PyArg_ParseTuple(args, "i", &sid))
		return (NULL);
	error = bd_seg_get_addr(sid, &addr);
	if (error) {
		PyErr_SetString(PyExc_IOError, strerror(error));
		return (NULL);
	}
	return (Py_BuildValue("k", addr));
}

static PyObject *
busdma_seg_get_size(PyObject *self, PyObject *args)
{
	u_long size;
	int error, sid;

	if (!PyArg_ParseTuple(args, "i", &sid))
		return (NULL);
	error = bd_seg_get_size(sid, &size);
	if (error) {
		PyErr_SetString(PyExc_IOError, strerror(error));
		return (NULL);
	}
	return (Py_BuildValue("k", size));
}

static PyObject *
busdma_sync(PyObject *self, PyObject *args)
{
	int error, mdid, op;

	if (!PyArg_ParseTuple(args, "ii", &mdid, &op))
		return (NULL);
	error = bd_sync(mdid, op, 0UL, ~0UL);
	if (error) {
		PyErr_SetString(PyExc_IOError, strerror(error));
		return (NULL);
	}
	Py_RETURN_NONE;
}

static PyObject *
busdma_sync_range(PyObject *self, PyObject *args)
{
	u_long ofs, len;
	int error, mdid, op;

	if (!PyArg_ParseTuple(args, "iikk", &mdid, &op, &ofs, &len))
		return (NULL);
	error = bd_sync(mdid, op, ofs, len);
	if (error) {
		PyErr_SetString(PyExc_IOError, strerror(error));
		return (NULL);
	}
	Py_RETURN_NONE;
}

static PyMethodDef bus_methods[] = {
    { "read_1", bus_read_1, METH_VARARGS, "Read a 1-byte data item." },
    { "read_2", bus_read_2, METH_VARARGS, "Read a 2-byte data item." },
    { "read_4", bus_read_4, METH_VARARGS, "Read a 4-byte data item." },

    { "write_1", bus_write_1, METH_VARARGS, "Write a 1-byte data item." },
    { "write_2", bus_write_2, METH_VARARGS, "Write a 2-byte data item." },
    { "write_4", bus_write_4, METH_VARARGS, "Write a 4-byte data item." },

    { "map", bus_map, METH_VARARGS,
	"Return a resource ID for a device file created by proto(4)" },
    { "unmap", bus_unmap, METH_VARARGS,
	"Free a resource ID" },
    { "subregion", bus_subregion, METH_VARARGS,
	"Return a resource ID for a subregion of another resource ID" },

    { NULL, NULL, 0, NULL }
};

static PyMethodDef busdma_methods[] = {
    { "tag_create", busdma_tag_create, METH_VARARGS,
	"Create a root tag." },
    { "tag_derive", busdma_tag_derive, METH_VARARGS,
	"Derive a child tag." },
    { "tag_destroy", busdma_tag_destroy, METH_VARARGS,
	"Destroy a tag." },

    { "md_create", busdma_md_create, METH_VARARGS,
	"Create a new and empty memory descriptor." },
    { "md_destroy", busdma_md_destroy, METH_VARARGS,
	"Destroy a previously created memory descriptor." },
    { "md_load", busdma_md_load, METH_VARARGS,
	"Load a buffer into a memory descriptor." },
    { "md_unload", busdma_md_unload, METH_VARARGS,
	"Unload a memory descriptor." },

    { "mem_alloc", busdma_mem_alloc, METH_VARARGS,
	"Allocate memory according to the DMA constraints." },
    { "mem_free", busdma_mem_free, METH_VARARGS,
	"Free allocated memory." },

    { "md_first_seg", busdma_md_first_seg, METH_VARARGS,
	"Return first segment in one of the segment lists." },
    { "md_next_seg", busdma_md_next_seg, METH_VARARGS,
	"Return next segment in the segment list." },
    { "seg_get_addr", busdma_seg_get_addr, METH_VARARGS,
	"Return the address of the segment." },
    { "seg_get_size", busdma_seg_get_size, METH_VARARGS,
	"Return the size of the segment." },

    { "sync", busdma_sync, METH_VARARGS,
	"Make the entire memory descriptor coherent WRT to DMA." },
    { "sync_range", busdma_sync_range, METH_VARARGS,
	"Make part of the memory descriptor coherent WRT to DMA." },

    { NULL, NULL, 0, NULL }
};

PyMODINIT_FUNC
initbus(void)
{
	PyObject *bus, *busdma;

	bus = Py_InitModule("bus", bus_methods);
	if (bus == NULL)
		return;
	busdma = Py_InitModule("busdma", busdma_methods);
	if (busdma == NULL)
		return;
	PyModule_AddObject(bus, "dma", busdma);

	PyModule_AddObject(busdma, "MD_BUS_SPACE", Py_BuildValue("i", 0));
	PyModule_AddObject(busdma, "MD_PHYS_SPACE", Py_BuildValue("i", 1));
	PyModule_AddObject(busdma, "MD_VIRT_SPACE", Py_BuildValue("i", 2));

	PyModule_AddObject(busdma, "SYNC_PREREAD", Py_BuildValue("i", 1));
	PyModule_AddObject(busdma, "SYNC_POSTREAD", Py_BuildValue("i", 2));
	PyModule_AddObject(busdma, "SYNC_PREWRITE", Py_BuildValue("i", 4));
	PyModule_AddObject(busdma, "SYNC_POSTWRITE", Py_BuildValue("i", 8));
}
