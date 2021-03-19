/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2016-20 Intel Corporation.
 */

#ifndef DEFINES_H
#define DEFINES_H

#include <stdint.h>

#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define __aligned(x) __attribute__((__aligned__(x)))
#define __packed __attribute__((packed))

#include "../../../../arch/x86/include/asm/sgx.h"
#include "../../../../arch/x86/include/asm/enclu.h"
#include "../../../../arch/x86/include/uapi/asm/sgx.h"

#endif /* DEFINES_H */
