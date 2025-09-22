//===-- stack_trace_compressor.cpp ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/stack_trace_compressor.h"

namespace gwp_asan {
namespace compression {
namespace {
// Encodes `Value` as a variable-length integer to `Out`. Returns zero if there
// was not enough space in the output buffer to write the complete varInt.
// Otherwise returns the length of the encoded integer.
size_t varIntEncode(uintptr_t Value, uint8_t *Out, size_t OutLen) {
  for (size_t i = 0; i < OutLen; ++i) {
    Out[i] = Value & 0x7f;
    Value >>= 7;
    if (!Value)
      return i + 1;

    Out[i] |= 0x80;
  }

  return 0;
}

// Decodes a variable-length integer to `Out`. Returns zero if the integer was
// too large to be represented in a uintptr_t, or if the input buffer finished
// before the integer was decoded (either case meaning that the `In` does not
// point to a valid varInt buffer). Otherwise, returns the number of bytes that
// were used to store the decoded integer.
size_t varIntDecode(const uint8_t *In, size_t InLen, uintptr_t *Out) {
  *Out = 0;
  uint8_t Shift = 0;

  for (size_t i = 0; i < InLen; ++i) {
    *Out |= (static_cast<uintptr_t>(In[i]) & 0x7f) << Shift;

    if (In[i] < 0x80)
      return i + 1;

    Shift += 7;

    // Disallow overflowing the range of the output integer.
    if (Shift >= sizeof(uintptr_t) * 8)
      return 0;
  }
  return 0;
}

uintptr_t zigzagEncode(uintptr_t Value) {
  uintptr_t Encoded = Value << 1;
  if (static_cast<intptr_t>(Value) >= 0)
    return Encoded;
  return ~Encoded;
}

uintptr_t zigzagDecode(uintptr_t Value) {
  uintptr_t Decoded = Value >> 1;
  if (!(Value & 1))
    return Decoded;
  return ~Decoded;
}
} // anonymous namespace

size_t pack(const uintptr_t *Unpacked, size_t UnpackedSize, uint8_t *Packed,
            size_t PackedMaxSize) {
  size_t Index = 0;
  for (size_t CurrentDepth = 0; CurrentDepth < UnpackedSize; CurrentDepth++) {
    uintptr_t Diff = Unpacked[CurrentDepth];
    if (CurrentDepth > 0)
      Diff -= Unpacked[CurrentDepth - 1];
    size_t EncodedLength =
        varIntEncode(zigzagEncode(Diff), Packed + Index, PackedMaxSize - Index);
    if (!EncodedLength)
      break;

    Index += EncodedLength;
  }

  return Index;
}

size_t unpack(const uint8_t *Packed, size_t PackedSize, uintptr_t *Unpacked,
              size_t UnpackedMaxSize) {
  size_t CurrentDepth;
  size_t Index = 0;
  for (CurrentDepth = 0; CurrentDepth < UnpackedMaxSize; CurrentDepth++) {
    uintptr_t EncodedDiff;
    size_t DecodedLength =
        varIntDecode(Packed + Index, PackedSize - Index, &EncodedDiff);
    if (!DecodedLength)
      break;
    Index += DecodedLength;

    Unpacked[CurrentDepth] = zigzagDecode(EncodedDiff);
    if (CurrentDepth > 0)
      Unpacked[CurrentDepth] += Unpacked[CurrentDepth - 1];
  }

  if (Index != PackedSize && CurrentDepth != UnpackedMaxSize)
    return 0;

  return CurrentDepth;
}

} // namespace compression
} // namespace gwp_asan
