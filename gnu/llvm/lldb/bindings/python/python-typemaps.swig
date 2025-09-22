/* Typemap definitions, to allow SWIG to properly handle 'char**' data types.

NOTE: If there's logic to free memory in a %typemap(freearg), it will also be
run if you call SWIG_fail on an error path. Don't manually free() an argument
AND call SWIG_fail at the same time, because it will result in a double free.

*/

%inline %{

#include "../bindings/python/python-typemaps.h"

%}

%typemap(in) char ** {
  /* Check if is a list  */
  if (PythonList::Check($input)) {
    PythonList list(PyRefType::Borrowed, $input);
    int size = list.GetSize();
    int i = 0;
    $1 = (char **)malloc((size + 1) * sizeof(char *));
    for (i = 0; i < size; i++) {
      PythonString py_str = list.GetItemAtIndex(i).AsType<PythonString>();
      if (!py_str.IsAllocated()) {
        PyErr_SetString(PyExc_TypeError, "list must contain strings");
        SWIG_fail;
      }

      $1[i] = const_cast<char *>(py_str.GetString().data());
    }
    $1[i] = 0;
  } else if ($input == Py_None) {
    $1 = NULL;
  } else {
    PyErr_SetString(PyExc_TypeError, "not a list");
    SWIG_fail;
  }
}

%typemap(typecheck) char ** {
  /* Check if is a list  */
  $1 = 1;
  if (PythonList::Check($input)) {
    PythonList list(PyRefType::Borrowed, $input);
    int size = list.GetSize();
    int i = 0;
    for (i = 0; i < size; i++) {
      PythonString s = list.GetItemAtIndex(i).AsType<PythonString>();
      if (!s.IsAllocated()) {
        $1 = 0;
      }
    }
  } else {
    $1 = (($input == Py_None) ? 1 : 0);
  }
}

%typemap(freearg) char** {
  free((char *) $1);
}

%typecheck(SWIG_TYPECHECK_POINTER) lldb::ScriptObjectPtr {
  PythonObject obj(PyRefType::Borrowed, $input);
  if (!obj.IsValid()) {
    PyErr_Clear();
    $1 = 0;
  } else {
    $1 = 1;
  }
}

%typemap(in) lldb::ScriptObjectPtr {
  if ($input == Py_None) {
    $1 = nullptr;
  } else {
    PythonObject obj(PyRefType::Borrowed, $input);
    if (!obj.IsValid()) {
      PyErr_SetString(PyExc_TypeError, "Script object is not valid");
      SWIG_fail;
    }

    auto lldb_module = PythonModule::Import("lldb");
    if (!lldb_module) {
      std::string err_msg = llvm::toString(lldb_module.takeError());
      PyErr_SetString(PyExc_TypeError, err_msg.c_str());
      SWIG_fail;
    }

    auto sb_structured_data_class = lldb_module.get().Get("SBStructuredData");
    if (!sb_structured_data_class) {
      std::string err_msg = llvm::toString(sb_structured_data_class.takeError());
      PyErr_SetString(PyExc_TypeError, err_msg.c_str());
      SWIG_fail;
    }

    if (obj.IsInstance(sb_structured_data_class.get())) {
      $1 = obj.get();
    } else {
      auto type = obj.GetType();
      if (!type) {
        std::string err_msg = llvm::toString(type.takeError());
        PyErr_SetString(PyExc_TypeError, err_msg.c_str());
        SWIG_fail;
      }

      auto type_name = As<std::string>(type.get().GetAttribute("__name__"));
      if (!type_name) {
        std::string err_msg = llvm::toString(type_name.takeError());
        PyErr_SetString(PyExc_TypeError, err_msg.c_str());
        SWIG_fail;
      }

      if (llvm::StringRef(type_name.get()).starts_with("SB")) {
        std::string error_msg = "Input type is invalid: " + type_name.get();
        PyErr_SetString(PyExc_TypeError, error_msg.c_str());
        SWIG_fail;
      } else {
        $1 = obj.get();
      }
    }
  }
}

%typemap(out) lldb::ScriptObjectPtr {
  $result = (PyObject*) $1;
  if (!$result)
    $result = Py_None;
  Py_INCREF($result);
}

%typemap(out) lldb::SBScriptObject {
  $result = nullptr;
  if (const void* impl = $1.GetPointer())
    $result = (PyObject*) impl;
  if (!$result) {
    $result = Py_None;
    Py_INCREF(Py_None);
  } else {
    Py_INCREF($result);
  }
}

%typemap(out) char** {
  int len;
  int i;
  len = 0;
  while ($1[len])
    len++;
  PythonList list(len);
  for (i = 0; i < len; i++)
    list.SetItemAtIndex(i, PythonString($1[i]));
  $result = list.release();
}

%typemap(in) lldb::tid_t {
  PythonObject obj = Retain<PythonObject>($input);
  lldb::tid_t value = unwrapOrSetPythonException(As<unsigned long long>(obj));
  if (PyErr_Occurred())
    SWIG_fail;
  $1 = value;
}

%typemap(in) lldb::StateType {
  PythonObject obj = Retain<PythonObject>($input);
  unsigned long long state_type_value =
      unwrapOrSetPythonException(As<unsigned long long>(obj));
  if (PyErr_Occurred())
    SWIG_fail;
  if (state_type_value > lldb::StateType::kLastStateType) {
    PyErr_SetString(PyExc_ValueError, "Not a valid StateType value");
    SWIG_fail;
  }
  $1 = static_cast<lldb::StateType>(state_type_value);
}

/* Typemap definitions to allow SWIG to properly handle char buffer. */

// typemap for a char buffer
%typemap(in) (char *dst, size_t dst_len) {
  if (!PyLong_Check($input)) {
    PyErr_SetString(PyExc_ValueError, "Expecting an integer");
    SWIG_fail;
  }
  $2 = PyLong_AsLong($input);
  if ($2 <= 0) {
    PyErr_SetString(PyExc_ValueError, "Positive integer expected");
    SWIG_fail;
  }
  $1 = (char *)malloc($2);
}
// SBProcess::ReadCStringFromMemory() uses a void*, but needs to be treated
// as char data instead of byte data.
%typemap(in) (void *char_buf, size_t size) = (char *dst, size_t dst_len);

// Return the char buffer.  Discarding any previous return result
%typemap(argout) (char *dst, size_t dst_len) {
  Py_XDECREF($result); /* Blow away any previous result */
  if (result == 0) {
    PythonString string("");
    $result = string.release();
    Py_INCREF($result);
  } else {
    llvm::StringRef ref(static_cast<const char *>($1), result);
    PythonString string(ref);
    $result = string.release();
  }
  free($1);
}
// SBProcess::ReadCStringFromMemory() uses a void*, but needs to be treated
// as char data instead of byte data.
%typemap(argout) (void *char_buf, size_t size) = (char *dst, size_t dst_len);


// typemap for handling an snprintf-like API like SBThread::GetStopDescription.
%typemap(in) (char *dst_or_null, size_t dst_len) {
  if (!PyLong_Check($input)) {
    PyErr_SetString(PyExc_ValueError, "Expecting an integer");
    SWIG_fail;
  }
  $2 = PyLong_AsLong($input);
  if ($2 <= 0) {
    PyErr_SetString(PyExc_ValueError, "Positive integer expected");
    SWIG_fail;
  }
  $1 = (char *)malloc($2);
}
%typemap(argout) (char *dst_or_null, size_t dst_len) {
  Py_XDECREF($result); /* Blow away any previous result */
  llvm::StringRef ref($1);
  PythonString string(ref);
  $result = string.release();
  free($1);
}


// typemap for an outgoing buffer
// See also SBEvent::SBEvent(uint32_t event, const char *cstr, uint32_t cstr_len).
// Ditto for SBProcess::PutSTDIN(const char *src, size_t src_len).
%typemap(in) (const char *cstr, uint32_t cstr_len),
             (const char *src, size_t src_len) {
  if (PythonString::Check($input)) {
    PythonString str(PyRefType::Borrowed, $input);
    $1 = (char *)str.GetString().data();
    $2 = str.GetSize();
  } else if (PythonByteArray::Check($input)) {
    PythonByteArray bytearray(PyRefType::Borrowed, $input);
    $1 = (char *)bytearray.GetBytes().data();
    $2 = bytearray.GetSize();
  } else if (PythonBytes::Check($input)) {
    PythonBytes bytes(PyRefType::Borrowed, $input);
    $1 = (char *)bytes.GetBytes().data();
    $2 = bytes.GetSize();
  } else {
    PyErr_SetString(PyExc_ValueError, "Expecting a string");
    SWIG_fail;
  }
}
// For SBProcess::WriteMemory, SBTarget::GetInstructions and SBDebugger::DispatchInput.
%typemap(in) (const void *buf, size_t size),
             (const void *data, size_t data_len),
             (const void *buf, uint64_t size) {
  if (PythonString::Check($input)) {
    PythonString str(PyRefType::Borrowed, $input);
    $1 = (void *)str.GetString().data();
    $2 = str.GetSize();
  } else if (PythonByteArray::Check($input)) {
    PythonByteArray bytearray(PyRefType::Borrowed, $input);
    $1 = (void *)bytearray.GetBytes().data();
    $2 = bytearray.GetSize();
  } else if (PythonBytes::Check($input)) {
    PythonBytes bytes(PyRefType::Borrowed, $input);
    $1 = (void *)bytes.GetBytes().data();
    $2 = bytes.GetSize();
  } else {
    PyErr_SetString(PyExc_ValueError, "Expecting a buffer");
    SWIG_fail;
  }
}

// typemap for an incoming buffer
// See also SBProcess::ReadMemory.
%typemap(in) (void *buf, size_t size) {
  if (PyLong_Check($input)) {
    $2 = PyLong_AsLong($input);
  } else {
    PyErr_SetString(PyExc_ValueError, "Expecting an integer or long object");
    SWIG_fail;
  }
  if ($2 <= 0) {
    PyErr_SetString(PyExc_ValueError, "Positive integer expected");
    SWIG_fail;
  }
  $1 = (void *)malloc($2);
}

// Return the buffer.  Discarding any previous return result
// See also SBProcess::ReadMemory.
%typemap(argout) (void *buf, size_t size) {
  Py_XDECREF($result); /* Blow away any previous result */
  if (result == 0) {
    $result = Py_None;
    Py_INCREF($result);
  } else {
    PythonBytes bytes(static_cast<const uint8_t *>($1), result);
    $result = bytes.release();
  }
  free($1);
}

%{
namespace {
template <class T>
T PyLongAsT(PyObject *obj) {
  static_assert(true, "unsupported type");
}

template <> uint64_t PyLongAsT<uint64_t>(PyObject *obj) {
  return static_cast<uint64_t>(PyLong_AsUnsignedLongLong(obj));
}

template <> uint32_t PyLongAsT<uint32_t>(PyObject *obj) {
  return static_cast<uint32_t>(PyLong_AsUnsignedLong(obj));
}

template <> int64_t PyLongAsT<int64_t>(PyObject *obj) {
  return static_cast<int64_t>(PyLong_AsLongLong(obj));
}

template <> int32_t PyLongAsT<int32_t>(PyObject *obj) {
  return static_cast<int32_t>(PyLong_AsLong(obj));
}

template <class T> bool SetNumberFromPyObject(T &number, PyObject *obj) {
  if (PyLong_Check(obj))
    number = PyLongAsT<T>(obj);
  else
    return false;

  return true;
}

template <> bool SetNumberFromPyObject<double>(double &number, PyObject *obj) {
  if (PyFloat_Check(obj)) {
    number = PyFloat_AsDouble(obj);
    return true;
  }

  return false;
}

} // namespace
%}

// these typemaps allow Python users to pass list objects
// and have them turn into C++ arrays (this is useful, for instance
// when creating SBData objects from lists of numbers)
%typemap(in) (uint64_t* array, size_t array_len),
             (uint32_t* array, size_t array_len),
             (int64_t* array, size_t array_len),
             (int32_t* array, size_t array_len),
             (double* array, size_t array_len) {
  /* Check if is a list  */
  if (PyList_Check($input)) {
    int size = PyList_Size($input);
    int i = 0;
    $2 = size;
    $1 = ($1_type)malloc(size * sizeof($*1_type));
    for (i = 0; i < size; i++) {
      PyObject *o = PyList_GetItem($input, i);
      if (!SetNumberFromPyObject($1[i], o)) {
        PyErr_SetString(PyExc_TypeError, "list must contain numbers");
        SWIG_fail;
      }

      if (PyErr_Occurred()) {
        SWIG_fail;
      }
    }
  } else if ($input == Py_None) {
    $1 = NULL;
    $2 = 0;
  } else {
    PyErr_SetString(PyExc_TypeError, "not a list");
    SWIG_fail;
  }
}

%typemap(freearg) (uint64_t* array, size_t array_len),
                  (uint32_t* array, size_t array_len),
                  (int64_t* array, size_t array_len),
                  (int32_t* array, size_t array_len),
                  (double* array, size_t array_len) {
  free($1);
}

// these typemaps wrap SBModule::GetVersion() from requiring a memory buffer
// to the more Pythonic style where a list is returned and no previous allocation
// is necessary - this will break if more than 50 versions are ever returned
%typemap(typecheck) (uint32_t *versions, uint32_t num_versions) {
  $1 = ($input == Py_None ? 1 : 0);
}

%typemap(in, numinputs=0) (uint32_t *versions) {
  $1 = (uint32_t *)malloc(sizeof(uint32_t) * 50);
}

%typemap(in, numinputs=0) (uint32_t num_versions) {
  $1 = 50;
}

%typemap(argout) (uint32_t *versions, uint32_t num_versions) {
  uint32_t count = result;
  if (count >= $2)
    count = $2;
  PyObject *list = PyList_New(count);
  for (uint32_t j = 0; j < count; j++) {
    PyObject *item = PyLong_FromLong($1[j]);
    int ok = PyList_SetItem(list, j, item);
    if (ok != 0) {
      $result = Py_None;
      break;
    }
  }
  $result = list;
}

%typemap(freearg) (uint32_t *versions) {
  free($1);
}

// For Log::LogOutputCallback
%typemap(in) (lldb::LogOutputCallback log_callback, void *baton) {
  if (!($input == Py_None ||
        PyCallable_Check(reinterpret_cast<PyObject *>($input)))) {
    PyErr_SetString(PyExc_TypeError, "Need a callable object or None!");
    SWIG_fail;
  }

  // FIXME (filcab): We can't currently check if our callback is already
  // LLDBSwigPythonCallPythonLogOutputCallback (to DECREF the previous
  // baton) nor can we just remove all traces of a callback, if we want to
  // revert to a file logging mechanism.

  // Don't lose the callback reference
  Py_INCREF($input);
  $1 = LLDBSwigPythonCallPythonLogOutputCallback;
  $2 = $input;
}

%typemap(typecheck) (lldb::LogOutputCallback log_callback, void *baton) {
  $1 = $input == Py_None;
  $1 = $1 || PyCallable_Check(reinterpret_cast<PyObject *>($input));
}

// For lldb::SBDebuggerDestroyCallback
%typemap(in) (lldb::SBDebuggerDestroyCallback destroy_callback, void *baton) {
  if (!($input == Py_None ||
        PyCallable_Check(reinterpret_cast<PyObject *>($input)))) {
    PyErr_SetString(PyExc_TypeError, "Need a callable object or None!");
    SWIG_fail;
  }

  // FIXME (filcab): We can't currently check if our callback is already
  // LLDBSwigPythonCallPythonSBDebuggerTerminateCallback (to DECREF the previous
  // baton) nor can we just remove all traces of a callback, if we want to
  // revert to a file logging mechanism.

  // Don't lose the callback reference
  Py_INCREF($input);
  $1 = LLDBSwigPythonCallPythonSBDebuggerTerminateCallback;
  $2 = $input;
}

%typemap(typecheck) (lldb::SBDebuggerDestroyCallback destroy_callback, void *baton) {
  $1 = $input == Py_None;
  $1 = $1 || PyCallable_Check(reinterpret_cast<PyObject *>($input));
}

%typemap(in) (lldb::CommandOverrideCallback callback, void *baton) {
  if (!($input == Py_None ||
        PyCallable_Check(reinterpret_cast<PyObject *>($input)))) {
    PyErr_SetString(PyExc_TypeError, "Need a callable object or None!");
    SWIG_fail;
  }

  // Don't lose the callback reference.
  Py_INCREF($input);
  $1 = LLDBSwigPythonCallPythonSBCommandInterpreterSetCommandOverrideCallback;
  $2 = $input;
}
%typemap(typecheck) (lldb::CommandOverrideCallback callback, void *baton) {
  $1 = $input == Py_None;
  $1 = $1 || PyCallable_Check(reinterpret_cast<PyObject *>($input));
}

%typemap(in) lldb::FileSP {
  PythonFile py_file(PyRefType::Borrowed, $input);
  if (!py_file) {
    PyErr_SetString(PyExc_TypeError, "not a file");
    SWIG_fail;
  }
  auto sp = unwrapOrSetPythonException(py_file.ConvertToFile());
  if (!sp)
    SWIG_fail;
  $1 = sp;
}

%typemap(in) lldb::FileSP FORCE_IO_METHODS {
  PythonFile py_file(PyRefType::Borrowed, $input);
  if (!py_file) {
    PyErr_SetString(PyExc_TypeError, "not a file");
    SWIG_fail;
  }
  auto sp = unwrapOrSetPythonException(
      py_file.ConvertToFileForcingUseOfScriptingIOMethods());
  if (!sp)
    SWIG_fail;
  $1 = sp;
}

%typemap(in) lldb::FileSP BORROWED {
  PythonFile py_file(PyRefType::Borrowed, $input);
  if (!py_file) {
    PyErr_SetString(PyExc_TypeError, "not a file");
    SWIG_fail;
  }
  auto sp =
      unwrapOrSetPythonException(py_file.ConvertToFile(/*borrowed=*/true));
  if (!sp)
    SWIG_fail;
  $1 = sp;
}

%typemap(in) lldb::FileSP BORROWED_FORCE_IO_METHODS {
  PythonFile py_file(PyRefType::Borrowed, $input);
  if (!py_file) {
    PyErr_SetString(PyExc_TypeError, "not a file");
    SWIG_fail;
  }
  auto sp = unwrapOrSetPythonException(
      py_file.ConvertToFileForcingUseOfScriptingIOMethods(/*borrowed=*/true));
  if (!sp)
    SWIG_fail;
  $1 = sp;
}

%typecheck(SWIG_TYPECHECK_POINTER) lldb::FileSP {
  if (PythonFile::Check($input)) {
    $1 = 1;
  } else {
    PyErr_Clear();
    $1 = 0;
  }
}

%typemap(out) lldb::FileSP {
  $result = nullptr;
  const lldb::FileSP &sp = $1;
  if (sp) {
    PythonFile pyfile = unwrapOrSetPythonException(PythonFile::FromFile(*sp));
    if (!pyfile.IsValid())
      SWIG_fail;
    $result = pyfile.release();
  }
  if (!$result) {
    $result = Py_None;
    Py_INCREF(Py_None);
  }
}

%typemap(in) (const char* string, int len) {
  if ($input == Py_None) {
    $1 = NULL;
    $2 = 0;
  } else if (PythonString::Check($input)) {
    PythonString py_str(PyRefType::Borrowed, $input);
    llvm::StringRef str = py_str.GetString();
    $1 = const_cast<char *>(str.data());
    $2 = str.size();
    // In Python 2, if $input is a PyUnicode object then this
    // will trigger a Unicode -> String conversion, in which
    // case the `PythonString` will now own the PyString.  Thus
    // if it goes out of scope, the data will be deleted.  The
    // only way to avoid this is to leak the Python object in
    // that case.  Note that if there was no conversion, then
    // releasing the string will not leak anything, since we
    // created this as a borrowed reference.
    py_str.release();
  } else {
    PyErr_SetString(PyExc_TypeError, "not a string-like object");
    SWIG_fail;
  }
}

// These two pybuffer macros are copied out of swig/Lib/python/pybuffer.i,
// and fixed so they will not crash if PyObject_GetBuffer fails.
// https://github.com/swig/swig/issues/1640
//
// I've also moved the call to PyBuffer_Release to the end of the SWIG wrapper,
// doing it right away is not legal according to the python buffer protocol.

%define %pybuffer_mutable_binary(TYPEMAP, SIZE)
%typemap(in) (TYPEMAP, SIZE) (Py_buffer_RAII view) {
  int res;
  Py_ssize_t size = 0;
  void *buf = 0;
  res = PyObject_GetBuffer($input, &view.buffer, PyBUF_WRITABLE);
  if (res < 0) {
    PyErr_Clear();
    %argument_fail(res, "(TYPEMAP, SIZE)", $symname, $argnum);
  }
  size = view.buffer.len;
  buf = view.buffer.buf;
  $1 = ($1_ltype)buf;
  $2 = ($2_ltype)(size / sizeof($*1_type));
}
%enddef

%define %pybuffer_binary(TYPEMAP, SIZE)
%typemap(in) (TYPEMAP, SIZE) (Py_buffer_RAII view) {
  int res;
  Py_ssize_t size = 0;
  const void *buf = 0;
  res = PyObject_GetBuffer($input, &view.buffer, PyBUF_CONTIG_RO);
  if (res < 0) {
    PyErr_Clear();
    %argument_fail(res, "(TYPEMAP, SIZE)", $symname, $argnum);
  }
  size = view.buffer.len;
  buf = view.buffer.buf;
  $1 = ($1_ltype)buf;
  $2 = ($2_ltype)(size / sizeof($*1_type));
}
%enddef

%pybuffer_binary(const uint8_t *buf, size_t num_bytes);
%pybuffer_mutable_binary(uint8_t *buf, size_t num_bytes);

%typemap(in) (const char **symbol_name, uint32_t num_names) {
  using namespace lldb_private;
  /* Check if is a list  */
  if (PythonList::Check($input)) {
    PythonList list(PyRefType::Borrowed, $input);
    $2 = list.GetSize();
    int i = 0;
    $1 = (char**)malloc(($2+1)*sizeof(char*));
    for (i = 0; i < $2; i++) {
      PythonString py_str = list.GetItemAtIndex(i).AsType<PythonString>();
      if (!py_str.IsAllocated()) {
        PyErr_SetString(PyExc_TypeError,"list must contain strings and blubby");
        free($1);
        return nullptr;
      }

      $1[i] = const_cast<char*>(py_str.GetString().data());
    }
    $1[i] = 0;
  } else if ($input == Py_None) {
    $1 =  NULL;
  } else {
    PyErr_SetString(PyExc_TypeError,"not a list");
    return NULL;
  }
}

// For lldb::SBPlatformLocateModuleCallback
%typemap(in) (lldb::SBPlatformLocateModuleCallback callback,
              void *callback_baton) {
  if (!($input == Py_None ||
        PyCallable_Check(reinterpret_cast<PyObject *>($input)))) {
    PyErr_SetString(PyExc_TypeError, "Need a callable object or None!");
    SWIG_fail;
  }

  if ($input == Py_None) {
    $1 = nullptr;
    $2 = nullptr;
  } else {
    PythonCallable callable = Retain<PythonCallable>($input);
    if (!callable.IsValid()) {
      PyErr_SetString(PyExc_TypeError, "Need a valid callable object");
      SWIG_fail;
    }

    llvm::Expected<PythonCallable::ArgInfo> arg_info = callable.GetArgInfo();
    if (!arg_info) {
      PyErr_SetString(PyExc_TypeError,
                      ("Could not get arguments: " +
                          llvm::toString(arg_info.takeError())).c_str());
      SWIG_fail;
    }

    if (arg_info.get().max_positional_args != 3) {
      PyErr_SetString(PyExc_TypeError, "Expected 3 argument callable object");
      SWIG_fail;
    }

    // NOTE: When this is called multiple times, this will leak the Python
    // callable object as other callbacks, because this does not call Py_DECREF
    // the object. But it should be almost zero impact since this method is
    // expected to be called only once.

    // Don't lose the callback reference
    Py_INCREF($input);

    $1 = LLDBSwigPythonCallLocateModuleCallback;
    $2 = $input;
  }
}

%typemap(typecheck) (lldb::SBPlatformLocateModuleCallback callback,
                     void *callback_baton) {
  $1 = $input == Py_None;
  $1 = $1 || PyCallable_Check(reinterpret_cast<PyObject *>($input));
}
