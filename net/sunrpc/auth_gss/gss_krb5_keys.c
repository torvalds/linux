/*
 * COPYRIGHT (c) 2008
 * The Regents of the University of Michigan
 * ALL RIGHTS RESERVED
 *
 * Permission is granted to use, copy, create derivative works
 * and redistribute this software and such derivative works
 * for any purpose, so long as the name of The University of
 * Michigan is not used in any advertising or publicity
 * pertaining to the use of distribution of this software
 * without specific, written prior authorization.  If the
 * above copyright notice or any other identification of the
 * University of Michigan is included in any copy of any
 * portion of this software, then the disclaimer below must
 * also be included.
 *
 * THIS SOFTWARE IS PROVIDED AS IS, WITHOUT REPRESENTATION
 * FROM THE UNIVERSITY OF MICHIGAN AS TO ITS FITNESS FOR ANY
 * PURPOSE, AND WITHOUT WARRANTY BY THE UNIVERSITY OF
 * MICHIGAN OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * WITHOUT LIMITATION THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE
 * REGENTS OF THE UNIVERSITY OF MICHIGAN SHALL NOT BE LIABLE
 * FOR ANY DAMAGES, INCLUDING SPECIAL, INDIRECT, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES, WITH RESPECT TO ANY CLAIM ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OF THE SOFTWARE, EVEN
 * IF IT HAS BEEN OR IS HEREAFTER ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES.
 */

/*
 * Copyright (C) 1998 by the FundsXpress, INC.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <crypto/skcipher.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/sunrpc/xdr.h>
#include <linux/lcm.h>

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

/*
 * This is the n-fold function as described in rfc3961, sec 5.1
 * Taken from MIT Kerberos and modified.
 */

static void krb5_nfold(u32 inbits, const u8 *in,
		       u32 outbits, u8 *out)
{
	unsigned long ulcm;
	int byte, i, msbit;

	/* the code below is more readable if I make these bytes
	   instead of bits */

	inbits >>= 3;
	outbits >>= 3;

	/* first compute lcm(n,k) */
	ulcm = lcm(inbits, outbits);

	/* now do the real work */

	memset(out, 0, outbits);
	byte = 0;

	/* this will end up cycling through k lcm(k,n)/k times, which
	   is correct */
	for (i = ulcm-1; i >= 0; i--) {
		/* compute the msbit in k which gets added into this byte */
		msbit = (
			/* first, start with the msbit in the first,
			 * unrotated byte */
			 ((inbits << 3) - 1)
			 /* then, for each byte, shift to the right
			  * for each repetition */
			 + (((inbits << 3) + 13) * (i/inbits))
			 /* last, pick out the correct byte within
			  * that shifted repetition */
			 + ((inbits - (i % inbits)) << 3)
			 ) % (inbits << 3);

		/* pull out the byte value itself */
		byte += (((in[((inbits - 1) - (msbit >> 3)) % inbits] << 8)|
				  (in[((inbits) - (msbit >> 3)) % inbits]))
				 >> ((msbit & 7) + 1)) & 0xff;

		/* do the addition */
		byte += out[i % outbits];
		out[i % outbits] = byte & 0xff;

		/* keep around the carry bit, if any */
		byte >>= 8;

	}

	/* if there's a carry bit left over, add it back in */
	if (byte) {
		for (i = outbits - 1; i >= 0; i--) {
			/* do the addition */
			byte += out[i];
			out[i] = byte & 0xff;

			/* keep around the carry bit, if any */
			byte >>= 8;
		}
	}
}

/*
 * This is the DK (derive_key) function as described in rfc3961, sec 5.1
 * Taken from MIT Kerberos and modified.
 */

u32 krb5_derive_key(const struct gss_krb5_enctype *gk5e,
		    const struct xdr_netobj *inkey,
		    struct xdr_netobj *outkey,
		    const struct xdr_netobj *in_constant,
		    gfp_t gfp_mask)
{
	size_t blocksize, keybytes, keylength, n;
	unsigned char *inblockdata, *outblockdata, *rawkey;
	struct xdr_netobj inblock, outblock;
	struct crypto_skcipher *cipher;
	u32 ret = EINVAL;

	blocksize = gk5e->blocksize;
	keybytes = gk5e->keybytes;
	keylength = gk5e->keylength;

	if ((inkey->len != keylength) || (outkey->len != keylength))
		goto err_return;

	cipher = crypto_alloc_skcipher(gk5e->encrypt_name, 0,
				       CRYPTO_ALG_ASYNC);
	if (IS_ERR(cipher))
		goto err_return;
	if (crypto_skcipher_setkey(cipher, inkey->data, inkey->len))
		goto err_return;

	/* allocate and set up buffers */

	ret = ENOMEM;
	inblockdata = kmalloc(blocksize, gfp_mask);
	if (inblockdata == NULL)
		goto err_free_cipher;

	outblockdata = kmalloc(blocksize, gfp_mask);
	if (outblockdata == NULL)
		goto err_free_in;

	rawkey = kmalloc(keybytes, gfp_mask);
	if (rawkey == NULL)
		goto err_free_out;

	inblock.data = (char *) inblockdata;
	inblock.len = blocksize;

	outblock.data = (char *) outblockdata;
	outblock.len = blocksize;

	/* initialize the input block */

	if (in_constant->len == inblock.len) {
		memcpy(inblock.data, in_constant->data, inblock.len);
	} else {
		krb5_nfold(in_constant->len * 8, in_constant->data,
			   inblock.len * 8, inblock.data);
	}

	/* loop encrypting the blocks until enough key bytes are generated */

	n = 0;
	while (n < keybytes) {
		(*(gk5e->encrypt))(cipher, NULL, inblock.data,
				   outblock.data, inblock.len);

		if ((keybytes - n) <= outblock.len) {
			memcpy(rawkey + n, outblock.data, (keybytes - n));
			break;
		}

		memcpy(rawkey + n, outblock.data, outblock.len);
		memcpy(inblock.data, outblock.data, outblock.len);
		n += outblock.len;
	}

	/* postprocess the key */

	inblock.data = (char *) rawkey;
	inblock.len = keybytes;

	BUG_ON(gk5e->mk_key == NULL);
	ret = (*(gk5e->mk_key))(gk5e, &inblock, outkey);
	if (ret) {
		dprintk("%s: got %d from mk_key function for '%s'\n",
			__func__, ret, gk5e->encrypt_name);
		goto err_free_raw;
	}

	/* clean memory, free resources and exit */

	ret = 0;

err_free_raw:
	memset(rawkey, 0, keybytes);
	kfree(rawkey);
err_free_out:
	memset(outblockdata, 0, blocksize);
	kfree(outblockdata);
err_free_in:
	memset(inblockdata, 0, blocksize);
	kfree(inblockdata);
err_free_cipher:
	crypto_free_skcipher(cipher);
err_return:
	return ret;
}

#define smask(step) ((1<<step)-1)
#define pstep(x, step) (((x)&smask(step))^(((x)>>step)&smask(step)))
#define parity_char(x) pstep(pstep(pstep((x), 4), 2), 1)

static void mit_des_fixup_key_parity(u8 key[8])
{
	int i;
	for (i = 0; i < 8; i++) {
		key[i] &= 0xfe;
		key[i] |= 1^parity_char(key[i]);
	}
}

/*
 * This is the des3 key derivation postprocess function
 */
u32 gss_krb5_des3_make_key(const struct gss_krb5_enctype *gk5e,
			   struct xdr_netobj *randombits,
			   struct xdr_netobj *key)
{
	int i;
	u32 ret = EINVAL;

	if (key->len != 24) {
		dprintk("%s: key->len is %d\n", __func__, key->len);
		goto err_out;
	}
	if (randombits->len != 21) {
		dprintk("%s: randombits->len is %d\n",
			__func__, randombits->len);
		goto err_out;
	}

	/* take the seven bytes, move them around into the top 7 bits of the
	   8 key bytes, then compute the parity bits.  Do this three times. */

	for (i = 0; i < 3; i++) {
		memcpy(key->data + i*8, randombits->data + i*7, 7);
		key->data[i*8+7] = (((key->data[i*8]&1)<<1) |
				    ((key->data[i*8+1]&1)<<2) |
				    ((key->data[i*8+2]&1)<<3) |
				    ((key->data[i*8+3]&1)<<4) |
				    ((key->data[i*8+4]&1)<<5) |
				    ((key->data[i*8+5]&1)<<6) |
				    ((key->data[i*8+6]&1)<<7));

		mit_des_fixup_key_parity(key->data + i*8);
	}
	ret = 0;
err_out:
	return ret;
}

/*
 * This is the aes key derivation postprocess function
 */
u32 gss_krb5_aes_make_key(const struct gss_krb5_enctype *gk5e,
			  struct xdr_netobj *randombits,
			  struct xdr_netobj *key)
{
	u32 ret = EINVAL;

	if (key->len != 16 && key->len != 32) {
		dprintk("%s: key->len is %d\n", __func__, key->len);
		goto err_out;
	}
	if (randombits->len != 16 && randombits->len != 32) {
		dprintk("%s: randombits->len is %d\n",
			__func__, randombits->len);
		goto err_out;
	}
	if (randombits->len != key->len) {
		dprintk("%s: randombits->len is %d, key->len is %d\n",
			__func__, randombits->len, key->len);
		goto err_out;
	}
	memcpy(key->data, randombits->data, key->len);
	ret = 0;
err_out:
	return ret;
}

