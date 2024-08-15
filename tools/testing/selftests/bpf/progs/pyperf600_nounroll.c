// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#define STACK_MAX_LEN 600
#define NO_UNROLL
/* clang will not unroll at all.
 * Total program size is around 2k insns
 */
#include "pyperf.h"
