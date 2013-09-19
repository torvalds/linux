/*
 * NFC Digital Protocol stack
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef __DIGITAL_H
#define __DIGITAL_H

#include <net/nfc/nfc.h>
#include <net/nfc/digital.h>

#define PR_DBG(fmt, ...)  pr_debug("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#define PR_ERR(fmt, ...)  pr_err("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#define PROTOCOL_ERR(req) pr_err("%s:%d: NFC Digital Protocol error: %s\n", \
				 __func__, __LINE__, req)

#endif /* __DIGITAL_H */
