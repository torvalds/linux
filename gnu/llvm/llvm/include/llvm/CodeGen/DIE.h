//===- lib/CodeGen/DIE.h - DWARF Info Entries -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Data structures for DWARF info entries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DIE_H
#define LLVM_CODEGEN_DIE_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace llvm {

class AsmPrinter;
class DIE;
class DIEUnit;
class DwarfCompileUnit;
class MCExpr;
class MCSection;
class MCSymbol;
class raw_ostream;

//===--------------------------------------------------------------------===//
/// Dwarf abbreviation data, describes one attribute of a Dwarf abbreviation.
class DIEAbbrevData {
  /// Dwarf attribute code.
  dwarf::Attribute Attribute;

  /// Dwarf form code.
  dwarf::Form Form;

  /// Dwarf attribute value for DW_FORM_implicit_const
  int64_t Value = 0;

public:
  DIEAbbrevData(dwarf::Attribute A, dwarf::Form F)
      : Attribute(A), Form(F) {}
  DIEAbbrevData(dwarf::Attribute A, int64_t V)
      : Attribute(A), Form(dwarf::DW_FORM_implicit_const), Value(V) {}

  /// Accessors.
  /// @{
  dwarf::Attribute getAttribute() const { return Attribute; }
  dwarf::Form getForm() const { return Form; }
  int64_t getValue() const { return Value; }
  /// @}

  /// Used to gather unique data for the abbreviation folding set.
  void Profile(FoldingSetNodeID &ID) const;
};

//===--------------------------------------------------------------------===//
/// Dwarf abbreviation, describes the organization of a debug information
/// object.
class DIEAbbrev : public FoldingSetNode {
  /// Unique number for node.
  unsigned Number = 0;

  /// Dwarf tag code.
  dwarf::Tag Tag;

  /// Whether or not this node has children.
  ///
  /// This cheats a bit in all of the uses since the values in the standard
  /// are 0 and 1 for no children and children respectively.
  bool Children;

  /// Raw data bytes for abbreviation.
  SmallVector<DIEAbbrevData, 12> Data;

public:
  DIEAbbrev(dwarf::Tag T, bool C) : Tag(T), Children(C) {}

  /// Accessors.
  /// @{
  dwarf::Tag getTag() const { return Tag; }
  unsigned getNumber() const { return Number; }
  bool hasChildren() const { return Children; }
  const SmallVectorImpl<DIEAbbrevData> &getData() const { return Data; }
  void setChildrenFlag(bool hasChild) { Children = hasChild; }
  void setNumber(unsigned N) { Number = N; }
  /// @}

  /// Adds another set of attribute information to the abbreviation.
  void AddAttribute(dwarf::Attribute Attribute, dwarf::Form Form) {
    Data.push_back(DIEAbbrevData(Attribute, Form));
  }

  /// Adds attribute with DW_FORM_implicit_const value
  void AddImplicitConstAttribute(dwarf::Attribute Attribute, int64_t Value) {
    Data.push_back(DIEAbbrevData(Attribute, Value));
  }

  /// Adds another set of attribute information to the abbreviation.
  void AddAttribute(const DIEAbbrevData &AbbrevData) {
    Data.push_back(AbbrevData);
  }

  /// Used to gather unique data for the abbreviation folding set.
  void Profile(FoldingSetNodeID &ID) const;

  /// Print the abbreviation using the specified asm printer.
  void Emit(const AsmPrinter *AP) const;

  void print(raw_ostream &O) const;
  void dump() const;
};

//===--------------------------------------------------------------------===//
/// Helps unique DIEAbbrev objects and assigns abbreviation numbers.
///
/// This class will unique the DIE abbreviations for a llvm::DIE object and
/// assign a unique abbreviation number to each unique DIEAbbrev object it
/// finds. The resulting collection of DIEAbbrev objects can then be emitted
/// into the .debug_abbrev section.
class DIEAbbrevSet {
  /// The bump allocator to use when creating DIEAbbrev objects in the uniqued
  /// storage container.
  BumpPtrAllocator &Alloc;
  /// FoldingSet that uniques the abbreviations.
  FoldingSet<DIEAbbrev> AbbreviationsSet;
  /// A list of all the unique abbreviations in use.
  std::vector<DIEAbbrev *> Abbreviations;

public:
  DIEAbbrevSet(BumpPtrAllocator &A) : Alloc(A) {}
  ~DIEAbbrevSet();

  /// Generate the abbreviation declaration for a DIE and return a pointer to
  /// the generated abbreviation.
  ///
  /// \param Die the debug info entry to generate the abbreviation for.
  /// \returns A reference to the uniqued abbreviation declaration that is
  /// owned by this class.
  DIEAbbrev &uniqueAbbreviation(DIE &Die);

  /// Print all abbreviations using the specified asm printer.
  void Emit(const AsmPrinter *AP, MCSection *Section) const;
};

//===--------------------------------------------------------------------===//
/// An integer value DIE.
///
class DIEInteger {
  uint64_t Integer;

public:
  explicit DIEInteger(uint64_t I) : Integer(I) {}

  /// Choose the best form for integer.
  static dwarf::Form BestForm(bool IsSigned, uint64_t Int) {
    if (IsSigned) {
      const int64_t SignedInt = Int;
      if ((char)Int == SignedInt)
        return dwarf::DW_FORM_data1;
      if ((short)Int == SignedInt)
        return dwarf::DW_FORM_data2;
      if ((int)Int == SignedInt)
        return dwarf::DW_FORM_data4;
    } else {
      if ((unsigned char)Int == Int)
        return dwarf::DW_FORM_data1;
      if ((unsigned short)Int == Int)
        return dwarf::DW_FORM_data2;
      if ((unsigned int)Int == Int)
        return dwarf::DW_FORM_data4;
    }
    return dwarf::DW_FORM_data8;
  }

  uint64_t getValue() const { return Integer; }
  void setValue(uint64_t Val) { Integer = Val; }

  void emitValue(const AsmPrinter *Asm, dwarf::Form Form) const;
  unsigned sizeOf(const dwarf::FormParams &FormParams, dwarf::Form Form) const;

  void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// An expression DIE.
class DIEExpr {
  const MCExpr *Expr;

public:
  explicit DIEExpr(const MCExpr *E) : Expr(E) {}

  /// Get MCExpr.
  const MCExpr *getValue() const { return Expr; }

  void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  unsigned sizeOf(const dwarf::FormParams &FormParams, dwarf::Form Form) const;

  void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A label DIE.
class DIELabel {
  const MCSymbol *Label;

public:
  explicit DIELabel(const MCSymbol *L) : Label(L) {}

  /// Get MCSymbol.
  const MCSymbol *getValue() const { return Label; }

  void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  unsigned sizeOf(const dwarf::FormParams &FormParams, dwarf::Form Form) const;

  void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A BaseTypeRef DIE.
class DIEBaseTypeRef {
  const DwarfCompileUnit *CU;
  const uint64_t Index;
  static constexpr unsigned ULEB128PadSize = 4;

public:
  explicit DIEBaseTypeRef(const DwarfCompileUnit *TheCU, uint64_t Idx)
    : CU(TheCU), Index(Idx) {}

  /// EmitValue - Emit base type reference.
  void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  /// sizeOf - Determine size of the base type reference in bytes.
  unsigned sizeOf(const dwarf::FormParams &, dwarf::Form) const;

  void print(raw_ostream &O) const;
  uint64_t getIndex() const { return Index; }
};

//===--------------------------------------------------------------------===//
/// A simple label difference DIE.
///
class DIEDelta {
  const MCSymbol *LabelHi;
  const MCSymbol *LabelLo;

public:
  DIEDelta(const MCSymbol *Hi, const MCSymbol *Lo) : LabelHi(Hi), LabelLo(Lo) {}

  void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  unsigned sizeOf(const dwarf::FormParams &FormParams, dwarf::Form Form) const;

  void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A container for string pool string values.
///
/// This class is used with the DW_FORM_strp and DW_FORM_GNU_str_index forms.
class DIEString {
  DwarfStringPoolEntryRef S;

public:
  DIEString(DwarfStringPoolEntryRef S) : S(S) {}

  /// Grab the string out of the object.
  StringRef getString() const { return S.getString(); }

  void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  unsigned sizeOf(const dwarf::FormParams &FormParams, dwarf::Form Form) const;

  void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A container for inline string values.
///
/// This class is used with the DW_FORM_string form.
class DIEInlineString {
  StringRef S;

public:
  template <typename Allocator>
  explicit DIEInlineString(StringRef Str, Allocator &A) : S(Str.copy(A)) {}

  ~DIEInlineString() = default;

  /// Grab the string out of the object.
  StringRef getString() const { return S; }

  void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  unsigned sizeOf(const dwarf::FormParams &, dwarf::Form) const;

  void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A pointer to another debug information entry.  An instance of this class can
/// also be used as a proxy for a debug information entry not yet defined
/// (ie. types.)
class DIEEntry {
  DIE *Entry;

public:
  DIEEntry() = delete;
  explicit DIEEntry(DIE &E) : Entry(&E) {}

  DIE &getEntry() const { return *Entry; }

  void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  unsigned sizeOf(const dwarf::FormParams &FormParams, dwarf::Form Form) const;

  void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// Represents a pointer to a location list in the debug_loc
/// section.
class DIELocList {
  /// Index into the .debug_loc vector.
  size_t Index;

public:
  DIELocList(size_t I) : Index(I) {}

  /// Grab the current index out.
  size_t getValue() const { return Index; }

  void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  unsigned sizeOf(const dwarf::FormParams &FormParams, dwarf::Form Form) const;

  void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A BaseTypeRef DIE.
class DIEAddrOffset {
  DIEInteger Addr;
  DIEDelta Offset;

public:
  explicit DIEAddrOffset(uint64_t Idx, const MCSymbol *Hi, const MCSymbol *Lo)
      : Addr(Idx), Offset(Hi, Lo) {}

  void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  unsigned sizeOf(const dwarf::FormParams &FormParams, dwarf::Form Form) const;

  void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A debug information entry value. Some of these roughly correlate
/// to DWARF attribute classes.
class DIEBlock;
class DIELoc;
class DIEValue {
public:
  enum Type {
    isNone,
#define HANDLE_DIEVALUE(T) is##T,
#include "llvm/CodeGen/DIEValue.def"
  };

private:
  /// Type of data stored in the value.
  Type Ty = isNone;
  dwarf::Attribute Attribute = (dwarf::Attribute)0;
  dwarf::Form Form = (dwarf::Form)0;

  /// Storage for the value.
  ///
  /// All values that aren't standard layout (or are larger than 8 bytes)
  /// should be stored by reference instead of by value.
  using ValTy =
      AlignedCharArrayUnion<DIEInteger, DIEString, DIEExpr, DIELabel,
                            DIEDelta *, DIEEntry, DIEBlock *, DIELoc *,
                            DIELocList, DIEBaseTypeRef *, DIEAddrOffset *>;

  static_assert(sizeof(ValTy) <= sizeof(uint64_t) ||
                    sizeof(ValTy) <= sizeof(void *),
                "Expected all large types to be stored via pointer");

  /// Underlying stored value.
  ValTy Val;

  template <class T> void construct(T V) {
    static_assert(std::is_standard_layout<T>::value ||
                      std::is_pointer<T>::value,
                  "Expected standard layout or pointer");
    new (reinterpret_cast<void *>(&Val)) T(V);
  }

  template <class T> T *get() { return reinterpret_cast<T *>(&Val); }
  template <class T> const T *get() const {
    return reinterpret_cast<const T *>(&Val);
  }
  template <class T> void destruct() { get<T>()->~T(); }

  /// Destroy the underlying value.
  ///
  /// This should get optimized down to a no-op.  We could skip it if we could
  /// add a static assert on \a std::is_trivially_copyable(), but we currently
  /// support versions of GCC that don't understand that.
  void destroyVal() {
    switch (Ty) {
    case isNone:
      return;
#define HANDLE_DIEVALUE_SMALL(T)                                               \
  case is##T:                                                                  \
    destruct<DIE##T>();                                                        \
    return;
#define HANDLE_DIEVALUE_LARGE(T)                                               \
  case is##T:                                                                  \
    destruct<const DIE##T *>();                                                \
    return;
#include "llvm/CodeGen/DIEValue.def"
    }
  }

  /// Copy the underlying value.
  ///
  /// This should get optimized down to a simple copy.  We need to actually
  /// construct the value, rather than calling memcpy, to satisfy strict
  /// aliasing rules.
  void copyVal(const DIEValue &X) {
    switch (Ty) {
    case isNone:
      return;
#define HANDLE_DIEVALUE_SMALL(T)                                               \
  case is##T:                                                                  \
    construct<DIE##T>(*X.get<DIE##T>());                                       \
    return;
#define HANDLE_DIEVALUE_LARGE(T)                                               \
  case is##T:                                                                  \
    construct<const DIE##T *>(*X.get<const DIE##T *>());                       \
    return;
#include "llvm/CodeGen/DIEValue.def"
    }
  }

public:
  DIEValue() = default;

  DIEValue(const DIEValue &X) : Ty(X.Ty), Attribute(X.Attribute), Form(X.Form) {
    copyVal(X);
  }

  DIEValue &operator=(const DIEValue &X) {
    if (this == &X)
      return *this;
    destroyVal();
    Ty = X.Ty;
    Attribute = X.Attribute;
    Form = X.Form;
    copyVal(X);
    return *this;
  }

  ~DIEValue() { destroyVal(); }

#define HANDLE_DIEVALUE_SMALL(T)                                               \
  DIEValue(dwarf::Attribute Attribute, dwarf::Form Form, const DIE##T &V)      \
      : Ty(is##T), Attribute(Attribute), Form(Form) {                          \
    construct<DIE##T>(V);                                                      \
  }
#define HANDLE_DIEVALUE_LARGE(T)                                               \
  DIEValue(dwarf::Attribute Attribute, dwarf::Form Form, const DIE##T *V)      \
      : Ty(is##T), Attribute(Attribute), Form(Form) {                          \
    assert(V && "Expected valid value");                                       \
    construct<const DIE##T *>(V);                                              \
  }
#include "llvm/CodeGen/DIEValue.def"

  /// Accessors.
  /// @{
  Type getType() const { return Ty; }
  dwarf::Attribute getAttribute() const { return Attribute; }
  dwarf::Form getForm() const { return Form; }
  explicit operator bool() const { return Ty; }
  /// @}

#define HANDLE_DIEVALUE_SMALL(T)                                               \
  const DIE##T &getDIE##T() const {                                            \
    assert(getType() == is##T && "Expected " #T);                              \
    return *get<DIE##T>();                                                     \
  }
#define HANDLE_DIEVALUE_LARGE(T)                                               \
  const DIE##T &getDIE##T() const {                                            \
    assert(getType() == is##T && "Expected " #T);                              \
    return **get<const DIE##T *>();                                            \
  }
#include "llvm/CodeGen/DIEValue.def"

  /// Emit value via the Dwarf writer.
  void emitValue(const AsmPrinter *AP) const;

  /// Return the size of a value in bytes.
  unsigned sizeOf(const dwarf::FormParams &FormParams) const;

  void print(raw_ostream &O) const;
  void dump() const;
};

struct IntrusiveBackListNode {
  PointerIntPair<IntrusiveBackListNode *, 1> Next;

  IntrusiveBackListNode() : Next(this, true) {}

  IntrusiveBackListNode *getNext() const {
    return Next.getInt() ? nullptr : Next.getPointer();
  }
};

struct IntrusiveBackListBase {
  using Node = IntrusiveBackListNode;

  Node *Last = nullptr;

  bool empty() const { return !Last; }

  void push_back(Node &N) {
    assert(N.Next.getPointer() == &N && "Expected unlinked node");
    assert(N.Next.getInt() == true && "Expected unlinked node");

    if (Last) {
      N.Next = Last->Next;
      Last->Next.setPointerAndInt(&N, false);
    }
    Last = &N;
  }

  void push_front(Node &N) {
    assert(N.Next.getPointer() == &N && "Expected unlinked node");
    assert(N.Next.getInt() == true && "Expected unlinked node");

    if (Last) {
      N.Next.setPointerAndInt(Last->Next.getPointer(), false);
      Last->Next.setPointerAndInt(&N, true);
    } else {
      Last = &N;
    }
  }
};

template <class T> class IntrusiveBackList : IntrusiveBackListBase {
public:
  using IntrusiveBackListBase::empty;

  void push_back(T &N) { IntrusiveBackListBase::push_back(N); }
  void push_front(T &N) { IntrusiveBackListBase::push_front(N); }

  T &back() { return *static_cast<T *>(Last); }
  const T &back() const { return *static_cast<T *>(Last); }
  T &front() {
    return *static_cast<T *>(Last ? Last->Next.getPointer() : nullptr);
  }
  const T &front() const {
    return *static_cast<T *>(Last ? Last->Next.getPointer() : nullptr);
  }

  void takeNodes(IntrusiveBackList<T> &Other) {
    if (Other.empty())
      return;

    T *FirstNode = static_cast<T *>(Other.Last->Next.getPointer());
    T *IterNode = FirstNode;
    do {
      // Keep a pointer to the node and increment the iterator.
      T *TmpNode = IterNode;
      IterNode = static_cast<T *>(IterNode->Next.getPointer());

      // Unlink the node and push it back to this list.
      TmpNode->Next.setPointerAndInt(TmpNode, true);
      push_back(*TmpNode);
    } while (IterNode != FirstNode);

    Other.Last = nullptr;
  }

  bool deleteNode(T &N) {
    if (Last == &N) {
      Last = Last->Next.getPointer();
      Last->Next.setInt(true);
      return true;
    }

    Node *cur = Last;
    while (cur && cur->Next.getPointer()) {
      if (cur->Next.getPointer() == &N) {
        cur->Next.setPointer(cur->Next.getPointer()->Next.getPointer());
        return true;
      }
      cur = cur->Next.getPointer();
    }

    return false;
  }

  class const_iterator;
  class iterator
      : public iterator_facade_base<iterator, std::forward_iterator_tag, T> {
    friend class const_iterator;

    Node *N = nullptr;

  public:
    iterator() = default;
    explicit iterator(T *N) : N(N) {}

    iterator &operator++() {
      N = N->getNext();
      return *this;
    }

    explicit operator bool() const { return N; }
    T &operator*() const { return *static_cast<T *>(N); }

    bool operator==(const iterator &X) const { return N == X.N; }
  };

  class const_iterator
      : public iterator_facade_base<const_iterator, std::forward_iterator_tag,
                                    const T> {
    const Node *N = nullptr;

  public:
    const_iterator() = default;
    // Placate MSVC by explicitly scoping 'iterator'.
    const_iterator(typename IntrusiveBackList<T>::iterator X) : N(X.N) {}
    explicit const_iterator(const T *N) : N(N) {}

    const_iterator &operator++() {
      N = N->getNext();
      return *this;
    }

    explicit operator bool() const { return N; }
    const T &operator*() const { return *static_cast<const T *>(N); }

    bool operator==(const const_iterator &X) const { return N == X.N; }
  };

  iterator begin() {
    return Last ? iterator(static_cast<T *>(Last->Next.getPointer())) : end();
  }
  const_iterator begin() const {
    return const_cast<IntrusiveBackList *>(this)->begin();
  }
  iterator end() { return iterator(); }
  const_iterator end() const { return const_iterator(); }

  static iterator toIterator(T &N) { return iterator(&N); }
  static const_iterator toIterator(const T &N) { return const_iterator(&N); }
};

/// A list of DIE values.
///
/// This is a singly-linked list, but instead of reversing the order of
/// insertion, we keep a pointer to the back of the list so we can push in
/// order.
///
/// There are two main reasons to choose a linked list over a customized
/// vector-like data structure.
///
///  1. For teardown efficiency, we want DIEs to be BumpPtrAllocated.  Using a
///     linked list here makes this way easier to accomplish.
///  2. Carrying an extra pointer per \a DIEValue isn't expensive.  45% of DIEs
///     have 2 or fewer values, and 90% have 5 or fewer.  A vector would be
///     over-allocated by 50% on average anyway, the same cost as the
///     linked-list node.
class DIEValueList {
  struct Node : IntrusiveBackListNode {
    DIEValue V;

    explicit Node(DIEValue V) : V(V) {}
  };

  using ListTy = IntrusiveBackList<Node>;

  ListTy List;

public:
  class const_value_iterator;
  class value_iterator
      : public iterator_adaptor_base<value_iterator, ListTy::iterator,
                                     std::forward_iterator_tag, DIEValue> {
    friend class const_value_iterator;

    using iterator_adaptor =
        iterator_adaptor_base<value_iterator, ListTy::iterator,
                              std::forward_iterator_tag, DIEValue>;

  public:
    value_iterator() = default;
    explicit value_iterator(ListTy::iterator X) : iterator_adaptor(X) {}

    explicit operator bool() const { return bool(wrapped()); }
    DIEValue &operator*() const { return wrapped()->V; }
  };

  class const_value_iterator : public iterator_adaptor_base<
                                   const_value_iterator, ListTy::const_iterator,
                                   std::forward_iterator_tag, const DIEValue> {
    using iterator_adaptor =
        iterator_adaptor_base<const_value_iterator, ListTy::const_iterator,
                              std::forward_iterator_tag, const DIEValue>;

  public:
    const_value_iterator() = default;
    const_value_iterator(DIEValueList::value_iterator X)
        : iterator_adaptor(X.wrapped()) {}
    explicit const_value_iterator(ListTy::const_iterator X)
        : iterator_adaptor(X) {}

    explicit operator bool() const { return bool(wrapped()); }
    const DIEValue &operator*() const { return wrapped()->V; }
  };

  using value_range = iterator_range<value_iterator>;
  using const_value_range = iterator_range<const_value_iterator>;

  value_iterator addValue(BumpPtrAllocator &Alloc, const DIEValue &V) {
    List.push_back(*new (Alloc) Node(V));
    return value_iterator(ListTy::toIterator(List.back()));
  }
  template <class T>
  value_iterator addValue(BumpPtrAllocator &Alloc, dwarf::Attribute Attribute,
                          dwarf::Form Form, T &&Value) {
    return addValue(Alloc, DIEValue(Attribute, Form, std::forward<T>(Value)));
  }

  /* zr33: add method here */
  template <class T>
  bool replaceValue(BumpPtrAllocator &Alloc, dwarf::Attribute Attribute,
                    dwarf::Attribute NewAttribute, dwarf::Form Form,
                    T &&NewValue) {
    for (llvm::DIEValue &val : values()) {
      if (val.getAttribute() == Attribute) {
        val = *new (Alloc)
                  DIEValue(NewAttribute, Form, std::forward<T>(NewValue));
        return true;
      }
    }

    return false;
  }

  template <class T>
  bool replaceValue(BumpPtrAllocator &Alloc, dwarf::Attribute Attribute,
                    dwarf::Form Form, T &&NewValue) {
    for (llvm::DIEValue &val : values()) {
      if (val.getAttribute() == Attribute) {
        val = *new (Alloc) DIEValue(Attribute, Form, std::forward<T>(NewValue));
        return true;
      }
    }

    return false;
  }

  bool replaceValue(BumpPtrAllocator &Alloc, dwarf::Attribute Attribute,
                    dwarf::Form Form, DIEValue &NewValue) {
    for (llvm::DIEValue &val : values()) {
      if (val.getAttribute() == Attribute) {
        val = NewValue;
        return true;
      }
    }

    return false;
  }

  bool deleteValue(dwarf::Attribute Attribute) {

    for (auto &node : List) {
      if (node.V.getAttribute() == Attribute) {
        return List.deleteNode(node);
      }
    }

    return false;
  }
  /* end */

  /// Take ownership of the nodes in \p Other, and append them to the back of
  /// the list.
  void takeValues(DIEValueList &Other) { List.takeNodes(Other.List); }

  value_range values() {
    return make_range(value_iterator(List.begin()), value_iterator(List.end()));
  }
  const_value_range values() const {
    return make_range(const_value_iterator(List.begin()),
                      const_value_iterator(List.end()));
  }
};

//===--------------------------------------------------------------------===//
/// A structured debug information entry.  Has an abbreviation which
/// describes its organization.
class DIE : IntrusiveBackListNode, public DIEValueList {
  friend class IntrusiveBackList<DIE>;
  friend class DIEUnit;

  /// Dwarf unit relative offset.
  unsigned Offset = 0;
  /// Size of instance + children.
  unsigned Size = 0;
  unsigned AbbrevNumber = ~0u;
  /// Dwarf tag code.
  dwarf::Tag Tag = (dwarf::Tag)0;
  /// Set to true to force a DIE to emit an abbreviation that says it has
  /// children even when it doesn't. This is used for unit testing purposes.
  bool ForceChildren = false;
  /// Children DIEs.
  IntrusiveBackList<DIE> Children;

  /// The owner is either the parent DIE for children of other DIEs, or a
  /// DIEUnit which contains this DIE as its unit DIE.
  PointerUnion<DIE *, DIEUnit *> Owner;

  explicit DIE(dwarf::Tag Tag) : Tag(Tag) {}

public:
  DIE() = delete;
  DIE(const DIE &RHS) = delete;
  DIE(DIE &&RHS) = delete;
  DIE &operator=(const DIE &RHS) = delete;
  DIE &operator=(const DIE &&RHS) = delete;

  static DIE *get(BumpPtrAllocator &Alloc, dwarf::Tag Tag) {
    return new (Alloc) DIE(Tag);
  }

  // Accessors.
  unsigned getAbbrevNumber() const { return AbbrevNumber; }
  dwarf::Tag getTag() const { return Tag; }
  /// Get the compile/type unit relative offset of this DIE.
  unsigned getOffset() const {
    // A real Offset can't be zero because the unit headers are at offset zero.
    assert(Offset && "Offset being queried before it's been computed.");
    return Offset;
  }
  unsigned getSize() const {
    // A real Size can't be zero because it includes the non-empty abbrev code.
    assert(Size && "Size being queried before it's been ocmputed.");
    return Size;
  }
  bool hasChildren() const { return ForceChildren || !Children.empty(); }
  void setForceChildren(bool B) { ForceChildren = B; }

  using child_iterator = IntrusiveBackList<DIE>::iterator;
  using const_child_iterator = IntrusiveBackList<DIE>::const_iterator;
  using child_range = iterator_range<child_iterator>;
  using const_child_range = iterator_range<const_child_iterator>;

  child_range children() {
    return make_range(Children.begin(), Children.end());
  }
  const_child_range children() const {
    return make_range(Children.begin(), Children.end());
  }

  DIE *getParent() const;

  /// Generate the abbreviation for this DIE.
  ///
  /// Calculate the abbreviation for this, which should be uniqued and
  /// eventually used to call \a setAbbrevNumber().
  DIEAbbrev generateAbbrev() const;

  /// Set the abbreviation number for this DIE.
  void setAbbrevNumber(unsigned I) { AbbrevNumber = I; }

  /// Get the absolute offset within the .debug_info or .debug_types section
  /// for this DIE.
  uint64_t getDebugSectionOffset() const;

  /// Compute the offset of this DIE and all its children.
  ///
  /// This function gets called just before we are going to generate the debug
  /// information and gives each DIE a chance to figure out its CU relative DIE
  /// offset, unique its abbreviation and fill in the abbreviation code, and
  /// return the unit offset that points to where the next DIE will be emitted
  /// within the debug unit section. After this function has been called for all
  /// DIE objects, the DWARF can be generated since all DIEs will be able to
  /// properly refer to other DIE objects since all DIEs have calculated their
  /// offsets.
  ///
  /// \param FormParams Used when calculating sizes.
  /// \param AbbrevSet the abbreviation used to unique DIE abbreviations.
  /// \param CUOffset the compile/type unit relative offset in bytes.
  /// \returns the offset for the DIE that follows this DIE within the
  /// current compile/type unit.
  unsigned computeOffsetsAndAbbrevs(const dwarf::FormParams &FormParams,
                                    DIEAbbrevSet &AbbrevSet, unsigned CUOffset);

  /// Climb up the parent chain to get the compile unit or type unit DIE that
  /// this DIE belongs to.
  ///
  /// \returns the compile or type unit DIE that owns this DIE, or NULL if
  /// this DIE hasn't been added to a unit DIE.
  const DIE *getUnitDie() const;

  /// Climb up the parent chain to get the compile unit or type unit that this
  /// DIE belongs to.
  ///
  /// \returns the DIEUnit that represents the compile or type unit that owns
  /// this DIE, or NULL if this DIE hasn't been added to a unit DIE.
  DIEUnit *getUnit() const;

  void setOffset(unsigned O) { Offset = O; }
  void setSize(unsigned S) { Size = S; }

  /// Add a child to the DIE.
  DIE &addChild(DIE *Child) {
    assert(!Child->getParent() && "Child should be orphaned");
    Child->Owner = this;
    Children.push_back(*Child);
    return Children.back();
  }

  DIE &addChildFront(DIE *Child) {
    assert(!Child->getParent() && "Child should be orphaned");
    Child->Owner = this;
    Children.push_front(*Child);
    return Children.front();
  }

  /// Find a value in the DIE with the attribute given.
  ///
  /// Returns a default-constructed DIEValue (where \a DIEValue::getType()
  /// gives \a DIEValue::isNone) if no such attribute exists.
  DIEValue findAttribute(dwarf::Attribute Attribute) const;

  void print(raw_ostream &O, unsigned IndentCount = 0) const;
  void dump() const;
};

//===--------------------------------------------------------------------===//
/// Represents a compile or type unit.
class DIEUnit {
  /// The compile unit or type unit DIE. This variable must be an instance of
  /// DIE so that we can calculate the DIEUnit from any DIE by traversing the
  /// parent backchain and getting the Unit DIE, and then casting itself to a
  /// DIEUnit. This allows us to be able to find the DIEUnit for any DIE without
  /// having to store a pointer to the DIEUnit in each DIE instance.
  DIE Die;
  /// The section this unit will be emitted in. This may or may not be set to
  /// a valid section depending on the client that is emitting DWARF.
  MCSection *Section = nullptr;
  uint64_t Offset = 0; /// .debug_info or .debug_types absolute section offset.
protected:
  virtual ~DIEUnit() = default;

public:
  explicit DIEUnit(dwarf::Tag UnitTag);
  DIEUnit(const DIEUnit &RHS) = delete;
  DIEUnit(DIEUnit &&RHS) = delete;
  void operator=(const DIEUnit &RHS) = delete;
  void operator=(const DIEUnit &&RHS) = delete;
  /// Set the section that this DIEUnit will be emitted into.
  ///
  /// This function is used by some clients to set the section. Not all clients
  /// that emit DWARF use this section variable.
  void setSection(MCSection *Section) {
    assert(!this->Section);
    this->Section = Section;
  }

  virtual const MCSymbol *getCrossSectionRelativeBaseAddress() const {
    return nullptr;
  }

  /// Return the section that this DIEUnit will be emitted into.
  ///
  /// \returns Section pointer which can be NULL.
  MCSection *getSection() const { return Section; }
  void setDebugSectionOffset(uint64_t O) { Offset = O; }
  uint64_t getDebugSectionOffset() const { return Offset; }
  DIE &getUnitDie() { return Die; }
  const DIE &getUnitDie() const { return Die; }
};

struct BasicDIEUnit final : DIEUnit {
  explicit BasicDIEUnit(dwarf::Tag UnitTag) : DIEUnit(UnitTag) {}
};

//===--------------------------------------------------------------------===//
/// DIELoc - Represents an expression location.
//
class DIELoc : public DIEValueList {
  mutable unsigned Size = 0; // Size in bytes excluding size header.

public:
  DIELoc() = default;

  /// Calculate the size of the location expression.
  unsigned computeSize(const dwarf::FormParams &FormParams) const;

  // TODO: move setSize() and Size to DIEValueList.
  void setSize(unsigned size) { Size = size; }

  /// BestForm - Choose the best form for data.
  ///
  dwarf::Form BestForm(unsigned DwarfVersion) const {
    if (DwarfVersion > 3)
      return dwarf::DW_FORM_exprloc;
    // Pre-DWARF4 location expressions were blocks and not exprloc.
    if ((unsigned char)Size == Size)
      return dwarf::DW_FORM_block1;
    if ((unsigned short)Size == Size)
      return dwarf::DW_FORM_block2;
    if ((unsigned int)Size == Size)
      return dwarf::DW_FORM_block4;
    return dwarf::DW_FORM_block;
  }

  void emitValue(const AsmPrinter *Asm, dwarf::Form Form) const;
  unsigned sizeOf(const dwarf::FormParams &, dwarf::Form Form) const;

  void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// DIEBlock - Represents a block of values.
//
class DIEBlock : public DIEValueList {
  mutable unsigned Size = 0; // Size in bytes excluding size header.

public:
  DIEBlock() = default;

  /// Calculate the size of the location expression.
  unsigned computeSize(const dwarf::FormParams &FormParams) const;

  // TODO: move setSize() and Size to DIEValueList.
  void setSize(unsigned size) { Size = size; }

  /// BestForm - Choose the best form for data.
  ///
  dwarf::Form BestForm() const {
    if ((unsigned char)Size == Size)
      return dwarf::DW_FORM_block1;
    if ((unsigned short)Size == Size)
      return dwarf::DW_FORM_block2;
    if ((unsigned int)Size == Size)
      return dwarf::DW_FORM_block4;
    return dwarf::DW_FORM_block;
  }

  void emitValue(const AsmPrinter *Asm, dwarf::Form Form) const;
  unsigned sizeOf(const dwarf::FormParams &, dwarf::Form Form) const;

  void print(raw_ostream &O) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_DIE_H
