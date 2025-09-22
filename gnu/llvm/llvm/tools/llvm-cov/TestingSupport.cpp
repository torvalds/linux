//===- TestingSupport.cpp - Convert objects files into test files --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/ProfileData/Coverage/CoverageMappingWriter.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <system_error>

using namespace llvm;
using namespace object;

int convertForTestingMain(int argc, const char *argv[]) {
  cl::opt<std::string> InputSourceFile(cl::Positional, cl::Required,
                                       cl::desc("<Source file>"));

  cl::opt<std::string> OutputFilename(
      "o", cl::Required,
      cl::desc(
          "File with the profile data obtained after an instrumented run"));

  cl::ParseCommandLineOptions(argc, argv, "LLVM code coverage tool\n");

  auto ObjErr = llvm::object::ObjectFile::createObjectFile(InputSourceFile);
  if (!ObjErr) {
    std::string Buf;
    raw_string_ostream OS(Buf);
    logAllUnhandledErrors(ObjErr.takeError(), OS);
    OS.flush();
    errs() << "error: " << Buf;
    return 1;
  }
  ObjectFile *OF = ObjErr.get().getBinary();
  auto BytesInAddress = OF->getBytesInAddress();
  if (BytesInAddress != 8) {
    errs() << "error: 64 bit binary expected\n";
    return 1;
  }

  // Look for the sections that we are interested in.
  int FoundSectionCount = 0;
  SectionRef ProfileNames, CoverageMapping, CoverageRecords;
  auto ObjFormat = OF->getTripleObjectFormat();

  auto ProfileNamesSection = getInstrProfSectionName(IPSK_name, ObjFormat,
                                                     /*AddSegmentInfo=*/false);
  auto CoverageMappingSection =
      getInstrProfSectionName(IPSK_covmap, ObjFormat, /*AddSegmentInfo=*/false);
  auto CoverageRecordsSection =
      getInstrProfSectionName(IPSK_covfun, ObjFormat, /*AddSegmentInfo=*/false);
  if (isa<object::COFFObjectFile>(OF)) {
    // On COFF, the object file section name may end in "$M". This tells the
    // linker to sort these sections between "$A" and "$Z". The linker removes
    // the dollar and everything after it in the final binary. Do the same to
    // match.
    auto Strip = [](std::string &Str) {
      auto Pos = Str.find('$');
      if (Pos != std::string::npos)
        Str.resize(Pos);
    };
    Strip(ProfileNamesSection);
    Strip(CoverageMappingSection);
    Strip(CoverageRecordsSection);
  }

  for (const auto &Section : OF->sections()) {
    StringRef Name;
    if (Expected<StringRef> NameOrErr = Section.getName()) {
      Name = *NameOrErr;
    } else {
      consumeError(NameOrErr.takeError());
      return 1;
    }

    if (Name == ProfileNamesSection)
      ProfileNames = Section;
    else if (Name == CoverageMappingSection)
      CoverageMapping = Section;
    else if (Name == CoverageRecordsSection)
      CoverageRecords = Section;
    else
      continue;
    ++FoundSectionCount;
  }
  if (FoundSectionCount != 3)
    return 1;

  // Get the contents of the given sections.
  uint64_t ProfileNamesAddress = ProfileNames.getAddress();
  StringRef CoverageMappingData;
  StringRef CoverageRecordsData;
  StringRef ProfileNamesData;
  if (Expected<StringRef> E = CoverageMapping.getContents())
    CoverageMappingData = *E;
  else {
    consumeError(E.takeError());
    return 1;
  }
  if (Expected<StringRef> E = CoverageRecords.getContents())
    CoverageRecordsData = *E;
  else {
    consumeError(E.takeError());
    return 1;
  }
  if (Expected<StringRef> E = ProfileNames.getContents())
    ProfileNamesData = *E;
  else {
    consumeError(E.takeError());
    return 1;
  }

  // If this is a linked PE/COFF file, then we have to skip over the null byte
  // that is allocated in the .lprfn$A section in the LLVM profiling runtime.
  if (isa<COFFObjectFile>(OF) && !OF->isRelocatableObject())
    ProfileNamesData = ProfileNamesData.drop_front(1);

  int FD;
  if (auto Err = sys::fs::openFileForWrite(OutputFilename, FD)) {
    errs() << "error: " << Err.message() << "\n";
    return 1;
  }

  coverage::TestingFormatWriter Writer(ProfileNamesAddress, ProfileNamesData,
                                       CoverageMappingData,
                                       CoverageRecordsData);
  raw_fd_ostream OS(FD, true);
  Writer.write(OS);

  return 0;
}
