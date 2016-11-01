/*
 * TI Syscon Reset definitions
 *
 * Copyright (C) 2015-2016 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
