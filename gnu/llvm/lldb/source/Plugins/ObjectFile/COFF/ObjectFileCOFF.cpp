//===-- ObjectFileCOFF.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ObjectFileCOFF.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Utility/LLDBLog.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/FormatAdapters.h"

using namespace lldb;
using namespace lldb_private;

using namespace llvm;
using namespace llvm::object;

static bool IsCOFFObjectFile(const DataBufferSP &data) {
  return identify_magic(toStringRef(data->GetData())) ==
         file_magic::coff_object;
}

LLDB_PLUGIN_DEFINE(ObjectFileCOFF)

char ObjectFileCOFF::ID;

ObjectFileCOFF::~ObjectFileCOFF() = default;

void ObjectFileCOFF::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                CreateMemoryInstance, GetModuleSpecifications);
}

void ObjectFileCOFF::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ObjectFile *
ObjectFileCOFF::CreateInstance(const ModuleSP &module_sp, DataBufferSP data_sp,
                               offset_t data_offset, const FileSpec *file,
                               offset_t file_offset, offset_t length) {
  Log *log = GetLog(LLDBLog::Object);

  if (!data_sp) {
    data_sp = MapFileData(*file, length, file_offset);
    if (!data_sp) {
      LLDB_LOG(log,
               "Failed to create ObjectFileCOFF instance: cannot read file {0}",
               file->GetPath());
      return nullptr;
    }
    data_offset = 0;
  }

  assert(data_sp && "must have mapped file at this point");

  if (!IsCOFFObjectFile(data_sp))
    return nullptr;

  if (data_sp->GetByteSize() < length) {
    data_sp = MapFileData(*file, length, file_offset);
    if (!data_sp) {
      LLDB_LOG(log,
               "Failed to create ObjectFileCOFF instance: cannot read file {0}",
               file->GetPath());
      return nullptr;
    }
    data_offset = 0;
  }


  MemoryBufferRef buffer{toStringRef(data_sp->GetData()),
                         file->GetFilename().GetStringRef()};

  Expected<std::unique_ptr<Binary>> binary = createBinary(buffer);
  if (!binary) {
    LLDB_LOG_ERROR(log, binary.takeError(),
                   "Failed to create binary for file ({1}): {0}",
                   file->GetPath());
    return nullptr;
  }

  LLDB_LOG(log, "ObjectFileCOFF::ObjectFileCOFF module = {1} ({2}), file = {3}",
           module_sp.get(), module_sp->GetSpecificationDescription(),
           file->GetPath());

  return new ObjectFileCOFF(unique_dyn_cast<COFFObjectFile>(std::move(*binary)),
                            module_sp, data_sp, data_offset, file, file_offset,
                            length);
}

lldb_private::ObjectFile *ObjectFileCOFF::CreateMemoryInstance(
    const ModuleSP &module_sp, WritableDataBufferSP data_sp,
    const ProcessSP &process_sp, addr_t header) {
  // FIXME: do we need to worry about construction from a memory region?
  return nullptr;
}

size_t ObjectFileCOFF::GetModuleSpecifications(
    const FileSpec &file, DataBufferSP &data_sp, offset_t data_offset,
    offset_t file_offset, offset_t length, ModuleSpecList &specs) {
  if (!IsCOFFObjectFile(data_sp))
    return 0;

  MemoryBufferRef buffer{toStringRef(data_sp->GetData()),
                         file.GetFilename().GetStringRef()};
  Expected<std::unique_ptr<Binary>> binary = createBinary(buffer);
  if (!binary) {
    Log *log = GetLog(LLDBLog::Object);
    LLDB_LOG_ERROR(log, binary.takeError(),
                   "Failed to create binary for file ({1}): {0}",
                   file.GetFilename());
    return 0;
  }

  std::unique_ptr<COFFObjectFile> object =
      unique_dyn_cast<COFFObjectFile>(std::move(*binary));
  switch (static_cast<COFF::MachineTypes>(object->getMachine())) {
  case COFF::IMAGE_FILE_MACHINE_I386:
    specs.Append(ModuleSpec(file, ArchSpec("i686-unknown-windows-msvc")));
    return 1;
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    specs.Append(ModuleSpec(file, ArchSpec("x86_64-unknown-windows-msvc")));
    return 1;
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    specs.Append(ModuleSpec(file, ArchSpec("armv7-unknown-windows-msvc")));
    return 1;
  case COFF::IMAGE_FILE_MACHINE_ARM64:
    specs.Append(ModuleSpec(file, ArchSpec("aarch64-unknown-windows-msvc")));
    return 1;
  default:
    return 0;
  }
}

void ObjectFileCOFF::Dump(Stream *stream) {
  ModuleSP module(GetModule());
  if (!module)
    return;

  std::lock_guard<std::recursive_mutex> guard(module->GetMutex());

  stream->Printf("%p: ", static_cast<void *>(this));
  stream->Indent();
  stream->PutCString("ObjectFileCOFF");
  *stream << ", file = '" << m_file
          << "', arch = " << GetArchitecture().GetArchitectureName() << '\n';

  if (SectionList *sections = GetSectionList())
    sections->Dump(stream->AsRawOstream(), stream->GetIndentLevel(), nullptr,
                   true, std::numeric_limits<uint32_t>::max());
}

uint32_t ObjectFileCOFF::GetAddressByteSize() const {
  return const_cast<ObjectFileCOFF *>(this)->GetArchitecture().GetAddressByteSize();
}

ArchSpec ObjectFileCOFF::GetArchitecture() {
  switch (static_cast<COFF::MachineTypes>(m_object->getMachine())) {
  case COFF::IMAGE_FILE_MACHINE_I386:
    return ArchSpec("i686-unknown-windows-msvc");
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return ArchSpec("x86_64-unknown-windows-msvc");
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    return ArchSpec("armv7-unknown-windows-msvc");
  case COFF::IMAGE_FILE_MACHINE_ARM64:
    return ArchSpec("aarch64-unknown-windows-msvc");
  default:
    return ArchSpec();
  }
}

void ObjectFileCOFF::CreateSections(lldb_private::SectionList &sections) {
  if (m_sections_up)
    return;

  m_sections_up = std::make_unique<SectionList>();
  ModuleSP module(GetModule());
  if (!module)
    return;

  std::lock_guard<std::recursive_mutex> guard(module->GetMutex());

  auto SectionType = [](StringRef Name,
                        const coff_section *Section) -> lldb::SectionType {
    lldb::SectionType type =
        StringSwitch<lldb::SectionType>(Name)
            // DWARF Debug Sections
            .Case(".debug_abbrev", eSectionTypeDWARFDebugAbbrev)
            .Case(".debug_info", eSectionTypeDWARFDebugInfo)
            .Case(".debug_line", eSectionTypeDWARFDebugLine)
            .Case(".debug_pubnames", eSectionTypeDWARFDebugPubNames)
            .Case(".debug_pubtypes", eSectionTypeDWARFDebugPubTypes)
            .Case(".debug_str", eSectionTypeDWARFDebugStr)
            // CodeView Debug Sections: .debug$S, .debug$T
            .StartsWith(".debug$", eSectionTypeDebug)
            .Case("clangast", eSectionTypeOther)
            .Default(eSectionTypeInvalid);
    if (type != eSectionTypeInvalid)
      return type;

    if (Section->Characteristics & COFF::IMAGE_SCN_CNT_CODE)
      return eSectionTypeCode;
    if (Section->Characteristics & COFF::IMAGE_SCN_CNT_INITIALIZED_DATA)
      return eSectionTypeData;
    if (Section->Characteristics & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA)
      return Section->SizeOfRawData ? eSectionTypeData : eSectionTypeZeroFill;
    return eSectionTypeOther;
  };
  auto Permissions = [](const object::coff_section *Section) -> uint32_t {
    uint32_t permissions = 0;
    if (Section->Characteristics & COFF::IMAGE_SCN_MEM_EXECUTE)
      permissions |= lldb::ePermissionsExecutable;
    if (Section->Characteristics & COFF::IMAGE_SCN_MEM_READ)
      permissions |= lldb::ePermissionsReadable;
    if (Section->Characteristics & COFF::IMAGE_SCN_MEM_WRITE)
      permissions |= lldb::ePermissionsWritable;
    return permissions;
  };

  for (const auto &SecRef : m_object->sections()) {
    const auto COFFSection = m_object->getCOFFSection(SecRef);

    llvm::Expected<StringRef> Name = SecRef.getName();
    StringRef SectionName = Name ? *Name : COFFSection->Name;
    if (!Name)
      consumeError(Name.takeError());

    SectionSP section =
        std::make_unique<Section>(module, this,
                                  static_cast<user_id_t>(SecRef.getIndex()),
                                  ConstString(SectionName),
                                  SectionType(SectionName, COFFSection),
                                  COFFSection->VirtualAddress,
                                  COFFSection->VirtualSize,
                                  COFFSection->PointerToRawData,
                                  COFFSection->SizeOfRawData,
                                  COFFSection->getAlignment(),
                                  0);
    section->SetPermissions(Permissions(COFFSection));

    m_sections_up->AddSection(section);
    sections.AddSection(section);
  }
}

void ObjectFileCOFF::ParseSymtab(lldb_private::Symtab &symtab) {
  Log *log = GetLog(LLDBLog::Object);

  SectionList *sections = GetSectionList();
  symtab.Reserve(symtab.GetNumSymbols() + m_object->getNumberOfSymbols());

  auto SymbolType = [](const COFFSymbolRef &Symbol) -> lldb::SymbolType {
    if (Symbol.getComplexType() == COFF::IMAGE_SYM_DTYPE_FUNCTION)
      return eSymbolTypeCode;
    if (Symbol.getBaseType() == COFF::IMAGE_SYM_TYPE_NULL &&
        Symbol.getComplexType() == COFF::IMAGE_SYM_DTYPE_NULL)
      return eSymbolTypeData;
    return eSymbolTypeInvalid;
  };

  for (const auto &SymRef : m_object->symbols()) {
    const auto COFFSymRef = m_object->getCOFFSymbol(SymRef);

    Expected<StringRef> NameOrErr = SymRef.getName();
    if (!NameOrErr) {
      LLDB_LOG_ERROR(log, NameOrErr.takeError(),
                     "ObjectFileCOFF: failed to get symbol name: {0}");
      continue;
    }

    Symbol symbol;
    symbol.GetMangled().SetValue(ConstString(*NameOrErr));

    int16_t SecIdx = static_cast<int16_t>(COFFSymRef.getSectionNumber());
    if (SecIdx == COFF::IMAGE_SYM_ABSOLUTE) {
      symbol.GetAddressRef() = Address{COFFSymRef.getValue()};
      symbol.SetType(eSymbolTypeAbsolute);
    } else if (SecIdx >= 1) {
      symbol.GetAddressRef() = Address(sections->GetSectionAtIndex(SecIdx - 1),
                                       COFFSymRef.getValue());
      symbol.SetType(SymbolType(COFFSymRef));
    }

    symtab.AddSymbol(symbol);
  }

  LLDB_LOG(log, "ObjectFileCOFF::ParseSymtab processed {0} symbols",
           m_object->getNumberOfSymbols());
}

bool ObjectFileCOFF::ParseHeader() {
  ModuleSP module(GetModule());
  if (!module)
    return false;

  std::lock_guard<std::recursive_mutex> guard(module->GetMutex());

  m_data.SetByteOrder(eByteOrderLittle);
  m_data.SetAddressByteSize(GetAddressByteSize());

  return true;
}
