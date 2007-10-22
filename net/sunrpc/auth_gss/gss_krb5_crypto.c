/*
 *  linux/net/sunrpc/gss_krb5_crypto.c
 *
 *  Copyright (c) 2000 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson   <andros@umich.edu>
 *  Bruce Fields   <bfields@umich.edu>
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
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/sunrpc/xdr.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

u32
krb5_encrypt(
	struct crypto_blkcipher *tfm,
	void * iv,
	void * in,
	void * out,
	int length)
{
	u32 ret = -EINVAL;
	struct scatterlist sg[1];
	u8 local_iv[16] = {0};
	struct blkcipher_desc desc = { .tfm = tfm, .info = local_iv };

	if (length % crypto_blkcipher_blocksize(tfm) != 0)
		goto out;

	if (crypto_blkcipher_ivsize(tfm) > 16) {
		dprintk("RPC:       gss_k5encrypt: tfm iv size to large %d\n",
			 crypto_blkcipher_ivsize(tfm));
		goto out;
	}

	if (iv)
		memcpy(local_iv, iv, crypto_blkcipher_ivsize(tfm));

	memcpy(out, in, length);
	sg_set_buf(sg, out, length);

	ret = crypto_blkcipher_encrypt_iv(&desc, sg, sg, length);
out:
	dprintk("RPC:       krb5_encrypt returns %d\n", ret);
	return ret;
}

EXPORT_SYMBOL(krb5_encrypt);

u32
krb5_decrypt(
     struct crypto_blkcipher *tfm,
     void * iv,
     void * in,
     void * out,
     int length)
{
	u32 ret = -EINVAL;
	struct scatterlist sg[1];
	u8 local_iv[16] = {0};
	struct blkcipher_desc desc = { .tfm = tfm, .info = local_iv };

	if (length % crypto_blkcipher_blocksize(tfm) != 0)
		goto out;

	if (crypto_blkcipher_ivsize(tfm) > 16) {
		dprintk("RPC:       gss_k5decrypt: tfm iv size to large %d\n",
			crypto_blkcipher_ivsize(tfm));
		goto out;
	}
	if (iv)
		memcpy(local_iv,iv, crypto_blkcipher_ivsize(tfm));

	memcpy(out, in, length);
	sg_set_buf(sg, out, length);

	ret = crypto_blkcipher_decrypt_iv(&desc, sg, sg, length);
out:
	dprintk("RPC:       gss_k5decrypt returns %d\n",ret);
	return ret;
}

EXPORT_SYMBOL(krb5_decrypt);

static int
checksummer(struct scatterlist *sg, void *data)
{
	struct hash_desc *desc = data;

	return crypto_hash_update(desc, sg, sg->length);
}

/* checksum the plaintext data and hdrlen bytes of the token header */
s32
make_checksum(char *cksumname, char *header, int hdrlen, struct xdr_buf *body,
		   int body_offset, struct xdr_netobj *cksum)
{
	struct hash_desc                desc; /* XXX add to ctx? */
	struct scatterlist              sg[1];
	int err;

	desc.tfm = crypto_alloc_hash(cksumname, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(desc.tfm))
		return GSS_S_FAILURE;
	cksum->len = crypto_hash_digestsize(desc.tfm);
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	err = crypto_hash_init(&desc);
	if (err)
		goto out;
	sg_set_buf(sg, header, hdrlen);
	err = crypto_hash_update(&desc, sg, hdrlen);
	if (err)
		goto out;
	err = xdr_process_buf(body, body_offset, body->len - body_offset,
			      checksummer, &desc);
	if (err)
		goto out;
	err = crypto_hash_final(&desc, cksum->data);

out:
	crypto_free_hash(desc.tfm);
	return err ? GSS_S_FAILURE : 0;
}

EXPORT_SYMBOL(make_checksum);

struct encryptor_desc {
	u8 iv[8]; /* XXX hard-coded blocksize */
	struct blkcipher_desc desc;
	int pos;
	struct xdr_buf *outbuf;
	struct page **pages;
	struct scatterlist infrags[4];
	struct scatterlist outfrags[4];
	int fragno;
	int fraglen;
};

static int
encryptor(struct scatterlist *sg, void *data)
{
	struct encryptor_desc *desc = data;
	struct xdr_buf *outbuf = desc->outbuf;
	struct page *in_page;
	int thislen = desc->fraglen + sg->length;
	int fraglen, ret;
	int page_pos;

	/* Worst case is 4 fragments: head, end of page 1, start
	 * of page 2, tail.  Anything more is a bug. */
	BUG_ON(desc->fragno > 3);
	desc->infrags[desc->fragno] = *sg;
	desc->outfrags[desc->fragno] = *sg;

	page_pos = desc->pos - outbuf->head[0].iov_len;
	if (page_pos >= 0 && page_pos < outbuf->page_len) {
		/* pages are not in place: */
		int i = (page_pos + outbuf->page_base) >> PAGE_CACHE_SHIFT;
		in_page = desc->pages[i];
	} else {
		in_page = sg_page(sg);
	}
	sg_set_page(&desc->infrags[desc->fragno], in_page);
	desc->fragno++;
	desc->fraglen += sg->length;
	desc->pos += sg->length;

	fraglen = thislen & 7; /* XXX hardcoded blocksize */
	thislen -= fraglen;

	if (thislen == 0)
		return 0;

	ret = crypto_blkcipher_encrypt_iv(&desc->desc, desc->outfrags,
					  desc->infrags, thislen);
	if (ret)
		return ret;
	if (fraglen) {
		sg_set_page(&desc->outfrags[0], sg_page(sg));
		desc->outfrags[0].offset = sg->offset + sg->length - fraglen;
		desc->outfrags[0].length = fraglen;
		desc->infrags[0] = desc->outfrags[0];
		sg_set_page(&desc->infrags[0], in_page);
		desc->fragno = 1;
		desc->fraglen = fraglen;
	} else {
		desc->fragno = 0;
		desc->fraglen = 0;
	}
	return 0;
}

int
gss_encrypt_xdr_buf(struct crypto_blkcipher *tfm, struct xdr_buf *buf,
		    int offset, struct page **pages)
{
	int ret;
	struct encryptor_desc desc;

	BUG_ON((buf->len - offset) % crypto_blkcipher_blocksize(tfm) != 0);

	memset(desc.iv, 0, sizeof(desc.iv));
	desc.desc.tfm = tfm;
	desc.desc.info = desc.iv;
	desc.desc.flags = 0;
	desc.pos = offset;
	desc.outbuf = buf;
	desc.pages = pages;
	desc.fragno = 0;
	desc.fraglen = 0;

	ret = xdr_process_buf(buf, offset, buf->len - offset, encryptor, &desc);
	return ret;
}

EXPORT_SYMBOL(gss_encrypt_xdr_buf);

struct decryptor_desc {
	u8 iv[8]; /* XXX hard-coded blocksize */
	struct blkcipher_desc desc;
	struct scatterlist frags[4];
	int fragno;
	int fraglen;
};

static int
decryptor(struct scatterlist *sg, void *data)
{
	struct decryptor_desc *desc = data;
	int thislen = desc->fraglen + sg->length;
	int fraglen, ret;

	/* Worst case is 4 fragments: head, end of page 1, start
	 * of page 2, tail.  Anything more is a bug. */
	BUG_ON(desc->fragno > 3);
	desc->frags[desc->fragno] = *sg;
	desc->fragno++;
	desc->fraglen += sg->length;

	fraglen = thislen & 7; /* XXX hardcoded blocksize */
	thislen -= fraglen;

	if (thislen == 0)
		return 0;

	ret = crypto_blkcipher_decrypt_iv(&desc->desc, desc->frags,
					  desc->frags, thislen);
	if (ret)
		return ret;
	if (fraglen) {
		sg_set_page(&desc->frags[0], sg_page(sg));
		desc->frags[0].offset = sg->offset + sg->length - fraglen;
		desc->frags[0].length = fraglen;
		desc->fragno = 1;
		desc->fraglen = fraglen;
	} else {
		desc->fragno = 0;
		desc->fraglen = 0;
	}
	return 0;
}

int
gss_decrypt_xdr_buf(struct crypto_blkcipher *tfm, struct xdr_buf *buf,
		    int offset)
{
	struct decryptor_desc desc;

	/* XXXJBF: */
	BUG_ON((buf->len - offset) % crypto_blkcipher_blocksize(tfm) != 0);

	memset(desc.iv, 0, sizeof(desc.iv));
	desc.desc.tfm = tfm;
	desc.desc.info = desc.iv;
	desc.desc.flags = 0;
	desc.fragno = 0;
	desc.fraglen = 0;
	return xdr_process_buf(buf, offset, buf->len - offset, decryptor, &desc);
}

EXPORT_SYMBOL(gss_decrypt_xdr_buf);
