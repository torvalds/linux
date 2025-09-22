/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef LIBCBOR_LOADERS_H
#define LIBCBOR_LOADERS_H

#include "cbor/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read the given uint from the given location, no questions asked */
_CBOR_NODISCARD
uint8_t _cbor_load_uint8(const unsigned char *source);

_CBOR_NODISCARD
uint16_t _cbor_load_uint16(const unsigned char *source);

_CBOR_NODISCARD
uint32_t _cbor_load_uint32(const unsigned char *source);

_CBOR_NODISCARD
uint64_t _cbor_load_uint64(const unsigned char *source);

_CBOR_NODISCARD
float _cbor_load_half(cbor_data source);

_CBOR_NODISCARD
float _cbor_load_float(cbor_data source);

_CBOR_NODISCARD
double _cbor_load_double(cbor_data source);

#ifdef __cplusplus
}
#endif

#endif  // LIBCBOR_LOADERS_H
