// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/* ******************************************************************
 * debug
 * Part of FSE library
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * You can contact the author at :
 * - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
****************************************************************** */


/*
 * This module only hosts one global variable
 * which can be used to dynamically influence the verbosity of traces,
 * such as DEBUGLOG and RAWLOG
 */

#include "debug.h"

#if (DEBUGLEVEL>=2)
/* We only use this when DEBUGLEVEL>=2, but we get -Werror=pedantic errors if a
 * translation unit is empty. So remove this from Linux kernel builds, but
 * otherwise just leave it in.
 */
int g_debuglevel = DEBUGLEVEL;
#endif
