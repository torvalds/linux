/* ******************************************************************
 * debug
 * Part of FSE library
 * Copyright (c) Yann Collet, Facebook, Inc.
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

int g_debuglevel = DEBUGLEVEL;
