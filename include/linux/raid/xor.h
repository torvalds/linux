/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XOR_H
#define _XOR_H

#define MAX_XOR_BLOCKS 4

extern void xor_blocks(unsigned int count, unsigned int bytes,
	void *dest, void **srcs);

#endif /* _XOR_H */
