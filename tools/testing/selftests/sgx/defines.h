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

enum encl_op_type {
	ENCL_OP_PUT,
	ENCL_OP_GET,
	ENCL_OP_MAX,
};

struct encl_op_header {
	uint64_t type;
};

struct encl_op_put {
	struct encl_op_header header;
	uint64_t value;
};

struct encl_op_get {
	struct encl_op_header header;
	uint64_t value;
};

#endif /* DEFINES_H */
