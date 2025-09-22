//===-- XCOFFDump.cpp - XCOFF-specific dumper -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the XCOFF-specific dumper for llvm-objdump.
///
//===----------------------------------------------------------------------===//

#include "XCOFFDump.h"

#include "llvm-objdump.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FormattedStream.h"
#include <algorithm>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::XCOFF;
using namespace llvm::support;

namespace {
class XCOFFDumper : public objdump::Dumper {
public:
  XCOFFDumper(const object::XCOFFObjectFile &O) : Dumper(O) {}
  void printPrivateHeaders() override {}
};
} // namespace

std::unique_ptr<objdump::Dumper>
objdump::createXCOFFDumper(const object::XCOFFObjectFile &Obj) {
  return std::make_unique<XCOFFDumper>(Obj);
}

Error objdump::getXCOFFRelocationValueString(const XCOFFObjectFile &Obj,
                                             const RelocationRef &Rel,
                                             bool SymbolDescription,
                                             SmallVectorImpl<char> &Result) {
  symbol_iterator SymI = Rel.getSymbol();
  if (SymI == Obj.symbol_end())
    return make_error<GenericBinaryError>(
        "invalid symbol reference in relocation entry",
        object_error::parse_failed);

  Expected<StringRef> SymNameOrErr = SymI->getName();
  if (!SymNameOrErr)
    return SymNameOrErr.takeError();

  std::string SymName =
      Demangle ? demangle(*SymNameOrErr) : SymNameOrErr->str();
  if (SymbolDescription)
    SymName = getXCOFFSymbolDescription(createSymbolInfo(Obj, *SymI), SymName);

  Result.append(SymName.begin(), SymName.end());
  return Error::success();
}

std::optional<XCOFF::StorageMappingClass>
objdump::getXCOFFSymbolCsectSMC(const XCOFFObjectFile &Obj,
                                const SymbolRef &Sym) {
  const XCOFFSymbolRef SymRef = Obj.toSymbolRef(Sym.getRawDataRefImpl());

  if (!SymRef.isCsectSymbol())
    return std::nullopt;

  auto CsectAuxEntOrErr = SymRef.getXCOFFCsectAuxRef();
  if (!CsectAuxEntOrErr)
    return std::nullopt;

  return CsectAuxEntOrErr.get().getStorageMappingClass();
}

std::optional<object::SymbolRef>
objdump::getXCOFFSymbolContainingSymbolRef(const XCOFFObjectFile &Obj,
                                           const SymbolRef &Sym) {
  const XCOFFSymbolRef SymRef = Obj.toSymbolRef(Sym.getRawDataRefImpl());
  if (!SymRef.isCsectSymbol())
    return std::nullopt;

  Expected<XCOFFCsectAuxRef> CsectAuxEntOrErr = SymRef.getXCOFFCsectAuxRef();
  if (!CsectAuxEntOrErr || !CsectAuxEntOrErr.get().isLabel())
    return std::nullopt;
  uint32_t Idx =
      static_cast<uint32_t>(CsectAuxEntOrErr.get().getSectionOrLength());
  DataRefImpl DRI;
  DRI.p = Obj.getSymbolByIndex(Idx);
  return SymbolRef(DRI, &Obj);
}

bool objdump::isLabel(const XCOFFObjectFile &Obj, const SymbolRef &Sym) {
  const XCOFFSymbolRef SymRef = Obj.toSymbolRef(Sym.getRawDataRefImpl());
  if (!SymRef.isCsectSymbol())
    return false;

  auto CsectAuxEntOrErr = SymRef.getXCOFFCsectAuxRef();
  if (!CsectAuxEntOrErr)
    return false;

  return CsectAuxEntOrErr.get().isLabel();
}

std::string objdump::getXCOFFSymbolDescription(const SymbolInfoTy &SymbolInfo,
                                               StringRef SymbolName) {
  assert(SymbolInfo.isXCOFF() && "Must be a XCOFFSymInfo.");

  std::string Result;
  // Dummy symbols have no symbol index.
  if (SymbolInfo.XCOFFSymInfo.Index)
    Result =
        ("(idx: " + Twine(*SymbolInfo.XCOFFSymInfo.Index) + ") " + SymbolName)
            .str();
  else
    Result.append(SymbolName.begin(), SymbolName.end());

  if (SymbolInfo.XCOFFSymInfo.StorageMappingClass &&
      !SymbolInfo.XCOFFSymInfo.IsLabel) {
    const XCOFF::StorageMappingClass Smc =
        *SymbolInfo.XCOFFSymInfo.StorageMappingClass;
    Result.append(("[" + XCOFF::getMappingClassString(Smc) + "]").str());
  }

  return Result;
}

#define PRINTBOOL(Prefix, Obj, Field)                                          \
  OS << Prefix << " " << ((Obj.Field()) ? "+" : "-") << #Field

#define PRINTGET(Prefix, Obj, Field)                                           \
  OS << Prefix << " " << #Field << " = "                                       \
     << static_cast<unsigned>(Obj.get##Field())

#define PRINTOPTIONAL(Field)                                                   \
  if (TbTable.get##Field()) {                                                  \
    OS << '\n';                                                                \
    printRawData(Bytes.slice(Index, 4), Address + Index, OS, STI);             \
    Index += 4;                                                                \
    OS << "\t# " << #Field << " = " << *TbTable.get##Field();                  \
  }

void objdump::dumpTracebackTable(ArrayRef<uint8_t> Bytes, uint64_t Address,
                                 formatted_raw_ostream &OS, uint64_t End,
                                 const MCSubtargetInfo &STI,
                                 const XCOFFObjectFile *Obj) {
  uint64_t Index = 0;
  unsigned TabStop = getInstStartColumn(STI) - 1;
  // Print traceback table boundary.
  printRawData(Bytes.slice(Index, 4), Address, OS, STI);
  OS << "\t# Traceback table start\n";
  Index += 4;

  uint64_t Size = End - Address;
  bool Is64Bit = Obj->is64Bit();

  // XCOFFTracebackTable::create modifies the size parameter, so ensure Size
  // isn't changed.
  uint64_t SizeCopy = End - Address;
  Expected<XCOFFTracebackTable> TTOrErr =
      XCOFFTracebackTable::create(Bytes.data() + Index, SizeCopy, Is64Bit);

  if (!TTOrErr) {
    std::string WarningMsgStr;
    raw_string_ostream WarningStream(WarningMsgStr);
    WarningStream << "failure parsing traceback table with address: 0x"
                  << utohexstr(Address) + "\n>>> "
                  << toString(TTOrErr.takeError())
                  << "\n>>> Raw traceback table data is:\n";

    uint64_t LastNonZero = Index;
    for (uint64_t I = Index; I < Size; I += 4)
      if (support::endian::read32be(Bytes.slice(I, 4).data()) != 0)
        LastNonZero = I + 4 > Size ? Size : I + 4;

    if (Size - LastNonZero <= 4)
      LastNonZero = Size;

    formatted_raw_ostream FOS(WarningStream);
    while (Index < LastNonZero) {
      printRawData(Bytes.slice(Index, 4), Address + Index, FOS, STI);
      Index += 4;
      WarningStream << '\n';
    }

    // Print all remaining zeroes as ...
    if (Size - LastNonZero >= 8)
      WarningStream << "\t\t...\n";

    reportWarning(WarningMsgStr, Obj->getFileName());
    return;
  }

  auto PrintBytes = [&](uint64_t N) {
    printRawData(Bytes.slice(Index, N), Address + Index, OS, STI);
    Index += N;
  };

  XCOFFTracebackTable TbTable = *TTOrErr;
  // Print the first of the 8 bytes of mandatory fields.
  PrintBytes(1);
  OS << format("\t# Version = %i", TbTable.getVersion()) << '\n';

  // Print the second of the 8 bytes of mandatory fields.
  PrintBytes(1);
  TracebackTable::LanguageID LangId =
      static_cast<TracebackTable::LanguageID>(TbTable.getLanguageID());
  OS << "\t# Language = " << getNameForTracebackTableLanguageId(LangId) << '\n';

  auto Split = [&]() {
    OS << '\n';
    OS.indent(TabStop);
  };

  // Print the third of the 8 bytes of mandatory fields.
  PrintBytes(1);
  PRINTBOOL("\t#", TbTable, isGlobalLinkage);
  PRINTBOOL(",", TbTable, isOutOfLineEpilogOrPrologue);
  Split();
  PRINTBOOL("\t ", TbTable, hasTraceBackTableOffset);
  PRINTBOOL(",", TbTable, isInternalProcedure);
  Split();
  PRINTBOOL("\t ", TbTable, hasControlledStorage);
  PRINTBOOL(",", TbTable, isTOCless);
  Split();
  PRINTBOOL("\t ", TbTable, isFloatingPointPresent);
  Split();
  PRINTBOOL("\t ", TbTable, isFloatingPointOperationLogOrAbortEnabled);
  OS << '\n';

  // Print the 4th of the 8 bytes of mandatory fields.
  PrintBytes(1);
  PRINTBOOL("\t#", TbTable, isInterruptHandler);
  PRINTBOOL(",", TbTable, isFuncNamePresent);
  PRINTBOOL(",", TbTable, isAllocaUsed);
  Split();
  PRINTGET("\t ", TbTable, OnConditionDirective);
  PRINTBOOL(",", TbTable, isCRSaved);
  PRINTBOOL(",", TbTable, isLRSaved);
  OS << '\n';

  // Print the 5th of the 8 bytes of mandatory fields.
  PrintBytes(1);
  PRINTBOOL("\t#", TbTable, isBackChainStored);
  PRINTBOOL(",", TbTable, isFixup);
  PRINTGET(",", TbTable, NumOfFPRsSaved);
  OS << '\n';

  // Print the 6th of the 8 bytes of mandatory fields.
  PrintBytes(1);
  PRINTBOOL("\t#", TbTable, hasExtensionTable);
  PRINTBOOL(",", TbTable, hasVectorInfo);
  PRINTGET(",", TbTable, NumOfGPRsSaved);
  OS << '\n';

  // Print the 7th of the 8 bytes of mandatory fields.
  PrintBytes(1);
  PRINTGET("\t#", TbTable, NumberOfFixedParms);
  OS << '\n';

  // Print the 8th of the 8 bytes of mandatory fields.
  PrintBytes(1);
  PRINTGET("\t#", TbTable, NumberOfFPParms);
  PRINTBOOL(",", TbTable, hasParmsOnStack);

  PRINTOPTIONAL(ParmsType);
  PRINTOPTIONAL(TraceBackTableOffset);
  PRINTOPTIONAL(HandlerMask);
  PRINTOPTIONAL(NumOfCtlAnchors);

  if (TbTable.getControlledStorageInfoDisp()) {
    SmallVector<uint32_t, 8> Disp = *TbTable.getControlledStorageInfoDisp();
    for (unsigned I = 0; I < Disp.size(); ++I) {
      OS << '\n';
      PrintBytes(4);
      OS << "\t" << (I ? " " : "#") << " ControlledStorageInfoDisp[" << I
         << "] = " << Disp[I];
    }
  }

  // If there is a name, print the function name and function name length.
  if (TbTable.isFuncNamePresent()) {
    uint16_t FunctionNameLen = TbTable.getFunctionName()->size();
    if (FunctionNameLen == 0) {
      OS << '\n';
      reportWarning(
          "the length of the function name must be greater than zero if the "
          "isFuncNamePresent bit is set in the traceback table",
          Obj->getFileName());
      return;
    }

    OS << '\n';
    PrintBytes(2);
    OS << "\t# FunctionNameLen = " << FunctionNameLen;

    uint16_t RemainingBytes = FunctionNameLen;
    bool HasPrinted = false;
    while (RemainingBytes > 0) {
      OS << '\n';
      uint16_t PrintLen = RemainingBytes >= 4 ? 4 : RemainingBytes;
      printRawData(Bytes.slice(Index, PrintLen), Address + Index, OS, STI);
      Index += PrintLen;
      RemainingBytes -= PrintLen;

      if (!HasPrinted) {
        OS << "\t# FunctionName = " << *TbTable.getFunctionName();
        HasPrinted = true;
      }
    }
  }

  if (TbTable.isAllocaUsed()) {
    OS << '\n';
    PrintBytes(1);
    OS << format("\t# AllocaRegister = %u", *TbTable.getAllocaRegister());
  }

  if (TbTable.getVectorExt()) {
    OS << '\n';
    TBVectorExt VecExt = *TbTable.getVectorExt();
    // Print first byte of VectorExt.
    PrintBytes(1);
    PRINTGET("\t#", VecExt, NumberOfVRSaved);
    PRINTBOOL(",", VecExt, isVRSavedOnStack);
    PRINTBOOL(",", VecExt, hasVarArgs);
    OS << '\n';

    // Print the second byte of VectorExt.
    PrintBytes(1);
    PRINTGET("\t#", VecExt, NumberOfVectorParms);
    PRINTBOOL(",", VecExt, hasVMXInstruction);
    OS << '\n';

    PrintBytes(4);
    OS << "\t# VectorParmsInfoString = " << VecExt.getVectorParmsInfo();

    // There are two bytes of padding after vector info.
    OS << '\n';
    PrintBytes(2);
    OS << "\t# Padding";
  }

  if (TbTable.getExtensionTable()) {
    OS << '\n';
    PrintBytes(1);
    ExtendedTBTableFlag Flag =
        static_cast<ExtendedTBTableFlag>(*TbTable.getExtensionTable());
    OS << "\t# ExtensionTable = " << getExtendedTBTableFlagString(Flag);
  }

  if (TbTable.getEhInfoDisp()) {
    // There are 4 bytes alignment before eh info displacement.
    if (Index % 4) {
      OS << '\n';
      PrintBytes(4 - Index % 4);
      OS << "\t# Alignment padding for eh info displacement";
    }
    OS << '\n';
    // The size of the displacement (address) is 4 bytes in 32-bit object files,
    // and 8 bytes in 64-bit object files.
    PrintBytes(4);
    OS << "\t# EH info displacement";
    if (Is64Bit) {
      OS << '\n';
      PrintBytes(4);
    }
  }

  OS << '\n';
  if (End == Address + Index)
    return;

  Size = End - Address;

  const char *LineSuffix = "\t# Padding\n";
  auto IsWordZero = [&](uint64_t WordPos) {
    if (WordPos >= Size)
      return false;
    uint64_t LineLength = std::min(4 - WordPos % 4, Size - WordPos);
    return std::all_of(Bytes.begin() + WordPos,
                       Bytes.begin() + WordPos + LineLength,
                       [](uint8_t Byte) { return Byte == 0; });
  };

  bool AreWordsZero[] = {IsWordZero(Index), IsWordZero(alignTo(Index, 4) + 4),
                         IsWordZero(alignTo(Index, 4) + 8)};
  bool ShouldPrintLine = true;
  while (true) {
    // Determine the length of the line (4, except for the first line, which
    // will be just enough to align to the word boundary, and the last line,
    // which will be the remainder of the data).
    uint64_t LineLength = std::min(4 - Index % 4, Size - Index);
    if (ShouldPrintLine) {
      // Print the line.
      printRawData(Bytes.slice(Index, LineLength), Address + Index, OS, STI);
      OS << LineSuffix;
      LineSuffix = "\n";
    }

    Index += LineLength;
    if (Index == Size)
      return;

    // For 3 or more consecutive lines of zeros, skip all but the first one, and
    // replace them with "...".
    if (AreWordsZero[0] && AreWordsZero[1] && AreWordsZero[2]) {
      if (ShouldPrintLine)
        OS << std::string(8, ' ') << "...\n";
      ShouldPrintLine = false;
    } else if (!AreWordsZero[1]) {
      // We have reached the end of a skipped block of zeros.
      ShouldPrintLine = true;
    }
    AreWordsZero[0] = AreWordsZero[1];
    AreWordsZero[1] = AreWordsZero[2];
    AreWordsZero[2] = IsWordZero(Index + 8);
  }
}
#undef PRINTBOOL
#undef PRINTGET
#undef PRINTOPTIONAL
