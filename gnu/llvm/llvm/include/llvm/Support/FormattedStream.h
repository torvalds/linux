//===-- llvm/Support/FormattedStream.h - Formatted streams ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains raw_ostream implementations for streams to do
// things like pretty-print comments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FORMATTEDSTREAM_H
#define LLVM_SUPPORT_FORMATTEDSTREAM_H

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include <utility>

namespace llvm {

/// formatted_raw_ostream - A raw_ostream that wraps another one and keeps track
/// of line and column position, allowing padding out to specific column
/// boundaries and querying the number of lines written to the stream. This
/// assumes that the contents of the stream is valid UTF-8 encoded text. This
/// doesn't attempt to handle everything Unicode can do (combining characters,
/// right-to-left markers, etc), but should cover the cases likely to appear in
/// source code or diagnostic messages.
class formatted_raw_ostream : public raw_ostream {
  /// TheStream - The real stream we output to. We set it to be
  /// unbuffered, since we're already doing our own buffering.
  ///
  raw_ostream *TheStream;

  /// Position - The current output column and line of the data that's
  /// been flushed and the portion of the buffer that's been
  /// scanned.  The line and column scheme is zero-based.
  ///
  std::pair<unsigned, unsigned> Position;

  /// Scanned - This points to one past the last character in the
  /// buffer we've scanned.
  ///
  const char *Scanned;

  /// PartialUTF8Char - Either empty or a prefix of a UTF-8 code unit sequence
  /// for a Unicode scalar value which should be prepended to the buffer for the
  /// next call to ComputePosition. This is needed when the buffer is flushed
  /// when it ends part-way through the UTF-8 encoding of a Unicode scalar
  /// value, so that we can compute the display width of the character once we
  /// have the rest of it.
  SmallString<4> PartialUTF8Char;

  /// DisableScan - Temporarily disable scanning of output. Used to ignore color
  /// codes.
  bool DisableScan;

  void write_impl(const char *Ptr, size_t Size) override;

  /// current_pos - Return the current position within the stream,
  /// not counting the bytes currently in the buffer.
  uint64_t current_pos() const override {
    // Our current position in the stream is all the contents which have been
    // written to the underlying stream (*not* the current position of the
    // underlying stream).
    return TheStream->tell();
  }

  /// ComputePosition - Examine the given output buffer and figure out the new
  /// position after output. This is safe to call multiple times on the same
  /// buffer, as it records the most recently scanned character and resumes from
  /// there when the buffer has not been flushed.
  void ComputePosition(const char *Ptr, size_t size);

  /// UpdatePosition - scan the characters in [Ptr, Ptr+Size), and update the
  /// line and column numbers. Unlike ComputePosition, this must be called
  /// exactly once on each region of the buffer.
  void UpdatePosition(const char *Ptr, size_t Size);

  void setStream(raw_ostream &Stream) {
    releaseStream();

    TheStream = &Stream;

    // This formatted_raw_ostream inherits from raw_ostream, so it'll do its
    // own buffering, and it doesn't need or want TheStream to do another
    // layer of buffering underneath. Resize the buffer to what TheStream
    // had been using, and tell TheStream not to do its own buffering.
    if (size_t BufferSize = TheStream->GetBufferSize())
      SetBufferSize(BufferSize);
    else
      SetUnbuffered();
    TheStream->SetUnbuffered();

    enable_colors(TheStream->colors_enabled());

    Scanned = nullptr;
  }

  void PreDisableScan() {
    assert(!DisableScan);
    ComputePosition(getBufferStart(), GetNumBytesInBuffer());
    assert(PartialUTF8Char.empty());
    DisableScan = true;
  }

  void PostDisableScan() {
    assert(DisableScan);
    DisableScan = false;
    Scanned = getBufferStart() + GetNumBytesInBuffer();
  }

  struct DisableScanScope {
    formatted_raw_ostream *S;

    DisableScanScope(formatted_raw_ostream *FRO) : S(FRO) {
      S->PreDisableScan();
    }
    ~DisableScanScope() { S->PostDisableScan(); }
  };

public:
  /// formatted_raw_ostream - Open the specified file for
  /// writing. If an error occurs, information about the error is
  /// put into ErrorInfo, and the stream should be immediately
  /// destroyed; the string will be empty if no error occurred.
  ///
  /// As a side effect, the given Stream is set to be Unbuffered.
  /// This is because formatted_raw_ostream does its own buffering,
  /// so it doesn't want another layer of buffering to be happening
  /// underneath it.
  ///
  formatted_raw_ostream(raw_ostream &Stream)
      : TheStream(nullptr), Position(0, 0), DisableScan(false) {
    setStream(Stream);
  }
  explicit formatted_raw_ostream()
      : TheStream(nullptr), Position(0, 0), Scanned(nullptr),
        DisableScan(false) {}

  ~formatted_raw_ostream() override {
    flush();
    releaseStream();
  }

  /// PadToColumn - Align the output to some column number.  If the current
  /// column is already equal to or more than NewCol, PadToColumn inserts one
  /// space.
  ///
  /// \param NewCol - The column to move to.
  formatted_raw_ostream &PadToColumn(unsigned NewCol);

  unsigned getColumn() {
    // Calculate current position, taking buffer contents into account.
    ComputePosition(getBufferStart(), GetNumBytesInBuffer());
    return Position.first;
  }

  unsigned getLine() {
    // Calculate current position, taking buffer contents into account.
    ComputePosition(getBufferStart(), GetNumBytesInBuffer());
    return Position.second;
  }

  raw_ostream &resetColor() override {
    if (colors_enabled()) {
      DisableScanScope S(this);
      raw_ostream::resetColor();
    }
    return *this;
  }

  raw_ostream &reverseColor() override {
    if (colors_enabled()) {
      DisableScanScope S(this);
      raw_ostream::reverseColor();
    }
    return *this;
  }

  raw_ostream &changeColor(enum Colors Color, bool Bold, bool BG) override {
    if (colors_enabled()) {
      DisableScanScope S(this);
      raw_ostream::changeColor(Color, Bold, BG);
    }
    return *this;
  }

  bool is_displayed() const override {
    return TheStream->is_displayed();
  }

private:
  void releaseStream() {
    // Transfer the buffer settings from this raw_ostream back to the underlying
    // stream.
    if (!TheStream)
      return;
    if (size_t BufferSize = GetBufferSize())
      TheStream->SetBufferSize(BufferSize);
    else
      TheStream->SetUnbuffered();
  }
};

/// fouts() - This returns a reference to a formatted_raw_ostream for
/// standard output.  Use it like: fouts() << "foo" << "bar";
formatted_raw_ostream &fouts();

/// ferrs() - This returns a reference to a formatted_raw_ostream for
/// standard error.  Use it like: ferrs() << "foo" << "bar";
formatted_raw_ostream &ferrs();

/// fdbgs() - This returns a reference to a formatted_raw_ostream for
/// debug output.  Use it like: fdbgs() << "foo" << "bar";
formatted_raw_ostream &fdbgs();

} // end llvm namespace


#endif
