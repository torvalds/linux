/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "callbacks.h"

void cbor_null_uint8_callback(void *_CBOR_UNUSED(_ctx),
                              uint8_t _CBOR_UNUSED(_val)) {}

void cbor_null_uint16_callback(void *_CBOR_UNUSED(_ctx),
                               uint16_t _CBOR_UNUSED(_val)) {}

void cbor_null_uint32_callback(void *_CBOR_UNUSED(_ctx),
                               uint32_t _CBOR_UNUSED(_val)) {}

void cbor_null_uint64_callback(void *_CBOR_UNUSED(_ctx),
                               uint64_t _CBOR_UNUSED(_val)) {}

void cbor_null_negint8_callback(void *_CBOR_UNUSED(_ctx),
                                uint8_t _CBOR_UNUSED(_val)) {}

void cbor_null_negint16_callback(void *_CBOR_UNUSED(_ctx),
                                 uint16_t _CBOR_UNUSED(_val)) {}

void cbor_null_negint32_callback(void *_CBOR_UNUSED(_ctx),
                                 uint32_t _CBOR_UNUSED(_val)) {}

void cbor_null_negint64_callback(void *_CBOR_UNUSED(_ctx),
                                 uint64_t _CBOR_UNUSED(_val)) {}

void cbor_null_string_callback(void *_CBOR_UNUSED(_ctx),
                               cbor_data _CBOR_UNUSED(_val),
                               uint64_t _CBOR_UNUSED(_val2)) {}

void cbor_null_string_start_callback(void *_CBOR_UNUSED(_ctx)) {}

void cbor_null_byte_string_callback(void *_CBOR_UNUSED(_ctx),
                                    cbor_data _CBOR_UNUSED(_val),
                                    uint64_t _CBOR_UNUSED(_val2)) {}

void cbor_null_byte_string_start_callback(void *_CBOR_UNUSED(_ctx)) {}

void cbor_null_array_start_callback(void *_CBOR_UNUSED(_ctx),
                                    uint64_t _CBOR_UNUSED(_val)) {}

void cbor_null_indef_array_start_callback(void *_CBOR_UNUSED(_ctx)) {}

void cbor_null_map_start_callback(void *_CBOR_UNUSED(_ctx),
                                  uint64_t _CBOR_UNUSED(_val)) {}

void cbor_null_indef_map_start_callback(void *_CBOR_UNUSED(_ctx)) {}

void cbor_null_tag_callback(void *_CBOR_UNUSED(_ctx),
                            uint64_t _CBOR_UNUSED(_val)) {}

void cbor_null_float2_callback(void *_CBOR_UNUSED(_ctx),
                               float _CBOR_UNUSED(_val)) {}

void cbor_null_float4_callback(void *_CBOR_UNUSED(_ctx),
                               float _CBOR_UNUSED(_val)) {}

void cbor_null_float8_callback(void *_CBOR_UNUSED(_ctx),
                               double _CBOR_UNUSED(_val)) {}

void cbor_null_null_callback(void *_CBOR_UNUSED(_ctx)) {}

void cbor_null_undefined_callback(void *_CBOR_UNUSED(_ctx)) {}

void cbor_null_boolean_callback(void *_CBOR_UNUSED(_ctx),
                                bool _CBOR_UNUSED(_val)) {}

void cbor_null_indef_break_callback(void *_CBOR_UNUSED(_ctx)) {}

CBOR_EXPORT const struct cbor_callbacks cbor_empty_callbacks = {
    /* Type 0 - Unsigned integers */
    .uint8 = cbor_null_uint8_callback,
    .uint16 = cbor_null_uint16_callback,
    .uint32 = cbor_null_uint32_callback,
    .uint64 = cbor_null_uint64_callback,

    /* Type 1 - Negative integers */
    .negint8 = cbor_null_negint8_callback,
    .negint16 = cbor_null_negint16_callback,
    .negint32 = cbor_null_negint32_callback,
    .negint64 = cbor_null_negint64_callback,

    /* Type 2 - Byte strings */
    .byte_string_start = cbor_null_byte_string_start_callback,
    .byte_string = cbor_null_byte_string_callback,

    /* Type 3 - Strings */
    .string_start = cbor_null_string_start_callback,
    .string = cbor_null_string_callback,

    /* Type 4 - Arrays */
    .indef_array_start = cbor_null_indef_array_start_callback,
    .array_start = cbor_null_array_start_callback,

    /* Type 5 - Maps */
    .indef_map_start = cbor_null_indef_map_start_callback,
    .map_start = cbor_null_map_start_callback,

    /* Type 6 - Tags */
    .tag = cbor_null_tag_callback,

    /* Type 7 - Floats & misc */
    /* Type names cannot be member names */
    .float2 = cbor_null_float2_callback,
    /* 2B float is not supported in standard C */
    .float4 = cbor_null_float4_callback,
    .float8 = cbor_null_float8_callback,
    .undefined = cbor_null_undefined_callback,
    .null = cbor_null_null_callback,
    .boolean = cbor_null_boolean_callback,

    /* Shared indefinites */
    .indef_break = cbor_null_indef_break_callback,
};
