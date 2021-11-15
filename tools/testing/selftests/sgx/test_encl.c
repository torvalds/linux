// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2016-20 Intel Corporation. */

#include <stddef.h>
#include "defines.h"

/*
 * Data buffer spanning two pages that will be placed first in .data
 * segment. Even if not used internally the second page is needed by
 * external test manipulating page permissions.
 */
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

static void do_encl_op_put_to_addr(void *_op)
{
	struct encl_op_put_to_addr *op = _op;

	memcpy((void *)op->addr, &op->value, 8);
}

static void do_encl_op_get_from_addr(void *_op)
{
	struct encl_op_get_from_addr *op = _op;

	memcpy(&op->value, (void *)op->addr, 8);
}

void encl_body(void *rdi,  void *rsi)
{
	const void (*encl_op_array[ENCL_OP_MAX])(void *) = {
		do_encl_op_put_to_buf,
		do_encl_op_get_from_buf,
		do_encl_op_put_to_addr,
		do_encl_op_get_from_addr,
	};

	struct encl_op_header *op = (struct encl_op_header *)rdi;

	if (op->type < ENCL_OP_MAX)
		(*encl_op_array[op->type])(op);
}
