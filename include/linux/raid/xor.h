/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XOR_H
#define _XOR_H

#define MAX_XOR_BLOCKS 4

extern void xor_blocks(unsigned int count, unsigned int bytes,
	void *dest, void **srcs);

void xor_gen(void *dest, void **srcs, unsigned int src_cnt, unsigned int bytes);

#endif /* _XOR_H */
