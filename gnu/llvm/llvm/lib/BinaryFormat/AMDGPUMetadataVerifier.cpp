//===- AMDGPUMetadataVerifier.cpp - MsgPack Types ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Implements a verifier for AMDGPU HSA metadata.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/AMDGPUMetadataVerifier.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/MsgPackDocument.h"

#include <utility>

namespace llvm {
namespace AMDGPU {
namespace HSAMD {
namespace V3 {

bool MetadataVerifier::verifyScalar(
    msgpack::DocNode &Node, msgpack::Type SKind,
    function_ref<bool(msgpack::DocNode &)> verifyValue) {
  if (!Node.isScalar())
    return false;
  if (Node.getKind() != SKind) {
    if (Strict)
      return false;
    // If we are not strict, we interpret string values as "implicitly typed"
    // and attempt to coerce them to the expected type here.
    if (Node.getKind() != msgpack::Type::String)
      return false;
    StringRef StringValue = Node.getString();
    Node.fromString(StringValue);
    if (Node.getKind() != SKind)
      return false;
  }
  if (verifyValue)
    return verifyValue(Node);
  return true;
}

bool MetadataVerifier::verifyInteger(msgpack::DocNode &Node) {
  if (!verifyScalar(Node, msgpack::Type::UInt))
    if (!verifyScalar(Node, msgpack::Type::Int))
      return false;
  return true;
}

bool MetadataVerifier::verifyArray(
    msgpack::DocNode &Node, function_ref<bool(msgpack::DocNode &)> verifyNode,
    std::optional<size_t> Size) {
  if (!Node.isArray())
    return false;
  auto &Array = Node.getArray();
  if (Size && Array.size() != *Size)
    return false;
  return llvm::all_of(Array, verifyNode);
}

bool MetadataVerifier::verifyEntry(
    msgpack::MapDocNode &MapNode, StringRef Key, bool Required,
    function_ref<bool(msgpack::DocNode &)> verifyNode) {
  auto Entry = MapNode.find(Key);
  if (Entry == MapNode.end())
    return !Required;
  return verifyNode(Entry->second);
}

bool MetadataVerifier::verifyScalarEntry(
    msgpack::MapDocNode &MapNode, StringRef Key, bool Required,
    msgpack::Type SKind,
    function_ref<bool(msgpack::DocNode &)> verifyValue) {
  return verifyEntry(MapNode, Key, Required, [=](msgpack::DocNode &Node) {
    return verifyScalar(Node, SKind, verifyValue);
  });
}

bool MetadataVerifier::verifyIntegerEntry(msgpack::MapDocNode &MapNode,
                                          StringRef Key, bool Required) {
  return verifyEntry(MapNode, Key, Required, [this](msgpack::DocNode &Node) {
    return verifyInteger(Node);
  });
}

bool MetadataVerifier::verifyKernelArgs(msgpack::DocNode &Node) {
  if (!Node.isMap())
    return false;
  auto &ArgsMap = Node.getMap();

  if (!verifyScalarEntry(ArgsMap, ".name", false,
                         msgpack::Type::String))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".type_name", false,
                         msgpack::Type::String))
    return false;
  if (!verifyIntegerEntry(ArgsMap, ".size", true))
    return false;
  if (!verifyIntegerEntry(ArgsMap, ".offset", true))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".value_kind", true, msgpack::Type::String,
                         [](msgpack::DocNode &SNode) {
                           return StringSwitch<bool>(SNode.getString())
                               .Case("by_value", true)
                               .Case("global_buffer", true)
                               .Case("dynamic_shared_pointer", true)
                               .Case("sampler", true)
                               .Case("image", true)
                               .Case("pipe", true)
                               .Case("queue", true)
                               .Case("hidden_block_count_x", true)
                               .Case("hidden_block_count_y", true)
                               .Case("hidden_block_count_z", true)
                               .Case("hidden_group_size_x", true)
                               .Case("hidden_group_size_y", true)
                               .Case("hidden_group_size_z", true)
                               .Case("hidden_remainder_x", true)
                               .Case("hidden_remainder_y", true)
                               .Case("hidden_remainder_z", true)
                               .Case("hidden_global_offset_x", true)
                               .Case("hidden_global_offset_y", true)
                               .Case("hidden_global_offset_z", true)
                               .Case("hidden_grid_dims", true)
                               .Case("hidden_none", true)
                               .Case("hidden_printf_buffer", true)
                               .Case("hidden_hostcall_buffer", true)
                               .Case("hidden_heap_v1", true)
                               .Case("hidden_default_queue", true)
                               .Case("hidden_completion_action", true)
                               .Case("hidden_multigrid_sync_arg", true)
                               .Case("hidden_dynamic_lds_size", true)
                               .Case("hidden_private_base", true)
                               .Case("hidden_shared_base", true)
                               .Case("hidden_queue_ptr", true)
                               .Default(false);
                         }))
    return false;
  if (!verifyIntegerEntry(ArgsMap, ".pointee_align", false))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".address_space", false,
                         msgpack::Type::String,
                         [](msgpack::DocNode &SNode) {
                           return StringSwitch<bool>(SNode.getString())
                               .Case("private", true)
                               .Case("global", true)
                               .Case("constant", true)
                               .Case("local", true)
                               .Case("generic", true)
                               .Case("region", true)
                               .Default(false);
                         }))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".access", false,
                         msgpack::Type::String,
                         [](msgpack::DocNode &SNode) {
                           return StringSwitch<bool>(SNode.getString())
                               .Case("read_only", true)
                               .Case("write_only", true)
                               .Case("read_write", true)
                               .Default(false);
                         }))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".actual_access", false,
                         msgpack::Type::String,
                         [](msgpack::DocNode &SNode) {
                           return StringSwitch<bool>(SNode.getString())
                               .Case("read_only", true)
                               .Case("write_only", true)
                               .Case("read_write", true)
                               .Default(false);
                         }))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".is_const", false,
                         msgpack::Type::Boolean))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".is_restrict", false,
                         msgpack::Type::Boolean))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".is_volatile", false,
                         msgpack::Type::Boolean))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".is_pipe", false,
                         msgpack::Type::Boolean))
    return false;

  return true;
}

bool MetadataVerifier::verifyKernel(msgpack::DocNode &Node) {
  if (!Node.isMap())
    return false;
  auto &KernelMap = Node.getMap();

  if (!verifyScalarEntry(KernelMap, ".name", true,
                         msgpack::Type::String))
    return false;
  if (!verifyScalarEntry(KernelMap, ".symbol", true,
                         msgpack::Type::String))
    return false;
  if (!verifyScalarEntry(KernelMap, ".language", false,
                         msgpack::Type::String,
                         [](msgpack::DocNode &SNode) {
                           return StringSwitch<bool>(SNode.getString())
                               .Case("OpenCL C", true)
                               .Case("OpenCL C++", true)
                               .Case("HCC", true)
                               .Case("HIP", true)
                               .Case("OpenMP", true)
                               .Case("Assembler", true)
                               .Default(false);
                         }))
    return false;
  if (!verifyEntry(
          KernelMap, ".language_version", false, [this](msgpack::DocNode &Node) {
            return verifyArray(
                Node,
                [this](msgpack::DocNode &Node) { return verifyInteger(Node); }, 2);
          }))
    return false;
  if (!verifyEntry(KernelMap, ".args", false, [this](msgpack::DocNode &Node) {
        return verifyArray(Node, [this](msgpack::DocNode &Node) {
          return verifyKernelArgs(Node);
        });
      }))
    return false;
  if (!verifyEntry(KernelMap, ".reqd_workgroup_size", false,
                   [this](msgpack::DocNode &Node) {
                     return verifyArray(Node,
                                        [this](msgpack::DocNode &Node) {
                                          return verifyInteger(Node);
                                        },
                                        3);
                   }))
    return false;
  if (!verifyEntry(KernelMap, ".workgroup_size_hint", false,
                   [this](msgpack::DocNode &Node) {
                     return verifyArray(Node,
                                        [this](msgpack::DocNode &Node) {
                                          return verifyInteger(Node);
                                        },
                                        3);
                   }))
    return false;
  if (!verifyScalarEntry(KernelMap, ".vec_type_hint", false,
                         msgpack::Type::String))
    return false;
  if (!verifyScalarEntry(KernelMap, ".device_enqueue_symbol", false,
                         msgpack::Type::String))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".kernarg_segment_size", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".group_segment_fixed_size", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".private_segment_fixed_size", true))
    return false;
  if (!verifyScalarEntry(KernelMap, ".uses_dynamic_stack", false,
                         msgpack::Type::Boolean))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".workgroup_processor_mode", false))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".kernarg_segment_align", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".wavefront_size", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".sgpr_count", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".vgpr_count", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".max_flat_workgroup_size", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".sgpr_spill_count", false))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".vgpr_spill_count", false))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".uniform_work_group_size", false))
    return false;


  return true;
}

bool MetadataVerifier::verify(msgpack::DocNode &HSAMetadataRoot) {
  if (!HSAMetadataRoot.isMap())
    return false;
  auto &RootMap = HSAMetadataRoot.getMap();

  if (!verifyEntry(
          RootMap, "amdhsa.version", true, [this](msgpack::DocNode &Node) {
            return verifyArray(
                Node,
                [this](msgpack::DocNode &Node) { return verifyInteger(Node); }, 2);
          }))
    return false;
  if (!verifyEntry(
          RootMap, "amdhsa.printf", false, [this](msgpack::DocNode &Node) {
            return verifyArray(Node, [this](msgpack::DocNode &Node) {
              return verifyScalar(Node, msgpack::Type::String);
            });
          }))
    return false;
  if (!verifyEntry(RootMap, "amdhsa.kernels", true,
                   [this](msgpack::DocNode &Node) {
                     return verifyArray(Node, [this](msgpack::DocNode &Node) {
                       return verifyKernel(Node);
                     });
                   }))
    return false;

  return true;
}

} // end namespace V3
} // end namespace HSAMD
} // end namespace AMDGPU
} // end namespace llvm
