/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Mitsuru IWASAKI
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

#ifndef _SYS_POWER_H_
#define _SYS_POWER_H_

#include <sys/eventhandler.h>

/* Power management system type */
#define POWER_PM_TYPE_APM		0x00
#define POWER_PM_TYPE_ACPI		0x01
#define POWER_PM_TYPE_NONE		0xff

/* Commands for Power management function */
#define POWER_CMD_SUSPEND		0x00

/* Sleep state */
#define POWER_SLEEP_STATE_STANDBY	0x00
#define POWER_SLEEP_STATE_SUSPEND	0x01
#define POWER_SLEEP_STATE_HIBERNATE	0x02

typedef int (*power_pm_fn_t)(u_long, void*, ...);
extern int	 power_pm_register(u_int, power_pm_fn_t, void *);
extern u_int	 power_pm_get_type(void);
extern void	 power_pm_suspend(int);

/*
 * System power API.
 */
#define POWER_PROFILE_PERFORMANCE        0
#define POWER_PROFILE_ECONOMY            1

extern int	power_profile_get_state(void);
extern void	power_profile_set_state(int);

typedef void (*power_profile_change_hook)(void *, int);
EVENTHANDLER_DECLARE(power_profile_change, power_profile_change_hook);

#endif	/* !_SYS_POWER_H_ */

