//===-- MsgPack.h - MessagePack Constants -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains constants used for implementing MessagePack support.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MSGPACK_H
#define LLVM_BINARYFORMAT_MSGPACK_H

#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Endian.h"

namespace llvm {
namespace msgpack {

/// The endianness of all multi-byte encoded values in MessagePack.
constexpr llvm::endianness Endianness = llvm::endianness::big;

/// The first byte identifiers of MessagePack object formats.
namespace FirstByte {
#define HANDLE_MP_FIRST_BYTE(ID, NAME) constexpr uint8_t NAME = ID;
#include "llvm/BinaryFormat/MsgPack.def"
}

/// Most significant bits used to identify "Fix" variants in MessagePack.
///
/// For example, FixStr objects encode their size in the five least significant
/// bits of their first byte, which is identified by the bit pattern "101" in
/// the three most significant bits. So FixBits::String contains 0b10100000.
///
/// A corresponding mask of the bit pattern is found in \c FixBitsMask.
namespace FixBits {
#define HANDLE_MP_FIX_BITS(ID, NAME) constexpr uint8_t NAME = ID;
#include "llvm/BinaryFormat/MsgPack.def"
}

/// Mask of bits used to identify "Fix" variants in MessagePack.
///
/// For example, FixStr objects encode their size in the five least significant
/// bits of their first byte, which is identified by the bit pattern "101" in
/// the three most significant bits. So FixBitsMask::String contains
/// 0b11100000.
///
/// The corresponding bit pattern to mask for is found in FixBits.
namespace FixBitsMask {
#define HANDLE_MP_FIX_BITS_MASK(ID, NAME) constexpr uint8_t NAME = ID;
#include "llvm/BinaryFormat/MsgPack.def"
}

/// The maximum value or size encodable in "Fix" variants of formats.
///
/// For example, FixStr objects encode their size in the five least significant
/// bits of their first byte, so the largest encodable size is 0b00011111.
namespace FixMax {
#define HANDLE_MP_FIX_MAX(ID, NAME) constexpr uint8_t NAME = ID;
#include "llvm/BinaryFormat/MsgPack.def"
}

/// The exact size encodable in "Fix" variants of formats.
///
/// The only objects for which an exact size makes sense are of Extension type.
///
/// For example, FixExt4 stores an extension type containing exactly four bytes.
namespace FixLen {
#define HANDLE_MP_FIX_LEN(ID, NAME) constexpr uint8_t NAME = ID;
#include "llvm/BinaryFormat/MsgPack.def"
}

/// The minimum value or size encodable in "Fix" variants of formats.
///
/// The only object for which a minimum makes sense is a negative FixNum.
///
/// Negative FixNum objects encode their signed integer value in one byte, but
/// they must have the pattern "111" as their three most significant bits. This
/// means all values are negative, and the smallest representable value is
/// 0b11100000.
namespace FixMin {
#define HANDLE_MP_FIX_MIN(ID, NAME) constexpr int8_t NAME = ID;
#include "llvm/BinaryFormat/MsgPack.def"
}

} // end namespace msgpack
} // end namespace llvm

#endif // LLVM_BINARYFORMAT_MSGPACK_H
