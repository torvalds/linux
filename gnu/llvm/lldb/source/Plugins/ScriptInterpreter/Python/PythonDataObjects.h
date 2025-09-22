//===-- PythonDataObjects.h--------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//
// !! FIXME FIXME FIXME !!
//
// Python APIs nearly all can return an exception.   They do this
// by returning NULL, or -1, or some such value and setting
// the exception state with PyErr_Set*().   Exceptions must be
// handled before further python API functions are called.   Failure
// to do so will result in asserts on debug builds of python.
// It will also sometimes, but not usually result in crashes of
// release builds.
//
// Nearly all the code in this header does not handle python exceptions
// correctly.  It should all be converted to return Expected<> or
// Error types to capture the exception.
//
// Everything in this file except functions that return Error or
// Expected<> is considered deprecated and should not be
// used in new code.  If you need to use it, fix it first.
//
//
// TODOs for this file
//
// * Make all methods safe for exceptions.
//
// * Eliminate method signatures that must translate exceptions into
//   empty objects or NULLs.   Almost everything here should return
//   Expected<>.   It should be acceptable for certain operations that
//   can never fail to assert instead, such as the creation of
//   PythonString from a string literal.
//
// * Eliminate Reset(), and make all non-default constructors private.
//   Python objects should be created with Retain<> or Take<>, and they
//   should be assigned with operator=
//
// * Eliminate default constructors, make python objects always
//   nonnull, and use optionals where necessary.
//


#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_PYTHONDATAOBJECTS_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_PYTHONDATAOBJECTS_H

#include "lldb/Host/Config.h"

#if LLDB_ENABLE_PYTHON

// LLDB Python header must be included first
#include "lldb-python.h"

#include "lldb/Host/File.h"
#include "lldb/Utility/StructuredData.h"

#include "llvm/ADT/ArrayRef.h"

namespace lldb_private {
namespace python {

class PythonObject;
class PythonBytes;
class PythonString;
class PythonList;
class PythonDictionary;
class PythonInteger;
class PythonException;

class GIL {
public:
  GIL() {
    m_state = PyGILState_Ensure();
    assert(!PyErr_Occurred());
  }
  ~GIL() { PyGILState_Release(m_state); }

protected:
  PyGILState_STATE m_state;
};

enum class PyObjectType {
  Unknown,
  None,
  Boolean,
  Integer,
  Dictionary,
  List,
  String,
  Bytes,
  ByteArray,
  Module,
  Callable,
  Tuple,
  File
};

enum class PyRefType {
  Borrowed, // We are not given ownership of the incoming PyObject.
            // We cannot safely hold it without calling Py_INCREF.
  Owned     // We have ownership of the incoming PyObject.  We should
            // not call Py_INCREF.
};


// Take a reference that you already own, and turn it into
// a PythonObject.
//
// Most python API methods will return a +1 reference
// if they succeed or NULL if and only if
// they set an exception.   Use this to collect such return
// values, after checking for NULL.
//
// If T is not just PythonObject, then obj must be already be
// checked to be of the correct type.
template <typename T> T Take(PyObject *obj) {
  assert(obj);
  assert(!PyErr_Occurred());
  T thing(PyRefType::Owned, obj);
  assert(thing.IsValid());
  return thing;
}

// Retain a reference you have borrowed, and turn it into
// a PythonObject.
//
// A minority of python APIs return a borrowed reference
// instead of a +1.   They will also return NULL if and only
// if they set an exception.   Use this to collect such return
// values, after checking for NULL.
//
// If T is not just PythonObject, then obj must be already be
// checked to be of the correct type.
template <typename T> T Retain(PyObject *obj) {
  assert(obj);
  assert(!PyErr_Occurred());
  T thing(PyRefType::Borrowed, obj);
  assert(thing.IsValid());
  return thing;
}

// This class can be used like a utility function to convert from
// a llvm-friendly Twine into a null-terminated const char *,
// which is the form python C APIs want their strings in.
//
// Example:
// const llvm::Twine &some_twine;
// PyFoo_Bar(x, y, z, NullTerminated(some_twine));
//
// Why a class instead of a function?  If the twine isn't already null
// terminated, it will need a temporary buffer to copy the string
// into.   We need that buffer to stick around for the lifetime of the
// statement.
class NullTerminated {
  const char *str;
  llvm::SmallString<32> storage;

public:
  NullTerminated(const llvm::Twine &twine) {
    llvm::StringRef ref = twine.toNullTerminatedStringRef(storage);
    str = ref.begin();
  }
  operator const char *() { return str; }
};

inline llvm::Error nullDeref() {
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "A NULL PyObject* was dereferenced");
}

inline llvm::Error exception(const char *s = nullptr) {
  return llvm::make_error<PythonException>(s);
}

inline llvm::Error keyError() {
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "key not in dict");
}

inline const char *py2_const_cast(const char *s) { return s; }

enum class PyInitialValue { Invalid, Empty };

// DOC: https://docs.python.org/3/c-api/arg.html#building-values
template <typename T, typename Enable = void> struct PythonFormat;

template <typename T, char F> struct PassthroughFormat {
  static constexpr char format = F;
  static constexpr T get(T t) { return t; }
};

template <> struct PythonFormat<char *> : PassthroughFormat<char *, 's'> {};
template <> struct PythonFormat<const char *> : 
    PassthroughFormat<const char *, 's'> {};
template <> struct PythonFormat<char> : PassthroughFormat<char, 'b'> {};
template <>
struct PythonFormat<unsigned char> : PassthroughFormat<unsigned char, 'B'> {};
template <> struct PythonFormat<short> : PassthroughFormat<short, 'h'> {};
template <>
struct PythonFormat<unsigned short> : PassthroughFormat<unsigned short, 'H'> {};
template <> struct PythonFormat<int> : PassthroughFormat<int, 'i'> {};
template <> struct PythonFormat<bool> : PassthroughFormat<bool, 'p'> {};
template <>
struct PythonFormat<unsigned int> : PassthroughFormat<unsigned int, 'I'> {};
template <> struct PythonFormat<long> : PassthroughFormat<long, 'l'> {};
template <>
struct PythonFormat<unsigned long> : PassthroughFormat<unsigned long, 'k'> {};
template <>
struct PythonFormat<long long> : PassthroughFormat<long long, 'L'> {};
template <>
struct PythonFormat<unsigned long long>
    : PassthroughFormat<unsigned long long, 'K'> {};
template <>
struct PythonFormat<PyObject *> : PassthroughFormat<PyObject *, 'O'> {};

template <typename T>
struct PythonFormat<
    T, typename std::enable_if<std::is_base_of<PythonObject, T>::value>::type> {
  static constexpr char format = 'O';
  static auto get(const T &value) { return value.get(); }
};

class PythonObject {
public:
  PythonObject() = default;

  PythonObject(PyRefType type, PyObject *py_obj) {
    m_py_obj = py_obj;
    // If this is a borrowed reference, we need to convert it to
    // an owned reference by incrementing it.  If it is an owned
    // reference (for example the caller allocated it with PyDict_New()
    // then we must *not* increment it.
    if (m_py_obj && Py_IsInitialized() && type == PyRefType::Borrowed)
      Py_XINCREF(m_py_obj);
  }

  PythonObject(const PythonObject &rhs)
      : PythonObject(PyRefType::Borrowed, rhs.m_py_obj) {}

  PythonObject(PythonObject &&rhs) {
    m_py_obj = rhs.m_py_obj;
    rhs.m_py_obj = nullptr;
  }

  ~PythonObject() { Reset(); }

  void Reset();

  void Dump() const {
    if (m_py_obj)
      _PyObject_Dump(m_py_obj);
    else
      puts("NULL");
  }

  void Dump(Stream &strm) const;

  PyObject *get() const { return m_py_obj; }

  PyObject *release() {
    PyObject *result = m_py_obj;
    m_py_obj = nullptr;
    return result;
  }

  PythonObject &operator=(PythonObject other) {
    Reset();
    m_py_obj = std::exchange(other.m_py_obj, nullptr);
    return *this;
  }

  PyObjectType GetObjectType() const;

  PythonString Repr() const;

  PythonString Str() const;

  static PythonObject ResolveNameWithDictionary(llvm::StringRef name,
                                                const PythonDictionary &dict);

  template <typename T>
  static T ResolveNameWithDictionary(llvm::StringRef name,
                                     const PythonDictionary &dict) {
    return ResolveNameWithDictionary(name, dict).AsType<T>();
  }

  PythonObject ResolveName(llvm::StringRef name) const;

  template <typename T> T ResolveName(llvm::StringRef name) const {
    return ResolveName(name).AsType<T>();
  }

  bool HasAttribute(llvm::StringRef attribute) const;

  PythonObject GetAttributeValue(llvm::StringRef attribute) const;

  bool IsNone() const { return m_py_obj == Py_None; }

  bool IsValid() const { return m_py_obj != nullptr; }

  bool IsAllocated() const { return IsValid() && !IsNone(); }

  explicit operator bool() const { return IsValid() && !IsNone(); }

  template <typename T> T AsType() const {
    if (!T::Check(m_py_obj))
      return T();
    return T(PyRefType::Borrowed, m_py_obj);
  }

  StructuredData::ObjectSP CreateStructuredObject() const;

  template <typename... T>
  llvm::Expected<PythonObject> CallMethod(const char *name,
                                          const T &... t) const {
    const char format[] = {'(', PythonFormat<T>::format..., ')', 0};
    PyObject *obj =
        PyObject_CallMethod(m_py_obj, py2_const_cast(name),
                            py2_const_cast(format), PythonFormat<T>::get(t)...);
    if (!obj)
      return exception();
    return python::Take<PythonObject>(obj);
  }

  template <typename... T>
  llvm::Expected<PythonObject> Call(const T &... t) const {
    const char format[] = {'(', PythonFormat<T>::format..., ')', 0};
    PyObject *obj = PyObject_CallFunction(m_py_obj, py2_const_cast(format),
                                          PythonFormat<T>::get(t)...);
    if (!obj)
      return exception();
    return python::Take<PythonObject>(obj);
  }

  llvm::Expected<PythonObject> GetAttribute(const llvm::Twine &name) const {
    if (!m_py_obj)
      return nullDeref();
    PyObject *obj = PyObject_GetAttrString(m_py_obj, NullTerminated(name));
    if (!obj)
      return exception();
    return python::Take<PythonObject>(obj);
  }

  llvm::Expected<PythonObject> GetType() const {
    if (!m_py_obj)
      return nullDeref();
    PyObject *obj = PyObject_Type(m_py_obj);
    if (!obj)
      return exception();
    return python::Take<PythonObject>(obj);
  }

  llvm::Expected<bool> IsTrue() {
    if (!m_py_obj)
      return nullDeref();
    int r = PyObject_IsTrue(m_py_obj);
    if (r < 0)
      return exception();
    return !!r;
  }

  llvm::Expected<long long> AsLongLong() const;

  llvm::Expected<unsigned long long> AsUnsignedLongLong() const;

  // wraps on overflow, instead of raising an error.
  llvm::Expected<unsigned long long> AsModuloUnsignedLongLong() const;

  llvm::Expected<bool> IsInstance(const PythonObject &cls) {
    if (!m_py_obj || !cls.IsValid())
      return nullDeref();
    int r = PyObject_IsInstance(m_py_obj, cls.get());
    if (r < 0)
      return exception();
    return !!r;
  }

protected:
  PyObject *m_py_obj = nullptr;
};


// This is why C++ needs monads.
template <typename T> llvm::Expected<T> As(llvm::Expected<PythonObject> &&obj) {
  if (!obj)
    return obj.takeError();
  if (!T::Check(obj.get().get()))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "type error");
  return T(PyRefType::Borrowed, std::move(obj.get().get()));
}

template <> llvm::Expected<bool> As<bool>(llvm::Expected<PythonObject> &&obj);

template <>
llvm::Expected<long long> As<long long>(llvm::Expected<PythonObject> &&obj);

template <>
llvm::Expected<unsigned long long>
As<unsigned long long>(llvm::Expected<PythonObject> &&obj);

template <>
llvm::Expected<std::string> As<std::string>(llvm::Expected<PythonObject> &&obj);


template <class T> class TypedPythonObject : public PythonObject {
public:
  TypedPythonObject(PyRefType type, PyObject *py_obj) {
    if (!py_obj)
      return;
    if (T::Check(py_obj))
      PythonObject::operator=(PythonObject(type, py_obj));
    else if (type == PyRefType::Owned)
      Py_DECREF(py_obj);
  }

  TypedPythonObject() = default;
};

class PythonBytes : public TypedPythonObject<PythonBytes> {
public:
  using TypedPythonObject::TypedPythonObject;
  explicit PythonBytes(llvm::ArrayRef<uint8_t> bytes);
  PythonBytes(const uint8_t *bytes, size_t length);

  static bool Check(PyObject *py_obj);

  llvm::ArrayRef<uint8_t> GetBytes() const;

  size_t GetSize() const;

  void SetBytes(llvm::ArrayRef<uint8_t> stringbytes);

  StructuredData::StringSP CreateStructuredString() const;
};

class PythonByteArray : public TypedPythonObject<PythonByteArray> {
public:
  using TypedPythonObject::TypedPythonObject;
  explicit PythonByteArray(llvm::ArrayRef<uint8_t> bytes);
  PythonByteArray(const uint8_t *bytes, size_t length);
  PythonByteArray(const PythonBytes &object);

  static bool Check(PyObject *py_obj);

  llvm::ArrayRef<uint8_t> GetBytes() const;

  size_t GetSize() const;

  void SetBytes(llvm::ArrayRef<uint8_t> stringbytes);

  StructuredData::StringSP CreateStructuredString() const;
};

class PythonString : public TypedPythonObject<PythonString> {
public:
  using TypedPythonObject::TypedPythonObject;
  static llvm::Expected<PythonString> FromUTF8(llvm::StringRef string);

  PythonString() : TypedPythonObject() {} // MSVC requires this for some reason

  explicit PythonString(llvm::StringRef string); // safe, null on error

  static bool Check(PyObject *py_obj);

  llvm::StringRef GetString() const; // safe, empty string on error

  llvm::Expected<llvm::StringRef> AsUTF8() const;

  size_t GetSize() const;

  void SetString(llvm::StringRef string); // safe, null on error

  StructuredData::StringSP CreateStructuredString() const;
};

class PythonInteger : public TypedPythonObject<PythonInteger> {
public:
  using TypedPythonObject::TypedPythonObject;

  PythonInteger() : TypedPythonObject() {} // MSVC requires this for some reason

  explicit PythonInteger(int64_t value);

  static bool Check(PyObject *py_obj);

  void SetInteger(int64_t value);

  StructuredData::IntegerSP CreateStructuredInteger() const;

  StructuredData::UnsignedIntegerSP CreateStructuredUnsignedInteger() const;

  StructuredData::SignedIntegerSP CreateStructuredSignedInteger() const;
};

class PythonBoolean : public TypedPythonObject<PythonBoolean> {
public:
  using TypedPythonObject::TypedPythonObject;

  explicit PythonBoolean(bool value);

  static bool Check(PyObject *py_obj);

  bool GetValue() const;

  void SetValue(bool value);

  StructuredData::BooleanSP CreateStructuredBoolean() const;
};

class PythonList : public TypedPythonObject<PythonList> {
public:
  using TypedPythonObject::TypedPythonObject;

  PythonList() : TypedPythonObject() {} // MSVC requires this for some reason

  explicit PythonList(PyInitialValue value);
  explicit PythonList(int list_size);

  static bool Check(PyObject *py_obj);

  uint32_t GetSize() const;

  PythonObject GetItemAtIndex(uint32_t index) const;

  void SetItemAtIndex(uint32_t index, const PythonObject &object);

  void AppendItem(const PythonObject &object);

  StructuredData::ArraySP CreateStructuredArray() const;
};

class PythonTuple : public TypedPythonObject<PythonTuple> {
public:
  using TypedPythonObject::TypedPythonObject;

  explicit PythonTuple(PyInitialValue value);
  explicit PythonTuple(int tuple_size);
  PythonTuple(std::initializer_list<PythonObject> objects);
  PythonTuple(std::initializer_list<PyObject *> objects);

  static bool Check(PyObject *py_obj);

  uint32_t GetSize() const;

  PythonObject GetItemAtIndex(uint32_t index) const;

  void SetItemAtIndex(uint32_t index, const PythonObject &object);

  StructuredData::ArraySP CreateStructuredArray() const;
};

class PythonDictionary : public TypedPythonObject<PythonDictionary> {
public:
  using TypedPythonObject::TypedPythonObject;

  PythonDictionary() : TypedPythonObject() {} // MSVC requires this for some reason

  explicit PythonDictionary(PyInitialValue value);

  static bool Check(PyObject *py_obj);

  bool HasKey(const llvm::Twine &key) const;

  uint32_t GetSize() const;

  PythonList GetKeys() const;

  PythonObject GetItemForKey(const PythonObject &key) const; // DEPRECATED
  void SetItemForKey(const PythonObject &key,
                     const PythonObject &value); // DEPRECATED

  llvm::Expected<PythonObject> GetItem(const PythonObject &key) const;
  llvm::Expected<PythonObject> GetItem(const llvm::Twine &key) const;
  llvm::Error SetItem(const PythonObject &key, const PythonObject &value) const;
  llvm::Error SetItem(const llvm::Twine &key, const PythonObject &value) const;

  StructuredData::DictionarySP CreateStructuredDictionary() const;
};

class PythonModule : public TypedPythonObject<PythonModule> {
public:
  using TypedPythonObject::TypedPythonObject;

  static bool Check(PyObject *py_obj);

  static PythonModule BuiltinsModule();

  static PythonModule MainModule();

  static PythonModule AddModule(llvm::StringRef module);

  // safe, returns invalid on error;
  static PythonModule ImportModule(llvm::StringRef name) {
    std::string s = std::string(name);
    auto mod = Import(s.c_str());
    if (!mod) {
      llvm::consumeError(mod.takeError());
      return PythonModule();
    }
    return std::move(mod.get());
  }

  static llvm::Expected<PythonModule> Import(const llvm::Twine &name);

  llvm::Expected<PythonObject> Get(const llvm::Twine &name);

  PythonDictionary GetDictionary() const;
};

class PythonCallable : public TypedPythonObject<PythonCallable> {
public:
  using TypedPythonObject::TypedPythonObject;

  struct ArgInfo {
    /* the largest number of positional arguments this callable
     * can accept, or UNBOUNDED, ie UINT_MAX if it's a varargs
     * function and can accept an arbitrary number */
    unsigned max_positional_args;
    static constexpr unsigned UNBOUNDED = UINT_MAX; // FIXME c++17 inline
  };

  static bool Check(PyObject *py_obj);

  llvm::Expected<ArgInfo> GetArgInfo() const;

  PythonObject operator()();

  PythonObject operator()(std::initializer_list<PyObject *> args);

  PythonObject operator()(std::initializer_list<PythonObject> args);

  template <typename Arg, typename... Args>
  PythonObject operator()(const Arg &arg, Args... args) {
    return operator()({arg, args...});
  }
};

class PythonFile : public TypedPythonObject<PythonFile> {
public:
  using TypedPythonObject::TypedPythonObject;

  PythonFile() : TypedPythonObject() {} // MSVC requires this for some reason

  static bool Check(PyObject *py_obj);

  static llvm::Expected<PythonFile> FromFile(File &file,
                                             const char *mode = nullptr);

  llvm::Expected<lldb::FileSP> ConvertToFile(bool borrowed = false);
  llvm::Expected<lldb::FileSP>
  ConvertToFileForcingUseOfScriptingIOMethods(bool borrowed = false);
};

class PythonException : public llvm::ErrorInfo<PythonException> {
private:
  PyObject *m_exception_type, *m_exception, *m_traceback;
  PyObject *m_repr_bytes;

public:
  static char ID;
  const char *toCString() const;
  PythonException(const char *caller = nullptr);
  void Restore();
  ~PythonException() override;
  void log(llvm::raw_ostream &OS) const override;
  std::error_code convertToErrorCode() const override;
  bool Matches(PyObject *exc) const;
  std::string ReadBacktrace() const;
};

// This extracts the underlying T out of an Expected<T> and returns it.
// If the Expected is an Error instead of a T, that error will be converted
// into a python exception, and this will return a default-constructed T.
//
// This is appropriate for use right at the boundary of python calling into
// C++, such as in a SWIG typemap.   In such a context you should simply
// check if the returned T is valid, and if it is, return a NULL back
// to python.   This will result in the Error being raised as an exception
// from python code's point of view.
//
// For example:
// ```
// Expected<Foo *> efoop = some_cpp_function();
// Foo *foop = unwrapOrSetPythonException(efoop);
// if (!foop)
//    return NULL;
// do_something(*foop);
//
// If the Error returned was itself created because a python exception was
// raised when C++ code called into python, then the original exception
// will be restored.   Otherwise a simple string exception will be raised.
template <typename T> T unwrapOrSetPythonException(llvm::Expected<T> expected) {
  if (expected)
    return expected.get();
  llvm::handleAllErrors(
      expected.takeError(), [](PythonException &E) { E.Restore(); },
      [](const llvm::ErrorInfoBase &E) {
        PyErr_SetString(PyExc_Exception, E.message().c_str());
      });
  return T();
}

// This is only here to help incrementally migrate old, exception-unsafe
// code.
template <typename T> T unwrapIgnoringErrors(llvm::Expected<T> expected) {
  if (expected)
    return std::move(expected.get());
  llvm::consumeError(expected.takeError());
  return T();
}

llvm::Expected<PythonObject> runStringOneLine(const llvm::Twine &string,
                                              const PythonDictionary &globals,
                                              const PythonDictionary &locals);

llvm::Expected<PythonObject> runStringMultiLine(const llvm::Twine &string,
                                                const PythonDictionary &globals,
                                                const PythonDictionary &locals);

// Sometimes the best way to interact with a python interpreter is
// to run some python code.   You construct a PythonScript with
// script string.   The script assigns some function to `_function_`
// and you get a C++ callable object that calls the python function.
//
// Example:
//
// const char script[] = R"(
// def main(x, y):
//    ....
// )";
//
// Expected<PythonObject> cpp_foo_wrapper(PythonObject x, PythonObject y) {
//   // no need to synchronize access to this global, we already have the GIL
//   static PythonScript foo(script)
//   return  foo(x, y);
// }
class PythonScript {
  const char *script;
  PythonCallable function;

  llvm::Error Init();

public:
  PythonScript(const char *script) : script(script), function() {}

  template <typename... Args>
  llvm::Expected<PythonObject> operator()(Args &&... args) {
    if (llvm::Error error = Init())
      return std::move(error);
    return function.Call(std::forward<Args>(args)...);
  }
};

class StructuredPythonObject : public StructuredData::Generic {
public:
  StructuredPythonObject() : StructuredData::Generic() {}

  // Take ownership of the object we received.
  StructuredPythonObject(PythonObject obj)
      : StructuredData::Generic(obj.release()) {}

  ~StructuredPythonObject() override {
    // Hand ownership back to a (temporary) PythonObject instance and let it
    // take care of releasing it.
    PythonObject(PyRefType::Owned, static_cast<PyObject *>(GetValue()));
  }

  bool IsValid() const override { return GetValue() && GetValue() != Py_None; }

  void Serialize(llvm::json::OStream &s) const override;

private:
  StructuredPythonObject(const StructuredPythonObject &) = delete;
  const StructuredPythonObject &
  operator=(const StructuredPythonObject &) = delete;
};

} // namespace python
} // namespace lldb_private

#endif

#endif // LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_PYTHONDATAOBJECTS_H
