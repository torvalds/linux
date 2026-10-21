/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright 2024 NXP
 */

#ifndef _DT_BINDINGS_I3C_I3C_H
#define _DT_BINDINGS_I3C_I3C_H

#define I2C_FM      (1 << 4)
#define I2C_FM_PLUS (0 << 4)

#define I2C_FILTER  (0 << 5)
#define I2C_NO_FILTER_HIGH_FREQUENCY    (1 << 5)
#define I2C_NO_FILTER_LOW_FREQUENCY     (2 << 5)

#define I3C_ADDR_METHOD_SETDASA     (1 << 0)
#define I3C_ADDR_METHOD_SETAASA     (1 << 1)
#define I3C_ADDR_METHOD_VENDOR      (1 << 2)

#endif
