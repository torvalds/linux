/*
 * XDR standard data types and function declarations
 *
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 *
 * Based on:
 *   RFC 4506 "XDR: External Data Representation Standard", May 2006
 */

#ifndef _SUNRPC_XDR_H_
#define _SUNRPC_XDR_H_

#ifdef __KERNEL__

#include <linux/uio.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <linux/scatterlist.h>

/*
 * Buffer adjustment
 */
#define XDR_QUADLEN(l)		(((l) + 3) >> 2)

/*
 * Generic opaque `network object.' At the kernel level, this type
 * is used only by lockd.
 */
#define XDR_MAX_NETOBJ		1024
struct xdr_netobj {
	unsigned int		len;
	u8 *			data;
};

/*
 * This is the legacy generic XDR function. rqstp is either a rpc_rqst
 * (client side) or svc_rqst pointer (server side).
 * Encode functions always assume there's enough room in the buffer.
 */
typedef int	(*kxdrproc_t)(void *rqstp, __be32 *data, void *obj);

/*
 * Basic structure for transmission/reception of a client XDR message.
 * Features a header (for a linear buffer containing RPC headers
 * and the data payload for short messages), and then an array of
 * pages.
 * The tail iovec allows you to append data after the page array. Its
 * main interest is for appending padding to the pages in order to
 * satisfy the int_32-alignment requirements in RFC1832.
 *
 * For the future, we might want to string several of these together
 * in a list if anybody wants to make use of NFSv4 COMPOUND
 * operations and/or has a need for scatter/gather involving pages.
 */
struct xdr_buf {
	struct kvec	head[1],	/* RPC header + non-page data */
			tail[1];	/* Appended after page data */

	struct page **	pages;		/* Array of pages */
	unsigned int	page_base,	/* Start of page data */
			page_len,	/* Length of page data */
			flags;		/* Flags for data disposition */
#define XDRBUF_READ		0x01		/* target of file read */
#define XDRBUF_WRITE		0x02		/* source of file write */

	unsigned int	buflen,		/* Total length of storage buffer */
			len;		/* Length of XDR encoded message */
};

static inline void
xdr_buf_init(struct xdr_buf *buf, void *start, size_t len)
{
	buf->head[0].iov_base = start;
	buf->head[0].iov_len = len;
	buf->tail[0].iov_len = 0;
	buf->page_len = 0;
	buf->flags = 0;
	buf->len = 0;
	buf->buflen = len;
}

/*
 * pre-xdr'ed macros.
 */

#define	xdr_zero	cpu_to_be32(0)
#define	xdr_one		cpu_to_be32(1)
#define	xdr_two		cpu_to_be32(2)

#define	rpc_success		cpu_to_be32(RPC_SUCCESS)
#define	rpc_prog_unavail	cpu_to_be32(RPC_PROG_UNAVAIL)
#define	rpc_prog_mismatch	cpu_to_be32(RPC_PROG_MISMATCH)
#define	rpc_proc_unavail	cpu_to_be32(RPC_PROC_UNAVAIL)
#define	rpc_garbage_args	cpu_to_be32(RPC_GARBAGE_ARGS)
#define	rpc_system_err		cpu_to_be32(RPC_SYSTEM_ERR)
#define	rpc_drop_reply		cpu_to_be32(RPC_DROP_REPLY)

#define	rpc_auth_ok		cpu_to_be32(RPC_AUTH_OK)
#define	rpc_autherr_badcred	cpu_to_be32(RPC_AUTH_BADCRED)
#define	rpc_autherr_rejectedcred cpu_to_be32(RPC_AUTH_REJECTEDCRED)
#define	rpc_autherr_badverf	cpu_to_be32(RPC_AUTH_BADVERF)
#define	rpc_autherr_rejectedverf cpu_to_be32(RPC_AUTH_REJECTEDVERF)
#define	rpc_autherr_tooweak	cpu_to_be32(RPC_AUTH_TOOWEAK)
#define	rpcsec_gsserr_credproblem	cpu_to_be32(RPCSEC_GSS_CREDPROBLEM)
#define	rpcsec_gsserr_ctxproblem	cpu_to_be32(RPCSEC_GSS_CTXPROBLEM)
#define	rpc_autherr_oldseqnum	cpu_to_be32(101)

/*
 * Miscellaneous XDR helper functions
 */
__be32 *xdr_encode_opaque_fixed(__be32 *p, const void *ptr, unsigned int len);
__be32 *xdr_encode_opaque(__be32 *p, const void *ptr, unsigned int len);
__be32 *xdr_encode_string(__be32 *p, const char *s);
__be32 *xdr_decode_string_inplace(__be32 *p, char **sp, unsigned int *lenp,
			unsigned int maxlen);
__be32 *xdr_encode_netobj(__be32 *p, const struct xdr_netobj *);
__be32 *xdr_decode_netobj(__be32 *p, struct xdr_netobj *);

void	xdr_inline_pages(struct xdr_buf *, unsigned int,
			 struct page **, unsigned int, unsigned int);
void	xdr_terminate_string(struct xdr_buf *, const u32);

static inline __be32 *xdr_encode_array(__be32 *p, const void *s, unsigned int len)
{
	return xdr_encode_opaque(p, s, len);
}

/*
 * Decode 64bit quantities (NFSv3 support)
 */
static inline __be32 *
xdr_encode_hyper(__be32 *p, __u64 val)
{
	put_unaligned_be64(val, p);
	return p + 2;
}

static inline __be32 *
xdr_decode_hyper(__be32 *p, __u64 *valp)
{
	*valp = get_unaligned_be64(p);
	return p + 2;
}

static inline __be32 *
xdr_decode_opaque_fixed(__be32 *p, void *ptr, unsigned int len)
{
	memcpy(ptr, p, len);
	return p + XDR_QUADLEN(len);
}

/*
 * Adjust kvec to reflect end of xdr'ed data (RPC client XDR)
 */
static inline int
xdr_adjust_iovec(struct kvec *iov, __be32 *p)
{
	return iov->iov_len = ((u8 *) p - (u8 *) iov->iov_base);
}

/*
 * XDR buffer helper functions
 */
extern void xdr_shift_buf(struct xdr_buf *, size_t);
extern void xdr_buf_from_iov(struct kvec *, struct xdr_buf *);
extern int xdr_buf_subsegment(struct xdr_buf *, struct xdr_buf *, unsigned int, unsigned int);
extern void xdr_buf_trim(struct xdr_buf *, unsigned int);
extern int xdr_buf_read_netobj(struct xdr_buf *, struct xdr_netobj *, unsigned int);
extern int read_bytes_from_xdr_buf(struct xdr_buf *, unsigned int, void *, unsigned int);
extern int write_bytes_to_xdr_buf(struct xdr_buf *, unsigned int, void *, unsigned int);

/*
 * Helper structure for copying from an sk_buff.
 */
struct xdr_skb_reader {
	struct sk_buff	*skb;
	unsigned int	offset;
	size_t		count;
	__wsum		csum;
};

typedef size_t (*xdr_skb_read_actor)(struct xdr_skb_reader *desc, void *to, size_t len);

size_t xdr_skb_read_bits(struct xdr_skb_reader *desc, void *to, size_t len);
extern int csum_partial_copy_to_xdr(struct xdr_buf *, struct sk_buff *);
extern ssize_t xdr_partial_copy_from_skb(struct xdr_buf *, unsigned int,
		struct xdr_skb_reader *, xdr_skb_read_actor);

extern int xdr_encode_word(struct xdr_buf *, unsigned int, u32);
extern int xdr_decode_word(struct xdr_buf *, unsigned int, u32 *);

struct xdr_array2_desc;
typedef int (*xdr_xcode_elem_t)(struct xdr_array2_desc *desc, void *elem);
struct xdr_array2_desc {
	unsigned int elem_size;
	unsigned int array_len;
	unsigned int array_maxlen;
	xdr_xcode_elem_t xcode;
};

extern int xdr_decode_array2(struct xdr_buf *buf, unsigned int base,
			     struct xdr_array2_desc *desc);
extern int xdr_encode_array2(struct xdr_buf *buf, unsigned int base,
			     struct xdr_array2_desc *desc);
extern void _copy_from_pages(char *p, struct page **pages, size_t pgbase,
			     size_t len);

/*
 * Provide some simple tools for XDR buffer overflow-checking etc.
 */
struct xdr_stream {
	__be32 *p;		/* start of available buffer */
	struct xdr_buf *buf;	/* XDR buffer to read/write */

	__be32 *end;		/* end of available buffer space */
	struct kvec *iov;	/* pointer to the current kvec */
	struct kvec scratch;	/* Scratch buffer */
	struct page **page_ptr;	/* pointer to the current page */
	unsigned int nwords;	/* Remaining decode buffer length */
};

/*
 * These are the xdr_stream style generic XDR encode and decode functions.
 */
typedef void	(*kxdreproc_t)(void *rqstp, struct xdr_stream *xdr, void *obj);
typedef int	(*kxdrdproc_t)(void *rqstp, struct xdr_stream *xdr, void *obj);

extern void xdr_init_encode(struct xdr_stream *xdr, struct xdr_buf *buf, __be32 *p);
extern __be32 *xdr_reserve_space(struct xdr_stream *xdr, size_t nbytes);
extern void xdr_commit_encode(struct xdr_stream *xdr);
extern void xdr_truncate_encode(struct xdr_stream *xdr, size_t len);
extern int xdr_restrict_buflen(struct xdr_stream *xdr, int newbuflen);
extern void xdr_write_pages(struct xdr_stream *xdr, struct page **pages,
		unsigned int base, unsigned int len);
extern unsigned int xdr_stream_pos(const struct xdr_stream *xdr);
extern void xdr_init_decode(struct xdr_stream *xdr, struct xdr_buf *buf, __be32 *p);
extern void xdr_init_decode_pages(struct xdr_stream *xdr, struct xdr_buf *buf,
		struct page **pages, unsigned int len);
extern void xdr_set_scratch_buffer(struct xdr_stream *xdr, void *buf, size_t buflen);
extern __be32 *xdr_inline_decode(struct xdr_stream *xdr, size_t nbytes);
extern unsigned int xdr_read_pages(struct xdr_stream *xdr, unsigned int len);
extern void xdr_enter_page(struct xdr_stream *xdr, unsigned int len);
extern int xdr_process_buf(struct xdr_buf *buf, unsigned int offset, unsigned int len, int (*actor)(struct scatterlist *, void *), void *data);

#endif /* __KERNEL__ */

#endif /* _SUNRPC_XDR_H_ */
