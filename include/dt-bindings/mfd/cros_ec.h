/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * DTS binding definitions used for the Chromium OS Embedded Controller.
 *
 * Copyright (c) 2022 The Chromium OS Authors. All rights reserved.
 */

#ifndef _DT_BINDINGS_MFD_CROS_EC_H
#define _DT_BINDINGS_MFD_CROS_EC_H

/* Typed channel for keyboard backlight. */
#define CROS_EC_PWM_DT_KB_LIGHT		0
/* Typed channel for display backlight. */
#define CROS_EC_PWM_DT_DISPLAY_LIGHT	1
/* Number of typed channels. */
#define CROS_EC_PWM_DT_COUNT		2

#endif
