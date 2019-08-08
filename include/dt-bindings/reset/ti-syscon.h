/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * TI Syscon Reset definitions
 *
 * Copyright (C) 2015-2016 Texas Instruments Incorporated - http://www.ti.com/
 */

#ifndef __DT_BINDINGS_RESET_TI_SYSCON_H__
#define __DT_BINDINGS_RESET_TI_SYSCON_H__

/*
 * The reset does not support the feature and corresponding
 * values are not valid
 */
#define ASSERT_NONE	(1 << 0)
#define DEASSERT_NONE	(1 << 1)
#define STATUS_NONE	(1 << 2)

/* When set this function is activated by setting(vs clearing) this bit */
#define ASSERT_SET	(1 << 3)
#define DEASSERT_SET	(1 << 4)
#define STATUS_SET	(1 << 5)

/* The following are the inverse of the above and are added for consistency */
#define ASSERT_CLEAR	(0 << 3)
#define DEASSERT_CLEAR	(0 << 4)
#define STATUS_CLEAR	(0 << 5)

#endif
