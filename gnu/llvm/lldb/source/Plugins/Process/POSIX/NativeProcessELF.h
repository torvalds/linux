//===-- NativeProcessELF.h ------------------------------------ -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NativeProcessELF_H_
#define liblldb_NativeProcessELF_H_

#include "Plugins/Process/Utility/AuxVector.h"
#include "lldb/Host/common/NativeProcessProtocol.h"
#include "llvm/BinaryFormat/ELF.h"
#include <optional>

namespace lldb_private {

/// \class NativeProcessELF
/// Abstract class that extends \a NativeProcessProtocol with ELF specific
/// logic. Meant to be subclassed by ELF based NativeProcess* implementations.
class NativeProcessELF : public NativeProcessProtocol {
  using NativeProcessProtocol::NativeProcessProtocol;

public:
  std::optional<uint64_t> GetAuxValue(enum AuxVector::EntryType type);

protected:
  template <typename T> struct ELFLinkMap {
    T l_addr;
    T l_name;
    T l_ld;
    T l_next;
    T l_prev;
  };

  lldb::addr_t GetSharedLibraryInfoAddress() override;

  template <typename ELF_EHDR, typename ELF_PHDR, typename ELF_DYN>
  lldb::addr_t GetELFImageInfoAddress();

  llvm::Expected<std::vector<SVR4LibraryInfo>>
  GetLoadedSVR4Libraries() override;

  template <typename T>
  llvm::Expected<SVR4LibraryInfo>
  ReadSVR4LibraryInfo(lldb::addr_t link_map_addr);

  void NotifyDidExec() override;

  std::unique_ptr<AuxVector> m_aux_vector;
  std::optional<lldb::addr_t> m_shared_library_info_addr;
};

// Explicitly declare the two 32/64 bit templates that NativeProcessELF.cpp will
// define. This allows us to keep the template definition here and usable
// elsewhere.
extern template lldb::addr_t NativeProcessELF::GetELFImageInfoAddress<
    llvm::ELF::Elf32_Ehdr, llvm::ELF::Elf32_Phdr, llvm::ELF::Elf32_Dyn>();
extern template lldb::addr_t NativeProcessELF::GetELFImageInfoAddress<
    llvm::ELF::Elf64_Ehdr, llvm::ELF::Elf64_Phdr, llvm::ELF::Elf64_Dyn>();

} // namespace lldb_private

#endif
