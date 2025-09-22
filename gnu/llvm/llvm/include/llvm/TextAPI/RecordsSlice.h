//===- llvm/TextAPI/RecordSlice.h - TAPI RecordSlice ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the TAPI Record Collection Type.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_RECORDSLICE_H
#define LLVM_TEXTAPI_RECORDSLICE_H

#include "llvm/Support/Allocator.h"
#include "llvm/TextAPI/FileTypes.h"
#include "llvm/TextAPI/PackedVersion.h"
#include "llvm/TextAPI/Record.h"
#include "llvm/TextAPI/RecordVisitor.h"

namespace llvm {
namespace MachO {

// Define collection of records for a library that are tied to a darwin target
// triple.
class RecordsSlice {
public:
  RecordsSlice(const llvm::Triple &T) : TargetTriple(T), TAPITarget(T) {}
  /// Get target triple.
  const llvm::Triple &getTriple() const { return TargetTriple; }
  /// Get TAPI converted target.
  const Target &getTarget() const { return TAPITarget; }

  /// Add unspecified record to slice.
  ///
  /// Assign specific record type based on properties and symbol name.
  ///
  /// \param Name The name of symbol.
  /// \param Flags The flags that describe attributes of the symbol.
  /// \param GV The kind of global, if this represents a non obj-c global
  /// symbol.
  /// \param Linkage The linkage of symbol.
  /// \return The non-owning pointer to added record in slice.
  Record *addRecord(StringRef Name, SymbolFlags Flags,
                    GlobalRecord::Kind GV = GlobalRecord::Kind::Unknown,
                    RecordLinkage Linkage = RecordLinkage::Unknown);

  /// Add non-ObjC global record.
  ///
  /// \param Name The name of symbol.
  /// \param Linkage The linkage of symbol.
  /// \param GV The kind of global.
  /// \param Flags The flags that describe attributes of the symbol.
  /// \param Inlined Whether declaration is inlined, only applicable to
  /// functions.
  /// \return The non-owning pointer to added record in slice.
  GlobalRecord *addGlobal(StringRef Name, RecordLinkage Linkage,
                          GlobalRecord::Kind GV,
                          SymbolFlags Flags = SymbolFlags::None,
                          bool Inlined = false);

  /// Add ObjC Class record.
  ///
  /// \param Name The name of class, not symbol.
  /// \param Linkage The linkage of symbol.
  /// \param SymType The symbols this class represents.
  /// \return The non-owning pointer to added record in slice.
  ObjCInterfaceRecord *addObjCInterface(StringRef Name, RecordLinkage Linkage,
                                        ObjCIFSymbolKind SymType);

  /// Add ObjC IVar record.
  ///
  /// \param Container Owning pointer for instance variable.
  /// \param Name The name of ivar, not symbol.
  /// \param Linkage The linkage of symbol.
  /// \return The non-owning pointer to added record in slice.
  ObjCIVarRecord *addObjCIVar(ObjCContainerRecord *Container, StringRef Name,
                              RecordLinkage Linkage);

  /// Add ObjC Category record.
  ///
  /// \param ClassToExtend The name of class that is being extended by the
  /// category, not symbol.
  /// \param Category The name of category.
  /// \return The non-owning pointer to added record in slice.
  ObjCCategoryRecord *addObjCCategory(StringRef ClassToExtend,
                                      StringRef Category);

  /// Find ObjC Class.
  ///
  /// \param Name name of class, not full symbol name.
  /// \return The non-owning pointer to record in slice.
  ObjCInterfaceRecord *findObjCInterface(StringRef Name) const;

  /// Find ObjC Category.
  ///
  /// \param ClassToExtend The name of class, not full symbol name.
  /// \param Category The name of category.
  /// \return The non-owning pointer to record in slice.
  ObjCCategoryRecord *findObjCCategory(StringRef ClassToExtend,
                                       StringRef Category) const;

  /// Find ObjC Container. This is commonly used for assigning for looking up
  /// instance variables that are assigned to either a category or class.
  ///
  /// \param IsIVar If true, the name is the name of the IVar, otherwise it will
  /// be looked up as the name of the container.
  /// \param Name Either the name of ivar or name of container.
  /// \return The non-owning pointer to record in
  /// slice.
  ObjCContainerRecord *findContainer(bool IsIVar, StringRef Name) const;

  /// Find ObjC instance variable.
  ///
  /// \param IsScopedName This is used to determine how to parse the name.
  /// \param Name Either the full name of the symbol or just the ivar.
  /// \return The non-owning pointer to record in slice.
  ObjCIVarRecord *findObjCIVar(bool IsScopedName, StringRef Name) const;

  /// Find non-objc global.
  ///
  /// \param Name The name of symbol.
  /// \param GV The Kind of global to find.
  /// \return The non-owning pointer to record in slice.
  GlobalRecord *
  findGlobal(StringRef Name,
             GlobalRecord::Kind GV = GlobalRecord::Kind::Unknown) const;

  // Determine if library attributes were assigned.
  bool hasBinaryAttrs() const { return BA.get(); }

  // Determine if record slice is unassigned.
  bool empty() const {
    return !hasBinaryAttrs() && Globals.empty() && Classes.empty() &&
           Categories.empty();
  }

  // Visit all records known to RecordsSlice.
  void visit(RecordVisitor &V) const;

  struct BinaryAttrs {
    std::vector<StringRef> AllowableClients;
    std::vector<StringRef> RexportedLibraries;
    std::vector<StringRef> RPaths;
    StringRef ParentUmbrella;
    StringRef InstallName;
    StringRef UUID;
    StringRef Path;
    FileType File = FileType::Invalid;
    llvm::MachO::PackedVersion CurrentVersion;
    llvm::MachO::PackedVersion CompatVersion;
    uint8_t SwiftABI = 0;
    bool TwoLevelNamespace = false;
    bool AppExtensionSafe = false;
    bool OSLibNotForSharedCache = false;
  };

  /// Return reference to BinaryAttrs.
  BinaryAttrs &getBinaryAttrs();

  /// Store any strings owned by RecordSlice into allocator and return back
  /// reference to that.
  StringRef copyString(StringRef String);

private:
  const llvm::Triple TargetTriple;
  // Hold tapi converted triple to avoid unecessary casts.
  const Target TAPITarget;

  /// BumpPtrAllocator to store generated/copied strings.
  llvm::BumpPtrAllocator StringAllocator;

  /// Promote linkage of requested record. It is no-op if linkage type is lower
  /// than the current assignment.
  ///
  /// \param R The record to update.
  /// \param L Linkage type to update to.
  void updateLinkage(Record *R, RecordLinkage L) {
    R->Linkage = std::max(R->Linkage, L);
  }

  /// Update set flags of requested record.
  ///
  /// \param R The record to update.
  /// \param F Flags to update to.
  void updateFlags(Record *R, SymbolFlags F) { R->Flags |= F; }

  RecordMap<GlobalRecord> Globals;
  RecordMap<ObjCInterfaceRecord> Classes;
  RecordMap<ObjCCategoryRecord, std::pair<StringRef, StringRef>> Categories;

  std::unique_ptr<BinaryAttrs> BA{nullptr};
};

using Records = llvm::SmallVector<std::shared_ptr<RecordsSlice>, 4>;
class InterfaceFile;
std::unique_ptr<InterfaceFile> convertToInterfaceFile(const Records &Slices);

} // namespace MachO
} // namespace llvm
#endif // LLVM_TEXTAPI_RECORDSLICE_H
