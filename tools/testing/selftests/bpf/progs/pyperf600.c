// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#define STACK_MAX_LEN 600
/* clang will not unroll the loop 600 times.
 * Instead it will unroll it to the amount it deemed
 * appropriate, but the loop will still execute 600 times.
 * Total program size is around 90k insns
 */
#include "pyperf.h"
