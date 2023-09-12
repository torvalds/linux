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

#include <crypto/skcipher.h>
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/pagemap.h>

#include "gss_krb5_internal.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

/*
 * We can shift data by up to LOCAL_BUF_LEN bytes in a pass.  If we need
 * to do more than that, we shift repeatedly.  Kevin Coffman reports
 * seeing 28 bytes as the value used by Microsoft clients and servers
 * with AES, so this constant is chosen to allow handling 28 in one pass
 * without using too much stack space.
 *
 * If that proves to a problem perhaps we could use a more clever
 * algorithm.
 */
#define LOCAL_BUF_LEN 32u

static void rotate_buf_a_little(struct xdr_buf *buf, unsigned int shift)
{
	char head[LOCAL_BUF_LEN];
	char tmp[LOCAL_BUF_LEN];
	unsigned int this_len, i;

	BUG_ON(shift > LOCAL_BUF_LEN);

	read_bytes_from_xdr_buf(buf, 0, head, shift);
	for (i = 0; i + shift < buf->len; i += LOCAL_BUF_LEN) {
		this_len = min(LOCAL_BUF_LEN, buf->len - (i + shift));
		read_bytes_from_xdr_buf(buf, i+shift, tmp, this_len);
		write_bytes_to_xdr_buf(buf, i, tmp, this_len);
	}
	write_bytes_to_xdr_buf(buf, buf->len - shift, head, shift);
}

static void _rotate_left(struct xdr_buf *buf, unsigned int shift)
{
	int shifted = 0;
	int this_shift;

	shift %= buf->len;
	while (shifted < shift) {
		this_shift = min(shift - shifted, LOCAL_BUF_LEN);
		rotate_buf_a_little(buf, this_shift);
		shifted += this_shift;
	}
}

static void rotate_left(u32 base, struct xdr_buf *buf, unsigned int shift)
{
	struct xdr_buf subbuf;

	xdr_buf_subsegment(buf, &subbuf, base, buf->len - base);
	_rotate_left(&subbuf, shift);
}

u32
gss_krb5_wrap_v2(struct krb5_ctx *kctx, int offset,
		 struct xdr_buf *buf, struct page **pages)
{
	u8		*ptr;
	time64_t	now;
	u8		flags = 0x00;
	__be16		*be16ptr;
	__be64		*be64ptr;
	u32		err;

	dprintk("RPC:       %s\n", __func__);

	/* make room for gss token header */
	if (xdr_extend_head(buf, offset, GSS_KRB5_TOK_HDR_LEN))
		return GSS_S_FAILURE;

	/* construct gss token header */
	ptr = buf->head[0].iov_base + offset;
	*ptr++ = (unsigned char) ((KG2_TOK_WRAP>>8) & 0xff);
	*ptr++ = (unsigned char) (KG2_TOK_WRAP & 0xff);

	if ((kctx->flags & KRB5_CTX_FLAG_INITIATOR) == 0)
		flags |= KG2_TOKEN_FLAG_SENTBYACCEPTOR;
	if ((kctx->flags & KRB5_CTX_FLAG_ACCEPTOR_SUBKEY) != 0)
		flags |= KG2_TOKEN_FLAG_ACCEPTORSUBKEY;
	/* We always do confidentiality in wrap tokens */
	flags |= KG2_TOKEN_FLAG_SEALED;

	*ptr++ = flags;
	*ptr++ = 0xff;
	be16ptr = (__be16 *)ptr;

	*be16ptr++ = 0;
	/* "inner" token header always uses 0 for RRC */
	*be16ptr++ = 0;

	be64ptr = (__be64 *)be16ptr;
	*be64ptr = cpu_to_be64(atomic64_fetch_inc(&kctx->seq_send64));

	err = (*kctx->gk5e->encrypt)(kctx, offset, buf, pages);
	if (err)
		return err;

	now = ktime_get_real_seconds();
	return (kctx->endtime < now) ? GSS_S_CONTEXT_EXPIRED : GSS_S_COMPLETE;
}

u32
gss_krb5_unwrap_v2(struct krb5_ctx *kctx, int offset, int len,
		   struct xdr_buf *buf, unsigned int *slack,
		   unsigned int *align)
{
	time64_t	now;
	u8		*ptr;
	u8		flags = 0x00;
	u16		ec, rrc;
	int		err;
	u32		headskip, tailskip;
	u8		decrypted_hdr[GSS_KRB5_TOK_HDR_LEN];
	unsigned int	movelen;


	dprintk("RPC:       %s\n", __func__);

	ptr = buf->head[0].iov_base + offset;

	if (be16_to_cpu(*((__be16 *)ptr)) != KG2_TOK_WRAP)
		return GSS_S_DEFECTIVE_TOKEN;

	flags = ptr[2];
	if ((!kctx->initiate && (flags & KG2_TOKEN_FLAG_SENTBYACCEPTOR)) ||
	    (kctx->initiate && !(flags & KG2_TOKEN_FLAG_SENTBYACCEPTOR)))
		return GSS_S_BAD_SIG;

	if ((flags & KG2_TOKEN_FLAG_SEALED) == 0) {
		dprintk("%s: token missing expected sealed flag\n", __func__);
		return GSS_S_DEFECTIVE_TOKEN;
	}

	if (ptr[3] != 0xff)
		return GSS_S_DEFECTIVE_TOKEN;

	ec = be16_to_cpup((__be16 *)(ptr + 4));
	rrc = be16_to_cpup((__be16 *)(ptr + 6));

	/*
	 * NOTE: the sequence number at ptr + 8 is skipped, rpcsec_gss
	 * doesn't want it checked; see page 6 of rfc 2203.
	 */

	if (rrc != 0)
		rotate_left(offset + 16, buf, rrc);

	err = (*kctx->gk5e->decrypt)(kctx, offset, len, buf,
				     &headskip, &tailskip);
	if (err)
		return GSS_S_FAILURE;

	/*
	 * Retrieve the decrypted gss token header and verify
	 * it against the original
	 */
	err = read_bytes_from_xdr_buf(buf,
				len - GSS_KRB5_TOK_HDR_LEN - tailskip,
				decrypted_hdr, GSS_KRB5_TOK_HDR_LEN);
	if (err) {
		dprintk("%s: error %u getting decrypted_hdr\n", __func__, err);
		return GSS_S_FAILURE;
	}
	if (memcmp(ptr, decrypted_hdr, 6)
				|| memcmp(ptr + 8, decrypted_hdr + 8, 8)) {
		dprintk("%s: token hdr, plaintext hdr mismatch!\n", __func__);
		return GSS_S_FAILURE;
	}

	/* do sequencing checks */

	/* it got through unscathed.  Make sure the context is unexpired */
	now = ktime_get_real_seconds();
	if (now > kctx->endtime)
		return GSS_S_CONTEXT_EXPIRED;

	/*
	 * Move the head data back to the right position in xdr_buf.
	 * We ignore any "ec" data since it might be in the head or
	 * the tail, and we really don't need to deal with it.
	 * Note that buf->head[0].iov_len may indicate the available
	 * head buffer space rather than that actually occupied.
	 */
	movelen = min_t(unsigned int, buf->head[0].iov_len, len);
	movelen -= offset + GSS_KRB5_TOK_HDR_LEN + headskip;
	BUG_ON(offset + GSS_KRB5_TOK_HDR_LEN + headskip + movelen >
							buf->head[0].iov_len);
	memmove(ptr, ptr + GSS_KRB5_TOK_HDR_LEN + headskip, movelen);
	buf->head[0].iov_len -= GSS_KRB5_TOK_HDR_LEN + headskip;
	buf->len = len - (GSS_KRB5_TOK_HDR_LEN + headskip);

	/* Trim off the trailing "extra count" and checksum blob */
	xdr_buf_trim(buf, ec + GSS_KRB5_TOK_HDR_LEN + tailskip);

	*align = XDR_QUADLEN(GSS_KRB5_TOK_HDR_LEN + headskip);
	*slack = *align + XDR_QUADLEN(ec + GSS_KRB5_TOK_HDR_LEN + tailskip);
	return GSS_S_COMPLETE;
}
