//===-- ObjectFileCOFF.h -------------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_COFF_OBJECTFILECOFF_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_COFF_OBJECTFILECOFF_H

#include "lldb/Symbol/ObjectFile.h"

#include "llvm/Object/COFF.h"

/// \class ObjectFileELF
/// Generic COFF object file reader.
///
/// This class provides a generic COFF reader plugin implementing the ObjectFile
/// protocol.  Assumes that the COFF object format is a Microsoft style COFF
/// rather than the full generality afforded by it.
class ObjectFileCOFF : public lldb_private::ObjectFile {
  std::unique_ptr<llvm::object::COFFObjectFile> m_object;
  lldb_private::UUID m_uuid;

  ObjectFileCOFF(std::unique_ptr<llvm::object::COFFObjectFile> object,
                 const lldb::ModuleSP &module_sp, lldb::DataBufferSP data_sp,
                 lldb::offset_t data_offset, const lldb_private::FileSpec *file,
                 lldb::offset_t file_offset, lldb::offset_t length)
    : ObjectFile(module_sp, file, file_offset, length, data_sp, data_offset),
      m_object(std::move(object)) {}

public:
  ~ObjectFileCOFF() override;

  static void Initialize();
  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "COFF"; }
  static llvm::StringRef GetPluginDescriptionStatic() {
    return "COFF Object File Reader";
  }

  static lldb_private::ObjectFile *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP data_sp,
                 lldb::offset_t data_offset, const lldb_private::FileSpec *file,
                 lldb::offset_t file_offset, lldb::offset_t length);

  static lldb_private::ObjectFile *
  CreateMemoryInstance(const lldb::ModuleSP &module_sp,
                       lldb::WritableDataBufferSP data_sp,
                       const lldb::ProcessSP &process_sp, lldb::addr_t header);

  static size_t GetModuleSpecifications(const lldb_private::FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        lldb_private::ModuleSpecList &specs);

  // LLVM RTTI support
  static char ID;
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || ObjectFile::isA(ClassID);
  }
  static bool classof(const ObjectFile *obj) { return obj->isA(&ID); }

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  // ObjectFile protocol
  void Dump(lldb_private::Stream *stream) override;

  uint32_t GetAddressByteSize() const override;

  uint32_t GetDependentModules(lldb_private::FileSpecList &specs) override {
    return 0;
  }

  bool IsExecutable() const override {
    // COFF is an object file format only, it cannot host an executable.
    return false;
  }

  lldb_private::ArchSpec GetArchitecture() override;

  void CreateSections(lldb_private::SectionList &) override;

  void ParseSymtab(lldb_private::Symtab &) override;

  bool IsStripped() override {
    // FIXME see if there is a good way to identify a /Z7 v /Zi or /ZI build.
    return false;
  }

  lldb_private::UUID GetUUID() override { return m_uuid; }

  lldb::ByteOrder GetByteOrder() const override {
    // Microsoft always uses little endian.
    return lldb::ByteOrder::eByteOrderLittle;
  }

  bool ParseHeader() override;

  lldb_private::ObjectFile::Type CalculateType() override {
    // COFF is an object file format only, it cannot host an executable.
    return lldb_private::ObjectFile::eTypeObjectFile;
  }

  lldb_private::ObjectFile::Strata CalculateStrata() override {
    // FIXME the object file may correspond to a kernel image.
    return lldb_private::ObjectFile::eStrataUser;
  }
};

#endif
