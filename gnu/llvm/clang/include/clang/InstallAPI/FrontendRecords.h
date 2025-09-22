//===- InstallAPI/FrontendRecords.h ------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INSTALLAPI_FRONTENDRECORDS_H
#define LLVM_CLANG_INSTALLAPI_FRONTENDRECORDS_H

#include "clang/AST/Availability.h"
#include "clang/AST/DeclObjC.h"
#include "clang/InstallAPI/HeaderFile.h"
#include "clang/InstallAPI/MachO.h"

namespace clang {
namespace installapi {

/// Frontend information captured about records.
struct FrontendAttrs {
  const AvailabilityInfo Avail;
  const Decl *D;
  const SourceLocation Loc;
  const HeaderType Access;
};

// Represents a collection of frontend records for a library that are tied to a
// darwin target triple.
class FrontendRecordsSlice : public llvm::MachO::RecordsSlice {
public:
  FrontendRecordsSlice(const llvm::Triple &T)
      : llvm::MachO::RecordsSlice({T}) {}

  /// Add non-ObjC global record with attributes from AST.
  ///
  /// \param Name The name of symbol.
  /// \param Linkage The linkage of symbol.
  /// \param GV The kind of global.
  /// \param Avail The availability information tied to the active target
  /// triple.
  /// \param D The pointer to the declaration from traversing AST.
  /// \param Access The intended access level of symbol.
  /// \param Flags The flags that describe attributes of the symbol.
  /// \param Inlined Whether declaration is inlined, only applicable to
  /// functions.
  /// \return The non-owning pointer to added record in slice with it's frontend
  /// attributes.
  std::pair<GlobalRecord *, FrontendAttrs *>
  addGlobal(StringRef Name, RecordLinkage Linkage, GlobalRecord::Kind GV,
            const clang::AvailabilityInfo Avail, const Decl *D,
            const HeaderType Access, SymbolFlags Flags = SymbolFlags::None,
            bool Inlined = false);

  /// Add ObjC Class record with attributes from AST.
  ///
  /// \param Name The name of class, not symbol.
  /// \param Linkage The linkage of symbol.
  /// \param Avail The availability information tied to the active target
  /// triple.
  /// \param D The pointer to the declaration from traversing AST.
  /// \param Access The intended access level of symbol.
  /// \param IsEHType Whether declaration has an exception attribute.
  /// \return The non-owning pointer to added record in slice with it's frontend
  /// attributes.
  std::pair<ObjCInterfaceRecord *, FrontendAttrs *>
  addObjCInterface(StringRef Name, RecordLinkage Linkage,
                   const clang::AvailabilityInfo Avail, const Decl *D,
                   HeaderType Access, bool IsEHType);

  /// Add ObjC Category record with attributes from AST.
  ///
  /// \param ClassToExtend The name of class that is extended by category, not
  /// symbol.
  /// \param CategoryName The name of category, not symbol.
  /// \param Avail The availability information tied
  /// to the active target triple.
  /// \param D The pointer to the declaration from traversing AST.
  /// \param Access The intended access level of symbol.
  /// \return The non-owning pointer to added record in slice with it's frontend
  /// attributes.
  std::pair<ObjCCategoryRecord *, FrontendAttrs *>
  addObjCCategory(StringRef ClassToExtend, StringRef CategoryName,
                  const clang::AvailabilityInfo Avail, const Decl *D,
                  HeaderType Access);

  /// Add ObjC IVar record with attributes from AST.
  ///
  /// \param Container The owning pointer for instance variable.
  /// \param Name The name of ivar, not symbol.
  /// \param Linkage The linkage of symbol.
  /// \param Avail The availability information tied to the active target
  /// triple.
  /// \param D The pointer to the declaration from traversing AST.
  /// \param Access The intended access level of symbol.
  /// \param AC The access control tied to the ivar declaration.
  /// \return The non-owning pointer to added record in slice with it's frontend
  /// attributes.
  std::pair<ObjCIVarRecord *, FrontendAttrs *>
  addObjCIVar(ObjCContainerRecord *Container, StringRef IvarName,
              RecordLinkage Linkage, const clang::AvailabilityInfo Avail,
              const Decl *D, HeaderType Access,
              const clang::ObjCIvarDecl::AccessControl AC);

private:
  /// Mapping of records stored in slice to their frontend attributes.
  llvm::DenseMap<Record *, FrontendAttrs> FrontendRecords;
};

} // namespace installapi
} // namespace clang

#endif // LLVM_CLANG_INSTALLAPI_FRONTENDRECORDS_H
