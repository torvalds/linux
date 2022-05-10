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

enum sgx_enclu_function {
	EACCEPT = 0x5,
	EMODPE = 0x6,
};

static void do_encl_emodpe(void *_op)
{
	struct sgx_secinfo secinfo __aligned(sizeof(struct sgx_secinfo)) = {0};
	struct encl_op_emodpe *op = _op;

	secinfo.flags = op->flags;

	asm volatile(".byte 0x0f, 0x01, 0xd7"
				:
				: "a" (EMODPE),
				  "b" (&secinfo),
				  "c" (op->epc_addr));
}

static void do_encl_eaccept(void *_op)
{
	struct sgx_secinfo secinfo __aligned(sizeof(struct sgx_secinfo)) = {0};
	struct encl_op_eaccept *op = _op;
	int rax;

	secinfo.flags = op->flags;

	asm volatile(".byte 0x0f, 0x01, 0xd7"
				: "=a" (rax)
				: "a" (EACCEPT),
				  "b" (&secinfo),
				  "c" (op->epc_addr));

	op->ret = rax;
}

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

static void do_encl_op_nop(void *_op)
{

}

void encl_body(void *rdi,  void *rsi)
{
	const void (*encl_op_array[ENCL_OP_MAX])(void *) = {
		do_encl_op_put_to_buf,
		do_encl_op_get_from_buf,
		do_encl_op_put_to_addr,
		do_encl_op_get_from_addr,
		do_encl_op_nop,
		do_encl_eaccept,
		do_encl_emodpe,
	};

	struct encl_op_header *op = (struct encl_op_header *)rdi;

	if (op->type < ENCL_OP_MAX)
		(*encl_op_array[op->type])(op);
}
