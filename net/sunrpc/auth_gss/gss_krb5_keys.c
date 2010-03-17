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

#include <linux/err.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/sunrpc/xdr.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

/*
 * This is the n-fold function as described in rfc3961, sec 5.1
 * Taken from MIT Kerberos and modified.
 */

static void krb5_nfold(u32 inbits, const u8 *in,
		       u32 outbits, u8 *out)
{
	int a, b, c, lcm;
	int byte, i, msbit;

	/* the code below is more readable if I make these bytes
	   instead of bits */

	inbits >>= 3;
	outbits >>= 3;

	/* first compute lcm(n,k) */

	a = outbits;
	b = inbits;

	while (b != 0) {
		c = b;
		b = a%b;
		a = c;
	}

	lcm = outbits*inbits/a;

	/* now do the real work */

	memset(out, 0, outbits);
	byte = 0;

	/* this will end up cycling through k lcm(k,n)/k times, which
	   is correct */
	for (i = lcm-1; i >= 0; i--) {
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

u32 krb5_derive_key(struct gss_krb5_enctype *gk5e,
		    const struct xdr_netobj *inkey,
		    struct xdr_netobj *outkey,
		    const struct xdr_netobj *in_constant)
{
	size_t blocksize, keybytes, keylength, n;
	unsigned char *inblockdata, *outblockdata, *rawkey;
	struct xdr_netobj inblock, outblock;
	struct crypto_blkcipher *cipher;
	u32 ret = EINVAL;

	blocksize = gk5e->blocksize;
	keybytes = gk5e->keybytes;
	keylength = gk5e->keylength;

	if ((inkey->len != keylength) || (outkey->len != keylength))
		goto err_return;

	cipher = crypto_alloc_blkcipher(gk5e->encrypt_name, 0,
					CRYPTO_ALG_ASYNC);
	if (IS_ERR(cipher))
		goto err_return;
	if (crypto_blkcipher_setkey(cipher, inkey->data, inkey->len))
		goto err_return;

	/* allocate and set up buffers */

	ret = ENOMEM;
	inblockdata = kmalloc(blocksize, GFP_KERNEL);
	if (inblockdata == NULL)
		goto err_free_cipher;

	outblockdata = kmalloc(blocksize, GFP_KERNEL);
	if (outblockdata == NULL)
		goto err_free_in;

	rawkey = kmalloc(keybytes, GFP_KERNEL);
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
	crypto_free_blkcipher(cipher);
err_return:
	return ret;
}
