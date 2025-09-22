//===- Utility.cpp ------ Collection of generic offloading utilities ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Frontend/Offloading/Utility.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Value.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace llvm::offloading;

StructType *offloading::getEntryTy(Module &M) {
  LLVMContext &C = M.getContext();
  StructType *EntryTy =
      StructType::getTypeByName(C, "struct.__tgt_offload_entry");
  if (!EntryTy)
    EntryTy = StructType::create(
        "struct.__tgt_offload_entry", PointerType::getUnqual(C),
        PointerType::getUnqual(C), M.getDataLayout().getIntPtrType(C),
        Type::getInt32Ty(C), Type::getInt32Ty(C));
  return EntryTy;
}

// TODO: Rework this interface to be more generic.
std::pair<Constant *, GlobalVariable *>
offloading::getOffloadingEntryInitializer(Module &M, Constant *Addr,
                                          StringRef Name, uint64_t Size,
                                          int32_t Flags, int32_t Data) {
  llvm::Triple Triple(M.getTargetTriple());
  Type *Int8PtrTy = PointerType::getUnqual(M.getContext());
  Type *Int32Ty = Type::getInt32Ty(M.getContext());
  Type *SizeTy = M.getDataLayout().getIntPtrType(M.getContext());

  Constant *AddrName = ConstantDataArray::getString(M.getContext(), Name);

  StringRef Prefix =
      Triple.isNVPTX() ? "$offloading$entry_name" : ".offloading.entry_name";

  // Create the constant string used to look up the symbol in the device.
  auto *Str =
      new GlobalVariable(M, AddrName->getType(), /*isConstant=*/true,
                         GlobalValue::InternalLinkage, AddrName, Prefix);
  Str->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  // Construct the offloading entry.
  Constant *EntryData[] = {
      ConstantExpr::getPointerBitCastOrAddrSpaceCast(Addr, Int8PtrTy),
      ConstantExpr::getPointerBitCastOrAddrSpaceCast(Str, Int8PtrTy),
      ConstantInt::get(SizeTy, Size),
      ConstantInt::get(Int32Ty, Flags),
      ConstantInt::get(Int32Ty, Data),
  };
  Constant *EntryInitializer = ConstantStruct::get(getEntryTy(M), EntryData);
  return {EntryInitializer, Str};
}

void offloading::emitOffloadingEntry(Module &M, Constant *Addr, StringRef Name,
                                     uint64_t Size, int32_t Flags, int32_t Data,
                                     StringRef SectionName) {
  llvm::Triple Triple(M.getTargetTriple());

  auto [EntryInitializer, NameGV] =
      getOffloadingEntryInitializer(M, Addr, Name, Size, Flags, Data);

  StringRef Prefix =
      Triple.isNVPTX() ? "$offloading$entry$" : ".offloading.entry.";
  auto *Entry = new GlobalVariable(
      M, getEntryTy(M),
      /*isConstant=*/true, GlobalValue::WeakAnyLinkage, EntryInitializer,
      Prefix + Name, nullptr, GlobalValue::NotThreadLocal,
      M.getDataLayout().getDefaultGlobalsAddressSpace());

  // The entry has to be created in the section the linker expects it to be.
  if (Triple.isOSBinFormatCOFF())
    Entry->setSection((SectionName + "$OE").str());
  else
    Entry->setSection(SectionName);
  Entry->setAlignment(Align(1));
}

std::pair<GlobalVariable *, GlobalVariable *>
offloading::getOffloadEntryArray(Module &M, StringRef SectionName) {
  llvm::Triple Triple(M.getTargetTriple());

  auto *ZeroInitilaizer =
      ConstantAggregateZero::get(ArrayType::get(getEntryTy(M), 0u));
  auto *EntryInit = Triple.isOSBinFormatCOFF() ? ZeroInitilaizer : nullptr;
  auto *EntryType = ArrayType::get(getEntryTy(M), 0);
  auto Linkage = Triple.isOSBinFormatCOFF() ? GlobalValue::WeakODRLinkage
                                            : GlobalValue::ExternalLinkage;

  auto *EntriesB =
      new GlobalVariable(M, EntryType, /*isConstant=*/true, Linkage, EntryInit,
                         "__start_" + SectionName);
  EntriesB->setVisibility(GlobalValue::HiddenVisibility);
  auto *EntriesE =
      new GlobalVariable(M, EntryType, /*isConstant=*/true, Linkage, EntryInit,
                         "__stop_" + SectionName);
  EntriesE->setVisibility(GlobalValue::HiddenVisibility);

  if (Triple.isOSBinFormatELF()) {
    // We assume that external begin/end symbols that we have created above will
    // be defined by the linker. This is done whenever a section name with a
    // valid C-identifier is present. We define a dummy variable here to force
    // the linker to always provide these symbols.
    auto *DummyEntry = new GlobalVariable(
        M, ZeroInitilaizer->getType(), true, GlobalVariable::InternalLinkage,
        ZeroInitilaizer, "__dummy." + SectionName);
    DummyEntry->setSection(SectionName);
    appendToCompilerUsed(M, DummyEntry);
  } else {
    // The COFF linker will merge sections containing a '$' together into a
    // single section. The order of entries in this section will be sorted
    // alphabetically by the characters following the '$' in the name. Set the
    // sections here to ensure that the beginning and end symbols are sorted.
    EntriesB->setSection((SectionName + "$OA").str());
    EntriesE->setSection((SectionName + "$OZ").str());
  }

  return std::make_pair(EntriesB, EntriesE);
}
