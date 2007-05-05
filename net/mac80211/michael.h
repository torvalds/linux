/*
 * Michael MIC implementation - optimized for TKIP MIC operations
 * Copyright 2002-2003, Instant802 Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MICHAEL_H
#define MICHAEL_H

#include <linux/types.h>

#define MICHAEL_MIC_LEN 8

void michael_mic(u8 *key, u8 *da, u8 *sa, u8 priority,
		 u8 *data, size_t data_len, u8 *mic);

#endif /* MICHAEL_H */
