//===- WasmObjectFile.cpp - Wasm object file implementation ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Object/Wasm.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>

#define DEBUG_TYPE "wasm-object"

using namespace llvm;
using namespace object;

void WasmSymbol::print(raw_ostream &Out) const {
  Out << "Name=" << Info.Name
      << ", Kind=" << toString(wasm::WasmSymbolType(Info.Kind)) << ", Flags=0x"
      << Twine::utohexstr(Info.Flags) << " [";
  switch (getBinding()) {
    case wasm::WASM_SYMBOL_BINDING_GLOBAL: Out << "global"; break;
    case wasm::WASM_SYMBOL_BINDING_LOCAL: Out << "local"; break;
    case wasm::WASM_SYMBOL_BINDING_WEAK: Out << "weak"; break;
  }
  if (isHidden()) {
    Out << ", hidden";
  } else {
    Out << ", default";
  }
  Out << "]";
  if (!isTypeData()) {
    Out << ", ElemIndex=" << Info.ElementIndex;
  } else if (isDefined()) {
    Out << ", Segment=" << Info.DataRef.Segment;
    Out << ", Offset=" << Info.DataRef.Offset;
    Out << ", Size=" << Info.DataRef.Size;
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void WasmSymbol::dump() const { print(dbgs()); }
#endif

Expected<std::unique_ptr<WasmObjectFile>>
ObjectFile::createWasmObjectFile(MemoryBufferRef Buffer) {
  Error Err = Error::success();
  auto ObjectFile = std::make_unique<WasmObjectFile>(Buffer, Err);
  if (Err)
    return std::move(Err);

  return std::move(ObjectFile);
}

#define VARINT7_MAX ((1 << 7) - 1)
#define VARINT7_MIN (-(1 << 7))
#define VARUINT7_MAX (1 << 7)
#define VARUINT1_MAX (1)

static uint8_t readUint8(WasmObjectFile::ReadContext &Ctx) {
  if (Ctx.Ptr == Ctx.End)
    report_fatal_error("EOF while reading uint8");
  return *Ctx.Ptr++;
}

static uint32_t readUint32(WasmObjectFile::ReadContext &Ctx) {
  if (Ctx.Ptr + 4 > Ctx.End)
    report_fatal_error("EOF while reading uint32");
  uint32_t Result = support::endian::read32le(Ctx.Ptr);
  Ctx.Ptr += 4;
  return Result;
}

static int32_t readFloat32(WasmObjectFile::ReadContext &Ctx) {
  if (Ctx.Ptr + 4 > Ctx.End)
    report_fatal_error("EOF while reading float64");
  int32_t Result = 0;
  memcpy(&Result, Ctx.Ptr, sizeof(Result));
  Ctx.Ptr += sizeof(Result);
  return Result;
}

static int64_t readFloat64(WasmObjectFile::ReadContext &Ctx) {
  if (Ctx.Ptr + 8 > Ctx.End)
    report_fatal_error("EOF while reading float64");
  int64_t Result = 0;
  memcpy(&Result, Ctx.Ptr, sizeof(Result));
  Ctx.Ptr += sizeof(Result);
  return Result;
}

static uint64_t readULEB128(WasmObjectFile::ReadContext &Ctx) {
  unsigned Count;
  const char *Error = nullptr;
  uint64_t Result = decodeULEB128(Ctx.Ptr, &Count, Ctx.End, &Error);
  if (Error)
    report_fatal_error(Error);
  Ctx.Ptr += Count;
  return Result;
}

static StringRef readString(WasmObjectFile::ReadContext &Ctx) {
  uint32_t StringLen = readULEB128(Ctx);
  if (Ctx.Ptr + StringLen > Ctx.End)
    report_fatal_error("EOF while reading string");
  StringRef Return =
      StringRef(reinterpret_cast<const char *>(Ctx.Ptr), StringLen);
  Ctx.Ptr += StringLen;
  return Return;
}

static int64_t readLEB128(WasmObjectFile::ReadContext &Ctx) {
  unsigned Count;
  const char *Error = nullptr;
  uint64_t Result = decodeSLEB128(Ctx.Ptr, &Count, Ctx.End, &Error);
  if (Error)
    report_fatal_error(Error);
  Ctx.Ptr += Count;
  return Result;
}

static uint8_t readVaruint1(WasmObjectFile::ReadContext &Ctx) {
  int64_t Result = readLEB128(Ctx);
  if (Result > VARUINT1_MAX || Result < 0)
    report_fatal_error("LEB is outside Varuint1 range");
  return Result;
}

static int32_t readVarint32(WasmObjectFile::ReadContext &Ctx) {
  int64_t Result = readLEB128(Ctx);
  if (Result > INT32_MAX || Result < INT32_MIN)
    report_fatal_error("LEB is outside Varint32 range");
  return Result;
}

static uint32_t readVaruint32(WasmObjectFile::ReadContext &Ctx) {
  uint64_t Result = readULEB128(Ctx);
  if (Result > UINT32_MAX)
    report_fatal_error("LEB is outside Varuint32 range");
  return Result;
}

static int64_t readVarint64(WasmObjectFile::ReadContext &Ctx) {
  return readLEB128(Ctx);
}

static uint64_t readVaruint64(WasmObjectFile::ReadContext &Ctx) {
  return readULEB128(Ctx);
}

static uint8_t readOpcode(WasmObjectFile::ReadContext &Ctx) {
  return readUint8(Ctx);
}

static wasm::ValType parseValType(WasmObjectFile::ReadContext &Ctx,
                                  uint32_t Code) {
  // only directly encoded FUNCREF/EXTERNREF/EXNREF are supported
  // (not ref null func, ref null extern, or ref null exn)
  switch (Code) {
  case wasm::WASM_TYPE_I32:
  case wasm::WASM_TYPE_I64:
  case wasm::WASM_TYPE_F32:
  case wasm::WASM_TYPE_F64:
  case wasm::WASM_TYPE_V128:
  case wasm::WASM_TYPE_FUNCREF:
  case wasm::WASM_TYPE_EXTERNREF:
  case wasm::WASM_TYPE_EXNREF:
    return wasm::ValType(Code);
  }
  if (Code == wasm::WASM_TYPE_NULLABLE || Code == wasm::WASM_TYPE_NONNULLABLE) {
    /* Discard HeapType */ readVarint64(Ctx);
  }
  return wasm::ValType(wasm::ValType::OTHERREF);
}

static Error readInitExpr(wasm::WasmInitExpr &Expr,
                          WasmObjectFile::ReadContext &Ctx) {
  auto Start = Ctx.Ptr;

  Expr.Extended = false;
  Expr.Inst.Opcode = readOpcode(Ctx);
  switch (Expr.Inst.Opcode) {
  case wasm::WASM_OPCODE_I32_CONST:
    Expr.Inst.Value.Int32 = readVarint32(Ctx);
    break;
  case wasm::WASM_OPCODE_I64_CONST:
    Expr.Inst.Value.Int64 = readVarint64(Ctx);
    break;
  case wasm::WASM_OPCODE_F32_CONST:
    Expr.Inst.Value.Float32 = readFloat32(Ctx);
    break;
  case wasm::WASM_OPCODE_F64_CONST:
    Expr.Inst.Value.Float64 = readFloat64(Ctx);
    break;
  case wasm::WASM_OPCODE_GLOBAL_GET:
    Expr.Inst.Value.Global = readULEB128(Ctx);
    break;
  case wasm::WASM_OPCODE_REF_NULL: {
    /* Discard type */ parseValType(Ctx, readVaruint32(Ctx));
    break;
  }
  default:
    Expr.Extended = true;
  }

  if (!Expr.Extended) {
    uint8_t EndOpcode = readOpcode(Ctx);
    if (EndOpcode != wasm::WASM_OPCODE_END)
      Expr.Extended = true;
  }

  if (Expr.Extended) {
    Ctx.Ptr = Start;
    while (true) {
      uint8_t Opcode = readOpcode(Ctx);
      switch (Opcode) {
      case wasm::WASM_OPCODE_I32_CONST:
      case wasm::WASM_OPCODE_GLOBAL_GET:
      case wasm::WASM_OPCODE_REF_NULL:
      case wasm::WASM_OPCODE_REF_FUNC:
      case wasm::WASM_OPCODE_I64_CONST:
        readULEB128(Ctx);
        break;
      case wasm::WASM_OPCODE_F32_CONST:
        readFloat32(Ctx);
        break;
      case wasm::WASM_OPCODE_F64_CONST:
        readFloat64(Ctx);
        break;
      case wasm::WASM_OPCODE_I32_ADD:
      case wasm::WASM_OPCODE_I32_SUB:
      case wasm::WASM_OPCODE_I32_MUL:
      case wasm::WASM_OPCODE_I64_ADD:
      case wasm::WASM_OPCODE_I64_SUB:
      case wasm::WASM_OPCODE_I64_MUL:
        break;
      case wasm::WASM_OPCODE_GC_PREFIX:
        break;
      // The GC opcodes are in a separate (prefixed space). This flat switch
      // structure works as long as there is no overlap between the GC and
      // general opcodes used in init exprs.
      case wasm::WASM_OPCODE_STRUCT_NEW:
      case wasm::WASM_OPCODE_STRUCT_NEW_DEFAULT:
      case wasm::WASM_OPCODE_ARRAY_NEW:
      case wasm::WASM_OPCODE_ARRAY_NEW_DEFAULT:
        readULEB128(Ctx); // heap type index
        break;
      case wasm::WASM_OPCODE_ARRAY_NEW_FIXED:
        readULEB128(Ctx); // heap type index
        readULEB128(Ctx); // array size
        break;
      case wasm::WASM_OPCODE_REF_I31:
        break;
      case wasm::WASM_OPCODE_END:
        Expr.Body = ArrayRef<uint8_t>(Start, Ctx.Ptr - Start);
        return Error::success();
      default:
        return make_error<GenericBinaryError>(
            Twine("invalid opcode in init_expr: ") + Twine(unsigned(Opcode)),
            object_error::parse_failed);
      }
    }
  }

  return Error::success();
}

static wasm::WasmLimits readLimits(WasmObjectFile::ReadContext &Ctx) {
  wasm::WasmLimits Result;
  Result.Flags = readVaruint32(Ctx);
  Result.Minimum = readVaruint64(Ctx);
  if (Result.Flags & wasm::WASM_LIMITS_FLAG_HAS_MAX)
    Result.Maximum = readVaruint64(Ctx);
  return Result;
}

static wasm::WasmTableType readTableType(WasmObjectFile::ReadContext &Ctx) {
  wasm::WasmTableType TableType;
  auto ElemType = parseValType(Ctx, readVaruint32(Ctx));
  TableType.ElemType = ElemType;
  TableType.Limits = readLimits(Ctx);
  return TableType;
}

static Error readSection(WasmSection &Section, WasmObjectFile::ReadContext &Ctx,
                         WasmSectionOrderChecker &Checker) {
  Section.Type = readUint8(Ctx);
  LLVM_DEBUG(dbgs() << "readSection type=" << Section.Type << "\n");
  // When reading the section's size, store the size of the LEB used to encode
  // it. This allows objcopy/strip to reproduce the binary identically.
  const uint8_t *PreSizePtr = Ctx.Ptr;
  uint32_t Size = readVaruint32(Ctx);
  Section.HeaderSecSizeEncodingLen = Ctx.Ptr - PreSizePtr;
  Section.Offset = Ctx.Ptr - Ctx.Start;
  if (Size == 0)
    return make_error<StringError>("zero length section",
                                   object_error::parse_failed);
  if (Ctx.Ptr + Size > Ctx.End)
    return make_error<StringError>("section too large",
                                   object_error::parse_failed);
  if (Section.Type == wasm::WASM_SEC_CUSTOM) {
    WasmObjectFile::ReadContext SectionCtx;
    SectionCtx.Start = Ctx.Ptr;
    SectionCtx.Ptr = Ctx.Ptr;
    SectionCtx.End = Ctx.Ptr + Size;

    Section.Name = readString(SectionCtx);

    uint32_t SectionNameSize = SectionCtx.Ptr - SectionCtx.Start;
    Ctx.Ptr += SectionNameSize;
    Size -= SectionNameSize;
  }

  if (!Checker.isValidSectionOrder(Section.Type, Section.Name)) {
    return make_error<StringError>("out of order section type: " +
                                       llvm::to_string(Section.Type),
                                   object_error::parse_failed);
  }

  Section.Content = ArrayRef<uint8_t>(Ctx.Ptr, Size);
  Ctx.Ptr += Size;
  return Error::success();
}

WasmObjectFile::WasmObjectFile(MemoryBufferRef Buffer, Error &Err)
    : ObjectFile(Binary::ID_Wasm, Buffer) {
  ErrorAsOutParameter ErrAsOutParam(&Err);
  Header.Magic = getData().substr(0, 4);
  if (Header.Magic != StringRef("\0asm", 4)) {
    Err = make_error<StringError>("invalid magic number",
                                  object_error::parse_failed);
    return;
  }

  ReadContext Ctx;
  Ctx.Start = getData().bytes_begin();
  Ctx.Ptr = Ctx.Start + 4;
  Ctx.End = Ctx.Start + getData().size();

  if (Ctx.Ptr + 4 > Ctx.End) {
    Err = make_error<StringError>("missing version number",
                                  object_error::parse_failed);
    return;
  }

  Header.Version = readUint32(Ctx);
  if (Header.Version != wasm::WasmVersion) {
    Err = make_error<StringError>("invalid version number: " +
                                      Twine(Header.Version),
                                  object_error::parse_failed);
    return;
  }

  WasmSectionOrderChecker Checker;
  while (Ctx.Ptr < Ctx.End) {
    WasmSection Sec;
    if ((Err = readSection(Sec, Ctx, Checker)))
      return;
    if ((Err = parseSection(Sec)))
      return;

    Sections.push_back(Sec);
  }
}

Error WasmObjectFile::parseSection(WasmSection &Sec) {
  ReadContext Ctx;
  Ctx.Start = Sec.Content.data();
  Ctx.End = Ctx.Start + Sec.Content.size();
  Ctx.Ptr = Ctx.Start;
  switch (Sec.Type) {
  case wasm::WASM_SEC_CUSTOM:
    return parseCustomSection(Sec, Ctx);
  case wasm::WASM_SEC_TYPE:
    return parseTypeSection(Ctx);
  case wasm::WASM_SEC_IMPORT:
    return parseImportSection(Ctx);
  case wasm::WASM_SEC_FUNCTION:
    return parseFunctionSection(Ctx);
  case wasm::WASM_SEC_TABLE:
    return parseTableSection(Ctx);
  case wasm::WASM_SEC_MEMORY:
    return parseMemorySection(Ctx);
  case wasm::WASM_SEC_TAG:
    return parseTagSection(Ctx);
  case wasm::WASM_SEC_GLOBAL:
    return parseGlobalSection(Ctx);
  case wasm::WASM_SEC_EXPORT:
    return parseExportSection(Ctx);
  case wasm::WASM_SEC_START:
    return parseStartSection(Ctx);
  case wasm::WASM_SEC_ELEM:
    return parseElemSection(Ctx);
  case wasm::WASM_SEC_CODE:
    return parseCodeSection(Ctx);
  case wasm::WASM_SEC_DATA:
    return parseDataSection(Ctx);
  case wasm::WASM_SEC_DATACOUNT:
    return parseDataCountSection(Ctx);
  default:
    return make_error<GenericBinaryError>(
        "invalid section type: " + Twine(Sec.Type), object_error::parse_failed);
  }
}

Error WasmObjectFile::parseDylinkSection(ReadContext &Ctx) {
  // Legacy "dylink" section support.
  // See parseDylink0Section for the current "dylink.0" section parsing.
  HasDylinkSection = true;
  DylinkInfo.MemorySize = readVaruint32(Ctx);
  DylinkInfo.MemoryAlignment = readVaruint32(Ctx);
  DylinkInfo.TableSize = readVaruint32(Ctx);
  DylinkInfo.TableAlignment = readVaruint32(Ctx);
  uint32_t Count = readVaruint32(Ctx);
  while (Count--) {
    DylinkInfo.Needed.push_back(readString(Ctx));
  }

  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("dylink section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseDylink0Section(ReadContext &Ctx) {
  // See
  // https://github.com/WebAssembly/tool-conventions/blob/main/DynamicLinking.md
  HasDylinkSection = true;

  const uint8_t *OrigEnd = Ctx.End;
  while (Ctx.Ptr < OrigEnd) {
    Ctx.End = OrigEnd;
    uint8_t Type = readUint8(Ctx);
    uint32_t Size = readVaruint32(Ctx);
    LLVM_DEBUG(dbgs() << "readSubsection type=" << int(Type) << " size=" << Size
                      << "\n");
    Ctx.End = Ctx.Ptr + Size;
    uint32_t Count;
    switch (Type) {
    case wasm::WASM_DYLINK_MEM_INFO:
      DylinkInfo.MemorySize = readVaruint32(Ctx);
      DylinkInfo.MemoryAlignment = readVaruint32(Ctx);
      DylinkInfo.TableSize = readVaruint32(Ctx);
      DylinkInfo.TableAlignment = readVaruint32(Ctx);
      break;
    case wasm::WASM_DYLINK_NEEDED:
      Count = readVaruint32(Ctx);
      while (Count--) {
        DylinkInfo.Needed.push_back(readString(Ctx));
      }
      break;
    case wasm::WASM_DYLINK_EXPORT_INFO: {
      uint32_t Count = readVaruint32(Ctx);
      while (Count--) {
        DylinkInfo.ExportInfo.push_back({readString(Ctx), readVaruint32(Ctx)});
      }
      break;
    }
    case wasm::WASM_DYLINK_IMPORT_INFO: {
      uint32_t Count = readVaruint32(Ctx);
      while (Count--) {
        DylinkInfo.ImportInfo.push_back(
            {readString(Ctx), readString(Ctx), readVaruint32(Ctx)});
      }
      break;
    }
    default:
      LLVM_DEBUG(dbgs() << "unknown dylink.0 sub-section: " << Type << "\n");
      Ctx.Ptr += Size;
      break;
    }
    if (Ctx.Ptr != Ctx.End) {
      return make_error<GenericBinaryError>(
          "dylink.0 sub-section ended prematurely", object_error::parse_failed);
    }
  }

  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("dylink.0 section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseNameSection(ReadContext &Ctx) {
  llvm::DenseSet<uint64_t> SeenFunctions;
  llvm::DenseSet<uint64_t> SeenGlobals;
  llvm::DenseSet<uint64_t> SeenSegments;

  // If we have linking section (symbol table) or if we are parsing a DSO
  // then we don't use the name section for symbol information.
  bool PopulateSymbolTable = !HasLinkingSection && !HasDylinkSection;

  // If we are using the name section for symbol information then it will
  // supersede any symbols created by the export section.
  if (PopulateSymbolTable)
    Symbols.clear();

  while (Ctx.Ptr < Ctx.End) {
    uint8_t Type = readUint8(Ctx);
    uint32_t Size = readVaruint32(Ctx);
    const uint8_t *SubSectionEnd = Ctx.Ptr + Size;

    switch (Type) {
    case wasm::WASM_NAMES_FUNCTION:
    case wasm::WASM_NAMES_GLOBAL:
    case wasm::WASM_NAMES_DATA_SEGMENT: {
      uint32_t Count = readVaruint32(Ctx);
      while (Count--) {
        uint32_t Index = readVaruint32(Ctx);
        StringRef Name = readString(Ctx);
        wasm::NameType nameType = wasm::NameType::FUNCTION;
        wasm::WasmSymbolInfo Info{Name,
                                  /*Kind */ wasm::WASM_SYMBOL_TYPE_FUNCTION,
                                  /* Flags */ 0,
                                  /* ImportModule */ std::nullopt,
                                  /* ImportName */ std::nullopt,
                                  /* ExportName */ std::nullopt,
                                  {/* ElementIndex */ Index}};
        const wasm::WasmSignature *Signature = nullptr;
        const wasm::WasmGlobalType *GlobalType = nullptr;
        const wasm::WasmTableType *TableType = nullptr;
        if (Type == wasm::WASM_NAMES_FUNCTION) {
          if (!SeenFunctions.insert(Index).second)
            return make_error<GenericBinaryError>(
                "function named more than once", object_error::parse_failed);
          if (!isValidFunctionIndex(Index) || Name.empty())
            return make_error<GenericBinaryError>("invalid function name entry",
                                                  object_error::parse_failed);

          if (isDefinedFunctionIndex(Index)) {
            wasm::WasmFunction &F = getDefinedFunction(Index);
            F.DebugName = Name;
            Signature = &Signatures[F.SigIndex];
            if (F.ExportName) {
              Info.ExportName = F.ExportName;
              Info.Flags |= wasm::WASM_SYMBOL_BINDING_GLOBAL;
            } else {
              Info.Flags |= wasm::WASM_SYMBOL_BINDING_LOCAL;
            }
          } else {
            Info.Flags |= wasm::WASM_SYMBOL_UNDEFINED;
          }
        } else if (Type == wasm::WASM_NAMES_GLOBAL) {
          if (!SeenGlobals.insert(Index).second)
            return make_error<GenericBinaryError>("global named more than once",
                                                  object_error::parse_failed);
          if (!isValidGlobalIndex(Index) || Name.empty())
            return make_error<GenericBinaryError>("invalid global name entry",
                                                  object_error::parse_failed);
          nameType = wasm::NameType::GLOBAL;
          Info.Kind = wasm::WASM_SYMBOL_TYPE_GLOBAL;
          if (isDefinedGlobalIndex(Index)) {
            GlobalType = &getDefinedGlobal(Index).Type;
          } else {
            Info.Flags |= wasm::WASM_SYMBOL_UNDEFINED;
          }
        } else {
          if (!SeenSegments.insert(Index).second)
            return make_error<GenericBinaryError>(
                "segment named more than once", object_error::parse_failed);
          if (Index > DataSegments.size())
            return make_error<GenericBinaryError>("invalid data segment name entry",
                                                  object_error::parse_failed);
          nameType = wasm::NameType::DATA_SEGMENT;
          Info.Kind = wasm::WASM_SYMBOL_TYPE_DATA;
          Info.Flags |= wasm::WASM_SYMBOL_BINDING_LOCAL;
          assert(Index < DataSegments.size());
          Info.DataRef = wasm::WasmDataReference{
              Index, 0, DataSegments[Index].Data.Content.size()};
        }
        DebugNames.push_back(wasm::WasmDebugName{nameType, Index, Name});
        if (PopulateSymbolTable)
          Symbols.emplace_back(Info, GlobalType, TableType, Signature);
      }
      break;
    }
    // Ignore local names for now
    case wasm::WASM_NAMES_LOCAL:
    default:
      Ctx.Ptr += Size;
      break;
    }
    if (Ctx.Ptr != SubSectionEnd)
      return make_error<GenericBinaryError>(
          "name sub-section ended prematurely", object_error::parse_failed);
  }

  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("name section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseLinkingSection(ReadContext &Ctx) {
  HasLinkingSection = true;

  LinkingData.Version = readVaruint32(Ctx);
  if (LinkingData.Version != wasm::WasmMetadataVersion) {
    return make_error<GenericBinaryError>(
        "unexpected metadata version: " + Twine(LinkingData.Version) +
            " (Expected: " + Twine(wasm::WasmMetadataVersion) + ")",
        object_error::parse_failed);
  }

  const uint8_t *OrigEnd = Ctx.End;
  while (Ctx.Ptr < OrigEnd) {
    Ctx.End = OrigEnd;
    uint8_t Type = readUint8(Ctx);
    uint32_t Size = readVaruint32(Ctx);
    LLVM_DEBUG(dbgs() << "readSubsection type=" << int(Type) << " size=" << Size
                      << "\n");
    Ctx.End = Ctx.Ptr + Size;
    switch (Type) {
    case wasm::WASM_SYMBOL_TABLE:
      if (Error Err = parseLinkingSectionSymtab(Ctx))
        return Err;
      break;
    case wasm::WASM_SEGMENT_INFO: {
      uint32_t Count = readVaruint32(Ctx);
      if (Count > DataSegments.size())
        return make_error<GenericBinaryError>("too many segment names",
                                              object_error::parse_failed);
      for (uint32_t I = 0; I < Count; I++) {
        DataSegments[I].Data.Name = readString(Ctx);
        DataSegments[I].Data.Alignment = readVaruint32(Ctx);
        DataSegments[I].Data.LinkingFlags = readVaruint32(Ctx);
      }
      break;
    }
    case wasm::WASM_INIT_FUNCS: {
      uint32_t Count = readVaruint32(Ctx);
      LinkingData.InitFunctions.reserve(Count);
      for (uint32_t I = 0; I < Count; I++) {
        wasm::WasmInitFunc Init;
        Init.Priority = readVaruint32(Ctx);
        Init.Symbol = readVaruint32(Ctx);
        if (!isValidFunctionSymbol(Init.Symbol))
          return make_error<GenericBinaryError>("invalid function symbol: " +
                                                    Twine(Init.Symbol),
                                                object_error::parse_failed);
        LinkingData.InitFunctions.emplace_back(Init);
      }
      break;
    }
    case wasm::WASM_COMDAT_INFO:
      if (Error Err = parseLinkingSectionComdat(Ctx))
        return Err;
      break;
    default:
      Ctx.Ptr += Size;
      break;
    }
    if (Ctx.Ptr != Ctx.End)
      return make_error<GenericBinaryError>(
          "linking sub-section ended prematurely", object_error::parse_failed);
  }
  if (Ctx.Ptr != OrigEnd)
    return make_error<GenericBinaryError>("linking section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseLinkingSectionSymtab(ReadContext &Ctx) {
  uint32_t Count = readVaruint32(Ctx);
  // Clear out any symbol information that was derived from the exports
  // section.
  Symbols.clear();
  Symbols.reserve(Count);
  StringSet<> SymbolNames;

  std::vector<wasm::WasmImport *> ImportedGlobals;
  std::vector<wasm::WasmImport *> ImportedFunctions;
  std::vector<wasm::WasmImport *> ImportedTags;
  std::vector<wasm::WasmImport *> ImportedTables;
  ImportedGlobals.reserve(Imports.size());
  ImportedFunctions.reserve(Imports.size());
  ImportedTags.reserve(Imports.size());
  ImportedTables.reserve(Imports.size());
  for (auto &I : Imports) {
    if (I.Kind == wasm::WASM_EXTERNAL_FUNCTION)
      ImportedFunctions.emplace_back(&I);
    else if (I.Kind == wasm::WASM_EXTERNAL_GLOBAL)
      ImportedGlobals.emplace_back(&I);
    else if (I.Kind == wasm::WASM_EXTERNAL_TAG)
      ImportedTags.emplace_back(&I);
    else if (I.Kind == wasm::WASM_EXTERNAL_TABLE)
      ImportedTables.emplace_back(&I);
  }

  while (Count--) {
    wasm::WasmSymbolInfo Info;
    const wasm::WasmSignature *Signature = nullptr;
    const wasm::WasmGlobalType *GlobalType = nullptr;
    const wasm::WasmTableType *TableType = nullptr;

    Info.Kind = readUint8(Ctx);
    Info.Flags = readVaruint32(Ctx);
    bool IsDefined = (Info.Flags & wasm::WASM_SYMBOL_UNDEFINED) == 0;

    switch (Info.Kind) {
    case wasm::WASM_SYMBOL_TYPE_FUNCTION:
      Info.ElementIndex = readVaruint32(Ctx);
      if (!isValidFunctionIndex(Info.ElementIndex) ||
          IsDefined != isDefinedFunctionIndex(Info.ElementIndex))
        return make_error<GenericBinaryError>("invalid function symbol index",
                                              object_error::parse_failed);
      if (IsDefined) {
        Info.Name = readString(Ctx);
        unsigned FuncIndex = Info.ElementIndex - NumImportedFunctions;
        wasm::WasmFunction &Function = Functions[FuncIndex];
        Signature = &Signatures[Function.SigIndex];
        if (Function.SymbolName.empty())
          Function.SymbolName = Info.Name;
      } else {
        wasm::WasmImport &Import = *ImportedFunctions[Info.ElementIndex];
        if ((Info.Flags & wasm::WASM_SYMBOL_EXPLICIT_NAME) != 0) {
          Info.Name = readString(Ctx);
          Info.ImportName = Import.Field;
        } else {
          Info.Name = Import.Field;
        }
        Signature = &Signatures[Import.SigIndex];
        Info.ImportModule = Import.Module;
      }
      break;

    case wasm::WASM_SYMBOL_TYPE_GLOBAL:
      Info.ElementIndex = readVaruint32(Ctx);
      if (!isValidGlobalIndex(Info.ElementIndex) ||
          IsDefined != isDefinedGlobalIndex(Info.ElementIndex))
        return make_error<GenericBinaryError>("invalid global symbol index",
                                              object_error::parse_failed);
      if (!IsDefined && (Info.Flags & wasm::WASM_SYMBOL_BINDING_MASK) ==
                            wasm::WASM_SYMBOL_BINDING_WEAK)
        return make_error<GenericBinaryError>("undefined weak global symbol",
                                              object_error::parse_failed);
      if (IsDefined) {
        Info.Name = readString(Ctx);
        unsigned GlobalIndex = Info.ElementIndex - NumImportedGlobals;
        wasm::WasmGlobal &Global = Globals[GlobalIndex];
        GlobalType = &Global.Type;
        if (Global.SymbolName.empty())
          Global.SymbolName = Info.Name;
      } else {
        wasm::WasmImport &Import = *ImportedGlobals[Info.ElementIndex];
        if ((Info.Flags & wasm::WASM_SYMBOL_EXPLICIT_NAME) != 0) {
          Info.Name = readString(Ctx);
          Info.ImportName = Import.Field;
        } else {
          Info.Name = Import.Field;
        }
        GlobalType = &Import.Global;
        Info.ImportModule = Import.Module;
      }
      break;

    case wasm::WASM_SYMBOL_TYPE_TABLE:
      Info.ElementIndex = readVaruint32(Ctx);
      if (!isValidTableNumber(Info.ElementIndex) ||
          IsDefined != isDefinedTableNumber(Info.ElementIndex))
        return make_error<GenericBinaryError>("invalid table symbol index",
                                              object_error::parse_failed);
      if (!IsDefined && (Info.Flags & wasm::WASM_SYMBOL_BINDING_MASK) ==
                            wasm::WASM_SYMBOL_BINDING_WEAK)
        return make_error<GenericBinaryError>("undefined weak table symbol",
                                              object_error::parse_failed);
      if (IsDefined) {
        Info.Name = readString(Ctx);
        unsigned TableNumber = Info.ElementIndex - NumImportedTables;
        wasm::WasmTable &Table = Tables[TableNumber];
        TableType = &Table.Type;
        if (Table.SymbolName.empty())
          Table.SymbolName = Info.Name;
      } else {
        wasm::WasmImport &Import = *ImportedTables[Info.ElementIndex];
        if ((Info.Flags & wasm::WASM_SYMBOL_EXPLICIT_NAME) != 0) {
          Info.Name = readString(Ctx);
          Info.ImportName = Import.Field;
        } else {
          Info.Name = Import.Field;
        }
        TableType = &Import.Table;
        Info.ImportModule = Import.Module;
      }
      break;

    case wasm::WASM_SYMBOL_TYPE_DATA:
      Info.Name = readString(Ctx);
      if (IsDefined) {
        auto Index = readVaruint32(Ctx);
        auto Offset = readVaruint64(Ctx);
        auto Size = readVaruint64(Ctx);
        if (!(Info.Flags & wasm::WASM_SYMBOL_ABSOLUTE)) {
          if (static_cast<size_t>(Index) >= DataSegments.size())
            return make_error<GenericBinaryError>(
                "invalid data segment index: " + Twine(Index),
                object_error::parse_failed);
          size_t SegmentSize = DataSegments[Index].Data.Content.size();
          if (Offset > SegmentSize)
            return make_error<GenericBinaryError>(
                "invalid data symbol offset: `" + Info.Name +
                    "` (offset: " + Twine(Offset) +
                    " segment size: " + Twine(SegmentSize) + ")",
                object_error::parse_failed);
        }
        Info.DataRef = wasm::WasmDataReference{Index, Offset, Size};
      }
      break;

    case wasm::WASM_SYMBOL_TYPE_SECTION: {
      if ((Info.Flags & wasm::WASM_SYMBOL_BINDING_MASK) !=
          wasm::WASM_SYMBOL_BINDING_LOCAL)
        return make_error<GenericBinaryError>(
            "section symbols must have local binding",
            object_error::parse_failed);
      Info.ElementIndex = readVaruint32(Ctx);
      // Use somewhat unique section name as symbol name.
      StringRef SectionName = Sections[Info.ElementIndex].Name;
      Info.Name = SectionName;
      break;
    }

    case wasm::WASM_SYMBOL_TYPE_TAG: {
      Info.ElementIndex = readVaruint32(Ctx);
      if (!isValidTagIndex(Info.ElementIndex) ||
          IsDefined != isDefinedTagIndex(Info.ElementIndex))
        return make_error<GenericBinaryError>("invalid tag symbol index",
                                              object_error::parse_failed);
      if (!IsDefined && (Info.Flags & wasm::WASM_SYMBOL_BINDING_MASK) ==
                            wasm::WASM_SYMBOL_BINDING_WEAK)
        return make_error<GenericBinaryError>("undefined weak global symbol",
                                              object_error::parse_failed);
      if (IsDefined) {
        Info.Name = readString(Ctx);
        unsigned TagIndex = Info.ElementIndex - NumImportedTags;
        wasm::WasmTag &Tag = Tags[TagIndex];
        Signature = &Signatures[Tag.SigIndex];
        if (Tag.SymbolName.empty())
          Tag.SymbolName = Info.Name;

      } else {
        wasm::WasmImport &Import = *ImportedTags[Info.ElementIndex];
        if ((Info.Flags & wasm::WASM_SYMBOL_EXPLICIT_NAME) != 0) {
          Info.Name = readString(Ctx);
          Info.ImportName = Import.Field;
        } else {
          Info.Name = Import.Field;
        }
        Signature = &Signatures[Import.SigIndex];
        Info.ImportModule = Import.Module;
      }
      break;
    }

    default:
      return make_error<GenericBinaryError>("invalid symbol type: " +
                                                Twine(unsigned(Info.Kind)),
                                            object_error::parse_failed);
    }

    if ((Info.Flags & wasm::WASM_SYMBOL_BINDING_MASK) !=
            wasm::WASM_SYMBOL_BINDING_LOCAL &&
        !SymbolNames.insert(Info.Name).second)
      return make_error<GenericBinaryError>("duplicate symbol name " +
                                                Twine(Info.Name),
                                            object_error::parse_failed);
    Symbols.emplace_back(Info, GlobalType, TableType, Signature);
    LLVM_DEBUG(dbgs() << "Adding symbol: " << Symbols.back() << "\n");
  }

  return Error::success();
}

Error WasmObjectFile::parseLinkingSectionComdat(ReadContext &Ctx) {
  uint32_t ComdatCount = readVaruint32(Ctx);
  StringSet<> ComdatSet;
  for (unsigned ComdatIndex = 0; ComdatIndex < ComdatCount; ++ComdatIndex) {
    StringRef Name = readString(Ctx);
    if (Name.empty() || !ComdatSet.insert(Name).second)
      return make_error<GenericBinaryError>("bad/duplicate COMDAT name " +
                                                Twine(Name),
                                            object_error::parse_failed);
    LinkingData.Comdats.emplace_back(Name);
    uint32_t Flags = readVaruint32(Ctx);
    if (Flags != 0)
      return make_error<GenericBinaryError>("unsupported COMDAT flags",
                                            object_error::parse_failed);

    uint32_t EntryCount = readVaruint32(Ctx);
    while (EntryCount--) {
      unsigned Kind = readVaruint32(Ctx);
      unsigned Index = readVaruint32(Ctx);
      switch (Kind) {
      default:
        return make_error<GenericBinaryError>("invalid COMDAT entry type",
                                              object_error::parse_failed);
      case wasm::WASM_COMDAT_DATA:
        if (Index >= DataSegments.size())
          return make_error<GenericBinaryError>(
              "COMDAT data index out of range", object_error::parse_failed);
        if (DataSegments[Index].Data.Comdat != UINT32_MAX)
          return make_error<GenericBinaryError>("data segment in two COMDATs",
                                                object_error::parse_failed);
        DataSegments[Index].Data.Comdat = ComdatIndex;
        break;
      case wasm::WASM_COMDAT_FUNCTION:
        if (!isDefinedFunctionIndex(Index))
          return make_error<GenericBinaryError>(
              "COMDAT function index out of range", object_error::parse_failed);
        if (getDefinedFunction(Index).Comdat != UINT32_MAX)
          return make_error<GenericBinaryError>("function in two COMDATs",
                                                object_error::parse_failed);
        getDefinedFunction(Index).Comdat = ComdatIndex;
        break;
      case wasm::WASM_COMDAT_SECTION:
        if (Index >= Sections.size())
          return make_error<GenericBinaryError>(
              "COMDAT section index out of range", object_error::parse_failed);
        if (Sections[Index].Type != wasm::WASM_SEC_CUSTOM)
          return make_error<GenericBinaryError>(
              "non-custom section in a COMDAT", object_error::parse_failed);
        Sections[Index].Comdat = ComdatIndex;
        break;
      }
    }
  }
  return Error::success();
}

Error WasmObjectFile::parseProducersSection(ReadContext &Ctx) {
  llvm::SmallSet<StringRef, 3> FieldsSeen;
  uint32_t Fields = readVaruint32(Ctx);
  for (size_t I = 0; I < Fields; ++I) {
    StringRef FieldName = readString(Ctx);
    if (!FieldsSeen.insert(FieldName).second)
      return make_error<GenericBinaryError>(
          "producers section does not have unique fields",
          object_error::parse_failed);
    std::vector<std::pair<std::string, std::string>> *ProducerVec = nullptr;
    if (FieldName == "language") {
      ProducerVec = &ProducerInfo.Languages;
    } else if (FieldName == "processed-by") {
      ProducerVec = &ProducerInfo.Tools;
    } else if (FieldName == "sdk") {
      ProducerVec = &ProducerInfo.SDKs;
    } else {
      return make_error<GenericBinaryError>(
          "producers section field is not named one of language, processed-by, "
          "or sdk",
          object_error::parse_failed);
    }
    uint32_t ValueCount = readVaruint32(Ctx);
    llvm::SmallSet<StringRef, 8> ProducersSeen;
    for (size_t J = 0; J < ValueCount; ++J) {
      StringRef Name = readString(Ctx);
      StringRef Version = readString(Ctx);
      if (!ProducersSeen.insert(Name).second) {
        return make_error<GenericBinaryError>(
            "producers section contains repeated producer",
            object_error::parse_failed);
      }
      ProducerVec->emplace_back(std::string(Name), std::string(Version));
    }
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("producers section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseTargetFeaturesSection(ReadContext &Ctx) {
  llvm::SmallSet<std::string, 8> FeaturesSeen;
  uint32_t FeatureCount = readVaruint32(Ctx);
  for (size_t I = 0; I < FeatureCount; ++I) {
    wasm::WasmFeatureEntry Feature;
    Feature.Prefix = readUint8(Ctx);
    switch (Feature.Prefix) {
    case wasm::WASM_FEATURE_PREFIX_USED:
    case wasm::WASM_FEATURE_PREFIX_REQUIRED:
    case wasm::WASM_FEATURE_PREFIX_DISALLOWED:
      break;
    default:
      return make_error<GenericBinaryError>("unknown feature policy prefix",
                                            object_error::parse_failed);
    }
    Feature.Name = std::string(readString(Ctx));
    if (!FeaturesSeen.insert(Feature.Name).second)
      return make_error<GenericBinaryError>(
          "target features section contains repeated feature \"" +
              Feature.Name + "\"",
          object_error::parse_failed);
    TargetFeatures.push_back(Feature);
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>(
        "target features section ended prematurely",
        object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseRelocSection(StringRef Name, ReadContext &Ctx) {
  uint32_t SectionIndex = readVaruint32(Ctx);
  if (SectionIndex >= Sections.size())
    return make_error<GenericBinaryError>("invalid section index",
                                          object_error::parse_failed);
  WasmSection &Section = Sections[SectionIndex];
  uint32_t RelocCount = readVaruint32(Ctx);
  uint32_t EndOffset = Section.Content.size();
  uint32_t PreviousOffset = 0;
  while (RelocCount--) {
    wasm::WasmRelocation Reloc = {};
    uint32_t type = readVaruint32(Ctx);
    Reloc.Type = type;
    Reloc.Offset = readVaruint32(Ctx);
    if (Reloc.Offset < PreviousOffset)
      return make_error<GenericBinaryError>("relocations not in offset order",
                                            object_error::parse_failed);

    auto badReloc = [&](StringRef msg) {
      return make_error<GenericBinaryError>(
          msg + ": " + Twine(Symbols[Reloc.Index].Info.Name),
          object_error::parse_failed);
    };

    PreviousOffset = Reloc.Offset;
    Reloc.Index = readVaruint32(Ctx);
    switch (type) {
    case wasm::R_WASM_FUNCTION_INDEX_LEB:
    case wasm::R_WASM_FUNCTION_INDEX_I32:
    case wasm::R_WASM_TABLE_INDEX_SLEB:
    case wasm::R_WASM_TABLE_INDEX_SLEB64:
    case wasm::R_WASM_TABLE_INDEX_I32:
    case wasm::R_WASM_TABLE_INDEX_I64:
    case wasm::R_WASM_TABLE_INDEX_REL_SLEB:
    case wasm::R_WASM_TABLE_INDEX_REL_SLEB64:
      if (!isValidFunctionSymbol(Reloc.Index))
        return badReloc("invalid function relocation");
      break;
    case wasm::R_WASM_TABLE_NUMBER_LEB:
      if (!isValidTableSymbol(Reloc.Index))
        return badReloc("invalid table relocation");
      break;
    case wasm::R_WASM_TYPE_INDEX_LEB:
      if (Reloc.Index >= Signatures.size())
        return badReloc("invalid relocation type index");
      break;
    case wasm::R_WASM_GLOBAL_INDEX_LEB:
      // R_WASM_GLOBAL_INDEX_LEB are can be used against function and data
      // symbols to refer to their GOT entries.
      if (!isValidGlobalSymbol(Reloc.Index) &&
          !isValidDataSymbol(Reloc.Index) &&
          !isValidFunctionSymbol(Reloc.Index))
        return badReloc("invalid global relocation");
      break;
    case wasm::R_WASM_GLOBAL_INDEX_I32:
      if (!isValidGlobalSymbol(Reloc.Index))
        return badReloc("invalid global relocation");
      break;
    case wasm::R_WASM_TAG_INDEX_LEB:
      if (!isValidTagSymbol(Reloc.Index))
        return badReloc("invalid tag relocation");
      break;
    case wasm::R_WASM_MEMORY_ADDR_LEB:
    case wasm::R_WASM_MEMORY_ADDR_SLEB:
    case wasm::R_WASM_MEMORY_ADDR_I32:
    case wasm::R_WASM_MEMORY_ADDR_REL_SLEB:
    case wasm::R_WASM_MEMORY_ADDR_TLS_SLEB:
    case wasm::R_WASM_MEMORY_ADDR_LOCREL_I32:
      if (!isValidDataSymbol(Reloc.Index))
        return badReloc("invalid data relocation");
      Reloc.Addend = readVarint32(Ctx);
      break;
    case wasm::R_WASM_MEMORY_ADDR_LEB64:
    case wasm::R_WASM_MEMORY_ADDR_SLEB64:
    case wasm::R_WASM_MEMORY_ADDR_I64:
    case wasm::R_WASM_MEMORY_ADDR_REL_SLEB64:
    case wasm::R_WASM_MEMORY_ADDR_TLS_SLEB64:
      if (!isValidDataSymbol(Reloc.Index))
        return badReloc("invalid data relocation");
      Reloc.Addend = readVarint64(Ctx);
      break;
    case wasm::R_WASM_FUNCTION_OFFSET_I32:
      if (!isValidFunctionSymbol(Reloc.Index))
        return badReloc("invalid function relocation");
      Reloc.Addend = readVarint32(Ctx);
      break;
    case wasm::R_WASM_FUNCTION_OFFSET_I64:
      if (!isValidFunctionSymbol(Reloc.Index))
        return badReloc("invalid function relocation");
      Reloc.Addend = readVarint64(Ctx);
      break;
    case wasm::R_WASM_SECTION_OFFSET_I32:
      if (!isValidSectionSymbol(Reloc.Index))
        return badReloc("invalid section relocation");
      Reloc.Addend = readVarint32(Ctx);
      break;
    default:
      return make_error<GenericBinaryError>("invalid relocation type: " +
                                                Twine(type),
                                            object_error::parse_failed);
    }

    // Relocations must fit inside the section, and must appear in order.  They
    // also shouldn't overlap a function/element boundary, but we don't bother
    // to check that.
    uint64_t Size = 5;
    if (Reloc.Type == wasm::R_WASM_MEMORY_ADDR_LEB64 ||
        Reloc.Type == wasm::R_WASM_MEMORY_ADDR_SLEB64 ||
        Reloc.Type == wasm::R_WASM_MEMORY_ADDR_REL_SLEB64)
      Size = 10;
    if (Reloc.Type == wasm::R_WASM_TABLE_INDEX_I32 ||
        Reloc.Type == wasm::R_WASM_MEMORY_ADDR_I32 ||
        Reloc.Type == wasm::R_WASM_MEMORY_ADDR_LOCREL_I32 ||
        Reloc.Type == wasm::R_WASM_SECTION_OFFSET_I32 ||
        Reloc.Type == wasm::R_WASM_FUNCTION_OFFSET_I32 ||
        Reloc.Type == wasm::R_WASM_FUNCTION_INDEX_I32 ||
        Reloc.Type == wasm::R_WASM_GLOBAL_INDEX_I32)
      Size = 4;
    if (Reloc.Type == wasm::R_WASM_TABLE_INDEX_I64 ||
        Reloc.Type == wasm::R_WASM_MEMORY_ADDR_I64 ||
        Reloc.Type == wasm::R_WASM_FUNCTION_OFFSET_I64)
      Size = 8;
    if (Reloc.Offset + Size > EndOffset)
      return make_error<GenericBinaryError>("invalid relocation offset",
                                            object_error::parse_failed);

    Section.Relocations.push_back(Reloc);
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("reloc section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseCustomSection(WasmSection &Sec, ReadContext &Ctx) {
  if (Sec.Name == "dylink") {
    if (Error Err = parseDylinkSection(Ctx))
      return Err;
  } else if (Sec.Name == "dylink.0") {
    if (Error Err = parseDylink0Section(Ctx))
      return Err;
  } else if (Sec.Name == "name") {
    if (Error Err = parseNameSection(Ctx))
      return Err;
  } else if (Sec.Name == "linking") {
    if (Error Err = parseLinkingSection(Ctx))
      return Err;
  } else if (Sec.Name == "producers") {
    if (Error Err = parseProducersSection(Ctx))
      return Err;
  } else if (Sec.Name == "target_features") {
    if (Error Err = parseTargetFeaturesSection(Ctx))
      return Err;
  } else if (Sec.Name.starts_with("reloc.")) {
    if (Error Err = parseRelocSection(Sec.Name, Ctx))
      return Err;
  }
  return Error::success();
}

Error WasmObjectFile::parseTypeSection(ReadContext &Ctx) {
  auto parseFieldDef = [&]() {
    uint32_t TypeCode = readVaruint32((Ctx));
    /* Discard StorageType */ parseValType(Ctx, TypeCode);
    /* Discard Mutability */ readVaruint32(Ctx);
  };

  uint32_t Count = readVaruint32(Ctx);
  Signatures.reserve(Count);
  while (Count--) {
    wasm::WasmSignature Sig;
    uint8_t Form = readUint8(Ctx);
    if (Form == wasm::WASM_TYPE_REC) {
      // Rec groups expand the type index space (beyond what was declared at
      // the top of the section, and also consume one element in that space.
      uint32_t RecSize = readVaruint32(Ctx);
      if (RecSize == 0)
        return make_error<GenericBinaryError>("Rec group size cannot be 0",
                                              object_error::parse_failed);
      Signatures.reserve(Signatures.size() + RecSize);
      Count += RecSize;
      Sig.Kind = wasm::WasmSignature::Placeholder;
      Signatures.push_back(std::move(Sig));
      HasUnmodeledTypes = true;
      continue;
    }
    if (Form != wasm::WASM_TYPE_FUNC) {
      // Currently LLVM only models function types, and not other composite
      // types. Here we parse the type declarations just enough to skip past
      // them in the binary.
      if (Form == wasm::WASM_TYPE_SUB || Form == wasm::WASM_TYPE_SUB_FINAL) {
        uint32_t Supers = readVaruint32(Ctx);
        if (Supers > 0) {
          if (Supers != 1)
            return make_error<GenericBinaryError>(
                "Invalid number of supertypes", object_error::parse_failed);
          /* Discard SuperIndex */ readVaruint32(Ctx);
        }
        Form = readVaruint32(Ctx);
      }
      if (Form == wasm::WASM_TYPE_STRUCT) {
        uint32_t FieldCount = readVaruint32(Ctx);
        while (FieldCount--) {
          parseFieldDef();
        }
      } else if (Form == wasm::WASM_TYPE_ARRAY) {
        parseFieldDef();
      } else {
        return make_error<GenericBinaryError>("bad form",
                                              object_error::parse_failed);
      }
      Sig.Kind = wasm::WasmSignature::Placeholder;
      Signatures.push_back(std::move(Sig));
      HasUnmodeledTypes = true;
      continue;
    }

    uint32_t ParamCount = readVaruint32(Ctx);
    Sig.Params.reserve(ParamCount);
    while (ParamCount--) {
      uint32_t ParamType = readUint8(Ctx);
      Sig.Params.push_back(parseValType(Ctx, ParamType));
      continue;
    }
    uint32_t ReturnCount = readVaruint32(Ctx);
    while (ReturnCount--) {
      uint32_t ReturnType = readUint8(Ctx);
      Sig.Returns.push_back(parseValType(Ctx, ReturnType));
    }

    Signatures.push_back(std::move(Sig));
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("type section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseImportSection(ReadContext &Ctx) {
  uint32_t Count = readVaruint32(Ctx);
  uint32_t NumTypes = Signatures.size();
  Imports.reserve(Count);
  for (uint32_t I = 0; I < Count; I++) {
    wasm::WasmImport Im;
    Im.Module = readString(Ctx);
    Im.Field = readString(Ctx);
    Im.Kind = readUint8(Ctx);
    switch (Im.Kind) {
    case wasm::WASM_EXTERNAL_FUNCTION:
      NumImportedFunctions++;
      Im.SigIndex = readVaruint32(Ctx);
      if (Im.SigIndex >= NumTypes)
        return make_error<GenericBinaryError>("invalid function type",
                                              object_error::parse_failed);
      break;
    case wasm::WASM_EXTERNAL_GLOBAL:
      NumImportedGlobals++;
      Im.Global.Type = readUint8(Ctx);
      Im.Global.Mutable = readVaruint1(Ctx);
      break;
    case wasm::WASM_EXTERNAL_MEMORY:
      Im.Memory = readLimits(Ctx);
      if (Im.Memory.Flags & wasm::WASM_LIMITS_FLAG_IS_64)
        HasMemory64 = true;
      break;
    case wasm::WASM_EXTERNAL_TABLE: {
      Im.Table = readTableType(Ctx);
      NumImportedTables++;
      auto ElemType = Im.Table.ElemType;
      if (ElemType != wasm::ValType::FUNCREF &&
          ElemType != wasm::ValType::EXTERNREF &&
          ElemType != wasm::ValType::EXNREF &&
          ElemType != wasm::ValType::OTHERREF)
        return make_error<GenericBinaryError>("invalid table element type",
                                              object_error::parse_failed);
      break;
    }
    case wasm::WASM_EXTERNAL_TAG:
      NumImportedTags++;
      if (readUint8(Ctx) != 0) // Reserved 'attribute' field
        return make_error<GenericBinaryError>("invalid attribute",
                                              object_error::parse_failed);
      Im.SigIndex = readVaruint32(Ctx);
      if (Im.SigIndex >= NumTypes)
        return make_error<GenericBinaryError>("invalid tag type",
                                              object_error::parse_failed);
      break;
    default:
      return make_error<GenericBinaryError>("unexpected import kind",
                                            object_error::parse_failed);
    }
    Imports.push_back(Im);
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("import section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseFunctionSection(ReadContext &Ctx) {
  uint32_t Count = readVaruint32(Ctx);
  Functions.reserve(Count);
  uint32_t NumTypes = Signatures.size();
  while (Count--) {
    uint32_t Type = readVaruint32(Ctx);
    if (Type >= NumTypes)
      return make_error<GenericBinaryError>("invalid function type",
                                            object_error::parse_failed);
    wasm::WasmFunction F;
    F.SigIndex = Type;
    Functions.push_back(F);
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("function section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseTableSection(ReadContext &Ctx) {
  TableSection = Sections.size();
  uint32_t Count = readVaruint32(Ctx);
  Tables.reserve(Count);
  while (Count--) {
    wasm::WasmTable T;
    T.Type = readTableType(Ctx);
    T.Index = NumImportedTables + Tables.size();
    Tables.push_back(T);
    auto ElemType = Tables.back().Type.ElemType;
    if (ElemType != wasm::ValType::FUNCREF &&
        ElemType != wasm::ValType::EXTERNREF &&
        ElemType != wasm::ValType::EXNREF &&
        ElemType != wasm::ValType::OTHERREF) {
      return make_error<GenericBinaryError>("invalid table element type",
                                            object_error::parse_failed);
    }
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("table section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseMemorySection(ReadContext &Ctx) {
  uint32_t Count = readVaruint32(Ctx);
  Memories.reserve(Count);
  while (Count--) {
    auto Limits = readLimits(Ctx);
    if (Limits.Flags & wasm::WASM_LIMITS_FLAG_IS_64)
      HasMemory64 = true;
    Memories.push_back(Limits);
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("memory section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseTagSection(ReadContext &Ctx) {
  TagSection = Sections.size();
  uint32_t Count = readVaruint32(Ctx);
  Tags.reserve(Count);
  uint32_t NumTypes = Signatures.size();
  while (Count--) {
    if (readUint8(Ctx) != 0) // Reserved 'attribute' field
      return make_error<GenericBinaryError>("invalid attribute",
                                            object_error::parse_failed);
    uint32_t Type = readVaruint32(Ctx);
    if (Type >= NumTypes)
      return make_error<GenericBinaryError>("invalid tag type",
                                            object_error::parse_failed);
    wasm::WasmTag Tag;
    Tag.Index = NumImportedTags + Tags.size();
    Tag.SigIndex = Type;
    Signatures[Type].Kind = wasm::WasmSignature::Tag;
    Tags.push_back(Tag);
  }

  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("tag section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseGlobalSection(ReadContext &Ctx) {
  GlobalSection = Sections.size();
  const uint8_t *SectionStart = Ctx.Ptr;
  uint32_t Count = readVaruint32(Ctx);
  Globals.reserve(Count);
  while (Count--) {
    wasm::WasmGlobal Global;
    Global.Index = NumImportedGlobals + Globals.size();
    const uint8_t *GlobalStart = Ctx.Ptr;
    Global.Offset = static_cast<uint32_t>(GlobalStart - SectionStart);
    auto GlobalOpcode = readVaruint32(Ctx);
    Global.Type.Type = (uint8_t)parseValType(Ctx, GlobalOpcode);
    Global.Type.Mutable = readVaruint1(Ctx);
    if (Error Err = readInitExpr(Global.InitExpr, Ctx))
      return Err;
    Global.Size = static_cast<uint32_t>(Ctx.Ptr - GlobalStart);
    Globals.push_back(Global);
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("global section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseExportSection(ReadContext &Ctx) {
  uint32_t Count = readVaruint32(Ctx);
  Exports.reserve(Count);
  Symbols.reserve(Count);
  for (uint32_t I = 0; I < Count; I++) {
    wasm::WasmExport Ex;
    Ex.Name = readString(Ctx);
    Ex.Kind = readUint8(Ctx);
    Ex.Index = readVaruint32(Ctx);
    const wasm::WasmSignature *Signature = nullptr;
    const wasm::WasmGlobalType *GlobalType = nullptr;
    const wasm::WasmTableType *TableType = nullptr;
    wasm::WasmSymbolInfo Info;
    Info.Name = Ex.Name;
    Info.Flags = 0;
    switch (Ex.Kind) {
    case wasm::WASM_EXTERNAL_FUNCTION: {
      if (!isDefinedFunctionIndex(Ex.Index))
        return make_error<GenericBinaryError>("invalid function export",
                                              object_error::parse_failed);
      getDefinedFunction(Ex.Index).ExportName = Ex.Name;
      Info.Kind = wasm::WASM_SYMBOL_TYPE_FUNCTION;
      Info.ElementIndex = Ex.Index;
      unsigned FuncIndex = Info.ElementIndex - NumImportedFunctions;
      wasm::WasmFunction &Function = Functions[FuncIndex];
      Signature = &Signatures[Function.SigIndex];
      break;
    }
    case wasm::WASM_EXTERNAL_GLOBAL: {
      if (!isValidGlobalIndex(Ex.Index))
        return make_error<GenericBinaryError>("invalid global export",
                                              object_error::parse_failed);
      Info.Kind = wasm::WASM_SYMBOL_TYPE_DATA;
      uint64_t Offset = 0;
      if (isDefinedGlobalIndex(Ex.Index)) {
        auto Global = getDefinedGlobal(Ex.Index);
        if (!Global.InitExpr.Extended) {
          auto Inst = Global.InitExpr.Inst;
          if (Inst.Opcode == wasm::WASM_OPCODE_I32_CONST) {
            Offset = Inst.Value.Int32;
          } else if (Inst.Opcode == wasm::WASM_OPCODE_I64_CONST) {
            Offset = Inst.Value.Int64;
          }
        }
      }
      Info.DataRef = wasm::WasmDataReference{0, Offset, 0};
      break;
    }
    case wasm::WASM_EXTERNAL_TAG:
      if (!isValidTagIndex(Ex.Index))
        return make_error<GenericBinaryError>("invalid tag export",
                                              object_error::parse_failed);
      Info.Kind = wasm::WASM_SYMBOL_TYPE_TAG;
      Info.ElementIndex = Ex.Index;
      break;
    case wasm::WASM_EXTERNAL_MEMORY:
      break;
    case wasm::WASM_EXTERNAL_TABLE:
      Info.Kind = wasm::WASM_SYMBOL_TYPE_TABLE;
      Info.ElementIndex = Ex.Index;
      break;
    default:
      return make_error<GenericBinaryError>("unexpected export kind",
                                            object_error::parse_failed);
    }
    Exports.push_back(Ex);
    if (Ex.Kind != wasm::WASM_EXTERNAL_MEMORY) {
      Symbols.emplace_back(Info, GlobalType, TableType, Signature);
      LLVM_DEBUG(dbgs() << "Adding symbol: " << Symbols.back() << "\n");
    }
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("export section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

bool WasmObjectFile::isValidFunctionIndex(uint32_t Index) const {
  return Index < NumImportedFunctions + Functions.size();
}

bool WasmObjectFile::isDefinedFunctionIndex(uint32_t Index) const {
  return Index >= NumImportedFunctions && isValidFunctionIndex(Index);
}

bool WasmObjectFile::isValidGlobalIndex(uint32_t Index) const {
  return Index < NumImportedGlobals + Globals.size();
}

bool WasmObjectFile::isValidTableNumber(uint32_t Index) const {
  return Index < NumImportedTables + Tables.size();
}

bool WasmObjectFile::isDefinedGlobalIndex(uint32_t Index) const {
  return Index >= NumImportedGlobals && isValidGlobalIndex(Index);
}

bool WasmObjectFile::isDefinedTableNumber(uint32_t Index) const {
  return Index >= NumImportedTables && isValidTableNumber(Index);
}

bool WasmObjectFile::isValidTagIndex(uint32_t Index) const {
  return Index < NumImportedTags + Tags.size();
}

bool WasmObjectFile::isDefinedTagIndex(uint32_t Index) const {
  return Index >= NumImportedTags && isValidTagIndex(Index);
}

bool WasmObjectFile::isValidFunctionSymbol(uint32_t Index) const {
  return Index < Symbols.size() && Symbols[Index].isTypeFunction();
}

bool WasmObjectFile::isValidTableSymbol(uint32_t Index) const {
  return Index < Symbols.size() && Symbols[Index].isTypeTable();
}

bool WasmObjectFile::isValidGlobalSymbol(uint32_t Index) const {
  return Index < Symbols.size() && Symbols[Index].isTypeGlobal();
}

bool WasmObjectFile::isValidTagSymbol(uint32_t Index) const {
  return Index < Symbols.size() && Symbols[Index].isTypeTag();
}

bool WasmObjectFile::isValidDataSymbol(uint32_t Index) const {
  return Index < Symbols.size() && Symbols[Index].isTypeData();
}

bool WasmObjectFile::isValidSectionSymbol(uint32_t Index) const {
  return Index < Symbols.size() && Symbols[Index].isTypeSection();
}

wasm::WasmFunction &WasmObjectFile::getDefinedFunction(uint32_t Index) {
  assert(isDefinedFunctionIndex(Index));
  return Functions[Index - NumImportedFunctions];
}

const wasm::WasmFunction &
WasmObjectFile::getDefinedFunction(uint32_t Index) const {
  assert(isDefinedFunctionIndex(Index));
  return Functions[Index - NumImportedFunctions];
}

const wasm::WasmGlobal &WasmObjectFile::getDefinedGlobal(uint32_t Index) const {
  assert(isDefinedGlobalIndex(Index));
  return Globals[Index - NumImportedGlobals];
}

wasm::WasmTag &WasmObjectFile::getDefinedTag(uint32_t Index) {
  assert(isDefinedTagIndex(Index));
  return Tags[Index - NumImportedTags];
}

Error WasmObjectFile::parseStartSection(ReadContext &Ctx) {
  StartFunction = readVaruint32(Ctx);
  if (!isValidFunctionIndex(StartFunction))
    return make_error<GenericBinaryError>("invalid start function",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseCodeSection(ReadContext &Ctx) {
  CodeSection = Sections.size();
  uint32_t FunctionCount = readVaruint32(Ctx);
  if (FunctionCount != Functions.size()) {
    return make_error<GenericBinaryError>("invalid function count",
                                          object_error::parse_failed);
  }

  for (uint32_t i = 0; i < FunctionCount; i++) {
    wasm::WasmFunction& Function = Functions[i];
    const uint8_t *FunctionStart = Ctx.Ptr;
    uint32_t Size = readVaruint32(Ctx);
    const uint8_t *FunctionEnd = Ctx.Ptr + Size;

    Function.CodeOffset = Ctx.Ptr - FunctionStart;
    Function.Index = NumImportedFunctions + i;
    Function.CodeSectionOffset = FunctionStart - Ctx.Start;
    Function.Size = FunctionEnd - FunctionStart;

    uint32_t NumLocalDecls = readVaruint32(Ctx);
    Function.Locals.reserve(NumLocalDecls);
    while (NumLocalDecls--) {
      wasm::WasmLocalDecl Decl;
      Decl.Count = readVaruint32(Ctx);
      Decl.Type = readUint8(Ctx);
      Function.Locals.push_back(Decl);
    }

    uint32_t BodySize = FunctionEnd - Ctx.Ptr;
    // Ensure that Function is within Ctx's buffer.
    if (Ctx.Ptr + BodySize > Ctx.End) {
      return make_error<GenericBinaryError>("Function extends beyond buffer",
                                            object_error::parse_failed);
    }
    Function.Body = ArrayRef<uint8_t>(Ctx.Ptr, BodySize);
    // This will be set later when reading in the linking metadata section.
    Function.Comdat = UINT32_MAX;
    Ctx.Ptr += BodySize;
    assert(Ctx.Ptr == FunctionEnd);
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("code section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseElemSection(ReadContext &Ctx) {
  uint32_t Count = readVaruint32(Ctx);
  ElemSegments.reserve(Count);
  while (Count--) {
    wasm::WasmElemSegment Segment;
    Segment.Flags = readVaruint32(Ctx);

    uint32_t SupportedFlags = wasm::WASM_ELEM_SEGMENT_HAS_TABLE_NUMBER |
                              wasm::WASM_ELEM_SEGMENT_IS_PASSIVE |
                              wasm::WASM_ELEM_SEGMENT_HAS_INIT_EXPRS;
    if (Segment.Flags & ~SupportedFlags)
      return make_error<GenericBinaryError>(
          "Unsupported flags for element segment", object_error::parse_failed);

    bool IsPassive = (Segment.Flags & wasm::WASM_ELEM_SEGMENT_IS_PASSIVE) != 0;
    bool IsDeclarative =
        IsPassive && (Segment.Flags & wasm::WASM_ELEM_SEGMENT_IS_DECLARATIVE);
    bool HasTableNumber =
        !IsPassive &&
        (Segment.Flags & wasm::WASM_ELEM_SEGMENT_HAS_TABLE_NUMBER);
    bool HasInitExprs =
        (Segment.Flags & wasm::WASM_ELEM_SEGMENT_HAS_INIT_EXPRS);
    bool HasElemKind =
        (Segment.Flags & wasm::WASM_ELEM_SEGMENT_MASK_HAS_ELEM_KIND) &&
        !HasInitExprs;

    if (HasTableNumber)
      Segment.TableNumber = readVaruint32(Ctx);
    else
      Segment.TableNumber = 0;

    if (!isValidTableNumber(Segment.TableNumber))
      return make_error<GenericBinaryError>("invalid TableNumber",
                                            object_error::parse_failed);

    if (IsPassive || IsDeclarative) {
      Segment.Offset.Extended = false;
      Segment.Offset.Inst.Opcode = wasm::WASM_OPCODE_I32_CONST;
      Segment.Offset.Inst.Value.Int32 = 0;
    } else {
      if (Error Err = readInitExpr(Segment.Offset, Ctx))
        return Err;
    }

    if (HasElemKind) {
      auto ElemKind = readVaruint32(Ctx);
      if (Segment.Flags & wasm::WASM_ELEM_SEGMENT_HAS_INIT_EXPRS) {
        Segment.ElemKind = parseValType(Ctx, ElemKind);
        if (Segment.ElemKind != wasm::ValType::FUNCREF &&
            Segment.ElemKind != wasm::ValType::EXTERNREF &&
            Segment.ElemKind != wasm::ValType::EXNREF &&
            Segment.ElemKind != wasm::ValType::OTHERREF) {
          return make_error<GenericBinaryError>("invalid elem type",
                                                object_error::parse_failed);
        }
      } else {
        if (ElemKind != 0)
          return make_error<GenericBinaryError>("invalid elem type",
                                                object_error::parse_failed);
        Segment.ElemKind = wasm::ValType::FUNCREF;
      }
    } else if (HasInitExprs) {
      auto ElemType = parseValType(Ctx, readVaruint32(Ctx));
      Segment.ElemKind = ElemType;
    } else {
      Segment.ElemKind = wasm::ValType::FUNCREF;
    }

    uint32_t NumElems = readVaruint32(Ctx);

    if (HasInitExprs) {
      while (NumElems--) {
        wasm::WasmInitExpr Expr;
        if (Error Err = readInitExpr(Expr, Ctx))
          return Err;
      }
    } else {
      while (NumElems--) {
        Segment.Functions.push_back(readVaruint32(Ctx));
      }
    }
    ElemSegments.push_back(Segment);
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("elem section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseDataSection(ReadContext &Ctx) {
  DataSection = Sections.size();
  uint32_t Count = readVaruint32(Ctx);
  if (DataCount && Count != *DataCount)
    return make_error<GenericBinaryError>(
        "number of data segments does not match DataCount section");
  DataSegments.reserve(Count);
  while (Count--) {
    WasmSegment Segment;
    Segment.Data.InitFlags = readVaruint32(Ctx);
    Segment.Data.MemoryIndex =
        (Segment.Data.InitFlags & wasm::WASM_DATA_SEGMENT_HAS_MEMINDEX)
            ? readVaruint32(Ctx)
            : 0;
    if ((Segment.Data.InitFlags & wasm::WASM_DATA_SEGMENT_IS_PASSIVE) == 0) {
      if (Error Err = readInitExpr(Segment.Data.Offset, Ctx))
        return Err;
    } else {
      Segment.Data.Offset.Extended = false;
      Segment.Data.Offset.Inst.Opcode = wasm::WASM_OPCODE_I32_CONST;
      Segment.Data.Offset.Inst.Value.Int32 = 0;
    }
    uint32_t Size = readVaruint32(Ctx);
    if (Size > (size_t)(Ctx.End - Ctx.Ptr))
      return make_error<GenericBinaryError>("invalid segment size",
                                            object_error::parse_failed);
    Segment.Data.Content = ArrayRef<uint8_t>(Ctx.Ptr, Size);
    // The rest of these Data fields are set later, when reading in the linking
    // metadata section.
    Segment.Data.Alignment = 0;
    Segment.Data.LinkingFlags = 0;
    Segment.Data.Comdat = UINT32_MAX;
    Segment.SectionOffset = Ctx.Ptr - Ctx.Start;
    Ctx.Ptr += Size;
    DataSegments.push_back(Segment);
  }
  if (Ctx.Ptr != Ctx.End)
    return make_error<GenericBinaryError>("data section ended prematurely",
                                          object_error::parse_failed);
  return Error::success();
}

Error WasmObjectFile::parseDataCountSection(ReadContext &Ctx) {
  DataCount = readVaruint32(Ctx);
  return Error::success();
}

const wasm::WasmObjectHeader &WasmObjectFile::getHeader() const {
  return Header;
}

void WasmObjectFile::moveSymbolNext(DataRefImpl &Symb) const { Symb.d.b++; }

Expected<uint32_t> WasmObjectFile::getSymbolFlags(DataRefImpl Symb) const {
  uint32_t Result = SymbolRef::SF_None;
  const WasmSymbol &Sym = getWasmSymbol(Symb);

  LLVM_DEBUG(dbgs() << "getSymbolFlags: ptr=" << &Sym << " " << Sym << "\n");
  if (Sym.isBindingWeak())
    Result |= SymbolRef::SF_Weak;
  if (!Sym.isBindingLocal())
    Result |= SymbolRef::SF_Global;
  if (Sym.isHidden())
    Result |= SymbolRef::SF_Hidden;
  if (!Sym.isDefined())
    Result |= SymbolRef::SF_Undefined;
  if (Sym.isTypeFunction())
    Result |= SymbolRef::SF_Executable;
  return Result;
}

basic_symbol_iterator WasmObjectFile::symbol_begin() const {
  DataRefImpl Ref;
  Ref.d.a = 1; // Arbitrary non-zero value so that Ref.p is non-null
  Ref.d.b = 0; // Symbol index
  return BasicSymbolRef(Ref, this);
}

basic_symbol_iterator WasmObjectFile::symbol_end() const {
  DataRefImpl Ref;
  Ref.d.a = 1; // Arbitrary non-zero value so that Ref.p is non-null
  Ref.d.b = Symbols.size(); // Symbol index
  return BasicSymbolRef(Ref, this);
}

const WasmSymbol &WasmObjectFile::getWasmSymbol(const DataRefImpl &Symb) const {
  return Symbols[Symb.d.b];
}

const WasmSymbol &WasmObjectFile::getWasmSymbol(const SymbolRef &Symb) const {
  return getWasmSymbol(Symb.getRawDataRefImpl());
}

Expected<StringRef> WasmObjectFile::getSymbolName(DataRefImpl Symb) const {
  return getWasmSymbol(Symb).Info.Name;
}

Expected<uint64_t> WasmObjectFile::getSymbolAddress(DataRefImpl Symb) const {
  auto &Sym = getWasmSymbol(Symb);
  if (!Sym.isDefined())
    return 0;
  Expected<section_iterator> Sec = getSymbolSection(Symb);
  if (!Sec)
    return Sec.takeError();
  uint32_t SectionAddress = getSectionAddress(Sec.get()->getRawDataRefImpl());
  if (Sym.Info.Kind == wasm::WASM_SYMBOL_TYPE_FUNCTION &&
      isDefinedFunctionIndex(Sym.Info.ElementIndex)) {
    return getDefinedFunction(Sym.Info.ElementIndex).CodeSectionOffset +
           SectionAddress;
  }
  if (Sym.Info.Kind == wasm::WASM_SYMBOL_TYPE_GLOBAL &&
      isDefinedGlobalIndex(Sym.Info.ElementIndex)) {
    return getDefinedGlobal(Sym.Info.ElementIndex).Offset + SectionAddress;
  }

  return getSymbolValue(Symb);
}

uint64_t WasmObjectFile::getWasmSymbolValue(const WasmSymbol &Sym) const {
  switch (Sym.Info.Kind) {
  case wasm::WASM_SYMBOL_TYPE_FUNCTION:
  case wasm::WASM_SYMBOL_TYPE_GLOBAL:
  case wasm::WASM_SYMBOL_TYPE_TAG:
  case wasm::WASM_SYMBOL_TYPE_TABLE:
    return Sym.Info.ElementIndex;
  case wasm::WASM_SYMBOL_TYPE_DATA: {
    // The value of a data symbol is the segment offset, plus the symbol
    // offset within the segment.
    uint32_t SegmentIndex = Sym.Info.DataRef.Segment;
    const wasm::WasmDataSegment &Segment = DataSegments[SegmentIndex].Data;
    if (Segment.Offset.Extended) {
      llvm_unreachable("extended init exprs not supported");
    } else if (Segment.Offset.Inst.Opcode == wasm::WASM_OPCODE_I32_CONST) {
      return Segment.Offset.Inst.Value.Int32 + Sym.Info.DataRef.Offset;
    } else if (Segment.Offset.Inst.Opcode == wasm::WASM_OPCODE_I64_CONST) {
      return Segment.Offset.Inst.Value.Int64 + Sym.Info.DataRef.Offset;
    } else if (Segment.Offset.Inst.Opcode == wasm::WASM_OPCODE_GLOBAL_GET) {
      return Sym.Info.DataRef.Offset;
    } else {
      llvm_unreachable("unknown init expr opcode");
    }
  }
  case wasm::WASM_SYMBOL_TYPE_SECTION:
    return 0;
  }
  llvm_unreachable("invalid symbol type");
}

uint64_t WasmObjectFile::getSymbolValueImpl(DataRefImpl Symb) const {
  return getWasmSymbolValue(getWasmSymbol(Symb));
}

uint32_t WasmObjectFile::getSymbolAlignment(DataRefImpl Symb) const {
  llvm_unreachable("not yet implemented");
  return 0;
}

uint64_t WasmObjectFile::getCommonSymbolSizeImpl(DataRefImpl Symb) const {
  llvm_unreachable("not yet implemented");
  return 0;
}

Expected<SymbolRef::Type>
WasmObjectFile::getSymbolType(DataRefImpl Symb) const {
  const WasmSymbol &Sym = getWasmSymbol(Symb);

  switch (Sym.Info.Kind) {
  case wasm::WASM_SYMBOL_TYPE_FUNCTION:
    return SymbolRef::ST_Function;
  case wasm::WASM_SYMBOL_TYPE_GLOBAL:
    return SymbolRef::ST_Other;
  case wasm::WASM_SYMBOL_TYPE_DATA:
    return SymbolRef::ST_Data;
  case wasm::WASM_SYMBOL_TYPE_SECTION:
    return SymbolRef::ST_Debug;
  case wasm::WASM_SYMBOL_TYPE_TAG:
    return SymbolRef::ST_Other;
  case wasm::WASM_SYMBOL_TYPE_TABLE:
    return SymbolRef::ST_Other;
  }

  llvm_unreachable("unknown WasmSymbol::SymbolType");
  return SymbolRef::ST_Other;
}

Expected<section_iterator>
WasmObjectFile::getSymbolSection(DataRefImpl Symb) const {
  const WasmSymbol &Sym = getWasmSymbol(Symb);
  if (Sym.isUndefined())
    return section_end();

  DataRefImpl Ref;
  Ref.d.a = getSymbolSectionIdImpl(Sym);
  return section_iterator(SectionRef(Ref, this));
}

uint32_t WasmObjectFile::getSymbolSectionId(SymbolRef Symb) const {
  const WasmSymbol &Sym = getWasmSymbol(Symb);
  return getSymbolSectionIdImpl(Sym);
}

uint32_t WasmObjectFile::getSymbolSectionIdImpl(const WasmSymbol &Sym) const {
  switch (Sym.Info.Kind) {
  case wasm::WASM_SYMBOL_TYPE_FUNCTION:
    return CodeSection;
  case wasm::WASM_SYMBOL_TYPE_GLOBAL:
    return GlobalSection;
  case wasm::WASM_SYMBOL_TYPE_DATA:
    return DataSection;
  case wasm::WASM_SYMBOL_TYPE_SECTION:
    return Sym.Info.ElementIndex;
  case wasm::WASM_SYMBOL_TYPE_TAG:
    return TagSection;
  case wasm::WASM_SYMBOL_TYPE_TABLE:
    return TableSection;
  default:
    llvm_unreachable("unknown WasmSymbol::SymbolType");
  }
}

uint32_t WasmObjectFile::getSymbolSize(SymbolRef Symb) const {
  const WasmSymbol &Sym = getWasmSymbol(Symb);
  if (!Sym.isDefined())
    return 0;
  if (Sym.isTypeGlobal())
    return getDefinedGlobal(Sym.Info.ElementIndex).Size;
  if (Sym.isTypeData())
    return Sym.Info.DataRef.Size;
  if (Sym.isTypeFunction())
    return functions()[Sym.Info.ElementIndex - getNumImportedFunctions()].Size;
  // Currently symbol size is only tracked for data segments and functions. In
  // principle we could also track size (e.g. binary size) for tables, globals
  // and element segments etc too.
  return 0;
}

void WasmObjectFile::moveSectionNext(DataRefImpl &Sec) const { Sec.d.a++; }

Expected<StringRef> WasmObjectFile::getSectionName(DataRefImpl Sec) const {
  const WasmSection &S = Sections[Sec.d.a];
  if (S.Type == wasm::WASM_SEC_CUSTOM)
    return S.Name;
  if (S.Type > wasm::WASM_SEC_LAST_KNOWN)
    return createStringError(object_error::invalid_section_index, "");
  return wasm::sectionTypeToString(S.Type);
}

uint64_t WasmObjectFile::getSectionAddress(DataRefImpl Sec) const {
  // For object files, use 0 for section addresses, and section offsets for
  // symbol addresses. For linked files, use file offsets.
  // See also getSymbolAddress.
  return isRelocatableObject() || isSharedObject() ? 0
                                                   : Sections[Sec.d.a].Offset;
}

uint64_t WasmObjectFile::getSectionIndex(DataRefImpl Sec) const {
  return Sec.d.a;
}

uint64_t WasmObjectFile::getSectionSize(DataRefImpl Sec) const {
  const WasmSection &S = Sections[Sec.d.a];
  return S.Content.size();
}

Expected<ArrayRef<uint8_t>>
WasmObjectFile::getSectionContents(DataRefImpl Sec) const {
  const WasmSection &S = Sections[Sec.d.a];
  // This will never fail since wasm sections can never be empty (user-sections
  // must have a name and non-user sections each have a defined structure).
  return S.Content;
}

uint64_t WasmObjectFile::getSectionAlignment(DataRefImpl Sec) const {
  return 1;
}

bool WasmObjectFile::isSectionCompressed(DataRefImpl Sec) const {
  return false;
}

bool WasmObjectFile::isSectionText(DataRefImpl Sec) const {
  return getWasmSection(Sec).Type == wasm::WASM_SEC_CODE;
}

bool WasmObjectFile::isSectionData(DataRefImpl Sec) const {
  return getWasmSection(Sec).Type == wasm::WASM_SEC_DATA;
}

bool WasmObjectFile::isSectionBSS(DataRefImpl Sec) const { return false; }

bool WasmObjectFile::isSectionVirtual(DataRefImpl Sec) const { return false; }

relocation_iterator WasmObjectFile::section_rel_begin(DataRefImpl Ref) const {
  DataRefImpl RelocRef;
  RelocRef.d.a = Ref.d.a;
  RelocRef.d.b = 0;
  return relocation_iterator(RelocationRef(RelocRef, this));
}

relocation_iterator WasmObjectFile::section_rel_end(DataRefImpl Ref) const {
  const WasmSection &Sec = getWasmSection(Ref);
  DataRefImpl RelocRef;
  RelocRef.d.a = Ref.d.a;
  RelocRef.d.b = Sec.Relocations.size();
  return relocation_iterator(RelocationRef(RelocRef, this));
}

void WasmObjectFile::moveRelocationNext(DataRefImpl &Rel) const { Rel.d.b++; }

uint64_t WasmObjectFile::getRelocationOffset(DataRefImpl Ref) const {
  const wasm::WasmRelocation &Rel = getWasmRelocation(Ref);
  return Rel.Offset;
}

symbol_iterator WasmObjectFile::getRelocationSymbol(DataRefImpl Ref) const {
  const wasm::WasmRelocation &Rel = getWasmRelocation(Ref);
  if (Rel.Type == wasm::R_WASM_TYPE_INDEX_LEB)
    return symbol_end();
  DataRefImpl Sym;
  Sym.d.a = 1;
  Sym.d.b = Rel.Index;
  return symbol_iterator(SymbolRef(Sym, this));
}

uint64_t WasmObjectFile::getRelocationType(DataRefImpl Ref) const {
  const wasm::WasmRelocation &Rel = getWasmRelocation(Ref);
  return Rel.Type;
}

void WasmObjectFile::getRelocationTypeName(
    DataRefImpl Ref, SmallVectorImpl<char> &Result) const {
  const wasm::WasmRelocation &Rel = getWasmRelocation(Ref);
  StringRef Res = "Unknown";

#define WASM_RELOC(name, value)                                                \
  case wasm::name:                                                             \
    Res = #name;                                                               \
    break;

  switch (Rel.Type) {
#include "llvm/BinaryFormat/WasmRelocs.def"
  }

#undef WASM_RELOC

  Result.append(Res.begin(), Res.end());
}

section_iterator WasmObjectFile::section_begin() const {
  DataRefImpl Ref;
  Ref.d.a = 0;
  return section_iterator(SectionRef(Ref, this));
}

section_iterator WasmObjectFile::section_end() const {
  DataRefImpl Ref;
  Ref.d.a = Sections.size();
  return section_iterator(SectionRef(Ref, this));
}

uint8_t WasmObjectFile::getBytesInAddress() const {
  return HasMemory64 ? 8 : 4;
}

StringRef WasmObjectFile::getFileFormatName() const { return "WASM"; }

Triple::ArchType WasmObjectFile::getArch() const {
  return HasMemory64 ? Triple::wasm64 : Triple::wasm32;
}

Expected<SubtargetFeatures> WasmObjectFile::getFeatures() const {
  return SubtargetFeatures();
}

bool WasmObjectFile::isRelocatableObject() const { return HasLinkingSection; }

bool WasmObjectFile::isSharedObject() const { return HasDylinkSection; }

const WasmSection &WasmObjectFile::getWasmSection(DataRefImpl Ref) const {
  assert(Ref.d.a < Sections.size());
  return Sections[Ref.d.a];
}

const WasmSection &
WasmObjectFile::getWasmSection(const SectionRef &Section) const {
  return getWasmSection(Section.getRawDataRefImpl());
}

const wasm::WasmRelocation &
WasmObjectFile::getWasmRelocation(const RelocationRef &Ref) const {
  return getWasmRelocation(Ref.getRawDataRefImpl());
}

const wasm::WasmRelocation &
WasmObjectFile::getWasmRelocation(DataRefImpl Ref) const {
  assert(Ref.d.a < Sections.size());
  const WasmSection &Sec = Sections[Ref.d.a];
  assert(Ref.d.b < Sec.Relocations.size());
  return Sec.Relocations[Ref.d.b];
}

int WasmSectionOrderChecker::getSectionOrder(unsigned ID,
                                             StringRef CustomSectionName) {
  switch (ID) {
  case wasm::WASM_SEC_CUSTOM:
    return StringSwitch<unsigned>(CustomSectionName)
        .Case("dylink", WASM_SEC_ORDER_DYLINK)
        .Case("dylink.0", WASM_SEC_ORDER_DYLINK)
        .Case("linking", WASM_SEC_ORDER_LINKING)
        .StartsWith("reloc.", WASM_SEC_ORDER_RELOC)
        .Case("name", WASM_SEC_ORDER_NAME)
        .Case("producers", WASM_SEC_ORDER_PRODUCERS)
        .Case("target_features", WASM_SEC_ORDER_TARGET_FEATURES)
        .Default(WASM_SEC_ORDER_NONE);
  case wasm::WASM_SEC_TYPE:
    return WASM_SEC_ORDER_TYPE;
  case wasm::WASM_SEC_IMPORT:
    return WASM_SEC_ORDER_IMPORT;
  case wasm::WASM_SEC_FUNCTION:
    return WASM_SEC_ORDER_FUNCTION;
  case wasm::WASM_SEC_TABLE:
    return WASM_SEC_ORDER_TABLE;
  case wasm::WASM_SEC_MEMORY:
    return WASM_SEC_ORDER_MEMORY;
  case wasm::WASM_SEC_GLOBAL:
    return WASM_SEC_ORDER_GLOBAL;
  case wasm::WASM_SEC_EXPORT:
    return WASM_SEC_ORDER_EXPORT;
  case wasm::WASM_SEC_START:
    return WASM_SEC_ORDER_START;
  case wasm::WASM_SEC_ELEM:
    return WASM_SEC_ORDER_ELEM;
  case wasm::WASM_SEC_CODE:
    return WASM_SEC_ORDER_CODE;
  case wasm::WASM_SEC_DATA:
    return WASM_SEC_ORDER_DATA;
  case wasm::WASM_SEC_DATACOUNT:
    return WASM_SEC_ORDER_DATACOUNT;
  case wasm::WASM_SEC_TAG:
    return WASM_SEC_ORDER_TAG;
  default:
    return WASM_SEC_ORDER_NONE;
  }
}

// Represents the edges in a directed graph where any node B reachable from node
// A is not allowed to appear before A in the section ordering, but may appear
// afterward.
int WasmSectionOrderChecker::DisallowedPredecessors
    [WASM_NUM_SEC_ORDERS][WASM_NUM_SEC_ORDERS] = {
        // WASM_SEC_ORDER_NONE
        {},
        // WASM_SEC_ORDER_TYPE
        {WASM_SEC_ORDER_TYPE, WASM_SEC_ORDER_IMPORT},
        // WASM_SEC_ORDER_IMPORT
        {WASM_SEC_ORDER_IMPORT, WASM_SEC_ORDER_FUNCTION},
        // WASM_SEC_ORDER_FUNCTION
        {WASM_SEC_ORDER_FUNCTION, WASM_SEC_ORDER_TABLE},
        // WASM_SEC_ORDER_TABLE
        {WASM_SEC_ORDER_TABLE, WASM_SEC_ORDER_MEMORY},
        // WASM_SEC_ORDER_MEMORY
        {WASM_SEC_ORDER_MEMORY, WASM_SEC_ORDER_TAG},
        // WASM_SEC_ORDER_TAG
        {WASM_SEC_ORDER_TAG, WASM_SEC_ORDER_GLOBAL},
        // WASM_SEC_ORDER_GLOBAL
        {WASM_SEC_ORDER_GLOBAL, WASM_SEC_ORDER_EXPORT},
        // WASM_SEC_ORDER_EXPORT
        {WASM_SEC_ORDER_EXPORT, WASM_SEC_ORDER_START},
        // WASM_SEC_ORDER_START
        {WASM_SEC_ORDER_START, WASM_SEC_ORDER_ELEM},
        // WASM_SEC_ORDER_ELEM
        {WASM_SEC_ORDER_ELEM, WASM_SEC_ORDER_DATACOUNT},
        // WASM_SEC_ORDER_DATACOUNT
        {WASM_SEC_ORDER_DATACOUNT, WASM_SEC_ORDER_CODE},
        // WASM_SEC_ORDER_CODE
        {WASM_SEC_ORDER_CODE, WASM_SEC_ORDER_DATA},
        // WASM_SEC_ORDER_DATA
        {WASM_SEC_ORDER_DATA, WASM_SEC_ORDER_LINKING},

        // Custom Sections
        // WASM_SEC_ORDER_DYLINK
        {WASM_SEC_ORDER_DYLINK, WASM_SEC_ORDER_TYPE},
        // WASM_SEC_ORDER_LINKING
        {WASM_SEC_ORDER_LINKING, WASM_SEC_ORDER_RELOC, WASM_SEC_ORDER_NAME},
        // WASM_SEC_ORDER_RELOC (can be repeated)
        {},
        // WASM_SEC_ORDER_NAME
        {WASM_SEC_ORDER_NAME, WASM_SEC_ORDER_PRODUCERS},
        // WASM_SEC_ORDER_PRODUCERS
        {WASM_SEC_ORDER_PRODUCERS, WASM_SEC_ORDER_TARGET_FEATURES},
        // WASM_SEC_ORDER_TARGET_FEATURES
        {WASM_SEC_ORDER_TARGET_FEATURES}};

bool WasmSectionOrderChecker::isValidSectionOrder(unsigned ID,
                                                  StringRef CustomSectionName) {
  int Order = getSectionOrder(ID, CustomSectionName);
  if (Order == WASM_SEC_ORDER_NONE)
    return true;

  // Disallowed predecessors we need to check for
  SmallVector<int, WASM_NUM_SEC_ORDERS> WorkList;

  // Keep track of completed checks to avoid repeating work
  bool Checked[WASM_NUM_SEC_ORDERS] = {};

  int Curr = Order;
  while (true) {
    // Add new disallowed predecessors to work list
    for (size_t I = 0;; ++I) {
      int Next = DisallowedPredecessors[Curr][I];
      if (Next == WASM_SEC_ORDER_NONE)
        break;
      if (Checked[Next])
        continue;
      WorkList.push_back(Next);
      Checked[Next] = true;
    }

    if (WorkList.empty())
      break;

    // Consider next disallowed predecessor
    Curr = WorkList.pop_back_val();
    if (Seen[Curr])
      return false;
  }

  // Have not seen any disallowed predecessors
  Seen[Order] = true;
  return true;
}
