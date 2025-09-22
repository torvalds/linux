/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef LIBCBOR_UNICODE_H
#define LIBCBOR_UNICODE_H

#include "cbor/common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum _cbor_unicode_status_error { _CBOR_UNICODE_OK, _CBOR_UNICODE_BADCP };

/** Signals unicode validation error and possibly its location */
struct _cbor_unicode_status {
  enum _cbor_unicode_status_error status;
  uint64_t location;
};

_CBOR_NODISCARD
uint64_t _cbor_unicode_codepoint_count(cbor_data source, uint64_t source_length,
                                       struct _cbor_unicode_status* status);

#ifdef __cplusplus
}
#endif

#endif  // LIBCBOR_UNICODE_H
