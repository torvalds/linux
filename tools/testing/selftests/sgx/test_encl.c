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
				: /* no outputs */
				: "a" (EMODPE),
				  "b" (&secinfo),
				  "c" (op->epc_addr)
				: "memory" /* read from secinfo pointer */);
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
				  "c" (op->epc_addr)
				: "memory" /* read from secinfo pointer */);

	op->ret = rax;
}

static void *memcpy(void *dest, const void *src, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		((char *)dest)[i] = ((char *)src)[i];

	return dest;
}

static void *memset(void *dest, int c, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		((char *)dest)[i] = c;

	return dest;
}

static void do_encl_init_tcs_page(void *_op)
{
	struct encl_op_init_tcs_page *op = _op;
	void *tcs = (void *)op->tcs_page;
	uint32_t val_32;

	memset(tcs, 0, 16);			/* STATE and FLAGS */
	memcpy(tcs + 16, &op->ssa, 8);		/* OSSA */
	memset(tcs + 24, 0, 4);			/* CSSA */
	val_32 = 1;
	memcpy(tcs + 28, &val_32, 4);		/* NSSA */
	memcpy(tcs + 32, &op->entry, 8);	/* OENTRY */
	memset(tcs + 40, 0, 24);		/* AEP, OFSBASE, OGSBASE */
	val_32 = 0xFFFFFFFF;
	memcpy(tcs + 64, &val_32, 4);		/* FSLIMIT */
	memcpy(tcs + 68, &val_32, 4);		/* GSLIMIT */
	memset(tcs + 72, 0, 4024);		/* Reserved */
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
		do_encl_init_tcs_page,
	};

	struct encl_op_header *op = (struct encl_op_header *)rdi;

	if (op->type < ENCL_OP_MAX)
		(*encl_op_array[op->type])(op);
}
