//===- OffloadBundler.h - File Bundling and Unbundling ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines an offload bundling API that bundles different files
/// that relate with the same source code but different targets into a single
/// one. Also the implements the opposite functionality, i.e. unbundle files
/// previous created by this API.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_OFFLOADBUNDLER_H
#define LLVM_CLANG_DRIVER_OFFLOADBUNDLER_H

#include "llvm/Support/Compression.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"
#include <llvm/Support/MemoryBuffer.h>
#include <string>
#include <vector>

namespace clang {

class OffloadBundlerConfig {
public:
  OffloadBundlerConfig();

  bool AllowNoHost = false;
  bool AllowMissingBundles = false;
  bool CheckInputArchive = false;
  bool PrintExternalCommands = false;
  bool HipOpenmpCompatible = false;
  bool Compress = false;
  bool Verbose = false;
  llvm::compression::Format CompressionFormat;
  int CompressionLevel;

  unsigned BundleAlignment = 1;
  unsigned HostInputIndex = ~0u;

  std::string FilesType;
  std::string ObjcopyPath;

  // TODO: Convert these to llvm::SmallVector
  std::vector<std::string> TargetNames;
  std::vector<std::string> InputFileNames;
  std::vector<std::string> OutputFileNames;
};

class OffloadBundler {
public:
  const OffloadBundlerConfig &BundlerConfig;

  // TODO: Add error checking from ClangOffloadBundler.cpp
  OffloadBundler(const OffloadBundlerConfig &BC) : BundlerConfig(BC) {}

  // List bundle IDs. Return true if an error was found.
  static llvm::Error
  ListBundleIDsInFile(llvm::StringRef InputFileName,
                      const OffloadBundlerConfig &BundlerConfig);

  llvm::Error BundleFiles();
  llvm::Error UnbundleFiles();
  llvm::Error UnbundleArchive();
};

/// Obtain the offload kind, real machine triple, and an optional TargetID
/// out of the target information specified by the user.
/// Bundle Entry ID (or, Offload Target String) has following components:
///  * Offload Kind - Host, OpenMP, or HIP
///  * Triple - Standard LLVM Triple
///  * TargetID (Optional) - target ID, like gfx906:xnack+ or sm_30
struct OffloadTargetInfo {
  llvm::StringRef OffloadKind;
  llvm::Triple Triple;
  llvm::StringRef TargetID;

  const OffloadBundlerConfig &BundlerConfig;

  OffloadTargetInfo(const llvm::StringRef Target,
                    const OffloadBundlerConfig &BC);
  bool hasHostKind() const;
  bool isOffloadKindValid() const;
  bool isOffloadKindCompatible(const llvm::StringRef TargetOffloadKind) const;
  bool isTripleValid() const;
  bool operator==(const OffloadTargetInfo &Target) const;
  std::string str() const;
};

// CompressedOffloadBundle represents the format for the compressed offload
// bundles.
//
// The format is as follows:
// - Magic Number (4 bytes) - A constant "CCOB".
// - Version (2 bytes)
// - Compression Method (2 bytes) - Uses the values from
// llvm::compression::Format.
// - Total file size (4 bytes). Available in version 2 and above.
// - Uncompressed Size (4 bytes).
// - Truncated MD5 Hash (8 bytes).
// - Compressed Data (variable length).

class CompressedOffloadBundle {
private:
  static inline const size_t MagicSize = 4;
  static inline const size_t VersionFieldSize = sizeof(uint16_t);
  static inline const size_t MethodFieldSize = sizeof(uint16_t);
  static inline const size_t FileSizeFieldSize = sizeof(uint32_t);
  static inline const size_t UncompressedSizeFieldSize = sizeof(uint32_t);
  static inline const size_t HashFieldSize = sizeof(uint64_t);
  static inline const size_t V1HeaderSize =
      MagicSize + VersionFieldSize + MethodFieldSize +
      UncompressedSizeFieldSize + HashFieldSize;
  static inline const size_t V2HeaderSize =
      MagicSize + VersionFieldSize + FileSizeFieldSize + MethodFieldSize +
      UncompressedSizeFieldSize + HashFieldSize;
  static inline const llvm::StringRef MagicNumber = "CCOB";
  static inline const uint16_t Version = 2;

public:
  static llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
  compress(llvm::compression::Params P, const llvm::MemoryBuffer &Input,
           bool Verbose = false);
  static llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
  decompress(const llvm::MemoryBuffer &Input, bool Verbose = false);
};

} // namespace clang

#endif // LLVM_CLANG_DRIVER_OFFLOADBUNDLER_H
