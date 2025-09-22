//===- RemarkCounter.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic tool to count remarks based on properties
//
//===----------------------------------------------------------------------===//

#include "RemarkCounter.h"
#include "RemarkUtilRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Regex.h"

using namespace llvm;
using namespace remarks;
using namespace llvm::remarkutil;

static cl::SubCommand CountSub("count",
                               "Collect remarks based on specified criteria.");

INPUT_FORMAT_COMMAND_LINE_OPTIONS(CountSub)
INPUT_OUTPUT_COMMAND_LINE_OPTIONS(CountSub)

static cl::list<std::string>
    Keys("args", cl::desc("Specify remark argument/s to count by."),
         cl::value_desc("arguments"), cl::sub(CountSub), cl::ValueOptional);
static cl::list<std::string> RKeys(
    "rargs",
    cl::desc(
        "Specify remark argument/s to count (accepts regular expressions)."),
    cl::value_desc("arguments"), cl::sub(CountSub), cl::ValueOptional);
static cl::opt<std::string>
    RemarkNameOpt("remark-name",
                  cl::desc("Optional remark name to filter collection by."),
                  cl::ValueOptional, cl::sub(CountSub));
static cl::opt<std::string>
    PassNameOpt("pass-name", cl::ValueOptional,
                cl::desc("Optional remark pass name to filter collection by."),
                cl::sub(CountSub));

static cl::opt<std::string> RemarkFilterArgByOpt(
    "filter-arg-by", cl::desc("Optional remark arg to filter collection by."),
    cl::ValueOptional, cl::sub(CountSub));
static cl::opt<std::string>
    RemarkNameOptRE("rremark-name",
                    cl::desc("Optional remark name to filter collection by "
                             "(accepts regular expressions)."),
                    cl::ValueOptional, cl::sub(CountSub));
static cl::opt<std::string>
    RemarkArgFilterOptRE("rfilter-arg-by",
                         cl::desc("Optional remark arg to filter collection by "
                                  "(accepts regular expressions)."),
                         cl::sub(CountSub), cl::ValueOptional);
static cl::opt<std::string>
    PassNameOptRE("rpass-name", cl::ValueOptional,
                  cl::desc("Optional remark pass name to filter collection "
                           "by (accepts regular expressions)."),
                  cl::sub(CountSub));
static cl::opt<Type> RemarkTypeOpt(
    "remark-type", cl::desc("Optional remark type to filter collection by."),
    cl::values(clEnumValN(Type::Unknown, "unknown", "UNKOWN"),
               clEnumValN(Type::Passed, "passed", "PASSED"),
               clEnumValN(Type::Missed, "missed", "MISSED"),
               clEnumValN(Type::Analysis, "analysis", "ANALYSIS"),
               clEnumValN(Type::AnalysisFPCommute, "analysis-fp-commute",
                          "ANALYSIS_FP_COMMUTE"),
               clEnumValN(Type::AnalysisAliasing, "analysis-aliasing",
                          "ANALYSIS_ALIASING"),
               clEnumValN(Type::Failure, "failure", "FAILURE")),
    cl::init(Type::Failure), cl::sub(CountSub));
static cl::opt<CountBy> CountByOpt(
    "count-by", cl::desc("Specify the property to collect remarks by."),
    cl::values(
        clEnumValN(CountBy::REMARK, "remark-name",
                   "Counts individual remarks based on how many of the remark "
                   "exists."),
        clEnumValN(CountBy::ARGUMENT, "arg",
                   "Counts based on the value each specified argument has. The "
                   "argument has to have a number value to be considered.")),
    cl::init(CountBy::REMARK), cl::sub(CountSub));
static cl::opt<GroupBy> GroupByOpt(
    "group-by", cl::desc("Specify the property to group remarks by."),
    cl::values(
        clEnumValN(
            GroupBy::PER_SOURCE, "source",
            "Display the count broken down by the filepath of each remark "
            "emitted. Requires remarks to have DebugLoc information."),
        clEnumValN(GroupBy::PER_FUNCTION, "function",
                   "Breakdown the count by function name."),
        clEnumValN(
            GroupBy::PER_FUNCTION_WITH_DEBUG_LOC, "function-with-loc",
            "Breakdown the count by function name taking into consideration "
            "the filepath info from the DebugLoc of the remark."),
        clEnumValN(GroupBy::TOTAL, "total",
                   "Output the total number corresponding to the count for the "
                   "provided input file.")),
    cl::init(GroupBy::PER_SOURCE), cl::sub(CountSub));

/// Look for matching argument with \p Key in \p Remark and return the parsed
/// integer value or 0 if it is has no integer value.
static unsigned getValForKey(StringRef Key, const Remark &Remark) {
  auto *RemarkArg = find_if(Remark.Args, [&Key](const Argument &Arg) {
    return Arg.Key == Key && Arg.isValInt();
  });
  if (RemarkArg == Remark.Args.end())
    return 0;
  return *RemarkArg->getValAsInt();
}

Error Filters::regexArgumentsValid() {
  if (RemarkNameFilter && RemarkNameFilter->IsRegex)
    if (auto E = checkRegex(RemarkNameFilter->FilterRE))
      return E;
  if (PassNameFilter && PassNameFilter->IsRegex)
    if (auto E = checkRegex(PassNameFilter->FilterRE))
      return E;
  if (ArgFilter && ArgFilter->IsRegex)
    if (auto E = checkRegex(ArgFilter->FilterRE))
      return E;
  return Error::success();
}

bool Filters::filterRemark(const Remark &Remark) {
  if (RemarkNameFilter && !RemarkNameFilter->match(Remark.RemarkName))
    return false;
  if (PassNameFilter && !PassNameFilter->match(Remark.PassName))
    return false;
  if (RemarkTypeFilter)
    return *RemarkTypeFilter == Remark.RemarkType;
  if (ArgFilter) {
    if (!any_of(Remark.Args,
                [this](Argument Arg) { return ArgFilter->match(Arg.Val); }))
      return false;
  }
  return true;
}

Error ArgumentCounter::getAllMatchingArgumentsInRemark(
    StringRef Buffer, ArrayRef<FilterMatcher> Arguments, Filters &Filter) {
  auto MaybeParser = createRemarkParser(InputFormat, Buffer);
  if (!MaybeParser)
    return MaybeParser.takeError();
  auto &Parser = **MaybeParser;
  auto MaybeRemark = Parser.next();
  for (; MaybeRemark; MaybeRemark = Parser.next()) {
    auto &Remark = **MaybeRemark;
    // Only collect keys from remarks included in the filter.
    if (!Filter.filterRemark(Remark))
      continue;
    for (auto &Key : Arguments) {
      for (Argument Arg : Remark.Args)
        if (Key.match(Arg.Key) && Arg.isValInt())
          ArgumentSetIdxMap.insert({Arg.Key, ArgumentSetIdxMap.size()});
    }
  }

  auto E = MaybeRemark.takeError();
  if (!E.isA<EndOfFileError>())
    return E;
  consumeError(std::move(E));
  return Error::success();
}

std::optional<std::string> Counter::getGroupByKey(const Remark &Remark) {
  switch (Group) {
  case GroupBy::PER_FUNCTION:
    return Remark.FunctionName.str();
  case GroupBy::TOTAL:
    return "Total";
  case GroupBy::PER_SOURCE:
  case GroupBy::PER_FUNCTION_WITH_DEBUG_LOC:
    if (!Remark.Loc.has_value())
      return std::nullopt;

    if (Group == GroupBy::PER_FUNCTION_WITH_DEBUG_LOC)
      return Remark.Loc->SourceFilePath.str() + ":" + Remark.FunctionName.str();
    return Remark.Loc->SourceFilePath.str();
  }
  llvm_unreachable("Fully covered switch above!");
}

void ArgumentCounter::collect(const Remark &Remark) {
  SmallVector<unsigned, 4> Row(ArgumentSetIdxMap.size());
  std::optional<std::string> GroupByKey = getGroupByKey(Remark);
  // Early return if we don't have a value
  if (!GroupByKey)
    return;
  auto GroupVal = *GroupByKey;
  CountByKeysMap.insert({GroupVal, Row});
  for (auto [Key, Idx] : ArgumentSetIdxMap) {
    auto Count = getValForKey(Key, Remark);
    CountByKeysMap[GroupVal][Idx] += Count;
  }
}

void RemarkCounter::collect(const Remark &Remark) {
  std::optional<std::string> Key = getGroupByKey(Remark);
  if (!Key.has_value())
    return;
  auto Iter = CountedByRemarksMap.insert({*Key, 1});
  if (!Iter.second)
    Iter.first->second += 1;
}

Error ArgumentCounter::print(StringRef OutputFileName) {
  auto MaybeOF =
      getOutputFileWithFlags(OutputFileName, sys::fs::OF_TextWithCRLF);
  if (!MaybeOF)
    return MaybeOF.takeError();

  auto OF = std::move(*MaybeOF);
  OF->os() << groupByToStr(Group) << ",";
  unsigned Idx = 0;
  for (auto [Key, _] : ArgumentSetIdxMap) {
    OF->os() << Key;
    if (Idx != ArgumentSetIdxMap.size() - 1)
      OF->os() << ",";
    Idx++;
  }
  OF->os() << "\n";
  for (auto [Header, CountVector] : CountByKeysMap) {
    OF->os() << Header << ",";
    unsigned Idx = 0;
    for (auto Count : CountVector) {
      OF->os() << Count;
      if (Idx != ArgumentSetIdxMap.size() - 1)
        OF->os() << ",";
      Idx++;
    }
    OF->os() << "\n";
  }
  return Error::success();
}

Error RemarkCounter::print(StringRef OutputFileName) {
  auto MaybeOF =
      getOutputFileWithFlags(OutputFileName, sys::fs::OF_TextWithCRLF);
  if (!MaybeOF)
    return MaybeOF.takeError();

  auto OF = std::move(*MaybeOF);
  OF->os() << groupByToStr(Group) << ","
           << "Count\n";
  for (auto [Key, Count] : CountedByRemarksMap)
    OF->os() << Key << "," << Count << "\n";
  OF->keep();
  return Error::success();
}

Expected<Filters> getRemarkFilter() {
  // Create Filter properties.
  std::optional<FilterMatcher> RemarkNameFilter;
  std::optional<FilterMatcher> PassNameFilter;
  std::optional<FilterMatcher> RemarkArgFilter;
  std::optional<Type> RemarkType;
  if (!RemarkNameOpt.empty())
    RemarkNameFilter = {RemarkNameOpt, false};
  else if (!RemarkNameOptRE.empty())
    RemarkNameFilter = {RemarkNameOptRE, true};
  if (!PassNameOpt.empty())
    PassNameFilter = {PassNameOpt, false};
  else if (!PassNameOptRE.empty())
    PassNameFilter = {PassNameOptRE, true};
  if (RemarkTypeOpt != Type::Failure)
    RemarkType = RemarkTypeOpt;
  if (!RemarkFilterArgByOpt.empty())
    RemarkArgFilter = {RemarkFilterArgByOpt, false};
  else if (!RemarkArgFilterOptRE.empty())
    RemarkArgFilter = {RemarkArgFilterOptRE, true};
  // Create RemarkFilter.
  return Filters::createRemarkFilter(std::move(RemarkNameFilter),
                                     std::move(PassNameFilter),
                                     std::move(RemarkArgFilter), RemarkType);
}

Error useCollectRemark(StringRef Buffer, Counter &Counter, Filters &Filter) {
  // Create Parser.
  auto MaybeParser = createRemarkParser(InputFormat, Buffer);
  if (!MaybeParser)
    return MaybeParser.takeError();
  auto &Parser = **MaybeParser;
  auto MaybeRemark = Parser.next();
  for (; MaybeRemark; MaybeRemark = Parser.next()) {
    const Remark &Remark = **MaybeRemark;
    if (Filter.filterRemark(Remark))
      Counter.collect(Remark);
  }

  if (auto E = Counter.print(OutputFileName))
    return E;
  auto E = MaybeRemark.takeError();
  if (!E.isA<EndOfFileError>())
    return E;
  consumeError(std::move(E));
  return Error::success();
}

static Error collectRemarks() {
  // Create a parser for the user-specified input format.
  auto MaybeBuf = getInputMemoryBuffer(InputFileName);
  if (!MaybeBuf)
    return MaybeBuf.takeError();
  StringRef Buffer = (*MaybeBuf)->getBuffer();
  auto MaybeFilter = getRemarkFilter();
  if (!MaybeFilter)
    return MaybeFilter.takeError();
  auto &Filter = *MaybeFilter;
  if (CountByOpt == CountBy::REMARK) {
    RemarkCounter RC(GroupByOpt);
    if (auto E = useCollectRemark(Buffer, RC, Filter))
      return E;
  } else if (CountByOpt == CountBy::ARGUMENT) {
    SmallVector<FilterMatcher, 4> ArgumentsVector;
    if (!Keys.empty()) {
      for (auto &Key : Keys)
        ArgumentsVector.push_back({Key, false});
    } else if (!RKeys.empty())
      for (auto Key : RKeys)
        ArgumentsVector.push_back({Key, true});
    else
      ArgumentsVector.push_back({".*", true});

    Expected<ArgumentCounter> AC = ArgumentCounter::createArgumentCounter(
        GroupByOpt, ArgumentsVector, Buffer, Filter);
    if (!AC)
      return AC.takeError();
    if (auto E = useCollectRemark(Buffer, *AC, Filter))
      return E;
  }
  return Error::success();
}

static CommandRegistration CountReg(&CountSub, collectRemarks);
