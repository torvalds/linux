/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include "result.h"

char const* result_get_error_string(result_t result) {
    switch (result_get_error(result)) {
        case result_error_ok:
            return "okay";
        case result_error_skip:
            return "skip";
        case result_error_system_error:
            return "system error";
        case result_error_compression_error:
            return "compression error";
        case result_error_decompression_error:
            return "decompression error";
        case result_error_round_trip_error:
            return "round trip error";
    }
}
