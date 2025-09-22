//===-- DNBRegisterInfo.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 8/3/07.
//
//===----------------------------------------------------------------------===//

#include "DNBRegisterInfo.h"
#include "DNBLog.h"
#include <cstring>

DNBRegisterValueClass::DNBRegisterValueClass(const DNBRegisterInfo *regInfo) {
  Clear();
  if (regInfo)
    info = *regInfo;
}

void DNBRegisterValueClass::Clear() {
  memset(&info, 0, sizeof(DNBRegisterInfo));
  memset(&value, 0, sizeof(value));
}

bool DNBRegisterValueClass::IsValid() const {
  return info.name != NULL && info.type != InvalidRegType && info.size > 0 &&
         info.size <= sizeof(value);
}

#define PRINT_COMMA_SEPARATOR                                                  \
  do {                                                                         \
    if (pos < end) {                                                           \
      if (i > 0) {                                                             \
        strlcpy(pos, ", ", end - pos);                                         \
        pos += 2;                                                              \
      }                                                                        \
    }                                                                          \
  } while (0)

void DNBRegisterValueClass::Dump(const char *pre, const char *post) const {
  if (info.name != NULL) {
    char str[1024];
    char *pos;
    char *end = str + sizeof(str);
    if (info.format == Hex) {
      switch (info.size) {
      case 0:
        snprintf(str, sizeof(str), "%s",
                 "error: invalid register size of zero.");
        break;
      case 1:
        snprintf(str, sizeof(str), "0x%2.2x", value.uint8);
        break;
      case 2:
        snprintf(str, sizeof(str), "0x%4.4x", value.uint16);
        break;
      case 4:
        snprintf(str, sizeof(str), "0x%8.8x", value.uint32);
        break;
      case 8:
        snprintf(str, sizeof(str), "0x%16.16llx", value.uint64);
        break;
      case 16:
        snprintf(str, sizeof(str), "0x%16.16llx%16.16llx", value.v_uint64[0],
                 value.v_uint64[1]);
        break;
      default:
        strlcpy(str, "0x", 3);
        pos = str + 2;
        for (uint32_t i = 0; i < info.size; ++i) {
          if (pos < end)
            pos +=
                snprintf(pos, end - pos, "%2.2x", (uint32_t)value.v_uint8[i]);
        }
        break;
      }
    } else {
      switch (info.type) {
      case Uint:
        switch (info.size) {
        case 1:
          snprintf(str, sizeof(str), "%u", value.uint8);
          break;
        case 2:
          snprintf(str, sizeof(str), "%u", value.uint16);
          break;
        case 4:
          snprintf(str, sizeof(str), "%u", value.uint32);
          break;
        case 8:
          snprintf(str, sizeof(str), "%llu", value.uint64);
          break;
        default:
          snprintf(str, sizeof(str), "error: unsupported uint byte size %d.",
                   info.size);
          break;
        }
        break;

      case Sint:
        switch (info.size) {
        case 1:
          snprintf(str, sizeof(str), "%d", value.sint8);
          break;
        case 2:
          snprintf(str, sizeof(str), "%d", value.sint16);
          break;
        case 4:
          snprintf(str, sizeof(str), "%d", value.sint32);
          break;
        case 8:
          snprintf(str, sizeof(str), "%lld", value.sint64);
          break;
        default:
          snprintf(str, sizeof(str), "error: unsupported sint byte size %d.",
                   info.size);
          break;
        }
        break;

      case IEEE754:
        switch (info.size) {
        case 4:
          snprintf(str, sizeof(str), "%f", value.float32);
          break;
        case 8:
          snprintf(str, sizeof(str), "%g", value.float64);
          break;
        default:
          snprintf(str, sizeof(str), "error: unsupported float byte size %d.",
                   info.size);
          break;
        }
        break;

      case Vector:
        if (info.size > 0) {
          switch (info.format) {
          case VectorOfSInt8:
            snprintf(str, sizeof(str), "%s", "sint8   { ");
            pos = str + strlen(str);
            for (uint32_t i = 0; i < info.size; ++i) {
              PRINT_COMMA_SEPARATOR;
              if (pos < end)
                pos +=
                    snprintf(pos, end - pos, "%d", (int32_t)value.v_sint8[i]);
            }
            strlcat(str, " }", sizeof(str));
            break;

          default:
            DNBLogError(
                "unsupported vector format %d, defaulting to hex bytes.",
                info.format);
            [[clang::fallthrough]];
          case VectorOfUInt8:
            snprintf(str, sizeof(str), "%s", "uint8   { ");
            pos = str + strlen(str);
            for (uint32_t i = 0; i < info.size; ++i) {
              PRINT_COMMA_SEPARATOR;
              if (pos < end)
                pos +=
                    snprintf(pos, end - pos, "%u", (uint32_t)value.v_uint8[i]);
            }
            break;

          case VectorOfSInt16:
            snprintf(str, sizeof(str), "%s", "sint16  { ");
            pos = str + strlen(str);
            for (uint32_t i = 0; i < info.size / 2; ++i) {
              PRINT_COMMA_SEPARATOR;
              if (pos < end)
                pos +=
                    snprintf(pos, end - pos, "%d", (int32_t)value.v_sint16[i]);
            }
            break;

          case VectorOfUInt16:
            snprintf(str, sizeof(str), "%s", "uint16  { ");
            pos = str + strlen(str);
            for (uint32_t i = 0; i < info.size / 2; ++i) {
              PRINT_COMMA_SEPARATOR;
              if (pos < end)
                pos +=
                    snprintf(pos, end - pos, "%u", (uint32_t)value.v_uint16[i]);
            }
            break;

          case VectorOfSInt32:
            snprintf(str, sizeof(str), "%s", "sint32  { ");
            pos = str + strlen(str);
            for (uint32_t i = 0; i < info.size / 4; ++i) {
              PRINT_COMMA_SEPARATOR;
              if (pos < end)
                pos +=
                    snprintf(pos, end - pos, "%d", (int32_t)value.v_sint32[i]);
            }
            break;

          case VectorOfUInt32:
            snprintf(str, sizeof(str), "%s", "uint32  { ");
            pos = str + strlen(str);
            for (uint32_t i = 0; i < info.size / 4; ++i) {
              PRINT_COMMA_SEPARATOR;
              if (pos < end)
                pos +=
                    snprintf(pos, end - pos, "%u", (uint32_t)value.v_uint32[i]);
            }
            break;

          case VectorOfFloat32:
            snprintf(str, sizeof(str), "%s", "float32 { ");
            pos = str + strlen(str);
            for (uint32_t i = 0; i < info.size / 4; ++i) {
              PRINT_COMMA_SEPARATOR;
              if (pos < end)
                pos += snprintf(pos, end - pos, "%f", value.v_float32[i]);
            }
            break;

          case VectorOfUInt128:
            snprintf(str, sizeof(str), "%s", "uint128 { ");
            pos = str + strlen(str);
            for (uint32_t i = 0; i < info.size / 16; ++i) {
              PRINT_COMMA_SEPARATOR;
              if (pos < end)
                pos += snprintf(pos, end - pos, "0x%16.16llx%16.16llx",
                                value.v_uint64[i], value.v_uint64[i + 1]);
            }
            break;
          }
          strlcat(str, " }", sizeof(str));
        } else {
          snprintf(str, sizeof(str), "error: unsupported vector size %d.",
                   info.size);
        }
        break;

      default:
        snprintf(str, sizeof(str), "error: unsupported register type %d.",
                 info.type);
        break;
      }
    }

    DNBLog("%s%4s = %s%s", pre ? pre : "", info.name, str, post ? post : "");
  }
}
