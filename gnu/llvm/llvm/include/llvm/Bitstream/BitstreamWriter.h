//===- BitstreamWriter.h - Low-level bitstream writer interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines the BitstreamWriter class.  This class can be used to
// write an arbitrary bitstream, regardless of its contents.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITSTREAM_BITSTREAMWRITER_H
#define LLVM_BITSTREAM_BITSTREAMWRITER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitCodes.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <optional>
#include <vector>

namespace llvm {

class BitstreamWriter {
  /// Owned buffer, used to init Buffer if the provided stream doesn't happen to
  /// be a buffer itself.
  SmallVector<char, 0> OwnBuffer;
  /// Internal buffer for unflushed bytes (unless there is no stream to flush
  /// to, case in which these are "the bytes"). The writer backpatches, so it is
  /// efficient to buffer.
  SmallVectorImpl<char> &Buffer;

  /// FS - The file stream that Buffer flushes to. If FS is a raw_fd_stream, the
  /// writer will incrementally flush at subblock boundaries. Otherwise flushing
  /// will happen at the end of BitstreamWriter's lifetime.
  raw_ostream *const FS;

  /// FlushThreshold - this is the threshold (unit B) to flush to FS, if FS is a
  /// raw_fd_stream.
  const uint64_t FlushThreshold;

  /// CurBit - Always between 0 and 31 inclusive, specifies the next bit to use.
  unsigned CurBit = 0;

  /// CurValue - The current value. Only bits < CurBit are valid.
  uint32_t CurValue = 0;

  /// CurCodeSize - This is the declared size of code values used for the
  /// current block, in bits.
  unsigned CurCodeSize = 2;

  /// BlockInfoCurBID - When emitting a BLOCKINFO_BLOCK, this is the currently
  /// selected BLOCK ID.
  unsigned BlockInfoCurBID = 0;

  /// CurAbbrevs - Abbrevs installed at in this block.
  std::vector<std::shared_ptr<BitCodeAbbrev>> CurAbbrevs;

  // Support for retrieving a section of the output, for purposes such as
  // checksumming.
  std::optional<size_t> BlockFlushingStartPos;

  struct Block {
    unsigned PrevCodeSize;
    size_t StartSizeWord;
    std::vector<std::shared_ptr<BitCodeAbbrev>> PrevAbbrevs;
    Block(unsigned PCS, size_t SSW) : PrevCodeSize(PCS), StartSizeWord(SSW) {}
  };

  /// BlockScope - This tracks the current blocks that we have entered.
  std::vector<Block> BlockScope;

  /// BlockInfo - This contains information emitted to BLOCKINFO_BLOCK blocks.
  /// These describe abbreviations that all blocks of the specified ID inherit.
  struct BlockInfo {
    unsigned BlockID;
    std::vector<std::shared_ptr<BitCodeAbbrev>> Abbrevs;
  };
  std::vector<BlockInfo> BlockInfoRecords;

  void WriteWord(unsigned Value) {
    Value =
        support::endian::byte_swap<uint32_t, llvm::endianness::little>(Value);
    Buffer.append(reinterpret_cast<const char *>(&Value),
                  reinterpret_cast<const char *>(&Value + 1));
  }

  uint64_t GetNumOfFlushedBytes() const {
    return fdStream() ? fdStream()->tell() : 0;
  }

  size_t GetBufferOffset() const {
    return Buffer.size() + GetNumOfFlushedBytes();
  }

  size_t GetWordIndex() const {
    size_t Offset = GetBufferOffset();
    assert((Offset & 3) == 0 && "Not 32-bit aligned");
    return Offset / 4;
  }

  void flushAndClear() {
    assert(FS);
    assert(!Buffer.empty());
    assert(!BlockFlushingStartPos &&
           "a call to markAndBlockFlushing should have been paired with a "
           "call to getMarkedBufferAndResumeFlushing");
    FS->write(Buffer.data(), Buffer.size());
    Buffer.clear();
  }

  /// If the related file stream is a raw_fd_stream, flush the buffer if its
  /// size is above a threshold. If \p OnClosing is true, flushing happens
  /// regardless of thresholds.
  void FlushToFile(bool OnClosing = false) {
    if (!FS || Buffer.empty())
      return;
    if (OnClosing)
      return flushAndClear();
    if (BlockFlushingStartPos)
      return;
    if (fdStream() && Buffer.size() > FlushThreshold)
      flushAndClear();
  }

  raw_fd_stream *fdStream() { return dyn_cast_or_null<raw_fd_stream>(FS); }

  const raw_fd_stream *fdStream() const {
    return dyn_cast_or_null<raw_fd_stream>(FS);
  }

  SmallVectorImpl<char> &getInternalBufferFromStream(raw_ostream &OutStream) {
    if (auto *SV = dyn_cast<raw_svector_ostream>(&OutStream))
      return SV->buffer();
    return OwnBuffer;
  }

public:
  /// Create a BitstreamWriter over a raw_ostream \p OutStream.
  /// If \p OutStream is a raw_svector_ostream, the BitstreamWriter will write
  /// directly to the latter's buffer. In all other cases, the BitstreamWriter
  /// will use an internal buffer and flush at the end of its lifetime.
  ///
  /// In addition, if \p is a raw_fd_stream supporting seek, tell, and read
  /// (besides write), the BitstreamWriter will also flush incrementally, when a
  /// subblock is finished, and if the FlushThreshold is passed.
  ///
  /// NOTE: \p FlushThreshold's unit is MB.
  BitstreamWriter(raw_ostream &OutStream, uint32_t FlushThreshold = 512)
      : Buffer(getInternalBufferFromStream(OutStream)),
        FS(!isa<raw_svector_ostream>(OutStream) ? &OutStream : nullptr),
        FlushThreshold(uint64_t(FlushThreshold) << 20) {}

  /// Convenience constructor for users that start with a vector - avoids
  /// needing to wrap it in a raw_svector_ostream.
  BitstreamWriter(SmallVectorImpl<char> &Buff)
      : Buffer(Buff), FS(nullptr), FlushThreshold(0) {}

  ~BitstreamWriter() {
    FlushToWord();
    assert(BlockScope.empty() && CurAbbrevs.empty() && "Block imbalance");
    FlushToFile(/*OnClosing=*/true);
  }

  /// For scenarios where the user wants to access a section of the stream to
  /// (for example) compute some checksum, disable flushing and remember the
  /// position in the internal buffer where that happened. Must be paired with a
  /// call to getMarkedBufferAndResumeFlushing.
  void markAndBlockFlushing() {
    assert(!BlockFlushingStartPos);
    BlockFlushingStartPos = Buffer.size();
  }

  /// resumes flushing, but does not flush, and returns the section in the
  /// internal buffer starting from the position marked with
  /// markAndBlockFlushing. The return should be processed before any additional
  /// calls to this object, because those may cause a flush and invalidate the
  /// return.
  StringRef getMarkedBufferAndResumeFlushing() {
    assert(BlockFlushingStartPos);
    size_t Start = *BlockFlushingStartPos;
    BlockFlushingStartPos.reset();
    return {&Buffer[Start], Buffer.size() - Start};
  }

  /// Retrieve the current position in the stream, in bits.
  uint64_t GetCurrentBitNo() const { return GetBufferOffset() * 8 + CurBit; }

  /// Retrieve the number of bits currently used to encode an abbrev ID.
  unsigned GetAbbrevIDWidth() const { return CurCodeSize; }

  //===--------------------------------------------------------------------===//
  // Basic Primitives for emitting bits to the stream.
  //===--------------------------------------------------------------------===//

  /// Backpatch a byte in the output at the given bit offset with the specified
  /// value.
  void BackpatchByte(uint64_t BitNo, uint8_t NewByte) {
    using namespace llvm::support;
    uint64_t ByteNo = BitNo / 8;
    uint64_t StartBit = BitNo & 7;
    uint64_t NumOfFlushedBytes = GetNumOfFlushedBytes();

    if (ByteNo >= NumOfFlushedBytes) {
      assert((!endian::readAtBitAlignment<uint8_t, llvm::endianness::little,
                                          unaligned>(
                 &Buffer[ByteNo - NumOfFlushedBytes], StartBit)) &&
             "Expected to be patching over 0-value placeholders");
      endian::writeAtBitAlignment<uint8_t, llvm::endianness::little, unaligned>(
          &Buffer[ByteNo - NumOfFlushedBytes], NewByte, StartBit);
      return;
    }

    // If we don't have a raw_fd_stream, GetNumOfFlushedBytes() should have
    // returned 0, and we shouldn't be here.
    assert(fdStream() != nullptr);
    // If the byte offset to backpatch is flushed, use seek to backfill data.
    // First, save the file position to restore later.
    uint64_t CurPos = fdStream()->tell();

    // Copy data to update into Bytes from the file FS and the buffer Out.
    char Bytes[3]; // Use one more byte to silence a warning from Visual C++.
    size_t BytesNum = StartBit ? 2 : 1;
    size_t BytesFromDisk = std::min(static_cast<uint64_t>(BytesNum), NumOfFlushedBytes - ByteNo);
    size_t BytesFromBuffer = BytesNum - BytesFromDisk;

    // When unaligned, copy existing data into Bytes from the file FS and the
    // buffer Buffer so that it can be updated before writing. For debug builds
    // read bytes unconditionally in order to check that the existing value is 0
    // as expected.
#ifdef NDEBUG
    if (StartBit)
#endif
    {
      fdStream()->seek(ByteNo);
      ssize_t BytesRead = fdStream()->read(Bytes, BytesFromDisk);
      (void)BytesRead; // silence warning
      assert(BytesRead >= 0 && static_cast<size_t>(BytesRead) == BytesFromDisk);
      for (size_t i = 0; i < BytesFromBuffer; ++i)
        Bytes[BytesFromDisk + i] = Buffer[i];
      assert((!endian::readAtBitAlignment<uint8_t, llvm::endianness::little,
                                          unaligned>(Bytes, StartBit)) &&
             "Expected to be patching over 0-value placeholders");
    }

    // Update Bytes in terms of bit offset and value.
    endian::writeAtBitAlignment<uint8_t, llvm::endianness::little, unaligned>(
        Bytes, NewByte, StartBit);

    // Copy updated data back to the file FS and the buffer Out.
    fdStream()->seek(ByteNo);
    fdStream()->write(Bytes, BytesFromDisk);
    for (size_t i = 0; i < BytesFromBuffer; ++i)
      Buffer[i] = Bytes[BytesFromDisk + i];

    // Restore the file position.
    fdStream()->seek(CurPos);
  }

  void BackpatchHalfWord(uint64_t BitNo, uint16_t Val) {
    BackpatchByte(BitNo, (uint8_t)Val);
    BackpatchByte(BitNo + 8, (uint8_t)(Val >> 8));
  }

  void BackpatchWord(uint64_t BitNo, unsigned Val) {
    BackpatchHalfWord(BitNo, (uint16_t)Val);
    BackpatchHalfWord(BitNo + 16, (uint16_t)(Val >> 16));
  }

  void BackpatchWord64(uint64_t BitNo, uint64_t Val) {
    BackpatchWord(BitNo, (uint32_t)Val);
    BackpatchWord(BitNo + 32, (uint32_t)(Val >> 32));
  }

  void Emit(uint32_t Val, unsigned NumBits) {
    assert(NumBits && NumBits <= 32 && "Invalid value size!");
    assert((Val & ~(~0U >> (32-NumBits))) == 0 && "High bits set!");
    CurValue |= Val << CurBit;
    if (CurBit + NumBits < 32) {
      CurBit += NumBits;
      return;
    }

    // Add the current word.
    WriteWord(CurValue);

    if (CurBit)
      CurValue = Val >> (32-CurBit);
    else
      CurValue = 0;
    CurBit = (CurBit+NumBits) & 31;
  }

  void FlushToWord() {
    if (CurBit) {
      WriteWord(CurValue);
      CurBit = 0;
      CurValue = 0;
    }
  }

  void EmitVBR(uint32_t Val, unsigned NumBits) {
    assert(NumBits <= 32 && "Too many bits to emit!");
    uint32_t Threshold = 1U << (NumBits-1);

    // Emit the bits with VBR encoding, NumBits-1 bits at a time.
    while (Val >= Threshold) {
      Emit((Val & ((1U << (NumBits - 1)) - 1)) | (1U << (NumBits - 1)),
           NumBits);
      Val >>= NumBits-1;
    }

    Emit(Val, NumBits);
  }

  void EmitVBR64(uint64_t Val, unsigned NumBits) {
    assert(NumBits <= 32 && "Too many bits to emit!");
    if ((uint32_t)Val == Val)
      return EmitVBR((uint32_t)Val, NumBits);

    uint32_t Threshold = 1U << (NumBits-1);

    // Emit the bits with VBR encoding, NumBits-1 bits at a time.
    while (Val >= Threshold) {
      Emit(((uint32_t)Val & ((1U << (NumBits - 1)) - 1)) |
               (1U << (NumBits - 1)),
           NumBits);
      Val >>= NumBits-1;
    }

    Emit((uint32_t)Val, NumBits);
  }

  /// EmitCode - Emit the specified code.
  void EmitCode(unsigned Val) {
    Emit(Val, CurCodeSize);
  }

  //===--------------------------------------------------------------------===//
  // Block Manipulation
  //===--------------------------------------------------------------------===//

  /// getBlockInfo - If there is block info for the specified ID, return it,
  /// otherwise return null.
  BlockInfo *getBlockInfo(unsigned BlockID) {
    // Common case, the most recent entry matches BlockID.
    if (!BlockInfoRecords.empty() && BlockInfoRecords.back().BlockID == BlockID)
      return &BlockInfoRecords.back();

    for (BlockInfo &BI : BlockInfoRecords)
      if (BI.BlockID == BlockID)
        return &BI;
    return nullptr;
  }

  void EnterSubblock(unsigned BlockID, unsigned CodeLen) {
    // Block header:
    //    [ENTER_SUBBLOCK, blockid, newcodelen, <align4bytes>, blocklen]
    EmitCode(bitc::ENTER_SUBBLOCK);
    EmitVBR(BlockID, bitc::BlockIDWidth);
    EmitVBR(CodeLen, bitc::CodeLenWidth);
    FlushToWord();

    size_t BlockSizeWordIndex = GetWordIndex();
    unsigned OldCodeSize = CurCodeSize;

    // Emit a placeholder, which will be replaced when the block is popped.
    Emit(0, bitc::BlockSizeWidth);

    CurCodeSize = CodeLen;

    // Push the outer block's abbrev set onto the stack, start out with an
    // empty abbrev set.
    BlockScope.emplace_back(OldCodeSize, BlockSizeWordIndex);
    BlockScope.back().PrevAbbrevs.swap(CurAbbrevs);

    // If there is a blockinfo for this BlockID, add all the predefined abbrevs
    // to the abbrev list.
    if (BlockInfo *Info = getBlockInfo(BlockID))
      append_range(CurAbbrevs, Info->Abbrevs);
  }

  void ExitBlock() {
    assert(!BlockScope.empty() && "Block scope imbalance!");
    const Block &B = BlockScope.back();

    // Block tail:
    //    [END_BLOCK, <align4bytes>]
    EmitCode(bitc::END_BLOCK);
    FlushToWord();

    // Compute the size of the block, in words, not counting the size field.
    size_t SizeInWords = GetWordIndex() - B.StartSizeWord - 1;
    uint64_t BitNo = uint64_t(B.StartSizeWord) * 32;

    // Update the block size field in the header of this sub-block.
    BackpatchWord(BitNo, SizeInWords);

    // Restore the inner block's code size and abbrev table.
    CurCodeSize = B.PrevCodeSize;
    CurAbbrevs = std::move(B.PrevAbbrevs);
    BlockScope.pop_back();
    FlushToFile();
  }

  //===--------------------------------------------------------------------===//
  // Record Emission
  //===--------------------------------------------------------------------===//

private:
  /// EmitAbbreviatedLiteral - Emit a literal value according to its abbrev
  /// record.  This is a no-op, since the abbrev specifies the literal to use.
  template<typename uintty>
  void EmitAbbreviatedLiteral(const BitCodeAbbrevOp &Op, uintty V) {
    assert(Op.isLiteral() && "Not a literal");
    // If the abbrev specifies the literal value to use, don't emit
    // anything.
    assert(V == Op.getLiteralValue() &&
           "Invalid abbrev for record!");
  }

  /// EmitAbbreviatedField - Emit a single scalar field value with the specified
  /// encoding.
  template<typename uintty>
  void EmitAbbreviatedField(const BitCodeAbbrevOp &Op, uintty V) {
    assert(!Op.isLiteral() && "Literals should use EmitAbbreviatedLiteral!");

    // Encode the value as we are commanded.
    switch (Op.getEncoding()) {
    default: llvm_unreachable("Unknown encoding!");
    case BitCodeAbbrevOp::Fixed:
      if (Op.getEncodingData())
        Emit((unsigned)V, (unsigned)Op.getEncodingData());
      break;
    case BitCodeAbbrevOp::VBR:
      if (Op.getEncodingData())
        EmitVBR64(V, (unsigned)Op.getEncodingData());
      break;
    case BitCodeAbbrevOp::Char6:
      Emit(BitCodeAbbrevOp::EncodeChar6((char)V), 6);
      break;
    }
  }

  /// EmitRecordWithAbbrevImpl - This is the core implementation of the record
  /// emission code.  If BlobData is non-null, then it specifies an array of
  /// data that should be emitted as part of the Blob or Array operand that is
  /// known to exist at the end of the record. If Code is specified, then
  /// it is the record code to emit before the Vals, which must not contain
  /// the code.
  template <typename uintty>
  void EmitRecordWithAbbrevImpl(unsigned Abbrev, ArrayRef<uintty> Vals,
                                StringRef Blob, std::optional<unsigned> Code) {
    const char *BlobData = Blob.data();
    unsigned BlobLen = (unsigned) Blob.size();
    unsigned AbbrevNo = Abbrev-bitc::FIRST_APPLICATION_ABBREV;
    assert(AbbrevNo < CurAbbrevs.size() && "Invalid abbrev #!");
    const BitCodeAbbrev *Abbv = CurAbbrevs[AbbrevNo].get();

    EmitCode(Abbrev);

    unsigned i = 0, e = static_cast<unsigned>(Abbv->getNumOperandInfos());
    if (Code) {
      assert(e && "Expected non-empty abbreviation");
      const BitCodeAbbrevOp &Op = Abbv->getOperandInfo(i++);

      if (Op.isLiteral())
        EmitAbbreviatedLiteral(Op, *Code);
      else {
        assert(Op.getEncoding() != BitCodeAbbrevOp::Array &&
               Op.getEncoding() != BitCodeAbbrevOp::Blob &&
               "Expected literal or scalar");
        EmitAbbreviatedField(Op, *Code);
      }
    }

    unsigned RecordIdx = 0;
    for (; i != e; ++i) {
      const BitCodeAbbrevOp &Op = Abbv->getOperandInfo(i);
      if (Op.isLiteral()) {
        assert(RecordIdx < Vals.size() && "Invalid abbrev/record");
        EmitAbbreviatedLiteral(Op, Vals[RecordIdx]);
        ++RecordIdx;
      } else if (Op.getEncoding() == BitCodeAbbrevOp::Array) {
        // Array case.
        assert(i + 2 == e && "array op not second to last?");
        const BitCodeAbbrevOp &EltEnc = Abbv->getOperandInfo(++i);

        // If this record has blob data, emit it, otherwise we must have record
        // entries to encode this way.
        if (BlobData) {
          assert(RecordIdx == Vals.size() &&
                 "Blob data and record entries specified for array!");
          // Emit a vbr6 to indicate the number of elements present.
          EmitVBR(static_cast<uint32_t>(BlobLen), 6);

          // Emit each field.
          for (unsigned i = 0; i != BlobLen; ++i)
            EmitAbbreviatedField(EltEnc, (unsigned char)BlobData[i]);

          // Know that blob data is consumed for assertion below.
          BlobData = nullptr;
        } else {
          // Emit a vbr6 to indicate the number of elements present.
          EmitVBR(static_cast<uint32_t>(Vals.size()-RecordIdx), 6);

          // Emit each field.
          for (unsigned e = Vals.size(); RecordIdx != e; ++RecordIdx)
            EmitAbbreviatedField(EltEnc, Vals[RecordIdx]);
        }
      } else if (Op.getEncoding() == BitCodeAbbrevOp::Blob) {
        // If this record has blob data, emit it, otherwise we must have record
        // entries to encode this way.

        if (BlobData) {
          assert(RecordIdx == Vals.size() &&
                 "Blob data and record entries specified for blob operand!");

          assert(Blob.data() == BlobData && "BlobData got moved");
          assert(Blob.size() == BlobLen && "BlobLen got changed");
          emitBlob(Blob);
          BlobData = nullptr;
        } else {
          emitBlob(Vals.slice(RecordIdx));
        }
      } else {  // Single scalar field.
        assert(RecordIdx < Vals.size() && "Invalid abbrev/record");
        EmitAbbreviatedField(Op, Vals[RecordIdx]);
        ++RecordIdx;
      }
    }
    assert(RecordIdx == Vals.size() && "Not all record operands emitted!");
    assert(BlobData == nullptr &&
           "Blob data specified for record that doesn't use it!");
  }

public:
  /// Emit a blob, including flushing before and tail-padding.
  template <class UIntTy>
  void emitBlob(ArrayRef<UIntTy> Bytes, bool ShouldEmitSize = true) {
    // Emit a vbr6 to indicate the number of elements present.
    if (ShouldEmitSize)
      EmitVBR(static_cast<uint32_t>(Bytes.size()), 6);

    // Flush to a 32-bit alignment boundary.
    FlushToWord();

    // Emit literal bytes.
    assert(llvm::all_of(Bytes, [](UIntTy B) { return isUInt<8>(B); }));
    Buffer.append(Bytes.begin(), Bytes.end());

    // Align end to 32-bits.
    while (GetBufferOffset() & 3)
      Buffer.push_back(0);
  }
  void emitBlob(StringRef Bytes, bool ShouldEmitSize = true) {
    emitBlob(ArrayRef((const uint8_t *)Bytes.data(), Bytes.size()),
             ShouldEmitSize);
  }

  /// EmitRecord - Emit the specified record to the stream, using an abbrev if
  /// we have one to compress the output.
  template <typename Container>
  void EmitRecord(unsigned Code, const Container &Vals, unsigned Abbrev = 0) {
    if (!Abbrev) {
      // If we don't have an abbrev to use, emit this in its fully unabbreviated
      // form.
      auto Count = static_cast<uint32_t>(std::size(Vals));
      EmitCode(bitc::UNABBREV_RECORD);
      EmitVBR(Code, 6);
      EmitVBR(Count, 6);
      for (unsigned i = 0, e = Count; i != e; ++i)
        EmitVBR64(Vals[i], 6);
      return;
    }

    EmitRecordWithAbbrevImpl(Abbrev, ArrayRef(Vals), StringRef(), Code);
  }

  /// EmitRecordWithAbbrev - Emit a record with the specified abbreviation.
  /// Unlike EmitRecord, the code for the record should be included in Vals as
  /// the first entry.
  template <typename Container>
  void EmitRecordWithAbbrev(unsigned Abbrev, const Container &Vals) {
    EmitRecordWithAbbrevImpl(Abbrev, ArrayRef(Vals), StringRef(), std::nullopt);
  }

  /// EmitRecordWithBlob - Emit the specified record to the stream, using an
  /// abbrev that includes a blob at the end.  The blob data to emit is
  /// specified by the pointer and length specified at the end.  In contrast to
  /// EmitRecord, this routine expects that the first entry in Vals is the code
  /// of the record.
  template <typename Container>
  void EmitRecordWithBlob(unsigned Abbrev, const Container &Vals,
                          StringRef Blob) {
    EmitRecordWithAbbrevImpl(Abbrev, ArrayRef(Vals), Blob, std::nullopt);
  }
  template <typename Container>
  void EmitRecordWithBlob(unsigned Abbrev, const Container &Vals,
                          const char *BlobData, unsigned BlobLen) {
    return EmitRecordWithAbbrevImpl(Abbrev, ArrayRef(Vals),
                                    StringRef(BlobData, BlobLen), std::nullopt);
  }

  /// EmitRecordWithArray - Just like EmitRecordWithBlob, works with records
  /// that end with an array.
  template <typename Container>
  void EmitRecordWithArray(unsigned Abbrev, const Container &Vals,
                           StringRef Array) {
    EmitRecordWithAbbrevImpl(Abbrev, ArrayRef(Vals), Array, std::nullopt);
  }
  template <typename Container>
  void EmitRecordWithArray(unsigned Abbrev, const Container &Vals,
                           const char *ArrayData, unsigned ArrayLen) {
    return EmitRecordWithAbbrevImpl(
        Abbrev, ArrayRef(Vals), StringRef(ArrayData, ArrayLen), std::nullopt);
  }

  //===--------------------------------------------------------------------===//
  // Abbrev Emission
  //===--------------------------------------------------------------------===//

private:
  // Emit the abbreviation as a DEFINE_ABBREV record.
  void EncodeAbbrev(const BitCodeAbbrev &Abbv) {
    EmitCode(bitc::DEFINE_ABBREV);
    EmitVBR(Abbv.getNumOperandInfos(), 5);
    for (unsigned i = 0, e = static_cast<unsigned>(Abbv.getNumOperandInfos());
         i != e; ++i) {
      const BitCodeAbbrevOp &Op = Abbv.getOperandInfo(i);
      Emit(Op.isLiteral(), 1);
      if (Op.isLiteral()) {
        EmitVBR64(Op.getLiteralValue(), 8);
      } else {
        Emit(Op.getEncoding(), 3);
        if (Op.hasEncodingData())
          EmitVBR64(Op.getEncodingData(), 5);
      }
    }
  }
public:

  /// Emits the abbreviation \p Abbv to the stream.
  unsigned EmitAbbrev(std::shared_ptr<BitCodeAbbrev> Abbv) {
    EncodeAbbrev(*Abbv);
    CurAbbrevs.push_back(std::move(Abbv));
    return static_cast<unsigned>(CurAbbrevs.size())-1 +
      bitc::FIRST_APPLICATION_ABBREV;
  }

  //===--------------------------------------------------------------------===//
  // BlockInfo Block Emission
  //===--------------------------------------------------------------------===//

  /// EnterBlockInfoBlock - Start emitting the BLOCKINFO_BLOCK.
  void EnterBlockInfoBlock() {
    EnterSubblock(bitc::BLOCKINFO_BLOCK_ID, 2);
    BlockInfoCurBID = ~0U;
    BlockInfoRecords.clear();
  }
private:
  /// SwitchToBlockID - If we aren't already talking about the specified block
  /// ID, emit a BLOCKINFO_CODE_SETBID record.
  void SwitchToBlockID(unsigned BlockID) {
    if (BlockInfoCurBID == BlockID) return;
    SmallVector<unsigned, 2> V;
    V.push_back(BlockID);
    EmitRecord(bitc::BLOCKINFO_CODE_SETBID, V);
    BlockInfoCurBID = BlockID;
  }

  BlockInfo &getOrCreateBlockInfo(unsigned BlockID) {
    if (BlockInfo *BI = getBlockInfo(BlockID))
      return *BI;

    // Otherwise, add a new record.
    BlockInfoRecords.emplace_back();
    BlockInfoRecords.back().BlockID = BlockID;
    return BlockInfoRecords.back();
  }

public:

  /// EmitBlockInfoAbbrev - Emit a DEFINE_ABBREV record for the specified
  /// BlockID.
  unsigned EmitBlockInfoAbbrev(unsigned BlockID, std::shared_ptr<BitCodeAbbrev> Abbv) {
    SwitchToBlockID(BlockID);
    EncodeAbbrev(*Abbv);

    // Add the abbrev to the specified block record.
    BlockInfo &Info = getOrCreateBlockInfo(BlockID);
    Info.Abbrevs.push_back(std::move(Abbv));

    return Info.Abbrevs.size()-1+bitc::FIRST_APPLICATION_ABBREV;
  }
};


} // End llvm namespace

#endif
