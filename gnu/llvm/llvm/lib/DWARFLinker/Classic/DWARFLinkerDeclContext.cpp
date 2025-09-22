//===- DWARFLinkerDeclContext.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DWARFLinker/Classic/DWARFLinkerDeclContext.h"
#include "llvm/DWARFLinker/Classic/DWARFLinkerCompileUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"

namespace llvm {

using namespace dwarf_linker;
using namespace dwarf_linker::classic;

/// Set the last DIE/CU a context was seen in and, possibly invalidate the
/// context if it is ambiguous.
///
/// In the current implementation, we don't handle overloaded functions well,
/// because the argument types are not taken into account when computing the
/// DeclContext tree.
///
/// Some of this is mitigated byt using mangled names that do contain the
/// arguments types, but sometimes (e.g. with function templates) we don't have
/// that. In that case, just do not unique anything that refers to the contexts
/// we are not able to distinguish.
///
/// If a context that is not a namespace appears twice in the same CU, we know
/// it is ambiguous. Make it invalid.
bool DeclContext::setLastSeenDIE(CompileUnit &U, const DWARFDie &Die) {
  if (LastSeenCompileUnitID == U.getUniqueID()) {
    DWARFUnit &OrigUnit = U.getOrigUnit();
    uint32_t FirstIdx = OrigUnit.getDIEIndex(LastSeenDIE);
    U.getInfo(FirstIdx).Ctxt = nullptr;
    return false;
  }

  LastSeenCompileUnitID = U.getUniqueID();
  LastSeenDIE = Die;
  return true;
}

PointerIntPair<DeclContext *, 1>
DeclContextTree::getChildDeclContext(DeclContext &Context, const DWARFDie &DIE,
                                     CompileUnit &U, bool InClangModule) {
  unsigned Tag = DIE.getTag();

  // FIXME: dsymutil-classic compat: We should bail out here if we
  // have a specification or an abstract_origin. We will get the
  // parent context wrong here.

  switch (Tag) {
  default:
    // By default stop gathering child contexts.
    return PointerIntPair<DeclContext *, 1>(nullptr);
  case dwarf::DW_TAG_module:
    break;
  case dwarf::DW_TAG_compile_unit:
    return PointerIntPair<DeclContext *, 1>(&Context);
  case dwarf::DW_TAG_subprogram:
    // Do not unique anything inside CU local functions.
    if ((Context.getTag() == dwarf::DW_TAG_namespace ||
         Context.getTag() == dwarf::DW_TAG_compile_unit) &&
        !dwarf::toUnsigned(DIE.find(dwarf::DW_AT_external), 0))
      return PointerIntPair<DeclContext *, 1>(nullptr);
    [[fallthrough]];
  case dwarf::DW_TAG_member:
  case dwarf::DW_TAG_namespace:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_enumeration_type:
  case dwarf::DW_TAG_typedef:
    // Artificial things might be ambiguous, because they might be created on
    // demand. For example implicitly defined constructors are ambiguous
    // because of the way we identify contexts, and they won't be generated
    // every time everywhere.
    if (dwarf::toUnsigned(DIE.find(dwarf::DW_AT_artificial), 0))
      return PointerIntPair<DeclContext *, 1>(nullptr);
    break;
  }

  StringRef NameRef;
  StringRef FileRef;

  if (const char *LinkageName = DIE.getLinkageName())
    NameRef = StringPool.internString(LinkageName);
  else if (const char *ShortName = DIE.getShortName())
    NameRef = StringPool.internString(ShortName);

  bool IsAnonymousNamespace = NameRef.empty() && Tag == dwarf::DW_TAG_namespace;
  if (IsAnonymousNamespace) {
    // FIXME: For dsymutil-classic compatibility. I think uniquing within
    // anonymous namespaces is wrong. There is no ODR guarantee there.
    NameRef = "(anonymous namespace)";
  }

  if (Tag != dwarf::DW_TAG_class_type && Tag != dwarf::DW_TAG_structure_type &&
      Tag != dwarf::DW_TAG_union_type &&
      Tag != dwarf::DW_TAG_enumeration_type && NameRef.empty())
    return PointerIntPair<DeclContext *, 1>(nullptr);

  unsigned Line = 0;
  unsigned ByteSize = std::numeric_limits<uint32_t>::max();

  if (!InClangModule) {
    // Gather some discriminating data about the DeclContext we will be
    // creating: File, line number and byte size. This shouldn't be necessary,
    // because the ODR is just about names, but given that we do some
    // approximations with overloaded functions and anonymous namespaces, use
    // these additional data points to make the process safer.
    //
    // This is disabled for clang modules, because forward declarations of
    // module-defined types do not have a file and line.
    ByteSize = dwarf::toUnsigned(DIE.find(dwarf::DW_AT_byte_size),
                                 std::numeric_limits<uint64_t>::max());
    if (Tag != dwarf::DW_TAG_namespace || IsAnonymousNamespace) {
      if (unsigned FileNum =
              dwarf::toUnsigned(DIE.find(dwarf::DW_AT_decl_file), 0)) {
        if (const auto *LT = U.getOrigUnit().getContext().getLineTableForUnit(
                &U.getOrigUnit())) {
          // FIXME: dsymutil-classic compatibility. I'd rather not
          // unique anything in anonymous namespaces, but if we do, then
          // verify that the file and line correspond.
          if (IsAnonymousNamespace)
            FileNum = 1;

          if (LT->hasFileAtIndex(FileNum)) {
            Line = dwarf::toUnsigned(DIE.find(dwarf::DW_AT_decl_line), 0);
            // Cache the resolved paths based on the index in the line table,
            // because calling realpath is expensive.
            FileRef = getResolvedPath(U, FileNum, *LT);
          }
        }
      }
    }
  }

  if (!Line && NameRef.empty())
    return PointerIntPair<DeclContext *, 1>(nullptr);

  // We hash NameRef, which is the mangled name, in order to get most
  // overloaded functions resolve correctly.
  //
  // Strictly speaking, hashing the Tag is only necessary for a
  // DW_TAG_module, to prevent uniquing of a module and a namespace
  // with the same name.
  //
  // FIXME: dsymutil-classic won't unique the same type presented
  // once as a struct and once as a class. Using the Tag in the fully
  // qualified name hash to get the same effect.
  unsigned Hash = hash_combine(Context.getQualifiedNameHash(), Tag, NameRef);

  // FIXME: dsymutil-classic compatibility: when we don't have a name,
  // use the filename.
  if (IsAnonymousNamespace)
    Hash = hash_combine(Hash, FileRef);

  // Now look if this context already exists.
  DeclContext Key(Hash, Line, ByteSize, Tag, NameRef, FileRef, Context);
  auto ContextIter = Contexts.find(&Key);

  if (ContextIter == Contexts.end()) {
    // The context wasn't found.
    bool Inserted;
    DeclContext *NewContext =
        new (Allocator) DeclContext(Hash, Line, ByteSize, Tag, NameRef, FileRef,
                                    Context, DIE, U.getUniqueID());
    std::tie(ContextIter, Inserted) = Contexts.insert(NewContext);
    assert(Inserted && "Failed to insert DeclContext");
    (void)Inserted;
  } else if (Tag != dwarf::DW_TAG_namespace &&
             !(*ContextIter)->setLastSeenDIE(U, DIE)) {
    // The context was found, but it is ambiguous with another context
    // in the same file. Mark it invalid.
    return PointerIntPair<DeclContext *, 1>(*ContextIter, /* IntVal= */ 1);
  }

  assert(ContextIter != Contexts.end());
  // FIXME: dsymutil-classic compatibility. Union types aren't
  // uniques, but their children might be.
  if ((Tag == dwarf::DW_TAG_subprogram &&
       Context.getTag() != dwarf::DW_TAG_structure_type &&
       Context.getTag() != dwarf::DW_TAG_class_type) ||
      (Tag == dwarf::DW_TAG_union_type))
    return PointerIntPair<DeclContext *, 1>(*ContextIter, /* IntVal= */ 1);

  return PointerIntPair<DeclContext *, 1>(*ContextIter);
}

StringRef
DeclContextTree::getResolvedPath(CompileUnit &CU, unsigned FileNum,
                                 const DWARFDebugLine::LineTable &LineTable) {
  std::pair<unsigned, unsigned> Key = {CU.getUniqueID(), FileNum};

  ResolvedPathsMap::const_iterator It = ResolvedPaths.find(Key);
  if (It == ResolvedPaths.end()) {
    std::string FileName;
    bool FoundFileName = LineTable.getFileNameByIndex(
        FileNum, CU.getOrigUnit().getCompilationDir(),
        DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, FileName);
    (void)FoundFileName;
    assert(FoundFileName && "Must get file name from line table");

    // Second level of caching, this time based on the file's parent
    // path.
    StringRef ResolvedPath = PathResolver.resolve(FileName, StringPool);

    It = ResolvedPaths.insert(std::make_pair(Key, ResolvedPath)).first;
  }

  return It->second;
}

} // namespace llvm
