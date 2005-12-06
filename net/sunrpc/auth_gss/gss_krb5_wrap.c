#include <linux/types.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/random.h>
#include <linux/pagemap.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

static inline int
gss_krb5_padding(int blocksize, int length)
{
	/* Most of the code is block-size independent but currently we
	 * use only 8: */
	BUG_ON(blocksize != 8);
	return 8 - (length & 7);
}

static inline void
gss_krb5_add_padding(struct xdr_buf *buf, int offset, int blocksize)
{
	int padding = gss_krb5_padding(blocksize, buf->len - offset);
	char *p;
	struct kvec *iov;

	if (buf->page_len || buf->tail[0].iov_len)
		iov = &buf->tail[0];
	else
		iov = &buf->head[0];
	p = iov->iov_base + iov->iov_len;
	iov->iov_len += padding;
	buf->len += padding;
	memset(p, padding, padding);
}

static inline int
gss_krb5_remove_padding(struct xdr_buf *buf, int blocksize)
{
	u8 *ptr;
	u8 pad;
	int len = buf->len;

	if (len <= buf->head[0].iov_len) {
		pad = *(u8 *)(buf->head[0].iov_base + len - 1);
		if (pad > buf->head[0].iov_len)
			return -EINVAL;
		buf->head[0].iov_len -= pad;
		goto out;
	} else
		len -= buf->head[0].iov_len;
	if (len <= buf->page_len) {
		int last = (buf->page_base + len - 1)
					>>PAGE_CACHE_SHIFT;
		int offset = (buf->page_base + len - 1)
					& (PAGE_CACHE_SIZE - 1);
		ptr = kmap_atomic(buf->pages[last], KM_SKB_SUNRPC_DATA);
		pad = *(ptr + offset);
		kunmap_atomic(ptr, KM_SKB_SUNRPC_DATA);
		goto out;
	} else
		len -= buf->page_len;
	BUG_ON(len > buf->tail[0].iov_len);
	pad = *(u8 *)(buf->tail[0].iov_base + len - 1);
out:
	/* XXX: NOTE: we do not adjust the page lengths--they represent
	 * a range of data in the real filesystem page cache, and we need
	 * to know that range so the xdr code can properly place read data.
	 * However adjusting the head length, as we do above, is harmless.
	 * In the case of a request that fits into a single page, the server
	 * also uses length and head length together to determine the original
	 * start of the request to copy the request for deferal; so it's
	 * easier on the server if we adjust head and tail length in tandem.
	 * It's not really a problem that we don't fool with the page and
	 * tail lengths, though--at worst badly formed xdr might lead the
	 * server to attempt to parse the padding.
	 * XXX: Document all these weird requirements for gss mechanism
	 * wrap/unwrap functions. */
	if (pad > blocksize)
		return -EINVAL;
	if (buf->len > pad)
		buf->len -= pad;
	else
		return -EINVAL;
	return 0;
}

static inline void
make_confounder(char *p, int blocksize)
{
	static u64 i = 0;
	u64 *q = (u64 *)p;

	/* rfc1964 claims this should be "random".  But all that's really
	 * necessary is that it be unique.  And not even that is necessary in
	 * our case since our "gssapi" implementation exists only to support
	 * rpcsec_gss, so we know that the only buffers we will ever encrypt
	 * already begin with a unique sequence number.  Just to hedge my bets
	 * I'll make a half-hearted attempt at something unique, but ensuring
	 * uniqueness would mean worrying about atomicity and rollover, and I
	 * don't care enough. */

	BUG_ON(blocksize != 8);
	*q = i++;
}

/* Assumptions: the head and tail of inbuf are ours to play with.
 * The pages, however, may be real pages in the page cache and we replace
 * them with scratch pages from **pages before writing to them. */
/* XXX: obviously the above should be documentation of wrap interface,
 * and shouldn't be in this kerberos-specific file. */

/* XXX factor out common code with seal/unseal. */

u32
gss_wrap_kerberos(struct gss_ctx *ctx, int offset,
		struct xdr_buf *buf, struct page **pages)
{
	struct krb5_ctx		*kctx = ctx->internal_ctx_id;
	s32			checksum_type;
	struct xdr_netobj	md5cksum = {.len = 0, .data = NULL};
	int			blocksize = 0, plainlen;
	unsigned char		*ptr, *krb5_hdr, *msg_start;
	s32			now;
	int			headlen;
	struct page		**tmp_pages;

	dprintk("RPC:     gss_wrap_kerberos\n");

	now = get_seconds();

	switch (kctx->signalg) {
		case SGN_ALG_DES_MAC_MD5:
			checksum_type = CKSUMTYPE_RSA_MD5;
			break;
		default:
			dprintk("RPC:      gss_krb5_seal: kctx->signalg %d not"
				" supported\n", kctx->signalg);
			goto out_err;
	}
	if (kctx->sealalg != SEAL_ALG_NONE && kctx->sealalg != SEAL_ALG_DES) {
		dprintk("RPC:      gss_krb5_seal: kctx->sealalg %d not supported\n",
			kctx->sealalg);
		goto out_err;
	}

	blocksize = crypto_tfm_alg_blocksize(kctx->enc);
	gss_krb5_add_padding(buf, offset, blocksize);
	BUG_ON((buf->len - offset) % blocksize);
	plainlen = blocksize + buf->len - offset;

	headlen = g_token_size(&kctx->mech_used, 22 + plainlen) -
						(buf->len - offset);

	ptr = buf->head[0].iov_base + offset;
	/* shift data to make room for header. */
	/* XXX Would be cleverer to encrypt while copying. */
	/* XXX bounds checking, slack, etc. */
	memmove(ptr + headlen, ptr, buf->head[0].iov_len - offset);
	buf->head[0].iov_len += headlen;
	buf->len += headlen;
	BUG_ON((buf->len - offset - headlen) % blocksize);

	g_make_token_header(&kctx->mech_used, 22 + plainlen, &ptr);


	*ptr++ = (unsigned char) ((KG_TOK_WRAP_MSG>>8)&0xff);
	*ptr++ = (unsigned char) (KG_TOK_WRAP_MSG&0xff);

	/* ptr now at byte 2 of header described in rfc 1964, section 1.2.1: */
	krb5_hdr = ptr - 2;
	msg_start = krb5_hdr + 24;
	/* XXXJBF: */ BUG_ON(buf->head[0].iov_base + offset + headlen != msg_start + blocksize);

	*(u16 *)(krb5_hdr + 2) = htons(kctx->signalg);
	memset(krb5_hdr + 4, 0xff, 4);
	*(u16 *)(krb5_hdr + 4) = htons(kctx->sealalg);

	make_confounder(msg_start, blocksize);

	/* XXXJBF: UGH!: */
	tmp_pages = buf->pages;
	buf->pages = pages;
	if (make_checksum(checksum_type, krb5_hdr, 8, buf,
				offset + headlen - blocksize, &md5cksum))
		goto out_err;
	buf->pages = tmp_pages;

	switch (kctx->signalg) {
	case SGN_ALG_DES_MAC_MD5:
		if (krb5_encrypt(kctx->seq, NULL, md5cksum.data,
				  md5cksum.data, md5cksum.len))
			goto out_err;
		memcpy(krb5_hdr + 16,
		       md5cksum.data + md5cksum.len - KRB5_CKSUM_LENGTH,
		       KRB5_CKSUM_LENGTH);

		dprintk("RPC:      make_seal_token: cksum data: \n");
		print_hexl((u32 *) (krb5_hdr + 16), KRB5_CKSUM_LENGTH, 0);
		break;
	default:
		BUG();
	}

	kfree(md5cksum.data);

	/* XXX would probably be more efficient to compute checksum
	 * and encrypt at the same time: */
	if ((krb5_make_seq_num(kctx->seq, kctx->initiate ? 0 : 0xff,
			       kctx->seq_send, krb5_hdr + 16, krb5_hdr + 8)))
		goto out_err;

	if (gss_encrypt_xdr_buf(kctx->enc, buf, offset + headlen - blocksize,
									pages))
		goto out_err;

	kctx->seq_send++;

	return ((kctx->endtime < now) ? GSS_S_CONTEXT_EXPIRED : GSS_S_COMPLETE);
out_err:
	if (md5cksum.data) kfree(md5cksum.data);
	return GSS_S_FAILURE;
}

u32
gss_unwrap_kerberos(struct gss_ctx *ctx, int offset, struct xdr_buf *buf)
{
	struct krb5_ctx		*kctx = ctx->internal_ctx_id;
	int			signalg;
	int			sealalg;
	s32			checksum_type;
	struct xdr_netobj	md5cksum = {.len = 0, .data = NULL};
	s32			now;
	int			direction;
	s32			seqnum;
	unsigned char		*ptr;
	int			bodysize;
	u32			ret = GSS_S_DEFECTIVE_TOKEN;
	void			*data_start, *orig_start;
	int			data_len;
	int			blocksize;

	dprintk("RPC:      gss_unwrap_kerberos\n");

	ptr = (u8 *)buf->head[0].iov_base + offset;
	if (g_verify_token_header(&kctx->mech_used, &bodysize, &ptr,
					buf->len - offset))
		goto out;

	if ((*ptr++ != ((KG_TOK_WRAP_MSG>>8)&0xff)) ||
	    (*ptr++ !=  (KG_TOK_WRAP_MSG    &0xff))   )
		goto out;

	/* XXX sanity-check bodysize?? */

	/* get the sign and seal algorithms */

	signalg = ptr[0] + (ptr[1] << 8);
	sealalg = ptr[2] + (ptr[3] << 8);

	/* Sanity checks */

	if ((ptr[4] != 0xff) || (ptr[5] != 0xff))
		goto out;

	if (sealalg == 0xffff)
		goto out;

	/* in the current spec, there is only one valid seal algorithm per
	   key type, so a simple comparison is ok */

	if (sealalg != kctx->sealalg)
		goto out;

	/* there are several mappings of seal algorithms to sign algorithms,
	   but few enough that we can try them all. */

	if ((kctx->sealalg == SEAL_ALG_NONE && signalg > 1) ||
	    (kctx->sealalg == SEAL_ALG_1 && signalg != SGN_ALG_3) ||
	    (kctx->sealalg == SEAL_ALG_DES3KD &&
	     signalg != SGN_ALG_HMAC_SHA1_DES3_KD))
		goto out;

	if (gss_decrypt_xdr_buf(kctx->enc, buf,
			ptr + 22 - (unsigned char *)buf->head[0].iov_base))
		goto out;

	/* compute the checksum of the message */

	/* initialize the the cksum */
	switch (signalg) {
	case SGN_ALG_DES_MAC_MD5:
		checksum_type = CKSUMTYPE_RSA_MD5;
		break;
	default:
		ret = GSS_S_DEFECTIVE_TOKEN;
		goto out;
	}

	switch (signalg) {
	case SGN_ALG_DES_MAC_MD5:
		ret = make_checksum(checksum_type, ptr - 2, 8, buf,
			 ptr + 22 - (unsigned char *)buf->head[0].iov_base, &md5cksum);
		if (ret)
			goto out;

		ret = krb5_encrypt(kctx->seq, NULL, md5cksum.data,
				   md5cksum.data, md5cksum.len);
		if (ret)
			goto out;

		if (memcmp(md5cksum.data + 8, ptr + 14, 8)) {
			ret = GSS_S_BAD_SIG;
			goto out;
		}
		break;
	default:
		ret = GSS_S_DEFECTIVE_TOKEN;
		goto out;
	}

	/* it got through unscathed.  Make sure the context is unexpired */

	now = get_seconds();

	ret = GSS_S_CONTEXT_EXPIRED;
	if (now > kctx->endtime)
		goto out;

	/* do sequencing checks */

	ret = GSS_S_BAD_SIG;
	if ((ret = krb5_get_seq_num(kctx->seq, ptr + 14, ptr + 6, &direction,
				    &seqnum)))
		goto out;

	if ((kctx->initiate && direction != 0xff) ||
	    (!kctx->initiate && direction != 0))
		goto out;

	/* Copy the data back to the right position.  XXX: Would probably be
	 * better to copy and encrypt at the same time. */

	blocksize = crypto_tfm_alg_blocksize(kctx->enc);
	data_start = ptr + 22 + blocksize;
	orig_start = buf->head[0].iov_base + offset;
	data_len = (buf->head[0].iov_base + buf->head[0].iov_len) - data_start;
	memmove(orig_start, data_start, data_len);
	buf->head[0].iov_len -= (data_start - orig_start);
	buf->len -= (data_start - orig_start);

	ret = GSS_S_DEFECTIVE_TOKEN;
	if (gss_krb5_remove_padding(buf, blocksize))
		goto out;

	ret = GSS_S_COMPLETE;
out:
	if (md5cksum.data) kfree(md5cksum.data);
	return ret;
}
