//===- AMDGPUMetadataVerifier.h - MsgPack Types -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This is a verifier for AMDGPU HSA metadata, which can verify both
/// well-typed metadata and untyped metadata. When verifying in the non-strict
/// mode, untyped metadata is coerced into the correct type if possible.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_AMDGPUMETADATAVERIFIER_H
#define LLVM_BINARYFORMAT_AMDGPUMETADATAVERIFIER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MsgPackReader.h"

#include <cstddef>
#include <optional>

namespace llvm {

namespace msgpack {
  class DocNode;
  class MapDocNode;
}

namespace AMDGPU {
namespace HSAMD {
namespace V3 {

/// Verifier for AMDGPU HSA metadata.
///
/// Operates in two modes:
///
/// In strict mode, metadata must already be well-typed.
///
/// In non-strict mode, metadata is coerced into expected types when possible.
class MetadataVerifier {
  bool Strict;

  bool verifyScalar(msgpack::DocNode &Node, msgpack::Type SKind,
                    function_ref<bool(msgpack::DocNode &)> verifyValue = {});
  bool verifyInteger(msgpack::DocNode &Node);
  bool verifyArray(msgpack::DocNode &Node,
                   function_ref<bool(msgpack::DocNode &)> verifyNode,
                   std::optional<size_t> Size = std::nullopt);
  bool verifyEntry(msgpack::MapDocNode &MapNode, StringRef Key, bool Required,
                   function_ref<bool(msgpack::DocNode &)> verifyNode);
  bool
  verifyScalarEntry(msgpack::MapDocNode &MapNode, StringRef Key, bool Required,
                    msgpack::Type SKind,
                    function_ref<bool(msgpack::DocNode &)> verifyValue = {});
  bool verifyIntegerEntry(msgpack::MapDocNode &MapNode, StringRef Key,
                          bool Required);
  bool verifyKernelArgs(msgpack::DocNode &Node);
  bool verifyKernel(msgpack::DocNode &Node);

public:
  /// Construct a MetadataVerifier, specifying whether it will operate in \p
  /// Strict mode.
  MetadataVerifier(bool Strict) : Strict(Strict) {}

  /// Verify given HSA metadata.
  ///
  /// \returns True when successful, false when metadata is invalid.
  bool verify(msgpack::DocNode &HSAMetadataRoot);
};

} // end namespace V3
} // end namespace HSAMD
} // end namespace AMDGPU
} // end namespace llvm

#endif // LLVM_BINARYFORMAT_AMDGPUMETADATAVERIFIER_H
