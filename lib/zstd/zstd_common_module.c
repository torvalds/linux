// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include <linux/module.h>

#include "common/huf.h"
#include "common/fse.h"
#include "common/zstd_internal.h"

// Export symbols shared by compress and decompress into a common module

#undef ZSTD_isError   /* defined within zstd_internal.h */
EXPORT_SYMBOL_GPL(FSE_readNCount);
EXPORT_SYMBOL_GPL(HUF_readStats);
EXPORT_SYMBOL_GPL(HUF_readStats_wksp);
EXPORT_SYMBOL_GPL(ZSTD_isError);
EXPORT_SYMBOL_GPL(ZSTD_getErrorName);
EXPORT_SYMBOL_GPL(ZSTD_getErrorCode);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Zstd Common");
