/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ppp-comp.h - Definitions for doing PPP packet compression.
 *
 * Copyright 1994-1998 Paul Mackerras.
 */
#ifndef _NET_PPP_COMP_H
#define _NET_PPP_COMP_H

#include <uapi/linux/ppp-comp.h>

struct compstat;
struct module;

/*
 * The following symbols control whether we include code for
 * various compression methods.
 */

#ifndef DO_BSD_COMPRESS
#define DO_BSD_COMPRESS	1	/* by default, include BSD-Compress */
#endif
#ifndef DO_DEFLATE
#define DO_DEFLATE	1	/* by default, include Deflate */
#endif
#define DO_PREDICTOR_1	0
#define DO_PREDICTOR_2	0

/*
 * Structure giving methods for compression/decompression.
 */

struct compressor {
	int	compress_proto;	/* CCP compression protocol number */

	/* Allocate space for a compressor (transmit side) */
	void	*(*comp_alloc) (unsigned char *options, int opt_len);

	/* Free space used by a compressor */
	void	(*comp_free) (void *state);

	/* Initialize a compressor */
	int	(*comp_init) (void *state, unsigned char *options,
			      int opt_len, int unit, int opthdr, int debug);

	/* Reset a compressor */
	void	(*comp_reset) (void *state);

	/* Compress a packet */
	int     (*compress) (void *state, unsigned char *rptr,
			      unsigned char *obuf, int isize, int osize);

	/* Return compression statistics */
	void	(*comp_stat) (void *state, struct compstat *stats);

	/* Allocate space for a decompressor (receive side) */
	void	*(*decomp_alloc) (unsigned char *options, int opt_len);

	/* Free space used by a decompressor */
	void	(*decomp_free) (void *state);

	/* Initialize a decompressor */
	int	(*decomp_init) (void *state, unsigned char *options,
				int opt_len, int unit, int opthdr, int mru,
				int debug);

	/* Reset a decompressor */
	void	(*decomp_reset) (void *state);

	/* Decompress a packet. */
	int	(*decompress) (void *state, unsigned char *ibuf, int isize,
				unsigned char *obuf, int osize);

	/* Update state for an incompressible packet received */
	void	(*incomp) (void *state, unsigned char *ibuf, int icnt);

	/* Return decompression statistics */
	void	(*decomp_stat) (void *state, struct compstat *stats);

	/* Used in locking compressor modules */
	struct module *owner;
	/* Extra skb space needed by the compressor algorithm */
	unsigned int comp_extra;
};

/*
 * The return value from decompress routine is the length of the
 * decompressed packet if successful, otherwise DECOMP_ERROR
 * or DECOMP_FATALERROR if an error occurred.
 * 
 * We need to make this distinction so that we can disable certain
 * useful functionality, namely sending a CCP reset-request as a result
 * of an error detected after decompression.  This is to avoid infringing
 * a patent held by Motorola.
 * Don't you just lurve software patents.
 */

#define DECOMP_ERROR		-1	/* error detected before decomp. */
#define DECOMP_FATALERROR	-2	/* error detected after decomp. */

extern int ppp_register_compressor(struct compressor *);
extern void ppp_unregister_compressor(struct compressor *);
#endif /* _NET_PPP_COMP_H */
