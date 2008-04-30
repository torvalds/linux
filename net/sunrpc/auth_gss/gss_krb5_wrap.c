#include <linux/types.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/random.h>
#include <linux/pagemap.h>
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
	size_t len = buf->len;

	if (len <= buf->head[0].iov_len) {
		pad = *(u8 *)(buf->head[0].iov_base + len - 1);
		if (pad > buf->head[0].iov_len)
			return -EINVAL;
		buf->head[0].iov_len -= pad;
		goto out;
	} else
		len -= buf->head[0].iov_len;
	if (len <= buf->page_len) {
		unsigned int last = (buf->page_base + len - 1)
					>>PAGE_CACHE_SHIFT;
		unsigned int offset = (buf->page_base + len - 1)
					& (PAGE_CACHE_SIZE - 1);
		ptr = kmap_atomic(buf->pages[last], KM_USER0);
		pad = *(ptr + offset);
		kunmap_atomic(ptr, KM_USER0);
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

static void
make_confounder(char *p, u32 conflen)
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

	/* initialize to random value */
	if (i == 0) {
		i = random32();
		i = (i << 32) | random32();
	}

	switch (conflen) {
	case 16:
		*q++ = i++;
		/* fall through */
	case 8:
		*q++ = i++;
		break;
	default:
		BUG();
	}
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
	char			cksumdata[16];
	struct xdr_netobj	md5cksum = {.len = 0, .data = cksumdata};
	int			blocksize = 0, plainlen;
	unsigned char		*ptr, *msg_start;
	s32			now;
	int			headlen;
	struct page		**tmp_pages;
	u32			seq_send;

	dprintk("RPC:       gss_wrap_kerberos\n");

	now = get_seconds();

	blocksize = crypto_blkcipher_blocksize(kctx->enc);
	gss_krb5_add_padding(buf, offset, blocksize);
	BUG_ON((buf->len - offset) % blocksize);
	plainlen = blocksize + buf->len - offset;

	headlen = g_token_size(&kctx->mech_used, 24 + plainlen) -
						(buf->len - offset);

	ptr = buf->head[0].iov_base + offset;
	/* shift data to make room for header. */
	/* XXX Would be cleverer to encrypt while copying. */
	/* XXX bounds checking, slack, etc. */
	memmove(ptr + headlen, ptr, buf->head[0].iov_len - offset);
	buf->head[0].iov_len += headlen;
	buf->len += headlen;
	BUG_ON((buf->len - offset - headlen) % blocksize);

	g_make_token_header(&kctx->mech_used,
				GSS_KRB5_TOK_HDR_LEN + 8 + plainlen, &ptr);


	/* ptr now at header described in rfc 1964, section 1.2.1: */
	ptr[0] = (unsigned char) ((KG_TOK_WRAP_MSG >> 8) & 0xff);
	ptr[1] = (unsigned char) (KG_TOK_WRAP_MSG & 0xff);

	msg_start = ptr + 24;

	*(__be16 *)(ptr + 2) = htons(SGN_ALG_DES_MAC_MD5);
	memset(ptr + 4, 0xff, 4);
	*(__be16 *)(ptr + 4) = htons(SEAL_ALG_DES);

	make_confounder(msg_start, blocksize);

	/* XXXJBF: UGH!: */
	tmp_pages = buf->pages;
	buf->pages = pages;
	if (make_checksum("md5", ptr, 8, buf,
				offset + headlen - blocksize, &md5cksum))
		return GSS_S_FAILURE;
	buf->pages = tmp_pages;

	if (krb5_encrypt(kctx->seq, NULL, md5cksum.data,
			  md5cksum.data, md5cksum.len))
		return GSS_S_FAILURE;
	memcpy(ptr + GSS_KRB5_TOK_HDR_LEN, md5cksum.data + md5cksum.len - 8, 8);

	spin_lock(&krb5_seq_lock);
	seq_send = kctx->seq_send++;
	spin_unlock(&krb5_seq_lock);

	/* XXX would probably be more efficient to compute checksum
	 * and encrypt at the same time: */
	if ((krb5_make_seq_num(kctx->seq, kctx->initiate ? 0 : 0xff,
			       seq_send, ptr + GSS_KRB5_TOK_HDR_LEN, ptr + 8)))
		return GSS_S_FAILURE;

	if (gss_encrypt_xdr_buf(kctx->enc, buf, offset + headlen - blocksize,
									pages))
		return GSS_S_FAILURE;

	return (kctx->endtime < now) ? GSS_S_CONTEXT_EXPIRED : GSS_S_COMPLETE;
}

u32
gss_unwrap_kerberos(struct gss_ctx *ctx, int offset, struct xdr_buf *buf)
{
	struct krb5_ctx		*kctx = ctx->internal_ctx_id;
	int			signalg;
	int			sealalg;
	char			cksumdata[16];
	struct xdr_netobj	md5cksum = {.len = 0, .data = cksumdata};
	s32			now;
	int			direction;
	s32			seqnum;
	unsigned char		*ptr;
	int			bodysize;
	void			*data_start, *orig_start;
	int			data_len;
	int			blocksize;

	dprintk("RPC:       gss_unwrap_kerberos\n");

	ptr = (u8 *)buf->head[0].iov_base + offset;
	if (g_verify_token_header(&kctx->mech_used, &bodysize, &ptr,
					buf->len - offset))
		return GSS_S_DEFECTIVE_TOKEN;

	if ((ptr[0] != ((KG_TOK_WRAP_MSG >> 8) & 0xff)) ||
	    (ptr[1] !=  (KG_TOK_WRAP_MSG & 0xff)))
		return GSS_S_DEFECTIVE_TOKEN;

	/* XXX sanity-check bodysize?? */

	/* get the sign and seal algorithms */

	signalg = ptr[2] + (ptr[3] << 8);
	if (signalg != SGN_ALG_DES_MAC_MD5)
		return GSS_S_DEFECTIVE_TOKEN;

	sealalg = ptr[4] + (ptr[5] << 8);
	if (sealalg != SEAL_ALG_DES)
		return GSS_S_DEFECTIVE_TOKEN;

	if ((ptr[6] != 0xff) || (ptr[7] != 0xff))
		return GSS_S_DEFECTIVE_TOKEN;

	if (gss_decrypt_xdr_buf(kctx->enc, buf,
			ptr + GSS_KRB5_TOK_HDR_LEN + 8 - (unsigned char *)buf->head[0].iov_base))
		return GSS_S_DEFECTIVE_TOKEN;

	if (make_checksum("md5", ptr, 8, buf,
		 ptr + GSS_KRB5_TOK_HDR_LEN + 8 - (unsigned char *)buf->head[0].iov_base, &md5cksum))
		return GSS_S_FAILURE;

	if (krb5_encrypt(kctx->seq, NULL, md5cksum.data,
			   md5cksum.data, md5cksum.len))
		return GSS_S_FAILURE;

	if (memcmp(md5cksum.data + 8, ptr + GSS_KRB5_TOK_HDR_LEN, 8))
		return GSS_S_BAD_SIG;

	/* it got through unscathed.  Make sure the context is unexpired */

	now = get_seconds();

	if (now > kctx->endtime)
		return GSS_S_CONTEXT_EXPIRED;

	/* do sequencing checks */

	if (krb5_get_seq_num(kctx->seq, ptr + GSS_KRB5_TOK_HDR_LEN, ptr + 8,
				    &direction, &seqnum))
		return GSS_S_BAD_SIG;

	if ((kctx->initiate && direction != 0xff) ||
	    (!kctx->initiate && direction != 0))
		return GSS_S_BAD_SIG;

	/* Copy the data back to the right position.  XXX: Would probably be
	 * better to copy and encrypt at the same time. */

	blocksize = crypto_blkcipher_blocksize(kctx->enc);
	data_start = ptr + GSS_KRB5_TOK_HDR_LEN + 8 + blocksize;
	orig_start = buf->head[0].iov_base + offset;
	data_len = (buf->head[0].iov_base + buf->head[0].iov_len) - data_start;
	memmove(orig_start, data_start, data_len);
	buf->head[0].iov_len -= (data_start - orig_start);
	buf->len -= (data_start - orig_start);

	if (gss_krb5_remove_padding(buf, blocksize))
		return GSS_S_DEFECTIVE_TOKEN;

	return GSS_S_COMPLETE;
}
