//===-- clang/Basic/Sarif.cpp - SarifDocumentWriter class definition ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the SARIFDocumentWriter class, and
/// associated builders such as:
/// - \ref SarifArtifact
/// - \ref SarifArtifactLocation
/// - \ref SarifRule
/// - \ref SarifResult
//===----------------------------------------------------------------------===//
#include "clang/Basic/Sarif.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"

#include <optional>
#include <string>
#include <utility>

using namespace clang;
using namespace llvm;

using clang::detail::SarifArtifact;
using clang::detail::SarifArtifactLocation;

static StringRef getFileName(FileEntryRef FE) {
  StringRef Filename = FE.getFileEntry().tryGetRealPathName();
  if (Filename.empty())
    Filename = FE.getName();
  return Filename;
}
/// \name URI
/// @{

/// \internal
/// \brief
/// Return the RFC3986 encoding of the input character.
///
/// \param C Character to encode to RFC3986.
///
/// \return The RFC3986 representation of \c C.
static std::string percentEncodeURICharacter(char C) {
  // RFC 3986 claims alpha, numeric, and this handful of
  // characters are not reserved for the path component and
  // should be written out directly. Otherwise, percent
  // encode the character and write that out instead of the
  // reserved character.
  if (llvm::isAlnum(C) || StringRef("-._~:@!$&'()*+,;=").contains(C))
    return std::string(&C, 1);
  return "%" + llvm::toHex(StringRef(&C, 1));
}

/// \internal
/// \brief Return a URI representing the given file name.
///
/// \param Filename The filename to be represented as URI.
///
/// \return RFC3986 URI representing the input file name.
static std::string fileNameToURI(StringRef Filename) {
  SmallString<32> Ret = StringRef("file://");

  // Get the root name to see if it has a URI authority.
  StringRef Root = sys::path::root_name(Filename);
  if (Root.starts_with("//")) {
    // There is an authority, so add it to the URI.
    Ret += Root.drop_front(2).str();
  } else if (!Root.empty()) {
    // There is no authority, so end the component and add the root to the URI.
    Ret += Twine("/" + Root).str();
  }

  auto Iter = sys::path::begin(Filename), End = sys::path::end(Filename);
  assert(Iter != End && "Expected there to be a non-root path component.");
  // Add the rest of the path components, encoding any reserved characters;
  // we skip past the first path component, as it was handled it above.
  for (StringRef Component : llvm::make_range(++Iter, End)) {
    // For reasons unknown to me, we may get a backslash with Windows native
    // paths for the initial backslash following the drive component, which
    // we need to ignore as a URI path part.
    if (Component == "\\")
      continue;

    // Add the separator between the previous path part and the one being
    // currently processed.
    Ret += "/";

    // URI encode the part.
    for (char C : Component) {
      Ret += percentEncodeURICharacter(C);
    }
  }

  return std::string(Ret);
}
///  @}

/// \brief Calculate the column position expressed in the number of UTF-8 code
/// points from column start to the source location
///
/// \param Loc The source location whose column needs to be calculated.
/// \param TokenLen Optional hint for when the token is multiple bytes long.
///
/// \return The column number as a UTF-8 aware byte offset from column start to
/// the effective source location.
static unsigned int adjustColumnPos(FullSourceLoc Loc,
                                    unsigned int TokenLen = 0) {
  assert(!Loc.isInvalid() && "invalid Loc when adjusting column position");

  std::pair<FileID, unsigned> LocInfo = Loc.getDecomposedExpansionLoc();
  std::optional<MemoryBufferRef> Buf =
      Loc.getManager().getBufferOrNone(LocInfo.first);
  assert(Buf && "got an invalid buffer for the location's file");
  assert(Buf->getBufferSize() >= (LocInfo.second + TokenLen) &&
         "token extends past end of buffer?");

  // Adjust the offset to be the start of the line, since we'll be counting
  // Unicode characters from there until our column offset.
  unsigned int Off = LocInfo.second - (Loc.getExpansionColumnNumber() - 1);
  unsigned int Ret = 1;
  while (Off < (LocInfo.second + TokenLen)) {
    Off += getNumBytesForUTF8(Buf->getBuffer()[Off]);
    Ret++;
  }

  return Ret;
}

/// \name SARIF Utilities
/// @{

/// \internal
json::Object createMessage(StringRef Text) {
  return json::Object{{"text", Text.str()}};
}

/// \internal
/// \pre CharSourceRange must be a token range
static json::Object createTextRegion(const SourceManager &SM,
                                     const CharSourceRange &R) {
  FullSourceLoc BeginCharLoc{R.getBegin(), SM};
  FullSourceLoc EndCharLoc{R.getEnd(), SM};
  json::Object Region{{"startLine", BeginCharLoc.getExpansionLineNumber()},
                      {"startColumn", adjustColumnPos(BeginCharLoc)}};

  if (BeginCharLoc == EndCharLoc) {
    Region["endColumn"] = adjustColumnPos(BeginCharLoc);
  } else {
    Region["endLine"] = EndCharLoc.getExpansionLineNumber();
    Region["endColumn"] = adjustColumnPos(EndCharLoc);
  }
  return Region;
}

static json::Object createLocation(json::Object &&PhysicalLocation,
                                   StringRef Message = "") {
  json::Object Ret{{"physicalLocation", std::move(PhysicalLocation)}};
  if (!Message.empty())
    Ret.insert({"message", createMessage(Message)});
  return Ret;
}

static StringRef importanceToStr(ThreadFlowImportance I) {
  switch (I) {
  case ThreadFlowImportance::Important:
    return "important";
  case ThreadFlowImportance::Essential:
    return "essential";
  case ThreadFlowImportance::Unimportant:
    return "unimportant";
  }
  llvm_unreachable("Fully covered switch is not so fully covered");
}

static StringRef resultLevelToStr(SarifResultLevel R) {
  switch (R) {
  case SarifResultLevel::None:
    return "none";
  case SarifResultLevel::Note:
    return "note";
  case SarifResultLevel::Warning:
    return "warning";
  case SarifResultLevel::Error:
    return "error";
  }
  llvm_unreachable("Potentially un-handled SarifResultLevel. "
                   "Is the switch not fully covered?");
}

static json::Object
createThreadFlowLocation(json::Object &&Location,
                         const ThreadFlowImportance &Importance) {
  return json::Object{{"location", std::move(Location)},
                      {"importance", importanceToStr(Importance)}};
}
///  @}

json::Object
SarifDocumentWriter::createPhysicalLocation(const CharSourceRange &R) {
  assert(R.isValid() &&
         "Cannot create a physicalLocation from invalid SourceRange!");
  assert(R.isCharRange() &&
         "Cannot create a physicalLocation from a token range!");
  FullSourceLoc Start{R.getBegin(), SourceMgr};
  OptionalFileEntryRef FE = Start.getExpansionLoc().getFileEntryRef();
  assert(FE && "Diagnostic does not exist within a valid file!");

  const std::string &FileURI = fileNameToURI(getFileName(*FE));
  auto I = CurrentArtifacts.find(FileURI);

  if (I == CurrentArtifacts.end()) {
    uint32_t Idx = static_cast<uint32_t>(CurrentArtifacts.size());
    const SarifArtifactLocation &Location =
        SarifArtifactLocation::create(FileURI).setIndex(Idx);
    const SarifArtifact &Artifact = SarifArtifact::create(Location)
                                        .setRoles({"resultFile"})
                                        .setLength(FE->getSize())
                                        .setMimeType("text/plain");
    auto StatusIter = CurrentArtifacts.insert({FileURI, Artifact});
    // If inserted, ensure the original iterator points to the newly inserted
    // element, so it can be used downstream.
    if (StatusIter.second)
      I = StatusIter.first;
  }
  assert(I != CurrentArtifacts.end() && "Failed to insert new artifact");
  const SarifArtifactLocation &Location = I->second.Location;
  json::Object ArtifactLocationObject{{"uri", Location.URI}};
  if (Location.Index.has_value())
    ArtifactLocationObject["index"] = *Location.Index;
  return json::Object{{{"artifactLocation", std::move(ArtifactLocationObject)},
                       {"region", createTextRegion(SourceMgr, R)}}};
}

json::Object &SarifDocumentWriter::getCurrentTool() {
  assert(!Closed && "SARIF Document is closed. "
                    "Need to call createRun() before using getcurrentTool!");

  // Since Closed = false here, expect there to be at least 1 Run, anything
  // else is an invalid state.
  assert(!Runs.empty() && "There are no runs associated with the document!");

  return *Runs.back().getAsObject()->get("tool")->getAsObject();
}

void SarifDocumentWriter::reset() {
  CurrentRules.clear();
  CurrentArtifacts.clear();
}

void SarifDocumentWriter::endRun() {
  // Exit early if trying to close a closed Document.
  if (Closed) {
    reset();
    return;
  }

  // Since Closed = false here, expect there to be at least 1 Run, anything
  // else is an invalid state.
  assert(!Runs.empty() && "There are no runs associated with the document!");

  // Flush all the rules.
  json::Object &Tool = getCurrentTool();
  json::Array Rules;
  for (const SarifRule &R : CurrentRules) {
    json::Object Config{
        {"enabled", R.DefaultConfiguration.Enabled},
        {"level", resultLevelToStr(R.DefaultConfiguration.Level)},
        {"rank", R.DefaultConfiguration.Rank}};
    json::Object Rule{
        {"name", R.Name},
        {"id", R.Id},
        {"fullDescription", json::Object{{"text", R.Description}}},
        {"defaultConfiguration", std::move(Config)}};
    if (!R.HelpURI.empty())
      Rule["helpUri"] = R.HelpURI;
    Rules.emplace_back(std::move(Rule));
  }
  json::Object &Driver = *Tool.getObject("driver");
  Driver["rules"] = std::move(Rules);

  // Flush all the artifacts.
  json::Object &Run = getCurrentRun();
  json::Array *Artifacts = Run.getArray("artifacts");
  SmallVector<std::pair<StringRef, SarifArtifact>, 0> Vec;
  for (const auto &[K, V] : CurrentArtifacts)
    Vec.emplace_back(K, V);
  llvm::sort(Vec, llvm::less_first());
  for (const auto &[_, A] : Vec) {
    json::Object Loc{{"uri", A.Location.URI}};
    if (A.Location.Index.has_value()) {
      Loc["index"] = static_cast<int64_t>(*A.Location.Index);
    }
    json::Object Artifact;
    Artifact["location"] = std::move(Loc);
    if (A.Length.has_value())
      Artifact["length"] = static_cast<int64_t>(*A.Length);
    if (!A.Roles.empty())
      Artifact["roles"] = json::Array(A.Roles);
    if (!A.MimeType.empty())
      Artifact["mimeType"] = A.MimeType;
    if (A.Offset.has_value())
      Artifact["offset"] = *A.Offset;
    Artifacts->push_back(json::Value(std::move(Artifact)));
  }

  // Clear, reset temporaries before next run.
  reset();

  // Mark the document as closed.
  Closed = true;
}

json::Array
SarifDocumentWriter::createThreadFlows(ArrayRef<ThreadFlow> ThreadFlows) {
  json::Object Ret{{"locations", json::Array{}}};
  json::Array Locs;
  for (const auto &ThreadFlow : ThreadFlows) {
    json::Object PLoc = createPhysicalLocation(ThreadFlow.Range);
    json::Object Loc = createLocation(std::move(PLoc), ThreadFlow.Message);
    Locs.emplace_back(
        createThreadFlowLocation(std::move(Loc), ThreadFlow.Importance));
  }
  Ret["locations"] = std::move(Locs);
  return json::Array{std::move(Ret)};
}

json::Object
SarifDocumentWriter::createCodeFlow(ArrayRef<ThreadFlow> ThreadFlows) {
  return json::Object{{"threadFlows", createThreadFlows(ThreadFlows)}};
}

void SarifDocumentWriter::createRun(StringRef ShortToolName,
                                    StringRef LongToolName,
                                    StringRef ToolVersion) {
  // Clear resources associated with a previous run.
  endRun();

  // Signify a new run has begun.
  Closed = false;

  json::Object Tool{
      {"driver",
       json::Object{{"name", ShortToolName},
                    {"fullName", LongToolName},
                    {"language", "en-US"},
                    {"version", ToolVersion},
                    {"informationUri",
                     "https://clang.llvm.org/docs/UsersManual.html"}}}};
  json::Object TheRun{{"tool", std::move(Tool)},
                      {"results", {}},
                      {"artifacts", {}},
                      {"columnKind", "unicodeCodePoints"}};
  Runs.emplace_back(std::move(TheRun));
}

json::Object &SarifDocumentWriter::getCurrentRun() {
  assert(!Closed &&
         "SARIF Document is closed. "
         "Can only getCurrentRun() if document is opened via createRun(), "
         "create a run first");

  // Since Closed = false here, expect there to be at least 1 Run, anything
  // else is an invalid state.
  assert(!Runs.empty() && "There are no runs associated with the document!");
  return *Runs.back().getAsObject();
}

size_t SarifDocumentWriter::createRule(const SarifRule &Rule) {
  size_t Ret = CurrentRules.size();
  CurrentRules.emplace_back(Rule);
  return Ret;
}

void SarifDocumentWriter::appendResult(const SarifResult &Result) {
  size_t RuleIdx = Result.RuleIdx;
  assert(RuleIdx < CurrentRules.size() &&
         "Trying to reference a rule that doesn't exist");
  const SarifRule &Rule = CurrentRules[RuleIdx];
  assert(Rule.DefaultConfiguration.Enabled &&
         "Cannot add a result referencing a disabled Rule");
  json::Object Ret{{"message", createMessage(Result.DiagnosticMessage)},
                   {"ruleIndex", static_cast<int64_t>(RuleIdx)},
                   {"ruleId", Rule.Id}};
  if (!Result.Locations.empty()) {
    json::Array Locs;
    for (auto &Range : Result.Locations) {
      Locs.emplace_back(createLocation(createPhysicalLocation(Range)));
    }
    Ret["locations"] = std::move(Locs);
  }
  if (!Result.ThreadFlows.empty())
    Ret["codeFlows"] = json::Array{createCodeFlow(Result.ThreadFlows)};

  Ret["level"] = resultLevelToStr(
      Result.LevelOverride.value_or(Rule.DefaultConfiguration.Level));

  json::Object &Run = getCurrentRun();
  json::Array *Results = Run.getArray("results");
  Results->emplace_back(std::move(Ret));
}

json::Object SarifDocumentWriter::createDocument() {
  // Flush all temporaries to their destinations if needed.
  endRun();

  json::Object Doc{
      {"$schema", SchemaURI},
      {"version", SchemaVersion},
  };
  if (!Runs.empty())
    Doc["runs"] = json::Array(Runs);
  return Doc;
}
