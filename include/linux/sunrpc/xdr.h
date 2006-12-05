/*
 * include/linux/sunrpc/xdr.h
 *
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _SUNRPC_XDR_H_
#define _SUNRPC_XDR_H_

#ifdef __KERNEL__

#include <linux/uio.h>
#include <asm/byteorder.h>

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
 * This is the generic XDR function. rqstp is either a rpc_rqst (client
 * side) or svc_rqst pointer (server side).
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

	struct page **	pages;		/* Array of contiguous pages */
	unsigned int	page_base,	/* Start of page data */
			page_len;	/* Length of page data */

	unsigned int	buflen,		/* Total length of storage buffer */
			len;		/* Length of XDR encoded message */

};

/*
 * pre-xdr'ed macros.
 */

#define	xdr_zero	__constant_htonl(0)
#define	xdr_one		__constant_htonl(1)
#define	xdr_two		__constant_htonl(2)

#define	rpc_success		__constant_htonl(RPC_SUCCESS)
#define	rpc_prog_unavail	__constant_htonl(RPC_PROG_UNAVAIL)
#define	rpc_prog_mismatch	__constant_htonl(RPC_PROG_MISMATCH)
#define	rpc_proc_unavail	__constant_htonl(RPC_PROC_UNAVAIL)
#define	rpc_garbage_args	__constant_htonl(RPC_GARBAGE_ARGS)
#define	rpc_system_err		__constant_htonl(RPC_SYSTEM_ERR)
#define	rpc_drop_reply		__constant_htonl(RPC_DROP_REPLY)

#define	rpc_auth_ok		__constant_htonl(RPC_AUTH_OK)
#define	rpc_autherr_badcred	__constant_htonl(RPC_AUTH_BADCRED)
#define	rpc_autherr_rejectedcred __constant_htonl(RPC_AUTH_REJECTEDCRED)
#define	rpc_autherr_badverf	__constant_htonl(RPC_AUTH_BADVERF)
#define	rpc_autherr_rejectedverf __constant_htonl(RPC_AUTH_REJECTEDVERF)
#define	rpc_autherr_tooweak	__constant_htonl(RPC_AUTH_TOOWEAK)
#define	rpcsec_gsserr_credproblem	__constant_htonl(RPCSEC_GSS_CREDPROBLEM)
#define	rpcsec_gsserr_ctxproblem	__constant_htonl(RPCSEC_GSS_CTXPROBLEM)
#define	rpc_autherr_oldseqnum	__constant_htonl(101)

/*
 * Miscellaneous XDR helper functions
 */
__be32 *xdr_encode_opaque_fixed(__be32 *p, const void *ptr, unsigned int len);
__be32 *xdr_encode_opaque(__be32 *p, const void *ptr, unsigned int len);
__be32 *xdr_encode_string(__be32 *p, const char *s);
__be32 *xdr_decode_string_inplace(__be32 *p, char **sp, int *lenp, int maxlen);
__be32 *xdr_encode_netobj(__be32 *p, const struct xdr_netobj *);
__be32 *xdr_decode_netobj(__be32 *p, struct xdr_netobj *);

void	xdr_encode_pages(struct xdr_buf *, struct page **, unsigned int,
			 unsigned int);
void	xdr_inline_pages(struct xdr_buf *, unsigned int,
			 struct page **, unsigned int, unsigned int);

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
	*p++ = htonl(val >> 32);
	*p++ = htonl(val & 0xFFFFFFFF);
	return p;
}

static inline __be32 *
xdr_decode_hyper(__be32 *p, __u64 *valp)
{
	*valp  = ((__u64) ntohl(*p++)) << 32;
	*valp |= ntohl(*p++);
	return p;
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
extern int xdr_buf_subsegment(struct xdr_buf *, struct xdr_buf *, int, int);
extern int xdr_buf_read_netobj(struct xdr_buf *, struct xdr_netobj *, int);
extern int read_bytes_from_xdr_buf(struct xdr_buf *, int, void *, int);
extern int write_bytes_to_xdr_buf(struct xdr_buf *, int, void *, int);

/*
 * Helper structure for copying from an sk_buff.
 */
typedef struct {
	struct sk_buff	*skb;
	unsigned int	offset;
	size_t		count;
	__wsum		csum;
} skb_reader_t;

typedef size_t (*skb_read_actor_t)(skb_reader_t *desc, void *to, size_t len);

extern int csum_partial_copy_to_xdr(struct xdr_buf *, struct sk_buff *);
extern ssize_t xdr_partial_copy_from_skb(struct xdr_buf *, unsigned int,
		skb_reader_t *, skb_read_actor_t);

extern int xdr_encode_word(struct xdr_buf *, int, u32);
extern int xdr_decode_word(struct xdr_buf *, int, u32 *);

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

/*
 * Provide some simple tools for XDR buffer overflow-checking etc.
 */
struct xdr_stream {
	__be32 *p;		/* start of available buffer */
	struct xdr_buf *buf;	/* XDR buffer to read/write */

	__be32 *end;		/* end of available buffer space */
	struct kvec *iov;	/* pointer to the current kvec */
};

extern void xdr_init_encode(struct xdr_stream *xdr, struct xdr_buf *buf, __be32 *p);
extern __be32 *xdr_reserve_space(struct xdr_stream *xdr, size_t nbytes);
extern void xdr_write_pages(struct xdr_stream *xdr, struct page **pages,
		unsigned int base, unsigned int len);
extern void xdr_init_decode(struct xdr_stream *xdr, struct xdr_buf *buf, __be32 *p);
extern __be32 *xdr_inline_decode(struct xdr_stream *xdr, size_t nbytes);
extern void xdr_read_pages(struct xdr_stream *xdr, unsigned int len);
extern void xdr_enter_page(struct xdr_stream *xdr, unsigned int len);

#endif /* __KERNEL__ */

#endif /* _SUNRPC_XDR_H_ */
