/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_PRESPLIT_H
#define ZSTD_PRESPLIT_H

#include <linux/types.h>  /* size_t */

#define ZSTD_SLIPBLOCK_WORKSPACESIZE 8208

/* ZSTD_splitBlock():
 * @level must be a value between 0 and 4.
 *        higher levels spend more energy to detect block boundaries.
 * @workspace must be aligned for size_t.
 * @wkspSize must be at least >= ZSTD_SLIPBLOCK_WORKSPACESIZE
 * note:
 * For the time being, this function only accepts full 128 KB blocks.
 * Therefore, @blockSize must be == 128 KB.
 * While this could be extended to smaller sizes in the future,
 * it is not yet clear if this would be useful. TBD.
 */
size_t ZSTD_splitBlock(const void* blockStart, size_t blockSize,
                    int level,
                    void* workspace, size_t wkspSize);

#endif /* ZSTD_PRESPLIT_H */
