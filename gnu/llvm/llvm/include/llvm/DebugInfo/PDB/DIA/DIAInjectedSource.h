//===- DIAInjectedSource.h - DIA impl for IPDBInjectedSource ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAINJECTEDSOURCE_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAINJECTEDSOURCE_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBInjectedSource.h"

namespace llvm {
namespace pdb {
class DIASession;

class DIAInjectedSource : public IPDBInjectedSource {
public:
  explicit DIAInjectedSource(CComPtr<IDiaInjectedSource> DiaSourceFile);

  uint32_t getCrc32() const override;
  uint64_t getCodeByteSize() const override;
  std::string getFileName() const override;
  std::string getObjectFileName() const override;
  std::string getVirtualFileName() const override;
  uint32_t getCompression() const override;
  std::string getCode() const override;

private:
  CComPtr<IDiaInjectedSource> SourceFile;
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_DIA_DIAINJECTEDSOURCE_H
