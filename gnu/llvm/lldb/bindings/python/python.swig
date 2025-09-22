/*
   lldb.swig

   This is the input file for SWIG, to create the appropriate C++ wrappers and
   functions for various scripting languages, to enable them to call the
   liblldb Script Bridge functions.
*/

/* Define our module docstring. */
%define DOCSTRING
"The lldb module contains the public APIs for Python binding.

Some of the important classes are described here:

* :py:class:`SBTarget`: Represents the target program running under the debugger.
* :py:class:`SBProcess`: Represents the process associated with the target program.
* :py:class:`SBThread`: Represents a thread of execution. :py:class:`SBProcess` contains SBThreads.
* :py:class:`SBFrame`: Represents one of the stack frames associated with a thread. :py:class:`SBThread`
  contains SBFrame(s).
* :py:class:`SBSymbolContext`: A container that stores various debugger related info.
* :py:class:`SBValue`: Represents the value of a variable, a register, or an expression.
* :py:class:`SBModule`: Represents an executable image and its associated object and symbol
  files.  :py:class:`SBTarget` contains SBModule.
* :py:class:`SBBreakpoint`: Represents a logical breakpoint and its associated settings.
  :py:class:`SBTarget` contains SBBreakpoints.
* :py:class:`SBSymbol`: Represents the symbol possibly associated with a stack frame.
* :py:class:`SBCompileUnit`: Represents a compilation unit, or compiled source file.
* :py:class:`SBFunction`: Represents a generic function, which can be inlined or not.
* :py:class:`SBBlock`: Represents a lexical block. :py:class:`SBFunction` contains SBBlocks.
* :py:class:`SBLineEntry`: Specifies an association with a contiguous range of instructions
  and a source file location. :py:class:`SBCompileUnit` contains SBLineEntry.

The different enums in the `lldb` module are described in :doc:`python_api_enums`.

"
%enddef

/*
Since version 3.0.9, swig's logic for importing the native module has changed in
a way that is incompatible with our usage of the python module as __init__.py
(See swig bug #769).  Fortunately, since version 3.0.11, swig provides a way for
us to override the module import logic to suit our needs. This does that.

Older swig versions will simply ignore this setting.
*/
%define MODULEIMPORT
"try:
    # Try an absolute import first.  If we're being loaded from lldb,
    # _lldb should be a built-in module.
    import $module
except ImportError:
    # Relative import should work if we are being loaded by Python.
    from . import $module"
%enddef

// The name of the module to be created.
%module(docstring=DOCSTRING, moduleimport=MODULEIMPORT) lldb

// Parameter types will be used in the autodoc string.
%feature("autodoc", "1");

%define ARRAYHELPER(type,name)
%inline %{
type *new_ ## name (int nitems) {
   return (type *) malloc(sizeof(type)*nitems);
}
void delete_ ## name(type *t) {
   free(t);
}
type name ## _get(type *t, int index) {
   return t[index];
}
void name ## _set(type *t, int index, type val) {
   t[index] = val;
}
%}
%enddef

%pythoncode%{
import uuid
import re
import os
%}

// Include the version of swig that was used to generate this interface.
%define EMBED_VERSION(VERSION)
%pythoncode%{
# SWIG_VERSION is written as a single hex number, but the components of it are
# meant to be interpreted in decimal. So, 0x030012 is swig 3.0.12, and not
# 3.0.18.
def _to_int(hex):
    return hex // 0x10 % 0x10 * 10 + hex % 0x10
swig_version = (_to_int(VERSION // 0x10000), _to_int(VERSION // 0x100), _to_int(VERSION))
del _to_int
%}
%enddef
EMBED_VERSION(SWIG_VERSION)

%pythoncode%{
# ===================================
# Iterator for lldb container objects
# ===================================
def lldb_iter(obj, getsize, getelem):
    """A generator adaptor to support iteration for lldb container objects."""
    size = getattr(obj, getsize)
    elem = getattr(obj, getelem)
    for i in range(size()):
        yield elem(i)
%}

%include <std_string.i>
%include "python-typemaps.swig"
%include "macros.swig"
%include "headers.swig"

%{
#include "../source/Plugins/ScriptInterpreter/Python/PythonDataObjects.h"
#include "../source/Plugins/ScriptInterpreter/Python/SWIGPythonBridge.h"
#include "../bindings/python/python-swigsafecast.swig"
using namespace lldb_private;
using namespace lldb_private::python;
using namespace lldb;
%}

%include "interfaces.swig"
%include "python-extensions.swig"
%include "python-wrapper.swig"

%pythoncode%{
debugger_unique_id = 0
SBDebugger.Initialize()
debugger = None
target = None
process = None
thread = None
frame = None
%}
