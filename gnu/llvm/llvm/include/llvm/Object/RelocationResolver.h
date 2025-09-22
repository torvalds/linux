//===- RelocVisitor.h - Visitor for object file relocations -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a wrapper around all the different types of relocations
// in different file formats, such that a client can handle them in a unified
// manner by only implementing a minimal number of functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_RELOCATIONRESOLVER_H
#define LLVM_OBJECT_RELOCATIONRESOLVER_H

#include <cstdint>
#include <utility>

namespace llvm {
namespace object {

class ObjectFile;
class RelocationRef;

using SupportsRelocation = bool (*)(uint64_t);
using RelocationResolver = uint64_t (*)(uint64_t Type, uint64_t Offset,
                                        uint64_t S, uint64_t LocData,
                                        int64_t Addend);

std::pair<SupportsRelocation, RelocationResolver>
getRelocationResolver(const ObjectFile &Obj);

uint64_t resolveRelocation(RelocationResolver Resolver, const RelocationRef &R,
                           uint64_t S, uint64_t LocData);

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_RELOCATIONRESOLVER_H
