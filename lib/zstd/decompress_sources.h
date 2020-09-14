/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/*
 * Copyright (c) Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/*
 * This file includes every .c file needed for decompression.
 * It is used by lib/decompress_unzstd.c to include the decompression
 * source into the translation-unit, so it can be used for kernel
 * decompression.
 */

#include "entropy_common.c"
#include "fse_decompress.c"
#include "huf_decompress.c"
#include "zstd_common.c"
#include "decompress.c"
