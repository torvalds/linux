/*
   lldb.swig

   This is the input file for SWIG, to create the appropriate C++ wrappers and
   functions for various scripting languages, to enable them to call the
   liblldb Script Bridge functions.
*/

%module lldb

%include <std_string.i>
%include "lua-typemaps.swig"
%include "macros.swig"
%include "headers.swig"

%{
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "../bindings/lua/lua-swigsafecast.swig"
#include "../source/Plugins/ScriptInterpreter/Lua/SWIGLuaBridge.h"

// required headers for typemaps
#include "lldb/Host/File.h"

using namespace lldb_private;
using namespace lldb;
%}

%include "interfaces.swig"
%include "lua-wrapper.swig"
