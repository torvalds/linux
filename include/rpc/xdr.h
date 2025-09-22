/*	$OpenBSD: xdr.h,v 1.14 2022/12/27 07:44:56 jmc Exp $	*/
/*	$NetBSD: xdr.h,v 1.7 1995/04/29 05:28:06 cgd Exp $	*/

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: @(#)xdr.h 1.19 87/04/22 SMI
 *	@(#)xdr.h	2.2 88/07/29 4.0 RPCSRC
 */

/*
 * xdr.h, External Data Representation Serialization Routines.
 */

#ifndef _RPC_XDR_H
#define _RPC_XDR_H
#include <sys/cdefs.h>

/*
 * XDR provides a conventional way for converting between C data
 * types and an external bit-string representation.  Library supplied
 * routines provide for the conversion on built-in C data types.  These
 * routines and utility routines defined here are used to help implement
 * a type encode/decode routine for each user-defined type.
 *
 * Each data type provides a single procedure which takes two arguments:
 *
 *	bool_t
 *	xdrproc(xdrs, argresp)
 *		XDR *xdrs;
 *		<type> *argresp;
 *
 * xdrs is an instance of a XDR handle, to which or from which the data
 * type is to be converted.  argresp is a pointer to the structure to be
 * converted.  The XDR handle contains an operation field which indicates
 * which of the operations (ENCODE, DECODE * or FREE) is to be performed.
 *
 * XDR_DECODE may allocate space if the pointer argresp is null.  This
 * data can be freed with the XDR_FREE operation.
 *
 * We write only one procedure per data type to make it easy
 * to keep the encode and decode procedures for a data type consistent.
 * In many cases the same code performs all operations on a user defined type,
 * because all the hard work is done in the component type routines.
 * decode as a series of calls on the nested data types.
 */

/*
 * Xdr operations.  XDR_ENCODE causes the type to be encoded into the
 * stream.  XDR_DECODE causes the type to be extracted from the stream.
 * XDR_FREE can be used to release the space allocated by an XDR_DECODE
 * request.
 */
enum xdr_op {
	XDR_ENCODE=0,
	XDR_DECODE=1,
	XDR_FREE=2
};

/*
 * This is the number of bytes per unit of external data.
 */
#define BYTES_PER_XDR_UNIT	(4)
#define RNDUP(x)  ((((x) + BYTES_PER_XDR_UNIT - 1) / BYTES_PER_XDR_UNIT) \
		    * BYTES_PER_XDR_UNIT)

/*
 * The XDR handle.
 * Contains operation which is being applied to the stream,
 * an operations vector for the particular implementation (e.g. see xdr_mem.c),
 * and two private fields for the use of the particular implementation.
 */
typedef struct __rpc_xdr {
	enum xdr_op	x_op;		/* operation; fast additional param */
	const struct xdr_ops {
		/* get a long from underlying stream */
		bool_t	(*x_getlong)(struct __rpc_xdr *, long *);
		/* put a long to " */
		bool_t	(*x_putlong)(struct __rpc_xdr *, long *);
		/* get some bytes from " */
		bool_t	(*x_getbytes)(struct __rpc_xdr *, caddr_t, 
		    unsigned int);
		/* put some bytes to " */
		bool_t	(*x_putbytes)(struct __rpc_xdr *, caddr_t, 
		    unsigned int);
		/* returns bytes off from beginning */
		unsigned int	(*x_getpostn)(struct __rpc_xdr *);
		/* lets you reposition the stream */
		bool_t  (*x_setpostn)(struct __rpc_xdr *, unsigned int);
		/* buf quick ptr to buffered data */
		int32_t	*(*x_inline)(struct __rpc_xdr *, unsigned int);
		/* free privates of this xdr_stream */
		void	(*x_destroy)(struct __rpc_xdr *);
		bool_t	(*x_control)(struct __rpc_xdr *, int, void *);
	} *x_ops;
	caddr_t 	x_public;	/* users' data */
	caddr_t		x_private;	/* pointer to private data */
	caddr_t 	x_base;		/* private used for position info */
	unsigned int	x_handy;	/* extra private word */
} XDR;

/*
 * A xdrproc_t exists for each data type which is to be encoded or decoded.
 *
 * The second argument to the xdrproc_t is a pointer to an opaque pointer.
 * The opaque pointer generally points to a structure of the data type
 * to be decoded.  If this pointer is 0, then the type routines should
 * allocate dynamic storage of the appropriate size and return it.
 *
 * XXX can't actually prototype it, because some take three args!!!
 */
typedef	bool_t (*xdrproc_t)(/* XDR *, void *, unsigned int */);

/*
 * Operations defined on a XDR handle
 *
 * XDR		*xdrs;
 * long		*longp;
 * caddr_t	 addr;
 * unsigned int	 len;
 * unsigned int	 pos;
 */
#define XDR_GETLONG(xdrs, longp)			\
	(*(xdrs)->x_ops->x_getlong)(xdrs, longp)
#define xdr_getlong(xdrs, longp)			\
	(*(xdrs)->x_ops->x_getlong)(xdrs, longp)

#define XDR_PUTLONG(xdrs, longp)			\
	(*(xdrs)->x_ops->x_putlong)(xdrs, longp)
#define xdr_putlong(xdrs, longp)			\
	(*(xdrs)->x_ops->x_putlong)(xdrs, longp)

#define XDR_GETBYTES(xdrs, addr, len)			\
	(*(xdrs)->x_ops->x_getbytes)(xdrs, addr, len)
#define xdr_getbytes(xdrs, addr, len)			\
	(*(xdrs)->x_ops->x_getbytes)(xdrs, addr, len)

#define XDR_PUTBYTES(xdrs, addr, len)			\
	(*(xdrs)->x_ops->x_putbytes)(xdrs, addr, len)
#define xdr_putbytes(xdrs, addr, len)			\
	(*(xdrs)->x_ops->x_putbytes)(xdrs, addr, len)

#define XDR_GETPOS(xdrs)				\
	(*(xdrs)->x_ops->x_getpostn)(xdrs)
#define xdr_getpos(xdrs)				\
	(*(xdrs)->x_ops->x_getpostn)(xdrs)

#define XDR_SETPOS(xdrs, pos)				\
	(*(xdrs)->x_ops->x_setpostn)(xdrs, pos)
#define xdr_setpos(xdrs, pos)				\
	(*(xdrs)->x_ops->x_setpostn)(xdrs, pos)

#define	XDR_INLINE(xdrs, len)				\
	(*(xdrs)->x_ops->x_inline)(xdrs, len)
#define	xdr_inline(xdrs, len)				\
	(*(xdrs)->x_ops->x_inline)(xdrs, len)

#define	XDR_DESTROY(xdrs)				\
	if ((xdrs)->x_ops->x_destroy) 			\
		(*(xdrs)->x_ops->x_destroy)(xdrs)
#define	xdr_destroy(xdrs)				\
	if ((xdrs)->x_ops->x_destroy) 			\
		(*(xdrs)->x_ops->x_destroy)(xdrs)

/*
 * Support struct for discriminated unions.
 * You create an array of xdrdiscrim structures, terminated with
 * a entry with a null procedure pointer.  The xdr_union routine gets
 * the discriminant value and then searches the array of structures
 * for a matching value.  If a match is found the associated xdr routine
 * is called to handle that part of the union.  If there is
 * no match, then a default routine may be called.
 * If there is no match and no default routine it is an error.
 */
#define NULL_xdrproc_t ((xdrproc_t)0)
struct xdr_discrim {
	int	value;
	xdrproc_t proc;
};

/*
 * In-line routines for fast encode/decode of primitive data types.
 * Caveat emptor: these use single memory cycles to get the
 * data from the underlying buffer, and will fail to operate
 * properly if the data is not aligned.  The standard way to use these
 * is to say:
 *	if ((buf = XDR_INLINE(xdrs, count)) == NULL)
 *		return (FALSE);
 *	<<< macro calls >>>
 * where ``count'' is the number of bytes of data occupied
 * by the primitive data types.
 *
 * N.B. and frozen for all time: each data type here uses 4 bytes
 * of external representation.
 */
#define IXDR_GET_LONG(buf)		((long)ntohl((unsigned long)*(buf)++))
#define IXDR_PUT_LONG(buf, v)		(*(buf)++ = (long)htonl((unsigned long)v))

#define IXDR_GET_BOOL(buf)		((bool_t)IXDR_GET_LONG(buf))
#define IXDR_GET_ENUM(buf, t)		((t)IXDR_GET_LONG(buf))
#define IXDR_GET_U_LONG(buf)		((unsigned long)IXDR_GET_LONG(buf))
#define IXDR_GET_SHORT(buf)		((short)IXDR_GET_LONG(buf))
#define IXDR_GET_U_SHORT(buf)		((unsigned short)IXDR_GET_LONG(buf))

#define IXDR_PUT_BOOL(buf, v)		IXDR_PUT_LONG((buf), ((long)(v)))
#define IXDR_PUT_ENUM(buf, v)		IXDR_PUT_LONG((buf), ((long)(v)))
#define IXDR_PUT_U_LONG(buf, v)		IXDR_PUT_LONG((buf), ((long)(v)))
#define IXDR_PUT_SHORT(buf, v)		IXDR_PUT_LONG((buf), ((long)(v)))
#define IXDR_PUT_U_SHORT(buf, v)	IXDR_PUT_LONG((buf), ((long)(v)))

/*
 * These are the "generic" xdr routines.
 */
__BEGIN_DECLS
extern bool_t	xdr_void(void);
extern bool_t	xdr_int(XDR *, int *);
extern bool_t	xdr_u_int(XDR *, unsigned int *);
extern bool_t	xdr_long(XDR *, long *);
extern bool_t	xdr_u_long(XDR *, unsigned long *);
extern bool_t	xdr_short(XDR *, short *);
extern bool_t	xdr_u_short(XDR *, unsigned short *);
extern bool_t	xdr_int16_t(XDR *, int16_t *);
extern bool_t	xdr_u_int16_t(XDR *, u_int16_t *);
extern bool_t	xdr_int32_t(XDR *, int32_t *);
extern bool_t	xdr_u_int32_t(XDR *, u_int32_t *);
extern bool_t	xdr_int64_t(XDR *, int64_t *);
extern bool_t	xdr_u_int64_t(XDR *, u_int64_t *);
extern bool_t	xdr_bool(XDR *, bool_t *);
extern bool_t	xdr_enum(XDR *, enum_t *);
extern bool_t	xdr_array(XDR *, char **, unsigned int *, unsigned int, 
    unsigned int, xdrproc_t);
extern bool_t	xdr_bytes(XDR *, char **, unsigned int *, unsigned int);
extern bool_t	xdr_opaque(XDR *, caddr_t, unsigned int);
extern bool_t	xdr_string(XDR *, char **, unsigned int);
extern bool_t	xdr_union(XDR *, enum_t *, char *, struct xdr_discrim *, 
    xdrproc_t);
extern bool_t	xdr_char(XDR *, char *);
extern bool_t	xdr_u_char(XDR *, unsigned char *);
extern bool_t	xdr_vector(XDR *, char *, unsigned int, unsigned int, 
    xdrproc_t);
extern bool_t	xdr_float(XDR *, float *);
extern bool_t	xdr_double(XDR *, double *);
extern bool_t	xdr_reference(XDR *, caddr_t *, unsigned int, xdrproc_t);
extern bool_t	xdr_pointer(XDR *, caddr_t *, unsigned int, xdrproc_t);
extern bool_t	xdr_wrapstring(XDR *, char **);
extern void	xdr_free(xdrproc_t, char *);
__END_DECLS

/*
 * Common opaque bytes objects used by many rpc protocols;
 * declared here due to commonality.
 */
#define MAX_NETOBJ_SZ 1024 
struct netobj {
	unsigned int	 n_len;
	char		*n_bytes;
};
typedef struct netobj netobj;
extern bool_t   xdr_netobj(XDR *, struct netobj *);

/*
 * These are the public routines for the various implementations of
 * xdr streams.
 */
__BEGIN_DECLS
/* XDR using memory buffers */
extern void   xdrmem_create(XDR *, char *, unsigned int, enum xdr_op);

#ifdef _STDIO_H_
/* XDR using stdio library */
extern void   xdrstdio_create(XDR *, FILE *, enum xdr_op);
#endif

/* XDR pseudo records for tcp */
extern void   xdrrec_create(XDR *, unsigned int, unsigned int, char *,
			    int (*)(caddr_t, caddr_t, int),
			    int (*)(caddr_t, caddr_t, int));

/* make end of xdr record */
extern bool_t xdrrec_endofrecord(XDR *, int);

/* move to beginning of next record */
extern bool_t xdrrec_skiprecord(XDR *);

/* true if no more input */
extern bool_t xdrrec_eof(XDR *);
__END_DECLS

#endif /* !_RPC_XDR_H */
