//===- DWARF.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lld/Common/DWARF.h"
#include "lld/Common/ErrorHandler.h"

using namespace llvm;

namespace lld {

DWARFCache::DWARFCache(std::unique_ptr<llvm::DWARFContext> d)
    : dwarf(std::move(d)) {
  for (std::unique_ptr<DWARFUnit> &cu : dwarf->compile_units()) {
    auto report = [](Error err) {
      handleAllErrors(std::move(err),
                      [](ErrorInfoBase &info) { warn(info.message()); });
    };
    Expected<const DWARFDebugLine::LineTable *> expectedLT =
        dwarf->getLineTableForUnit(cu.get(), report);
    const DWARFDebugLine::LineTable *lt = nullptr;
    if (expectedLT)
      lt = *expectedLT;
    else
      report(expectedLT.takeError());
    if (!lt)
      continue;
    lineTables.push_back(lt);

    // Loop over variable records and insert them to variableLoc.
    for (const auto &entry : cu->dies()) {
      DWARFDie die(cu.get(), &entry);
      // Skip all tags that are not variables.
      if (die.getTag() != dwarf::DW_TAG_variable)
        continue;

      // Skip if a local variable because we don't need them for generating
      // error messages. In general, only non-local symbols can fail to be
      // linked.
      if (!dwarf::toUnsigned(die.find(dwarf::DW_AT_external), 0))
        continue;

      // Get the source filename index for the variable.
      unsigned file = dwarf::toUnsigned(die.find(dwarf::DW_AT_decl_file), 0);
      if (!lt->hasFileAtIndex(file))
        continue;

      // Get the line number on which the variable is declared.
      unsigned line = dwarf::toUnsigned(die.find(dwarf::DW_AT_decl_line), 0);

      // Here we want to take the variable name to add it into variableLoc.
      // Variable can have regular and linkage name associated. At first, we try
      // to get linkage name as it can be different, for example when we have
      // two variables in different namespaces of the same object. Use common
      // name otherwise, but handle the case when it also absent in case if the
      // input object file lacks some debug info.
      StringRef name =
          dwarf::toString(die.find(dwarf::DW_AT_linkage_name),
                          dwarf::toString(die.find(dwarf::DW_AT_name), ""));
      if (!name.empty())
        variableLoc.insert({name, {lt, file, line}});
    }
  }
}

// Returns the pair of file name and line number describing location of data
// object (variable, array, etc) definition.
std::optional<std::pair<std::string, unsigned>>
DWARFCache::getVariableLoc(StringRef name) {
  // Return if we have no debug information about data object.
  auto it = variableLoc.find(name);
  if (it == variableLoc.end())
    return std::nullopt;

  // Take file name string from line table.
  std::string fileName;
  if (!it->second.lt->getFileNameByIndex(
          it->second.file, {},
          DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, fileName))
    return std::nullopt;

  return std::make_pair(fileName, it->second.line);
}

// Returns source line information for a given offset
// using DWARF debug info.
std::optional<DILineInfo> DWARFCache::getDILineInfo(uint64_t offset,
                                                    uint64_t sectionIndex) {
  DILineInfo info;
  for (const llvm::DWARFDebugLine::LineTable *lt : lineTables) {
    if (lt->getFileLineInfoForAddress(
            {offset, sectionIndex}, nullptr,
            DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, info))
      return info;
  }
  return std::nullopt;
}

} // namespace lld
