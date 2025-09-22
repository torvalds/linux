//===- Symbol.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the Symbol.
//
//===----------------------------------------------------------------------===//

#include "llvm/TextAPI/Symbol.h"
#include <string>

namespace llvm {
namespace MachO {

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void Symbol::dump(raw_ostream &OS) const {
  std::string Result;
  if (isUndefined())
    Result += "(undef) ";
  if (isWeakDefined())
    Result += "(weak-def) ";
  if (isWeakReferenced())
    Result += "(weak-ref) ";
  if (isThreadLocalValue())
    Result += "(tlv) ";
  switch (Kind) {
  case EncodeKind::GlobalSymbol:
    Result += Name.str();
    break;
  case EncodeKind::ObjectiveCClass:
    Result += "(ObjC Class) " + Name.str();
    break;
  case EncodeKind::ObjectiveCClassEHType:
    Result += "(ObjC Class EH) " + Name.str();
    break;
  case EncodeKind::ObjectiveCInstanceVariable:
    Result += "(ObjC IVar) " + Name.str();
    break;
  }
  OS << Result;
}
#endif

Symbol::const_filtered_target_range
Symbol::targets(ArchitectureSet Architectures) const {
  std::function<bool(const Target &)> FN =
      [Architectures](const Target &Target) {
        return Architectures.has(Target.Arch);
      };
  return make_filter_range(Targets, FN);
}

bool Symbol::operator==(const Symbol &O) const {
  // Older Tapi files do not express all these symbol flags. In those
  // cases, ignore those differences.
  auto RemoveFlag = [](const Symbol &Sym, SymbolFlags &Flag) {
    if (Sym.isData())
      Flag &= ~SymbolFlags::Data;
    if (Sym.isText())
      Flag &= ~SymbolFlags::Text;
  };
  SymbolFlags LHSFlags = Flags;
  SymbolFlags RHSFlags = O.Flags;
  // Ignore Text and Data for now.
  RemoveFlag(*this, LHSFlags);
  RemoveFlag(O, RHSFlags);
  return std::tie(Name, Kind, Targets, LHSFlags) ==
         std::tie(O.Name, O.Kind, O.Targets, RHSFlags);
}

SimpleSymbol parseSymbol(StringRef SymName) {
  if (SymName.starts_with(ObjC1ClassNamePrefix))
    return {SymName.drop_front(ObjC1ClassNamePrefix.size()),
            EncodeKind::ObjectiveCClass, ObjCIFSymbolKind::Class};
  if (SymName.starts_with(ObjC2ClassNamePrefix))
    return {SymName.drop_front(ObjC2ClassNamePrefix.size()),
            EncodeKind::ObjectiveCClass, ObjCIFSymbolKind::Class};
  if (SymName.starts_with(ObjC2MetaClassNamePrefix))
    return {SymName.drop_front(ObjC2MetaClassNamePrefix.size()),
            EncodeKind::ObjectiveCClass, ObjCIFSymbolKind::MetaClass};
  if (SymName.starts_with(ObjC2EHTypePrefix))
    return {SymName.drop_front(ObjC2EHTypePrefix.size()),
            EncodeKind::ObjectiveCClassEHType, ObjCIFSymbolKind::EHType};
  if (SymName.starts_with(ObjC2IVarPrefix))
    return {SymName.drop_front(ObjC2IVarPrefix.size()),
            EncodeKind::ObjectiveCInstanceVariable, ObjCIFSymbolKind::None};
  return {SymName, EncodeKind::GlobalSymbol, ObjCIFSymbolKind::None};
}

} // end namespace MachO.
} // end namespace llvm.
