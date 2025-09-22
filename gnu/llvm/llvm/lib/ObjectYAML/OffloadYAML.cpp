//===- OffloadYAML.cpp - Offload Binary YAMLIO implementation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of offload
// binaries.
//
//===----------------------------------------------------------------------===//

#include <llvm/ObjectYAML/OffloadYAML.h>

namespace llvm {

namespace yaml {

void ScalarEnumerationTraits<object::ImageKind>::enumeration(
    IO &IO, object::ImageKind &Value) {
#define ECase(X) IO.enumCase(Value, #X, object::X)
  ECase(IMG_None);
  ECase(IMG_Object);
  ECase(IMG_Bitcode);
  ECase(IMG_Cubin);
  ECase(IMG_Fatbinary);
  ECase(IMG_PTX);
  ECase(IMG_LAST);
#undef ECase
  IO.enumFallback<Hex16>(Value);
}

void ScalarEnumerationTraits<object::OffloadKind>::enumeration(
    IO &IO, object::OffloadKind &Value) {
#define ECase(X) IO.enumCase(Value, #X, object::X)
  ECase(OFK_None);
  ECase(OFK_OpenMP);
  ECase(OFK_Cuda);
  ECase(OFK_HIP);
  ECase(OFK_LAST);
#undef ECase
  IO.enumFallback<Hex16>(Value);
}

void MappingTraits<OffloadYAML::Binary>::mapping(IO &IO,
                                                 OffloadYAML::Binary &O) {
  assert(!IO.getContext() && "The IO context is initialized already");
  IO.setContext(&O);
  IO.mapTag("!Offload", true);
  IO.mapOptional("Version", O.Version);
  IO.mapOptional("Size", O.Size);
  IO.mapOptional("EntryOffset", O.EntryOffset);
  IO.mapOptional("EntrySize", O.EntrySize);
  IO.mapRequired("Members", O.Members);
  IO.setContext(nullptr);
}

void MappingTraits<OffloadYAML::Binary::StringEntry>::mapping(
    IO &IO, OffloadYAML::Binary::StringEntry &SE) {
  assert(IO.getContext() && "The IO context is not initialized");
  IO.mapRequired("Key", SE.Key);
  IO.mapRequired("Value", SE.Value);
}

void MappingTraits<OffloadYAML::Binary::Member>::mapping(
    IO &IO, OffloadYAML::Binary::Member &M) {
  assert(IO.getContext() && "The IO context is not initialized");
  IO.mapOptional("ImageKind", M.ImageKind);
  IO.mapOptional("OffloadKind", M.OffloadKind);
  IO.mapOptional("Flags", M.Flags);
  IO.mapOptional("String", M.StringEntries);
  IO.mapOptional("Content", M.Content);
}

} // namespace yaml

} // namespace llvm
