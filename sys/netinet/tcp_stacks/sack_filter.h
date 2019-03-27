#ifndef __sack_filter_h__
#define __sack_filter_h__
/*-
 * Copyright (c) 2017 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * __FBSDID("$FreeBSD$");
 */

/*
 * Seven entry's is carefully choosen to
 * fit in one cache line. We can easily
 * change this to 15 (but it gets very
 * little extra filtering). To change it
 * to be larger than 15 would require either
 * sf_bits becoming a uint32_t and then you
 * could go to 31.. or change it to a full
 * bitstring.. It is really doubtful you
 * will get much benefit beyond 7, in testing
 * there was a small amount but very very small.
 */
#define SACK_FILTER_BLOCKS 7

struct sack_filter {
	tcp_seq sf_ack;
	uint16_t sf_bits;
	uint8_t sf_cur;
	uint8_t sf_used;
	struct sackblk sf_blks[SACK_FILTER_BLOCKS];
};
#ifdef _KERNEL
void sack_filter_clear(struct sack_filter *sf, tcp_seq seq);
int sack_filter_blks(struct sack_filter *sf, struct sackblk *in, int numblks, tcp_seq th_ack);

#endif
#endif
