#ifndef LLDB_BINDINGS_PYTHON_PYTHON_TYPEMAPS_H
#define LLDB_BINDINGS_PYTHON_PYTHON_TYPEMAPS_H

#include <Python.h>

// Defined here instead of a .swig file because SWIG 2 doesn't support
// explicit deleted functions.
struct Py_buffer_RAII {
  Py_buffer buffer = {};
  Py_buffer_RAII(){};
  Py_buffer &operator=(const Py_buffer_RAII &) = delete;
  Py_buffer_RAII(const Py_buffer_RAII &) = delete;
  ~Py_buffer_RAII() {
    if (buffer.obj)
      PyBuffer_Release(&buffer);
  }
};

#endif // LLDB_BINDINGS_PYTHON_PYTHON_TYPEMAPS_H
