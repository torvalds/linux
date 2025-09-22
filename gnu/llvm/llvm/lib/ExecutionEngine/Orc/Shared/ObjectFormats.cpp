//===---------- ObjectFormats.cpp - Object format details for ORC ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ORC-specific object format details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Shared/ObjectFormats.h"

namespace llvm {
namespace orc {

StringRef MachODataCommonSectionName = "__DATA,__common";
StringRef MachODataDataSectionName = "__DATA,__data";
StringRef MachOEHFrameSectionName = "__TEXT,__eh_frame";
StringRef MachOCompactUnwindInfoSectionName = "__TEXT,__unwind_info";
StringRef MachOCStringSectionName = "__TEXT,__cstring";
StringRef MachOModInitFuncSectionName = "__DATA,__mod_init_func";
StringRef MachOObjCCatListSectionName = "__DATA,__objc_catlist";
StringRef MachOObjCCatList2SectionName = "__DATA,__objc_catlist2";
StringRef MachOObjCClassListSectionName = "__DATA,__objc_classlist";
StringRef MachOObjCClassNameSectionName = "__TEXT,__objc_classname";
StringRef MachOObjCClassRefsSectionName = "__DATA,__objc_classrefs";
StringRef MachOObjCConstSectionName = "__DATA,__objc_const";
StringRef MachOObjCDataSectionName = "__DATA,__objc_data";
StringRef MachOObjCImageInfoSectionName = "__DATA,__objc_imageinfo";
StringRef MachOObjCMethNameSectionName = "__TEXT,__objc_methname";
StringRef MachOObjCMethTypeSectionName = "__TEXT,__objc_methtype";
StringRef MachOObjCNLCatListSectionName = "__DATA,__objc_nlcatlist";
StringRef MachOObjCNLClassListSectionName = "__DATA,__objc_nlclslist";
StringRef MachOObjCProtoListSectionName = "__DATA,__objc_protolist";
StringRef MachOObjCProtoRefsSectionName = "__DATA,__objc_protorefs";
StringRef MachOObjCSelRefsSectionName = "__DATA,__objc_selrefs";
StringRef MachOSwift5ProtoSectionName = "__TEXT,__swift5_proto";
StringRef MachOSwift5ProtosSectionName = "__TEXT,__swift5_protos";
StringRef MachOSwift5TypesSectionName = "__TEXT,__swift5_types";
StringRef MachOSwift5TypeRefSectionName = "__TEXT,__swift5_typeref";
StringRef MachOSwift5FieldMetadataSectionName = "__TEXT,__swift5_fieldmd";
StringRef MachOSwift5EntrySectionName = "__TEXT,__swift5_entry";
StringRef MachOThreadBSSSectionName = "__DATA,__thread_bss";
StringRef MachOThreadDataSectionName = "__DATA,__thread_data";
StringRef MachOThreadVarsSectionName = "__DATA,__thread_vars";

StringRef MachOInitSectionNames[22] = {
    MachOModInitFuncSectionName,         MachOObjCCatListSectionName,
    MachOObjCCatList2SectionName,        MachOObjCClassListSectionName,
    MachOObjCClassNameSectionName,       MachOObjCClassRefsSectionName,
    MachOObjCConstSectionName,           MachOObjCDataSectionName,
    MachOObjCImageInfoSectionName,       MachOObjCMethNameSectionName,
    MachOObjCMethTypeSectionName,        MachOObjCNLCatListSectionName,
    MachOObjCNLClassListSectionName,     MachOObjCProtoListSectionName,
    MachOObjCProtoRefsSectionName,       MachOObjCSelRefsSectionName,
    MachOSwift5ProtoSectionName,         MachOSwift5ProtosSectionName,
    MachOSwift5TypesSectionName,         MachOSwift5TypeRefSectionName,
    MachOSwift5FieldMetadataSectionName, MachOSwift5EntrySectionName,
};

StringRef ELFEHFrameSectionName = ".eh_frame";

StringRef ELFInitArrayFuncSectionName = ".init_array";
StringRef ELFInitFuncSectionName = ".init";
StringRef ELFFiniArrayFuncSectionName = ".fini_array";
StringRef ELFFiniFuncSectionName = ".fini";
StringRef ELFCtorArrayFuncSectionName = ".ctors";
StringRef ELFDtorArrayFuncSectionName = ".dtors";

StringRef ELFInitSectionNames[3]{
    ELFInitArrayFuncSectionName,
    ELFInitFuncSectionName,
    ELFCtorArrayFuncSectionName,
};

StringRef ELFThreadBSSSectionName = ".tbss";
StringRef ELFThreadDataSectionName = ".tdata";

bool isMachOInitializerSection(StringRef SegName, StringRef SecName) {
  for (auto &InitSection : MachOInitSectionNames) {
    // Loop below assumes all MachO init sectios have a length-6
    // segment name.
    assert(InitSection[6] == ',' && "Init section seg name has length != 6");
    if (InitSection.starts_with(SegName) && InitSection.substr(7) == SecName)
      return true;
  }
  return false;
}

bool isMachOInitializerSection(StringRef QualifiedName) {
  for (auto &InitSection : MachOInitSectionNames)
    if (InitSection == QualifiedName)
      return true;
  return false;
}

bool isELFInitializerSection(StringRef SecName) {
  for (StringRef InitSection : ELFInitSectionNames) {
    StringRef Name = SecName;
    if (Name.consume_front(InitSection) && (Name.empty() || Name[0] == '.'))
      return true;
  }
  return false;
}

bool isCOFFInitializerSection(StringRef SecName) {
  return SecName.starts_with(".CRT");
}

} // namespace orc
} // namespace llvm
