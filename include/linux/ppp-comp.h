/*
 * ppp-comp.h - Definitions for doing PPP packet compression.
 *
 * Copyright (c) 1994 The Australian National University.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, provided that the above copyright
 * notice appears in all copies.  This software is provided without any
 * warranty, express or implied. The Australian National University
 * makes no representations about the suitability of this software for
 * any purpose.
 *
 * IN NO EVENT SHALL THE AUSTRALIAN NATIONAL UNIVERSITY BE LIABLE TO ANY
 * PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
 * THE AUSTRALIAN NATIONAL UNIVERSITY HAVE BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * THE AUSTRALIAN NATIONAL UNIVERSITY SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE AUSTRALIAN NATIONAL UNIVERSITY HAS NO
 * OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS,
 * OR MODIFICATIONS.
 */

/*
 *  ==FILEVERSION 980319==
 *
 *  NOTE TO MAINTAINERS:
 *     If you modify this file at all, please set the above date.
 *     ppp-comp.h is shipped with a PPP distribution as well as with the kernel;
 *     if everyone increases the FILEVERSION number above, then scripts
 *     can do the right thing when deciding whether to install a new ppp-comp.h
 *     file.  Don't change the format of that line otherwise, so the
 *     installation script can recognize it.
 */

#ifndef _NET_PPP_COMP_H
#define _NET_PPP_COMP_H

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

/*
 * CCP codes.
 */

#define CCP_CONFREQ	1
#define CCP_CONFACK	2
#define CCP_TERMREQ	5
#define CCP_TERMACK	6
#define CCP_RESETREQ	14
#define CCP_RESETACK	15

/*
 * Max # bytes for a CCP option
 */

#define CCP_MAX_OPTION_LENGTH	32

/*
 * Parts of a CCP packet.
 */

#define CCP_CODE(dp)		((dp)[0])
#define CCP_ID(dp)		((dp)[1])
#define CCP_LENGTH(dp)		(((dp)[2] << 8) + (dp)[3])
#define CCP_HDRLEN		4

#define CCP_OPT_CODE(dp)	((dp)[0])
#define CCP_OPT_LENGTH(dp)	((dp)[1])
#define CCP_OPT_MINLEN		2

/*
 * Definitions for BSD-Compress.
 */

#define CI_BSD_COMPRESS		21	/* config. option for BSD-Compress */
#define CILEN_BSD_COMPRESS	3	/* length of config. option */

/* Macros for handling the 3rd byte of the BSD-Compress config option. */
#define BSD_NBITS(x)		((x) & 0x1F)	/* number of bits requested */
#define BSD_VERSION(x)		((x) >> 5)	/* version of option format */
#define BSD_CURRENT_VERSION	1		/* current version number */
#define BSD_MAKE_OPT(v, n)	(((v) << 5) | (n))

#define BSD_MIN_BITS		9	/* smallest code size supported */
#define BSD_MAX_BITS		15	/* largest code size supported */

/*
 * Definitions for Deflate.
 */

#define CI_DEFLATE		26	/* config option for Deflate */
#define CI_DEFLATE_DRAFT	24	/* value used in original draft RFC */
#define CILEN_DEFLATE		4	/* length of its config option */

#define DEFLATE_MIN_SIZE	9
#define DEFLATE_MAX_SIZE	15
#define DEFLATE_METHOD_VAL	8
#define DEFLATE_SIZE(x)		(((x) >> 4) + 8)
#define DEFLATE_METHOD(x)	((x) & 0x0F)
#define DEFLATE_MAKE_OPT(w)	((((w) - 8) << 4) + DEFLATE_METHOD_VAL)
#define DEFLATE_CHK_SEQUENCE	0

/*
 * Definitions for MPPE.
 */

#define CI_MPPE                18      /* config option for MPPE */
#define CILEN_MPPE              6      /* length of config option */

/*
 * Definitions for other, as yet unsupported, compression methods.
 */

#define CI_PREDICTOR_1		1	/* config option for Predictor-1 */
#define CILEN_PREDICTOR_1	2	/* length of its config option */
#define CI_PREDICTOR_2		2	/* config option for Predictor-2 */
#define CILEN_PREDICTOR_2	2	/* length of its config option */

#ifdef __KERNEL__
extern int ppp_register_compressor(struct compressor *);
extern void ppp_unregister_compressor(struct compressor *);
#endif /* __KERNEL__ */

#endif /* _NET_PPP_COMP_H */
