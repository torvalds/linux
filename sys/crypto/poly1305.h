/*	$OpenBSD: poly1305.h,v 1.2 2020/07/22 13:54:30 tobhe Exp $	*/
/*
 * Public Domain poly1305 from Andrew Moon
 *
 * poly1305 implementation using 32 bit * 32 bit = 64 bit multiplication
 * and 64 bit addition from https://github.com/floodyberry/poly1305-donna
 */

#ifndef _POLY1305_H_
#define _POLY1305_H_

#define poly1305_block_size 16

typedef struct poly1305_state {
	unsigned long r[5];
	unsigned long h[5];
	unsigned long pad[4];
	size_t leftover;
	unsigned char buffer[poly1305_block_size];
	unsigned char final;
} poly1305_state;

void	poly1305_init(poly1305_state *, const unsigned char[32]);
void	poly1305_update(poly1305_state *, const unsigned char *, size_t);
void	poly1305_finish(poly1305_state *, unsigned char[16]);

#endif	/* _POLY1305_H_ */
