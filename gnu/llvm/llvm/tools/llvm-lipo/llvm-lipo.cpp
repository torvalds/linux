//===-- llvm-lipo.cpp - a tool for manipulating universal binaries --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A utility for creating / splitting / inspecting universal binaries.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/MachOUniversalWriter.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/WithColor.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/TextAPI/Architecture.h"
#include <optional>

using namespace llvm;
using namespace llvm::object;

static const StringRef ToolName = "llvm-lipo";

[[noreturn]] static void reportError(Twine Message) {
  WithColor::error(errs(), ToolName) << Message << "\n";
  errs().flush();
  exit(EXIT_FAILURE);
}

[[noreturn]] static void reportError(Error E) {
  assert(E);
  std::string Buf;
  raw_string_ostream OS(Buf);
  logAllUnhandledErrors(std::move(E), OS);
  OS.flush();
  reportError(Buf);
}

[[noreturn]] static void reportError(StringRef File, Error E) {
  assert(E);
  std::string Buf;
  raw_string_ostream OS(Buf);
  logAllUnhandledErrors(std::move(E), OS);
  OS.flush();
  WithColor::error(errs(), ToolName) << "'" << File << "': " << Buf;
  exit(EXIT_FAILURE);
}

namespace {
enum LipoID {
  LIPO_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID_WITH_ID_PREFIX(LIPO_, __VA_ARGS__),
#include "LipoOpts.inc"
#undef OPTION
};

namespace lipo {
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr llvm::StringLiteral NAME##_init[] = VALUE;                  \
  static constexpr llvm::ArrayRef<llvm::StringLiteral> NAME(                   \
      NAME##_init, std::size(NAME##_init) - 1);
#include "LipoOpts.inc"
#undef PREFIX

using namespace llvm::opt;
static constexpr opt::OptTable::Info LipoInfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO_WITH_ID_PREFIX(LIPO_, __VA_ARGS__),
#include "LipoOpts.inc"
#undef OPTION
};
} // namespace lipo

class LipoOptTable : public opt::GenericOptTable {
public:
  LipoOptTable() : opt::GenericOptTable(lipo::LipoInfoTable) {}
};

enum class LipoAction {
  PrintArchs,
  PrintInfo,
  VerifyArch,
  ThinArch,
  ExtractArch,
  CreateUniversal,
  ReplaceArch,
};

struct InputFile {
  std::optional<StringRef> ArchType;
  StringRef FileName;
};

struct Config {
  SmallVector<InputFile, 1> InputFiles;
  SmallVector<std::string, 1> VerifyArchList;
  SmallVector<InputFile, 1> ReplacementFiles;
  StringMap<const uint32_t> SegmentAlignments;
  std::string ArchType;
  std::string OutputFile;
  LipoAction ActionToPerform;
  bool UseFat64;
};

static Slice createSliceFromArchive(LLVMContext &LLVMCtx, const Archive &A) {
  Expected<Slice> ArchiveOrSlice = Slice::create(A, &LLVMCtx);
  if (!ArchiveOrSlice)
    reportError(A.getFileName(), ArchiveOrSlice.takeError());
  return *ArchiveOrSlice;
}

static Slice createSliceFromIR(const IRObjectFile &IRO, unsigned Align) {
  Expected<Slice> IROrErr = Slice::create(IRO, Align);
  if (!IROrErr)
    reportError(IRO.getFileName(), IROrErr.takeError());
  return *IROrErr;
}

} // end namespace

static void validateArchitectureName(StringRef ArchitectureName) {
  if (!MachOObjectFile::isValidArch(ArchitectureName)) {
    std::string Buf;
    raw_string_ostream OS(Buf);
    OS << "Invalid architecture: " << ArchitectureName
       << "\nValid architecture names are:";
    for (auto arch : MachOObjectFile::getValidArchs())
      OS << " " << arch;
    reportError(OS.str());
  }
}

static Config parseLipoOptions(ArrayRef<const char *> ArgsArr) {
  Config C;
  LipoOptTable T;
  unsigned MissingArgumentIndex, MissingArgumentCount;
  opt::InputArgList InputArgs =
      T.ParseArgs(ArgsArr, MissingArgumentIndex, MissingArgumentCount);

  if (MissingArgumentCount)
    reportError("missing argument to " +
                StringRef(InputArgs.getArgString(MissingArgumentIndex)) +
                " option");

  if (InputArgs.size() == 0) {
    // printHelp does not accept Twine.
    T.printHelp(errs(), "llvm-lipo input[s] option[s]", "llvm-lipo");
    exit(EXIT_FAILURE);
  }

  if (InputArgs.hasArg(LIPO_help)) {
    // printHelp does not accept Twine.
    T.printHelp(outs(), "llvm-lipo input[s] option[s]", "llvm-lipo");
    exit(EXIT_SUCCESS);
  }

  if (InputArgs.hasArg(LIPO_version)) {
    outs() << ToolName + "\n";
    cl::PrintVersionMessage();
    exit(EXIT_SUCCESS);
  }

  for (auto *Arg : InputArgs.filtered(LIPO_UNKNOWN))
    reportError("unknown argument '" + Arg->getAsString(InputArgs) + "'");

  for (auto *Arg : InputArgs.filtered(LIPO_INPUT))
    C.InputFiles.push_back({std::nullopt, Arg->getValue()});
  for (auto *Arg : InputArgs.filtered(LIPO_arch)) {
    validateArchitectureName(Arg->getValue(0));
    assert(Arg->getValue(1) && "file_name is missing");
    C.InputFiles.push_back({StringRef(Arg->getValue(0)), Arg->getValue(1)});
  }

  if (C.InputFiles.empty())
    reportError("at least one input file should be specified");

  if (InputArgs.hasArg(LIPO_output))
    C.OutputFile = std::string(InputArgs.getLastArgValue(LIPO_output));

  for (auto *Segalign : InputArgs.filtered(LIPO_segalign)) {
    if (!Segalign->getValue(1))
      reportError("segalign is missing an argument: expects -segalign "
                  "arch_type alignment_value");

    validateArchitectureName(Segalign->getValue(0));

    uint32_t AlignmentValue;
    if (!to_integer<uint32_t>(Segalign->getValue(1), AlignmentValue, 16))
      reportError("argument to -segalign <arch_type> " +
                  Twine(Segalign->getValue(1)) +
                  " (hex) is not a proper hexadecimal number");
    if (!isPowerOf2_32(AlignmentValue))
      reportError("argument to -segalign <arch_type> " +
                  Twine(Segalign->getValue(1)) +
                  " (hex) must be a non-zero power of two");
    if (Log2_32(AlignmentValue) > MachOUniversalBinary::MaxSectionAlignment)
      reportError(
          "argument to -segalign <arch_type> " + Twine(Segalign->getValue(1)) +
          " (hex) must be less than or equal to the maximum section align 2^" +
          Twine(MachOUniversalBinary::MaxSectionAlignment));
    auto Entry = C.SegmentAlignments.try_emplace(Segalign->getValue(0),
                                                 Log2_32(AlignmentValue));
    if (!Entry.second)
      reportError("-segalign " + Twine(Segalign->getValue(0)) +
                  " <alignment_value> specified multiple times: " +
                  Twine(1 << Entry.first->second) + ", " +
                  Twine(AlignmentValue));
  }

  C.UseFat64 = InputArgs.hasArg(LIPO_fat64);

  SmallVector<opt::Arg *, 1> ActionArgs(InputArgs.filtered(LIPO_action_group));
  if (ActionArgs.empty())
    reportError("at least one action should be specified");
  // errors if multiple actions specified other than replace
  // multiple replace flags may be specified, as long as they are not mixed with
  // other action flags
  auto ReplacementArgsRange = InputArgs.filtered(LIPO_replace);
  if (ActionArgs.size() > 1 &&
      ActionArgs.size() !=
          static_cast<size_t>(std::distance(ReplacementArgsRange.begin(),
                                            ReplacementArgsRange.end()))) {
    std::string Buf;
    raw_string_ostream OS(Buf);
    OS << "only one of the following actions can be specified:";
    for (auto *Arg : ActionArgs)
      OS << " " << Arg->getSpelling();
    reportError(OS.str());
  }

  switch (ActionArgs[0]->getOption().getID()) {
  case LIPO_verify_arch:
    for (auto A : InputArgs.getAllArgValues(LIPO_verify_arch))
      C.VerifyArchList.push_back(A);
    if (C.VerifyArchList.empty())
      reportError(
          "verify_arch requires at least one architecture to be specified");
    if (C.InputFiles.size() > 1)
      reportError("verify_arch expects a single input file");
    C.ActionToPerform = LipoAction::VerifyArch;
    return C;

  case LIPO_archs:
    if (C.InputFiles.size() > 1)
      reportError("archs expects a single input file");
    C.ActionToPerform = LipoAction::PrintArchs;
    return C;

  case LIPO_info:
    C.ActionToPerform = LipoAction::PrintInfo;
    return C;

  case LIPO_thin:
    if (C.InputFiles.size() > 1)
      reportError("thin expects a single input file");
    if (C.OutputFile.empty())
      reportError("thin expects a single output file");
    C.ArchType = ActionArgs[0]->getValue();
    validateArchitectureName(C.ArchType);
    C.ActionToPerform = LipoAction::ThinArch;
    return C;

  case LIPO_extract:
    if (C.InputFiles.size() > 1)
      reportError("extract expects a single input file");
    if (C.OutputFile.empty())
      reportError("extract expects a single output file");
    C.ArchType = ActionArgs[0]->getValue();
    validateArchitectureName(C.ArchType);
    C.ActionToPerform = LipoAction::ExtractArch;
    return C;

  case LIPO_create:
    if (C.OutputFile.empty())
      reportError("create expects a single output file to be specified");
    C.ActionToPerform = LipoAction::CreateUniversal;
    return C;

  case LIPO_replace:
    for (auto *Action : ActionArgs) {
      assert(Action->getValue(1) && "file_name is missing");
      validateArchitectureName(Action->getValue(0));
      C.ReplacementFiles.push_back(
          {StringRef(Action->getValue(0)), Action->getValue(1)});
    }

    if (C.OutputFile.empty())
      reportError("replace expects a single output file to be specified");
    if (C.InputFiles.size() > 1)
      reportError("replace expects a single input file");
    C.ActionToPerform = LipoAction::ReplaceArch;
    return C;

  default:
    reportError("llvm-lipo action unspecified");
  }
}

static SmallVector<OwningBinary<Binary>, 1>
readInputBinaries(LLVMContext &LLVMCtx, ArrayRef<InputFile> InputFiles) {
  SmallVector<OwningBinary<Binary>, 1> InputBinaries;
  for (const InputFile &IF : InputFiles) {
    Expected<OwningBinary<Binary>> BinaryOrErr =
        createBinary(IF.FileName, &LLVMCtx);
    if (!BinaryOrErr)
      reportError(IF.FileName, BinaryOrErr.takeError());
    const Binary *B = BinaryOrErr->getBinary();
    if (!B->isArchive() && !B->isMachO() && !B->isMachOUniversalBinary() &&
        !B->isIR())
      reportError("File " + IF.FileName + " has unsupported binary format");
    if (IF.ArchType && (B->isMachO() || B->isArchive() || B->isIR())) {
      const auto S = B->isMachO() ? Slice(*cast<MachOObjectFile>(B))
                     : B->isArchive()
                         ? createSliceFromArchive(LLVMCtx, *cast<Archive>(B))
                         : createSliceFromIR(*cast<IRObjectFile>(B), 0);
      const auto SpecifiedCPUType = MachO::getCPUTypeFromArchitecture(
                                        MachO::getArchitectureFromName(
                                            Triple(*IF.ArchType).getArchName()))
                                        .first;
      // For compatibility with cctools' lipo the comparison is relaxed just to
      // checking cputypes.
      if (S.getCPUType() != SpecifiedCPUType)
        reportError("specified architecture: " + *IF.ArchType +
                    " for file: " + B->getFileName() +
                    " does not match the file's architecture (" +
                    S.getArchString() + ")");
    }
    InputBinaries.push_back(std::move(*BinaryOrErr));
  }
  return InputBinaries;
}

[[noreturn]] static void
verifyArch(ArrayRef<OwningBinary<Binary>> InputBinaries,
           ArrayRef<std::string> VerifyArchList) {
  assert(!VerifyArchList.empty() &&
         "The list of architectures should be non-empty");
  assert(InputBinaries.size() == 1 && "Incorrect number of input binaries");

  for (StringRef Arch : VerifyArchList)
    validateArchitectureName(Arch);

  if (auto UO =
          dyn_cast<MachOUniversalBinary>(InputBinaries.front().getBinary())) {
    for (StringRef Arch : VerifyArchList) {
      Expected<MachOUniversalBinary::ObjectForArch> Obj =
          UO->getObjectForArch(Arch);
      if (!Obj)
        exit(EXIT_FAILURE);
    }
  } else if (auto O =
                 dyn_cast<MachOObjectFile>(InputBinaries.front().getBinary())) {
    const Triple::ArchType ObjectArch = O->getArch();
    for (StringRef Arch : VerifyArchList)
      if (ObjectArch != Triple(Arch).getArch())
        exit(EXIT_FAILURE);
  } else {
    llvm_unreachable("Unexpected binary format");
  }
  exit(EXIT_SUCCESS);
}

static void printBinaryArchs(LLVMContext &LLVMCtx, const Binary *Binary,
                             raw_ostream &OS) {
  // Prints trailing space for compatibility with cctools lipo.
  if (auto UO = dyn_cast<MachOUniversalBinary>(Binary)) {
    for (const auto &O : UO->objects()) {
      // Order here is important, because both MachOObjectFile and
      // IRObjectFile can be created with a binary that has embedded bitcode.
      Expected<std::unique_ptr<MachOObjectFile>> MachOObjOrError =
          O.getAsObjectFile();
      if (MachOObjOrError) {
        OS << Slice(*(MachOObjOrError->get())).getArchString() << " ";
        continue;
      }
      Expected<std::unique_ptr<IRObjectFile>> IROrError =
          O.getAsIRObject(LLVMCtx);
      if (IROrError) {
        consumeError(MachOObjOrError.takeError());
        Expected<Slice> SliceOrErr = Slice::create(**IROrError, O.getAlign());
        if (!SliceOrErr) {
          reportError(Binary->getFileName(), SliceOrErr.takeError());
          continue;
        }
        OS << SliceOrErr.get().getArchString() << " ";
        continue;
      }
      Expected<std::unique_ptr<Archive>> ArchiveOrError = O.getAsArchive();
      if (ArchiveOrError) {
        consumeError(MachOObjOrError.takeError());
        consumeError(IROrError.takeError());
        OS << createSliceFromArchive(LLVMCtx, **ArchiveOrError).getArchString()
           << " ";
        continue;
      }
      consumeError(ArchiveOrError.takeError());
      reportError(Binary->getFileName(), MachOObjOrError.takeError());
      reportError(Binary->getFileName(), IROrError.takeError());
    }
    OS << "\n";
    return;
  }

  if (const auto *MachO = dyn_cast<MachOObjectFile>(Binary)) {
    OS << Slice(*MachO).getArchString() << " \n";
    return;
  }

  // This should be always the case, as this is tested in readInputBinaries
  const auto *IR = cast<IRObjectFile>(Binary);
  Expected<Slice> SliceOrErr = createSliceFromIR(*IR, 0);
  if (!SliceOrErr)
    reportError(IR->getFileName(), SliceOrErr.takeError());

  OS << SliceOrErr->getArchString() << " \n";
}

[[noreturn]] static void
printArchs(LLVMContext &LLVMCtx, ArrayRef<OwningBinary<Binary>> InputBinaries) {
  assert(InputBinaries.size() == 1 && "Incorrect number of input binaries");
  printBinaryArchs(LLVMCtx, InputBinaries.front().getBinary(), outs());
  exit(EXIT_SUCCESS);
}

[[noreturn]] static void
printInfo(LLVMContext &LLVMCtx, ArrayRef<OwningBinary<Binary>> InputBinaries) {
  // Group universal and thin files together for compatibility with cctools lipo
  for (auto &IB : InputBinaries) {
    const Binary *Binary = IB.getBinary();
    if (Binary->isMachOUniversalBinary()) {
      outs() << "Architectures in the fat file: " << Binary->getFileName()
             << " are: ";
      printBinaryArchs(LLVMCtx, Binary, outs());
    }
  }
  for (auto &IB : InputBinaries) {
    const Binary *Binary = IB.getBinary();
    if (!Binary->isMachOUniversalBinary()) {
      assert(Binary->isMachO() && "expected MachO binary");
      outs() << "Non-fat file: " << Binary->getFileName()
             << " is architecture: ";
      printBinaryArchs(LLVMCtx, Binary, outs());
    }
  }
  exit(EXIT_SUCCESS);
}

[[noreturn]] static void thinSlice(LLVMContext &LLVMCtx,
                                   ArrayRef<OwningBinary<Binary>> InputBinaries,
                                   StringRef ArchType,
                                   StringRef OutputFileName) {
  assert(!ArchType.empty() && "The architecture type should be non-empty");
  assert(InputBinaries.size() == 1 && "Incorrect number of input binaries");
  assert(!OutputFileName.empty() && "Thin expects a single output file");

  if (InputBinaries.front().getBinary()->isMachO()) {
    reportError("input file " +
                InputBinaries.front().getBinary()->getFileName() +
                " must be a fat file when the -thin option is specified");
    exit(EXIT_FAILURE);
  }

  auto *UO = cast<MachOUniversalBinary>(InputBinaries.front().getBinary());
  Expected<std::unique_ptr<MachOObjectFile>> Obj =
      UO->getMachOObjectForArch(ArchType);
  Expected<std::unique_ptr<IRObjectFile>> IRObj =
      UO->getIRObjectForArch(ArchType, LLVMCtx);
  Expected<std::unique_ptr<Archive>> Ar = UO->getArchiveForArch(ArchType);
  if (!Obj && !IRObj && !Ar)
    reportError("fat input file " + UO->getFileName() +
                " does not contain the specified architecture " + ArchType +
                " to thin it to");
  Binary *B;
  // Order here is important, because both Obj and IRObj will be valid with a
  // binary that has embedded bitcode.
  if (Obj)
    B = Obj->get();
  else if (IRObj)
    B = IRObj->get();
  else
    B = Ar->get();

  Expected<std::unique_ptr<FileOutputBuffer>> OutFileOrError =
      FileOutputBuffer::create(OutputFileName,
                               B->getMemoryBufferRef().getBufferSize(),
                               sys::fs::can_execute(UO->getFileName())
                                   ? FileOutputBuffer::F_executable
                                   : 0);
  if (!OutFileOrError)
    reportError(OutputFileName, OutFileOrError.takeError());
  std::copy(B->getMemoryBufferRef().getBufferStart(),
            B->getMemoryBufferRef().getBufferEnd(),
            OutFileOrError.get()->getBufferStart());
  if (Error E = OutFileOrError.get()->commit())
    reportError(OutputFileName, std::move(E));
  exit(EXIT_SUCCESS);
}

static void checkArchDuplicates(ArrayRef<Slice> Slices) {
  DenseMap<uint64_t, const Binary *> CPUIds;
  for (const auto &S : Slices) {
    auto Entry = CPUIds.try_emplace(S.getCPUID(), S.getBinary());
    if (!Entry.second)
      reportError(Entry.first->second->getFileName() + " and " +
                  S.getBinary()->getFileName() +
                  " have the same architecture " + S.getArchString() +
                  " and therefore cannot be in the same universal binary");
  }
}

template <typename Range>
static void updateAlignments(Range &Slices,
                             const StringMap<const uint32_t> &Alignments) {
  for (auto &Slice : Slices) {
    auto Alignment = Alignments.find(Slice.getArchString());
    if (Alignment != Alignments.end())
      Slice.setP2Alignment(Alignment->second);
  }
}

static void checkUnusedAlignments(ArrayRef<Slice> Slices,
                                  const StringMap<const uint32_t> &Alignments) {
  auto HasArch = [&](StringRef Arch) {
    return llvm::any_of(Slices,
                        [Arch](Slice S) { return S.getArchString() == Arch; });
  };
  for (StringRef Arch : Alignments.keys())
    if (!HasArch(Arch))
      reportError("-segalign " + Arch +
                  " <value> specified but resulting fat file does not contain "
                  "that architecture ");
}

// Updates vector ExtractedObjects with the MachOObjectFiles extracted from
// Universal Binary files to transfer ownership.
static SmallVector<Slice, 2>
buildSlices(LLVMContext &LLVMCtx, ArrayRef<OwningBinary<Binary>> InputBinaries,
            const StringMap<const uint32_t> &Alignments,
            SmallVectorImpl<std::unique_ptr<SymbolicFile>> &ExtractedObjects) {
  SmallVector<Slice, 2> Slices;
  for (auto &IB : InputBinaries) {
    const Binary *InputBinary = IB.getBinary();
    if (auto UO = dyn_cast<MachOUniversalBinary>(InputBinary)) {
      for (const auto &O : UO->objects()) {
        // Order here is important, because both MachOObjectFile and
        // IRObjectFile can be created with a binary that has embedded bitcode.
        Expected<std::unique_ptr<MachOObjectFile>> BinaryOrError =
            O.getAsObjectFile();
        if (BinaryOrError) {
          Slices.emplace_back(*(BinaryOrError.get()), O.getAlign());
          ExtractedObjects.push_back(std::move(BinaryOrError.get()));
          continue;
        }
        Expected<std::unique_ptr<IRObjectFile>> IROrError =
            O.getAsIRObject(LLVMCtx);
        if (IROrError) {
          consumeError(BinaryOrError.takeError());
          Slice S = createSliceFromIR(**IROrError, O.getAlign());
          ExtractedObjects.emplace_back(std::move(IROrError.get()));
          Slices.emplace_back(std::move(S));
          continue;
        }
        reportError(InputBinary->getFileName(), BinaryOrError.takeError());
      }
    } else if (const auto *O = dyn_cast<MachOObjectFile>(InputBinary)) {
      Slices.emplace_back(*O);
    } else if (const auto *A = dyn_cast<Archive>(InputBinary)) {
      Slices.push_back(createSliceFromArchive(LLVMCtx, *A));
    } else if (const auto *IRO = dyn_cast<IRObjectFile>(InputBinary)) {
      // Original Apple's lipo set the alignment to 0
      Expected<Slice> SliceOrErr = Slice::create(*IRO, 0);
      if (!SliceOrErr) {
        reportError(InputBinary->getFileName(), SliceOrErr.takeError());
        continue;
      }
      Slices.emplace_back(std::move(SliceOrErr.get()));
    } else {
      llvm_unreachable("Unexpected binary format");
    }
  }
  updateAlignments(Slices, Alignments);
  return Slices;
}

[[noreturn]] static void
createUniversalBinary(LLVMContext &LLVMCtx,
                      ArrayRef<OwningBinary<Binary>> InputBinaries,
                      const StringMap<const uint32_t> &Alignments,
                      StringRef OutputFileName, FatHeaderType HeaderType) {
  assert(InputBinaries.size() >= 1 && "Incorrect number of input binaries");
  assert(!OutputFileName.empty() && "Create expects a single output file");

  SmallVector<std::unique_ptr<SymbolicFile>, 1> ExtractedObjects;
  SmallVector<Slice, 1> Slices =
      buildSlices(LLVMCtx, InputBinaries, Alignments, ExtractedObjects);
  checkArchDuplicates(Slices);
  checkUnusedAlignments(Slices, Alignments);

  llvm::stable_sort(Slices);
  if (Error E = writeUniversalBinary(Slices, OutputFileName, HeaderType))
    reportError(std::move(E));

  exit(EXIT_SUCCESS);
}

[[noreturn]] static void
extractSlice(LLVMContext &LLVMCtx, ArrayRef<OwningBinary<Binary>> InputBinaries,
             const StringMap<const uint32_t> &Alignments, StringRef ArchType,
             StringRef OutputFileName) {
  assert(!ArchType.empty() &&
         "The architecture type should be non-empty");
  assert(InputBinaries.size() == 1 && "Incorrect number of input binaries");
  assert(!OutputFileName.empty() && "Thin expects a single output file");

  if (InputBinaries.front().getBinary()->isMachO()) {
    reportError("input file " +
                InputBinaries.front().getBinary()->getFileName() +
                " must be a fat file when the -extract option is specified");
  }

  SmallVector<std::unique_ptr<SymbolicFile>, 2> ExtractedObjects;
  SmallVector<Slice, 2> Slices =
      buildSlices(LLVMCtx, InputBinaries, Alignments, ExtractedObjects);
  erase_if(Slices, [ArchType](const Slice &S) {
    return ArchType != S.getArchString();
  });

  if (Slices.empty())
    reportError(
        "fat input file " + InputBinaries.front().getBinary()->getFileName() +
        " does not contain the specified architecture " + ArchType);

  llvm::stable_sort(Slices);
  if (Error E = writeUniversalBinary(Slices, OutputFileName))
    reportError(std::move(E));
  exit(EXIT_SUCCESS);
}

static StringMap<Slice>
buildReplacementSlices(ArrayRef<OwningBinary<Binary>> ReplacementBinaries,
                       const StringMap<const uint32_t> &Alignments) {
  StringMap<Slice> Slices;
  // populates StringMap of slices to replace with; error checks for mismatched
  // replace flag args, fat files, and duplicate arch_types
  for (const auto &OB : ReplacementBinaries) {
    const Binary *ReplacementBinary = OB.getBinary();
    auto O = dyn_cast<MachOObjectFile>(ReplacementBinary);
    if (!O)
      reportError("replacement file: " + ReplacementBinary->getFileName() +
                  " is a fat file (must be a thin file)");
    Slice S(*O);
    auto Entry = Slices.try_emplace(S.getArchString(), S);
    if (!Entry.second)
      reportError("-replace " + S.getArchString() +
                  " <file_name> specified multiple times: " +
                  Entry.first->second.getBinary()->getFileName() + ", " +
                  O->getFileName());
  }
  auto SlicesMapRange = map_range(
      Slices, [](StringMapEntry<Slice> &E) -> Slice & { return E.getValue(); });
  updateAlignments(SlicesMapRange, Alignments);
  return Slices;
}

[[noreturn]] static void
replaceSlices(LLVMContext &LLVMCtx,
              ArrayRef<OwningBinary<Binary>> InputBinaries,
              const StringMap<const uint32_t> &Alignments,
              StringRef OutputFileName, ArrayRef<InputFile> ReplacementFiles) {
  assert(InputBinaries.size() == 1 && "Incorrect number of input binaries");
  assert(!OutputFileName.empty() && "Replace expects a single output file");

  if (InputBinaries.front().getBinary()->isMachO())
    reportError("input file " +
                InputBinaries.front().getBinary()->getFileName() +
                " must be a fat file when the -replace option is specified");

  SmallVector<OwningBinary<Binary>, 1> ReplacementBinaries =
      readInputBinaries(LLVMCtx, ReplacementFiles);

  StringMap<Slice> ReplacementSlices =
      buildReplacementSlices(ReplacementBinaries, Alignments);
  SmallVector<std::unique_ptr<SymbolicFile>, 2> ExtractedObjects;
  SmallVector<Slice, 2> Slices =
      buildSlices(LLVMCtx, InputBinaries, Alignments, ExtractedObjects);

  for (auto &Slice : Slices) {
    auto It = ReplacementSlices.find(Slice.getArchString());
    if (It != ReplacementSlices.end()) {
      Slice = It->second;
      ReplacementSlices.erase(It); // only keep remaining replacing arch_types
    }
  }

  if (!ReplacementSlices.empty())
    reportError("-replace " + ReplacementSlices.begin()->first() +
                " <file_name> specified but fat file: " +
                InputBinaries.front().getBinary()->getFileName() +
                " does not contain that architecture");

  checkUnusedAlignments(Slices, Alignments);

  llvm::stable_sort(Slices);
  if (Error E = writeUniversalBinary(Slices, OutputFileName))
    reportError(std::move(E));
  exit(EXIT_SUCCESS);
}

int llvm_lipo_main(int argc, char **argv, const llvm::ToolContext &) {
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();

  Config C = parseLipoOptions(ArrayRef(argv + 1, argc - 1));
  LLVMContext LLVMCtx;
  SmallVector<OwningBinary<Binary>, 1> InputBinaries =
      readInputBinaries(LLVMCtx, C.InputFiles);

  switch (C.ActionToPerform) {
  case LipoAction::VerifyArch:
    verifyArch(InputBinaries, C.VerifyArchList);
    break;
  case LipoAction::PrintArchs:
    printArchs(LLVMCtx, InputBinaries);
    break;
  case LipoAction::PrintInfo:
    printInfo(LLVMCtx, InputBinaries);
    break;
  case LipoAction::ThinArch:
    thinSlice(LLVMCtx, InputBinaries, C.ArchType, C.OutputFile);
    break;
  case LipoAction::ExtractArch:
    extractSlice(LLVMCtx, InputBinaries, C.SegmentAlignments, C.ArchType,
                 C.OutputFile);
    break;
  case LipoAction::CreateUniversal:
    createUniversalBinary(
        LLVMCtx, InputBinaries, C.SegmentAlignments, C.OutputFile,
        C.UseFat64 ? FatHeaderType::Fat64Header : FatHeaderType::FatHeader);
    break;
  case LipoAction::ReplaceArch:
    replaceSlices(LLVMCtx, InputBinaries, C.SegmentAlignments, C.OutputFile,
                  C.ReplacementFiles);
    break;
  }
  return EXIT_SUCCESS;
}
