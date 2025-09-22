//===- IRObjectFile.cpp - IR object file implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Part of the IRObjectFile class implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/IRObjectFile.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/ObjectFile.h"
using namespace llvm;
using namespace object;

namespace llvm {
class LLVMContext;
class raw_ostream;
} // namespace llvm

IRObjectFile::IRObjectFile(MemoryBufferRef Object,
                           std::vector<std::unique_ptr<Module>> Mods)
    : SymbolicFile(Binary::ID_IR, Object), Mods(std::move(Mods)) {
  for (auto &M : this->Mods)
    SymTab.addModule(M.get());
}

IRObjectFile::~IRObjectFile() = default;

static ModuleSymbolTable::Symbol getSym(DataRefImpl &Symb) {
  return *reinterpret_cast<ModuleSymbolTable::Symbol *>(Symb.p);
}

void IRObjectFile::moveSymbolNext(DataRefImpl &Symb) const {
  Symb.p += sizeof(ModuleSymbolTable::Symbol);
}

Error IRObjectFile::printSymbolName(raw_ostream &OS, DataRefImpl Symb) const {
  SymTab.printSymbolName(OS, getSym(Symb));
  return Error::success();
}

Expected<uint32_t> IRObjectFile::getSymbolFlags(DataRefImpl Symb) const {
  return SymTab.getSymbolFlags(getSym(Symb));
}

basic_symbol_iterator IRObjectFile::symbol_begin() const {
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(SymTab.symbols().data());
  return basic_symbol_iterator(BasicSymbolRef(Ret, this));
}

basic_symbol_iterator IRObjectFile::symbol_end() const {
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(SymTab.symbols().data() +
                                      SymTab.symbols().size());
  return basic_symbol_iterator(BasicSymbolRef(Ret, this));
}

StringRef IRObjectFile::getTargetTriple() const {
  // Each module must have the same target triple, so we arbitrarily access the
  // first one.
  return Mods[0]->getTargetTriple();
}

Expected<MemoryBufferRef>
IRObjectFile::findBitcodeInObject(const ObjectFile &Obj) {
  for (const SectionRef &Sec : Obj.sections()) {
    if (Sec.isBitcode()) {
      Expected<StringRef> Contents = Sec.getContents();
      if (!Contents)
        return Contents.takeError();
      if (Contents->size() <= 1)
        return errorCodeToError(object_error::bitcode_section_not_found);
      return MemoryBufferRef(*Contents, Obj.getFileName());
    }
  }

  return errorCodeToError(object_error::bitcode_section_not_found);
}

Expected<MemoryBufferRef>
IRObjectFile::findBitcodeInMemBuffer(MemoryBufferRef Object) {
  file_magic Type = identify_magic(Object.getBuffer());
  switch (Type) {
  case file_magic::bitcode:
    return Object;
  case file_magic::elf_relocatable:
  case file_magic::macho_object:
  case file_magic::wasm_object:
  case file_magic::coff_object: {
    Expected<std::unique_ptr<ObjectFile>> ObjFile =
        ObjectFile::createObjectFile(Object, Type);
    if (!ObjFile)
      return ObjFile.takeError();
    return findBitcodeInObject(*ObjFile->get());
  }
  default:
    return errorCodeToError(object_error::invalid_file_type);
  }
}

Expected<std::unique_ptr<IRObjectFile>>
IRObjectFile::create(MemoryBufferRef Object, LLVMContext &Context) {
  Expected<MemoryBufferRef> BCOrErr = findBitcodeInMemBuffer(Object);
  if (!BCOrErr)
    return BCOrErr.takeError();

  Expected<std::vector<BitcodeModule>> BMsOrErr =
      getBitcodeModuleList(*BCOrErr);
  if (!BMsOrErr)
    return BMsOrErr.takeError();

  std::vector<std::unique_ptr<Module>> Mods;
  for (auto BM : *BMsOrErr) {
    Expected<std::unique_ptr<Module>> MOrErr =
        BM.getLazyModule(Context, /*ShouldLazyLoadMetadata*/ true,
                         /*IsImporting*/ false);
    if (!MOrErr)
      return MOrErr.takeError();

    Mods.push_back(std::move(*MOrErr));
  }

  return std::unique_ptr<IRObjectFile>(
      new IRObjectFile(*BCOrErr, std::move(Mods)));
}

Expected<IRSymtabFile> object::readIRSymtab(MemoryBufferRef MBRef) {
  IRSymtabFile F;
  Expected<MemoryBufferRef> BCOrErr =
      IRObjectFile::findBitcodeInMemBuffer(MBRef);
  if (!BCOrErr)
    return BCOrErr.takeError();

  Expected<BitcodeFileContents> BFCOrErr = getBitcodeFileContents(*BCOrErr);
  if (!BFCOrErr)
    return BFCOrErr.takeError();

  Expected<irsymtab::FileContents> FCOrErr = irsymtab::readBitcode(*BFCOrErr);
  if (!FCOrErr)
    return FCOrErr.takeError();

  F.Mods = std::move(BFCOrErr->Mods);
  F.Symtab = std::move(FCOrErr->Symtab);
  F.Strtab = std::move(FCOrErr->Strtab);
  F.TheReader = std::move(FCOrErr->TheReader);
  return std::move(F);
}
