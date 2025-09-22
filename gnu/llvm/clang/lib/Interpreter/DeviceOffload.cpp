//===---------- DeviceOffload.cpp - Device Offloading------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements offloading to CUDA devices.
//
//===----------------------------------------------------------------------===//

#include "DeviceOffload.h"

#include "clang/Basic/TargetOptions.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Frontend/CompilerInstance.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"

namespace clang {

IncrementalCUDADeviceParser::IncrementalCUDADeviceParser(
    Interpreter &Interp, std::unique_ptr<CompilerInstance> Instance,
    IncrementalParser &HostParser, llvm::LLVMContext &LLVMCtx,
    llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> FS,
    llvm::Error &Err)
    : IncrementalParser(Interp, std::move(Instance), LLVMCtx, Err),
      HostParser(HostParser), VFS(FS) {
  if (Err)
    return;
  StringRef Arch = CI->getTargetOpts().CPU;
  if (!Arch.starts_with("sm_") || Arch.substr(3).getAsInteger(10, SMVersion)) {
    Err = llvm::joinErrors(std::move(Err), llvm::make_error<llvm::StringError>(
                                               "Invalid CUDA architecture",
                                               llvm::inconvertibleErrorCode()));
    return;
  }
}

llvm::Expected<PartialTranslationUnit &>
IncrementalCUDADeviceParser::Parse(llvm::StringRef Input) {
  auto PTU = IncrementalParser::Parse(Input);
  if (!PTU)
    return PTU.takeError();

  auto PTX = GeneratePTX();
  if (!PTX)
    return PTX.takeError();

  auto Err = GenerateFatbinary();
  if (Err)
    return std::move(Err);

  std::string FatbinFileName =
      "/incr_module_" + std::to_string(PTUs.size()) + ".fatbin";
  VFS->addFile(FatbinFileName, 0,
               llvm::MemoryBuffer::getMemBuffer(
                   llvm::StringRef(FatbinContent.data(), FatbinContent.size()),
                   "", false));

  HostParser.getCI()->getCodeGenOpts().CudaGpuBinaryFileName = FatbinFileName;

  FatbinContent.clear();

  return PTU;
}

llvm::Expected<llvm::StringRef> IncrementalCUDADeviceParser::GeneratePTX() {
  auto &PTU = PTUs.back();
  std::string Error;

  const llvm::Target *Target = llvm::TargetRegistry::lookupTarget(
      PTU.TheModule->getTargetTriple(), Error);
  if (!Target)
    return llvm::make_error<llvm::StringError>(std::move(Error),
                                               std::error_code());
  llvm::TargetOptions TO = llvm::TargetOptions();
  llvm::TargetMachine *TargetMachine = Target->createTargetMachine(
      PTU.TheModule->getTargetTriple(), getCI()->getTargetOpts().CPU, "", TO,
      llvm::Reloc::Model::PIC_);
  PTU.TheModule->setDataLayout(TargetMachine->createDataLayout());

  PTXCode.clear();
  llvm::raw_svector_ostream dest(PTXCode);

  llvm::legacy::PassManager PM;
  if (TargetMachine->addPassesToEmitFile(PM, dest, nullptr,
                                         llvm::CodeGenFileType::AssemblyFile)) {
    return llvm::make_error<llvm::StringError>(
        "NVPTX backend cannot produce PTX code.",
        llvm::inconvertibleErrorCode());
  }

  if (!PM.run(*PTU.TheModule))
    return llvm::make_error<llvm::StringError>("Failed to emit PTX code.",
                                               llvm::inconvertibleErrorCode());

  PTXCode += '\0';
  while (PTXCode.size() % 8)
    PTXCode += '\0';
  return PTXCode.str();
}

llvm::Error IncrementalCUDADeviceParser::GenerateFatbinary() {
  enum FatBinFlags {
    AddressSize64 = 0x01,
    HasDebugInfo = 0x02,
    ProducerCuda = 0x04,
    HostLinux = 0x10,
    HostMac = 0x20,
    HostWindows = 0x40
  };

  struct FatBinInnerHeader {
    uint16_t Kind;             // 0x00
    uint16_t unknown02;        // 0x02
    uint32_t HeaderSize;       // 0x04
    uint32_t DataSize;         // 0x08
    uint32_t unknown0c;        // 0x0c
    uint32_t CompressedSize;   // 0x10
    uint32_t SubHeaderSize;    // 0x14
    uint16_t VersionMinor;     // 0x18
    uint16_t VersionMajor;     // 0x1a
    uint32_t CudaArch;         // 0x1c
    uint32_t unknown20;        // 0x20
    uint32_t unknown24;        // 0x24
    uint32_t Flags;            // 0x28
    uint32_t unknown2c;        // 0x2c
    uint32_t unknown30;        // 0x30
    uint32_t unknown34;        // 0x34
    uint32_t UncompressedSize; // 0x38
    uint32_t unknown3c;        // 0x3c
    uint32_t unknown40;        // 0x40
    uint32_t unknown44;        // 0x44
    FatBinInnerHeader(uint32_t DataSize, uint32_t CudaArch, uint32_t Flags)
        : Kind(1 /*PTX*/), unknown02(0x0101), HeaderSize(sizeof(*this)),
          DataSize(DataSize), unknown0c(0), CompressedSize(0),
          SubHeaderSize(HeaderSize - 8), VersionMinor(2), VersionMajor(4),
          CudaArch(CudaArch), unknown20(0), unknown24(0), Flags(Flags),
          unknown2c(0), unknown30(0), unknown34(0), UncompressedSize(0),
          unknown3c(0), unknown40(0), unknown44(0) {}
  };

  struct FatBinHeader {
    uint32_t Magic;      // 0x00
    uint16_t Version;    // 0x04
    uint16_t HeaderSize; // 0x06
    uint32_t DataSize;   // 0x08
    uint32_t unknown0c;  // 0x0c
  public:
    FatBinHeader(uint32_t DataSize)
        : Magic(0xba55ed50), Version(1), HeaderSize(sizeof(*this)),
          DataSize(DataSize), unknown0c(0) {}
  };

  FatBinHeader OuterHeader(sizeof(FatBinInnerHeader) + PTXCode.size());
  FatbinContent.append((char *)&OuterHeader,
                       ((char *)&OuterHeader) + OuterHeader.HeaderSize);

  FatBinInnerHeader InnerHeader(PTXCode.size(), SMVersion,
                                FatBinFlags::AddressSize64 |
                                    FatBinFlags::HostLinux);
  FatbinContent.append((char *)&InnerHeader,
                       ((char *)&InnerHeader) + InnerHeader.HeaderSize);

  FatbinContent.append(PTXCode.begin(), PTXCode.end());

  return llvm::Error::success();
}

IncrementalCUDADeviceParser::~IncrementalCUDADeviceParser() {}

} // namespace clang
