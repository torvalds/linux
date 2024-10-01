// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2016-20 Intel Corporation. */

#include <stddef.h>
#include "defines.h"

/*
 * Data buffer spanning two pages that will be placed first in the .data
 * segment via the linker script. Even if not used internally the second page
 * is needed by external test manipulating page permissions, so mark
 * encl_buffer as "used" to make sure it is entirely preserved by the compiler.
 */
static uint8_t __used __section(".data.encl_buffer") encl_buffer[8192] = { 1 };

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

/*
 * Symbol placed at the start of the enclave image by the linker script.
 * Declare this extern symbol with visibility "hidden" to ensure the compiler
 * does not access it through the GOT and generates position-independent
 * addressing as __encl_base(%rip), so we can get the actual enclave base
 * during runtime.
 */
extern const uint8_t __attribute__((visibility("hidden"))) __encl_base;

typedef void (*encl_op_t)(void *);
static const encl_op_t encl_op_array[ENCL_OP_MAX] = {
	do_encl_op_put_to_buf,
	do_encl_op_get_from_buf,
	do_encl_op_put_to_addr,
	do_encl_op_get_from_addr,
	do_encl_op_nop,
	do_encl_eaccept,
	do_encl_emodpe,
	do_encl_init_tcs_page,
};

void encl_body(void *rdi,  void *rsi)
{
	struct encl_op_header *header = (struct encl_op_header *)rdi;
	encl_op_t op;

	if (header->type >= ENCL_OP_MAX)
		return;

	/*
	 * The enclave base address needs to be added, as this call site
	 * *cannot be* made rip-relative by the compiler, or fixed up by
	 * any other possible means.
	 */
	op = ((uint64_t)&__encl_base) + encl_op_array[header->type];

	(*op)(header);
}
