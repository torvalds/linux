//===-- ObjectFilePDB.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ObjectFilePDB.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/Support/BinaryByteStream.h"

using namespace lldb;
using namespace lldb_private;
using namespace llvm::pdb;
using namespace llvm::codeview;

LLDB_PLUGIN_DEFINE(ObjectFilePDB)

static UUID GetPDBUUID(InfoStream &IS, DbiStream &DS) {
  UUID::CvRecordPdb70 debug_info;
  memcpy(&debug_info.Uuid, IS.getGuid().Guid, sizeof(debug_info.Uuid));
  debug_info.Age = DS.getAge();
  return UUID(debug_info);
}

char ObjectFilePDB::ID;

void ObjectFilePDB::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                CreateMemoryInstance, GetModuleSpecifications);
}

void ObjectFilePDB::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ArchSpec ObjectFilePDB::GetArchitecture() {
  auto dbi_stream = m_file_up->getPDBDbiStream();
  if (!dbi_stream) {
    llvm::consumeError(dbi_stream.takeError());
    return ArchSpec();
  }

  PDB_Machine machine = dbi_stream->getMachineType();
  switch (machine) {
  default:
    break;
  case PDB_Machine::Amd64:
  case PDB_Machine::x86:
  case PDB_Machine::PowerPC:
  case PDB_Machine::PowerPCFP:
  case PDB_Machine::Arm:
  case PDB_Machine::ArmNT:
  case PDB_Machine::Thumb:
  case PDB_Machine::Arm64:
    ArchSpec arch;
    arch.SetArchitecture(eArchTypeCOFF, static_cast<int>(machine),
                         LLDB_INVALID_CPUTYPE);
    return arch;
  }
  return ArchSpec();
}

bool ObjectFilePDB::initPDBFile() {
  m_file_up = loadPDBFile(m_file.GetPath(), m_allocator);
  if (!m_file_up)
    return false;
  auto info_stream = m_file_up->getPDBInfoStream();
  if (!info_stream) {
    llvm::consumeError(info_stream.takeError());
    return false;
  }
  auto dbi_stream = m_file_up->getPDBDbiStream();
  if (!dbi_stream) {
    llvm::consumeError(dbi_stream.takeError());
    return false;
  }
  m_uuid = GetPDBUUID(*info_stream, *dbi_stream);
  return true;
}

ObjectFile *
ObjectFilePDB::CreateInstance(const ModuleSP &module_sp, DataBufferSP data_sp,
                              offset_t data_offset, const FileSpec *file,
                              offset_t file_offset, offset_t length) {
  auto objfile_up = std::make_unique<ObjectFilePDB>(
      module_sp, data_sp, data_offset, file, file_offset, length);
  if (!objfile_up->initPDBFile())
    return nullptr;
  return objfile_up.release();
}

ObjectFile *ObjectFilePDB::CreateMemoryInstance(const ModuleSP &module_sp,
                                                WritableDataBufferSP data_sp,
                                                const ProcessSP &process_sp,
                                                addr_t header_addr) {
  return nullptr;
}

size_t ObjectFilePDB::GetModuleSpecifications(
    const FileSpec &file, DataBufferSP &data_sp, offset_t data_offset,
    offset_t file_offset, offset_t length, ModuleSpecList &specs) {
  const size_t initial_count = specs.GetSize();
  ModuleSpec module_spec(file);
  llvm::BumpPtrAllocator allocator;
  std::unique_ptr<PDBFile> pdb_file = loadPDBFile(file.GetPath(), allocator);
  if (!pdb_file)
    return initial_count;

  auto info_stream = pdb_file->getPDBInfoStream();
  if (!info_stream) {
    llvm::consumeError(info_stream.takeError());
    return initial_count;
  }
  auto dbi_stream = pdb_file->getPDBDbiStream();
  if (!dbi_stream) {
    llvm::consumeError(dbi_stream.takeError());
    return initial_count;
  }

  lldb_private::UUID &uuid = module_spec.GetUUID();
  uuid = GetPDBUUID(*info_stream, *dbi_stream);

  ArchSpec &module_arch = module_spec.GetArchitecture();
  switch (dbi_stream->getMachineType()) {
  case PDB_Machine::Amd64:
    module_arch.SetTriple("x86_64-pc-windows");
    specs.Append(module_spec);
    break;
  case PDB_Machine::x86:
    module_arch.SetTriple("i386-pc-windows");
    specs.Append(module_spec);
    break;
  case PDB_Machine::ArmNT:
    module_arch.SetTriple("armv7-pc-windows");
    specs.Append(module_spec);
    break;
  case PDB_Machine::Arm64:
    module_arch.SetTriple("aarch64-pc-windows");
    specs.Append(module_spec);
    break;
  default:
    break;
  }

  return specs.GetSize() - initial_count;
}

ObjectFilePDB::ObjectFilePDB(const ModuleSP &module_sp, DataBufferSP &data_sp,
                             offset_t data_offset, const FileSpec *file,
                             offset_t offset, offset_t length)
    : ObjectFile(module_sp, file, offset, length, data_sp, data_offset) {}

std::unique_ptr<PDBFile>
ObjectFilePDB::loadPDBFile(std::string PdbPath,
                           llvm::BumpPtrAllocator &Allocator) {
  llvm::file_magic magic;
  auto ec = llvm::identify_magic(PdbPath, magic);
  if (ec || magic != llvm::file_magic::pdb)
    return nullptr;
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> ErrorOrBuffer =
      llvm::MemoryBuffer::getFile(PdbPath, /*IsText=*/false,
                                  /*RequiresNullTerminator=*/false);
  if (!ErrorOrBuffer)
    return nullptr;
  std::unique_ptr<llvm::MemoryBuffer> Buffer = std::move(*ErrorOrBuffer);

  llvm::StringRef Path = Buffer->getBufferIdentifier();
  auto Stream = std::make_unique<llvm::MemoryBufferByteStream>(
      std::move(Buffer), llvm::endianness::little);

  auto File = std::make_unique<PDBFile>(Path, std::move(Stream), Allocator);
  if (auto EC = File->parseFileHeaders()) {
    llvm::consumeError(std::move(EC));
    return nullptr;
  }
  if (auto EC = File->parseStreamData()) {
    llvm::consumeError(std::move(EC));
    return nullptr;
  }

  return File;
}
