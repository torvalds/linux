//===--- JSON.h - JSON values, parsing and serialization -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
///
/// \file
/// This file supports working with JSON data.
///
/// It comprises:
///
/// - classes which hold dynamically-typed parsed JSON structures
///   These are value types that can be composed, inspected, and modified.
///   See json::Value, and the related types json::Object and json::Array.
///
/// - functions to parse JSON text into Values, and to serialize Values to text.
///   See parse(), operator<<, and format_provider.
///
/// - a convention and helpers for mapping between json::Value and user-defined
///   types. See fromJSON(), ObjectMapper, and the class comment on Value.
///
/// - an output API json::OStream which can emit JSON without materializing
///   all structures as json::Value.
///
/// Typically, JSON data would be read from an external source, parsed into
/// a Value, and then converted into some native data structure before doing
/// real work on it. (And vice versa when writing).
///
/// Other serialization mechanisms you may consider:
///
/// - YAML is also text-based, and more human-readable than JSON. It's a more
///   complex format and data model, and YAML parsers aren't ubiquitous.
///   YAMLParser.h is a streaming parser suitable for parsing large documents
///   (including JSON, as YAML is a superset). It can be awkward to use
///   directly. YAML I/O (YAMLTraits.h) provides data mapping that is more
///   declarative than the toJSON/fromJSON conventions here.
///
/// - LLVM bitstream is a space- and CPU- efficient binary format. Typically it
///   encodes LLVM IR ("bitcode"), but it can be a container for other data.
///   Low-level reader/writer libraries are in Bitstream/Bitstream*.h
///
//===---------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_JSON_H
#define LLVM_SUPPORT_JSON_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <cmath>
#include <map>

namespace llvm {
namespace json {

// === String encodings ===
//
// JSON strings are character sequences (not byte sequences like std::string).
// We need to know the encoding, and for simplicity only support UTF-8.
//
//   - When parsing, invalid UTF-8 is a syntax error like any other
//
//   - When creating Values from strings, callers must ensure they are UTF-8.
//        with asserts on, invalid UTF-8 will crash the program
//        with asserts off, we'll substitute the replacement character (U+FFFD)
//     Callers can use json::isUTF8() and json::fixUTF8() for validation.
//
//   - When retrieving strings from Values (e.g. asString()), the result will
//     always be valid UTF-8.

template <typename T>
constexpr bool is_uint_64_bit_v =
    std::is_integral_v<T> && std::is_unsigned_v<T> &&
    sizeof(T) == sizeof(uint64_t);

/// Returns true if \p S is valid UTF-8, which is required for use as JSON.
/// If it returns false, \p Offset is set to a byte offset near the first error.
bool isUTF8(llvm::StringRef S, size_t *ErrOffset = nullptr);
/// Replaces invalid UTF-8 sequences in \p S with the replacement character
/// (U+FFFD). The returned string is valid UTF-8.
/// This is much slower than isUTF8, so test that first.
std::string fixUTF8(llvm::StringRef S);

class Array;
class ObjectKey;
class Value;
template <typename T> Value toJSON(const std::optional<T> &Opt);

/// An Object is a JSON object, which maps strings to heterogenous JSON values.
/// It simulates DenseMap<ObjectKey, Value>. ObjectKey is a maybe-owned string.
class Object {
  using Storage = DenseMap<ObjectKey, Value, llvm::DenseMapInfo<StringRef>>;
  Storage M;

public:
  using key_type = ObjectKey;
  using mapped_type = Value;
  using value_type = Storage::value_type;
  using iterator = Storage::iterator;
  using const_iterator = Storage::const_iterator;

  Object() = default;
  // KV is a trivial key-value struct for list-initialization.
  // (using std::pair forces extra copies).
  struct KV;
  explicit Object(std::initializer_list<KV> Properties);

  iterator begin() { return M.begin(); }
  const_iterator begin() const { return M.begin(); }
  iterator end() { return M.end(); }
  const_iterator end() const { return M.end(); }

  bool empty() const { return M.empty(); }
  size_t size() const { return M.size(); }

  void clear() { M.clear(); }
  std::pair<iterator, bool> insert(KV E);
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const ObjectKey &K, Ts &&... Args) {
    return M.try_emplace(K, std::forward<Ts>(Args)...);
  }
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(ObjectKey &&K, Ts &&... Args) {
    return M.try_emplace(std::move(K), std::forward<Ts>(Args)...);
  }
  bool erase(StringRef K);
  void erase(iterator I) { M.erase(I); }

  iterator find(StringRef K) { return M.find_as(K); }
  const_iterator find(StringRef K) const { return M.find_as(K); }
  // operator[] acts as if Value was default-constructible as null.
  Value &operator[](const ObjectKey &K);
  Value &operator[](ObjectKey &&K);
  // Look up a property, returning nullptr if it doesn't exist.
  Value *get(StringRef K);
  const Value *get(StringRef K) const;
  // Typed accessors return std::nullopt/nullptr if
  //   - the property doesn't exist
  //   - or it has the wrong type
  std::optional<std::nullptr_t> getNull(StringRef K) const;
  std::optional<bool> getBoolean(StringRef K) const;
  std::optional<double> getNumber(StringRef K) const;
  std::optional<int64_t> getInteger(StringRef K) const;
  std::optional<llvm::StringRef> getString(StringRef K) const;
  const json::Object *getObject(StringRef K) const;
  json::Object *getObject(StringRef K);
  const json::Array *getArray(StringRef K) const;
  json::Array *getArray(StringRef K);
};
bool operator==(const Object &LHS, const Object &RHS);
inline bool operator!=(const Object &LHS, const Object &RHS) {
  return !(LHS == RHS);
}

/// An Array is a JSON array, which contains heterogeneous JSON values.
/// It simulates std::vector<Value>.
class Array {
  std::vector<Value> V;

public:
  using value_type = Value;
  using iterator = std::vector<Value>::iterator;
  using const_iterator = std::vector<Value>::const_iterator;

  Array() = default;
  explicit Array(std::initializer_list<Value> Elements);
  template <typename Collection> explicit Array(const Collection &C) {
    for (const auto &V : C)
      emplace_back(V);
  }

  Value &operator[](size_t I);
  const Value &operator[](size_t I) const;
  Value &front();
  const Value &front() const;
  Value &back();
  const Value &back() const;
  Value *data();
  const Value *data() const;

  iterator begin();
  const_iterator begin() const;
  iterator end();
  const_iterator end() const;

  bool empty() const;
  size_t size() const;
  void reserve(size_t S);

  void clear();
  void push_back(const Value &E);
  void push_back(Value &&E);
  template <typename... Args> void emplace_back(Args &&...A);
  void pop_back();
  iterator insert(const_iterator P, const Value &E);
  iterator insert(const_iterator P, Value &&E);
  template <typename It> iterator insert(const_iterator P, It A, It Z);
  template <typename... Args> iterator emplace(const_iterator P, Args &&...A);

  friend bool operator==(const Array &L, const Array &R);
};
inline bool operator!=(const Array &L, const Array &R) { return !(L == R); }

/// A Value is an JSON value of unknown type.
/// They can be copied, but should generally be moved.
///
/// === Composing values ===
///
/// You can implicitly construct Values from:
///   - strings: std::string, SmallString, formatv, StringRef, char*
///              (char*, and StringRef are references, not copies!)
///   - numbers
///   - booleans
///   - null: nullptr
///   - arrays: {"foo", 42.0, false}
///   - serializable things: types with toJSON(const T&)->Value, found by ADL
///
/// They can also be constructed from object/array helpers:
///   - json::Object is a type like map<ObjectKey, Value>
///   - json::Array is a type like vector<Value>
/// These can be list-initialized, or used to build up collections in a loop.
/// json::ary(Collection) converts all items in a collection to Values.
///
/// === Inspecting values ===
///
/// Each Value is one of the JSON kinds:
///   null    (nullptr_t)
///   boolean (bool)
///   number  (double, int64 or uint64)
///   string  (StringRef)
///   array   (json::Array)
///   object  (json::Object)
///
/// The kind can be queried directly, or implicitly via the typed accessors:
///   if (std::optional<StringRef> S = E.getAsString()
///     assert(E.kind() == Value::String);
///
/// Array and Object also have typed indexing accessors for easy traversal:
///   Expected<Value> E = parse(R"( {"options": {"font": "sans-serif"}} )");
///   if (Object* O = E->getAsObject())
///     if (Object* Opts = O->getObject("options"))
///       if (std::optional<StringRef> Font = Opts->getString("font"))
///         assert(Opts->at("font").kind() == Value::String);
///
/// === Converting JSON values to C++ types ===
///
/// The convention is to have a deserializer function findable via ADL:
///     fromJSON(const json::Value&, T&, Path) -> bool
///
/// The return value indicates overall success, and Path is used for precise
/// error reporting. (The Path::Root passed in at the top level fromJSON call
/// captures any nested error and can render it in context).
/// If conversion fails, fromJSON calls Path::report() and immediately returns.
/// This ensures that the first fatal error survives.
///
/// Deserializers are provided for:
///   - bool
///   - int and int64_t
///   - double
///   - std::string
///   - vector<T>, where T is deserializable
///   - map<string, T>, where T is deserializable
///   - std::optional<T>, where T is deserializable
/// ObjectMapper can help writing fromJSON() functions for object types.
///
/// For conversion in the other direction, the serializer function is:
///    toJSON(const T&) -> json::Value
/// If this exists, then it also allows constructing Value from T, and can
/// be used to serialize vector<T>, map<string, T>, and std::optional<T>.
///
/// === Serialization ===
///
/// Values can be serialized to JSON:
///   1) raw_ostream << Value                    // Basic formatting.
///   2) raw_ostream << formatv("{0}", Value)    // Basic formatting.
///   3) raw_ostream << formatv("{0:2}", Value)  // Pretty-print with indent 2.
///
/// And parsed:
///   Expected<Value> E = json::parse("[1, 2, null]");
///   assert(E && E->kind() == Value::Array);
class Value {
public:
  enum Kind {
    Null,
    Boolean,
    /// Number values can store both int64s and doubles at full precision,
    /// depending on what they were constructed/parsed from.
    Number,
    String,
    Array,
    Object,
  };

  // It would be nice to have Value() be null. But that would make {} null too.
  Value(const Value &M) { copyFrom(M); }
  Value(Value &&M) { moveFrom(std::move(M)); }
  Value(std::initializer_list<Value> Elements);
  Value(json::Array &&Elements) : Type(T_Array) {
    create<json::Array>(std::move(Elements));
  }
  template <typename Elt>
  Value(const std::vector<Elt> &C) : Value(json::Array(C)) {}
  Value(json::Object &&Properties) : Type(T_Object) {
    create<json::Object>(std::move(Properties));
  }
  template <typename Elt>
  Value(const std::map<std::string, Elt> &C) : Value(json::Object(C)) {}
  // Strings: types with value semantics. Must be valid UTF-8.
  Value(std::string V) : Type(T_String) {
    if (LLVM_UNLIKELY(!isUTF8(V))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      V = fixUTF8(std::move(V));
    }
    create<std::string>(std::move(V));
  }
  Value(const llvm::SmallVectorImpl<char> &V)
      : Value(std::string(V.begin(), V.end())) {}
  Value(const llvm::formatv_object_base &V) : Value(V.str()) {}
  // Strings: types with reference semantics. Must be valid UTF-8.
  Value(StringRef V) : Type(T_StringRef) {
    create<llvm::StringRef>(V);
    if (LLVM_UNLIKELY(!isUTF8(V))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *this = Value(fixUTF8(V));
    }
  }
  Value(const char *V) : Value(StringRef(V)) {}
  Value(std::nullptr_t) : Type(T_Null) {}
  // Boolean (disallow implicit conversions).
  // (The last template parameter is a dummy to keep templates distinct.)
  template <typename T, typename = std::enable_if_t<std::is_same_v<T, bool>>,
            bool = false>
  Value(T B) : Type(T_Boolean) {
    create<bool>(B);
  }

  // Unsigned 64-bit integers.
  template <typename T, typename = std::enable_if_t<is_uint_64_bit_v<T>>>
  Value(T V) : Type(T_UINT64) {
    create<uint64_t>(uint64_t{V});
  }

  // Integers (except boolean and uint64_t).
  // Must be non-narrowing convertible to int64_t.
  template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>,
            typename = std::enable_if_t<!std::is_same_v<T, bool>>,
            typename = std::enable_if_t<!is_uint_64_bit_v<T>>>
  Value(T I) : Type(T_Integer) {
    create<int64_t>(int64_t{I});
  }
  // Floating point. Must be non-narrowing convertible to double.
  template <typename T,
            typename = std::enable_if_t<std::is_floating_point_v<T>>,
            double * = nullptr>
  Value(T D) : Type(T_Double) {
    create<double>(double{D});
  }
  // Serializable types: with a toJSON(const T&)->Value function, found by ADL.
  template <typename T,
            typename = std::enable_if_t<
                std::is_same_v<Value, decltype(toJSON(*(const T *)nullptr))>>,
            Value * = nullptr>
  Value(const T &V) : Value(toJSON(V)) {}

  Value &operator=(const Value &M) {
    destroy();
    copyFrom(M);
    return *this;
  }
  Value &operator=(Value &&M) {
    destroy();
    moveFrom(std::move(M));
    return *this;
  }
  ~Value() { destroy(); }

  Kind kind() const {
    switch (Type) {
    case T_Null:
      return Null;
    case T_Boolean:
      return Boolean;
    case T_Double:
    case T_Integer:
    case T_UINT64:
      return Number;
    case T_String:
    case T_StringRef:
      return String;
    case T_Object:
      return Object;
    case T_Array:
      return Array;
    }
    llvm_unreachable("Unknown kind");
  }

  // Typed accessors return std::nullopt/nullptr if the Value is not of this
  // type.
  std::optional<std::nullptr_t> getAsNull() const {
    if (LLVM_LIKELY(Type == T_Null))
      return nullptr;
    return std::nullopt;
  }
  std::optional<bool> getAsBoolean() const {
    if (LLVM_LIKELY(Type == T_Boolean))
      return as<bool>();
    return std::nullopt;
  }
  std::optional<double> getAsNumber() const {
    if (LLVM_LIKELY(Type == T_Double))
      return as<double>();
    if (LLVM_LIKELY(Type == T_Integer))
      return as<int64_t>();
    if (LLVM_LIKELY(Type == T_UINT64))
      return as<uint64_t>();
    return std::nullopt;
  }
  // Succeeds if the Value is a Number, and exactly representable as int64_t.
  std::optional<int64_t> getAsInteger() const {
    if (LLVM_LIKELY(Type == T_Integer))
      return as<int64_t>();
    if (LLVM_LIKELY(Type == T_UINT64)) {
      uint64_t U = as<uint64_t>();
      if (LLVM_LIKELY(U <= uint64_t(std::numeric_limits<int64_t>::max()))) {
        return U;
      }
    }
    if (LLVM_LIKELY(Type == T_Double)) {
      double D = as<double>();
      if (LLVM_LIKELY(std::modf(D, &D) == 0.0 &&
                      D >= double(std::numeric_limits<int64_t>::min()) &&
                      D <= double(std::numeric_limits<int64_t>::max())))
        return D;
    }
    return std::nullopt;
  }
  std::optional<uint64_t> getAsUINT64() const {
    if (Type == T_UINT64)
      return as<uint64_t>();
    else if (Type == T_Integer) {
      int64_t N = as<int64_t>();
      if (N >= 0)
        return as<uint64_t>();
    }
    return std::nullopt;
  }
  std::optional<llvm::StringRef> getAsString() const {
    if (Type == T_String)
      return llvm::StringRef(as<std::string>());
    if (LLVM_LIKELY(Type == T_StringRef))
      return as<llvm::StringRef>();
    return std::nullopt;
  }
  const json::Object *getAsObject() const {
    return LLVM_LIKELY(Type == T_Object) ? &as<json::Object>() : nullptr;
  }
  json::Object *getAsObject() {
    return LLVM_LIKELY(Type == T_Object) ? &as<json::Object>() : nullptr;
  }
  const json::Array *getAsArray() const {
    return LLVM_LIKELY(Type == T_Array) ? &as<json::Array>() : nullptr;
  }
  json::Array *getAsArray() {
    return LLVM_LIKELY(Type == T_Array) ? &as<json::Array>() : nullptr;
  }

private:
  void destroy();
  void copyFrom(const Value &M);
  // We allow moving from *const* Values, by marking all members as mutable!
  // This hack is needed to support initializer-list syntax efficiently.
  // (std::initializer_list<T> is a container of const T).
  void moveFrom(const Value &&M);
  friend class Array;
  friend class Object;

  template <typename T, typename... U> void create(U &&... V) {
#if LLVM_ADDRESS_SANITIZER_BUILD
    // Unpoisoning to prevent overwriting poisoned object (e.g., annotated short
    // string). Objects that have had their memory poisoned may cause an ASan
    // error if their memory is reused without calling their destructor.
    // Unpoisoning the memory prevents this error from occurring.
    // FIXME: This is a temporary solution to prevent buildbots from failing.
    //  The more appropriate approach would be to call the object's destructor
    //  to unpoison memory. This would prevent any potential memory leaks (long
    //  strings). Read for details:
    //  https://github.com/llvm/llvm-project/pull/79065#discussion_r1462621761
    __asan_unpoison_memory_region(&Union, sizeof(T));
#endif
    new (reinterpret_cast<T *>(&Union)) T(std::forward<U>(V)...);
  }
  template <typename T> T &as() const {
    // Using this two-step static_cast via void * instead of reinterpret_cast
    // silences a -Wstrict-aliasing false positive from GCC6 and earlier.
    void *Storage = static_cast<void *>(&Union);
    return *static_cast<T *>(Storage);
  }

  friend class OStream;

  enum ValueType : char16_t {
    T_Null,
    T_Boolean,
    T_Double,
    T_Integer,
    T_UINT64,
    T_StringRef,
    T_String,
    T_Object,
    T_Array,
  };
  // All members mutable, see moveFrom().
  mutable ValueType Type;
  mutable llvm::AlignedCharArrayUnion<bool, double, int64_t, uint64_t,
                                      llvm::StringRef, std::string, json::Array,
                                      json::Object>
      Union;
  friend bool operator==(const Value &, const Value &);
};

bool operator==(const Value &, const Value &);
inline bool operator!=(const Value &L, const Value &R) { return !(L == R); }

// Array Methods
inline Value &Array::operator[](size_t I) { return V[I]; }
inline const Value &Array::operator[](size_t I) const { return V[I]; }
inline Value &Array::front() { return V.front(); }
inline const Value &Array::front() const { return V.front(); }
inline Value &Array::back() { return V.back(); }
inline const Value &Array::back() const { return V.back(); }
inline Value *Array::data() { return V.data(); }
inline const Value *Array::data() const { return V.data(); }

inline typename Array::iterator Array::begin() { return V.begin(); }
inline typename Array::const_iterator Array::begin() const { return V.begin(); }
inline typename Array::iterator Array::end() { return V.end(); }
inline typename Array::const_iterator Array::end() const { return V.end(); }

inline bool Array::empty() const { return V.empty(); }
inline size_t Array::size() const { return V.size(); }
inline void Array::reserve(size_t S) { V.reserve(S); }

inline void Array::clear() { V.clear(); }
inline void Array::push_back(const Value &E) { V.push_back(E); }
inline void Array::push_back(Value &&E) { V.push_back(std::move(E)); }
template <typename... Args> inline void Array::emplace_back(Args &&...A) {
  V.emplace_back(std::forward<Args>(A)...);
}
inline void Array::pop_back() { V.pop_back(); }
inline typename Array::iterator Array::insert(const_iterator P, const Value &E) {
  return V.insert(P, E);
}
inline typename Array::iterator Array::insert(const_iterator P, Value &&E) {
  return V.insert(P, std::move(E));
}
template <typename It>
inline typename Array::iterator Array::insert(const_iterator P, It A, It Z) {
  return V.insert(P, A, Z);
}
template <typename... Args>
inline typename Array::iterator Array::emplace(const_iterator P, Args &&...A) {
  return V.emplace(P, std::forward<Args>(A)...);
}
inline bool operator==(const Array &L, const Array &R) { return L.V == R.V; }

/// ObjectKey is a used to capture keys in Object. Like Value but:
///   - only strings are allowed
///   - it's optimized for the string literal case (Owned == nullptr)
/// Like Value, strings must be UTF-8. See isUTF8 documentation for details.
class ObjectKey {
public:
  ObjectKey(const char *S) : ObjectKey(StringRef(S)) {}
  ObjectKey(std::string S) : Owned(new std::string(std::move(S))) {
    if (LLVM_UNLIKELY(!isUTF8(*Owned))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *Owned = fixUTF8(std::move(*Owned));
    }
    Data = *Owned;
  }
  ObjectKey(llvm::StringRef S) : Data(S) {
    if (LLVM_UNLIKELY(!isUTF8(Data))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *this = ObjectKey(fixUTF8(S));
    }
  }
  ObjectKey(const llvm::SmallVectorImpl<char> &V)
      : ObjectKey(std::string(V.begin(), V.end())) {}
  ObjectKey(const llvm::formatv_object_base &V) : ObjectKey(V.str()) {}

  ObjectKey(const ObjectKey &C) { *this = C; }
  ObjectKey(ObjectKey &&C) : ObjectKey(static_cast<const ObjectKey &&>(C)) {}
  ObjectKey &operator=(const ObjectKey &C) {
    if (C.Owned) {
      Owned.reset(new std::string(*C.Owned));
      Data = *Owned;
    } else {
      Data = C.Data;
    }
    return *this;
  }
  ObjectKey &operator=(ObjectKey &&) = default;

  operator llvm::StringRef() const { return Data; }
  std::string str() const { return Data.str(); }

private:
  // FIXME: this is unneccesarily large (3 pointers). Pointer + length + owned
  // could be 2 pointers at most.
  std::unique_ptr<std::string> Owned;
  llvm::StringRef Data;
};

inline bool operator==(const ObjectKey &L, const ObjectKey &R) {
  return llvm::StringRef(L) == llvm::StringRef(R);
}
inline bool operator!=(const ObjectKey &L, const ObjectKey &R) {
  return !(L == R);
}
inline bool operator<(const ObjectKey &L, const ObjectKey &R) {
  return StringRef(L) < StringRef(R);
}

struct Object::KV {
  ObjectKey K;
  Value V;
};

inline Object::Object(std::initializer_list<KV> Properties) {
  for (const auto &P : Properties) {
    auto R = try_emplace(P.K, nullptr);
    if (R.second)
      R.first->getSecond().moveFrom(std::move(P.V));
  }
}
inline std::pair<Object::iterator, bool> Object::insert(KV E) {
  return try_emplace(std::move(E.K), std::move(E.V));
}
inline bool Object::erase(StringRef K) {
  return M.erase(ObjectKey(K));
}

std::vector<const Object::value_type *> sortedElements(const Object &O);

/// A "cursor" marking a position within a Value.
/// The Value is a tree, and this is the path from the root to the current node.
/// This is used to associate errors with particular subobjects.
class Path {
public:
  class Root;

  /// Records that the value at the current path is invalid.
  /// Message is e.g. "expected number" and becomes part of the final error.
  /// This overwrites any previously written error message in the root.
  void report(llvm::StringLiteral Message);

  /// The root may be treated as a Path.
  Path(Root &R) : Parent(nullptr), Seg(&R) {}
  /// Derives a path for an array element: this[Index]
  Path index(unsigned Index) const { return Path(this, Segment(Index)); }
  /// Derives a path for an object field: this.Field
  Path field(StringRef Field) const { return Path(this, Segment(Field)); }

private:
  /// One element in a JSON path: an object field (.foo) or array index [27].
  /// Exception: the root Path encodes a pointer to the Path::Root.
  class Segment {
    uintptr_t Pointer;
    unsigned Offset;

  public:
    Segment() = default;
    Segment(Root *R) : Pointer(reinterpret_cast<uintptr_t>(R)) {}
    Segment(llvm::StringRef Field)
        : Pointer(reinterpret_cast<uintptr_t>(Field.data())),
          Offset(static_cast<unsigned>(Field.size())) {}
    Segment(unsigned Index) : Pointer(0), Offset(Index) {}

    bool isField() const { return Pointer != 0; }
    StringRef field() const {
      return StringRef(reinterpret_cast<const char *>(Pointer), Offset);
    }
    unsigned index() const { return Offset; }
    Root *root() const { return reinterpret_cast<Root *>(Pointer); }
  };

  const Path *Parent;
  Segment Seg;

  Path(const Path *Parent, Segment S) : Parent(Parent), Seg(S) {}
};

/// The root is the trivial Path to the root value.
/// It also stores the latest reported error and the path where it occurred.
class Path::Root {
  llvm::StringRef Name;
  llvm::StringLiteral ErrorMessage;
  std::vector<Path::Segment> ErrorPath; // Only valid in error state. Reversed.

  friend void Path::report(llvm::StringLiteral Message);

public:
  Root(llvm::StringRef Name = "") : Name(Name), ErrorMessage("") {}
  // No copy/move allowed as there are incoming pointers.
  Root(Root &&) = delete;
  Root &operator=(Root &&) = delete;
  Root(const Root &) = delete;
  Root &operator=(const Root &) = delete;

  /// Returns the last error reported, or else a generic error.
  Error getError() const;
  /// Print the root value with the error shown inline as a comment.
  /// Unrelated parts of the value are elided for brevity, e.g.
  ///   {
  ///      "id": 42,
  ///      "name": /* expected string */ null,
  ///      "properties": { ... }
  ///   }
  void printErrorContext(const Value &, llvm::raw_ostream &) const;
};

// Standard deserializers are provided for primitive types.
// See comments on Value.
inline bool fromJSON(const Value &E, std::string &Out, Path P) {
  if (auto S = E.getAsString()) {
    Out = std::string(*S);
    return true;
  }
  P.report("expected string");
  return false;
}
inline bool fromJSON(const Value &E, int &Out, Path P) {
  if (auto S = E.getAsInteger()) {
    Out = *S;
    return true;
  }
  P.report("expected integer");
  return false;
}
inline bool fromJSON(const Value &E, int64_t &Out, Path P) {
  if (auto S = E.getAsInteger()) {
    Out = *S;
    return true;
  }
  P.report("expected integer");
  return false;
}
inline bool fromJSON(const Value &E, double &Out, Path P) {
  if (auto S = E.getAsNumber()) {
    Out = *S;
    return true;
  }
  P.report("expected number");
  return false;
}
inline bool fromJSON(const Value &E, bool &Out, Path P) {
  if (auto S = E.getAsBoolean()) {
    Out = *S;
    return true;
  }
  P.report("expected boolean");
  return false;
}
inline bool fromJSON(const Value &E, uint64_t &Out, Path P) {
  if (auto S = E.getAsUINT64()) {
    Out = *S;
    return true;
  }
  P.report("expected uint64_t");
  return false;
}
inline bool fromJSON(const Value &E, std::nullptr_t &Out, Path P) {
  if (auto S = E.getAsNull()) {
    Out = *S;
    return true;
  }
  P.report("expected null");
  return false;
}
template <typename T>
bool fromJSON(const Value &E, std::optional<T> &Out, Path P) {
  if (E.getAsNull()) {
    Out = std::nullopt;
    return true;
  }
  T Result = {};
  if (!fromJSON(E, Result, P))
    return false;
  Out = std::move(Result);
  return true;
}
template <typename T>
bool fromJSON(const Value &E, std::vector<T> &Out, Path P) {
  if (auto *A = E.getAsArray()) {
    Out.clear();
    Out.resize(A->size());
    for (size_t I = 0; I < A->size(); ++I)
      if (!fromJSON((*A)[I], Out[I], P.index(I)))
        return false;
    return true;
  }
  P.report("expected array");
  return false;
}
template <typename T>
bool fromJSON(const Value &E, std::map<std::string, T> &Out, Path P) {
  if (auto *O = E.getAsObject()) {
    Out.clear();
    for (const auto &KV : *O)
      if (!fromJSON(KV.second, Out[std::string(llvm::StringRef(KV.first))],
                    P.field(KV.first)))
        return false;
    return true;
  }
  P.report("expected object");
  return false;
}

// Allow serialization of std::optional<T> for supported T.
template <typename T> Value toJSON(const std::optional<T> &Opt) {
  return Opt ? Value(*Opt) : Value(nullptr);
}

/// Helper for mapping JSON objects onto protocol structs.
///
/// Example:
/// \code
///   bool fromJSON(const Value &E, MyStruct &R, Path P) {
///     ObjectMapper O(E, P);
///     // When returning false, error details were already reported.
///     return O && O.map("mandatory_field", R.MandatoryField) &&
///         O.mapOptional("optional_field", R.OptionalField);
///   }
/// \endcode
class ObjectMapper {
public:
  /// If O is not an object, this mapper is invalid and an error is reported.
  ObjectMapper(const Value &E, Path P) : O(E.getAsObject()), P(P) {
    if (!O)
      P.report("expected object");
  }

  /// True if the expression is an object.
  /// Must be checked before calling map().
  operator bool() const { return O; }

  /// Maps a property to a field.
  /// If the property is missing or invalid, reports an error.
  template <typename T> bool map(StringLiteral Prop, T &Out) {
    assert(*this && "Must check this is an object before calling map()");
    if (const Value *E = O->get(Prop))
      return fromJSON(*E, Out, P.field(Prop));
    P.field(Prop).report("missing value");
    return false;
  }

  /// Maps a property to a field, if it exists.
  /// If the property exists and is invalid, reports an error.
  /// (Optional requires special handling, because missing keys are OK).
  template <typename T> bool map(StringLiteral Prop, std::optional<T> &Out) {
    assert(*this && "Must check this is an object before calling map()");
    if (const Value *E = O->get(Prop))
      return fromJSON(*E, Out, P.field(Prop));
    Out = std::nullopt;
    return true;
  }

  /// Maps a property to a field, if it exists.
  /// If the property exists and is invalid, reports an error.
  /// If the property does not exist, Out is unchanged.
  template <typename T> bool mapOptional(StringLiteral Prop, T &Out) {
    assert(*this && "Must check this is an object before calling map()");
    if (const Value *E = O->get(Prop))
      return fromJSON(*E, Out, P.field(Prop));
    return true;
  }

private:
  const Object *O;
  Path P;
};

/// Parses the provided JSON source, or returns a ParseError.
/// The returned Value is self-contained and owns its strings (they do not refer
/// to the original source).
llvm::Expected<Value> parse(llvm::StringRef JSON);

class ParseError : public llvm::ErrorInfo<ParseError> {
  const char *Msg;
  unsigned Line, Column, Offset;

public:
  static char ID;
  ParseError(const char *Msg, unsigned Line, unsigned Column, unsigned Offset)
      : Msg(Msg), Line(Line), Column(Column), Offset(Offset) {}
  void log(llvm::raw_ostream &OS) const override {
    OS << llvm::formatv("[{0}:{1}, byte={2}]: {3}", Line, Column, Offset, Msg);
  }
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }
};

/// Version of parse() that converts the parsed value to the type T.
/// RootName describes the root object and is used in error messages.
template <typename T>
Expected<T> parse(const llvm::StringRef &JSON, const char *RootName = "") {
  auto V = parse(JSON);
  if (!V)
    return V.takeError();
  Path::Root R(RootName);
  T Result;
  if (fromJSON(*V, Result, R))
    return std::move(Result);
  return R.getError();
}

/// json::OStream allows writing well-formed JSON without materializing
/// all structures as json::Value ahead of time.
/// It's faster, lower-level, and less safe than OS << json::Value.
/// It also allows emitting more constructs, such as comments.
///
/// Only one "top-level" object can be written to a stream.
/// Simplest usage involves passing lambdas (Blocks) to fill in containers:
///
///   json::OStream J(OS);
///   J.array([&]{
///     for (const Event &E : Events)
///       J.object([&] {
///         J.attribute("timestamp", int64_t(E.Time));
///         J.attributeArray("participants", [&] {
///           for (const Participant &P : E.Participants)
///             J.value(P.toString());
///         });
///       });
///   });
///
/// This would produce JSON like:
///
///   [
///     {
///       "timestamp": 19287398741,
///       "participants": [
///         "King Kong",
///         "Miley Cyrus",
///         "Cleopatra"
///       ]
///     },
///     ...
///   ]
///
/// The lower level begin/end methods (arrayBegin()) are more flexible but
/// care must be taken to pair them correctly:
///
///   json::OStream J(OS);
//    J.arrayBegin();
///   for (const Event &E : Events) {
///     J.objectBegin();
///     J.attribute("timestamp", int64_t(E.Time));
///     J.attributeBegin("participants");
///     for (const Participant &P : E.Participants)
///       J.value(P.toString());
///     J.attributeEnd();
///     J.objectEnd();
///   }
///   J.arrayEnd();
///
/// If the call sequence isn't valid JSON, asserts will fire in debug mode.
/// This can be mismatched begin()/end() pairs, trying to emit attributes inside
/// an array, and so on.
/// With asserts disabled, this is undefined behavior.
class OStream {
 public:
  using Block = llvm::function_ref<void()>;
  // If IndentSize is nonzero, output is pretty-printed.
  explicit OStream(llvm::raw_ostream &OS, unsigned IndentSize = 0)
      : OS(OS), IndentSize(IndentSize) {
    Stack.emplace_back();
  }
  ~OStream() {
    assert(Stack.size() == 1 && "Unmatched begin()/end()");
    assert(Stack.back().Ctx == Singleton);
    assert(Stack.back().HasValue && "Did not write top-level value");
  }

  /// Flushes the underlying ostream. OStream does not buffer internally.
  void flush() { OS.flush(); }

  // High level functions to output a value.
  // Valid at top-level (exactly once), in an attribute value (exactly once),
  // or in an array (any number of times).

  /// Emit a self-contained value (number, string, vector<string> etc).
  void value(const Value &V);
  /// Emit an array whose elements are emitted in the provided Block.
  void array(Block Contents) {
    arrayBegin();
    Contents();
    arrayEnd();
  }
  /// Emit an object whose elements are emitted in the provided Block.
  void object(Block Contents) {
    objectBegin();
    Contents();
    objectEnd();
  }
  /// Emit an externally-serialized value.
  /// The caller must write exactly one valid JSON value to the provided stream.
  /// No validation or formatting of this value occurs.
  void rawValue(llvm::function_ref<void(raw_ostream &)> Contents) {
    rawValueBegin();
    Contents(OS);
    rawValueEnd();
  }
  void rawValue(llvm::StringRef Contents) {
    rawValue([&](raw_ostream &OS) { OS << Contents; });
  }
  /// Emit a JavaScript comment associated with the next printed value.
  /// The string must be valid until the next attribute or value is emitted.
  /// Comments are not part of standard JSON, and many parsers reject them!
  void comment(llvm::StringRef);

  // High level functions to output object attributes.
  // Valid only within an object (any number of times).

  /// Emit an attribute whose value is self-contained (number, vector<int> etc).
  void attribute(llvm::StringRef Key, const Value& Contents) {
    attributeImpl(Key, [&] { value(Contents); });
  }
  /// Emit an attribute whose value is an array with elements from the Block.
  void attributeArray(llvm::StringRef Key, Block Contents) {
    attributeImpl(Key, [&] { array(Contents); });
  }
  /// Emit an attribute whose value is an object with attributes from the Block.
  void attributeObject(llvm::StringRef Key, Block Contents) {
    attributeImpl(Key, [&] { object(Contents); });
  }

  // Low-level begin/end functions to output arrays, objects, and attributes.
  // Must be correctly paired. Allowed contexts are as above.

  void arrayBegin();
  void arrayEnd();
  void objectBegin();
  void objectEnd();
  void attributeBegin(llvm::StringRef Key);
  void attributeEnd();
  raw_ostream &rawValueBegin();
  void rawValueEnd();

private:
  void attributeImpl(llvm::StringRef Key, Block Contents) {
    attributeBegin(Key);
    Contents();
    attributeEnd();
  }

  void valueBegin();
  void flushComment();
  void newline();

  enum Context {
    Singleton, // Top level, or object attribute.
    Array,
    Object,
    RawValue, // External code writing a value to OS directly.
  };
  struct State {
    Context Ctx = Singleton;
    bool HasValue = false;
  };
  llvm::SmallVector<State, 16> Stack; // Never empty.
  llvm::StringRef PendingComment;
  llvm::raw_ostream &OS;
  unsigned IndentSize;
  unsigned Indent = 0;
};

/// Serializes this Value to JSON, writing it to the provided stream.
/// The formatting is compact (no extra whitespace) and deterministic.
/// For pretty-printing, use the formatv() format_provider below.
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const Value &V) {
  OStream(OS).value(V);
  return OS;
}
} // namespace json

/// Allow printing json::Value with formatv().
/// The default style is basic/compact formatting, like operator<<.
/// A format string like formatv("{0:2}", Value) pretty-prints with indent 2.
template <> struct format_provider<llvm::json::Value> {
  static void format(const llvm::json::Value &, raw_ostream &, StringRef);
};
} // namespace llvm

#endif
