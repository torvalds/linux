/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#pragma once

#include "utils/Range.h"

#include <array>
#include <cstddef>
#include <memory>

namespace pzstd {

/**
 * A `Buffer` has a pointer to a shared buffer, and a range of the buffer that
 * it owns.
 * The idea is that you can allocate one buffer, and write chunks into it
 * and break off those chunks.
 * The underlying buffer is reference counted, and will be destroyed when all
 * `Buffer`s that reference it are destroyed.
 */
class Buffer {
  std::shared_ptr<unsigned char> buffer_;
  MutableByteRange range_;

  static void delete_buffer(unsigned char* buffer) {
    delete[] buffer;
  }

 public:
  /// Construct an empty buffer that owns no data.
  explicit Buffer() {}

  /// Construct a `Buffer` that owns a new underlying buffer of size `size`.
  explicit Buffer(std::size_t size)
      : buffer_(new unsigned char[size], delete_buffer),
        range_(buffer_.get(), buffer_.get() + size) {}

  explicit Buffer(std::shared_ptr<unsigned char> buffer, MutableByteRange data)
      : buffer_(buffer), range_(data) {}

  Buffer(Buffer&&) = default;
  Buffer& operator=(Buffer&&) & = default;

  /**
   * Splits the data into two pieces: [begin, begin + n), [begin + n, end).
   * Their data both points into the same underlying buffer.
   * Modifies the original `Buffer` to point to only [begin + n, end).
   *
   * @param n  The offset to split at.
   * @returns  A buffer that owns the data [begin, begin + n).
   */
  Buffer splitAt(std::size_t n) {
    auto firstPiece = range_.subpiece(0, n);
    range_.advance(n);
    return Buffer(buffer_, firstPiece);
  }

  /// Modifies the buffer to point to the range [begin + n, end).
  void advance(std::size_t n) {
    range_.advance(n);
  }

  /// Modifies the buffer to point to the range [begin, end - n).
  void subtract(std::size_t n) {
    range_.subtract(n);
  }

  /// Returns a read only `Range` pointing to the `Buffer`s data.
  ByteRange range() const {
    return range_;
  }
  /// Returns a mutable `Range` pointing to the `Buffer`s data.
  MutableByteRange range() {
    return range_;
  }

  const unsigned char* data() const {
    return range_.data();
  }

  unsigned char* data() {
    return range_.data();
  }

  std::size_t size() const {
    return range_.size();
  }

  bool empty() const {
    return range_.empty();
  }
};
}
