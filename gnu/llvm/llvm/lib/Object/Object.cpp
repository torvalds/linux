//===- Object.cpp - C bindings to the object file library--------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the C bindings to the file-format-independent object
// library.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Object.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Support/MemAlloc.h"

using namespace llvm;
using namespace object;

inline OwningBinary<ObjectFile> *unwrap(LLVMObjectFileRef OF) {
  return reinterpret_cast<OwningBinary<ObjectFile> *>(OF);
}

inline LLVMObjectFileRef wrap(const OwningBinary<ObjectFile> *OF) {
  return reinterpret_cast<LLVMObjectFileRef>(
      const_cast<OwningBinary<ObjectFile> *>(OF));
}

inline section_iterator *unwrap(LLVMSectionIteratorRef SI) {
  return reinterpret_cast<section_iterator*>(SI);
}

inline LLVMSectionIteratorRef
wrap(const section_iterator *SI) {
  return reinterpret_cast<LLVMSectionIteratorRef>
    (const_cast<section_iterator*>(SI));
}

inline symbol_iterator *unwrap(LLVMSymbolIteratorRef SI) {
  return reinterpret_cast<symbol_iterator*>(SI);
}

inline LLVMSymbolIteratorRef
wrap(const symbol_iterator *SI) {
  return reinterpret_cast<LLVMSymbolIteratorRef>
    (const_cast<symbol_iterator*>(SI));
}

inline relocation_iterator *unwrap(LLVMRelocationIteratorRef SI) {
  return reinterpret_cast<relocation_iterator*>(SI);
}

inline LLVMRelocationIteratorRef
wrap(const relocation_iterator *SI) {
  return reinterpret_cast<LLVMRelocationIteratorRef>
    (const_cast<relocation_iterator*>(SI));
}

/*--.. Operations on binary files ..........................................--*/

LLVMBinaryRef LLVMCreateBinary(LLVMMemoryBufferRef MemBuf,
                               LLVMContextRef Context,
                               char **ErrorMessage) {
  auto maybeContext = Context ? unwrap(Context) : nullptr;
  Expected<std::unique_ptr<Binary>> ObjOrErr(
      createBinary(unwrap(MemBuf)->getMemBufferRef(), maybeContext));
  if (!ObjOrErr) {
    *ErrorMessage = strdup(toString(ObjOrErr.takeError()).c_str());
    return nullptr;
  }

  return wrap(ObjOrErr.get().release());
}

LLVMMemoryBufferRef LLVMBinaryCopyMemoryBuffer(LLVMBinaryRef BR) {
  auto Buf = unwrap(BR)->getMemoryBufferRef();
  return wrap(llvm::MemoryBuffer::getMemBuffer(
                Buf.getBuffer(), Buf.getBufferIdentifier(),
                /*RequiresNullTerminator*/false).release());
}

void LLVMDisposeBinary(LLVMBinaryRef BR) {
  delete unwrap(BR);
}

LLVMBinaryType LLVMBinaryGetType(LLVMBinaryRef BR) {
  class BinaryTypeMapper final : public Binary {
  public:
    static LLVMBinaryType mapBinaryTypeToLLVMBinaryType(unsigned Kind) {
      switch (Kind) {
      case ID_Archive:
        return LLVMBinaryTypeArchive;
      case ID_MachOUniversalBinary:
        return LLVMBinaryTypeMachOUniversalBinary;
      case ID_COFFImportFile:
        return LLVMBinaryTypeCOFFImportFile;
      case ID_IR:
        return LLVMBinaryTypeIR;
      case ID_WinRes:
        return LLVMBinaryTypeWinRes;
      case ID_COFF:
        return LLVMBinaryTypeCOFF;
      case ID_ELF32L:
        return LLVMBinaryTypeELF32L;
      case ID_ELF32B:
        return LLVMBinaryTypeELF32B;
      case ID_ELF64L:
        return LLVMBinaryTypeELF64L;
      case ID_ELF64B:
        return LLVMBinaryTypeELF64B;
      case ID_MachO32L:
        return LLVMBinaryTypeMachO32L;
      case ID_MachO32B:
        return LLVMBinaryTypeMachO32B;
      case ID_MachO64L:
        return LLVMBinaryTypeMachO64L;
      case ID_MachO64B:
        return LLVMBinaryTypeMachO64B;
      case ID_Offload:
        return LLVMBinaryTypeOffload;
      case ID_Wasm:
        return LLVMBinaryTypeWasm;
      case ID_StartObjects:
      case ID_EndObjects:
        llvm_unreachable("Marker types are not valid binary kinds!");
      default:
        llvm_unreachable("Unknown binary kind!");
      }
    }
  };
  return BinaryTypeMapper::mapBinaryTypeToLLVMBinaryType(unwrap(BR)->getType());
}

LLVMBinaryRef LLVMMachOUniversalBinaryCopyObjectForArch(LLVMBinaryRef BR,
                                                        const char *Arch,
                                                        size_t ArchLen,
                                                        char **ErrorMessage) {
  auto universal = cast<MachOUniversalBinary>(unwrap(BR));
  Expected<std::unique_ptr<ObjectFile>> ObjOrErr(
      universal->getMachOObjectForArch({Arch, ArchLen}));
  if (!ObjOrErr) {
    *ErrorMessage = strdup(toString(ObjOrErr.takeError()).c_str());
    return nullptr;
  }
  return wrap(ObjOrErr.get().release());
}

LLVMSectionIteratorRef LLVMObjectFileCopySectionIterator(LLVMBinaryRef BR) {
  auto OF = cast<ObjectFile>(unwrap(BR));
  auto sections = OF->sections();
  if (sections.begin() == sections.end())
    return nullptr;
  return wrap(new section_iterator(sections.begin()));
}

LLVMBool LLVMObjectFileIsSectionIteratorAtEnd(LLVMBinaryRef BR,
                                              LLVMSectionIteratorRef SI) {
  auto OF = cast<ObjectFile>(unwrap(BR));
  return (*unwrap(SI) == OF->section_end()) ? 1 : 0;
}

LLVMSymbolIteratorRef LLVMObjectFileCopySymbolIterator(LLVMBinaryRef BR) {
  auto OF = cast<ObjectFile>(unwrap(BR));
  auto symbols = OF->symbols();
  if (symbols.begin() == symbols.end())
    return nullptr;
  return wrap(new symbol_iterator(symbols.begin()));
}

LLVMBool LLVMObjectFileIsSymbolIteratorAtEnd(LLVMBinaryRef BR,
                                             LLVMSymbolIteratorRef SI) {
  auto OF = cast<ObjectFile>(unwrap(BR));
  return (*unwrap(SI) == OF->symbol_end()) ? 1 : 0;
}

// ObjectFile creation
LLVMObjectFileRef LLVMCreateObjectFile(LLVMMemoryBufferRef MemBuf) {
  std::unique_ptr<MemoryBuffer> Buf(unwrap(MemBuf));
  Expected<std::unique_ptr<ObjectFile>> ObjOrErr(
      ObjectFile::createObjectFile(Buf->getMemBufferRef()));
  std::unique_ptr<ObjectFile> Obj;
  if (!ObjOrErr) {
    // TODO: Actually report errors helpfully.
    consumeError(ObjOrErr.takeError());
    return nullptr;
  }

  auto *Ret = new OwningBinary<ObjectFile>(std::move(ObjOrErr.get()), std::move(Buf));
  return wrap(Ret);
}

void LLVMDisposeObjectFile(LLVMObjectFileRef ObjectFile) {
  delete unwrap(ObjectFile);
}

// ObjectFile Section iterators
LLVMSectionIteratorRef LLVMGetSections(LLVMObjectFileRef OF) {
  OwningBinary<ObjectFile> *OB = unwrap(OF);
  section_iterator SI = OB->getBinary()->section_begin();
  return wrap(new section_iterator(SI));
}

void LLVMDisposeSectionIterator(LLVMSectionIteratorRef SI) {
  delete unwrap(SI);
}

LLVMBool LLVMIsSectionIteratorAtEnd(LLVMObjectFileRef OF,
                                    LLVMSectionIteratorRef SI) {
  OwningBinary<ObjectFile> *OB = unwrap(OF);
  return (*unwrap(SI) == OB->getBinary()->section_end()) ? 1 : 0;
}

void LLVMMoveToNextSection(LLVMSectionIteratorRef SI) {
  ++(*unwrap(SI));
}

void LLVMMoveToContainingSection(LLVMSectionIteratorRef Sect,
                                 LLVMSymbolIteratorRef Sym) {
  Expected<section_iterator> SecOrErr = (*unwrap(Sym))->getSection();
  if (!SecOrErr) {
   std::string Buf;
   raw_string_ostream OS(Buf);
   logAllUnhandledErrors(SecOrErr.takeError(), OS);
   report_fatal_error(Twine(OS.str()));
  }
  *unwrap(Sect) = *SecOrErr;
}

// ObjectFile Symbol iterators
LLVMSymbolIteratorRef LLVMGetSymbols(LLVMObjectFileRef OF) {
  OwningBinary<ObjectFile> *OB = unwrap(OF);
  symbol_iterator SI = OB->getBinary()->symbol_begin();
  return wrap(new symbol_iterator(SI));
}

void LLVMDisposeSymbolIterator(LLVMSymbolIteratorRef SI) {
  delete unwrap(SI);
}

LLVMBool LLVMIsSymbolIteratorAtEnd(LLVMObjectFileRef OF,
                                   LLVMSymbolIteratorRef SI) {
  OwningBinary<ObjectFile> *OB = unwrap(OF);
  return (*unwrap(SI) == OB->getBinary()->symbol_end()) ? 1 : 0;
}

void LLVMMoveToNextSymbol(LLVMSymbolIteratorRef SI) {
  ++(*unwrap(SI));
}

// SectionRef accessors
const char *LLVMGetSectionName(LLVMSectionIteratorRef SI) {
  auto NameOrErr = (*unwrap(SI))->getName();
  if (!NameOrErr)
    report_fatal_error(NameOrErr.takeError());
  return NameOrErr->data();
}

uint64_t LLVMGetSectionSize(LLVMSectionIteratorRef SI) {
  return (*unwrap(SI))->getSize();
}

const char *LLVMGetSectionContents(LLVMSectionIteratorRef SI) {
  if (Expected<StringRef> E = (*unwrap(SI))->getContents())
    return E->data();
  else
    report_fatal_error(E.takeError());
}

uint64_t LLVMGetSectionAddress(LLVMSectionIteratorRef SI) {
  return (*unwrap(SI))->getAddress();
}

LLVMBool LLVMGetSectionContainsSymbol(LLVMSectionIteratorRef SI,
                                 LLVMSymbolIteratorRef Sym) {
  return (*unwrap(SI))->containsSymbol(**unwrap(Sym));
}

// Section Relocation iterators
LLVMRelocationIteratorRef LLVMGetRelocations(LLVMSectionIteratorRef Section) {
  relocation_iterator SI = (*unwrap(Section))->relocation_begin();
  return wrap(new relocation_iterator(SI));
}

void LLVMDisposeRelocationIterator(LLVMRelocationIteratorRef SI) {
  delete unwrap(SI);
}

LLVMBool LLVMIsRelocationIteratorAtEnd(LLVMSectionIteratorRef Section,
                                       LLVMRelocationIteratorRef SI) {
  return (*unwrap(SI) == (*unwrap(Section))->relocation_end()) ? 1 : 0;
}

void LLVMMoveToNextRelocation(LLVMRelocationIteratorRef SI) {
  ++(*unwrap(SI));
}


// SymbolRef accessors
const char *LLVMGetSymbolName(LLVMSymbolIteratorRef SI) {
  Expected<StringRef> Ret = (*unwrap(SI))->getName();
  if (!Ret) {
    std::string Buf;
    raw_string_ostream OS(Buf);
    logAllUnhandledErrors(Ret.takeError(), OS);
    report_fatal_error(Twine(OS.str()));
  }
  return Ret->data();
}

uint64_t LLVMGetSymbolAddress(LLVMSymbolIteratorRef SI) {
  Expected<uint64_t> Ret = (*unwrap(SI))->getAddress();
  if (!Ret) {
    std::string Buf;
    raw_string_ostream OS(Buf);
    logAllUnhandledErrors(Ret.takeError(), OS);
    report_fatal_error(Twine(OS.str()));
  }
  return *Ret;
}

uint64_t LLVMGetSymbolSize(LLVMSymbolIteratorRef SI) {
  return (*unwrap(SI))->getCommonSize();
}

// RelocationRef accessors
uint64_t LLVMGetRelocationOffset(LLVMRelocationIteratorRef RI) {
  return (*unwrap(RI))->getOffset();
}

LLVMSymbolIteratorRef LLVMGetRelocationSymbol(LLVMRelocationIteratorRef RI) {
  symbol_iterator ret = (*unwrap(RI))->getSymbol();
  return wrap(new symbol_iterator(ret));
}

uint64_t LLVMGetRelocationType(LLVMRelocationIteratorRef RI) {
  return (*unwrap(RI))->getType();
}

// NOTE: Caller takes ownership of returned string.
const char *LLVMGetRelocationTypeName(LLVMRelocationIteratorRef RI) {
  SmallVector<char, 0> ret;
  (*unwrap(RI))->getTypeName(ret);
  char *str = static_cast<char*>(safe_malloc(ret.size()));
  llvm::copy(ret, str);
  return str;
}

// NOTE: Caller takes ownership of returned string.
const char *LLVMGetRelocationValueString(LLVMRelocationIteratorRef RI) {
  return strdup("");
}

