//===------ utils/obj2yaml.cpp - obj2yaml conversion tool -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "obj2yaml.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/Minidump.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace llvm::object;

static cl::OptionCategory Cat("obj2yaml Options");

static cl::opt<std::string>
    InputFilename(cl::Positional, cl::desc("<input file>"), cl::init("-"));
static cl::opt<std::string> OutputFilename("o", cl::desc("Output filename"),
                                           cl::value_desc("filename"),
                                           cl::init("-"), cl::Prefix,
                                           cl::cat(Cat));
static cl::bits<RawSegments> RawSegment(
    "raw-segment",
    cl::desc("Mach-O: dump the raw contents of the listed segments instead of "
             "parsing them:"),
    cl::values(clEnumVal(data, "__DATA"), clEnumVal(linkedit, "__LINKEDIT")),
    cl::cat(Cat));

static Error dumpObject(const ObjectFile &Obj, raw_ostream &OS) {
  if (Obj.isCOFF())
    return errorCodeToError(coff2yaml(OS, cast<COFFObjectFile>(Obj)));

  if (Obj.isXCOFF())
    return xcoff2yaml(OS, cast<XCOFFObjectFile>(Obj));

  if (Obj.isELF())
    return elf2yaml(OS, Obj);

  if (Obj.isWasm())
    return errorCodeToError(wasm2yaml(OS, cast<WasmObjectFile>(Obj)));

  llvm_unreachable("unexpected object file format");
}

static Error dumpInput(StringRef File, raw_ostream &OS) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(File, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);
  if (std::error_code EC = FileOrErr.getError())
    return errorCodeToError(EC);
  std::unique_ptr<MemoryBuffer> &Buffer = FileOrErr.get();
  MemoryBufferRef MemBuf = Buffer->getMemBufferRef();
  switch (identify_magic(MemBuf.getBuffer())) {
  case file_magic::archive:
    return archive2yaml(OS, MemBuf);
  case file_magic::dxcontainer_object:
    return dxcontainer2yaml(OS, MemBuf);
  case file_magic::offload_binary:
    return offload2yaml(OS, MemBuf);
  default:
    break;
  }

  Expected<std::unique_ptr<Binary>> BinOrErr =
      createBinary(MemBuf, /*Context=*/nullptr);
  if (!BinOrErr)
    return BinOrErr.takeError();

  Binary &Binary = *BinOrErr->get();
  // Universal MachO is not a subclass of ObjectFile, so it needs to be handled
  // here with the other binary types.
  if (Binary.isMachO() || Binary.isMachOUniversalBinary())
    return macho2yaml(OS, Binary, RawSegment.getBits());
  if (ObjectFile *Obj = dyn_cast<ObjectFile>(&Binary))
    return dumpObject(*Obj, OS);
  if (MinidumpFile *Minidump = dyn_cast<MinidumpFile>(&Binary))
    return minidump2yaml(OS, *Minidump);

  return Error::success();
}

static void reportError(StringRef Input, Error Err) {
  if (Input == "-")
    Input = "<stdin>";
  std::string ErrMsg;
  raw_string_ostream OS(ErrMsg);
  logAllUnhandledErrors(std::move(Err), OS);
  OS.flush();
  errs() << "Error reading file: " << Input << ": " << ErrMsg;
  errs().flush();
}

int main(int argc, char *argv[]) {
  InitLLVM X(argc, argv);
  cl::HideUnrelatedOptions(Cat);
  cl::ParseCommandLineOptions(
      argc, argv, "Dump a YAML description from an object file", nullptr,
      nullptr, /*LongOptionsUseDoubleDash=*/true);

  std::error_code EC;
  std::unique_ptr<ToolOutputFile> Out(
      new ToolOutputFile(OutputFilename, EC, sys::fs::OF_Text));
  if (EC) {
    WithColor::error(errs(), "obj2yaml")
        << "failed to open '" + OutputFilename + "': " + EC.message() << '\n';
    return 1;
  }
  if (Error Err = dumpInput(InputFilename, Out->os())) {
    reportError(InputFilename, std::move(Err));
    return 1;
  }
  Out->keep();

  return 0;
}
