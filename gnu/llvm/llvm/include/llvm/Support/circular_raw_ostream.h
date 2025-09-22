//===-- llvm/Support/circular_raw_ostream.h - Buffered streams --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains raw_ostream implementations for streams to do circular
// buffering of their output.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CIRCULAR_RAW_OSTREAM_H
#define LLVM_SUPPORT_CIRCULAR_RAW_OSTREAM_H

#include "llvm/Support/raw_ostream.h"

namespace llvm {
  /// circular_raw_ostream - A raw_ostream which *can* save its data
  /// to a circular buffer, or can pass it through directly to an
  /// underlying stream if specified with a buffer of zero.
  ///
  class circular_raw_ostream : public raw_ostream {
  public:
    /// TAKE_OWNERSHIP - Tell this stream that it owns the underlying
    /// stream and is responsible for cleanup, memory management
    /// issues, etc.
    ///
    static constexpr bool TAKE_OWNERSHIP = true;

    /// REFERENCE_ONLY - Tell this stream it should not manage the
    /// held stream.
    ///
    static constexpr bool REFERENCE_ONLY = false;

  private:
    /// TheStream - The real stream we output to. We set it to be
    /// unbuffered, since we're already doing our own buffering.
    ///
    raw_ostream *TheStream = nullptr;

    /// OwnsStream - Are we responsible for managing the underlying
    /// stream?
    ///
    bool OwnsStream;

    /// BufferSize - The size of the buffer in bytes.
    ///
    size_t BufferSize;

    /// BufferArray - The actual buffer storage.
    ///
    char *BufferArray = nullptr;

    /// Cur - Pointer to the current output point in BufferArray.
    ///
    char *Cur;

    /// Filled - Indicate whether the buffer has been completely
    /// filled.  This helps avoid garbage output.
    ///
    bool Filled = false;

    /// Banner - A pointer to a banner to print before dumping the
    /// log.
    ///
    const char *Banner;

    /// flushBuffer - Dump the contents of the buffer to Stream.
    ///
    void flushBuffer() {
      if (Filled)
        // Write the older portion of the buffer.
        TheStream->write(Cur, BufferArray + BufferSize - Cur);
      // Write the newer portion of the buffer.
      TheStream->write(BufferArray, Cur - BufferArray);
      Cur = BufferArray;
      Filled = false;
    }

    void write_impl(const char *Ptr, size_t Size) override;

    /// current_pos - Return the current position within the stream,
    /// not counting the bytes currently in the buffer.
    ///
    uint64_t current_pos() const override {
      // This has the same effect as calling TheStream.current_pos(),
      // but that interface is private.
      return TheStream->tell() - TheStream->GetNumBytesInBuffer();
    }

  public:
    /// circular_raw_ostream - Construct an optionally
    /// circular-buffered stream, handing it an underlying stream to
    /// do the "real" output.
    ///
    /// As a side effect, if BuffSize is nonzero, the given Stream is
    /// set to be Unbuffered.  This is because circular_raw_ostream
    /// does its own buffering, so it doesn't want another layer of
    /// buffering to be happening underneath it.
    ///
    /// "Owns" tells the circular_raw_ostream whether it is
    /// responsible for managing the held stream, doing memory
    /// management of it, etc.
    ///
    circular_raw_ostream(raw_ostream &Stream, const char *Header,
                         size_t BuffSize = 0, bool Owns = REFERENCE_ONLY)
        : raw_ostream(/*unbuffered*/ true), OwnsStream(Owns),
          BufferSize(BuffSize), Banner(Header) {
      if (BufferSize != 0)
        BufferArray = new char[BufferSize];
      Cur = BufferArray;
      setStream(Stream, Owns);
    }

    ~circular_raw_ostream() override {
      flush();
      flushBufferWithBanner();
      releaseStream();
      delete[] BufferArray;
    }

    bool is_displayed() const override {
      return TheStream->is_displayed();
    }

    /// setStream - Tell the circular_raw_ostream to output a
    /// different stream.  "Owns" tells circular_raw_ostream whether
    /// it should take responsibility for managing the underlying
    /// stream.
    ///
    void setStream(raw_ostream &Stream, bool Owns = REFERENCE_ONLY) {
      releaseStream();
      TheStream = &Stream;
      OwnsStream = Owns;
    }

    /// flushBufferWithBanner - Force output of the buffer along with
    /// a small header.
    ///
    void flushBufferWithBanner();

  private:
    /// releaseStream - Delete the held stream if needed. Otherwise,
    /// transfer the buffer settings from this circular_raw_ostream
    /// back to the underlying stream.
    ///
    void releaseStream() {
      if (!TheStream)
        return;
      if (OwnsStream)
        delete TheStream;
    }
  };
} // end llvm namespace

#endif
