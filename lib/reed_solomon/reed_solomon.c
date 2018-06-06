// SPDX-License-Identifier: GPL-2.0
/*
 * Generic Reed Solomon encoder / decoder library
 *
 * Copyright (C) 2004 Thomas Gleixner (tglx@linutronix.de)
 *
 * Reed Solomon code lifted from reed solomon library written by Phil Karn
 * Copyright 2002 Phil Karn, KA9Q
 *
 * Description:
 *
 * The generic Reed Solomon library provides runtime configurable
 * encoding / decoding of RS codes.
 *
 * Each user must call init_rs to get a pointer to a rs_control structure
 * for the given rs parameters. The control struct is unique per instance.
 * It points to a codec which can be shared by multiple control structures.
 * If a codec is newly allocated then the polynomial arrays for fast
 * encoding / decoding are built. This can take some time so make sure not
 * to call this function from a time critical path.  Usually a module /
 * driver should initialize the necessary rs_control structure on module /
 * driver init and release it on exit.
 *
 * The encoding puts the calculated syndrome into a given syndrome buffer.
 *
 * The decoding is a two step process. The first step calculates the
 * syndrome over the received (data + syndrome) and calls the second stage,
 * which does the decoding / error correction itself.  Many hw encoders
 * provide a syndrome calculation over the received data + syndrome and can
 * call the second stage directly.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/rslib.h>
#include <linux/slab.h>
#include <linux/mutex.h>

enum {
	RS_DECODE_LAMBDA,
	RS_DECODE_SYN,
	RS_DECODE_B,
	RS_DECODE_T,
	RS_DECODE_OMEGA,
	RS_DECODE_ROOT,
	RS_DECODE_REG,
	RS_DECODE_LOC,
	RS_DECODE_NUM_BUFFERS
};

/* This list holds all currently allocated rs codec structures */
static LIST_HEAD(codec_list);
/* Protection for the list */
static DEFINE_MUTEX(rslistlock);

/**
 * codec_init - Initialize a Reed-Solomon codec
 * @symsize:	symbol size, bits (1-8)
 * @gfpoly:	Field generator polynomial coefficients
 * @gffunc:	Field generator function
 * @fcr:	first root of RS code generator polynomial, index form
 * @prim:	primitive element to generate polynomial roots
 * @nroots:	RS code generator polynomial degree (number of roots)
 * @gfp:	GFP_ flags for allocations
 *
 * Allocate a codec structure and the polynom arrays for faster
 * en/decoding. Fill the arrays according to the given parameters.
 */
static struct rs_codec *codec_init(int symsize, int gfpoly, int (*gffunc)(int),
				   int fcr, int prim, int nroots, gfp_t gfp)
{
	int i, j, sr, root, iprim;
	struct rs_codec *rs;

	rs = kzalloc(sizeof(*rs), gfp);
	if (!rs)
		return NULL;

	INIT_LIST_HEAD(&rs->list);

	rs->mm = symsize;
	rs->nn = (1 << symsize) - 1;
	rs->fcr = fcr;
	rs->prim = prim;
	rs->nroots = nroots;
	rs->gfpoly = gfpoly;
	rs->gffunc = gffunc;

	/* Allocate the arrays */
	rs->alpha_to = kmalloc(sizeof(uint16_t) * (rs->nn + 1), gfp);
	if (rs->alpha_to == NULL)
		goto err;

	rs->index_of = kmalloc(sizeof(uint16_t) * (rs->nn + 1), gfp);
	if (rs->index_of == NULL)
		goto err;

	rs->genpoly = kmalloc(sizeof(uint16_t) * (rs->nroots + 1), gfp);
	if(rs->genpoly == NULL)
		goto err;

	/* Generate Galois field lookup tables */
	rs->index_of[0] = rs->nn;	/* log(zero) = -inf */
	rs->alpha_to[rs->nn] = 0;	/* alpha**-inf = 0 */
	if (gfpoly) {
		sr = 1;
		for (i = 0; i < rs->nn; i++) {
			rs->index_of[sr] = i;
			rs->alpha_to[i] = sr;
			sr <<= 1;
			if (sr & (1 << symsize))
				sr ^= gfpoly;
			sr &= rs->nn;
		}
	} else {
		sr = gffunc(0);
		for (i = 0; i < rs->nn; i++) {
			rs->index_of[sr] = i;
			rs->alpha_to[i] = sr;
			sr = gffunc(sr);
		}
	}
	/* If it's not primitive, exit */
	if(sr != rs->alpha_to[0])
		goto err;

	/* Find prim-th root of 1, used in decoding */
	for(iprim = 1; (iprim % prim) != 0; iprim += rs->nn);
	/* prim-th root of 1, index form */
	rs->iprim = iprim / prim;

	/* Form RS code generator polynomial from its roots */
	rs->genpoly[0] = 1;
	for (i = 0, root = fcr * prim; i < nroots; i++, root += prim) {
		rs->genpoly[i + 1] = 1;
		/* Multiply rs->genpoly[] by  @**(root + x) */
		for (j = i; j > 0; j--) {
			if (rs->genpoly[j] != 0) {
				rs->genpoly[j] = rs->genpoly[j -1] ^
					rs->alpha_to[rs_modnn(rs,
					rs->index_of[rs->genpoly[j]] + root)];
			} else
				rs->genpoly[j] = rs->genpoly[j - 1];
		}
		/* rs->genpoly[0] can never be zero */
		rs->genpoly[0] =
			rs->alpha_to[rs_modnn(rs,
				rs->index_of[rs->genpoly[0]] + root)];
	}
	/* convert rs->genpoly[] to index form for quicker encoding */
	for (i = 0; i <= nroots; i++)
		rs->genpoly[i] = rs->index_of[rs->genpoly[i]];

	rs->users = 1;
	list_add(&rs->list, &codec_list);
	return rs;

err:
	kfree(rs->genpoly);
	kfree(rs->index_of);
	kfree(rs->alpha_to);
	kfree(rs);
	return NULL;
}


/**
 *  free_rs - Free the rs control structure
 *  @rs:	The control structure which is not longer used by the
 *		caller
 *
 * Free the control structure. If @rs is the last user of the associated
 * codec, free the codec as well.
 */
void free_rs(struct rs_control *rs)
{
	struct rs_codec *cd;

	if (!rs)
		return;

	cd = rs->codec;
	mutex_lock(&rslistlock);
	cd->users--;
	if(!cd->users) {
		list_del(&cd->list);
		kfree(cd->alpha_to);
		kfree(cd->index_of);
		kfree(cd->genpoly);
		kfree(cd);
	}
	mutex_unlock(&rslistlock);
	kfree(rs);
}
EXPORT_SYMBOL_GPL(free_rs);

/**
 * init_rs_internal - Allocate rs control, find a matching codec or allocate a new one
 *  @symsize:	the symbol size (number of bits)
 *  @gfpoly:	the extended Galois field generator polynomial coefficients,
 *		with the 0th coefficient in the low order bit. The polynomial
 *		must be primitive;
 *  @gffunc:	pointer to function to generate the next field element,
 *		or the multiplicative identity element if given 0.  Used
 *		instead of gfpoly if gfpoly is 0
 *  @fcr:	the first consecutive root of the rs code generator polynomial
 *		in index form
 *  @prim:	primitive element to generate polynomial roots
 *  @nroots:	RS code generator polynomial degree (number of roots)
 *  @gfp:	GFP_ flags for allocations
 */
static struct rs_control *init_rs_internal(int symsize, int gfpoly,
					   int (*gffunc)(int), int fcr,
					   int prim, int nroots, gfp_t gfp)
{
	struct list_head *tmp;
	struct rs_control *rs;
	unsigned int bsize;

	/* Sanity checks */
	if (symsize < 1)
		return NULL;
	if (fcr < 0 || fcr >= (1<<symsize))
		return NULL;
	if (prim <= 0 || prim >= (1<<symsize))
		return NULL;
	if (nroots < 0 || nroots >= (1<<symsize))
		return NULL;

	/*
	 * The decoder needs buffers in each control struct instance to
	 * avoid variable size or large fixed size allocations on
	 * stack. Size the buffers to arrays of [nroots + 1].
	 */
	bsize = sizeof(uint16_t) * RS_DECODE_NUM_BUFFERS * (nroots + 1);
	rs = kzalloc(sizeof(*rs) + bsize, gfp);
	if (!rs)
		return NULL;

	mutex_lock(&rslistlock);

	/* Walk through the list and look for a matching entry */
	list_for_each(tmp, &codec_list) {
		struct rs_codec *cd = list_entry(tmp, struct rs_codec, list);

		if (symsize != cd->mm)
			continue;
		if (gfpoly != cd->gfpoly)
			continue;
		if (gffunc != cd->gffunc)
			continue;
		if (fcr != cd->fcr)
			continue;
		if (prim != cd->prim)
			continue;
		if (nroots != cd->nroots)
			continue;
		/* We have a matching one already */
		cd->users++;
		rs->codec = cd;
		goto out;
	}

	/* Create a new one */
	rs->codec = codec_init(symsize, gfpoly, gffunc, fcr, prim, nroots, gfp);
	if (!rs->codec) {
		kfree(rs);
		rs = NULL;
	}
out:
	mutex_unlock(&rslistlock);
	return rs;
}

/**
 * init_rs_gfp - Create a RS control struct and initialize it
 *  @symsize:	the symbol size (number of bits)
 *  @gfpoly:	the extended Galois field generator polynomial coefficients,
 *		with the 0th coefficient in the low order bit. The polynomial
 *		must be primitive;
 *  @fcr:	the first consecutive root of the rs code generator polynomial
 *		in index form
 *  @prim:	primitive element to generate polynomial roots
 *  @nroots:	RS code generator polynomial degree (number of roots)
 *  @gfp:	GFP_ flags for allocations
 */
struct rs_control *init_rs_gfp(int symsize, int gfpoly, int fcr, int prim,
			       int nroots, gfp_t gfp)
{
	return init_rs_internal(symsize, gfpoly, NULL, fcr, prim, nroots, gfp);
}
EXPORT_SYMBOL_GPL(init_rs_gfp);

/**
 * init_rs_non_canonical - Allocate rs control struct for fields with
 *                         non-canonical representation
 *  @symsize:	the symbol size (number of bits)
 *  @gffunc:	pointer to function to generate the next field element,
 *		or the multiplicative identity element if given 0.  Used
 *		instead of gfpoly if gfpoly is 0
 *  @fcr:	the first consecutive root of the rs code generator polynomial
 *		in index form
 *  @prim:	primitive element to generate polynomial roots
 *  @nroots:	RS code generator polynomial degree (number of roots)
 */
struct rs_control *init_rs_non_canonical(int symsize, int (*gffunc)(int),
					 int fcr, int prim, int nroots)
{
	return init_rs_internal(symsize, 0, gffunc, fcr, prim, nroots,
				GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(init_rs_non_canonical);

#ifdef CONFIG_REED_SOLOMON_ENC8
/**
 *  encode_rs8 - Calculate the parity for data values (8bit data width)
 *  @rsc:	the rs control structure
 *  @data:	data field of a given type
 *  @len:	data length
 *  @par:	parity data, must be initialized by caller (usually all 0)
 *  @invmsk:	invert data mask (will be xored on data)
 *
 *  The parity uses a uint16_t data type to enable
 *  symbol size > 8. The calling code must take care of encoding of the
 *  syndrome result for storage itself.
 */
int encode_rs8(struct rs_control *rsc, uint8_t *data, int len, uint16_t *par,
	       uint16_t invmsk)
{
#include "encode_rs.c"
}
EXPORT_SYMBOL_GPL(encode_rs8);
#endif

#ifdef CONFIG_REED_SOLOMON_DEC8
/**
 *  decode_rs8 - Decode codeword (8bit data width)
 *  @rsc:	the rs control structure
 *  @data:	data field of a given type
 *  @par:	received parity data field
 *  @len:	data length
 *  @s:		syndrome data field (if NULL, syndrome is calculated)
 *  @no_eras:	number of erasures
 *  @eras_pos:	position of erasures, can be NULL
 *  @invmsk:	invert data mask (will be xored on data, not on parity!)
 *  @corr:	buffer to store correction bitmask on eras_pos
 *
 *  The syndrome and parity uses a uint16_t data type to enable
 *  symbol size > 8. The calling code must take care of decoding of the
 *  syndrome result and the received parity before calling this code.
 *
 *  Note: The rs_control struct @rsc contains buffers which are used for
 *  decoding, so the caller has to ensure that decoder invocations are
 *  serialized.
 *
 *  Returns the number of corrected bits or -EBADMSG for uncorrectable errors.
 */
int decode_rs8(struct rs_control *rsc, uint8_t *data, uint16_t *par, int len,
	       uint16_t *s, int no_eras, int *eras_pos, uint16_t invmsk,
	       uint16_t *corr)
{
#include "decode_rs.c"
}
EXPORT_SYMBOL_GPL(decode_rs8);
#endif

#ifdef CONFIG_REED_SOLOMON_ENC16
/**
 *  encode_rs16 - Calculate the parity for data values (16bit data width)
 *  @rsc:	the rs control structure
 *  @data:	data field of a given type
 *  @len:	data length
 *  @par:	parity data, must be initialized by caller (usually all 0)
 *  @invmsk:	invert data mask (will be xored on data, not on parity!)
 *
 *  Each field in the data array contains up to symbol size bits of valid data.
 */
int encode_rs16(struct rs_control *rsc, uint16_t *data, int len, uint16_t *par,
	uint16_t invmsk)
{
#include "encode_rs.c"
}
EXPORT_SYMBOL_GPL(encode_rs16);
#endif

#ifdef CONFIG_REED_SOLOMON_DEC16
/**
 *  decode_rs16 - Decode codeword (16bit data width)
 *  @rsc:	the rs control structure
 *  @data:	data field of a given type
 *  @par:	received parity data field
 *  @len:	data length
 *  @s:		syndrome data field (if NULL, syndrome is calculated)
 *  @no_eras:	number of erasures
 *  @eras_pos:	position of erasures, can be NULL
 *  @invmsk:	invert data mask (will be xored on data, not on parity!)
 *  @corr:	buffer to store correction bitmask on eras_pos
 *
 *  Each field in the data array contains up to symbol size bits of valid data.
 *
 *  Note: The rc_control struct @rsc contains buffers which are used for
 *  decoding, so the caller has to ensure that decoder invocations are
 *  serialized.
 *
 *  Returns the number of corrected bits or -EBADMSG for uncorrectable errors.
 */
int decode_rs16(struct rs_control *rsc, uint16_t *data, uint16_t *par, int len,
		uint16_t *s, int no_eras, int *eras_pos, uint16_t invmsk,
		uint16_t *corr)
{
#include "decode_rs.c"
}
EXPORT_SYMBOL_GPL(decode_rs16);
#endif

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Reed Solomon encoder/decoder");
MODULE_AUTHOR("Phil Karn, Thomas Gleixner");

