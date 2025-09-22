/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "streaming.h"
#include "internal/loaders.h"

static bool claim_bytes(size_t required, size_t provided,
                        struct cbor_decoder_result *result) {
  if (required > (provided - result->read)) {
    result->required = required + result->read;
    result->read = 0;
    result->status = CBOR_DECODER_NEDATA;
    return false;
  } else {
    result->read += required;
    result->required = 0;
    return true;
  }
}

// Use implicit capture as an exception to avoid the super long parameter list
#define CLAIM_BYTES_AND_INVOKE(callback_name, length, source_extra_offset) \
  do {                                                                     \
    if (claim_bytes(length, source_size, &result)) {                       \
      callbacks->callback_name(context, source + 1 + source_extra_offset,  \
                               length);                                    \
    }                                                                      \
  } while (0)

#define READ_CLAIM_INVOKE(callback_name, length_reader, length_bytes) \
  do {                                                                \
    if (claim_bytes(length_bytes, source_size, &result)) {            \
      uint64_t length = length_reader(source + 1);                    \
      CLAIM_BYTES_AND_INVOKE(callback_name, length, length_bytes);    \
    }                                                                 \
    return result;                                                    \
  } while (0)

struct cbor_decoder_result cbor_stream_decode(
    cbor_data source, size_t source_size,
    const struct cbor_callbacks *callbacks, void *context) {
  // Attempt to claim the initial MTB byte
  struct cbor_decoder_result result = {.status = CBOR_DECODER_FINISHED};
  if (!claim_bytes(1, source_size, &result)) {
    return result;
  }

  switch (*source) {
    case 0x00: /* Fallthrough */
    case 0x01: /* Fallthrough */
    case 0x02: /* Fallthrough */
    case 0x03: /* Fallthrough */
    case 0x04: /* Fallthrough */
    case 0x05: /* Fallthrough */
    case 0x06: /* Fallthrough */
    case 0x07: /* Fallthrough */
    case 0x08: /* Fallthrough */
    case 0x09: /* Fallthrough */
    case 0x0A: /* Fallthrough */
    case 0x0B: /* Fallthrough */
    case 0x0C: /* Fallthrough */
    case 0x0D: /* Fallthrough */
    case 0x0E: /* Fallthrough */
    case 0x0F: /* Fallthrough */
    case 0x10: /* Fallthrough */
    case 0x11: /* Fallthrough */
    case 0x12: /* Fallthrough */
    case 0x13: /* Fallthrough */
    case 0x14: /* Fallthrough */
    case 0x15: /* Fallthrough */
    case 0x16: /* Fallthrough */
    case 0x17:
      /* Embedded one byte unsigned integer */
      {
        callbacks->uint8(context, _cbor_load_uint8(source));
        return result;
      }
    case 0x18:
      /* One byte unsigned integer */
      {
        if (claim_bytes(1, source_size, &result)) {
          callbacks->uint8(context, _cbor_load_uint8(source + 1));
        }
        return result;
      }
    case 0x19:
      /* Two bytes unsigned integer */
      {
        if (claim_bytes(2, source_size, &result)) {
          callbacks->uint16(context, _cbor_load_uint16(source + 1));
        }
        return result;
      }
    case 0x1A:
      /* Four bytes unsigned integer */
      {
        if (claim_bytes(4, source_size, &result)) {
          callbacks->uint32(context, _cbor_load_uint32(source + 1));
        }
        return result;
      }
    case 0x1B:
      /* Eight bytes unsigned integer */
      {
        if (claim_bytes(8, source_size, &result)) {
          callbacks->uint64(context, _cbor_load_uint64(source + 1));
        }
        return result;
      }
    case 0x1C: /* Fallthrough */
    case 0x1D: /* Fallthrough */
    case 0x1E: /* Fallthrough */
    case 0x1F:
      /* Reserved */
      { return (struct cbor_decoder_result){.status = CBOR_DECODER_ERROR}; }
    case 0x20: /* Fallthrough */
    case 0x21: /* Fallthrough */
    case 0x22: /* Fallthrough */
    case 0x23: /* Fallthrough */
    case 0x24: /* Fallthrough */
    case 0x25: /* Fallthrough */
    case 0x26: /* Fallthrough */
    case 0x27: /* Fallthrough */
    case 0x28: /* Fallthrough */
    case 0x29: /* Fallthrough */
    case 0x2A: /* Fallthrough */
    case 0x2B: /* Fallthrough */
    case 0x2C: /* Fallthrough */
    case 0x2D: /* Fallthrough */
    case 0x2E: /* Fallthrough */
    case 0x2F: /* Fallthrough */
    case 0x30: /* Fallthrough */
    case 0x31: /* Fallthrough */
    case 0x32: /* Fallthrough */
    case 0x33: /* Fallthrough */
    case 0x34: /* Fallthrough */
    case 0x35: /* Fallthrough */
    case 0x36: /* Fallthrough */
    case 0x37:
      /* Embedded one byte negative integer */
      {
        callbacks->negint8(context,
                           _cbor_load_uint8(source) - 0x20); /* 0x20 offset */
        return result;
      }
    case 0x38:
      /* One byte negative integer */
      {
        if (claim_bytes(1, source_size, &result)) {
          callbacks->negint8(context, _cbor_load_uint8(source + 1));
        }
        return result;
      }
    case 0x39:
      /* Two bytes negative integer */
      {
        if (claim_bytes(2, source_size, &result)) {
          callbacks->negint16(context, _cbor_load_uint16(source + 1));
        }
        return result;
      }
    case 0x3A:
      /* Four bytes negative integer */
      {
        if (claim_bytes(4, source_size, &result)) {
          callbacks->negint32(context, _cbor_load_uint32(source + 1));
        }
        return result;
      }
    case 0x3B:
      /* Eight bytes negative integer */
      {
        if (claim_bytes(8, source_size, &result)) {
          callbacks->negint64(context, _cbor_load_uint64(source + 1));
        }
        return result;
      }
    case 0x3C: /* Fallthrough */
    case 0x3D: /* Fallthrough */
    case 0x3E: /* Fallthrough */
    case 0x3F:
      /* Reserved */
      { return (struct cbor_decoder_result){.status = CBOR_DECODER_ERROR}; }
    case 0x40: /* Fallthrough */
    case 0x41: /* Fallthrough */
    case 0x42: /* Fallthrough */
    case 0x43: /* Fallthrough */
    case 0x44: /* Fallthrough */
    case 0x45: /* Fallthrough */
    case 0x46: /* Fallthrough */
    case 0x47: /* Fallthrough */
    case 0x48: /* Fallthrough */
    case 0x49: /* Fallthrough */
    case 0x4A: /* Fallthrough */
    case 0x4B: /* Fallthrough */
    case 0x4C: /* Fallthrough */
    case 0x4D: /* Fallthrough */
    case 0x4E: /* Fallthrough */
    case 0x4F: /* Fallthrough */
    case 0x50: /* Fallthrough */
    case 0x51: /* Fallthrough */
    case 0x52: /* Fallthrough */
    case 0x53: /* Fallthrough */
    case 0x54: /* Fallthrough */
    case 0x55: /* Fallthrough */
    case 0x56: /* Fallthrough */
    case 0x57:
      /* Embedded length byte string */
      {
        uint64_t length = _cbor_load_uint8(source) - 0x40; /* 0x40 offset */
        CLAIM_BYTES_AND_INVOKE(byte_string, length, 0);
        return result;
      }
    case 0x58:
      /* One byte length byte string */
      READ_CLAIM_INVOKE(byte_string, _cbor_load_uint8, 1);
    case 0x59:
      /* Two bytes length byte string */
      READ_CLAIM_INVOKE(byte_string, _cbor_load_uint16, 2);
    case 0x5A:
      /* Four bytes length byte string */
      READ_CLAIM_INVOKE(byte_string, _cbor_load_uint32, 4);
    case 0x5B:
      /* Eight bytes length byte string */
      READ_CLAIM_INVOKE(byte_string, _cbor_load_uint64, 8);
    case 0x5C: /* Fallthrough */
    case 0x5D: /* Fallthrough */
    case 0x5E:
      /* Reserved */
      { return (struct cbor_decoder_result){.status = CBOR_DECODER_ERROR}; }
    case 0x5F:
      /* Indefinite byte string */
      {
        callbacks->byte_string_start(context);
        return result;
      }
    case 0x60: /* Fallthrough */
    case 0x61: /* Fallthrough */
    case 0x62: /* Fallthrough */
    case 0x63: /* Fallthrough */
    case 0x64: /* Fallthrough */
    case 0x65: /* Fallthrough */
    case 0x66: /* Fallthrough */
    case 0x67: /* Fallthrough */
    case 0x68: /* Fallthrough */
    case 0x69: /* Fallthrough */
    case 0x6A: /* Fallthrough */
    case 0x6B: /* Fallthrough */
    case 0x6C: /* Fallthrough */
    case 0x6D: /* Fallthrough */
    case 0x6E: /* Fallthrough */
    case 0x6F: /* Fallthrough */
    case 0x70: /* Fallthrough */
    case 0x71: /* Fallthrough */
    case 0x72: /* Fallthrough */
    case 0x73: /* Fallthrough */
    case 0x74: /* Fallthrough */
    case 0x75: /* Fallthrough */
    case 0x76: /* Fallthrough */
    case 0x77:
      /* Embedded one byte length string */
      {
        uint64_t length = _cbor_load_uint8(source) - 0x60; /* 0x60 offset */
        CLAIM_BYTES_AND_INVOKE(string, length, 0);
        return result;
      }
    case 0x78:
      /* One byte length string */
      READ_CLAIM_INVOKE(string, _cbor_load_uint8, 1);
    case 0x79:
      /* Two bytes length string */
      READ_CLAIM_INVOKE(string, _cbor_load_uint16, 2);
    case 0x7A:
      /* Four bytes length string */
      READ_CLAIM_INVOKE(string, _cbor_load_uint32, 4);
    case 0x7B:
      /* Eight bytes length string */
      READ_CLAIM_INVOKE(string, _cbor_load_uint64, 8);
    case 0x7C: /* Fallthrough */
    case 0x7D: /* Fallthrough */
    case 0x7E:
      /* Reserved */
      { return (struct cbor_decoder_result){.status = CBOR_DECODER_ERROR}; }
    case 0x7F:
      /* Indefinite length string */
      {
        callbacks->string_start(context);
        return result;
      }
    case 0x80: /* Fallthrough */
    case 0x81: /* Fallthrough */
    case 0x82: /* Fallthrough */
    case 0x83: /* Fallthrough */
    case 0x84: /* Fallthrough */
    case 0x85: /* Fallthrough */
    case 0x86: /* Fallthrough */
    case 0x87: /* Fallthrough */
    case 0x88: /* Fallthrough */
    case 0x89: /* Fallthrough */
    case 0x8A: /* Fallthrough */
    case 0x8B: /* Fallthrough */
    case 0x8C: /* Fallthrough */
    case 0x8D: /* Fallthrough */
    case 0x8E: /* Fallthrough */
    case 0x8F: /* Fallthrough */
    case 0x90: /* Fallthrough */
    case 0x91: /* Fallthrough */
    case 0x92: /* Fallthrough */
    case 0x93: /* Fallthrough */
    case 0x94: /* Fallthrough */
    case 0x95: /* Fallthrough */
    case 0x96: /* Fallthrough */
    case 0x97:
      /* Embedded one byte length array */
      {
        callbacks->array_start(
            context, _cbor_load_uint8(source) - 0x80); /* 0x40 offset */
        return result;
      }
    case 0x98:
      /* One byte length array */
      {
        if (claim_bytes(1, source_size, &result)) {
          callbacks->array_start(context, _cbor_load_uint8(source + 1));
        }
        return result;
      }
    case 0x99:
      /* Two bytes length array */
      {
        if (claim_bytes(2, source_size, &result)) {
          callbacks->array_start(context, _cbor_load_uint16(source + 1));
        }
        return result;
      }
    case 0x9A:
      /* Four bytes length array */
      {
        if (claim_bytes(4, source_size, &result)) {
          callbacks->array_start(context, _cbor_load_uint32(source + 1));
        }
        return result;
      }
    case 0x9B:
      /* Eight bytes length array */
      {
        if (claim_bytes(8, source_size, &result)) {
          callbacks->array_start(context, _cbor_load_uint64(source + 1));
        }
        return result;
      }
    case 0x9C: /* Fallthrough */
    case 0x9D: /* Fallthrough */
    case 0x9E:
      /* Reserved */
      { return (struct cbor_decoder_result){.status = CBOR_DECODER_ERROR}; }
    case 0x9F:
      /* Indefinite length array */
      {
        callbacks->indef_array_start(context);
        return result;
      }
    case 0xA0: /* Fallthrough */
    case 0xA1: /* Fallthrough */
    case 0xA2: /* Fallthrough */
    case 0xA3: /* Fallthrough */
    case 0xA4: /* Fallthrough */
    case 0xA5: /* Fallthrough */
    case 0xA6: /* Fallthrough */
    case 0xA7: /* Fallthrough */
    case 0xA8: /* Fallthrough */
    case 0xA9: /* Fallthrough */
    case 0xAA: /* Fallthrough */
    case 0xAB: /* Fallthrough */
    case 0xAC: /* Fallthrough */
    case 0xAD: /* Fallthrough */
    case 0xAE: /* Fallthrough */
    case 0xAF: /* Fallthrough */
    case 0xB0: /* Fallthrough */
    case 0xB1: /* Fallthrough */
    case 0xB2: /* Fallthrough */
    case 0xB3: /* Fallthrough */
    case 0xB4: /* Fallthrough */
    case 0xB5: /* Fallthrough */
    case 0xB6: /* Fallthrough */
    case 0xB7:
      /* Embedded one byte length map */
      {
        callbacks->map_start(context,
                             _cbor_load_uint8(source) - 0xA0); /* 0xA0 offset */
        return result;
      }
    case 0xB8:
      /* One byte length map */
      {
        if (claim_bytes(1, source_size, &result)) {
          callbacks->map_start(context, _cbor_load_uint8(source + 1));
        }
        return result;
      }
    case 0xB9:
      /* Two bytes length map */
      {
        if (claim_bytes(2, source_size, &result)) {
          callbacks->map_start(context, _cbor_load_uint16(source + 1));
        }
        return result;
      }
    case 0xBA:
      /* Four bytes length map */
      {
        if (claim_bytes(4, source_size, &result)) {
          callbacks->map_start(context, _cbor_load_uint32(source + 1));
        }
        return result;
      }
    case 0xBB:
      /* Eight bytes length map */
      {
        if (claim_bytes(8, source_size, &result)) {
          callbacks->map_start(context, _cbor_load_uint64(source + 1));
        }
        return result;
      }
    case 0xBC: /* Fallthrough */
    case 0xBD: /* Fallthrough */
    case 0xBE:
      /* Reserved */
      { return (struct cbor_decoder_result){.status = CBOR_DECODER_ERROR}; }
    case 0xBF:
      /* Indefinite length map */
      {
        callbacks->indef_map_start(context);
        return result;
      }
    case 0xC0:
      /* Text date/time - RFC 3339 tag, fallthrough */
    case 0xC1:
      /* Epoch date tag, fallthrough */
    case 0xC2:
      /* Positive bignum tag, fallthrough */
    case 0xC3:
      /* Negative bignum tag, fallthrough */
    case 0xC4:
      /* Fraction, fallthrough */
    case 0xC5:
      /* Big float */
      {
        callbacks->tag(context, (uint64_t)(_cbor_load_uint8(source) -
                                           0xC0)); /* 0xC0 offset */
        return result;
      }
    case 0xC6: /* Fallthrough */
    case 0xC7: /* Fallthrough */
    case 0xC8: /* Fallthrough */
    case 0xC9: /* Fallthrough */
    case 0xCA: /* Fallthrough */
    case 0xCB: /* Fallthrough */
    case 0xCC: /* Fallthrough */
    case 0xCD: /* Fallthrough */
    case 0xCE: /* Fallthrough */
    case 0xCF: /* Fallthrough */
    case 0xD0: /* Fallthrough */
    case 0xD1: /* Fallthrough */
    case 0xD2: /* Fallthrough */
    case 0xD3: /* Fallthrough */
    case 0xD4: /* Unassigned tag value */
    {
      return (struct cbor_decoder_result){.status = CBOR_DECODER_ERROR};
    }
    case 0xD5: /* Expected b64url conversion tag - fallthrough */
    case 0xD6: /* Expected b64 conversion tag - fallthrough */
    case 0xD7: /* Expected b16 conversion tag */
    {
      callbacks->tag(context, (uint64_t)(_cbor_load_uint8(source) -
                                         0xC0)); /* 0xC0 offset */
      return result;
    }
    case 0xD8: /* 1B tag */
    {
      if (claim_bytes(1, source_size, &result)) {
        callbacks->tag(context, _cbor_load_uint8(source + 1));
      }
      return result;
    }
    case 0xD9: /* 2B tag */
    {
      if (claim_bytes(2, source_size, &result)) {
        callbacks->tag(context, _cbor_load_uint16(source + 1));
      }
      return result;
    }
    case 0xDA: /* 4B tag */
    {
      if (claim_bytes(4, source_size, &result)) {
        callbacks->tag(context, _cbor_load_uint32(source + 1));
      }
      return result;
    }
    case 0xDB: /* 8B tag */
    {
      if (claim_bytes(8, source_size, &result)) {
        callbacks->tag(context, _cbor_load_uint64(source + 1));
      }
      return result;
    }
    case 0xDC: /* Fallthrough */
    case 0xDD: /* Fallthrough */
    case 0xDE: /* Fallthrough */
    case 0xDF: /* Reserved */
    {
      return (struct cbor_decoder_result){.status = CBOR_DECODER_ERROR};
    }
    case 0xE0: /* Fallthrough */
    case 0xE1: /* Fallthrough */
    case 0xE2: /* Fallthrough */
    case 0xE3: /* Fallthrough */
    case 0xE4: /* Fallthrough */
    case 0xE5: /* Fallthrough */
    case 0xE6: /* Fallthrough */
    case 0xE7: /* Fallthrough */
    case 0xE8: /* Fallthrough */
    case 0xE9: /* Fallthrough */
    case 0xEA: /* Fallthrough */
    case 0xEB: /* Fallthrough */
    case 0xEC: /* Fallthrough */
    case 0xED: /* Fallthrough */
    case 0xEE: /* Fallthrough */
    case 0xEF: /* Fallthrough */
    case 0xF0: /* Fallthrough */
    case 0xF1: /* Fallthrough */
    case 0xF2: /* Fallthrough */
    case 0xF3: /* Simple value - unassigned */
    {
      return (struct cbor_decoder_result){.status = CBOR_DECODER_ERROR};
    }
    case 0xF4:
      /* False */
      {
        callbacks->boolean(context, false);
        return result;
      }
    case 0xF5:
      /* True */
      {
        callbacks->boolean(context, true);
        return result;
      }
    case 0xF6:
      /* Null */
      {
        callbacks->null(context);
        return result;
      }
    case 0xF7:
      /* Undefined */
      {
        callbacks->undefined(context);
        return result;
      }
    case 0xF8:
      /* 1B simple value, unassigned */
      { return (struct cbor_decoder_result){.status = CBOR_DECODER_ERROR}; }
    case 0xF9:
      /* 2B float */
      {
        if (claim_bytes(2, source_size, &result)) {
          callbacks->float2(context, _cbor_load_half(source + 1));
        }
        return result;
      }
    case 0xFA:
      /* 4B float */
      {
        if (claim_bytes(4, source_size, &result)) {
          callbacks->float4(context, _cbor_load_float(source + 1));
        }
        return result;
      }
    case 0xFB:
      /* 8B float */
      {
        if (claim_bytes(8, source_size, &result)) {
          callbacks->float8(context, _cbor_load_double(source + 1));
        }
        return result;
      }
    case 0xFC: /* Fallthrough */
    case 0xFD: /* Fallthrough */
    case 0xFE:
      /* Reserved */
      { return (struct cbor_decoder_result){.status = CBOR_DECODER_ERROR}; }
    case 0xFF:
      /* Break */
      callbacks->indef_break(context);
      // Never happens, the switch statement is exhaustive on the 1B range; make
      // compiler happy
    default:
      return result;
  }
}
