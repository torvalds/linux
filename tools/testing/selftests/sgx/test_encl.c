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

static void do_encl_op_put_to_buf(void *op)
{
	struct encl_op_put_to_buf *op2 = op;

	memcpy(&encl_buffer[0], &op2->value, 8);
}

static void do_encl_op_get_from_buf(void *op)
{
	struct encl_op_get_from_buf *op2 = op;

	memcpy(&op2->value, &encl_buffer[0], 8);
}

void encl_body(void *rdi,  void *rsi)
{
	const void (*encl_op_array[ENCL_OP_MAX])(void *) = {
		do_encl_op_put_to_buf,
		do_encl_op_get_from_buf,
	};

	struct encl_op_header *op = (struct encl_op_header *)rdi;

	if (op->type < ENCL_OP_MAX)
		(*encl_op_array[op->type])(op);
}
