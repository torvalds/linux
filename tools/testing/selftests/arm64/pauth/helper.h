/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 ARM Limited */

#ifndef _HELPER_H_
#define _HELPER_H_

#include <stdlib.h>

void pac_corruptor(void);

/* PAuth sign a value with key ia and modifier value 0 */
size_t keyia_sign(size_t val);
size_t keyib_sign(size_t val);
size_t keyda_sign(size_t val);
size_t keydb_sign(size_t val);
size_t keyg_sign(size_t val);

#endif
