/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT) */
/*
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RK803_H
#define _UAPI_RK803_H

#include <linux/types.h>

#define RK803_SET_GPIO1		_IOW('p',  1, int)
#define RK803_SET_GPIO2		_IOW('p',  2, int)
#define RK803_SET_CURENT1	_IOW('p',  3, int)
#define RK803_SET_CURENT2	_IOW('p',  4, int)

#endif /* _UAPI_RK803_H */
