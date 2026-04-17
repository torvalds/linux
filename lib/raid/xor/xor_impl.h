/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XOR_IMPL_H
#define _XOR_IMPL_H

#include <linux/init.h>
#include <linux/minmax.h>

struct xor_block_template {
	struct xor_block_template *next;
	const char *name;
	int speed;
	void (*xor_gen)(void *dest, void **srcs, unsigned int src_cnt,
			unsigned int bytes);
};

#define __DO_XOR_BLOCKS(_name, _handle1, _handle2, _handle3, _handle4)	\
void								\
xor_gen_##_name(void *dest, void **srcs, unsigned int src_cnt,		\
		unsigned int bytes)					\
{									\
	unsigned int src_off = 0;					\
									\
	while (src_cnt > 0) {						\
		unsigned int this_cnt = min(src_cnt, 4);		\
									\
		if (this_cnt == 1)					\
			_handle1(bytes, dest, srcs[src_off]);		\
		else if (this_cnt == 2)					\
			_handle2(bytes, dest, srcs[src_off],		\
				srcs[src_off + 1]);			\
		else if (this_cnt == 3)					\
			_handle3(bytes, dest, srcs[src_off],		\
				srcs[src_off + 1], srcs[src_off + 2]);	\
		else							\
			_handle4(bytes, dest, srcs[src_off],		\
				srcs[src_off + 1], srcs[src_off + 2],	\
				srcs[src_off + 3]);			\
									\
		src_cnt -= this_cnt;					\
		src_off += this_cnt;					\
	}								\
}

#define DO_XOR_BLOCKS(_name, _handle1, _handle2, _handle3, _handle4)	\
	static __DO_XOR_BLOCKS(_name, _handle1, _handle2, _handle3, _handle4)

/* generic implementations */
extern struct xor_block_template xor_block_8regs;
extern struct xor_block_template xor_block_32regs;
extern struct xor_block_template xor_block_8regs_p;
extern struct xor_block_template xor_block_32regs_p;

void __init xor_register(struct xor_block_template *tmpl);
void __init xor_force(struct xor_block_template *tmpl);

#endif /* _XOR_IMPL_H */
