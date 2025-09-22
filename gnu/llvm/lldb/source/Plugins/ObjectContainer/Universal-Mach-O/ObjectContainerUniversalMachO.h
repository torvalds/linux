//===-- ObjectContainerUniversalMachO.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTCONTAINER_UNIVERSAL_MACH_O_OBJECTCONTAINERUNIVERSALMACHO_H
#define LLDB_SOURCE_PLUGINS_OBJECTCONTAINER_UNIVERSAL_MACH_O_OBJECTCONTAINERUNIVERSALMACHO_H

#include "lldb/Host/SafeMachO.h"
#include "lldb/Symbol/ObjectContainer.h"
#include "lldb/Utility/FileSpec.h"

class ObjectContainerUniversalMachO : public lldb_private::ObjectContainer {
public:
  ObjectContainerUniversalMachO(const lldb::ModuleSP &module_sp,
                                lldb::DataBufferSP &data_sp,
                                lldb::offset_t data_offset,
                                const lldb_private::FileSpec *file,
                                lldb::offset_t offset, lldb::offset_t length);

  ~ObjectContainerUniversalMachO() override;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "mach-o"; }

  static llvm::StringRef GetPluginDescriptionStatic() {
    return "Universal mach-o object container reader.";
  }

  static lldb_private::ObjectContainer *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
                 lldb::offset_t data_offset, const lldb_private::FileSpec *file,
                 lldb::offset_t offset, lldb::offset_t length);

  static size_t GetModuleSpecifications(const lldb_private::FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        lldb_private::ModuleSpecList &specs);

  static bool MagicBytesMatch(const lldb_private::DataExtractor &data);

  // Member Functions
  bool ParseHeader() override;

  size_t GetNumArchitectures() const override;

  bool GetArchitectureAtIndex(uint32_t cpu_idx,
                              lldb_private::ArchSpec &arch) const override;

  lldb::ObjectFileSP GetObjectFile(const lldb_private::FileSpec *file) override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  llvm::MachO::fat_header m_header;

  struct FatArch {
    FatArch(llvm::MachO::fat_arch arch) : m_arch(arch), m_is_fat64(false) {}
    FatArch(llvm::MachO::fat_arch_64 arch) : m_arch(arch), m_is_fat64(true) {}

    uint32_t GetCPUType() const {
      return m_is_fat64 ? m_arch.fat_arch_64.cputype : m_arch.fat_arch.cputype;
    }

    uint32_t GetCPUSubType() const {
      return m_is_fat64 ? m_arch.fat_arch_64.cpusubtype
                        : m_arch.fat_arch.cpusubtype;
    }

    uint64_t GetOffset() const {
      return m_is_fat64 ? m_arch.fat_arch_64.offset : m_arch.fat_arch.offset;
    }

    uint64_t GetSize() const {
      return m_is_fat64 ? m_arch.fat_arch_64.size : m_arch.fat_arch.size;
    }

    uint32_t GetAlign() const {
      return m_is_fat64 ? m_arch.fat_arch_64.align : m_arch.fat_arch.align;
    }

  private:
    const union Arch {
      Arch(llvm::MachO::fat_arch arch) : fat_arch(arch) {}
      Arch(llvm::MachO::fat_arch_64 arch) : fat_arch_64(arch) {}
      llvm::MachO::fat_arch fat_arch;
      llvm::MachO::fat_arch_64 fat_arch_64;
    } m_arch;
    const bool m_is_fat64;
  };
  std::vector<FatArch> m_fat_archs;

  static bool ParseHeader(lldb_private::DataExtractor &data,
                          llvm::MachO::fat_header &header,
                          std::vector<FatArch> &fat_archs);
};

#endif // LLDB_SOURCE_PLUGINS_OBJECTCONTAINER_UNIVERSAL_MACH_O_OBJECTCONTAINERUNIVERSALMACHO_H
