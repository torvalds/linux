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

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/gss_krb5.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

u32
krb5_encrypt(
	struct crypto_tfm *tfm,
	void * iv,
	void * in,
	void * out,
	int length)
{
	u32 ret = -EINVAL;
        struct scatterlist sg[1];
	u8 local_iv[16] = {0};

	dprintk("RPC:      krb5_encrypt: input data:\n");
	print_hexl((u32 *)in, length, 0);

	if (length % crypto_tfm_alg_blocksize(tfm) != 0)
		goto out;

	if (crypto_tfm_alg_ivsize(tfm) > 16) {
		dprintk("RPC:      gss_k5encrypt: tfm iv size to large %d\n",
		         crypto_tfm_alg_ivsize(tfm));
		goto out;
	}

	if (iv)
		memcpy(local_iv, iv, crypto_tfm_alg_ivsize(tfm));

	memcpy(out, in, length);
	sg[0].page = virt_to_page(out);
	sg[0].offset = offset_in_page(out);
	sg[0].length = length;

	ret = crypto_cipher_encrypt_iv(tfm, sg, sg, length, local_iv);

	dprintk("RPC:      krb5_encrypt: output data:\n");
	print_hexl((u32 *)out, length, 0);
out:
	dprintk("RPC:      krb5_encrypt returns %d\n",ret);
	return(ret);
}

EXPORT_SYMBOL(krb5_encrypt);

u32
krb5_decrypt(
     struct crypto_tfm *tfm,
     void * iv,
     void * in,
     void * out,
     int length)
{
	u32 ret = -EINVAL;
	struct scatterlist sg[1];
	u8 local_iv[16] = {0};

	dprintk("RPC:      krb5_decrypt: input data:\n");
	print_hexl((u32 *)in, length, 0);

	if (length % crypto_tfm_alg_blocksize(tfm) != 0)
		goto out;

	if (crypto_tfm_alg_ivsize(tfm) > 16) {
		dprintk("RPC:      gss_k5decrypt: tfm iv size to large %d\n",
			crypto_tfm_alg_ivsize(tfm));
		goto out;
	}
	if (iv)
		memcpy(local_iv,iv, crypto_tfm_alg_ivsize(tfm));

	memcpy(out, in, length);
	sg[0].page = virt_to_page(out);
	sg[0].offset = offset_in_page(out);
	sg[0].length = length;

	ret = crypto_cipher_decrypt_iv(tfm, sg, sg, length, local_iv);

	dprintk("RPC:      krb5_decrypt: output_data:\n");
	print_hexl((u32 *)out, length, 0);
out:
	dprintk("RPC:      gss_k5decrypt returns %d\n",ret);
	return(ret);
}

EXPORT_SYMBOL(krb5_decrypt);

static void
buf_to_sg(struct scatterlist *sg, char *ptr, int len) {
	sg->page = virt_to_page(ptr);
	sg->offset = offset_in_page(ptr);
	sg->length = len;
}

static int
process_xdr_buf(struct xdr_buf *buf, int offset, int len,
		int (*actor)(struct scatterlist *, void *), void *data)
{
	int i, page_len, thislen, page_offset, ret = 0;
	struct scatterlist	sg[1];

	if (offset >= buf->head[0].iov_len) {
		offset -= buf->head[0].iov_len;
	} else {
		thislen = buf->head[0].iov_len - offset;
		if (thislen > len)
			thislen = len;
		buf_to_sg(sg, buf->head[0].iov_base + offset, thislen);
		ret = actor(sg, data);
		if (ret)
			goto out;
		offset = 0;
		len -= thislen;
	}
	if (len == 0)
		goto out;

	if (offset >= buf->page_len) {
		offset -= buf->page_len;
	} else {
		page_len = buf->page_len - offset;
		if (page_len > len)
			page_len = len;
		len -= page_len;
		page_offset = (offset + buf->page_base) & (PAGE_CACHE_SIZE - 1);
		i = (offset + buf->page_base) >> PAGE_CACHE_SHIFT;
		thislen = PAGE_CACHE_SIZE - page_offset;
		do {
			if (thislen > page_len)
				thislen = page_len;
			sg->page = buf->pages[i];
			sg->offset = page_offset;
			sg->length = thislen;
			ret = actor(sg, data);
			if (ret)
				goto out;
			page_len -= thislen;
			i++;
			page_offset = 0;
			thislen = PAGE_CACHE_SIZE;
		} while (page_len != 0);
		offset = 0;
	}
	if (len == 0)
		goto out;

	if (offset < buf->tail[0].iov_len) {
		thislen = buf->tail[0].iov_len - offset;
		if (thislen > len)
			thislen = len;
		buf_to_sg(sg, buf->tail[0].iov_base + offset, thislen);
		ret = actor(sg, data);
		len -= thislen;
	}
	if (len != 0)
		ret = -EINVAL;
out:
	return ret;
}

static int
checksummer(struct scatterlist *sg, void *data)
{
	struct crypto_tfm *tfm = (struct crypto_tfm *)data;

	crypto_digest_update(tfm, sg, 1);

	return 0;
}

/* checksum the plaintext data and hdrlen bytes of the token header */
s32
make_checksum(s32 cksumtype, char *header, int hdrlen, struct xdr_buf *body,
		   int body_offset, struct xdr_netobj *cksum)
{
	char                            *cksumname;
	struct crypto_tfm               *tfm = NULL; /* XXX add to ctx? */
	struct scatterlist              sg[1];
	u32                             code = GSS_S_FAILURE;

	switch (cksumtype) {
		case CKSUMTYPE_RSA_MD5:
			cksumname = "md5";
			break;
		default:
			dprintk("RPC:      krb5_make_checksum:"
				" unsupported checksum %d", cksumtype);
			goto out;
	}
	if (!(tfm = crypto_alloc_tfm(cksumname, CRYPTO_TFM_REQ_MAY_SLEEP)))
		goto out;
	cksum->len = crypto_tfm_alg_digestsize(tfm);
	if ((cksum->data = kmalloc(cksum->len, GFP_KERNEL)) == NULL)
		goto out;

	crypto_digest_init(tfm);
	buf_to_sg(sg, header, hdrlen);
	crypto_digest_update(tfm, sg, 1);
	process_xdr_buf(body, body_offset, body->len - body_offset,
			checksummer, tfm);
	crypto_digest_final(tfm, cksum->data);
	code = 0;
out:
	crypto_free_tfm(tfm);
	return code;
}

EXPORT_SYMBOL(make_checksum);

struct encryptor_desc {
	u8 iv[8]; /* XXX hard-coded blocksize */
	struct crypto_tfm *tfm;
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
		in_page = sg->page;
	}
	desc->infrags[desc->fragno].page = in_page;
	desc->fragno++;
	desc->fraglen += sg->length;
	desc->pos += sg->length;

	fraglen = thislen & 7; /* XXX hardcoded blocksize */
	thislen -= fraglen;

	if (thislen == 0)
		return 0;

	ret = crypto_cipher_encrypt_iv(desc->tfm, desc->outfrags, desc->infrags,
					thislen, desc->iv);
	if (ret)
		return ret;
	if (fraglen) {
		desc->outfrags[0].page = sg->page;
		desc->outfrags[0].offset = sg->offset + sg->length - fraglen;
		desc->outfrags[0].length = fraglen;
		desc->infrags[0] = desc->outfrags[0];
		desc->infrags[0].page = in_page;
		desc->fragno = 1;
		desc->fraglen = fraglen;
	} else {
		desc->fragno = 0;
		desc->fraglen = 0;
	}
	return 0;
}

int
gss_encrypt_xdr_buf(struct crypto_tfm *tfm, struct xdr_buf *buf, int offset,
		struct page **pages)
{
	int ret;
	struct encryptor_desc desc;

	BUG_ON((buf->len - offset) % crypto_tfm_alg_blocksize(tfm) != 0);

	memset(desc.iv, 0, sizeof(desc.iv));
	desc.tfm = tfm;
	desc.pos = offset;
	desc.outbuf = buf;
	desc.pages = pages;
	desc.fragno = 0;
	desc.fraglen = 0;

	ret = process_xdr_buf(buf, offset, buf->len - offset, encryptor, &desc);
	return ret;
}

EXPORT_SYMBOL(gss_encrypt_xdr_buf);

struct decryptor_desc {
	u8 iv[8]; /* XXX hard-coded blocksize */
	struct crypto_tfm *tfm;
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

	ret = crypto_cipher_decrypt_iv(desc->tfm, desc->frags, desc->frags,
					thislen, desc->iv);
	if (ret)
		return ret;
	if (fraglen) {
		desc->frags[0].page = sg->page;
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
gss_decrypt_xdr_buf(struct crypto_tfm *tfm, struct xdr_buf *buf, int offset)
{
	struct decryptor_desc desc;

	/* XXXJBF: */
	BUG_ON((buf->len - offset) % crypto_tfm_alg_blocksize(tfm) != 0);

	memset(desc.iv, 0, sizeof(desc.iv));
	desc.tfm = tfm;
	desc.fragno = 0;
	desc.fraglen = 0;
	return process_xdr_buf(buf, offset, buf->len - offset, decryptor, &desc);
}

EXPORT_SYMBOL(gss_decrypt_xdr_buf);
