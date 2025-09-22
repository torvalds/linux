//===- MsgPackWriter.cpp - Simple MsgPack writer ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
///  \file
///  This file implements a MessagePack writer.
///
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MsgPackWriter.h"
#include "llvm/BinaryFormat/MsgPack.h"

#include <cmath>

using namespace llvm;
using namespace msgpack;

Writer::Writer(raw_ostream &OS, bool Compatible)
    : EW(OS, Endianness), Compatible(Compatible) {}

void Writer::writeNil() { EW.write(FirstByte::Nil); }

void Writer::write(bool b) { EW.write(b ? FirstByte::True : FirstByte::False); }

void Writer::write(int64_t i) {
  if (i >= 0) {
    write(static_cast<uint64_t>(i));
    return;
  }

  if (i >= FixMin::NegativeInt) {
    EW.write(static_cast<int8_t>(i));
    return;
  }

  if (i >= INT8_MIN) {
    EW.write(FirstByte::Int8);
    EW.write(static_cast<int8_t>(i));
    return;
  }

  if (i >= INT16_MIN) {
    EW.write(FirstByte::Int16);
    EW.write(static_cast<int16_t>(i));
    return;
  }

  if (i >= INT32_MIN) {
    EW.write(FirstByte::Int32);
    EW.write(static_cast<int32_t>(i));
    return;
  }

  EW.write(FirstByte::Int64);
  EW.write(i);
}

void Writer::write(uint64_t u) {
  if (u <= FixMax::PositiveInt) {
    EW.write(static_cast<uint8_t>(u));
    return;
  }

  if (u <= UINT8_MAX) {
    EW.write(FirstByte::UInt8);
    EW.write(static_cast<uint8_t>(u));
    return;
  }

  if (u <= UINT16_MAX) {
    EW.write(FirstByte::UInt16);
    EW.write(static_cast<uint16_t>(u));
    return;
  }

  if (u <= UINT32_MAX) {
    EW.write(FirstByte::UInt32);
    EW.write(static_cast<uint32_t>(u));
    return;
  }

  EW.write(FirstByte::UInt64);
  EW.write(u);
}

void Writer::write(double d) {
  // If no loss of precision, encode as a Float32.
  double a = std::fabs(d);
  if (a >= std::numeric_limits<float>::min() &&
      a <= std::numeric_limits<float>::max()) {
    EW.write(FirstByte::Float32);
    EW.write(static_cast<float>(d));
  } else {
    EW.write(FirstByte::Float64);
    EW.write(d);
  }
}

void Writer::write(StringRef s) {
  size_t Size = s.size();

  if (Size <= FixMax::String)
    EW.write(static_cast<uint8_t>(FixBits::String | Size));
  else if (!Compatible && Size <= UINT8_MAX) {
    EW.write(FirstByte::Str8);
    EW.write(static_cast<uint8_t>(Size));
  } else if (Size <= UINT16_MAX) {
    EW.write(FirstByte::Str16);
    EW.write(static_cast<uint16_t>(Size));
  } else {
    assert(Size <= UINT32_MAX && "String object too long to be encoded");
    EW.write(FirstByte::Str32);
    EW.write(static_cast<uint32_t>(Size));
  }

  EW.OS << s;
}

void Writer::write(MemoryBufferRef Buffer) {
  assert(!Compatible && "Attempt to write Bin format in compatible mode");

  size_t Size = Buffer.getBufferSize();

  if (Size <= UINT8_MAX) {
    EW.write(FirstByte::Bin8);
    EW.write(static_cast<uint8_t>(Size));
  } else if (Size <= UINT16_MAX) {
    EW.write(FirstByte::Bin16);
    EW.write(static_cast<uint16_t>(Size));
  } else {
    assert(Size <= UINT32_MAX && "Binary object too long to be encoded");
    EW.write(FirstByte::Bin32);
    EW.write(static_cast<uint32_t>(Size));
  }

  EW.OS.write(Buffer.getBufferStart(), Size);
}

void Writer::writeArraySize(uint32_t Size) {
  if (Size <= FixMax::Array) {
    EW.write(static_cast<uint8_t>(FixBits::Array | Size));
    return;
  }

  if (Size <= UINT16_MAX) {
    EW.write(FirstByte::Array16);
    EW.write(static_cast<uint16_t>(Size));
    return;
  }

  EW.write(FirstByte::Array32);
  EW.write(Size);
}

void Writer::writeMapSize(uint32_t Size) {
  if (Size <= FixMax::Map) {
    EW.write(static_cast<uint8_t>(FixBits::Map | Size));
    return;
  }

  if (Size <= UINT16_MAX) {
    EW.write(FirstByte::Map16);
    EW.write(static_cast<uint16_t>(Size));
    return;
  }

  EW.write(FirstByte::Map32);
  EW.write(Size);
}

void Writer::writeExt(int8_t Type, MemoryBufferRef Buffer) {
  size_t Size = Buffer.getBufferSize();

  switch (Size) {
  case FixLen::Ext1:
    EW.write(FirstByte::FixExt1);
    break;
  case FixLen::Ext2:
    EW.write(FirstByte::FixExt2);
    break;
  case FixLen::Ext4:
    EW.write(FirstByte::FixExt4);
    break;
  case FixLen::Ext8:
    EW.write(FirstByte::FixExt8);
    break;
  case FixLen::Ext16:
    EW.write(FirstByte::FixExt16);
    break;
  default:
    if (Size <= UINT8_MAX) {
      EW.write(FirstByte::Ext8);
      EW.write(static_cast<uint8_t>(Size));
    } else if (Size <= UINT16_MAX) {
      EW.write(FirstByte::Ext16);
      EW.write(static_cast<uint16_t>(Size));
    } else {
      assert(Size <= UINT32_MAX && "Ext size too large to be encoded");
      EW.write(FirstByte::Ext32);
      EW.write(static_cast<uint32_t>(Size));
    }
  }

  EW.write(Type);
  EW.OS.write(Buffer.getBufferStart(), Size);
}
