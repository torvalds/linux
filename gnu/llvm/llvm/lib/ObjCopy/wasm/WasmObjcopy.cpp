//===- WasmObjcopy.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjCopy/wasm/WasmObjcopy.h"
#include "WasmObject.h"
#include "WasmReader.h"
#include "WasmWriter.h"
#include "llvm/ObjCopy/CommonConfig.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileOutputBuffer.h"

namespace llvm {
namespace objcopy {
namespace wasm {

using namespace object;
using SectionPred = std::function<bool(const Section &Sec)>;

static bool isDebugSection(const Section &Sec) {
  return Sec.Name.starts_with(".debug");
}

static bool isLinkerSection(const Section &Sec) {
  return Sec.Name.starts_with("reloc.") || Sec.Name == "linking";
}

static bool isNameSection(const Section &Sec) { return Sec.Name == "name"; }

// Sections which are known to be "comments" or informational and do not affect
// program semantics.
static bool isCommentSection(const Section &Sec) {
  return Sec.Name == "producers";
}

static Error dumpSectionToFile(StringRef SecName, StringRef Filename,
                               Object &Obj) {
  for (const Section &Sec : Obj.Sections) {
    if (Sec.Name == SecName) {
      ArrayRef<uint8_t> Contents = Sec.Contents;
      Expected<std::unique_ptr<FileOutputBuffer>> BufferOrErr =
          FileOutputBuffer::create(Filename, Contents.size());
      if (!BufferOrErr)
        return BufferOrErr.takeError();
      std::unique_ptr<FileOutputBuffer> Buf = std::move(*BufferOrErr);
      std::copy(Contents.begin(), Contents.end(), Buf->getBufferStart());
      if (Error E = Buf->commit())
        return E;
      return Error::success();
    }
  }
  return createStringError(errc::invalid_argument, "section '%s' not found",
                           SecName.str().c_str());
}

static void removeSections(const CommonConfig &Config, Object &Obj) {
  SectionPred RemovePred = [](const Section &) { return false; };

  // Explicitly-requested sections.
  if (!Config.ToRemove.empty()) {
    RemovePred = [&Config](const Section &Sec) {
      return Config.ToRemove.matches(Sec.Name);
    };
  }

  if (Config.StripDebug) {
    RemovePred = [RemovePred](const Section &Sec) {
      return RemovePred(Sec) || isDebugSection(Sec);
    };
  }

  if (Config.StripAll) {
    RemovePred = [RemovePred](const Section &Sec) {
      return RemovePred(Sec) || isDebugSection(Sec) || isLinkerSection(Sec) ||
             isNameSection(Sec) || isCommentSection(Sec);
    };
  }

  if (Config.OnlyKeepDebug) {
    RemovePred = [&Config](const Section &Sec) {
      // Keep debug sections, unless explicitly requested to remove.
      // Remove everything else, including known sections.
      return Config.ToRemove.matches(Sec.Name) || !isDebugSection(Sec);
    };
  }

  if (!Config.OnlySection.empty()) {
    RemovePred = [&Config](const Section &Sec) {
      // Explicitly keep these sections regardless of previous removes.
      // Remove everything else, inluding known sections.
      return !Config.OnlySection.matches(Sec.Name);
    };
  }

  if (!Config.KeepSection.empty()) {
    RemovePred = [&Config, RemovePred](const Section &Sec) {
      // Explicitly keep these sections regardless of previous removes.
      if (Config.KeepSection.matches(Sec.Name))
        return false;
      // Otherwise defer to RemovePred.
      return RemovePred(Sec);
    };
  }

  Obj.removeSections(RemovePred);
}

static Error handleArgs(const CommonConfig &Config, Object &Obj) {
  // Only support AddSection, DumpSection, RemoveSection for now.
  for (StringRef Flag : Config.DumpSection) {
    StringRef SecName;
    StringRef FileName;
    std::tie(SecName, FileName) = Flag.split("=");
    if (Error E = dumpSectionToFile(SecName, FileName, Obj))
      return createFileError(FileName, std::move(E));
  }

  removeSections(Config, Obj);

  for (const NewSectionInfo &NewSection : Config.AddSection) {
    Section Sec;
    Sec.SectionType = llvm::wasm::WASM_SEC_CUSTOM;
    Sec.Name = NewSection.SectionName;

    llvm::StringRef InputData =
        llvm::StringRef(NewSection.SectionData->getBufferStart(),
                        NewSection.SectionData->getBufferSize());
    std::unique_ptr<MemoryBuffer> BufferCopy = MemoryBuffer::getMemBufferCopy(
        InputData, NewSection.SectionData->getBufferIdentifier());
    Sec.Contents = ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(BufferCopy->getBufferStart()),
        BufferCopy->getBufferSize());

    Obj.addSectionWithOwnedContents(Sec, std::move(BufferCopy));
  }

  return Error::success();
}

Error executeObjcopyOnBinary(const CommonConfig &Config, const WasmConfig &,
                             object::WasmObjectFile &In, raw_ostream &Out) {
  Reader TheReader(In);
  Expected<std::unique_ptr<Object>> ObjOrErr = TheReader.create();
  if (!ObjOrErr)
    return createFileError(Config.InputFilename, ObjOrErr.takeError());
  Object *Obj = ObjOrErr->get();
  assert(Obj && "Unable to deserialize Wasm object");
  if (Error E = handleArgs(Config, *Obj))
    return E;
  Writer TheWriter(*Obj, Out);
  if (Error E = TheWriter.write())
    return createFileError(Config.OutputFilename, std::move(E));
  return Error::success();
}

} // end namespace wasm
} // end namespace objcopy
} // end namespace llvm
