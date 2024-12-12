/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Boolean types support for NOLIBC
 * Copyright (C) 2024 Thomas Weißschuh <linux@weissschuh.net>
 */

#ifndef _NOLIBC_STDBOOL_H
#define _NOLIBC_STDBOOL_H

#define bool _Bool
#define true 1
#define false 0

#define __bool_true_false_are_defined 1

#endif /* _NOLIBC_STDBOOL_H */
