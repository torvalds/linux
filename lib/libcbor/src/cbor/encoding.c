/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "encoding.h"
#include "internal/encoders.h"

size_t cbor_encode_uint8(uint8_t value, unsigned char *buffer,
                         size_t buffer_size) {
  return _cbor_encode_uint8(value, buffer, buffer_size, 0x00);
}

size_t cbor_encode_uint16(uint16_t value, unsigned char *buffer,
                          size_t buffer_size) {
  return _cbor_encode_uint16(value, buffer, buffer_size, 0x00);
}

size_t cbor_encode_uint32(uint32_t value, unsigned char *buffer,
                          size_t buffer_size) {
  return _cbor_encode_uint32(value, buffer, buffer_size, 0x00);
}

size_t cbor_encode_uint64(uint64_t value, unsigned char *buffer,
                          size_t buffer_size) {
  return _cbor_encode_uint64(value, buffer, buffer_size, 0x00);
}

size_t cbor_encode_uint(uint64_t value, unsigned char *buffer,
                        size_t buffer_size) {
  return _cbor_encode_uint(value, buffer, buffer_size, 0x00);
}

size_t cbor_encode_negint8(uint8_t value, unsigned char *buffer,
                           size_t buffer_size) {
  return _cbor_encode_uint8(value, buffer, buffer_size, 0x20);
}

size_t cbor_encode_negint16(uint16_t value, unsigned char *buffer,
                            size_t buffer_size) {
  return _cbor_encode_uint16(value, buffer, buffer_size, 0x20);
}

size_t cbor_encode_negint32(uint32_t value, unsigned char *buffer,
                            size_t buffer_size) {
  return _cbor_encode_uint32(value, buffer, buffer_size, 0x20);
}

size_t cbor_encode_negint64(uint64_t value, unsigned char *buffer,
                            size_t buffer_size) {
  return _cbor_encode_uint64(value, buffer, buffer_size, 0x20);
}

size_t cbor_encode_negint(uint64_t value, unsigned char *buffer,
                          size_t buffer_size) {
  return _cbor_encode_uint(value, buffer, buffer_size, 0x20);
}

size_t cbor_encode_bytestring_start(size_t length, unsigned char *buffer,
                                    size_t buffer_size) {
  return _cbor_encode_uint((size_t)length, buffer, buffer_size, 0x40);
}

size_t _cbor_encode_byte(uint8_t value, unsigned char *buffer,
                         size_t buffer_size) {
  if (buffer_size >= 1) {
    buffer[0] = value;
    return 1;
  } else
    return 0;
}

size_t cbor_encode_indef_bytestring_start(unsigned char *buffer,
                                          size_t buffer_size) {
  return _cbor_encode_byte(0x5F, buffer, buffer_size);
}

size_t cbor_encode_string_start(size_t length, unsigned char *buffer,
                                size_t buffer_size) {
  return _cbor_encode_uint((size_t)length, buffer, buffer_size, 0x60);
}

size_t cbor_encode_indef_string_start(unsigned char *buffer,
                                      size_t buffer_size) {
  return _cbor_encode_byte(0x7F, buffer, buffer_size);
}

size_t cbor_encode_array_start(size_t length, unsigned char *buffer,
                               size_t buffer_size) {
  return _cbor_encode_uint((size_t)length, buffer, buffer_size, 0x80);
}

size_t cbor_encode_indef_array_start(unsigned char *buffer,
                                     size_t buffer_size) {
  return _cbor_encode_byte(0x9F, buffer, buffer_size);
}

size_t cbor_encode_map_start(size_t length, unsigned char *buffer,
                             size_t buffer_size) {
  return _cbor_encode_uint((size_t)length, buffer, buffer_size, 0xA0);
}

size_t cbor_encode_indef_map_start(unsigned char *buffer, size_t buffer_size) {
  return _cbor_encode_byte(0xBF, buffer, buffer_size);
}

size_t cbor_encode_tag(uint64_t value, unsigned char *buffer,
                       size_t buffer_size) {
  return _cbor_encode_uint(value, buffer, buffer_size, 0xC0);
}

size_t cbor_encode_bool(bool value, unsigned char *buffer, size_t buffer_size) {
  return value ? _cbor_encode_byte(0xF5, buffer, buffer_size)
               : _cbor_encode_byte(0xF4, buffer, buffer_size);
}

size_t cbor_encode_null(unsigned char *buffer, size_t buffer_size) {
  return _cbor_encode_byte(0xF6, buffer, buffer_size);
}

size_t cbor_encode_undef(unsigned char *buffer, size_t buffer_size) {
  return _cbor_encode_byte(0xF7, buffer, buffer_size);
}

size_t cbor_encode_half(float value, unsigned char *buffer,
                        size_t buffer_size) {
  /* Assuming value is normalized */
  uint32_t val = ((union _cbor_float_helper){.as_float = value}).as_uint;
  uint16_t res;
  uint8_t exp = (uint8_t)((val & 0x7F800000u) >>
                          23u); /* 0b0111_1111_1000_0000_0000_0000_0000_0000 */
  uint32_t mant =
      val & 0x7FFFFFu; /* 0b0000_0000_0111_1111_1111_1111_1111_1111 */
  if (exp == 0xFF) {   /* Infinity or NaNs */
    if (value != value) {
      // We discard information bits in half-float NaNs. This is
      // not required for the core CBOR protocol (it is only a suggestion in
      // Section 3.9).
      // See https://github.com/PJK/libcbor/issues/215
      res = (uint16_t)0x007e00;
    } else {
      // If the mantissa is non-zero, we have a NaN, but those are handled
      // above. See
      // https://en.wikipedia.org/wiki/Half-precision_floating-point_format
      CBOR_ASSERT(mant == 0u);
      res = (uint16_t)((val & 0x80000000u) >> 16u | 0x7C00u);
    }
  } else if (exp == 0x00) { /* Zeroes or subnorms */
    res = (uint16_t)((val & 0x80000000u) >> 16u | mant >> 13u);
  } else { /* Normal numbers */
    int8_t logical_exp = (int8_t)(exp - 127);
    CBOR_ASSERT(logical_exp == exp - 127);

    // Now we know that 2^exp <= 0 logically
    if (logical_exp < -24) {
      /* No unambiguous representation exists, this float is not a half float
         and is too small to be represented using a half, round off to zero.
         Consistent with the reference implementation. */
      res = 0;
    } else if (logical_exp < -14) {
      /* Offset the remaining decimal places by shifting the significand, the
         value is lost. This is an implementation decision that works around the
         absence of standard half-float in the language. */
      res = (uint16_t)((val & 0x80000000u) >> 16u) |  // Extract sign bit
            ((uint16_t)(1u << (24u + logical_exp)) +
             (uint16_t)(((mant >> (-logical_exp - 2)) + 1) >>
                        1));  // Round half away from zero for simplicity
    } else {
      res = (uint16_t)((val & 0x80000000u) >> 16u |
                       ((((uint8_t)logical_exp) + 15u) << 10u) |
                       (uint16_t)(mant >> 13u));
    }
  }
  return _cbor_encode_uint16(res, buffer, buffer_size, 0xE0);
}

size_t cbor_encode_single(float value, unsigned char *buffer,
                          size_t buffer_size) {
  return _cbor_encode_uint32(
      ((union _cbor_float_helper){.as_float = value}).as_uint, buffer,
      buffer_size, 0xE0);
}

size_t cbor_encode_double(double value, unsigned char *buffer,
                          size_t buffer_size) {
  return _cbor_encode_uint64(
      ((union _cbor_double_helper){.as_double = value}).as_uint, buffer,
      buffer_size, 0xE0);
}

size_t cbor_encode_break(unsigned char *buffer, size_t buffer_size) {
  return _cbor_encode_byte(0xFF, buffer, buffer_size);
}

size_t cbor_encode_ctrl(uint8_t value, unsigned char *buffer,
                        size_t buffer_size) {
  return _cbor_encode_uint8(value, buffer, buffer_size, 0xE0);
}
