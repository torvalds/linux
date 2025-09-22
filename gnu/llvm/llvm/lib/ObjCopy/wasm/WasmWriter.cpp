//===- WasmWriter.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "WasmWriter.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace objcopy {
namespace wasm {

using namespace object;
using namespace llvm::wasm;

Writer::SectionHeader Writer::createSectionHeader(const Section &S,
                                                  size_t &SectionSize) {
  SectionHeader Header;
  raw_svector_ostream OS(Header);
  OS << S.SectionType;
  bool HasName = S.SectionType == WASM_SEC_CUSTOM;
  SectionSize = S.Contents.size();
  if (HasName)
    SectionSize += getULEB128Size(S.Name.size()) + S.Name.size();
  // If we read this section from an object file, use its original size for the
  // padding of the LEB value to avoid changing the file size. Otherwise, pad
  // out to 5 bytes to make it predictable, and match the behavior of clang.
  unsigned HeaderSecSizeEncodingLen =
      S.HeaderSecSizeEncodingLen ? *S.HeaderSecSizeEncodingLen : 5;
  encodeULEB128(SectionSize, OS, HeaderSecSizeEncodingLen);
  if (HasName) {
    encodeULEB128(S.Name.size(), OS);
    OS << S.Name;
  }
  // Total section size is the content size plus 1 for the section type and
  // the LEB-encoded size.
  SectionSize = SectionSize + 1 + HeaderSecSizeEncodingLen;
  return Header;
}

size_t Writer::finalize() {
  size_t ObjectSize = sizeof(WasmMagic) + sizeof(WasmVersion);
  SectionHeaders.reserve(Obj.Sections.size());
  // Finalize the headers of each section so we know the total size.
  for (const Section &S : Obj.Sections) {
    size_t SectionSize;
    SectionHeaders.push_back(createSectionHeader(S, SectionSize));
    ObjectSize += SectionSize;
  }
  return ObjectSize;
}

Error Writer::write() {
  size_t TotalSize = finalize();
  Out.reserveExtraSpace(TotalSize);

  // Write the header.
  Out.write(Obj.Header.Magic.data(), Obj.Header.Magic.size());
  uint32_t Version;
  support::endian::write32le(&Version, Obj.Header.Version);
  Out.write(reinterpret_cast<const char *>(&Version), sizeof(Version));

  // Write each section.
  for (size_t I = 0, S = SectionHeaders.size(); I < S; ++I) {
    Out.write(SectionHeaders[I].data(), SectionHeaders[I].size());
    Out.write(reinterpret_cast<const char *>(Obj.Sections[I].Contents.data()),
              Obj.Sections[I].Contents.size());
  }

  return Error::success();
}

} // end namespace wasm
} // end namespace objcopy
} // end namespace llvm
