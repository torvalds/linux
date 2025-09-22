//===- PDBSymbol.h - base class for user-facing symbol types -----*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOL_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOL_H

#include "IPDBRawSymbol.h"
#include "PDBExtras.h"
#include "PDBTypes.h"
#include "llvm/Support/Casting.h"

#define FORWARD_SYMBOL_METHOD(MethodName)                                      \
  decltype(auto) MethodName() const { return RawSymbol->MethodName(); }

#define FORWARD_CONCRETE_SYMBOL_ID_METHOD_WITH_NAME(ConcreteType, PrivateName, \
                                                    PublicName)                \
  decltype(auto) PublicName##Id() const {                                      \
    return RawSymbol->PrivateName##Id();                                       \
  }                                                                            \
  std::unique_ptr<ConcreteType> PublicName() const {                           \
    uint32_t Id = PublicName##Id();                                            \
    return getConcreteSymbolByIdHelper<ConcreteType>(Id);                      \
  }

#define FORWARD_SYMBOL_ID_METHOD_WITH_NAME(PrivateName, PublicName)            \
  FORWARD_CONCRETE_SYMBOL_ID_METHOD_WITH_NAME(PDBSymbol, PrivateName,          \
                                              PublicName)

#define FORWARD_SYMBOL_ID_METHOD(MethodName)                                   \
  FORWARD_SYMBOL_ID_METHOD_WITH_NAME(MethodName, MethodName)

namespace llvm {

class StringRef;
class raw_ostream;

namespace pdb {
class IPDBSession;
class PDBSymDumper;
class PDBSymbol;
template <typename ChildType> class ConcreteSymbolEnumerator;

#define DECLARE_PDB_SYMBOL_CONCRETE_TYPE(TagValue)                             \
private:                                                                       \
  using PDBSymbol::PDBSymbol;                                                  \
  friend class PDBSymbol;                                                      \
                                                                               \
public:                                                                        \
  static const PDB_SymType Tag = TagValue;                                     \
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

#define DECLARE_PDB_SYMBOL_CUSTOM_TYPE(Condition)                              \
private:                                                                       \
  using PDBSymbol::PDBSymbol;                                                  \
  friend class PDBSymbol;                                                      \
                                                                               \
public:                                                                        \
  static bool classof(const PDBSymbol *S) { return Condition; }

/// PDBSymbol defines the base of the inheritance hierarchy for concrete symbol
/// types (e.g. functions, executables, vtables, etc).  All concrete symbol
/// types inherit from PDBSymbol and expose the exact set of methods that are
/// valid for that particular symbol type, as described in the Microsoft
/// reference "Lexical and Class Hierarchy of Symbol Types":
/// https://msdn.microsoft.com/en-us/library/370hs6k4.aspx
class PDBSymbol {
  static std::unique_ptr<PDBSymbol> createSymbol(const IPDBSession &PDBSession,
                                                 PDB_SymType Tag);

protected:
  explicit PDBSymbol(const IPDBSession &PDBSession);
  PDBSymbol(PDBSymbol &&Other);

public:
  static std::unique_ptr<PDBSymbol>
  create(const IPDBSession &PDBSession,
         std::unique_ptr<IPDBRawSymbol> RawSymbol);
  static std::unique_ptr<PDBSymbol> create(const IPDBSession &PDBSession,
                                           IPDBRawSymbol &RawSymbol);

  template <typename ConcreteT>
  static std::unique_ptr<ConcreteT>
  createAs(const IPDBSession &PDBSession,
           std::unique_ptr<IPDBRawSymbol> RawSymbol) {
    std::unique_ptr<PDBSymbol> S = create(PDBSession, std::move(RawSymbol));
    return unique_dyn_cast_or_null<ConcreteT>(std::move(S));
  }
  template <typename ConcreteT>
  static std::unique_ptr<ConcreteT> createAs(const IPDBSession &PDBSession,
                                             IPDBRawSymbol &RawSymbol) {
    std::unique_ptr<PDBSymbol> S = create(PDBSession, RawSymbol);
    return unique_dyn_cast_or_null<ConcreteT>(std::move(S));
  }

  virtual ~PDBSymbol();

  /// Dumps the contents of a symbol a raw_ostream.  By default this will just
  /// call dump() on the underlying RawSymbol, which allows us to discover
  /// unknown properties, but individual implementations of PDBSymbol may
  /// override the behavior to only dump known fields.
  virtual void dump(PDBSymDumper &Dumper) const = 0;

  /// For certain PDBSymbolTypes, dumps additional information for the type that
  /// normally goes on the right side of the symbol.
  virtual void dumpRight(PDBSymDumper &Dumper) const {}

  void defaultDump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowFlags,
                   PdbSymbolIdField RecurseFlags) const;
  void dumpProperties() const;
  void dumpChildStats() const;

  PDB_SymType getSymTag() const;
  uint32_t getSymIndexId() const;

  template <typename T> std::unique_ptr<T> findOneChild() const {
    auto Enumerator(findAllChildren<T>());
    if (!Enumerator)
      return nullptr;
    return Enumerator->getNext();
  }

  template <typename T>
  std::unique_ptr<ConcreteSymbolEnumerator<T>> findAllChildren() const {
    auto BaseIter = RawSymbol->findChildren(T::Tag);
    if (!BaseIter)
      return nullptr;
    return std::make_unique<ConcreteSymbolEnumerator<T>>(std::move(BaseIter));
  }
  std::unique_ptr<IPDBEnumSymbols> findAllChildren(PDB_SymType Type) const;
  std::unique_ptr<IPDBEnumSymbols> findAllChildren() const;

  std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type, StringRef Name,
               PDB_NameSearchFlags Flags) const;
  std::unique_ptr<IPDBEnumSymbols> findChildrenByRVA(PDB_SymType Type,
                                                     StringRef Name,
                                                     PDB_NameSearchFlags Flags,
                                                     uint32_t RVA) const;
  std::unique_ptr<IPDBEnumSymbols> findInlineFramesByVA(uint64_t VA) const;
  std::unique_ptr<IPDBEnumSymbols> findInlineFramesByRVA(uint32_t RVA) const;
  std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByVA(uint64_t VA, uint32_t Length) const;
  std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByRVA(uint32_t RVA, uint32_t Length) const;

  std::string getName() const;

  const IPDBRawSymbol &getRawSymbol() const { return *RawSymbol; }
  IPDBRawSymbol &getRawSymbol() { return *RawSymbol; }

  const IPDBSession &getSession() const { return Session; }

  std::unique_ptr<IPDBEnumSymbols> getChildStats(TagStats &Stats) const;

protected:
  std::unique_ptr<PDBSymbol> getSymbolByIdHelper(uint32_t Id) const;

  template <typename ConcreteType>
  std::unique_ptr<ConcreteType> getConcreteSymbolByIdHelper(uint32_t Id) const {
    return unique_dyn_cast_or_null<ConcreteType>(getSymbolByIdHelper(Id));
  }

  const IPDBSession &Session;
  std::unique_ptr<IPDBRawSymbol> OwnedRawSymbol;
  IPDBRawSymbol *RawSymbol = nullptr;
};

} // namespace llvm
}

#endif
