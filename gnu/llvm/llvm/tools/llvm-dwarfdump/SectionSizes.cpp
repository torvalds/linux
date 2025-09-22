//===-- SectionSizes.cpp - Debug section sizes ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm-dwarfdump.h"

#define DEBUG_TYPE "dwarfdump"

using namespace llvm;
using namespace llvm::dwarfdump;
using namespace llvm::object;

static size_t getNameColumnWidth(const SectionSizes &Sizes,
                                 const StringRef SectionNameTitle) {
  // The minimum column width should be the size of "SECTION".
  size_t Width = SectionNameTitle.size();
  for (const auto &It : Sizes.DebugSectionSizes)
    Width = std::max(Width, It.first.size());
  return Width;
}

static size_t getSizeColumnWidth(const SectionSizes &Sizes,
                                 const StringRef SectionSizeTitle) {
  // The minimum column width should be the size of the column title.
  size_t Width = SectionSizeTitle.size();
  for (const auto &It : Sizes.DebugSectionSizes) {
    size_t NumWidth = std::to_string(It.second).size();
    Width = std::max(Width, NumWidth);
  }
  return Width;
}

static void prettyPrintSectionSizes(const ObjectFile &Obj,
                                    const SectionSizes &Sizes,
                                    raw_ostream &OS) {
  const StringRef SectionNameTitle = "SECTION";
  const StringRef SectionSizeTitle = "SIZE (b)";

  size_t NameColWidth = getNameColumnWidth(Sizes, SectionNameTitle);
  size_t SizeColWidth = getSizeColumnWidth(Sizes, SectionSizeTitle);

  OS << "----------------------------------------------------" << '\n';
  OS << SectionNameTitle;
  size_t SectionNameTitleWidth = SectionNameTitle.size();
  for (unsigned i = 0; i < (NameColWidth - SectionNameTitleWidth) + 2; i++)
    OS << " ";
  OS << SectionSizeTitle << '\n';
  for (unsigned i = 0; i < NameColWidth; i++)
    OS << "-";
  OS << "  ";

  for (unsigned i = 0; i < SizeColWidth; i++)
    OS << "-";
  OS << '\n';

  for (const auto &It : Sizes.DebugSectionSizes) {
    OS << left_justify(It.first, NameColWidth) << "  ";

    std::string NumBytes = std::to_string(It.second);
    OS << right_justify(NumBytes, SizeColWidth) << " ("
       << format("%0.2f",
                 It.second / static_cast<double>(Sizes.TotalObjectSize) * 100)
       << "%)\n";
  }

  OS << '\n';
  OS << " Total Size: " << Sizes.TotalDebugSectionsSize << "  ("
     << format("%0.2f", Sizes.TotalDebugSectionsSize /
                            static_cast<double>(Sizes.TotalObjectSize) * 100)
     << "%)\n";
  OS << " Total File Size: " << Sizes.TotalObjectSize << '\n';
  OS << "----------------------------------------------------" << '\n';
}

void dwarfdump::calculateSectionSizes(const ObjectFile &Obj,
                                      SectionSizes &Sizes,
                                      const Twine &Filename) {
  // Get total size.
  Sizes.TotalObjectSize = Obj.getData().size();

  for (const SectionRef &Section : Obj.sections()) {
    StringRef SectionName;
    if (Expected<StringRef> NameOrErr = Section.getName())
      SectionName = *NameOrErr;
    else
      WithColor::defaultWarningHandler(
          createFileError(Filename, NameOrErr.takeError()));

    LLVM_DEBUG(dbgs() << SectionName.str() << ": " << Section.getSize()
                      << '\n');

    if (!Section.isDebugSection())
      continue;

    Sizes.TotalDebugSectionsSize += Section.getSize();
    Sizes.DebugSectionSizes[std::string(SectionName)] += Section.getSize();
  }
}

bool dwarfdump::collectObjectSectionSizes(ObjectFile &Obj,
                                          DWARFContext & /*DICtx*/,
                                          const Twine &Filename,
                                          raw_ostream &OS) {
  SectionSizes Sizes;

  // Get the section sizes.
  calculateSectionSizes(Obj, Sizes, Filename);

  OS << "----------------------------------------------------\n";
  OS << "file: " << Filename.str() << '\n';

  prettyPrintSectionSizes(Obj, Sizes, OS);

  // TODO: If the input file is an archive, print the cumulative summary of all
  // files from the archive.

  return true;
}
