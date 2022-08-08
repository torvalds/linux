// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2016-20 Intel Corporation. */

#include <stddef.h>
#include "defines.h"

static uint8_t encl_buffer[8192] = { 1 };

static void *memcpy(void *dest, const void *src, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		((char *)dest)[i] = ((char *)src)[i];

	return dest;
}

void encl_body(void *rdi,  void *rsi)
{
	struct encl_op *op = (struct encl_op *)rdi;

	switch (op->type) {
	case ENCL_OP_PUT:
		memcpy(&encl_buffer[0], &op->buffer, 8);
		break;

	case ENCL_OP_GET:
		memcpy(&op->buffer, &encl_buffer[0], 8);
		break;

	default:
		break;
	}
}
