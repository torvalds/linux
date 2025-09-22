//===- Binary.h - A generic binary file -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Binary class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_BINARY_H
#define LLVM_OBJECT_BINARY_H

#include "llvm-c/Types.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>
#include <utility>

namespace llvm {

class LLVMContext;
class StringRef;

namespace object {

class Binary {
private:
  unsigned int TypeID;

protected:
  MemoryBufferRef Data;

  Binary(unsigned int Type, MemoryBufferRef Source);

  enum {
    ID_Archive,
    ID_MachOUniversalBinary,
    ID_COFFImportFile,
    ID_IR,            // LLVM IR
    ID_TapiUniversal, // Text-based Dynamic Library Stub file.
    ID_TapiFile,      // Text-based Dynamic Library Stub file.

    ID_Minidump,

    ID_WinRes, // Windows resource (.res) file.

    ID_Offload, // Offloading binary file.

    // Object and children.
    ID_StartObjects,
    ID_COFF,

    ID_XCOFF32, // AIX XCOFF 32-bit
    ID_XCOFF64, // AIX XCOFF 64-bit

    ID_ELF32L, // ELF 32-bit, little endian
    ID_ELF32B, // ELF 32-bit, big endian
    ID_ELF64L, // ELF 64-bit, little endian
    ID_ELF64B, // ELF 64-bit, big endian

    ID_MachO32L, // MachO 32-bit, little endian
    ID_MachO32B, // MachO 32-bit, big endian
    ID_MachO64L, // MachO 64-bit, little endian
    ID_MachO64B, // MachO 64-bit, big endian

    ID_GOFF,
    ID_Wasm,

    ID_EndObjects
  };

  static inline unsigned int getELFType(bool isLE, bool is64Bits) {
    if (isLE)
      return is64Bits ? ID_ELF64L : ID_ELF32L;
    else
      return is64Bits ? ID_ELF64B : ID_ELF32B;
  }

  static unsigned int getMachOType(bool isLE, bool is64Bits) {
    if (isLE)
      return is64Bits ? ID_MachO64L : ID_MachO32L;
    else
      return is64Bits ? ID_MachO64B : ID_MachO32B;
  }

public:
  Binary() = delete;
  Binary(const Binary &other) = delete;
  virtual ~Binary();

  virtual Error initContent() { return Error::success(); };

  StringRef getData() const;
  StringRef getFileName() const;
  MemoryBufferRef getMemoryBufferRef() const;

  // Cast methods.
  unsigned int getType() const { return TypeID; }

  // Convenience methods
  bool isObject() const {
    return TypeID > ID_StartObjects && TypeID < ID_EndObjects;
  }

  bool isSymbolic() const {
    return isIR() || isObject() || isCOFFImportFile() || isTapiFile();
  }

  bool isArchive() const { return TypeID == ID_Archive; }

  bool isMachOUniversalBinary() const {
    return TypeID == ID_MachOUniversalBinary;
  }

  bool isTapiUniversal() const { return TypeID == ID_TapiUniversal; }

  bool isELF() const {
    return TypeID >= ID_ELF32L && TypeID <= ID_ELF64B;
  }

  bool isMachO() const {
    return TypeID >= ID_MachO32L && TypeID <= ID_MachO64B;
  }

  bool isCOFF() const {
    return TypeID == ID_COFF;
  }

  bool isXCOFF() const { return TypeID == ID_XCOFF32 || TypeID == ID_XCOFF64; }

  bool isWasm() const { return TypeID == ID_Wasm; }

  bool isOffloadFile() const { return TypeID == ID_Offload; }

  bool isCOFFImportFile() const {
    return TypeID == ID_COFFImportFile;
  }

  bool isIR() const {
    return TypeID == ID_IR;
  }

  bool isGOFF() const { return TypeID == ID_GOFF; }

  bool isMinidump() const { return TypeID == ID_Minidump; }

  bool isTapiFile() const { return TypeID == ID_TapiFile; }

  bool isLittleEndian() const {
    return !(TypeID == ID_ELF32B || TypeID == ID_ELF64B ||
             TypeID == ID_MachO32B || TypeID == ID_MachO64B ||
             TypeID == ID_XCOFF32 || TypeID == ID_XCOFF64);
  }

  bool isWinRes() const { return TypeID == ID_WinRes; }

  Triple::ObjectFormatType getTripleObjectFormat() const {
    if (isCOFF())
      return Triple::COFF;
    if (isMachO())
      return Triple::MachO;
    if (isELF())
      return Triple::ELF;
    if (isGOFF())
      return Triple::GOFF;
    return Triple::UnknownObjectFormat;
  }

  static Error checkOffset(MemoryBufferRef M, uintptr_t Addr,
                           const uint64_t Size) {
    if (Addr + Size < Addr || Addr + Size < Size ||
        Addr + Size > reinterpret_cast<uintptr_t>(M.getBufferEnd()) ||
        Addr < reinterpret_cast<uintptr_t>(M.getBufferStart())) {
      return errorCodeToError(object_error::unexpected_eof);
    }
    return Error::success();
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_ISA_CONVERSION_FUNCTIONS(Binary, LLVMBinaryRef)

/// Create a Binary from Source, autodetecting the file type.
///
/// @param Source The data to create the Binary from.
Expected<std::unique_ptr<Binary>> createBinary(MemoryBufferRef Source,
                                               LLVMContext *Context = nullptr,
                                               bool InitContent = true);

template <typename T> class OwningBinary {
  std::unique_ptr<T> Bin;
  std::unique_ptr<MemoryBuffer> Buf;

public:
  OwningBinary();
  OwningBinary(std::unique_ptr<T> Bin, std::unique_ptr<MemoryBuffer> Buf);
  OwningBinary(OwningBinary<T>&& Other);
  OwningBinary<T> &operator=(OwningBinary<T> &&Other);

  std::pair<std::unique_ptr<T>, std::unique_ptr<MemoryBuffer>> takeBinary();

  T* getBinary();
  const T* getBinary() const;
};

template <typename T>
OwningBinary<T>::OwningBinary(std::unique_ptr<T> Bin,
                              std::unique_ptr<MemoryBuffer> Buf)
    : Bin(std::move(Bin)), Buf(std::move(Buf)) {}

template <typename T> OwningBinary<T>::OwningBinary() = default;

template <typename T>
OwningBinary<T>::OwningBinary(OwningBinary &&Other)
    : Bin(std::move(Other.Bin)), Buf(std::move(Other.Buf)) {}

template <typename T>
OwningBinary<T> &OwningBinary<T>::operator=(OwningBinary &&Other) {
  Bin = std::move(Other.Bin);
  Buf = std::move(Other.Buf);
  return *this;
}

template <typename T>
std::pair<std::unique_ptr<T>, std::unique_ptr<MemoryBuffer>>
OwningBinary<T>::takeBinary() {
  return std::make_pair(std::move(Bin), std::move(Buf));
}

template <typename T> T* OwningBinary<T>::getBinary() {
  return Bin.get();
}

template <typename T> const T* OwningBinary<T>::getBinary() const {
  return Bin.get();
}

Expected<OwningBinary<Binary>> createBinary(StringRef Path,
                                            LLVMContext *Context = nullptr,
                                            bool InitContent = true);

} // end namespace object

} // end namespace llvm

#endif // LLVM_OBJECT_BINARY_H
