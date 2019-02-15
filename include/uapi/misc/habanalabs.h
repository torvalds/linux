/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABS_H_
#define HABANALABS_H_

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Defines that are asic-specific but constitutes as ABI between kernel driver
 * and userspace
 */
#define GOYA_KMD_SRAM_RESERVED_SIZE_FROM_START	0x8000	/* 32KB */

#endif /* HABANALABS_H_ */
