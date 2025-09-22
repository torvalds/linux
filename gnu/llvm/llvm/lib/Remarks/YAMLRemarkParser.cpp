//===- YAMLRemarkParser.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides utility methods used by clients that want to use the
// parser for remark diagnostics in LLVM.
//
//===----------------------------------------------------------------------===//

#include "YAMLRemarkParser.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Path.h"
#include <optional>

using namespace llvm;
using namespace llvm::remarks;

char YAMLParseError::ID = 0;

static void handleDiagnostic(const SMDiagnostic &Diag, void *Ctx) {
  assert(Ctx && "Expected non-null Ctx in diagnostic handler.");
  std::string &Message = *static_cast<std::string *>(Ctx);
  assert(Message.empty() && "Expected an empty string.");
  raw_string_ostream OS(Message);
  Diag.print(/*ProgName=*/nullptr, OS, /*ShowColors*/ false,
             /*ShowKindLabels*/ true);
  OS << '\n';
  OS.flush();
}

YAMLParseError::YAMLParseError(StringRef Msg, SourceMgr &SM,
                               yaml::Stream &Stream, yaml::Node &Node) {
  // 1) Set up a diagnostic handler to avoid errors being printed out to
  // stderr.
  // 2) Use the stream to print the error with the associated node.
  // 3) The stream will use the source manager to print the error, which will
  // call the diagnostic handler.
  // 4) The diagnostic handler will stream the error directly into this object's
  // Message member, which is used when logging is asked for.
  auto OldDiagHandler = SM.getDiagHandler();
  auto OldDiagCtx = SM.getDiagContext();
  SM.setDiagHandler(handleDiagnostic, &Message);
  Stream.printError(&Node, Twine(Msg) + Twine('\n'));
  // Restore the old handlers.
  SM.setDiagHandler(OldDiagHandler, OldDiagCtx);
}

static SourceMgr setupSM(std::string &LastErrorMessage) {
  SourceMgr SM;
  SM.setDiagHandler(handleDiagnostic, &LastErrorMessage);
  return SM;
}

// Parse the magic number. This function returns true if this represents remark
// metadata, false otherwise.
static Expected<bool> parseMagic(StringRef &Buf) {
  if (!Buf.consume_front(remarks::Magic))
    return false;

  if (Buf.size() < 1 || !Buf.consume_front(StringRef("\0", 1)))
    return createStringError(std::errc::illegal_byte_sequence,
                             "Expecting \\0 after magic number.");
  return true;
}

static Expected<uint64_t> parseVersion(StringRef &Buf) {
  if (Buf.size() < sizeof(uint64_t))
    return createStringError(std::errc::illegal_byte_sequence,
                             "Expecting version number.");

  uint64_t Version =
      support::endian::read<uint64_t, llvm::endianness::little>(Buf.data());
  if (Version != remarks::CurrentRemarkVersion)
    return createStringError(std::errc::illegal_byte_sequence,
                             "Mismatching remark version. Got %" PRId64
                             ", expected %" PRId64 ".",
                             Version, remarks::CurrentRemarkVersion);
  Buf = Buf.drop_front(sizeof(uint64_t));
  return Version;
}

static Expected<uint64_t> parseStrTabSize(StringRef &Buf) {
  if (Buf.size() < sizeof(uint64_t))
    return createStringError(std::errc::illegal_byte_sequence,
                             "Expecting string table size.");
  uint64_t StrTabSize =
      support::endian::read<uint64_t, llvm::endianness::little>(Buf.data());
  Buf = Buf.drop_front(sizeof(uint64_t));
  return StrTabSize;
}

static Expected<ParsedStringTable> parseStrTab(StringRef &Buf,
                                               uint64_t StrTabSize) {
  if (Buf.size() < StrTabSize)
    return createStringError(std::errc::illegal_byte_sequence,
                             "Expecting string table.");

  // Attach the string table to the parser.
  ParsedStringTable Result(StringRef(Buf.data(), StrTabSize));
  Buf = Buf.drop_front(StrTabSize);
  return Expected<ParsedStringTable>(std::move(Result));
}

Expected<std::unique_ptr<YAMLRemarkParser>> remarks::createYAMLParserFromMeta(
    StringRef Buf, std::optional<ParsedStringTable> StrTab,
    std::optional<StringRef> ExternalFilePrependPath) {
  // We now have a magic number. The metadata has to be correct.
  Expected<bool> isMeta = parseMagic(Buf);
  if (!isMeta)
    return isMeta.takeError();
  // If it's not recognized as metadata, roll back.
  std::unique_ptr<MemoryBuffer> SeparateBuf;
  if (*isMeta) {
    Expected<uint64_t> Version = parseVersion(Buf);
    if (!Version)
      return Version.takeError();

    Expected<uint64_t> StrTabSize = parseStrTabSize(Buf);
    if (!StrTabSize)
      return StrTabSize.takeError();

    // If the size of string table is not 0, try to build one.
    if (*StrTabSize != 0) {
      if (StrTab)
        return createStringError(std::errc::illegal_byte_sequence,
                                 "String table already provided.");
      Expected<ParsedStringTable> MaybeStrTab = parseStrTab(Buf, *StrTabSize);
      if (!MaybeStrTab)
        return MaybeStrTab.takeError();
      StrTab = std::move(*MaybeStrTab);
    }
    // If it starts with "---", there is no external file.
    if (!Buf.starts_with("---")) {
      // At this point, we expect Buf to contain the external file path.
      StringRef ExternalFilePath = Buf;
      SmallString<80> FullPath;
      if (ExternalFilePrependPath)
        FullPath = *ExternalFilePrependPath;
      sys::path::append(FullPath, ExternalFilePath);

      // Try to open the file and start parsing from there.
      ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
          MemoryBuffer::getFile(FullPath);
      if (std::error_code EC = BufferOrErr.getError())
        return createFileError(FullPath, EC);

      // Keep the buffer alive.
      SeparateBuf = std::move(*BufferOrErr);
      Buf = SeparateBuf->getBuffer();
    }
  }

  std::unique_ptr<YAMLRemarkParser> Result =
      StrTab
          ? std::make_unique<YAMLStrTabRemarkParser>(Buf, std::move(*StrTab))
          : std::make_unique<YAMLRemarkParser>(Buf);
  if (SeparateBuf)
    Result->SeparateBuf = std::move(SeparateBuf);
  return std::move(Result);
}

YAMLRemarkParser::YAMLRemarkParser(StringRef Buf)
    : YAMLRemarkParser(Buf, std::nullopt) {}

YAMLRemarkParser::YAMLRemarkParser(StringRef Buf,
                                   std::optional<ParsedStringTable> StrTab)
    : RemarkParser{Format::YAML}, StrTab(std::move(StrTab)),
      SM(setupSM(LastErrorMessage)), Stream(Buf, SM), YAMLIt(Stream.begin()) {}

Error YAMLRemarkParser::error(StringRef Message, yaml::Node &Node) {
  return make_error<YAMLParseError>(Message, SM, Stream, Node);
}

Error YAMLRemarkParser::error() {
  if (LastErrorMessage.empty())
    return Error::success();
  Error E = make_error<YAMLParseError>(LastErrorMessage);
  LastErrorMessage.clear();
  return E;
}

Expected<std::unique_ptr<Remark>>
YAMLRemarkParser::parseRemark(yaml::Document &RemarkEntry) {
  if (Error E = error())
    return std::move(E);

  yaml::Node *YAMLRoot = RemarkEntry.getRoot();
  if (!YAMLRoot) {
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "not a valid YAML file.");
  }

  auto *Root = dyn_cast<yaml::MappingNode>(YAMLRoot);
  if (!Root)
    return error("document root is not of mapping type.", *YAMLRoot);

  std::unique_ptr<Remark> Result = std::make_unique<Remark>();
  Remark &TheRemark = *Result;

  // First, the type. It needs special handling since is not part of the
  // key-value stream.
  Expected<Type> T = parseType(*Root);
  if (!T)
    return T.takeError();
  else
    TheRemark.RemarkType = *T;

  // Then, parse the fields, one by one.
  for (yaml::KeyValueNode &RemarkField : *Root) {
    Expected<StringRef> MaybeKey = parseKey(RemarkField);
    if (!MaybeKey)
      return MaybeKey.takeError();
    StringRef KeyName = *MaybeKey;

    if (KeyName == "Pass") {
      if (Expected<StringRef> MaybeStr = parseStr(RemarkField))
        TheRemark.PassName = *MaybeStr;
      else
        return MaybeStr.takeError();
    } else if (KeyName == "Name") {
      if (Expected<StringRef> MaybeStr = parseStr(RemarkField))
        TheRemark.RemarkName = *MaybeStr;
      else
        return MaybeStr.takeError();
    } else if (KeyName == "Function") {
      if (Expected<StringRef> MaybeStr = parseStr(RemarkField))
        TheRemark.FunctionName = *MaybeStr;
      else
        return MaybeStr.takeError();
    } else if (KeyName == "Hotness") {
      if (Expected<unsigned> MaybeU = parseUnsigned(RemarkField))
        TheRemark.Hotness = *MaybeU;
      else
        return MaybeU.takeError();
    } else if (KeyName == "DebugLoc") {
      if (Expected<RemarkLocation> MaybeLoc = parseDebugLoc(RemarkField))
        TheRemark.Loc = *MaybeLoc;
      else
        return MaybeLoc.takeError();
    } else if (KeyName == "Args") {
      auto *Args = dyn_cast<yaml::SequenceNode>(RemarkField.getValue());
      if (!Args)
        return error("wrong value type for key.", RemarkField);

      for (yaml::Node &Arg : *Args) {
        if (Expected<Argument> MaybeArg = parseArg(Arg))
          TheRemark.Args.push_back(*MaybeArg);
        else
          return MaybeArg.takeError();
      }
    } else {
      return error("unknown key.", RemarkField);
    }
  }

  // Check if any of the mandatory fields are missing.
  if (TheRemark.RemarkType == Type::Unknown || TheRemark.PassName.empty() ||
      TheRemark.RemarkName.empty() || TheRemark.FunctionName.empty())
    return error("Type, Pass, Name or Function missing.",
                 *RemarkEntry.getRoot());

  return std::move(Result);
}

Expected<Type> YAMLRemarkParser::parseType(yaml::MappingNode &Node) {
  auto Type = StringSwitch<remarks::Type>(Node.getRawTag())
                  .Case("!Passed", remarks::Type::Passed)
                  .Case("!Missed", remarks::Type::Missed)
                  .Case("!Analysis", remarks::Type::Analysis)
                  .Case("!AnalysisFPCommute", remarks::Type::AnalysisFPCommute)
                  .Case("!AnalysisAliasing", remarks::Type::AnalysisAliasing)
                  .Case("!Failure", remarks::Type::Failure)
                  .Default(remarks::Type::Unknown);
  if (Type == remarks::Type::Unknown)
    return error("expected a remark tag.", Node);
  return Type;
}

Expected<StringRef> YAMLRemarkParser::parseKey(yaml::KeyValueNode &Node) {
  if (auto *Key = dyn_cast<yaml::ScalarNode>(Node.getKey()))
    return Key->getRawValue();

  return error("key is not a string.", Node);
}

Expected<StringRef> YAMLRemarkParser::parseStr(yaml::KeyValueNode &Node) {
  auto *Value = dyn_cast<yaml::ScalarNode>(Node.getValue());
  yaml::BlockScalarNode *ValueBlock;
  StringRef Result;
  if (!Value) {
    // Try to parse the value as a block node.
    ValueBlock = dyn_cast<yaml::BlockScalarNode>(Node.getValue());
    if (!ValueBlock)
      return error("expected a value of scalar type.", Node);
    Result = ValueBlock->getValue();
  } else
    Result = Value->getRawValue();

  Result.consume_front("\'");
  Result.consume_back("\'");

  return Result;
}

Expected<unsigned> YAMLRemarkParser::parseUnsigned(yaml::KeyValueNode &Node) {
  SmallVector<char, 4> Tmp;
  auto *Value = dyn_cast<yaml::ScalarNode>(Node.getValue());
  if (!Value)
    return error("expected a value of scalar type.", Node);
  unsigned UnsignedValue = 0;
  if (Value->getValue(Tmp).getAsInteger(10, UnsignedValue))
    return error("expected a value of integer type.", *Value);
  return UnsignedValue;
}

Expected<RemarkLocation>
YAMLRemarkParser::parseDebugLoc(yaml::KeyValueNode &Node) {
  auto *DebugLoc = dyn_cast<yaml::MappingNode>(Node.getValue());
  if (!DebugLoc)
    return error("expected a value of mapping type.", Node);

  std::optional<StringRef> File;
  std::optional<unsigned> Line;
  std::optional<unsigned> Column;

  for (yaml::KeyValueNode &DLNode : *DebugLoc) {
    Expected<StringRef> MaybeKey = parseKey(DLNode);
    if (!MaybeKey)
      return MaybeKey.takeError();
    StringRef KeyName = *MaybeKey;

    if (KeyName == "File") {
      if (Expected<StringRef> MaybeStr = parseStr(DLNode))
        File = *MaybeStr;
      else
        return MaybeStr.takeError();
    } else if (KeyName == "Column") {
      if (Expected<unsigned> MaybeU = parseUnsigned(DLNode))
        Column = *MaybeU;
      else
        return MaybeU.takeError();
    } else if (KeyName == "Line") {
      if (Expected<unsigned> MaybeU = parseUnsigned(DLNode))
        Line = *MaybeU;
      else
        return MaybeU.takeError();
    } else {
      return error("unknown entry in DebugLoc map.", DLNode);
    }
  }

  // If any of the debug loc fields is missing, return an error.
  if (!File || !Line || !Column)
    return error("DebugLoc node incomplete.", Node);

  return RemarkLocation{*File, *Line, *Column};
}

Expected<Argument> YAMLRemarkParser::parseArg(yaml::Node &Node) {
  auto *ArgMap = dyn_cast<yaml::MappingNode>(&Node);
  if (!ArgMap)
    return error("expected a value of mapping type.", Node);

  std::optional<StringRef> KeyStr;
  std::optional<StringRef> ValueStr;
  std::optional<RemarkLocation> Loc;

  for (yaml::KeyValueNode &ArgEntry : *ArgMap) {
    Expected<StringRef> MaybeKey = parseKey(ArgEntry);
    if (!MaybeKey)
      return MaybeKey.takeError();
    StringRef KeyName = *MaybeKey;

    // Try to parse debug locs.
    if (KeyName == "DebugLoc") {
      // Can't have multiple DebugLoc entries per argument.
      if (Loc)
        return error("only one DebugLoc entry is allowed per argument.",
                     ArgEntry);

      if (Expected<RemarkLocation> MaybeLoc = parseDebugLoc(ArgEntry)) {
        Loc = *MaybeLoc;
        continue;
      } else
        return MaybeLoc.takeError();
    }

    // If we already have a string, error out.
    if (ValueStr)
      return error("only one string entry is allowed per argument.", ArgEntry);

    // Try to parse the value.
    if (Expected<StringRef> MaybeStr = parseStr(ArgEntry))
      ValueStr = *MaybeStr;
    else
      return MaybeStr.takeError();

    // Keep the key from the string.
    KeyStr = KeyName;
  }

  if (!KeyStr)
    return error("argument key is missing.", *ArgMap);
  if (!ValueStr)
    return error("argument value is missing.", *ArgMap);

  return Argument{*KeyStr, *ValueStr, Loc};
}

Expected<std::unique_ptr<Remark>> YAMLRemarkParser::next() {
  if (YAMLIt == Stream.end())
    return make_error<EndOfFileError>();

  Expected<std::unique_ptr<Remark>> MaybeResult = parseRemark(*YAMLIt);
  if (!MaybeResult) {
    // Avoid garbage input, set the iterator to the end.
    YAMLIt = Stream.end();
    return MaybeResult.takeError();
  }

  ++YAMLIt;

  return std::move(*MaybeResult);
}

Expected<StringRef> YAMLStrTabRemarkParser::parseStr(yaml::KeyValueNode &Node) {
  auto *Value = dyn_cast<yaml::ScalarNode>(Node.getValue());
  yaml::BlockScalarNode *ValueBlock;
  StringRef Result;
  if (!Value) {
    // Try to parse the value as a block node.
    ValueBlock = dyn_cast<yaml::BlockScalarNode>(Node.getValue());
    if (!ValueBlock)
      return error("expected a value of scalar type.", Node);
    Result = ValueBlock->getValue();
  } else
    Result = Value->getRawValue();
  // If we have a string table, parse it as an unsigned.
  unsigned StrID = 0;
  if (Expected<unsigned> MaybeStrID = parseUnsigned(Node))
    StrID = *MaybeStrID;
  else
    return MaybeStrID.takeError();

  if (Expected<StringRef> Str = (*StrTab)[StrID])
    Result = *Str;
  else
    return Str.takeError();

  Result.consume_front("\'");
  Result.consume_back("\'");

  return Result;
}
