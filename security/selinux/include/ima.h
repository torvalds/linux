/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2021 Microsoft Corporation
 *
 * Author: Lakshmi Ramasubramanian (nramas@linux.microsoft.com)
 *
 * Measure critical data structures maintained by SELinux
 * using IMA subsystem.
 */

#ifndef _SELINUX_IMA_H_
#define _SELINUX_IMA_H_

#include "security.h"

#ifdef CONFIG_IMA
extern void selinux_ima_measure_state(void);
extern void selinux_ima_measure_state_locked(void);
#else
static inline void selinux_ima_measure_state(void)
{
}
static inline void selinux_ima_measure_state_locked(void)
{
}
#endif

#endif	/* _SELINUX_IMA_H_ */
