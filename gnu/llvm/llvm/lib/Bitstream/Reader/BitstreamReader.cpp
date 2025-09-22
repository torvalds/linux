//===- BitstreamReader.cpp - BitstreamReader implementation ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <optional>
#include <string>

using namespace llvm;

//===----------------------------------------------------------------------===//
//  BitstreamCursor implementation
//===----------------------------------------------------------------------===//
//
static Error error(const char *Message) {
  return createStringError(std::errc::illegal_byte_sequence, Message);
}

/// Having read the ENTER_SUBBLOCK abbrevid, enter the block.
Error BitstreamCursor::EnterSubBlock(unsigned BlockID, unsigned *NumWordsP) {
  // Save the current block's state on BlockScope.
  BlockScope.push_back(Block(CurCodeSize));
  BlockScope.back().PrevAbbrevs.swap(CurAbbrevs);

  // Add the abbrevs specific to this block to the CurAbbrevs list.
  if (BlockInfo) {
    if (const BitstreamBlockInfo::BlockInfo *Info =
            BlockInfo->getBlockInfo(BlockID)) {
      llvm::append_range(CurAbbrevs, Info->Abbrevs);
    }
  }

  // Get the codesize of this block.
  Expected<uint32_t> MaybeVBR = ReadVBR(bitc::CodeLenWidth);
  if (!MaybeVBR)
    return MaybeVBR.takeError();
  CurCodeSize = MaybeVBR.get();

  if (CurCodeSize > MaxChunkSize)
    return llvm::createStringError(
        std::errc::illegal_byte_sequence,
        "can't read more than %zu at a time, trying to read %u", +MaxChunkSize,
        CurCodeSize);

  SkipToFourByteBoundary();
  Expected<word_t> MaybeNum = Read(bitc::BlockSizeWidth);
  if (!MaybeNum)
    return MaybeNum.takeError();
  word_t NumWords = MaybeNum.get();
  if (NumWordsP)
    *NumWordsP = NumWords;

  if (CurCodeSize == 0)
    return llvm::createStringError(
        std::errc::illegal_byte_sequence,
        "can't enter sub-block: current code size is 0");
  if (AtEndOfStream())
    return llvm::createStringError(
        std::errc::illegal_byte_sequence,
        "can't enter sub block: already at end of stream");

  return Error::success();
}

static Expected<uint64_t> readAbbreviatedField(BitstreamCursor &Cursor,
                                               const BitCodeAbbrevOp &Op) {
  assert(!Op.isLiteral() && "Not to be used with literals!");

  // Decode the value as we are commanded.
  switch (Op.getEncoding()) {
  case BitCodeAbbrevOp::Array:
  case BitCodeAbbrevOp::Blob:
    llvm_unreachable("Should not reach here");
  case BitCodeAbbrevOp::Fixed:
    assert((unsigned)Op.getEncodingData() <= Cursor.MaxChunkSize);
    return Cursor.Read((unsigned)Op.getEncodingData());
  case BitCodeAbbrevOp::VBR:
    assert((unsigned)Op.getEncodingData() <= Cursor.MaxChunkSize);
    return Cursor.ReadVBR64((unsigned)Op.getEncodingData());
  case BitCodeAbbrevOp::Char6:
    if (Expected<unsigned> Res = Cursor.Read(6))
      return BitCodeAbbrevOp::DecodeChar6(Res.get());
    else
      return Res.takeError();
  }
  llvm_unreachable("invalid abbreviation encoding");
}

/// skipRecord - Read the current record and discard it.
Expected<unsigned> BitstreamCursor::skipRecord(unsigned AbbrevID) {
  // Skip unabbreviated records by reading past their entries.
  if (AbbrevID == bitc::UNABBREV_RECORD) {
    Expected<uint32_t> MaybeCode = ReadVBR(6);
    if (!MaybeCode)
      return MaybeCode.takeError();
    unsigned Code = MaybeCode.get();
    Expected<uint32_t> MaybeVBR = ReadVBR(6);
    if (!MaybeVBR)
      return MaybeVBR.takeError();
    unsigned NumElts = MaybeVBR.get();
    for (unsigned i = 0; i != NumElts; ++i)
      if (Expected<uint64_t> Res = ReadVBR64(6))
        ; // Skip!
      else
        return Res.takeError();
    return Code;
  }

  Expected<const BitCodeAbbrev *> MaybeAbbv = getAbbrev(AbbrevID);
  if (!MaybeAbbv)
    return MaybeAbbv.takeError();

  const BitCodeAbbrev *Abbv = MaybeAbbv.get();
  const BitCodeAbbrevOp &CodeOp = Abbv->getOperandInfo(0);
  unsigned Code;
  if (CodeOp.isLiteral())
    Code = CodeOp.getLiteralValue();
  else {
    if (CodeOp.getEncoding() == BitCodeAbbrevOp::Array ||
        CodeOp.getEncoding() == BitCodeAbbrevOp::Blob)
      return llvm::createStringError(
          std::errc::illegal_byte_sequence,
          "Abbreviation starts with an Array or a Blob");
    Expected<uint64_t> MaybeCode = readAbbreviatedField(*this, CodeOp);
    if (!MaybeCode)
      return MaybeCode.takeError();
    Code = MaybeCode.get();
  }

  for (unsigned i = 1, e = Abbv->getNumOperandInfos(); i < e; ++i) {
    const BitCodeAbbrevOp &Op = Abbv->getOperandInfo(i);
    if (Op.isLiteral())
      continue;

    if (Op.getEncoding() != BitCodeAbbrevOp::Array &&
        Op.getEncoding() != BitCodeAbbrevOp::Blob) {
      if (Expected<uint64_t> MaybeField = readAbbreviatedField(*this, Op))
        continue;
      else
        return MaybeField.takeError();
    }

    if (Op.getEncoding() == BitCodeAbbrevOp::Array) {
      // Array case.  Read the number of elements as a vbr6.
      Expected<uint32_t> MaybeNum = ReadVBR(6);
      if (!MaybeNum)
        return MaybeNum.takeError();
      unsigned NumElts = MaybeNum.get();

      // Get the element encoding.
      assert(i+2 == e && "array op not second to last?");
      const BitCodeAbbrevOp &EltEnc = Abbv->getOperandInfo(++i);

      // Read all the elements.
      // Decode the value as we are commanded.
      switch (EltEnc.getEncoding()) {
      default:
        return error("Array element type can't be an Array or a Blob");
      case BitCodeAbbrevOp::Fixed:
        assert((unsigned)EltEnc.getEncodingData() <= MaxChunkSize);
        if (Error Err =
                JumpToBit(GetCurrentBitNo() + static_cast<uint64_t>(NumElts) *
                                                  EltEnc.getEncodingData()))
          return Err;
        break;
      case BitCodeAbbrevOp::VBR:
        assert((unsigned)EltEnc.getEncodingData() <= MaxChunkSize);
        for (; NumElts; --NumElts)
          if (Expected<uint64_t> Res =
                  ReadVBR64((unsigned)EltEnc.getEncodingData()))
            ; // Skip!
          else
            return Res.takeError();
        break;
      case BitCodeAbbrevOp::Char6:
        if (Error Err = JumpToBit(GetCurrentBitNo() + NumElts * 6))
          return Err;
        break;
      }
      continue;
    }

    assert(Op.getEncoding() == BitCodeAbbrevOp::Blob);
    // Blob case.  Read the number of bytes as a vbr6.
    Expected<uint32_t> MaybeNum = ReadVBR(6);
    if (!MaybeNum)
      return MaybeNum.takeError();
    unsigned NumElts = MaybeNum.get();
    SkipToFourByteBoundary();  // 32-bit alignment

    // Figure out where the end of this blob will be including tail padding.
    const size_t NewEnd = GetCurrentBitNo() + alignTo(NumElts, 4) * 8;

    // If this would read off the end of the bitcode file, just set the
    // record to empty and return.
    if (!canSkipToPos(NewEnd/8)) {
      skipToEnd();
      break;
    }

    // Skip over the blob.
    if (Error Err = JumpToBit(NewEnd))
      return Err;
  }
  return Code;
}

Expected<unsigned> BitstreamCursor::readRecord(unsigned AbbrevID,
                                               SmallVectorImpl<uint64_t> &Vals,
                                               StringRef *Blob) {
  if (AbbrevID == bitc::UNABBREV_RECORD) {
    Expected<uint32_t> MaybeCode = ReadVBR(6);
    if (!MaybeCode)
      return MaybeCode.takeError();
    uint32_t Code = MaybeCode.get();
    Expected<uint32_t> MaybeNumElts = ReadVBR(6);
    if (!MaybeNumElts)
      return error(
          ("Failed to read size: " + toString(MaybeNumElts.takeError()))
              .c_str());
    uint32_t NumElts = MaybeNumElts.get();
    if (!isSizePlausible(NumElts))
      return error("Size is not plausible");
    Vals.reserve(Vals.size() + NumElts);

    for (unsigned i = 0; i != NumElts; ++i)
      if (Expected<uint64_t> MaybeVal = ReadVBR64(6))
        Vals.push_back(MaybeVal.get());
      else
        return MaybeVal.takeError();
    return Code;
  }

  Expected<const BitCodeAbbrev *> MaybeAbbv = getAbbrev(AbbrevID);
  if (!MaybeAbbv)
    return MaybeAbbv.takeError();
  const BitCodeAbbrev *Abbv = MaybeAbbv.get();

  // Read the record code first.
  assert(Abbv->getNumOperandInfos() != 0 && "no record code in abbreviation?");
  const BitCodeAbbrevOp &CodeOp = Abbv->getOperandInfo(0);
  unsigned Code;
  if (CodeOp.isLiteral())
    Code = CodeOp.getLiteralValue();
  else {
    if (CodeOp.getEncoding() == BitCodeAbbrevOp::Array ||
        CodeOp.getEncoding() == BitCodeAbbrevOp::Blob)
      return error("Abbreviation starts with an Array or a Blob");
    if (Expected<uint64_t> MaybeCode = readAbbreviatedField(*this, CodeOp))
      Code = MaybeCode.get();
    else
      return MaybeCode.takeError();
  }

  for (unsigned i = 1, e = Abbv->getNumOperandInfos(); i != e; ++i) {
    const BitCodeAbbrevOp &Op = Abbv->getOperandInfo(i);
    if (Op.isLiteral()) {
      Vals.push_back(Op.getLiteralValue());
      continue;
    }

    if (Op.getEncoding() != BitCodeAbbrevOp::Array &&
        Op.getEncoding() != BitCodeAbbrevOp::Blob) {
      if (Expected<uint64_t> MaybeVal = readAbbreviatedField(*this, Op))
        Vals.push_back(MaybeVal.get());
      else
        return MaybeVal.takeError();
      continue;
    }

    if (Op.getEncoding() == BitCodeAbbrevOp::Array) {
      // Array case.  Read the number of elements as a vbr6.
      Expected<uint32_t> MaybeNumElts = ReadVBR(6);
      if (!MaybeNumElts)
        return error(
            ("Failed to read size: " + toString(MaybeNumElts.takeError()))
                .c_str());
      uint32_t NumElts = MaybeNumElts.get();
      if (!isSizePlausible(NumElts))
        return error("Size is not plausible");
      Vals.reserve(Vals.size() + NumElts);

      // Get the element encoding.
      if (i + 2 != e)
        return error("Array op not second to last");
      const BitCodeAbbrevOp &EltEnc = Abbv->getOperandInfo(++i);
      if (!EltEnc.isEncoding())
        return error(
            "Array element type has to be an encoding of a type");

      // Read all the elements.
      switch (EltEnc.getEncoding()) {
      default:
        return error("Array element type can't be an Array or a Blob");
      case BitCodeAbbrevOp::Fixed:
        for (; NumElts; --NumElts)
          if (Expected<SimpleBitstreamCursor::word_t> MaybeVal =
                  Read((unsigned)EltEnc.getEncodingData()))
            Vals.push_back(MaybeVal.get());
          else
            return MaybeVal.takeError();
        break;
      case BitCodeAbbrevOp::VBR:
        for (; NumElts; --NumElts)
          if (Expected<uint64_t> MaybeVal =
                  ReadVBR64((unsigned)EltEnc.getEncodingData()))
            Vals.push_back(MaybeVal.get());
          else
            return MaybeVal.takeError();
        break;
      case BitCodeAbbrevOp::Char6:
        for (; NumElts; --NumElts)
          if (Expected<SimpleBitstreamCursor::word_t> MaybeVal = Read(6))
            Vals.push_back(BitCodeAbbrevOp::DecodeChar6(MaybeVal.get()));
          else
            return MaybeVal.takeError();
      }
      continue;
    }

    assert(Op.getEncoding() == BitCodeAbbrevOp::Blob);
    // Blob case.  Read the number of bytes as a vbr6.
    Expected<uint32_t> MaybeNumElts = ReadVBR(6);
    if (!MaybeNumElts)
      return MaybeNumElts.takeError();
    uint32_t NumElts = MaybeNumElts.get();
    SkipToFourByteBoundary();  // 32-bit alignment

    // Figure out where the end of this blob will be including tail padding.
    size_t CurBitPos = GetCurrentBitNo();
    const size_t NewEnd = CurBitPos + alignTo(NumElts, 4) * 8;

    // Make sure the bitstream is large enough to contain the blob.
    if (!canSkipToPos(NewEnd/8))
      return error("Blob ends too soon");

    // Otherwise, inform the streamer that we need these bytes in memory.  Skip
    // over tail padding first, in case jumping to NewEnd invalidates the Blob
    // pointer.
    if (Error Err = JumpToBit(NewEnd))
      return Err;
    const char *Ptr = (const char *)getPointerToBit(CurBitPos, NumElts);

    // If we can return a reference to the data, do so to avoid copying it.
    if (Blob) {
      *Blob = StringRef(Ptr, NumElts);
    } else {
      // Otherwise, unpack into Vals with zero extension.
      auto *UPtr = reinterpret_cast<const unsigned char *>(Ptr);
      Vals.append(UPtr, UPtr + NumElts);
    }
  }

  return Code;
}

Error BitstreamCursor::ReadAbbrevRecord() {
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Expected<uint32_t> MaybeNumOpInfo = ReadVBR(5);
  if (!MaybeNumOpInfo)
    return MaybeNumOpInfo.takeError();
  unsigned NumOpInfo = MaybeNumOpInfo.get();
  for (unsigned i = 0; i != NumOpInfo; ++i) {
    Expected<word_t> MaybeIsLiteral = Read(1);
    if (!MaybeIsLiteral)
      return MaybeIsLiteral.takeError();
    bool IsLiteral = MaybeIsLiteral.get();
    if (IsLiteral) {
      Expected<uint64_t> MaybeOp = ReadVBR64(8);
      if (!MaybeOp)
        return MaybeOp.takeError();
      Abbv->Add(BitCodeAbbrevOp(MaybeOp.get()));
      continue;
    }

    Expected<word_t> MaybeEncoding = Read(3);
    if (!MaybeEncoding)
      return MaybeEncoding.takeError();
    if (!BitCodeAbbrevOp::isValidEncoding(MaybeEncoding.get()))
      return error("Invalid encoding");

    BitCodeAbbrevOp::Encoding E =
        (BitCodeAbbrevOp::Encoding)MaybeEncoding.get();
    if (BitCodeAbbrevOp::hasEncodingData(E)) {
      Expected<uint64_t> MaybeData = ReadVBR64(5);
      if (!MaybeData)
        return MaybeData.takeError();
      uint64_t Data = MaybeData.get();

      // As a special case, handle fixed(0) (i.e., a fixed field with zero bits)
      // and vbr(0) as a literal zero.  This is decoded the same way, and avoids
      // a slow path in Read() to have to handle reading zero bits.
      if ((E == BitCodeAbbrevOp::Fixed || E == BitCodeAbbrevOp::VBR) &&
          Data == 0) {
        Abbv->Add(BitCodeAbbrevOp(0));
        continue;
      }

      if ((E == BitCodeAbbrevOp::Fixed || E == BitCodeAbbrevOp::VBR) &&
          Data > MaxChunkSize)
        return error("Fixed or VBR abbrev record with size > MaxChunkData");

      Abbv->Add(BitCodeAbbrevOp(E, Data));
    } else
      Abbv->Add(BitCodeAbbrevOp(E));
  }

  if (Abbv->getNumOperandInfos() == 0)
    return error("Abbrev record with no operands");
  CurAbbrevs.push_back(std::move(Abbv));

  return Error::success();
}

Expected<std::optional<BitstreamBlockInfo>>
BitstreamCursor::ReadBlockInfoBlock(bool ReadBlockInfoNames) {
  if (llvm::Error Err = EnterSubBlock(bitc::BLOCKINFO_BLOCK_ID))
    return Err;

  BitstreamBlockInfo NewBlockInfo;

  SmallVector<uint64_t, 64> Record;
  BitstreamBlockInfo::BlockInfo *CurBlockInfo = nullptr;

  // Read all the records for this module.
  while (true) {
    Expected<BitstreamEntry> MaybeEntry =
        advanceSkippingSubblocks(AF_DontAutoprocessAbbrevs);
    if (!MaybeEntry)
      return MaybeEntry.takeError();
    BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock: // Handled for us already.
    case llvm::BitstreamEntry::Error:
      return std::nullopt;
    case llvm::BitstreamEntry::EndBlock:
      return std::move(NewBlockInfo);
    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read abbrev records, associate them with CurBID.
    if (Entry.ID == bitc::DEFINE_ABBREV) {
      if (!CurBlockInfo)
        return std::nullopt;
      if (Error Err = ReadAbbrevRecord())
        return Err;

      // ReadAbbrevRecord installs the abbrev in CurAbbrevs.  Move it to the
      // appropriate BlockInfo.
      CurBlockInfo->Abbrevs.push_back(std::move(CurAbbrevs.back()));
      CurAbbrevs.pop_back();
      continue;
    }

    // Read a record.
    Record.clear();
    Expected<unsigned> MaybeBlockInfo = readRecord(Entry.ID, Record);
    if (!MaybeBlockInfo)
      return MaybeBlockInfo.takeError();
    switch (MaybeBlockInfo.get()) {
    default:
      break; // Default behavior, ignore unknown content.
    case bitc::BLOCKINFO_CODE_SETBID:
      if (Record.size() < 1)
        return std::nullopt;
      CurBlockInfo = &NewBlockInfo.getOrCreateBlockInfo((unsigned)Record[0]);
      break;
    case bitc::BLOCKINFO_CODE_BLOCKNAME: {
      if (!CurBlockInfo)
        return std::nullopt;
      if (!ReadBlockInfoNames)
        break; // Ignore name.
      CurBlockInfo->Name = std::string(Record.begin(), Record.end());
      break;
    }
      case bitc::BLOCKINFO_CODE_SETRECORDNAME: {
      if (!CurBlockInfo)
        return std::nullopt;
      if (!ReadBlockInfoNames)
        break; // Ignore name.
      CurBlockInfo->RecordNames.emplace_back(
          (unsigned)Record[0], std::string(Record.begin() + 1, Record.end()));
      break;
      }
      }
  }
}
