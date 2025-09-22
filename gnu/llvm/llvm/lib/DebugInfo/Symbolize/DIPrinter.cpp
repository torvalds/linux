//===- lib/DebugInfo/Symbolize/DIPrinter.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the DIPrinter class, which is responsible for printing
// structures defined in DebugInfo/DIContext.h
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/Symbolize/DIPrinter.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {
namespace symbolize {

class SourceCode {
  std::unique_ptr<MemoryBuffer> MemBuf;

  std::optional<StringRef>
  load(StringRef FileName, const std::optional<StringRef> &EmbeddedSource) {
    if (Lines <= 0)
      return std::nullopt;

    if (EmbeddedSource)
      return EmbeddedSource;
    else {
      ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
          MemoryBuffer::getFile(FileName);
      if (!BufOrErr)
        return std::nullopt;
      MemBuf = std::move(*BufOrErr);
      return MemBuf->getBuffer();
    }
  }

  std::optional<StringRef> pruneSource(const std::optional<StringRef> &Source) {
    if (!Source)
      return std::nullopt;
    size_t FirstLinePos = StringRef::npos, Pos = 0;
    for (int64_t L = 1; L <= LastLine; ++L, ++Pos) {
      if (L == FirstLine)
        FirstLinePos = Pos;
      Pos = Source->find('\n', Pos);
      if (Pos == StringRef::npos)
        break;
    }
    if (FirstLinePos == StringRef::npos)
      return std::nullopt;
    return Source->substr(FirstLinePos, (Pos == StringRef::npos)
                                            ? StringRef::npos
                                            : Pos - FirstLinePos);
  }

public:
  const int64_t Line;
  const int Lines;
  const int64_t FirstLine;
  const int64_t LastLine;
  const std::optional<StringRef> PrunedSource;

  SourceCode(StringRef FileName, int64_t Line, int Lines,
             const std::optional<StringRef> &EmbeddedSource =
                 std::optional<StringRef>())
      : Line(Line), Lines(Lines),
        FirstLine(std::max(static_cast<int64_t>(1), Line - Lines / 2)),
        LastLine(FirstLine + Lines - 1),
        PrunedSource(pruneSource(load(FileName, EmbeddedSource))) {}

  void format(raw_ostream &OS) {
    if (!PrunedSource)
      return;
    size_t MaxLineNumberWidth = std::ceil(std::log10(LastLine));
    int64_t L = FirstLine;
    for (size_t Pos = 0; Pos < PrunedSource->size(); ++L) {
      size_t PosEnd = PrunedSource->find('\n', Pos);
      StringRef String = PrunedSource->substr(
          Pos, (PosEnd == StringRef::npos) ? StringRef::npos : (PosEnd - Pos));
      if (String.ends_with("\r"))
        String = String.drop_back(1);
      OS << format_decimal(L, MaxLineNumberWidth);
      if (L == Line)
        OS << " >: ";
      else
        OS << "  : ";
      OS << String << '\n';
      if (PosEnd == StringRef::npos)
        break;
      Pos = PosEnd + 1;
    }
  }
};

void PlainPrinterBase::printHeader(std::optional<uint64_t> Address) {
  if (Address.has_value() && Config.PrintAddress) {
    OS << "0x";
    OS.write_hex(*Address);
    StringRef Delimiter = Config.Pretty ? ": " : "\n";
    OS << Delimiter;
  }
}

// Prints source code around in the FileName the Line.
void PlainPrinterBase::printContext(SourceCode SourceCode) {
  SourceCode.format(OS);
}

void PlainPrinterBase::printFunctionName(StringRef FunctionName, bool Inlined) {
  if (Config.PrintFunctions) {
    if (FunctionName == DILineInfo::BadString)
      FunctionName = DILineInfo::Addr2LineBadString;
    StringRef Delimiter = Config.Pretty ? " at " : "\n";
    StringRef Prefix = (Config.Pretty && Inlined) ? " (inlined by) " : "";
    OS << Prefix << FunctionName << Delimiter;
  }
}

void LLVMPrinter::printSimpleLocation(StringRef Filename,
                                      const DILineInfo &Info) {
  OS << Filename << ':' << Info.Line << ':' << Info.Column << '\n';
  printContext(
      SourceCode(Filename, Info.Line, Config.SourceContextLines, Info.Source));
}

void GNUPrinter::printSimpleLocation(StringRef Filename,
                                     const DILineInfo &Info) {
  OS << Filename << ':' << Info.Line;
  if (Info.Discriminator)
    OS << " (discriminator " << Info.Discriminator << ')';
  OS << '\n';
  printContext(
      SourceCode(Filename, Info.Line, Config.SourceContextLines, Info.Source));
}

void PlainPrinterBase::printVerbose(StringRef Filename,
                                    const DILineInfo &Info) {
  OS << "  Filename: " << Filename << '\n';
  if (Info.StartLine) {
    OS << "  Function start filename: " << Info.StartFileName << '\n';
    OS << "  Function start line: " << Info.StartLine << '\n';
  }
  printStartAddress(Info);
  OS << "  Line: " << Info.Line << '\n';
  OS << "  Column: " << Info.Column << '\n';
  if (Info.Discriminator)
    OS << "  Discriminator: " << Info.Discriminator << '\n';
}

void LLVMPrinter::printStartAddress(const DILineInfo &Info) {
  if (Info.StartAddress) {
    OS << "  Function start address: 0x";
    OS.write_hex(*Info.StartAddress);
    OS << '\n';
  }
}

void LLVMPrinter::printFooter() { OS << '\n'; }

void PlainPrinterBase::print(const DILineInfo &Info, bool Inlined) {
  printFunctionName(Info.FunctionName, Inlined);
  StringRef Filename = Info.FileName;
  if (Filename == DILineInfo::BadString)
    Filename = DILineInfo::Addr2LineBadString;
  if (Config.Verbose)
    printVerbose(Filename, Info);
  else
    printSimpleLocation(Filename, Info);
}

void PlainPrinterBase::print(const Request &Request, const DILineInfo &Info) {
  printHeader(Request.Address);
  print(Info, false);
  printFooter();
}

void PlainPrinterBase::print(const Request &Request,
                             const DIInliningInfo &Info) {
  printHeader(*Request.Address);
  uint32_t FramesNum = Info.getNumberOfFrames();
  if (FramesNum == 0)
    print(DILineInfo(), false);
  else
    for (uint32_t I = 0; I < FramesNum; ++I)
      print(Info.getFrame(I), I > 0);
  printFooter();
}

void PlainPrinterBase::print(const Request &Request, const DIGlobal &Global) {
  printHeader(*Request.Address);
  StringRef Name = Global.Name;
  if (Name == DILineInfo::BadString)
    Name = DILineInfo::Addr2LineBadString;
  OS << Name << "\n";
  OS << Global.Start << " " << Global.Size << "\n";
  if (Global.DeclFile.empty())
    OS << "??:?\n";
  else
    OS << Global.DeclFile << ":" << Global.DeclLine << "\n";
  printFooter();
}

void PlainPrinterBase::print(const Request &Request,
                             const std::vector<DILocal> &Locals) {
  printHeader(*Request.Address);
  if (Locals.empty())
    OS << DILineInfo::Addr2LineBadString << '\n';
  else
    for (const DILocal &L : Locals) {
      if (L.FunctionName.empty())
        OS << DILineInfo::Addr2LineBadString;
      else
        OS << L.FunctionName;
      OS << '\n';

      if (L.Name.empty())
        OS << DILineInfo::Addr2LineBadString;
      else
        OS << L.Name;
      OS << '\n';

      if (L.DeclFile.empty())
        OS << DILineInfo::Addr2LineBadString;
      else
        OS << L.DeclFile;

      OS << ':' << L.DeclLine << '\n';

      if (L.FrameOffset)
        OS << *L.FrameOffset;
      else
        OS << DILineInfo::Addr2LineBadString;
      OS << ' ';

      if (L.Size)
        OS << *L.Size;
      else
        OS << DILineInfo::Addr2LineBadString;
      OS << ' ';

      if (L.TagOffset)
        OS << *L.TagOffset;
      else
        OS << DILineInfo::Addr2LineBadString;
      OS << '\n';
    }
  printFooter();
}

void PlainPrinterBase::print(const Request &Request,
                             const std::vector<DILineInfo> &Locations) {
  if (Locations.empty()) {
    print(Request, DILineInfo());
  } else {
    for (const DILineInfo &L : Locations)
      print(L, false);
    printFooter();
  }
}

bool PlainPrinterBase::printError(const Request &Request,
                                  const ErrorInfoBase &ErrorInfo) {
  ErrHandler(ErrorInfo, Request.ModuleName);
  // Print an empty struct too.
  return true;
}

static std::string toHex(uint64_t V) {
  return ("0x" + Twine::utohexstr(V)).str();
}

static json::Object toJSON(const Request &Request, StringRef ErrorMsg = "") {
  json::Object Json({{"ModuleName", Request.ModuleName.str()}});
  if (!Request.Symbol.empty())
    Json["SymName"] = Request.Symbol.str();
  if (Request.Address)
    Json["Address"] = toHex(*Request.Address);
  if (!ErrorMsg.empty())
    Json["Error"] = json::Object({{"Message", ErrorMsg.str()}});
  return Json;
}

static json::Object toJSON(const DILineInfo &LineInfo) {
  return json::Object(
      {{"FunctionName", LineInfo.FunctionName != DILineInfo::BadString
                            ? LineInfo.FunctionName
                            : ""},
       {"StartFileName", LineInfo.StartFileName != DILineInfo::BadString
                             ? LineInfo.StartFileName
                             : ""},
       {"StartLine", LineInfo.StartLine},
       {"StartAddress",
        LineInfo.StartAddress ? toHex(*LineInfo.StartAddress) : ""},
       {"FileName",
        LineInfo.FileName != DILineInfo::BadString ? LineInfo.FileName : ""},
       {"Line", LineInfo.Line},
       {"Column", LineInfo.Column},
       {"Discriminator", LineInfo.Discriminator}});
}

void JSONPrinter::print(const Request &Request, const DILineInfo &Info) {
  DIInliningInfo InliningInfo;
  InliningInfo.addFrame(Info);
  print(Request, InliningInfo);
}

void JSONPrinter::print(const Request &Request, const DIInliningInfo &Info) {
  json::Array Array;
  for (uint32_t I = 0, N = Info.getNumberOfFrames(); I < N; ++I) {
    const DILineInfo &LineInfo = Info.getFrame(I);
    json::Object Object = toJSON(LineInfo);
    SourceCode SourceCode(LineInfo.FileName, LineInfo.Line,
                          Config.SourceContextLines, LineInfo.Source);
    std::string FormattedSource;
    raw_string_ostream Stream(FormattedSource);
    SourceCode.format(Stream);
    if (!FormattedSource.empty())
      Object["Source"] = std::move(FormattedSource);
    Array.push_back(std::move(Object));
  }
  json::Object Json = toJSON(Request);
  Json["Symbol"] = std::move(Array);
  if (ObjectList)
    ObjectList->push_back(std::move(Json));
  else
    printJSON(std::move(Json));
}

void JSONPrinter::print(const Request &Request, const DIGlobal &Global) {
  json::Object Data(
      {{"Name", Global.Name != DILineInfo::BadString ? Global.Name : ""},
       {"Start", toHex(Global.Start)},
       {"Size", toHex(Global.Size)}});
  json::Object Json = toJSON(Request);
  Json["Data"] = std::move(Data);
  if (ObjectList)
    ObjectList->push_back(std::move(Json));
  else
    printJSON(std::move(Json));
}

void JSONPrinter::print(const Request &Request,
                        const std::vector<DILocal> &Locals) {
  json::Array Frame;
  for (const DILocal &Local : Locals) {
    json::Object FrameObject(
        {{"FunctionName", Local.FunctionName},
         {"Name", Local.Name},
         {"DeclFile", Local.DeclFile},
         {"DeclLine", int64_t(Local.DeclLine)},
         {"Size", Local.Size ? toHex(*Local.Size) : ""},
         {"TagOffset", Local.TagOffset ? toHex(*Local.TagOffset) : ""}});
    if (Local.FrameOffset)
      FrameObject["FrameOffset"] = *Local.FrameOffset;
    Frame.push_back(std::move(FrameObject));
  }
  json::Object Json = toJSON(Request);
  Json["Frame"] = std::move(Frame);
  if (ObjectList)
    ObjectList->push_back(std::move(Json));
  else
    printJSON(std::move(Json));
}

void JSONPrinter::print(const Request &Request,
                        const std::vector<DILineInfo> &Locations) {
  json::Array Definitions;
  for (const DILineInfo &L : Locations)
    Definitions.push_back(toJSON(L));
  json::Object Json = toJSON(Request);
  Json["Loc"] = std::move(Definitions);
  if (ObjectList)
    ObjectList->push_back(std::move(Json));
  else
    printJSON(std::move(Json));
}

bool JSONPrinter::printError(const Request &Request,
                             const ErrorInfoBase &ErrorInfo) {
  json::Object Json = toJSON(Request, ErrorInfo.message());
  if (ObjectList)
    ObjectList->push_back(std::move(Json));
  else
    printJSON(std::move(Json));
  return false;
}

void JSONPrinter::listBegin() {
  assert(!ObjectList);
  ObjectList = std::make_unique<json::Array>();
}

void JSONPrinter::listEnd() {
  assert(ObjectList);
  printJSON(std::move(*ObjectList));
  ObjectList.reset();
}

} // end namespace symbolize
} // end namespace llvm
