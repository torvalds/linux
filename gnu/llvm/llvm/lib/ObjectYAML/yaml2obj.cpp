//===-- yaml2obj.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/ObjectYAML/ObjectYAML.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/YAMLTraits.h"

namespace llvm {
namespace yaml {

bool convertYAML(yaml::Input &YIn, raw_ostream &Out, ErrorHandler ErrHandler,
                 unsigned DocNum, uint64_t MaxSize) {
  unsigned CurDocNum = 0;
  do {
    if (++CurDocNum != DocNum)
      continue;

    yaml::YamlObjectFile Doc;
    YIn >> Doc;
    if (std::error_code EC = YIn.error()) {
      ErrHandler("failed to parse YAML input: " + EC.message());
      return false;
    }

    if (Doc.Arch)
      return yaml2archive(*Doc.Arch, Out, ErrHandler);
    if (Doc.Elf)
      return yaml2elf(*Doc.Elf, Out, ErrHandler, MaxSize);
    if (Doc.Coff)
      return yaml2coff(*Doc.Coff, Out, ErrHandler);
    if (Doc.Goff)
      return yaml2goff(*Doc.Goff, Out, ErrHandler);
    if (Doc.MachO || Doc.FatMachO)
      return yaml2macho(Doc, Out, ErrHandler);
    if (Doc.Minidump)
      return yaml2minidump(*Doc.Minidump, Out, ErrHandler);
    if (Doc.Offload)
      return yaml2offload(*Doc.Offload, Out, ErrHandler);
    if (Doc.Wasm)
      return yaml2wasm(*Doc.Wasm, Out, ErrHandler);
    if (Doc.Xcoff)
      return yaml2xcoff(*Doc.Xcoff, Out, ErrHandler);
    if (Doc.DXContainer)
      return yaml2dxcontainer(*Doc.DXContainer, Out, ErrHandler);

    ErrHandler("unknown document type");
    return false;

  } while (YIn.nextDocument());

  ErrHandler("cannot find the " + Twine(DocNum) +
             getOrdinalSuffix(DocNum).data() + " document");
  return false;
}

std::unique_ptr<object::ObjectFile>
yaml2ObjectFile(SmallVectorImpl<char> &Storage, StringRef Yaml,
                ErrorHandler ErrHandler) {
  Storage.clear();
  raw_svector_ostream OS(Storage);

  yaml::Input YIn(Yaml);
  if (!convertYAML(YIn, OS, ErrHandler))
    return {};

  Expected<std::unique_ptr<object::ObjectFile>> ObjOrErr =
      object::ObjectFile::createObjectFile(
          MemoryBufferRef(OS.str(), "YamlObject"));
  if (ObjOrErr)
    return std::move(*ObjOrErr);

  ErrHandler(toString(ObjOrErr.takeError()));
  return {};
}

} // namespace yaml
} // namespace llvm
