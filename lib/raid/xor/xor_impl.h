/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XOR_IMPL_H
#define _XOR_IMPL_H

#include <linux/init.h>

struct xor_block_template {
	struct xor_block_template *next;
	const char *name;
	int speed;
	void (*do_2)(unsigned long, unsigned long * __restrict,
		     const unsigned long * __restrict);
	void (*do_3)(unsigned long, unsigned long * __restrict,
		     const unsigned long * __restrict,
		     const unsigned long * __restrict);
	void (*do_4)(unsigned long, unsigned long * __restrict,
		     const unsigned long * __restrict,
		     const unsigned long * __restrict,
		     const unsigned long * __restrict);
	void (*do_5)(unsigned long, unsigned long * __restrict,
		     const unsigned long * __restrict,
		     const unsigned long * __restrict,
		     const unsigned long * __restrict,
		     const unsigned long * __restrict);
};

/* generic implementations */
extern struct xor_block_template xor_block_8regs;
extern struct xor_block_template xor_block_32regs;
extern struct xor_block_template xor_block_8regs_p;
extern struct xor_block_template xor_block_32regs_p;

void __init xor_register(struct xor_block_template *tmpl);
void __init xor_force(struct xor_block_template *tmpl);

#endif /* _XOR_IMPL_H */
