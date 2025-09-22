//===- BitcodeAnalyzer.cpp - Internal BitcodeAnalyzer implementation ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeAnalyzer.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/LLVMBitCodes.h"
#include "llvm/Bitstream/BitCodes.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/SHA1.h"
#include <optional>

using namespace llvm;

static Error reportError(StringRef Message) {
  return createStringError(std::errc::illegal_byte_sequence, Message.data());
}

/// Return a symbolic block name if known, otherwise return null.
static std::optional<const char *>
GetBlockName(unsigned BlockID, const BitstreamBlockInfo &BlockInfo,
             CurStreamTypeType CurStreamType) {
  // Standard blocks for all bitcode files.
  if (BlockID < bitc::FIRST_APPLICATION_BLOCKID) {
    if (BlockID == bitc::BLOCKINFO_BLOCK_ID)
      return "BLOCKINFO_BLOCK";
    return std::nullopt;
  }

  // Check to see if we have a blockinfo record for this block, with a name.
  if (const BitstreamBlockInfo::BlockInfo *Info =
          BlockInfo.getBlockInfo(BlockID)) {
    if (!Info->Name.empty())
      return Info->Name.c_str();
  }

  if (CurStreamType != LLVMIRBitstream)
    return std::nullopt;

  switch (BlockID) {
  default:
    return std::nullopt;
  case bitc::OPERAND_BUNDLE_TAGS_BLOCK_ID:
    return "OPERAND_BUNDLE_TAGS_BLOCK";
  case bitc::MODULE_BLOCK_ID:
    return "MODULE_BLOCK";
  case bitc::PARAMATTR_BLOCK_ID:
    return "PARAMATTR_BLOCK";
  case bitc::PARAMATTR_GROUP_BLOCK_ID:
    return "PARAMATTR_GROUP_BLOCK_ID";
  case bitc::TYPE_BLOCK_ID_NEW:
    return "TYPE_BLOCK_ID";
  case bitc::CONSTANTS_BLOCK_ID:
    return "CONSTANTS_BLOCK";
  case bitc::FUNCTION_BLOCK_ID:
    return "FUNCTION_BLOCK";
  case bitc::IDENTIFICATION_BLOCK_ID:
    return "IDENTIFICATION_BLOCK_ID";
  case bitc::VALUE_SYMTAB_BLOCK_ID:
    return "VALUE_SYMTAB";
  case bitc::METADATA_BLOCK_ID:
    return "METADATA_BLOCK";
  case bitc::METADATA_KIND_BLOCK_ID:
    return "METADATA_KIND_BLOCK";
  case bitc::METADATA_ATTACHMENT_ID:
    return "METADATA_ATTACHMENT_BLOCK";
  case bitc::USELIST_BLOCK_ID:
    return "USELIST_BLOCK_ID";
  case bitc::GLOBALVAL_SUMMARY_BLOCK_ID:
    return "GLOBALVAL_SUMMARY_BLOCK";
  case bitc::FULL_LTO_GLOBALVAL_SUMMARY_BLOCK_ID:
    return "FULL_LTO_GLOBALVAL_SUMMARY_BLOCK";
  case bitc::MODULE_STRTAB_BLOCK_ID:
    return "MODULE_STRTAB_BLOCK";
  case bitc::STRTAB_BLOCK_ID:
    return "STRTAB_BLOCK";
  case bitc::SYMTAB_BLOCK_ID:
    return "SYMTAB_BLOCK";
  }
}

/// Return a symbolic code name if known, otherwise return null.
static std::optional<const char *>
GetCodeName(unsigned CodeID, unsigned BlockID,
            const BitstreamBlockInfo &BlockInfo,
            CurStreamTypeType CurStreamType) {
  // Standard blocks for all bitcode files.
  if (BlockID < bitc::FIRST_APPLICATION_BLOCKID) {
    if (BlockID == bitc::BLOCKINFO_BLOCK_ID) {
      switch (CodeID) {
      default:
        return std::nullopt;
      case bitc::BLOCKINFO_CODE_SETBID:
        return "SETBID";
      case bitc::BLOCKINFO_CODE_BLOCKNAME:
        return "BLOCKNAME";
      case bitc::BLOCKINFO_CODE_SETRECORDNAME:
        return "SETRECORDNAME";
      }
    }
    return std::nullopt;
  }

  // Check to see if we have a blockinfo record for this record, with a name.
  if (const BitstreamBlockInfo::BlockInfo *Info =
          BlockInfo.getBlockInfo(BlockID)) {
    for (const std::pair<unsigned, std::string> &RN : Info->RecordNames)
      if (RN.first == CodeID)
        return RN.second.c_str();
  }

  if (CurStreamType != LLVMIRBitstream)
    return std::nullopt;

#define STRINGIFY_CODE(PREFIX, CODE)                                           \
  case bitc::PREFIX##_##CODE:                                                  \
    return #CODE;
  switch (BlockID) {
  default:
    return std::nullopt;
  case bitc::MODULE_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
      STRINGIFY_CODE(MODULE_CODE, VERSION)
      STRINGIFY_CODE(MODULE_CODE, TRIPLE)
      STRINGIFY_CODE(MODULE_CODE, DATALAYOUT)
      STRINGIFY_CODE(MODULE_CODE, ASM)
      STRINGIFY_CODE(MODULE_CODE, SECTIONNAME)
      STRINGIFY_CODE(MODULE_CODE, DEPLIB) // Deprecated, present in old bitcode
      STRINGIFY_CODE(MODULE_CODE, GLOBALVAR)
      STRINGIFY_CODE(MODULE_CODE, FUNCTION)
      STRINGIFY_CODE(MODULE_CODE, ALIAS)
      STRINGIFY_CODE(MODULE_CODE, GCNAME)
      STRINGIFY_CODE(MODULE_CODE, COMDAT)
      STRINGIFY_CODE(MODULE_CODE, VSTOFFSET)
      STRINGIFY_CODE(MODULE_CODE, METADATA_VALUES_UNUSED)
      STRINGIFY_CODE(MODULE_CODE, SOURCE_FILENAME)
      STRINGIFY_CODE(MODULE_CODE, HASH)
    }
  case bitc::IDENTIFICATION_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
      STRINGIFY_CODE(IDENTIFICATION_CODE, STRING)
      STRINGIFY_CODE(IDENTIFICATION_CODE, EPOCH)
    }
  case bitc::PARAMATTR_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
    // FIXME: Should these be different?
    case bitc::PARAMATTR_CODE_ENTRY_OLD:
      return "ENTRY";
    case bitc::PARAMATTR_CODE_ENTRY:
      return "ENTRY";
    }
  case bitc::PARAMATTR_GROUP_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
    case bitc::PARAMATTR_GRP_CODE_ENTRY:
      return "ENTRY";
    }
  case bitc::TYPE_BLOCK_ID_NEW:
    switch (CodeID) {
    default:
      return std::nullopt;
      STRINGIFY_CODE(TYPE_CODE, NUMENTRY)
      STRINGIFY_CODE(TYPE_CODE, VOID)
      STRINGIFY_CODE(TYPE_CODE, FLOAT)
      STRINGIFY_CODE(TYPE_CODE, DOUBLE)
      STRINGIFY_CODE(TYPE_CODE, LABEL)
      STRINGIFY_CODE(TYPE_CODE, OPAQUE)
      STRINGIFY_CODE(TYPE_CODE, INTEGER)
      STRINGIFY_CODE(TYPE_CODE, POINTER)
      STRINGIFY_CODE(TYPE_CODE, HALF)
      STRINGIFY_CODE(TYPE_CODE, ARRAY)
      STRINGIFY_CODE(TYPE_CODE, VECTOR)
      STRINGIFY_CODE(TYPE_CODE, X86_FP80)
      STRINGIFY_CODE(TYPE_CODE, FP128)
      STRINGIFY_CODE(TYPE_CODE, PPC_FP128)
      STRINGIFY_CODE(TYPE_CODE, METADATA)
      STRINGIFY_CODE(TYPE_CODE, X86_MMX)
      STRINGIFY_CODE(TYPE_CODE, STRUCT_ANON)
      STRINGIFY_CODE(TYPE_CODE, STRUCT_NAME)
      STRINGIFY_CODE(TYPE_CODE, STRUCT_NAMED)
      STRINGIFY_CODE(TYPE_CODE, FUNCTION)
      STRINGIFY_CODE(TYPE_CODE, TOKEN)
      STRINGIFY_CODE(TYPE_CODE, BFLOAT)
    }

  case bitc::CONSTANTS_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
      STRINGIFY_CODE(CST_CODE, SETTYPE)
      STRINGIFY_CODE(CST_CODE, NULL)
      STRINGIFY_CODE(CST_CODE, UNDEF)
      STRINGIFY_CODE(CST_CODE, INTEGER)
      STRINGIFY_CODE(CST_CODE, WIDE_INTEGER)
      STRINGIFY_CODE(CST_CODE, FLOAT)
      STRINGIFY_CODE(CST_CODE, AGGREGATE)
      STRINGIFY_CODE(CST_CODE, STRING)
      STRINGIFY_CODE(CST_CODE, CSTRING)
      STRINGIFY_CODE(CST_CODE, CE_BINOP)
      STRINGIFY_CODE(CST_CODE, CE_CAST)
      STRINGIFY_CODE(CST_CODE, CE_GEP)
      STRINGIFY_CODE(CST_CODE, CE_INBOUNDS_GEP)
      STRINGIFY_CODE(CST_CODE, CE_SELECT)
      STRINGIFY_CODE(CST_CODE, CE_EXTRACTELT)
      STRINGIFY_CODE(CST_CODE, CE_INSERTELT)
      STRINGIFY_CODE(CST_CODE, CE_SHUFFLEVEC)
      STRINGIFY_CODE(CST_CODE, CE_CMP)
      STRINGIFY_CODE(CST_CODE, INLINEASM)
      STRINGIFY_CODE(CST_CODE, CE_SHUFVEC_EX)
      STRINGIFY_CODE(CST_CODE, CE_UNOP)
      STRINGIFY_CODE(CST_CODE, DSO_LOCAL_EQUIVALENT)
      STRINGIFY_CODE(CST_CODE, NO_CFI_VALUE)
      STRINGIFY_CODE(CST_CODE, PTRAUTH)
    case bitc::CST_CODE_BLOCKADDRESS:
      return "CST_CODE_BLOCKADDRESS";
      STRINGIFY_CODE(CST_CODE, DATA)
    }
  case bitc::FUNCTION_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
      STRINGIFY_CODE(FUNC_CODE, DECLAREBLOCKS)
      STRINGIFY_CODE(FUNC_CODE, INST_BINOP)
      STRINGIFY_CODE(FUNC_CODE, INST_CAST)
      STRINGIFY_CODE(FUNC_CODE, INST_GEP_OLD)
      STRINGIFY_CODE(FUNC_CODE, INST_INBOUNDS_GEP_OLD)
      STRINGIFY_CODE(FUNC_CODE, INST_SELECT)
      STRINGIFY_CODE(FUNC_CODE, INST_EXTRACTELT)
      STRINGIFY_CODE(FUNC_CODE, INST_INSERTELT)
      STRINGIFY_CODE(FUNC_CODE, INST_SHUFFLEVEC)
      STRINGIFY_CODE(FUNC_CODE, INST_CMP)
      STRINGIFY_CODE(FUNC_CODE, INST_RET)
      STRINGIFY_CODE(FUNC_CODE, INST_BR)
      STRINGIFY_CODE(FUNC_CODE, INST_SWITCH)
      STRINGIFY_CODE(FUNC_CODE, INST_INVOKE)
      STRINGIFY_CODE(FUNC_CODE, INST_UNOP)
      STRINGIFY_CODE(FUNC_CODE, INST_UNREACHABLE)
      STRINGIFY_CODE(FUNC_CODE, INST_CLEANUPRET)
      STRINGIFY_CODE(FUNC_CODE, INST_CATCHRET)
      STRINGIFY_CODE(FUNC_CODE, INST_CATCHPAD)
      STRINGIFY_CODE(FUNC_CODE, INST_PHI)
      STRINGIFY_CODE(FUNC_CODE, INST_ALLOCA)
      STRINGIFY_CODE(FUNC_CODE, INST_LOAD)
      STRINGIFY_CODE(FUNC_CODE, INST_VAARG)
      STRINGIFY_CODE(FUNC_CODE, INST_STORE)
      STRINGIFY_CODE(FUNC_CODE, INST_EXTRACTVAL)
      STRINGIFY_CODE(FUNC_CODE, INST_INSERTVAL)
      STRINGIFY_CODE(FUNC_CODE, INST_CMP2)
      STRINGIFY_CODE(FUNC_CODE, INST_VSELECT)
      STRINGIFY_CODE(FUNC_CODE, DEBUG_LOC_AGAIN)
      STRINGIFY_CODE(FUNC_CODE, INST_CALL)
      STRINGIFY_CODE(FUNC_CODE, DEBUG_LOC)
      STRINGIFY_CODE(FUNC_CODE, INST_GEP)
      STRINGIFY_CODE(FUNC_CODE, OPERAND_BUNDLE)
      STRINGIFY_CODE(FUNC_CODE, INST_FENCE)
      STRINGIFY_CODE(FUNC_CODE, INST_ATOMICRMW)
      STRINGIFY_CODE(FUNC_CODE, INST_LOADATOMIC)
      STRINGIFY_CODE(FUNC_CODE, INST_STOREATOMIC)
      STRINGIFY_CODE(FUNC_CODE, INST_CMPXCHG)
      STRINGIFY_CODE(FUNC_CODE, INST_CALLBR)
      STRINGIFY_CODE(FUNC_CODE, BLOCKADDR_USERS)
      STRINGIFY_CODE(FUNC_CODE, DEBUG_RECORD_DECLARE)
      STRINGIFY_CODE(FUNC_CODE, DEBUG_RECORD_VALUE)
      STRINGIFY_CODE(FUNC_CODE, DEBUG_RECORD_ASSIGN)
      STRINGIFY_CODE(FUNC_CODE, DEBUG_RECORD_VALUE_SIMPLE)
      STRINGIFY_CODE(FUNC_CODE, DEBUG_RECORD_LABEL)
    }
  case bitc::VALUE_SYMTAB_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
      STRINGIFY_CODE(VST_CODE, ENTRY)
      STRINGIFY_CODE(VST_CODE, BBENTRY)
      STRINGIFY_CODE(VST_CODE, FNENTRY)
      STRINGIFY_CODE(VST_CODE, COMBINED_ENTRY)
    }
  case bitc::MODULE_STRTAB_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
      STRINGIFY_CODE(MST_CODE, ENTRY)
      STRINGIFY_CODE(MST_CODE, HASH)
    }
  case bitc::GLOBALVAL_SUMMARY_BLOCK_ID:
  case bitc::FULL_LTO_GLOBALVAL_SUMMARY_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
      STRINGIFY_CODE(FS, PERMODULE)
      STRINGIFY_CODE(FS, PERMODULE_PROFILE)
      STRINGIFY_CODE(FS, PERMODULE_RELBF)
      STRINGIFY_CODE(FS, PERMODULE_GLOBALVAR_INIT_REFS)
      STRINGIFY_CODE(FS, PERMODULE_VTABLE_GLOBALVAR_INIT_REFS)
      STRINGIFY_CODE(FS, COMBINED)
      STRINGIFY_CODE(FS, COMBINED_PROFILE)
      STRINGIFY_CODE(FS, COMBINED_GLOBALVAR_INIT_REFS)
      STRINGIFY_CODE(FS, ALIAS)
      STRINGIFY_CODE(FS, COMBINED_ALIAS)
      STRINGIFY_CODE(FS, COMBINED_ORIGINAL_NAME)
      STRINGIFY_CODE(FS, VERSION)
      STRINGIFY_CODE(FS, FLAGS)
      STRINGIFY_CODE(FS, TYPE_TESTS)
      STRINGIFY_CODE(FS, TYPE_TEST_ASSUME_VCALLS)
      STRINGIFY_CODE(FS, TYPE_CHECKED_LOAD_VCALLS)
      STRINGIFY_CODE(FS, TYPE_TEST_ASSUME_CONST_VCALL)
      STRINGIFY_CODE(FS, TYPE_CHECKED_LOAD_CONST_VCALL)
      STRINGIFY_CODE(FS, VALUE_GUID)
      STRINGIFY_CODE(FS, CFI_FUNCTION_DEFS)
      STRINGIFY_CODE(FS, CFI_FUNCTION_DECLS)
      STRINGIFY_CODE(FS, TYPE_ID)
      STRINGIFY_CODE(FS, TYPE_ID_METADATA)
      STRINGIFY_CODE(FS, BLOCK_COUNT)
      STRINGIFY_CODE(FS, PARAM_ACCESS)
      STRINGIFY_CODE(FS, PERMODULE_CALLSITE_INFO)
      STRINGIFY_CODE(FS, PERMODULE_ALLOC_INFO)
      STRINGIFY_CODE(FS, COMBINED_CALLSITE_INFO)
      STRINGIFY_CODE(FS, COMBINED_ALLOC_INFO)
      STRINGIFY_CODE(FS, STACK_IDS)
    }
  case bitc::METADATA_ATTACHMENT_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
      STRINGIFY_CODE(METADATA, ATTACHMENT)
    }
  case bitc::METADATA_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
      STRINGIFY_CODE(METADATA, STRING_OLD)
      STRINGIFY_CODE(METADATA, VALUE)
      STRINGIFY_CODE(METADATA, NODE)
      STRINGIFY_CODE(METADATA, NAME)
      STRINGIFY_CODE(METADATA, DISTINCT_NODE)
      STRINGIFY_CODE(METADATA, KIND) // Older bitcode has it in a MODULE_BLOCK
      STRINGIFY_CODE(METADATA, LOCATION)
      STRINGIFY_CODE(METADATA, OLD_NODE)
      STRINGIFY_CODE(METADATA, OLD_FN_NODE)
      STRINGIFY_CODE(METADATA, NAMED_NODE)
      STRINGIFY_CODE(METADATA, GENERIC_DEBUG)
      STRINGIFY_CODE(METADATA, SUBRANGE)
      STRINGIFY_CODE(METADATA, ENUMERATOR)
      STRINGIFY_CODE(METADATA, BASIC_TYPE)
      STRINGIFY_CODE(METADATA, FILE)
      STRINGIFY_CODE(METADATA, DERIVED_TYPE)
      STRINGIFY_CODE(METADATA, COMPOSITE_TYPE)
      STRINGIFY_CODE(METADATA, SUBROUTINE_TYPE)
      STRINGIFY_CODE(METADATA, COMPILE_UNIT)
      STRINGIFY_CODE(METADATA, SUBPROGRAM)
      STRINGIFY_CODE(METADATA, LEXICAL_BLOCK)
      STRINGIFY_CODE(METADATA, LEXICAL_BLOCK_FILE)
      STRINGIFY_CODE(METADATA, NAMESPACE)
      STRINGIFY_CODE(METADATA, TEMPLATE_TYPE)
      STRINGIFY_CODE(METADATA, TEMPLATE_VALUE)
      STRINGIFY_CODE(METADATA, GLOBAL_VAR)
      STRINGIFY_CODE(METADATA, LOCAL_VAR)
      STRINGIFY_CODE(METADATA, EXPRESSION)
      STRINGIFY_CODE(METADATA, OBJC_PROPERTY)
      STRINGIFY_CODE(METADATA, IMPORTED_ENTITY)
      STRINGIFY_CODE(METADATA, MODULE)
      STRINGIFY_CODE(METADATA, MACRO)
      STRINGIFY_CODE(METADATA, MACRO_FILE)
      STRINGIFY_CODE(METADATA, STRINGS)
      STRINGIFY_CODE(METADATA, GLOBAL_DECL_ATTACHMENT)
      STRINGIFY_CODE(METADATA, GLOBAL_VAR_EXPR)
      STRINGIFY_CODE(METADATA, INDEX_OFFSET)
      STRINGIFY_CODE(METADATA, INDEX)
      STRINGIFY_CODE(METADATA, ARG_LIST)
    }
  case bitc::METADATA_KIND_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
      STRINGIFY_CODE(METADATA, KIND)
    }
  case bitc::USELIST_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
    case bitc::USELIST_CODE_DEFAULT:
      return "USELIST_CODE_DEFAULT";
    case bitc::USELIST_CODE_BB:
      return "USELIST_CODE_BB";
    }

  case bitc::OPERAND_BUNDLE_TAGS_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
    case bitc::OPERAND_BUNDLE_TAG:
      return "OPERAND_BUNDLE_TAG";
    }
  case bitc::STRTAB_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
    case bitc::STRTAB_BLOB:
      return "BLOB";
    }
  case bitc::SYMTAB_BLOCK_ID:
    switch (CodeID) {
    default:
      return std::nullopt;
    case bitc::SYMTAB_BLOB:
      return "BLOB";
    }
  }
#undef STRINGIFY_CODE
}

static void printSize(raw_ostream &OS, double Bits) {
  OS << format("%.2f/%.2fB/%luW", Bits, Bits / 8, (unsigned long)(Bits / 32));
}
static void printSize(raw_ostream &OS, uint64_t Bits) {
  OS << format("%lub/%.2fB/%luW", (unsigned long)Bits, (double)Bits / 8,
               (unsigned long)(Bits / 32));
}

static Expected<CurStreamTypeType> ReadSignature(BitstreamCursor &Stream) {
  auto tryRead = [&Stream](char &Dest, size_t size) -> Error {
    if (Expected<SimpleBitstreamCursor::word_t> MaybeWord = Stream.Read(size))
      Dest = MaybeWord.get();
    else
      return MaybeWord.takeError();
    return Error::success();
  };

  char Signature[6];
  if (Error Err = tryRead(Signature[0], 8))
    return std::move(Err);
  if (Error Err = tryRead(Signature[1], 8))
    return std::move(Err);

  // Autodetect the file contents, if it is one we know.
  if (Signature[0] == 'C' && Signature[1] == 'P') {
    if (Error Err = tryRead(Signature[2], 8))
      return std::move(Err);
    if (Error Err = tryRead(Signature[3], 8))
      return std::move(Err);
    if (Signature[2] == 'C' && Signature[3] == 'H')
      return ClangSerializedASTBitstream;
  } else if (Signature[0] == 'D' && Signature[1] == 'I') {
    if (Error Err = tryRead(Signature[2], 8))
      return std::move(Err);
    if (Error Err = tryRead(Signature[3], 8))
      return std::move(Err);
    if (Signature[2] == 'A' && Signature[3] == 'G')
      return ClangSerializedDiagnosticsBitstream;
  } else if (Signature[0] == 'R' && Signature[1] == 'M') {
    if (Error Err = tryRead(Signature[2], 8))
      return std::move(Err);
    if (Error Err = tryRead(Signature[3], 8))
      return std::move(Err);
    if (Signature[2] == 'R' && Signature[3] == 'K')
      return LLVMBitstreamRemarks;
  } else {
    if (Error Err = tryRead(Signature[2], 4))
      return std::move(Err);
    if (Error Err = tryRead(Signature[3], 4))
      return std::move(Err);
    if (Error Err = tryRead(Signature[4], 4))
      return std::move(Err);
    if (Error Err = tryRead(Signature[5], 4))
      return std::move(Err);
    if (Signature[0] == 'B' && Signature[1] == 'C' && Signature[2] == 0x0 &&
        Signature[3] == 0xC && Signature[4] == 0xE && Signature[5] == 0xD)
      return LLVMIRBitstream;
  }
  return UnknownBitstream;
}

static Expected<CurStreamTypeType> analyzeHeader(std::optional<BCDumpOptions> O,
                                                 BitstreamCursor &Stream) {
  ArrayRef<uint8_t> Bytes = Stream.getBitcodeBytes();
  const unsigned char *BufPtr = (const unsigned char *)Bytes.data();
  const unsigned char *EndBufPtr = BufPtr + Bytes.size();

  // If we have a wrapper header, parse it and ignore the non-bc file
  // contents. The magic number is 0x0B17C0DE stored in little endian.
  if (isBitcodeWrapper(BufPtr, EndBufPtr)) {
    if (Bytes.size() < BWH_HeaderSize)
      return reportError("Invalid bitcode wrapper header");

    if (O) {
      unsigned Magic = support::endian::read32le(&BufPtr[BWH_MagicField]);
      unsigned Version = support::endian::read32le(&BufPtr[BWH_VersionField]);
      unsigned Offset = support::endian::read32le(&BufPtr[BWH_OffsetField]);
      unsigned Size = support::endian::read32le(&BufPtr[BWH_SizeField]);
      unsigned CPUType = support::endian::read32le(&BufPtr[BWH_CPUTypeField]);

      O->OS << "<BITCODE_WRAPPER_HEADER"
            << " Magic=" << format_hex(Magic, 10)
            << " Version=" << format_hex(Version, 10)
            << " Offset=" << format_hex(Offset, 10)
            << " Size=" << format_hex(Size, 10)
            << " CPUType=" << format_hex(CPUType, 10) << "/>\n";
    }

    if (SkipBitcodeWrapperHeader(BufPtr, EndBufPtr, true))
      return reportError("Invalid bitcode wrapper header");
  }

  // Use the cursor modified by skipping the wrapper header.
  Stream = BitstreamCursor(ArrayRef<uint8_t>(BufPtr, EndBufPtr));

  return ReadSignature(Stream);
}

static bool canDecodeBlob(unsigned Code, unsigned BlockID) {
  return BlockID == bitc::METADATA_BLOCK_ID && Code == bitc::METADATA_STRINGS;
}

Error BitcodeAnalyzer::decodeMetadataStringsBlob(StringRef Indent,
                                                 ArrayRef<uint64_t> Record,
                                                 StringRef Blob,
                                                 raw_ostream &OS) {
  if (Blob.empty())
    return reportError("Cannot decode empty blob.");

  if (Record.size() != 2)
    return reportError(
        "Decoding metadata strings blob needs two record entries.");

  unsigned NumStrings = Record[0];
  unsigned StringsOffset = Record[1];
  OS << " num-strings = " << NumStrings << " {\n";

  StringRef Lengths = Blob.slice(0, StringsOffset);
  SimpleBitstreamCursor R(Lengths);
  StringRef Strings = Blob.drop_front(StringsOffset);
  do {
    if (R.AtEndOfStream())
      return reportError("bad length");

    uint32_t Size;
    if (Error E = R.ReadVBR(6).moveInto(Size))
      return E;
    if (Strings.size() < Size)
      return reportError("truncated chars");

    OS << Indent << "    '";
    OS.write_escaped(Strings.slice(0, Size), /*hex=*/true);
    OS << "'\n";
    Strings = Strings.drop_front(Size);
  } while (--NumStrings);

  OS << Indent << "  }";
  return Error::success();
}

BitcodeAnalyzer::BitcodeAnalyzer(StringRef Buffer,
                                 std::optional<StringRef> BlockInfoBuffer)
    : Stream(Buffer) {
  if (BlockInfoBuffer)
    BlockInfoStream.emplace(*BlockInfoBuffer);
}

Error BitcodeAnalyzer::analyze(std::optional<BCDumpOptions> O,
                               std::optional<StringRef> CheckHash) {
  if (Error E = analyzeHeader(O, Stream).moveInto(CurStreamType))
    return E;

  Stream.setBlockInfo(&BlockInfo);

  // Read block info from BlockInfoStream, if specified.
  // The block info must be a top-level block.
  if (BlockInfoStream) {
    BitstreamCursor BlockInfoCursor(*BlockInfoStream);
    if (Error E = analyzeHeader(O, BlockInfoCursor).takeError())
      return E;

    while (!BlockInfoCursor.AtEndOfStream()) {
      Expected<unsigned> MaybeCode = BlockInfoCursor.ReadCode();
      if (!MaybeCode)
        return MaybeCode.takeError();
      if (MaybeCode.get() != bitc::ENTER_SUBBLOCK)
        return reportError("Invalid record at top-level in block info file");

      Expected<unsigned> MaybeBlockID = BlockInfoCursor.ReadSubBlockID();
      if (!MaybeBlockID)
        return MaybeBlockID.takeError();
      if (MaybeBlockID.get() == bitc::BLOCKINFO_BLOCK_ID) {
        std::optional<BitstreamBlockInfo> NewBlockInfo;
        if (Error E =
                BlockInfoCursor.ReadBlockInfoBlock(/*ReadBlockInfoNames=*/true)
                    .moveInto(NewBlockInfo))
          return E;
        if (!NewBlockInfo)
          return reportError("Malformed BlockInfoBlock in block info file");
        BlockInfo = std::move(*NewBlockInfo);
        break;
      }

      if (Error Err = BlockInfoCursor.SkipBlock())
        return Err;
    }
  }

  // Parse the top-level structure.  We only allow blocks at the top-level.
  while (!Stream.AtEndOfStream()) {
    Expected<unsigned> MaybeCode = Stream.ReadCode();
    if (!MaybeCode)
      return MaybeCode.takeError();
    if (MaybeCode.get() != bitc::ENTER_SUBBLOCK)
      return reportError("Invalid record at top-level");

    Expected<unsigned> MaybeBlockID = Stream.ReadSubBlockID();
    if (!MaybeBlockID)
      return MaybeBlockID.takeError();

    if (Error E = parseBlock(MaybeBlockID.get(), 0, O, CheckHash))
      return E;
    ++NumTopBlocks;
  }

  return Error::success();
}

void BitcodeAnalyzer::printStats(BCDumpOptions O,
                                 std::optional<StringRef> Filename) {
  uint64_t BufferSizeBits = Stream.getBitcodeBytes().size() * CHAR_BIT;
  // Print a summary of the read file.
  O.OS << "Summary ";
  if (Filename)
    O.OS << "of " << Filename->data() << ":\n";
  O.OS << "         Total size: ";
  printSize(O.OS, BufferSizeBits);
  O.OS << "\n";
  O.OS << "        Stream type: ";
  switch (CurStreamType) {
  case UnknownBitstream:
    O.OS << "unknown\n";
    break;
  case LLVMIRBitstream:
    O.OS << "LLVM IR\n";
    break;
  case ClangSerializedASTBitstream:
    O.OS << "Clang Serialized AST\n";
    break;
  case ClangSerializedDiagnosticsBitstream:
    O.OS << "Clang Serialized Diagnostics\n";
    break;
  case LLVMBitstreamRemarks:
    O.OS << "LLVM Remarks\n";
    break;
  }
  O.OS << "  # Toplevel Blocks: " << NumTopBlocks << "\n";
  O.OS << "\n";

  // Emit per-block stats.
  O.OS << "Per-block Summary:\n";
  for (const auto &Stat : BlockIDStats) {
    O.OS << "  Block ID #" << Stat.first;
    if (std::optional<const char *> BlockName =
            GetBlockName(Stat.first, BlockInfo, CurStreamType))
      O.OS << " (" << *BlockName << ")";
    O.OS << ":\n";

    const PerBlockIDStats &Stats = Stat.second;
    O.OS << "      Num Instances: " << Stats.NumInstances << "\n";
    O.OS << "         Total Size: ";
    printSize(O.OS, Stats.NumBits);
    O.OS << "\n";
    double pct = (Stats.NumBits * 100.0) / BufferSizeBits;
    O.OS << "    Percent of file: " << format("%2.4f%%", pct) << "\n";
    if (Stats.NumInstances > 1) {
      O.OS << "       Average Size: ";
      printSize(O.OS, Stats.NumBits / (double)Stats.NumInstances);
      O.OS << "\n";
      O.OS << "  Tot/Avg SubBlocks: " << Stats.NumSubBlocks << "/"
           << Stats.NumSubBlocks / (double)Stats.NumInstances << "\n";
      O.OS << "    Tot/Avg Abbrevs: " << Stats.NumAbbrevs << "/"
           << Stats.NumAbbrevs / (double)Stats.NumInstances << "\n";
      O.OS << "    Tot/Avg Records: " << Stats.NumRecords << "/"
           << Stats.NumRecords / (double)Stats.NumInstances << "\n";
    } else {
      O.OS << "      Num SubBlocks: " << Stats.NumSubBlocks << "\n";
      O.OS << "        Num Abbrevs: " << Stats.NumAbbrevs << "\n";
      O.OS << "        Num Records: " << Stats.NumRecords << "\n";
    }
    if (Stats.NumRecords) {
      double pct = (Stats.NumAbbreviatedRecords * 100.0) / Stats.NumRecords;
      O.OS << "    Percent Abbrevs: " << format("%2.4f%%", pct) << "\n";
    }
    O.OS << "\n";

    // Print a histogram of the codes we see.
    if (O.Histogram && !Stats.CodeFreq.empty()) {
      std::vector<std::pair<unsigned, unsigned>> FreqPairs; // <freq,code>
      for (unsigned i = 0, e = Stats.CodeFreq.size(); i != e; ++i)
        if (unsigned Freq = Stats.CodeFreq[i].NumInstances)
          FreqPairs.push_back(std::make_pair(Freq, i));
      llvm::stable_sort(FreqPairs);
      std::reverse(FreqPairs.begin(), FreqPairs.end());

      O.OS << "\tRecord Histogram:\n";
      O.OS << "\t\t  Count    # Bits     b/Rec   % Abv  Record Kind\n";
      for (const auto &FreqPair : FreqPairs) {
        const PerRecordStats &RecStats = Stats.CodeFreq[FreqPair.second];

        O.OS << format("\t\t%7d %9lu", RecStats.NumInstances,
                       (unsigned long)RecStats.TotalBits);

        if (RecStats.NumInstances > 1)
          O.OS << format(" %9.1f",
                         (double)RecStats.TotalBits / RecStats.NumInstances);
        else
          O.OS << "          ";

        if (RecStats.NumAbbrev)
          O.OS << format(" %7.2f", (double)RecStats.NumAbbrev /
                                       RecStats.NumInstances * 100);
        else
          O.OS << "        ";

        O.OS << "  ";
        if (std::optional<const char *> CodeName = GetCodeName(
                FreqPair.second, Stat.first, BlockInfo, CurStreamType))
          O.OS << *CodeName << "\n";
        else
          O.OS << "UnknownCode" << FreqPair.second << "\n";
      }
      O.OS << "\n";
    }
  }
}

Error BitcodeAnalyzer::parseBlock(unsigned BlockID, unsigned IndentLevel,
                                  std::optional<BCDumpOptions> O,
                                  std::optional<StringRef> CheckHash) {
  std::string Indent(IndentLevel * 2, ' ');
  uint64_t BlockBitStart = Stream.GetCurrentBitNo();

  // Get the statistics for this BlockID.
  PerBlockIDStats &BlockStats = BlockIDStats[BlockID];

  BlockStats.NumInstances++;

  // BLOCKINFO is a special part of the stream.
  bool DumpRecords = O.has_value();
  if (BlockID == bitc::BLOCKINFO_BLOCK_ID) {
    if (O && !O->DumpBlockinfo)
      O->OS << Indent << "<BLOCKINFO_BLOCK/>\n";
    std::optional<BitstreamBlockInfo> NewBlockInfo;
    if (Error E = Stream.ReadBlockInfoBlock(/*ReadBlockInfoNames=*/true)
                      .moveInto(NewBlockInfo))
      return E;
    if (!NewBlockInfo)
      return reportError("Malformed BlockInfoBlock");
    BlockInfo = std::move(*NewBlockInfo);
    if (Error Err = Stream.JumpToBit(BlockBitStart))
      return Err;
    // It's not really interesting to dump the contents of the blockinfo
    // block, so only do it if the user explicitly requests it.
    DumpRecords = O && O->DumpBlockinfo;
  }

  unsigned NumWords = 0;
  if (Error Err = Stream.EnterSubBlock(BlockID, &NumWords))
    return Err;

  // Keep it for later, when we see a MODULE_HASH record
  uint64_t BlockEntryPos = Stream.getCurrentByteNo();

  std::optional<const char *> BlockName;
  if (DumpRecords) {
    O->OS << Indent << "<";
    if ((BlockName = GetBlockName(BlockID, BlockInfo, CurStreamType)))
      O->OS << *BlockName;
    else
      O->OS << "UnknownBlock" << BlockID;

    if (!O->Symbolic && BlockName)
      O->OS << " BlockID=" << BlockID;

    O->OS << " NumWords=" << NumWords
          << " BlockCodeSize=" << Stream.getAbbrevIDWidth() << ">\n";
  }

  SmallVector<uint64_t, 64> Record;

  // Keep the offset to the metadata index if seen.
  uint64_t MetadataIndexOffset = 0;

  // Read all the records for this block.
  while (true) {
    if (Stream.AtEndOfStream())
      return reportError("Premature end of bitstream");

    uint64_t RecordStartBit = Stream.GetCurrentBitNo();

    BitstreamEntry Entry;
    if (Error E = Stream.advance(BitstreamCursor::AF_DontAutoprocessAbbrevs)
                      .moveInto(Entry))
      return E;

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return reportError("malformed bitcode file");
    case BitstreamEntry::EndBlock: {
      uint64_t BlockBitEnd = Stream.GetCurrentBitNo();
      BlockStats.NumBits += BlockBitEnd - BlockBitStart;
      if (DumpRecords) {
        O->OS << Indent << "</";
        if (BlockName)
          O->OS << *BlockName << ">\n";
        else
          O->OS << "UnknownBlock" << BlockID << ">\n";
      }
      return Error::success();
    }

    case BitstreamEntry::SubBlock: {
      uint64_t SubBlockBitStart = Stream.GetCurrentBitNo();
      if (Error E = parseBlock(Entry.ID, IndentLevel + 1, O, CheckHash))
        return E;
      ++BlockStats.NumSubBlocks;
      uint64_t SubBlockBitEnd = Stream.GetCurrentBitNo();

      // Don't include subblock sizes in the size of this block.
      BlockBitStart += SubBlockBitEnd - SubBlockBitStart;
      continue;
    }
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    if (Entry.ID == bitc::DEFINE_ABBREV) {
      if (Error Err = Stream.ReadAbbrevRecord())
        return Err;
      ++BlockStats.NumAbbrevs;
      continue;
    }

    Record.clear();

    ++BlockStats.NumRecords;

    StringRef Blob;
    uint64_t CurrentRecordPos = Stream.GetCurrentBitNo();
    unsigned Code;
    if (Error E = Stream.readRecord(Entry.ID, Record, &Blob).moveInto(Code))
      return E;

    // Increment the # occurrences of this code.
    if (BlockStats.CodeFreq.size() <= Code)
      BlockStats.CodeFreq.resize(Code + 1);
    BlockStats.CodeFreq[Code].NumInstances++;
    BlockStats.CodeFreq[Code].TotalBits +=
        Stream.GetCurrentBitNo() - RecordStartBit;
    if (Entry.ID != bitc::UNABBREV_RECORD) {
      BlockStats.CodeFreq[Code].NumAbbrev++;
      ++BlockStats.NumAbbreviatedRecords;
    }

    if (DumpRecords) {
      O->OS << Indent << "  <";
      std::optional<const char *> CodeName =
          GetCodeName(Code, BlockID, BlockInfo, CurStreamType);
      if (CodeName)
        O->OS << *CodeName;
      else
        O->OS << "UnknownCode" << Code;
      if (!O->Symbolic && CodeName)
        O->OS << " codeid=" << Code;
      const BitCodeAbbrev *Abbv = nullptr;
      if (Entry.ID != bitc::UNABBREV_RECORD) {
        Expected<const BitCodeAbbrev *> MaybeAbbv = Stream.getAbbrev(Entry.ID);
        if (!MaybeAbbv)
          return MaybeAbbv.takeError();
        Abbv = MaybeAbbv.get();
        O->OS << " abbrevid=" << Entry.ID;
      }

      for (unsigned i = 0, e = Record.size(); i != e; ++i)
        O->OS << " op" << i << "=" << (int64_t)Record[i];

      // If we found a metadata index, let's verify that we had an offset
      // before and validate its forward reference offset was correct!
      if (BlockID == bitc::METADATA_BLOCK_ID) {
        if (Code == bitc::METADATA_INDEX_OFFSET) {
          if (Record.size() != 2)
            O->OS << "(Invalid record)";
          else {
            auto Offset = Record[0] + (Record[1] << 32);
            MetadataIndexOffset = Stream.GetCurrentBitNo() + Offset;
          }
        }
        if (Code == bitc::METADATA_INDEX) {
          O->OS << " (offset ";
          if (MetadataIndexOffset == RecordStartBit)
            O->OS << "match)";
          else
            O->OS << "mismatch: " << MetadataIndexOffset << " vs "
                  << RecordStartBit << ")";
        }
      }

      // If we found a module hash, let's verify that it matches!
      if (BlockID == bitc::MODULE_BLOCK_ID && Code == bitc::MODULE_CODE_HASH &&
          CheckHash) {
        if (Record.size() != 5)
          O->OS << " (invalid)";
        else {
          // Recompute the hash and compare it to the one in the bitcode
          SHA1 Hasher;
          std::array<uint8_t, 20> Hash;
          Hasher.update(*CheckHash);
          {
            int BlockSize = (CurrentRecordPos / 8) - BlockEntryPos;
            auto Ptr = Stream.getPointerToByte(BlockEntryPos, BlockSize);
            Hasher.update(ArrayRef<uint8_t>(Ptr, BlockSize));
            Hash = Hasher.result();
          }
          std::array<uint8_t, 20> RecordedHash;
          int Pos = 0;
          for (auto &Val : Record) {
            assert(!(Val >> 32) && "Unexpected high bits set");
            support::endian::write32be(&RecordedHash[Pos], Val);
            Pos += 4;
          }
          if (Hash == RecordedHash)
            O->OS << " (match)";
          else
            O->OS << " (!mismatch!)";
        }
      }

      O->OS << "/>";

      if (Abbv) {
        for (unsigned i = 1, e = Abbv->getNumOperandInfos(); i != e; ++i) {
          const BitCodeAbbrevOp &Op = Abbv->getOperandInfo(i);
          if (!Op.isEncoding() || Op.getEncoding() != BitCodeAbbrevOp::Array)
            continue;
          assert(i + 2 == e && "Array op not second to last");
          std::string Str;
          bool ArrayIsPrintable = true;
          for (unsigned j = i - 1, je = Record.size(); j != je; ++j) {
            if (!isPrint(static_cast<unsigned char>(Record[j]))) {
              ArrayIsPrintable = false;
              break;
            }
            Str += (char)Record[j];
          }
          if (ArrayIsPrintable)
            O->OS << " record string = '" << Str << "'";
          break;
        }
      }

      if (Blob.data()) {
        if (canDecodeBlob(Code, BlockID)) {
          if (Error E = decodeMetadataStringsBlob(Indent, Record, Blob, O->OS))
            return E;
        } else {
          O->OS << " blob data = ";
          if (O->ShowBinaryBlobs) {
            O->OS << "'";
            O->OS.write_escaped(Blob, /*hex=*/true) << "'";
          } else {
            bool BlobIsPrintable = true;
            for (char C : Blob)
              if (!isPrint(static_cast<unsigned char>(C))) {
                BlobIsPrintable = false;
                break;
              }

            if (BlobIsPrintable)
              O->OS << "'" << Blob << "'";
            else
              O->OS << "unprintable, " << Blob.size() << " bytes.";
          }
        }
      }

      O->OS << "\n";
    }

    // Make sure that we can skip the current record.
    if (Error Err = Stream.JumpToBit(CurrentRecordPos))
      return Err;
    if (Expected<unsigned> Skipped = Stream.skipRecord(Entry.ID))
      ; // Do nothing.
    else
      return Skipped.takeError();
  }
}

