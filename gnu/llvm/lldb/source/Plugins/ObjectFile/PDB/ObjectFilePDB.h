//===-- ObjectFilePDB.h --------------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_PDB_OBJECTFILEPDB_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_PDB_OBJECTFILEPDB_H

#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/ArchSpec.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace lldb_private {

class ObjectFilePDB : public ObjectFile {
public:
  // Static Functions
  static void Initialize();
  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "pdb"; }
  static const char *GetPluginDescriptionStatic() {
    return "PDB object file reader.";
  }

  static std::unique_ptr<llvm::pdb::PDBFile>
  loadPDBFile(std::string PdbPath, llvm::BumpPtrAllocator &Allocator);

  static ObjectFile *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP data_sp,
                 lldb::offset_t data_offset, const FileSpec *file,
                 lldb::offset_t file_offset, lldb::offset_t length);

  static ObjectFile *CreateMemoryInstance(const lldb::ModuleSP &module_sp,
                                          lldb::WritableDataBufferSP data_sp,
                                          const lldb::ProcessSP &process_sp,
                                          lldb::addr_t header_addr);

  static size_t GetModuleSpecifications(const FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        ModuleSpecList &specs);

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  // LLVM RTTI support
  static char ID;
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || ObjectFile::isA(ClassID);
  }
  static bool classof(const ObjectFile *obj) { return obj->isA(&ID); }

  // ObjectFile Protocol.
  uint32_t GetAddressByteSize() const override { return 8; }

  lldb::ByteOrder GetByteOrder() const override {
    return lldb::eByteOrderLittle;
  }

  bool ParseHeader() override { return true; }

  bool IsExecutable() const override { return false; }

  void ParseSymtab(lldb_private::Symtab &symtab) override {}

  bool IsStripped() override { return false; }

  // No section in PDB file.
  void CreateSections(SectionList &unified_section_list) override {}

  void Dump(Stream *s) override {}

  ArchSpec GetArchitecture() override;

  UUID GetUUID() override { return m_uuid; }

  uint32_t GetDependentModules(FileSpecList &files) override { return 0; }

  Type CalculateType() override { return eTypeDebugInfo; }

  Strata CalculateStrata() override { return eStrataUser; }

  llvm::pdb::PDBFile &GetPDBFile() { return *m_file_up; }

  ObjectFilePDB(const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
                lldb::offset_t data_offset, const FileSpec *file,
                lldb::offset_t offset, lldb::offset_t length);

private:
  UUID m_uuid;
  llvm::BumpPtrAllocator m_allocator;
  std::unique_ptr<llvm::pdb::PDBFile> m_file_up;

  bool initPDBFile();
};

} // namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_OBJECTFILE_PDB_OBJECTFILEPDB_H
