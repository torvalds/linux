//===- tools/dsymutil/RelocationMap.cpp - Relocation map representation---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RelocationMap.h"

namespace llvm {

namespace dsymutil {

void RelocationMap::print(raw_ostream &OS) const {
  yaml::Output yout(OS, /* Ctxt = */ nullptr, /* WrapColumn = */ 0);
  yout << const_cast<RelocationMap &>(*this);
}

#ifndef NDEBUG
void RelocationMap::dump() const { print(errs()); }
#endif

void RelocationMap::addRelocationMapEntry(const ValidReloc &Relocation) {
  Relocations.push_back(Relocation);
}

namespace {

struct YAMLContext {
  StringRef PrependPath;
  Triple BinaryTriple;
};

} // end anonymous namespace

ErrorOr<std::unique_ptr<RelocationMap>>
RelocationMap::parseYAMLRelocationMap(StringRef InputFile,
                                      StringRef PrependPath) {
  auto ErrOrFile = MemoryBuffer::getFileOrSTDIN(InputFile);
  if (auto Err = ErrOrFile.getError())
    return Err;

  YAMLContext Ctxt;

  Ctxt.PrependPath = PrependPath;

  std::unique_ptr<RelocationMap> Result;
  yaml::Input yin((*ErrOrFile)->getBuffer(), &Ctxt);
  yin >> Result;

  if (auto EC = yin.error())
    return EC;
  return std::move(Result);
}

} // end namespace dsymutil

namespace yaml {

void MappingTraits<dsymutil::ValidReloc>::mapping(IO &io,
                                                  dsymutil::ValidReloc &VR) {
  io.mapRequired("offset", VR.Offset);
  io.mapRequired("size", VR.Size);
  io.mapRequired("addend", VR.Addend);
  io.mapRequired("symName", VR.SymbolName);
  io.mapOptional("symObjAddr", VR.SymbolMapping.ObjectAddress);
  io.mapRequired("symBinAddr", VR.SymbolMapping.BinaryAddress);
  io.mapRequired("symSize", VR.SymbolMapping.Size);
}

void MappingTraits<dsymutil::RelocationMap>::mapping(
    IO &io, dsymutil::RelocationMap &RM) {
  io.mapRequired("triple", RM.BinaryTriple);
  io.mapRequired("binary-path", RM.BinaryPath);
  if (void *Ctxt = io.getContext())
    reinterpret_cast<YAMLContext *>(Ctxt)->BinaryTriple = RM.BinaryTriple;
  io.mapRequired("relocations", RM.Relocations);
}

void MappingTraits<std::unique_ptr<dsymutil::RelocationMap>>::mapping(
    IO &io, std::unique_ptr<dsymutil::RelocationMap> &RM) {
  if (!RM)
    RM.reset(new RelocationMap());
  io.mapRequired("triple", RM->BinaryTriple);
  io.mapRequired("binary-path", RM->BinaryPath);
  if (void *Ctxt = io.getContext())
    reinterpret_cast<YAMLContext *>(Ctxt)->BinaryTriple = RM->BinaryTriple;
  io.mapRequired("relocations", RM->Relocations);
}
} // end namespace yaml
} // end namespace llvm
