/* SPDX-License-Identifier: GPL-2.0+ */
#define atomic_t int32_t
#define atomic_inc(x) uatomic_inc(x)
#define atomic_read(x) uatomic_read(x)
#define atomic_set(x, y) uatomic_set(x, y)
#define U8_MAX UCHAR_MAX
#include "../../../../include/linux/maple_tree.h"
