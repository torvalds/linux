//===- xray-account.h - XRay Function Call Accounting ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements basic function call accounting from an XRay trace.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <numeric>
#include <system_error>
#include <utility>

#include "xray-account.h"
#include "xray-registry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/XRay/InstrumentationMap.h"
#include "llvm/XRay/Trace.h"

#include <cmath>

using namespace llvm;
using namespace llvm::xray;

static cl::SubCommand Account("account", "Function call accounting");
static cl::opt<std::string> AccountInput(cl::Positional,
                                         cl::desc("<xray log file>"),
                                         cl::Required, cl::sub(Account));
static cl::opt<bool>
    AccountKeepGoing("keep-going", cl::desc("Keep going on errors encountered"),
                     cl::sub(Account), cl::init(false));
static cl::alias AccountKeepGoing2("k", cl::aliasopt(AccountKeepGoing),
                                   cl::desc("Alias for -keep_going"));
static cl::opt<bool> AccountRecursiveCallsOnly(
    "recursive-calls-only", cl::desc("Only count the calls that are recursive"),
    cl::sub(Account), cl::init(false));
static cl::opt<bool> AccountDeduceSiblingCalls(
    "deduce-sibling-calls",
    cl::desc("Deduce sibling calls when unrolling function call stacks"),
    cl::sub(Account), cl::init(false));
static cl::alias
    AccountDeduceSiblingCalls2("d", cl::aliasopt(AccountDeduceSiblingCalls),
                               cl::desc("Alias for -deduce_sibling_calls"));
static cl::opt<std::string>
    AccountOutput("output", cl::value_desc("output file"), cl::init("-"),
                  cl::desc("output file; use '-' for stdout"),
                  cl::sub(Account));
static cl::alias AccountOutput2("o", cl::aliasopt(AccountOutput),
                                cl::desc("Alias for -output"));
enum class AccountOutputFormats { TEXT, CSV };
static cl::opt<AccountOutputFormats>
    AccountOutputFormat("format", cl::desc("output format"),
                        cl::values(clEnumValN(AccountOutputFormats::TEXT,
                                              "text", "report stats in text"),
                                   clEnumValN(AccountOutputFormats::CSV, "csv",
                                              "report stats in csv")),
                        cl::sub(Account));
static cl::alias AccountOutputFormat2("f", cl::desc("Alias of -format"),
                                      cl::aliasopt(AccountOutputFormat));

enum class SortField {
  FUNCID,
  COUNT,
  MIN,
  MED,
  PCT90,
  PCT99,
  MAX,
  SUM,
  FUNC,
};

static cl::opt<SortField> AccountSortOutput(
    "sort", cl::desc("sort output by this field"), cl::value_desc("field"),
    cl::sub(Account), cl::init(SortField::FUNCID),
    cl::values(clEnumValN(SortField::FUNCID, "funcid", "function id"),
               clEnumValN(SortField::COUNT, "count", "function call counts"),
               clEnumValN(SortField::MIN, "min", "minimum function durations"),
               clEnumValN(SortField::MED, "med", "median function durations"),
               clEnumValN(SortField::PCT90, "90p", "90th percentile durations"),
               clEnumValN(SortField::PCT99, "99p", "99th percentile durations"),
               clEnumValN(SortField::MAX, "max", "maximum function durations"),
               clEnumValN(SortField::SUM, "sum", "sum of call durations"),
               clEnumValN(SortField::FUNC, "func", "function names")));
static cl::alias AccountSortOutput2("s", cl::aliasopt(AccountSortOutput),
                                    cl::desc("Alias for -sort"));

enum class SortDirection {
  ASCENDING,
  DESCENDING,
};
static cl::opt<SortDirection> AccountSortOrder(
    "sortorder", cl::desc("sort ordering"), cl::init(SortDirection::ASCENDING),
    cl::values(clEnumValN(SortDirection::ASCENDING, "asc", "ascending"),
               clEnumValN(SortDirection::DESCENDING, "dsc", "descending")),
    cl::sub(Account));
static cl::alias AccountSortOrder2("r", cl::aliasopt(AccountSortOrder),
                                   cl::desc("Alias for -sortorder"));

static cl::opt<int> AccountTop("top", cl::desc("only show the top N results"),
                               cl::value_desc("N"), cl::sub(Account),
                               cl::init(-1));
static cl::alias AccountTop2("p", cl::desc("Alias for -top"),
                             cl::aliasopt(AccountTop));

static cl::opt<std::string>
    AccountInstrMap("instr_map",
                    cl::desc("binary with the instrumentation map, or "
                             "a separate instrumentation map"),
                    cl::value_desc("binary with xray_instr_map"),
                    cl::sub(Account), cl::init(""));
static cl::alias AccountInstrMap2("m", cl::aliasopt(AccountInstrMap),
                                  cl::desc("Alias for -instr_map"));

namespace {

template <class T, class U> void setMinMax(std::pair<T, T> &MM, U &&V) {
  if (MM.first == 0 || MM.second == 0)
    MM = std::make_pair(std::forward<U>(V), std::forward<U>(V));
  else
    MM = std::make_pair(std::min(MM.first, V), std::max(MM.second, V));
}

template <class T> T diff(T L, T R) { return std::max(L, R) - std::min(L, R); }

} // namespace

using RecursionStatus = LatencyAccountant::FunctionStack::RecursionStatus;
RecursionStatus &RecursionStatus::operator++() {
  auto Depth = Bitfield::get<RecursionStatus::Depth>(Storage);
  assert(Depth >= 0 && Depth < std::numeric_limits<decltype(Depth)>::max());
  ++Depth;
  Bitfield::set<RecursionStatus::Depth>(Storage, Depth); // ++Storage
  // Did this function just (maybe indirectly) call itself the first time?
  if (!isRecursive() && Depth == 2) // Storage == 2  /  Storage s> 1
    Bitfield::set<RecursionStatus::IsRecursive>(Storage,
                                                true); // Storage |= INT_MIN
  return *this;
}
RecursionStatus &RecursionStatus::operator--() {
  auto Depth = Bitfield::get<RecursionStatus::Depth>(Storage);
  assert(Depth > 0);
  --Depth;
  Bitfield::set<RecursionStatus::Depth>(Storage, Depth); // --Storage
  // Did we leave a function that previouly (maybe indirectly) called itself?
  if (isRecursive() && Depth == 0) // Storage == INT_MIN
    Bitfield::set<RecursionStatus::IsRecursive>(Storage, false); // Storage = 0
  return *this;
}
bool RecursionStatus::isRecursive() const {
  return Bitfield::get<RecursionStatus::IsRecursive>(Storage); // Storage s< 0
}

bool LatencyAccountant::accountRecord(const XRayRecord &Record) {
  setMinMax(PerThreadMinMaxTSC[Record.TId], Record.TSC);
  setMinMax(PerCPUMinMaxTSC[Record.CPU], Record.TSC);

  if (CurrentMaxTSC == 0)
    CurrentMaxTSC = Record.TSC;

  if (Record.TSC < CurrentMaxTSC)
    return false;

  auto &ThreadStack = PerThreadFunctionStack[Record.TId];
  if (RecursiveCallsOnly && !ThreadStack.RecursionDepth)
    ThreadStack.RecursionDepth.emplace();
  switch (Record.Type) {
  case RecordTypes::CUSTOM_EVENT:
  case RecordTypes::TYPED_EVENT:
    // TODO: Support custom and typed event accounting in the future.
    return true;
  case RecordTypes::ENTER:
  case RecordTypes::ENTER_ARG: {
    ThreadStack.Stack.emplace_back(Record.FuncId, Record.TSC);
    if (ThreadStack.RecursionDepth)
      ++(*ThreadStack.RecursionDepth)[Record.FuncId];
    break;
  }
  case RecordTypes::EXIT:
  case RecordTypes::TAIL_EXIT: {
    if (ThreadStack.Stack.empty())
      return false;

    if (ThreadStack.Stack.back().first == Record.FuncId) {
      const auto &Top = ThreadStack.Stack.back();
      if (!ThreadStack.RecursionDepth ||
          (*ThreadStack.RecursionDepth)[Top.first].isRecursive())
        recordLatency(Top.first, diff(Top.second, Record.TSC));
      if (ThreadStack.RecursionDepth)
        --(*ThreadStack.RecursionDepth)[Top.first];
      ThreadStack.Stack.pop_back();
      break;
    }

    if (!DeduceSiblingCalls)
      return false;

    // Look for the parent up the stack.
    auto Parent =
        llvm::find_if(llvm::reverse(ThreadStack.Stack),
                      [&](const std::pair<const int32_t, uint64_t> &E) {
                        return E.first == Record.FuncId;
                      });
    if (Parent == ThreadStack.Stack.rend())
      return false;

    // Account time for this apparently sibling call exit up the stack.
    // Considering the following case:
    //
    //   f()
    //    g()
    //      h()
    //
    // We might only ever see the following entries:
    //
    //   -> f()
    //   -> g()
    //   -> h()
    //   <- h()
    //   <- f()
    //
    // Now we don't see the exit to g() because some older version of the XRay
    // runtime wasn't instrumenting tail exits. If we don't deduce tail calls,
    // we may potentially never account time for g() -- and this code would have
    // already bailed out, because `<- f()` doesn't match the current "top" of
    // stack where we're waiting for the exit to `g()` instead. This is not
    // ideal and brittle -- so instead we provide a potentially inaccurate
    // accounting of g() instead, computing it from the exit of f().
    //
    // While it might be better that we account the time between `-> g()` and
    // `-> h()` as the proper accounting of time for g() here, this introduces
    // complexity to do correctly (need to backtrack, etc.).
    //
    // FIXME: Potentially implement the more complex deduction algorithm?
    auto R = make_range(std::next(Parent).base(), ThreadStack.Stack.end());
    for (auto &E : R) {
      if (!ThreadStack.RecursionDepth ||
          (*ThreadStack.RecursionDepth)[E.first].isRecursive())
        recordLatency(E.first, diff(E.second, Record.TSC));
    }
    for (auto &Top : reverse(R)) {
      if (ThreadStack.RecursionDepth)
        --(*ThreadStack.RecursionDepth)[Top.first];
      ThreadStack.Stack.pop_back();
    }
    break;
  }
  }

  return true;
}

namespace {

// We consolidate the data into a struct which we can output in various forms.
struct ResultRow {
  uint64_t Count;
  double Min;
  double Median;
  double Pct90;
  double Pct99;
  double Max;
  double Sum;
  std::string DebugInfo;
  std::string Function;
};

ResultRow getStats(MutableArrayRef<uint64_t> Timings) {
  assert(!Timings.empty());
  ResultRow R;
  R.Sum = std::accumulate(Timings.begin(), Timings.end(), 0.0);
  auto MinMax = std::minmax_element(Timings.begin(), Timings.end());
  R.Min = *MinMax.first;
  R.Max = *MinMax.second;
  R.Count = Timings.size();

  auto MedianOff = Timings.size() / 2;
  std::nth_element(Timings.begin(), Timings.begin() + MedianOff, Timings.end());
  R.Median = Timings[MedianOff];

  auto Pct90Off = std::floor(Timings.size() * 0.9);
  std::nth_element(Timings.begin(), Timings.begin() + (uint64_t)Pct90Off,
                   Timings.end());
  R.Pct90 = Timings[Pct90Off];

  auto Pct99Off = std::floor(Timings.size() * 0.99);
  std::nth_element(Timings.begin(), Timings.begin() + (uint64_t)Pct99Off,
                   Timings.end());
  R.Pct99 = Timings[Pct99Off];
  return R;
}

} // namespace

using TupleType = std::tuple<int32_t, uint64_t, ResultRow>;

template <typename F>
static void sortByKey(std::vector<TupleType> &Results, F Fn) {
  bool ASC = AccountSortOrder == SortDirection::ASCENDING;
  llvm::sort(Results, [=](const TupleType &L, const TupleType &R) {
    return ASC ? Fn(L) < Fn(R) : Fn(L) > Fn(R);
  });
}

template <class F>
void LatencyAccountant::exportStats(const XRayFileHeader &Header, F Fn) const {
  std::vector<TupleType> Results;
  Results.reserve(FunctionLatencies.size());
  for (auto FT : FunctionLatencies) {
    const auto &FuncId = FT.first;
    auto &Timings = FT.second;
    Results.emplace_back(FuncId, Timings.size(), getStats(Timings));
    auto &Row = std::get<2>(Results.back());
    if (Header.CycleFrequency) {
      double CycleFrequency = Header.CycleFrequency;
      Row.Min /= CycleFrequency;
      Row.Median /= CycleFrequency;
      Row.Pct90 /= CycleFrequency;
      Row.Pct99 /= CycleFrequency;
      Row.Max /= CycleFrequency;
      Row.Sum /= CycleFrequency;
    }

    Row.Function = FuncIdHelper.SymbolOrNumber(FuncId);
    Row.DebugInfo = FuncIdHelper.FileLineAndColumn(FuncId);
  }

  // Sort the data according to user-provided flags.
  switch (AccountSortOutput) {
  case SortField::FUNCID:
    sortByKey(Results, [](const TupleType &X) { return std::get<0>(X); });
    break;
  case SortField::COUNT:
    sortByKey(Results, [](const TupleType &X) { return std::get<1>(X); });
    break;
  case SortField::MIN:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Min; });
    break;
  case SortField::MED:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Median; });
    break;
  case SortField::PCT90:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Pct90; });
    break;
  case SortField::PCT99:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Pct99; });
    break;
  case SortField::MAX:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Max; });
    break;
  case SortField::SUM:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Sum; });
    break;
  case SortField::FUNC:
    llvm_unreachable("Not implemented");
  }

  if (AccountTop > 0) {
    auto MaxTop =
        std::min(AccountTop.getValue(), static_cast<int>(Results.size()));
    Results.erase(Results.begin() + MaxTop, Results.end());
  }

  for (const auto &R : Results)
    Fn(std::get<0>(R), std::get<1>(R), std::get<2>(R));
}

void LatencyAccountant::exportStatsAsText(raw_ostream &OS,
                                          const XRayFileHeader &Header) const {
  OS << "Functions with latencies: " << FunctionLatencies.size() << "\n";

  // We spend some effort to make the text output more readable, so we do the
  // following formatting decisions for each of the fields:
  //
  //   - funcid: 32-bit, but we can determine the largest number and be
  //   between
  //     a minimum of 5 characters, up to 9 characters, right aligned.
  //   - count:  64-bit, but we can determine the largest number and be
  //   between
  //     a minimum of 5 characters, up to 9 characters, right aligned.
  //   - min, median, 90pct, 99pct, max: double precision, but we want to keep
  //     the values in seconds, with microsecond precision (0.000'001), so we
  //     have at most 6 significant digits, with the whole number part to be
  //     at
  //     least 1 character. For readability we'll right-align, with full 9
  //     characters each.
  //   - debug info, function name: we format this as a concatenation of the
  //     debug info and the function name.
  //
  static constexpr char StatsHeaderFormat[] =
      "{0,+9} {1,+10} [{2,+9}, {3,+9}, {4,+9}, {5,+9}, {6,+9}] {7,+9}";
  static constexpr char StatsFormat[] =
      R"({0,+9} {1,+10} [{2,+9:f6}, {3,+9:f6}, {4,+9:f6}, {5,+9:f6}, {6,+9:f6}] {7,+9:f6})";
  OS << llvm::formatv(StatsHeaderFormat, "funcid", "count", "min", "med", "90p",
                      "99p", "max", "sum")
     << llvm::formatv("  {0,-12}\n", "function");
  exportStats(Header, [&](int32_t FuncId, size_t Count, const ResultRow &Row) {
    OS << llvm::formatv(StatsFormat, FuncId, Count, Row.Min, Row.Median,
                        Row.Pct90, Row.Pct99, Row.Max, Row.Sum)
       << "  " << Row.DebugInfo << ": " << Row.Function << "\n";
  });
}

void LatencyAccountant::exportStatsAsCSV(raw_ostream &OS,
                                         const XRayFileHeader &Header) const {
  OS << "funcid,count,min,median,90%ile,99%ile,max,sum,debug,function\n";
  exportStats(Header, [&](int32_t FuncId, size_t Count, const ResultRow &Row) {
    OS << FuncId << ',' << Count << ',' << Row.Min << ',' << Row.Median << ','
       << Row.Pct90 << ',' << Row.Pct99 << ',' << Row.Max << "," << Row.Sum
       << ",\"" << Row.DebugInfo << "\",\"" << Row.Function << "\"\n";
  });
}

using namespace llvm::xray;

namespace llvm {
template <> struct format_provider<llvm::xray::RecordTypes> {
  static void format(const llvm::xray::RecordTypes &T, raw_ostream &Stream,
                     StringRef Style) {
    switch (T) {
    case RecordTypes::ENTER:
      Stream << "enter";
      break;
    case RecordTypes::ENTER_ARG:
      Stream << "enter-arg";
      break;
    case RecordTypes::EXIT:
      Stream << "exit";
      break;
    case RecordTypes::TAIL_EXIT:
      Stream << "tail-exit";
      break;
    case RecordTypes::CUSTOM_EVENT:
      Stream << "custom-event";
      break;
    case RecordTypes::TYPED_EVENT:
      Stream << "typed-event";
      break;
    }
  }
};
} // namespace llvm

static CommandRegistration Unused(&Account, []() -> Error {
  InstrumentationMap Map;
  if (!AccountInstrMap.empty()) {
    auto InstrumentationMapOrError = loadInstrumentationMap(AccountInstrMap);
    if (!InstrumentationMapOrError)
      return joinErrors(make_error<StringError>(
                            Twine("Cannot open instrumentation map '") +
                                AccountInstrMap + "'",
                            std::make_error_code(std::errc::invalid_argument)),
                        InstrumentationMapOrError.takeError());
    Map = std::move(*InstrumentationMapOrError);
  }

  std::error_code EC;
  raw_fd_ostream OS(AccountOutput, EC, sys::fs::OpenFlags::OF_TextWithCRLF);
  if (EC)
    return make_error<StringError>(
        Twine("Cannot open file '") + AccountOutput + "' for writing.", EC);

  const auto &FunctionAddresses = Map.getFunctionAddresses();
  symbolize::LLVMSymbolizer Symbolizer;
  llvm::xray::FuncIdConversionHelper FuncIdHelper(AccountInstrMap, Symbolizer,
                                                  FunctionAddresses);
  xray::LatencyAccountant FCA(FuncIdHelper, AccountRecursiveCallsOnly,
                              AccountDeduceSiblingCalls);
  auto TraceOrErr = loadTraceFile(AccountInput);
  if (!TraceOrErr)
    return joinErrors(
        make_error<StringError>(
            Twine("Failed loading input file '") + AccountInput + "'",
            std::make_error_code(std::errc::executable_format_error)),
        TraceOrErr.takeError());

  auto &T = *TraceOrErr;
  for (const auto &Record : T) {
    if (FCA.accountRecord(Record))
      continue;
    errs()
        << "Error processing record: "
        << llvm::formatv(
               R"({{type: {0}; cpu: {1}; record-type: {2}; function-id: {3}; tsc: {4}; thread-id: {5}; process-id: {6}}})",
               Record.RecordType, Record.CPU, Record.Type, Record.FuncId,
               Record.TSC, Record.TId, Record.PId)
        << '\n';
    for (const auto &ThreadStack : FCA.getPerThreadFunctionStack()) {
      errs() << "Thread ID: " << ThreadStack.first << "\n";
      if (ThreadStack.second.Stack.empty()) {
        errs() << "  (empty stack)\n";
        continue;
      }
      auto Level = ThreadStack.second.Stack.size();
      for (const auto &Entry : llvm::reverse(ThreadStack.second.Stack))
        errs() << "  #" << Level-- << "\t"
               << FuncIdHelper.SymbolOrNumber(Entry.first) << '\n';
    }
    if (!AccountKeepGoing)
      return make_error<StringError>(
          Twine("Failed accounting function calls in file '") + AccountInput +
              "'.",
          std::make_error_code(std::errc::executable_format_error));
  }
  switch (AccountOutputFormat) {
  case AccountOutputFormats::TEXT:
    FCA.exportStatsAsText(OS, T.getFileHeader());
    break;
  case AccountOutputFormats::CSV:
    FCA.exportStatsAsCSV(OS, T.getFileHeader());
    break;
  }

  return Error::success();
});
