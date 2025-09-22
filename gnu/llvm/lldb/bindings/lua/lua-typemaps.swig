%include <typemaps.i>

// FIXME: We need to port more typemaps from Python

//===----------------------------------------------------------------------===//

// In Lua 5.3 and beyond the VM supports integers, so we need to remap
// SWIG's internal handling of integers.


%define LLDB_NUMBER_TYPEMAP(TYPE)

// Primitive integer mapping
%typemap(in,checkfn="lua_isinteger") TYPE
%{ $1 = ($type)lua_tointeger(L, $input); %}
%typemap(in,checkfn="lua_isinteger") const TYPE&($basetype temp)
%{ temp=($basetype)lua_tointeger(L,$input); $1=&temp;%}
%typemap(out) TYPE
%{ lua_pushinteger(L, (lua_Integer) $1); SWIG_arg++;%}
%typemap(out) const TYPE&
%{ lua_pushinteger(L, (lua_Integer) $1); SWIG_arg++;%}

// Pointer and reference mapping
%typemap(in,checkfn="lua_isinteger") TYPE *INPUT($*ltype temp), TYPE &INPUT($*ltype temp)
%{ temp = ($*ltype)lua_tointeger(L,$input);
   $1 = &temp; %}
%typemap(in, numinputs=0) TYPE *OUTPUT ($*ltype temp)
%{ $1 = &temp; %}
%typemap(argout) TYPE *OUTPUT
%{  lua_pushinteger(L, (lua_Integer) *$1); SWIG_arg++;%}
%typemap(in) TYPE *INOUT = TYPE *INPUT;
%typemap(argout) TYPE *INOUT = TYPE *OUTPUT;
%typemap(in) TYPE &OUTPUT = TYPE *OUTPUT;
%typemap(argout) TYPE &OUTPUT = TYPE *OUTPUT;
%typemap(in) TYPE &INOUT = TYPE *INPUT;
%typemap(argout) TYPE &INOUT = TYPE *OUTPUT;
%typemap(in,checkfn="lua_isinteger") const TYPE *INPUT($*ltype temp)
%{ temp = ($*ltype)lua_tointeger(L,$input);
   $1 = &temp; %}

%enddef // LLDB_NUMBER_TYPEMAP

LLDB_NUMBER_TYPEMAP(unsigned char);
LLDB_NUMBER_TYPEMAP(signed char);
LLDB_NUMBER_TYPEMAP(short);
LLDB_NUMBER_TYPEMAP(unsigned short);
LLDB_NUMBER_TYPEMAP(signed short);
LLDB_NUMBER_TYPEMAP(int);
LLDB_NUMBER_TYPEMAP(unsigned int);
LLDB_NUMBER_TYPEMAP(signed int);
LLDB_NUMBER_TYPEMAP(long);
LLDB_NUMBER_TYPEMAP(unsigned long);
LLDB_NUMBER_TYPEMAP(signed long);
LLDB_NUMBER_TYPEMAP(long long);
LLDB_NUMBER_TYPEMAP(unsigned long long);
LLDB_NUMBER_TYPEMAP(signed long long);
LLDB_NUMBER_TYPEMAP(enum SWIGTYPE);

%apply unsigned long { size_t };
%apply const unsigned long & { const size_t & };
%apply long { ssize_t };
%apply const long & { const ssize_t & };

//===----------------------------------------------------------------------===//

// FIXME:
//  Ideally all the typemaps should be revisited in a future SB API revision.
//  Typemaps, usually, modifies the function signatures and might spawn
//  different LLDB APIs across languages (C++, Python, Lua...).
//  Historically, typemaps have been used to replace SWIG's deficiencies,
//  but SWIG itself evolved and some API design choices are now redundant.

//===----------------------------------------------------------------------===//

// Typemap definitions to allow SWIG to properly handle char buffer.

// typemap for a char buffer
%typemap(in) (char *dst, size_t dst_len) {
  $2 = luaL_checkinteger(L, $input);
  if ($2 <= 0) {
    return luaL_error(L, "Positive integer expected");
  }
  $1 = (char *)malloc($2);
}

// SBProcess::ReadCStringFromMemory() uses a void*, but needs to be treated
// as char data instead of byte data.
%typemap(in) (void *char_buf, size_t size) = (char *dst, size_t dst_len);

// Also SBProcess::ReadMemory.
%typemap(in) (void *buf, size_t size) = (char *dst, size_t dst_len);

// Return the char buffer.  Discarding any previous return result
%typemap(argout) (char *dst, size_t dst_len) {
  lua_pop(L, 1); // Blow away the previous result
  if ($result == 0) {
    lua_pushliteral(L, "");
  } else {
    lua_pushlstring(L, (const char *)$1, $result);
  }
  free($1);
  // SWIG_arg was already incremented
}

// SBProcess::ReadCStringFromMemory() uses a void*, but needs to be treated
// as char data instead of byte data.
%typemap(argout) (void *char_buf, size_t size) = (char *dst, size_t dst_len);

// Also SBProcess::ReadMemory.
%typemap(argout) (void *buf, size_t size) = (char *dst, size_t dst_len);

//===----------------------------------------------------------------------===//

// Typemap for handling a snprintf-like API like SBThread::GetStopDescription.

%typemap(in) (char *dst_or_null, size_t dst_len) {
  $2 = luaL_checkinteger(L, $input);
  if ($2 <= 0) {
    return luaL_error(L, "Positive integer expected");
  }
  $1 = (char *)malloc($2);
}

%typemap(argout) (char *dst_or_null, size_t dst_len) {
  lua_pop(L, 1); // Blow away the previous result
  lua_pushlstring(L, (const char *)$1, $result);
  free($1);
  // SWIG_arg was already incremented
}

//===----------------------------------------------------------------------===//

// Typemap for handling SBModule::GetVersion

%typemap(in) (uint32_t *versions, uint32_t num_versions) {
  $2 = 99;
  $1 = (uint32_t *)malloc(sizeof(uint32_t) * $2);
}

%typemap(argout) (uint32_t *versions, uint32_t num_versions) {
  uint32_t count = result;
  if (count >= $2)
    count = $2;
  lua_newtable(L);
  int i = 0;
  while (i++ < count) {
    lua_pushinteger(L, $1[i - 1]);
    lua_seti(L, -2, i);
  }
  SWIG_arg++;
  free($1);
}

//===----------------------------------------------------------------------===//

// Typemap for handling SBDebugger::SetLoggingCallback

%typemap(in) (lldb::LogOutputCallback log_callback, void *baton) {
  $1 = LLDBSwigLuaCallLuaLogOutputCallback;
  $2 = (void *)L;

  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_settop(L, 2);

  lua_pushlightuserdata(L, (void *)&LLDBSwigLuaCallLuaLogOutputCallback);
  lua_insert(L, 2);
  lua_settable(L, LUA_REGISTRYINDEX);
}

//===----------------------------------------------------------------------===//

// Typemap for handling SBEvent::SBEvent(uint32_t event, const char *cstr, uint32_t cstr_len)

%typemap(in) (const char *cstr, uint32_t cstr_len) {
  $1 = (char *)luaL_checklstring(L, $input, (size_t *)&$2);
}

// Typemap for handling SBProcess::PutSTDIN

%typemap(in) (const char *src, size_t src_len) {
  $1 = (char *)luaL_checklstring(L, $input, &$2);
}

// Typemap for handling SBProcess::WriteMemory, SBTarget::GetInstructions...

%typemap(in) (const void *buf, size_t size),
             (const void *data, size_t data_len) {
  $1 = (void *)luaL_checklstring(L, $input, &$2);
}

//===----------------------------------------------------------------------===//

// Typemap for handling char ** in SBTarget::LaunchSimple, SBTarget::Launch...

// It should accept a Lua table of strings, for stuff like "argv" and "envp".

%typemap(in) char ** {
  if (lua_istable(L, $input)) {
    size_t size = lua_rawlen(L, $input);
    $1 = (char **)malloc((size + 1) * sizeof(char *));
    int i = 0, j = 0;
    while (i++ < size) {
      lua_rawgeti(L, $input, i);
      if (!lua_isstring(L, -1)) {
        // if current element cannot be converted to string, raise an error
        lua_pop(L, 1);
        return luaL_error(L, "List should only contain strings");
      }
      $1[j++] = (char *)lua_tostring(L, -1);
      lua_pop(L, 1);
    }
    $1[j] = 0;
  } else if (lua_isnil(L, $input)) {
    // "nil" is also acceptable, equivalent as an empty table
    $1 = NULL;
  } else {
    return luaL_error(L, "A list of strings expected");
  }
}

%typemap(freearg) char ** {
  free((char *) $1);
}

%typecheck(SWIG_TYPECHECK_STRING_ARRAY) char ** {
  $1 = (lua_istable(L, $input) || lua_isnil(L, $input));
}

//===----------------------------------------------------------------------===//

// Typemap for file handles (e.g. used in SBDebugger::SetOutputFile)

%typemap(in) lldb::FileSP {
  luaL_Stream *p = (luaL_Stream *)luaL_checkudata(L, $input, LUA_FILEHANDLE);
  lldb::FileSP file_sp;
  file_sp = std::make_shared<lldb_private::NativeFile>(p->f, false);
  if (!file_sp->IsValid())
    return luaL_error(L, "Invalid file");
  $1 = file_sp;
}

%typecheck(SWIG_TYPECHECK_POINTER) lldb::FileSP {
  $1 = (lua_isuserdata(L, $input)) &&
       (luaL_testudata(L, $input, LUA_FILEHANDLE) != nullptr);
}

// Typemap for file handles (e.g. used in SBDebugger::GetOutputFileHandle)

%typemap(out) lldb::FileSP {
  lldb::FileSP sp = $1;
  if (sp && sp->IsValid()) {
    luaL_Stream *p = (luaL_Stream *)lua_newuserdata(L, sizeof(luaL_Stream));
    p->closef = &LLDBSwigLuaCloseFileHandle;
    p->f = sp->GetStream();
    luaL_setmetatable(L, LUA_FILEHANDLE);
    SWIG_arg++;
  }
}

//===----------------------------------------------------------------------===//

// Typemap for SBData::CreateDataFromUInt64Array, SBData::SetDataFromUInt64Array ...

%typemap(in) (uint64_t* array, size_t array_len),
             (uint32_t* array, size_t array_len),
             (int64_t* array, size_t array_len),
             (int32_t* array, size_t array_len),
             (double* array, size_t array_len) {
  if (lua_istable(L, $input)) {
    // It should accept a table of numbers.
    $2 = lua_rawlen(L, $input);
    $1 = ($1_ltype)malloc(($2) * sizeof($*1_type));
    int i = 0, j = 0;
    while (i++ < $2) {
      lua_rawgeti(L, $input, i);
      if (!lua_isnumber(L, -1)) {
        // if current element cannot be converted to number, raise an error
        lua_pop(L, 1);
        return luaL_error(L, "List should only contain numbers");
      }
      $1[j++] = ($*1_ltype) lua_tonumber(L, -1);
      lua_pop(L, 1);
    }
  } else if (lua_isnil(L, $input)) {
    // "nil" is also acceptable, equivalent as an empty table
    $1 = NULL;
    $2 = 0;
  } else {
    // else raise an error
    return luaL_error(L, "A list of numbers expected.");
  }
}

%typemap(freearg) (uint64_t* array, size_t array_len),
             (uint32_t* array, size_t array_len),
             (int64_t* array, size_t array_len),
             (int32_t* array, size_t array_len),
             (double* array, size_t array_len) {
  free($1);
}

//===----------------------------------------------------------------------===//

// Typemap for SBCommandReturnObject::PutCString

%typemap(in) (const char *string, int len) {
  if (lua_isnil(L, $input)) {
    $1 = NULL;
    $2 = 0;
  } else {
    $1 = (char *)luaL_checklstring(L, $input, (size_t *)&$2);
  }
}

//===----------------------------------------------------------------------===//
