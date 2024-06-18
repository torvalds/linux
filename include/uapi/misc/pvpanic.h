/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef __PVPANIC_H__
#define __PVPANIC_H__

#include <linux/const.h>

#define PVPANIC_PANICKED	_BITUL(0)
#define PVPANIC_CRASH_LOADED	_BITUL(1)
#define PVPANIC_SHUTDOWN	_BITUL(2)

#endif /* __PVPANIC_H__ */
