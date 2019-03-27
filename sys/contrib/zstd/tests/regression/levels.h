/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef LEVEL
# error LEVEL(x) must be defined
#endif
#ifndef FAST_LEVEL
# error FAST_LEVEL(x) must be defined
#endif

/**
 * The levels are chosen to trigger every strategy in every source size,
 * as well as some fast levels and the default level.
 * If you change the compression levels, you should probably update these.
 */

FAST_LEVEL(5)

FAST_LEVEL(3)

FAST_LEVEL(1)
LEVEL(0)
LEVEL(1)

LEVEL(3)
LEVEL(4)
LEVEL(5)
LEVEL(6)
LEVEL(7)

LEVEL(9)

LEVEL(13)

LEVEL(16)

LEVEL(19)
