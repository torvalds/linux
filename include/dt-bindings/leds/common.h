/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides macros for the common LEDs device tree bindings.
 *
 * Copyright (C) 2015, Samsung Electronics Co., Ltd.
 *
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 */

#ifndef __DT_BINDINGS_LEDS_H
#define __DT_BINDINGS_LEDS_H

/* External trigger type */
#define LEDS_TRIG_TYPE_EDGE	0
#define LEDS_TRIG_TYPE_LEVEL	1

/* Boost modes */
#define LEDS_BOOST_OFF		0
#define LEDS_BOOST_ADAPTIVE	1
#define LEDS_BOOST_FIXED	2

#endif /* __DT_BINDINGS_LEDS_H */
