//===-- PythonDataObjects.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"

#if LLDB_ENABLE_PYTHON

#include "PythonDataObjects.h"
#include "ScriptInterpreterPython.h"

#include "lldb/Host/File.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Errno.h"

#include <cstdio>
#include <variant>

using namespace lldb_private;
using namespace lldb;
using namespace lldb_private::python;
using llvm::cantFail;
using llvm::Error;
using llvm::Expected;
using llvm::Twine;

template <> Expected<bool> python::As<bool>(Expected<PythonObject> &&obj) {
  if (!obj)
    return obj.takeError();
  return obj.get().IsTrue();
}

template <>
Expected<long long> python::As<long long>(Expected<PythonObject> &&obj) {
  if (!obj)
    return obj.takeError();
  return obj->AsLongLong();
}

template <>
Expected<unsigned long long>
python::As<unsigned long long>(Expected<PythonObject> &&obj) {
  if (!obj)
    return obj.takeError();
  return obj->AsUnsignedLongLong();
}

template <>
Expected<std::string> python::As<std::string>(Expected<PythonObject> &&obj) {
  if (!obj)
    return obj.takeError();
  PyObject *str_obj = PyObject_Str(obj.get().get());
  if (!str_obj)
    return llvm::make_error<PythonException>();
  auto str = Take<PythonString>(str_obj);
  auto utf8 = str.AsUTF8();
  if (!utf8)
    return utf8.takeError();
  return std::string(utf8.get());
}

static bool python_is_finalizing() {
#if (PY_MAJOR_VERSION == 3 && PY_MINOR_VERSION >= 13) || (PY_MAJOR_VERSION > 3)
  return Py_IsFinalizing();
#elif PY_MAJOR_VERSION == 3 && PY_MINOR_VERSION < 7
  return _Py_Finalizing != nullptr;
#else
  return _Py_IsFinalizing();
#endif
}

void PythonObject::Reset() {
  if (m_py_obj && Py_IsInitialized()) {
    if (python_is_finalizing()) {
      // Leak m_py_obj rather than crashing the process.
      // https://docs.python.org/3/c-api/init.html#c.PyGILState_Ensure
    } else {
      PyGILState_STATE state = PyGILState_Ensure();
      Py_DECREF(m_py_obj);
      PyGILState_Release(state);
    }
  }
  m_py_obj = nullptr;
}

Expected<long long> PythonObject::AsLongLong() const {
  if (!m_py_obj)
    return nullDeref();
  assert(!PyErr_Occurred());
  long long r = PyLong_AsLongLong(m_py_obj);
  if (PyErr_Occurred())
    return exception();
  return r;
}

Expected<unsigned long long> PythonObject::AsUnsignedLongLong() const {
  if (!m_py_obj)
    return nullDeref();
  assert(!PyErr_Occurred());
  long long r = PyLong_AsUnsignedLongLong(m_py_obj);
  if (PyErr_Occurred())
    return exception();
  return r;
}

// wraps on overflow, instead of raising an error.
Expected<unsigned long long> PythonObject::AsModuloUnsignedLongLong() const {
  if (!m_py_obj)
    return nullDeref();
  assert(!PyErr_Occurred());
  unsigned long long r = PyLong_AsUnsignedLongLongMask(m_py_obj);
  // FIXME: We should fetch the exception message and hoist it.
  if (PyErr_Occurred())
    return exception();
  return r;
}

void StructuredPythonObject::Serialize(llvm::json::OStream &s) const {
  s.value(llvm::formatv("Python Obj: {0:X}", GetValue()).str());
}

// PythonObject

void PythonObject::Dump(Stream &strm) const {
  if (m_py_obj) {
    FILE *file = llvm::sys::RetryAfterSignal(nullptr, ::tmpfile);
    if (file) {
      ::PyObject_Print(m_py_obj, file, 0);
      const long length = ftell(file);
      if (length) {
        ::rewind(file);
        std::vector<char> file_contents(length, '\0');
        const size_t length_read =
            ::fread(file_contents.data(), 1, file_contents.size(), file);
        if (length_read > 0)
          strm.Write(file_contents.data(), length_read);
      }
      ::fclose(file);
    }
  } else
    strm.PutCString("NULL");
}

PyObjectType PythonObject::GetObjectType() const {
  if (!IsAllocated())
    return PyObjectType::None;

  if (PythonModule::Check(m_py_obj))
    return PyObjectType::Module;
  if (PythonList::Check(m_py_obj))
    return PyObjectType::List;
  if (PythonTuple::Check(m_py_obj))
    return PyObjectType::Tuple;
  if (PythonDictionary::Check(m_py_obj))
    return PyObjectType::Dictionary;
  if (PythonString::Check(m_py_obj))
    return PyObjectType::String;
  if (PythonBytes::Check(m_py_obj))
    return PyObjectType::Bytes;
  if (PythonByteArray::Check(m_py_obj))
    return PyObjectType::ByteArray;
  if (PythonBoolean::Check(m_py_obj))
    return PyObjectType::Boolean;
  if (PythonInteger::Check(m_py_obj))
    return PyObjectType::Integer;
  if (PythonFile::Check(m_py_obj))
    return PyObjectType::File;
  if (PythonCallable::Check(m_py_obj))
    return PyObjectType::Callable;
  return PyObjectType::Unknown;
}

PythonString PythonObject::Repr() const {
  if (!m_py_obj)
    return PythonString();
  PyObject *repr = PyObject_Repr(m_py_obj);
  if (!repr)
    return PythonString();
  return PythonString(PyRefType::Owned, repr);
}

PythonString PythonObject::Str() const {
  if (!m_py_obj)
    return PythonString();
  PyObject *str = PyObject_Str(m_py_obj);
  if (!str)
    return PythonString();
  return PythonString(PyRefType::Owned, str);
}

PythonObject
PythonObject::ResolveNameWithDictionary(llvm::StringRef name,
                                        const PythonDictionary &dict) {
  size_t dot_pos = name.find('.');
  llvm::StringRef piece = name.substr(0, dot_pos);
  PythonObject result = dict.GetItemForKey(PythonString(piece));
  if (dot_pos == llvm::StringRef::npos) {
    // There was no dot, we're done.
    return result;
  }

  // There was a dot.  The remaining portion of the name should be looked up in
  // the context of the object that was found in the dictionary.
  return result.ResolveName(name.substr(dot_pos + 1));
}

PythonObject PythonObject::ResolveName(llvm::StringRef name) const {
  // Resolve the name in the context of the specified object.  If, for example,
  // `this` refers to a PyModule, then this will look for `name` in this
  // module.  If `this` refers to a PyType, then it will resolve `name` as an
  // attribute of that type.  If `this` refers to an instance of an object,
  // then it will resolve `name` as the value of the specified field.
  //
  // This function handles dotted names so that, for example, if `m_py_obj`
  // refers to the `sys` module, and `name` == "path.append", then it will find
  // the function `sys.path.append`.

  size_t dot_pos = name.find('.');
  if (dot_pos == llvm::StringRef::npos) {
    // No dots in the name, we should be able to find the value immediately as
    // an attribute of `m_py_obj`.
    return GetAttributeValue(name);
  }

  // Look up the first piece of the name, and resolve the rest as a child of
  // that.
  PythonObject parent = ResolveName(name.substr(0, dot_pos));
  if (!parent.IsAllocated())
    return PythonObject();

  // Tail recursion.. should be optimized by the compiler
  return parent.ResolveName(name.substr(dot_pos + 1));
}

bool PythonObject::HasAttribute(llvm::StringRef attr) const {
  if (!IsValid())
    return false;
  PythonString py_attr(attr);
  return !!PyObject_HasAttr(m_py_obj, py_attr.get());
}

PythonObject PythonObject::GetAttributeValue(llvm::StringRef attr) const {
  if (!IsValid())
    return PythonObject();

  PythonString py_attr(attr);
  if (!PyObject_HasAttr(m_py_obj, py_attr.get()))
    return PythonObject();

  return PythonObject(PyRefType::Owned,
                      PyObject_GetAttr(m_py_obj, py_attr.get()));
}

StructuredData::ObjectSP PythonObject::CreateStructuredObject() const {
  assert(PyGILState_Check());
  switch (GetObjectType()) {
  case PyObjectType::Dictionary:
    return PythonDictionary(PyRefType::Borrowed, m_py_obj)
        .CreateStructuredDictionary();
  case PyObjectType::Boolean:
    return PythonBoolean(PyRefType::Borrowed, m_py_obj)
        .CreateStructuredBoolean();
  case PyObjectType::Integer: {
    StructuredData::IntegerSP int_sp =
        PythonInteger(PyRefType::Borrowed, m_py_obj).CreateStructuredInteger();
    if (std::holds_alternative<StructuredData::UnsignedIntegerSP>(int_sp))
      return std::get<StructuredData::UnsignedIntegerSP>(int_sp);
    if (std::holds_alternative<StructuredData::SignedIntegerSP>(int_sp))
      return std::get<StructuredData::SignedIntegerSP>(int_sp);
    return nullptr;
  };
  case PyObjectType::List:
    return PythonList(PyRefType::Borrowed, m_py_obj).CreateStructuredArray();
  case PyObjectType::String:
    return PythonString(PyRefType::Borrowed, m_py_obj).CreateStructuredString();
  case PyObjectType::Bytes:
    return PythonBytes(PyRefType::Borrowed, m_py_obj).CreateStructuredString();
  case PyObjectType::ByteArray:
    return PythonByteArray(PyRefType::Borrowed, m_py_obj)
        .CreateStructuredString();
  case PyObjectType::None:
    return StructuredData::ObjectSP();
  default:
    return StructuredData::ObjectSP(new StructuredPythonObject(
        PythonObject(PyRefType::Borrowed, m_py_obj)));
  }
}

// PythonString

PythonBytes::PythonBytes(llvm::ArrayRef<uint8_t> bytes) { SetBytes(bytes); }

PythonBytes::PythonBytes(const uint8_t *bytes, size_t length) {
  SetBytes(llvm::ArrayRef<uint8_t>(bytes, length));
}

bool PythonBytes::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;
  return PyBytes_Check(py_obj);
}

llvm::ArrayRef<uint8_t> PythonBytes::GetBytes() const {
  if (!IsValid())
    return llvm::ArrayRef<uint8_t>();

  Py_ssize_t size;
  char *c;

  PyBytes_AsStringAndSize(m_py_obj, &c, &size);
  return llvm::ArrayRef<uint8_t>(reinterpret_cast<uint8_t *>(c), size);
}

size_t PythonBytes::GetSize() const {
  if (!IsValid())
    return 0;
  return PyBytes_Size(m_py_obj);
}

void PythonBytes::SetBytes(llvm::ArrayRef<uint8_t> bytes) {
  const char *data = reinterpret_cast<const char *>(bytes.data());
  *this = Take<PythonBytes>(PyBytes_FromStringAndSize(data, bytes.size()));
}

StructuredData::StringSP PythonBytes::CreateStructuredString() const {
  StructuredData::StringSP result(new StructuredData::String);
  Py_ssize_t size;
  char *c;
  PyBytes_AsStringAndSize(m_py_obj, &c, &size);
  result->SetValue(std::string(c, size));
  return result;
}

PythonByteArray::PythonByteArray(llvm::ArrayRef<uint8_t> bytes)
    : PythonByteArray(bytes.data(), bytes.size()) {}

PythonByteArray::PythonByteArray(const uint8_t *bytes, size_t length) {
  const char *str = reinterpret_cast<const char *>(bytes);
  *this = Take<PythonByteArray>(PyByteArray_FromStringAndSize(str, length));
}

bool PythonByteArray::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;
  return PyByteArray_Check(py_obj);
}

llvm::ArrayRef<uint8_t> PythonByteArray::GetBytes() const {
  if (!IsValid())
    return llvm::ArrayRef<uint8_t>();

  char *c = PyByteArray_AsString(m_py_obj);
  size_t size = GetSize();
  return llvm::ArrayRef<uint8_t>(reinterpret_cast<uint8_t *>(c), size);
}

size_t PythonByteArray::GetSize() const {
  if (!IsValid())
    return 0;

  return PyByteArray_Size(m_py_obj);
}

StructuredData::StringSP PythonByteArray::CreateStructuredString() const {
  StructuredData::StringSP result(new StructuredData::String);
  llvm::ArrayRef<uint8_t> bytes = GetBytes();
  const char *str = reinterpret_cast<const char *>(bytes.data());
  result->SetValue(std::string(str, bytes.size()));
  return result;
}

// PythonString

Expected<PythonString> PythonString::FromUTF8(llvm::StringRef string) {
  PyObject *str = PyUnicode_FromStringAndSize(string.data(), string.size());
  if (!str)
    return llvm::make_error<PythonException>();
  return Take<PythonString>(str);
}

PythonString::PythonString(llvm::StringRef string) { SetString(string); }

bool PythonString::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;

  if (PyUnicode_Check(py_obj))
    return true;
  return false;
}

llvm::StringRef PythonString::GetString() const {
  auto s = AsUTF8();
  if (!s) {
    llvm::consumeError(s.takeError());
    return llvm::StringRef("");
  }
  return s.get();
}

Expected<llvm::StringRef> PythonString::AsUTF8() const {
  if (!IsValid())
    return nullDeref();

  Py_ssize_t size;
  const char *data;

  data = PyUnicode_AsUTF8AndSize(m_py_obj, &size);

  if (!data)
    return exception();

  return llvm::StringRef(data, size);
}

size_t PythonString::GetSize() const {
  if (IsValid()) {
#if PY_MINOR_VERSION >= 3
    return PyUnicode_GetLength(m_py_obj);
#else
    return PyUnicode_GetSize(m_py_obj);
#endif
  }
  return 0;
}

void PythonString::SetString(llvm::StringRef string) {
  auto s = FromUTF8(string);
  if (!s) {
    llvm::consumeError(s.takeError());
    Reset();
  } else {
    *this = std::move(s.get());
  }
}

StructuredData::StringSP PythonString::CreateStructuredString() const {
  StructuredData::StringSP result(new StructuredData::String);
  result->SetValue(GetString());
  return result;
}

// PythonInteger

PythonInteger::PythonInteger(int64_t value) { SetInteger(value); }

bool PythonInteger::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;

  // Python 3 does not have PyInt_Check.  There is only one type of integral
  // value, long.
  return PyLong_Check(py_obj);
}

void PythonInteger::SetInteger(int64_t value) {
  *this = Take<PythonInteger>(PyLong_FromLongLong(value));
}

StructuredData::IntegerSP PythonInteger::CreateStructuredInteger() const {
  StructuredData::UnsignedIntegerSP uint_sp = CreateStructuredUnsignedInteger();
  return uint_sp ? StructuredData::IntegerSP(uint_sp)
                 : CreateStructuredSignedInteger();
}

StructuredData::UnsignedIntegerSP
PythonInteger::CreateStructuredUnsignedInteger() const {
  StructuredData::UnsignedIntegerSP result = nullptr;
  llvm::Expected<unsigned long long> value = AsUnsignedLongLong();
  if (!value)
    llvm::consumeError(value.takeError());
  else
    result = std::make_shared<StructuredData::UnsignedInteger>(value.get());

  return result;
}

StructuredData::SignedIntegerSP
PythonInteger::CreateStructuredSignedInteger() const {
  StructuredData::SignedIntegerSP result = nullptr;
  llvm::Expected<long long> value = AsLongLong();
  if (!value)
    llvm::consumeError(value.takeError());
  else
    result = std::make_shared<StructuredData::SignedInteger>(value.get());

  return result;
}

// PythonBoolean

PythonBoolean::PythonBoolean(bool value) {
  SetValue(value);
}

bool PythonBoolean::Check(PyObject *py_obj) {
  return py_obj ? PyBool_Check(py_obj) : false;
}

bool PythonBoolean::GetValue() const {
  return m_py_obj ? PyObject_IsTrue(m_py_obj) : false;
}

void PythonBoolean::SetValue(bool value) {
  *this = Take<PythonBoolean>(PyBool_FromLong(value));
}

StructuredData::BooleanSP PythonBoolean::CreateStructuredBoolean() const {
  StructuredData::BooleanSP result(new StructuredData::Boolean);
  result->SetValue(GetValue());
  return result;
}

// PythonList

PythonList::PythonList(PyInitialValue value) {
  if (value == PyInitialValue::Empty)
    *this = Take<PythonList>(PyList_New(0));
}

PythonList::PythonList(int list_size) {
  *this = Take<PythonList>(PyList_New(list_size));
}

bool PythonList::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;
  return PyList_Check(py_obj);
}

uint32_t PythonList::GetSize() const {
  if (IsValid())
    return PyList_GET_SIZE(m_py_obj);
  return 0;
}

PythonObject PythonList::GetItemAtIndex(uint32_t index) const {
  if (IsValid())
    return PythonObject(PyRefType::Borrowed, PyList_GetItem(m_py_obj, index));
  return PythonObject();
}

void PythonList::SetItemAtIndex(uint32_t index, const PythonObject &object) {
  if (IsAllocated() && object.IsValid()) {
    // PyList_SetItem is documented to "steal" a reference, so we need to
    // convert it to an owned reference by incrementing it.
    Py_INCREF(object.get());
    PyList_SetItem(m_py_obj, index, object.get());
  }
}

void PythonList::AppendItem(const PythonObject &object) {
  if (IsAllocated() && object.IsValid()) {
    // `PyList_Append` does *not* steal a reference, so do not call `Py_INCREF`
    // here like we do with `PyList_SetItem`.
    PyList_Append(m_py_obj, object.get());
  }
}

StructuredData::ArraySP PythonList::CreateStructuredArray() const {
  StructuredData::ArraySP result(new StructuredData::Array);
  uint32_t count = GetSize();
  for (uint32_t i = 0; i < count; ++i) {
    PythonObject obj = GetItemAtIndex(i);
    result->AddItem(obj.CreateStructuredObject());
  }
  return result;
}

// PythonTuple

PythonTuple::PythonTuple(PyInitialValue value) {
  if (value == PyInitialValue::Empty)
    *this = Take<PythonTuple>(PyTuple_New(0));
}

PythonTuple::PythonTuple(int tuple_size) {
  *this = Take<PythonTuple>(PyTuple_New(tuple_size));
}

PythonTuple::PythonTuple(std::initializer_list<PythonObject> objects) {
  m_py_obj = PyTuple_New(objects.size());

  uint32_t idx = 0;
  for (auto object : objects) {
    if (object.IsValid())
      SetItemAtIndex(idx, object);
    idx++;
  }
}

PythonTuple::PythonTuple(std::initializer_list<PyObject *> objects) {
  m_py_obj = PyTuple_New(objects.size());

  uint32_t idx = 0;
  for (auto py_object : objects) {
    PythonObject object(PyRefType::Borrowed, py_object);
    if (object.IsValid())
      SetItemAtIndex(idx, object);
    idx++;
  }
}

bool PythonTuple::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;
  return PyTuple_Check(py_obj);
}

uint32_t PythonTuple::GetSize() const {
  if (IsValid())
    return PyTuple_GET_SIZE(m_py_obj);
  return 0;
}

PythonObject PythonTuple::GetItemAtIndex(uint32_t index) const {
  if (IsValid())
    return PythonObject(PyRefType::Borrowed, PyTuple_GetItem(m_py_obj, index));
  return PythonObject();
}

void PythonTuple::SetItemAtIndex(uint32_t index, const PythonObject &object) {
  if (IsAllocated() && object.IsValid()) {
    // PyTuple_SetItem is documented to "steal" a reference, so we need to
    // convert it to an owned reference by incrementing it.
    Py_INCREF(object.get());
    PyTuple_SetItem(m_py_obj, index, object.get());
  }
}

StructuredData::ArraySP PythonTuple::CreateStructuredArray() const {
  StructuredData::ArraySP result(new StructuredData::Array);
  uint32_t count = GetSize();
  for (uint32_t i = 0; i < count; ++i) {
    PythonObject obj = GetItemAtIndex(i);
    result->AddItem(obj.CreateStructuredObject());
  }
  return result;
}

// PythonDictionary

PythonDictionary::PythonDictionary(PyInitialValue value) {
  if (value == PyInitialValue::Empty)
    *this = Take<PythonDictionary>(PyDict_New());
}

bool PythonDictionary::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;

  return PyDict_Check(py_obj);
}

bool PythonDictionary::HasKey(const llvm::Twine &key) const {
  if (!IsValid())
    return false;

  PythonString key_object(key.isSingleStringRef() ? key.getSingleStringRef()
                                                  : key.str());

  if (int res = PyDict_Contains(m_py_obj, key_object.get()) > 0)
    return res;

  PyErr_Print();
  return false;
}

uint32_t PythonDictionary::GetSize() const {
  if (IsValid())
    return PyDict_Size(m_py_obj);
  return 0;
}

PythonList PythonDictionary::GetKeys() const {
  if (IsValid())
    return PythonList(PyRefType::Owned, PyDict_Keys(m_py_obj));
  return PythonList(PyInitialValue::Invalid);
}

PythonObject PythonDictionary::GetItemForKey(const PythonObject &key) const {
  auto item = GetItem(key);
  if (!item) {
    llvm::consumeError(item.takeError());
    return PythonObject();
  }
  return std::move(item.get());
}

Expected<PythonObject>
PythonDictionary::GetItem(const PythonObject &key) const {
  if (!IsValid())
    return nullDeref();
  PyObject *o = PyDict_GetItemWithError(m_py_obj, key.get());
  if (PyErr_Occurred())
    return exception();
  if (!o)
    return keyError();
  return Retain<PythonObject>(o);
}

Expected<PythonObject> PythonDictionary::GetItem(const Twine &key) const {
  if (!IsValid())
    return nullDeref();
  PyObject *o = PyDict_GetItemString(m_py_obj, NullTerminated(key));
  if (PyErr_Occurred())
    return exception();
  if (!o)
    return keyError();
  return Retain<PythonObject>(o);
}

Error PythonDictionary::SetItem(const PythonObject &key,
                                const PythonObject &value) const {
  if (!IsValid() || !value.IsValid())
    return nullDeref();
  int r = PyDict_SetItem(m_py_obj, key.get(), value.get());
  if (r < 0)
    return exception();
  return Error::success();
}

Error PythonDictionary::SetItem(const Twine &key,
                                const PythonObject &value) const {
  if (!IsValid() || !value.IsValid())
    return nullDeref();
  int r = PyDict_SetItemString(m_py_obj, NullTerminated(key), value.get());
  if (r < 0)
    return exception();
  return Error::success();
}

void PythonDictionary::SetItemForKey(const PythonObject &key,
                                     const PythonObject &value) {
  Error error = SetItem(key, value);
  if (error)
    llvm::consumeError(std::move(error));
}

StructuredData::DictionarySP
PythonDictionary::CreateStructuredDictionary() const {
  StructuredData::DictionarySP result(new StructuredData::Dictionary);
  PythonList keys(GetKeys());
  uint32_t num_keys = keys.GetSize();
  for (uint32_t i = 0; i < num_keys; ++i) {
    PythonObject key = keys.GetItemAtIndex(i);
    PythonObject value = GetItemForKey(key);
    StructuredData::ObjectSP structured_value = value.CreateStructuredObject();
    result->AddItem(key.Str().GetString(), structured_value);
  }
  return result;
}

PythonModule PythonModule::BuiltinsModule() { return AddModule("builtins"); }

PythonModule PythonModule::MainModule() { return AddModule("__main__"); }

PythonModule PythonModule::AddModule(llvm::StringRef module) {
  std::string str = module.str();
  return PythonModule(PyRefType::Borrowed, PyImport_AddModule(str.c_str()));
}

Expected<PythonModule> PythonModule::Import(const Twine &name) {
  PyObject *mod = PyImport_ImportModule(NullTerminated(name));
  if (!mod)
    return exception();
  return Take<PythonModule>(mod);
}

Expected<PythonObject> PythonModule::Get(const Twine &name) {
  if (!IsValid())
    return nullDeref();
  PyObject *dict = PyModule_GetDict(m_py_obj);
  if (!dict)
    return exception();
  PyObject *item = PyDict_GetItemString(dict, NullTerminated(name));
  if (!item)
    return exception();
  return Retain<PythonObject>(item);
}

bool PythonModule::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;

  return PyModule_Check(py_obj);
}

PythonDictionary PythonModule::GetDictionary() const {
  if (!IsValid())
    return PythonDictionary();
  return Retain<PythonDictionary>(PyModule_GetDict(m_py_obj));
}

bool PythonCallable::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;

  return PyCallable_Check(py_obj);
}

#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 3
static const char get_arg_info_script[] = R"(
from inspect import signature, Parameter, ismethod
from collections import namedtuple
ArgInfo = namedtuple('ArgInfo', ['count', 'has_varargs'])
def main(f):
    count = 0
    varargs = False
    for parameter in signature(f).parameters.values():
        kind = parameter.kind
        if kind in (Parameter.POSITIONAL_ONLY,
                    Parameter.POSITIONAL_OR_KEYWORD):
            count += 1
        elif kind == Parameter.VAR_POSITIONAL:
            varargs = True
        elif kind in (Parameter.KEYWORD_ONLY,
                      Parameter.VAR_KEYWORD):
            pass
        else:
            raise Exception(f'unknown parameter kind: {kind}')
    return ArgInfo(count, varargs)
)";
#endif

Expected<PythonCallable::ArgInfo> PythonCallable::GetArgInfo() const {
  ArgInfo result = {};
  if (!IsValid())
    return nullDeref();

#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 3

  // no need to synchronize access to this global, we already have the GIL
  static PythonScript get_arg_info(get_arg_info_script);
  Expected<PythonObject> pyarginfo = get_arg_info(*this);
  if (!pyarginfo)
    return pyarginfo.takeError();
  long long count =
      cantFail(As<long long>(pyarginfo.get().GetAttribute("count")));
  bool has_varargs =
      cantFail(As<bool>(pyarginfo.get().GetAttribute("has_varargs")));
  result.max_positional_args = has_varargs ? ArgInfo::UNBOUNDED : count;

#else
  PyObject *py_func_obj;
  bool is_bound_method = false;
  bool is_class = false;

  if (PyType_Check(m_py_obj) || PyClass_Check(m_py_obj)) {
    auto init = GetAttribute("__init__");
    if (!init)
      return init.takeError();
    py_func_obj = init.get().get();
    is_class = true;
  } else {
    py_func_obj = m_py_obj;
  }

  if (PyMethod_Check(py_func_obj)) {
    py_func_obj = PyMethod_GET_FUNCTION(py_func_obj);
    PythonObject im_self = GetAttributeValue("im_self");
    if (im_self.IsValid() && !im_self.IsNone())
      is_bound_method = true;
  } else {
    // see if this is a callable object with an __call__ method
    if (!PyFunction_Check(py_func_obj)) {
      PythonObject __call__ = GetAttributeValue("__call__");
      if (__call__.IsValid()) {
        auto __callable__ = __call__.AsType<PythonCallable>();
        if (__callable__.IsValid()) {
          py_func_obj = PyMethod_GET_FUNCTION(__callable__.get());
          PythonObject im_self = __callable__.GetAttributeValue("im_self");
          if (im_self.IsValid() && !im_self.IsNone())
            is_bound_method = true;
        }
      }
    }
  }

  if (!py_func_obj)
    return result;

  PyCodeObject *code = (PyCodeObject *)PyFunction_GET_CODE(py_func_obj);
  if (!code)
    return result;

  auto count = code->co_argcount;
  bool has_varargs = !!(code->co_flags & CO_VARARGS);
  result.max_positional_args =
      has_varargs ? ArgInfo::UNBOUNDED
                  : (count - (int)is_bound_method) - (int)is_class;

#endif

  return result;
}

constexpr unsigned
    PythonCallable::ArgInfo::UNBOUNDED; // FIXME delete after c++17

PythonObject PythonCallable::operator()() {
  return PythonObject(PyRefType::Owned, PyObject_CallObject(m_py_obj, nullptr));
}

PythonObject PythonCallable::
operator()(std::initializer_list<PyObject *> args) {
  PythonTuple arg_tuple(args);
  return PythonObject(PyRefType::Owned,
                      PyObject_CallObject(m_py_obj, arg_tuple.get()));
}

PythonObject PythonCallable::
operator()(std::initializer_list<PythonObject> args) {
  PythonTuple arg_tuple(args);
  return PythonObject(PyRefType::Owned,
                      PyObject_CallObject(m_py_obj, arg_tuple.get()));
}

bool PythonFile::Check(PyObject *py_obj) {
  if (!py_obj)
    return false;
  // In Python 3, there is no `PyFile_Check`, and in fact PyFile is not even a
  // first-class object type anymore.  `PyFile_FromFd` is just a thin wrapper
  // over `io.open()`, which returns some object derived from `io.IOBase`. As a
  // result, the only way to detect a file in Python 3 is to check whether it
  // inherits from `io.IOBase`.
  auto io_module = PythonModule::Import("io");
  if (!io_module) {
    llvm::consumeError(io_module.takeError());
    return false;
  }
  auto iobase = io_module.get().Get("IOBase");
  if (!iobase) {
    llvm::consumeError(iobase.takeError());
    return false;
  }
  int r = PyObject_IsInstance(py_obj, iobase.get().get());
  if (r < 0) {
    llvm::consumeError(exception()); // clear the exception and log it.
    return false;
  }
  return !!r;
}

const char *PythonException::toCString() const {
  if (!m_repr_bytes)
    return "unknown exception";
  return PyBytes_AS_STRING(m_repr_bytes);
}

PythonException::PythonException(const char *caller) {
  assert(PyErr_Occurred());
  m_exception_type = m_exception = m_traceback = m_repr_bytes = nullptr;
  PyErr_Fetch(&m_exception_type, &m_exception, &m_traceback);
  PyErr_NormalizeException(&m_exception_type, &m_exception, &m_traceback);
  PyErr_Clear();
  if (m_exception) {
    PyObject *repr = PyObject_Repr(m_exception);
    if (repr) {
      m_repr_bytes = PyUnicode_AsEncodedString(repr, "utf-8", nullptr);
      if (!m_repr_bytes) {
        PyErr_Clear();
      }
      Py_XDECREF(repr);
    } else {
      PyErr_Clear();
    }
  }
  Log *log = GetLog(LLDBLog::Script);
  if (caller)
    LLDB_LOGF(log, "%s failed with exception: %s", caller, toCString());
  else
    LLDB_LOGF(log, "python exception: %s", toCString());
}
void PythonException::Restore() {
  if (m_exception_type && m_exception) {
    PyErr_Restore(m_exception_type, m_exception, m_traceback);
  } else {
    PyErr_SetString(PyExc_Exception, toCString());
  }
  m_exception_type = m_exception = m_traceback = nullptr;
}

PythonException::~PythonException() {
  Py_XDECREF(m_exception_type);
  Py_XDECREF(m_exception);
  Py_XDECREF(m_traceback);
  Py_XDECREF(m_repr_bytes);
}

void PythonException::log(llvm::raw_ostream &OS) const { OS << toCString(); }

std::error_code PythonException::convertToErrorCode() const {
  return llvm::inconvertibleErrorCode();
}

bool PythonException::Matches(PyObject *exc) const {
  return PyErr_GivenExceptionMatches(m_exception_type, exc);
}

const char read_exception_script[] = R"(
import sys
from traceback import print_exception
if sys.version_info.major < 3:
  from StringIO import StringIO
else:
  from io import StringIO
def main(exc_type, exc_value, tb):
  f = StringIO()
  print_exception(exc_type, exc_value, tb, file=f)
  return f.getvalue()
)";

std::string PythonException::ReadBacktrace() const {

  if (!m_traceback)
    return toCString();

  // no need to synchronize access to this global, we already have the GIL
  static PythonScript read_exception(read_exception_script);

  Expected<std::string> backtrace = As<std::string>(
      read_exception(m_exception_type, m_exception, m_traceback));

  if (!backtrace) {
    std::string message =
        std::string(toCString()) + "\n" +
        "Traceback unavailable, an error occurred while reading it:\n";
    return (message + llvm::toString(backtrace.takeError()));
  }

  return std::move(backtrace.get());
}

char PythonException::ID = 0;

llvm::Expected<File::OpenOptions>
GetOptionsForPyObject(const PythonObject &obj) {
  auto options = File::OpenOptions(0);
  auto readable = As<bool>(obj.CallMethod("readable"));
  if (!readable)
    return readable.takeError();
  auto writable = As<bool>(obj.CallMethod("writable"));
  if (!writable)
    return writable.takeError();
  if (readable.get() && writable.get())
    options |= File::eOpenOptionReadWrite;
  else if (writable.get())
    options |= File::eOpenOptionWriteOnly;
  else if (readable.get())
    options |= File::eOpenOptionReadOnly;
  return options;
}

// Base class template for python files.   All it knows how to do
// is hold a reference to the python object and close or flush it
// when the File is closed.
namespace {
template <typename Base> class OwnedPythonFile : public Base {
public:
  template <typename... Args>
  OwnedPythonFile(const PythonFile &file, bool borrowed, Args... args)
      : Base(args...), m_py_obj(file), m_borrowed(borrowed) {
    assert(m_py_obj);
  }

  ~OwnedPythonFile() override {
    assert(m_py_obj);
    GIL takeGIL;
    Close();
    // we need to ensure the python object is released while we still
    // hold the GIL
    m_py_obj.Reset();
  }

  bool IsPythonSideValid() const {
    GIL takeGIL;
    auto closed = As<bool>(m_py_obj.GetAttribute("closed"));
    if (!closed) {
      llvm::consumeError(closed.takeError());
      return false;
    }
    return !closed.get();
  }

  bool IsValid() const override {
    return IsPythonSideValid() && Base::IsValid();
  }

  Status Close() override {
    assert(m_py_obj);
    Status py_error, base_error;
    GIL takeGIL;
    if (!m_borrowed) {
      auto r = m_py_obj.CallMethod("close");
      if (!r)
        py_error = Status(r.takeError());
    }
    base_error = Base::Close();
    if (py_error.Fail())
      return py_error;
    return base_error;
  };

  PyObject *GetPythonObject() const {
    assert(m_py_obj.IsValid());
    return m_py_obj.get();
  }

  static bool classof(const File *file) = delete;

protected:
  PythonFile m_py_obj;
  bool m_borrowed;
};
} // namespace

// A SimplePythonFile is a OwnedPythonFile that just does all I/O as
// a NativeFile
namespace {
class SimplePythonFile : public OwnedPythonFile<NativeFile> {
public:
  SimplePythonFile(const PythonFile &file, bool borrowed, int fd,
                   File::OpenOptions options)
      : OwnedPythonFile(file, borrowed, fd, options, false) {}

  static char ID;
  bool isA(const void *classID) const override {
    return classID == &ID || NativeFile::isA(classID);
  }
  static bool classof(const File *file) { return file->isA(&ID); }
};
char SimplePythonFile::ID = 0;
} // namespace

namespace {
class PythonBuffer {
public:
  PythonBuffer &operator=(const PythonBuffer &) = delete;
  PythonBuffer(const PythonBuffer &) = delete;

  static Expected<PythonBuffer> Create(PythonObject &obj,
                                       int flags = PyBUF_SIMPLE) {
    Py_buffer py_buffer = {};
    PyObject_GetBuffer(obj.get(), &py_buffer, flags);
    if (!py_buffer.obj)
      return llvm::make_error<PythonException>();
    return PythonBuffer(py_buffer);
  }

  PythonBuffer(PythonBuffer &&other) {
    m_buffer = other.m_buffer;
    other.m_buffer.obj = nullptr;
  }

  ~PythonBuffer() {
    if (m_buffer.obj)
      PyBuffer_Release(&m_buffer);
  }

  Py_buffer &get() { return m_buffer; }

private:
  // takes ownership of the buffer.
  PythonBuffer(const Py_buffer &py_buffer) : m_buffer(py_buffer) {}
  Py_buffer m_buffer;
};
} // namespace

// Shared methods between TextPythonFile and BinaryPythonFile
namespace {
class PythonIOFile : public OwnedPythonFile<File> {
public:
  PythonIOFile(const PythonFile &file, bool borrowed)
      : OwnedPythonFile(file, borrowed) {}

  ~PythonIOFile() override { Close(); }

  bool IsValid() const override { return IsPythonSideValid(); }

  Status Close() override {
    assert(m_py_obj);
    GIL takeGIL;
    if (m_borrowed)
      return Flush();
    auto r = m_py_obj.CallMethod("close");
    if (!r)
      return Status(r.takeError());
    return Status();
  }

  Status Flush() override {
    GIL takeGIL;
    auto r = m_py_obj.CallMethod("flush");
    if (!r)
      return Status(r.takeError());
    return Status();
  }

  Expected<File::OpenOptions> GetOptions() const override {
    GIL takeGIL;
    return GetOptionsForPyObject(m_py_obj);
  }

  static char ID;
  bool isA(const void *classID) const override {
    return classID == &ID || File::isA(classID);
  }
  static bool classof(const File *file) { return file->isA(&ID); }
};
char PythonIOFile::ID = 0;
} // namespace

namespace {
class BinaryPythonFile : public PythonIOFile {
protected:
  int m_descriptor;

public:
  BinaryPythonFile(int fd, const PythonFile &file, bool borrowed)
      : PythonIOFile(file, borrowed),
        m_descriptor(File::DescriptorIsValid(fd) ? fd
                                                 : File::kInvalidDescriptor) {}

  int GetDescriptor() const override { return m_descriptor; }

  Status Write(const void *buf, size_t &num_bytes) override {
    GIL takeGIL;
    PyObject *pybuffer_p = PyMemoryView_FromMemory(
        const_cast<char *>((const char *)buf), num_bytes, PyBUF_READ);
    if (!pybuffer_p)
      return Status(llvm::make_error<PythonException>());
    auto pybuffer = Take<PythonObject>(pybuffer_p);
    num_bytes = 0;
    auto bytes_written = As<long long>(m_py_obj.CallMethod("write", pybuffer));
    if (!bytes_written)
      return Status(bytes_written.takeError());
    if (bytes_written.get() < 0)
      return Status(".write() method returned a negative number!");
    static_assert(sizeof(long long) >= sizeof(size_t), "overflow");
    num_bytes = bytes_written.get();
    return Status();
  }

  Status Read(void *buf, size_t &num_bytes) override {
    GIL takeGIL;
    static_assert(sizeof(long long) >= sizeof(size_t), "overflow");
    auto pybuffer_obj =
        m_py_obj.CallMethod("read", (unsigned long long)num_bytes);
    if (!pybuffer_obj)
      return Status(pybuffer_obj.takeError());
    num_bytes = 0;
    if (pybuffer_obj.get().IsNone()) {
      // EOF
      num_bytes = 0;
      return Status();
    }
    auto pybuffer = PythonBuffer::Create(pybuffer_obj.get());
    if (!pybuffer)
      return Status(pybuffer.takeError());
    memcpy(buf, pybuffer.get().get().buf, pybuffer.get().get().len);
    num_bytes = pybuffer.get().get().len;
    return Status();
  }
};
} // namespace

namespace {
class TextPythonFile : public PythonIOFile {
protected:
  int m_descriptor;

public:
  TextPythonFile(int fd, const PythonFile &file, bool borrowed)
      : PythonIOFile(file, borrowed),
        m_descriptor(File::DescriptorIsValid(fd) ? fd
                                                 : File::kInvalidDescriptor) {}

  int GetDescriptor() const override { return m_descriptor; }

  Status Write(const void *buf, size_t &num_bytes) override {
    GIL takeGIL;
    auto pystring =
        PythonString::FromUTF8(llvm::StringRef((const char *)buf, num_bytes));
    if (!pystring)
      return Status(pystring.takeError());
    num_bytes = 0;
    auto bytes_written =
        As<long long>(m_py_obj.CallMethod("write", pystring.get()));
    if (!bytes_written)
      return Status(bytes_written.takeError());
    if (bytes_written.get() < 0)
      return Status(".write() method returned a negative number!");
    static_assert(sizeof(long long) >= sizeof(size_t), "overflow");
    num_bytes = bytes_written.get();
    return Status();
  }

  Status Read(void *buf, size_t &num_bytes) override {
    GIL takeGIL;
    size_t num_chars = num_bytes / 6;
    size_t orig_num_bytes = num_bytes;
    num_bytes = 0;
    if (orig_num_bytes < 6) {
      return Status("can't read less than 6 bytes from a utf8 text stream");
    }
    auto pystring = As<PythonString>(
        m_py_obj.CallMethod("read", (unsigned long long)num_chars));
    if (!pystring)
      return Status(pystring.takeError());
    if (pystring.get().IsNone()) {
      // EOF
      return Status();
    }
    auto stringref = pystring.get().AsUTF8();
    if (!stringref)
      return Status(stringref.takeError());
    num_bytes = stringref.get().size();
    memcpy(buf, stringref.get().begin(), num_bytes);
    return Status();
  }
};
} // namespace

llvm::Expected<FileSP> PythonFile::ConvertToFile(bool borrowed) {
  if (!IsValid())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid PythonFile");

  int fd = PyObject_AsFileDescriptor(m_py_obj);
  if (fd < 0) {
    PyErr_Clear();
    return ConvertToFileForcingUseOfScriptingIOMethods(borrowed);
  }
  auto options = GetOptionsForPyObject(*this);
  if (!options)
    return options.takeError();

  File::OpenOptions rw =
      options.get() & (File::eOpenOptionReadOnly | File::eOpenOptionWriteOnly |
                       File::eOpenOptionReadWrite);
  if (rw == File::eOpenOptionWriteOnly || rw == File::eOpenOptionReadWrite) {
    // LLDB and python will not share I/O buffers.  We should probably
    // flush the python buffers now.
    auto r = CallMethod("flush");
    if (!r)
      return r.takeError();
  }

  FileSP file_sp;
  if (borrowed) {
    // In this case we don't need to retain the python
    // object at all.
    file_sp = std::make_shared<NativeFile>(fd, options.get(), false);
  } else {
    file_sp = std::static_pointer_cast<File>(
        std::make_shared<SimplePythonFile>(*this, borrowed, fd, options.get()));
  }
  if (!file_sp->IsValid())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid File");

  return file_sp;
}

llvm::Expected<FileSP>
PythonFile::ConvertToFileForcingUseOfScriptingIOMethods(bool borrowed) {

  assert(!PyErr_Occurred());

  if (!IsValid())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid PythonFile");

  int fd = PyObject_AsFileDescriptor(m_py_obj);
  if (fd < 0) {
    PyErr_Clear();
    fd = File::kInvalidDescriptor;
  }

  auto io_module = PythonModule::Import("io");
  if (!io_module)
    return io_module.takeError();
  auto textIOBase = io_module.get().Get("TextIOBase");
  if (!textIOBase)
    return textIOBase.takeError();
  auto rawIOBase = io_module.get().Get("RawIOBase");
  if (!rawIOBase)
    return rawIOBase.takeError();
  auto bufferedIOBase = io_module.get().Get("BufferedIOBase");
  if (!bufferedIOBase)
    return bufferedIOBase.takeError();

  FileSP file_sp;

  auto isTextIO = IsInstance(textIOBase.get());
  if (!isTextIO)
    return isTextIO.takeError();
  if (isTextIO.get())
    file_sp = std::static_pointer_cast<File>(
        std::make_shared<TextPythonFile>(fd, *this, borrowed));

  auto isRawIO = IsInstance(rawIOBase.get());
  if (!isRawIO)
    return isRawIO.takeError();
  auto isBufferedIO = IsInstance(bufferedIOBase.get());
  if (!isBufferedIO)
    return isBufferedIO.takeError();

  if (isRawIO.get() || isBufferedIO.get()) {
    file_sp = std::static_pointer_cast<File>(
        std::make_shared<BinaryPythonFile>(fd, *this, borrowed));
  }

  if (!file_sp)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "python file is neither text nor binary");

  if (!file_sp->IsValid())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid File");

  return file_sp;
}

Expected<PythonFile> PythonFile::FromFile(File &file, const char *mode) {
  if (!file.IsValid())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid file");

  if (auto *simple = llvm::dyn_cast<SimplePythonFile>(&file))
    return Retain<PythonFile>(simple->GetPythonObject());
  if (auto *pythonio = llvm::dyn_cast<PythonIOFile>(&file))
    return Retain<PythonFile>(pythonio->GetPythonObject());

  if (!mode) {
    auto m = file.GetOpenMode();
    if (!m)
      return m.takeError();
    mode = m.get();
  }

  PyObject *file_obj;
  file_obj = PyFile_FromFd(file.GetDescriptor(), nullptr, mode, -1, nullptr,
                           "ignore", nullptr, /*closefd=*/0);

  if (!file_obj)
    return exception();

  return Take<PythonFile>(file_obj);
}

Error PythonScript::Init() {
  if (function.IsValid())
    return Error::success();

  PythonDictionary globals(PyInitialValue::Empty);
  auto builtins = PythonModule::BuiltinsModule();
  if (Error error = globals.SetItem("__builtins__", builtins))
    return error;
  PyObject *o =
      PyRun_String(script, Py_file_input, globals.get(), globals.get());
  if (!o)
    return exception();
  Take<PythonObject>(o);
  auto f = As<PythonCallable>(globals.GetItem("main"));
  if (!f)
    return f.takeError();
  function = std::move(f.get());

  return Error::success();
}

llvm::Expected<PythonObject>
python::runStringOneLine(const llvm::Twine &string,
                         const PythonDictionary &globals,
                         const PythonDictionary &locals) {
  if (!globals.IsValid() || !locals.IsValid())
    return nullDeref();

  PyObject *code =
      Py_CompileString(NullTerminated(string), "<string>", Py_eval_input);
  if (!code) {
    PyErr_Clear();
    code =
        Py_CompileString(NullTerminated(string), "<string>", Py_single_input);
  }
  if (!code)
    return exception();
  auto code_ref = Take<PythonObject>(code);

  PyObject *result = PyEval_EvalCode(code, globals.get(), locals.get());

  if (!result)
    return exception();

  return Take<PythonObject>(result);
}

llvm::Expected<PythonObject>
python::runStringMultiLine(const llvm::Twine &string,
                           const PythonDictionary &globals,
                           const PythonDictionary &locals) {
  if (!globals.IsValid() || !locals.IsValid())
    return nullDeref();
  PyObject *result = PyRun_String(NullTerminated(string), Py_file_input,
                                  globals.get(), locals.get());
  if (!result)
    return exception();
  return Take<PythonObject>(result);
}

#endif
