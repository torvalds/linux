/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This header provides constants for Keystone pinctrl bindings.
 *
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/
 */

#ifndef _DT_BINDINGS_PINCTRL_KEYSTONE_H
#define _DT_BINDINGS_PINCTRL_KEYSTONE_H

#define MUX_MODE0	0
#define MUX_MODE1	1
#define MUX_MODE2	2
#define MUX_MODE3	3
#define MUX_MODE4	4
#define MUX_MODE5	5

#define BUFFER_CLASS_B	(0 << 19)
#define BUFFER_CLASS_C	(1 << 19)
#define BUFFER_CLASS_D	(2 << 19)
#define BUFFER_CLASS_E	(3 << 19)

#define PULL_DISABLE	(1 << 16)
#define PIN_PULLUP	(1 << 17)
#define PIN_PULLDOWN	(0 << 17)

#define KEYSTONE_IOPAD_OFFSET(pa, offset) (((pa) & 0xffff) - (offset))

#define K2G_CORE_IOPAD(pa) KEYSTONE_IOPAD_OFFSET((pa), 0x1000)

#endif
