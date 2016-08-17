/*****************************************************************************\
 *  Copyright (c) 2008-2010 Sun Microsystems, Inc.
 *  Written by Ricardo Correia <Ricardo.M.Correia@Sun.COM>
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Solaris Porting Layer (SPL) XDR Implementation.
\*****************************************************************************/

#include <linux/string.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/types.h>
#include <rpc/types.h>
#include <rpc/xdr.h>

/*
 * SPL's XDR mem implementation.
 *
 * This is used by libnvpair to serialize/deserialize the name-value pair data
 * structures into byte arrays in a well-defined and portable manner.
 *
 * These data structures are used by the DMU/ZFS to flexibly manipulate various
 * information in memory and later serialize it/deserialize it to disk.
 * Examples of usages include the pool configuration, lists of pool and dataset
 * properties, etc.
 *
 * Reference documentation for the XDR representation and XDR operations can be
 * found in RFC 1832 and xdr(3), respectively.
 *
 * ===  Implementation shortcomings ===
 *
 * It is assumed that the following C types have the following sizes:
 *
 * char/unsigned char:      1 byte
 * short/unsigned short:    2 bytes
 * int/unsigned int:        4 bytes
 * longlong_t/u_longlong_t: 8 bytes
 *
 * The C standard allows these types to be larger (and in the case of ints,
 * shorter), so if that is the case on some compiler/architecture, the build
 * will fail (on purpose).
 *
 * If someone wants to fix the code to work properly on such environments, then:
 *
 * 1) Preconditions should be added to xdrmem_enc functions to make sure the
 *    caller doesn't pass arguments which exceed the expected range.
 * 2) Functions which take signed integers should be changed to properly do
 *    sign extension.
 * 3) For ints with less than 32 bits, well.. I suspect you'll have bigger
 *    problems than this implementation.
 *
 * It is also assumed that:
 *
 * 1) Chars have 8 bits.
 * 2) We can always do 32-bit-aligned int memory accesses and byte-aligned
 *    memcpy, memset and memcmp.
 * 3) Arrays passed to xdr_array() are packed and the compiler/architecture
 *    supports element-sized-aligned memory accesses.
 * 4) Negative integers are natively stored in two's complement binary
 *    representation.
 *
 * No checks are done for the 4 assumptions above, though.
 *
 * === Caller expectations ===
 *
 * Existing documentation does not describe the semantics of XDR operations very
 * well.  Therefore, some assumptions about failure semantics will be made and
 * will be described below:
 *
 * 1) If any encoding operation fails (e.g., due to lack of buffer space), the
 * the stream should be considered valid only up to the encoding operation
 * previous to the one that first failed. However, the stream size as returned
 * by xdr_control() cannot be considered to be strictly correct (it may be
 * bigger).
 *
 * Putting it another way, if there is an encoding failure it's undefined
 * whether anything is added to the stream in that operation and therefore
 * neither xdr_control() nor future encoding operations on the same stream can
 * be relied upon to produce correct results.
 *
 * 2) If a decoding operation fails, it's undefined whether anything will be
 * decoded into passed buffers/pointers during that operation, or what the
 * values on those buffers will look like.
 *
 * Future decoding operations on the same stream will also have similar
 * undefined behavior.
 *
 * 3) When the first decoding operation fails it is OK to trust the results of
 * previous decoding operations on the same stream, as long as the caller
 * expects a failure to be possible (e.g. due to end-of-stream).
 *
 * However, this is highly discouraged because the caller should know the
 * stream size and should be coded to expect any decoding failure to be data
 * corruption due to hardware, accidental or even malicious causes, which should
 * be handled gracefully in all cases.
 *
 * In very rare situations where there are strong reasons to believe the data
 * can be trusted to be valid and non-tampered with, then the caller may assume
 * a decoding failure to be a bug (e.g. due to mismatched data types) and may
 * fail non-gracefully.
 *
 * 4) Non-zero padding bytes will cause the decoding operation to fail.
 *
 * 5) Zero bytes on string types will also cause the decoding operation to fail.
 *
 * 6) It is assumed that either the pointer to the stream buffer given by the
 * caller is 32-bit aligned or the architecture supports non-32-bit-aligned int
 * memory accesses.
 *
 * 7) The stream buffer and encoding/decoding buffers/ptrs should not overlap.
 *
 * 8) If a caller passes pointers to non-kernel memory (e.g., pointers to user
 * space or MMIO space), the computer may explode.
 */

static struct xdr_ops xdrmem_encode_ops;
static struct xdr_ops xdrmem_decode_ops;

void
xdrmem_create(XDR *xdrs, const caddr_t addr, const uint_t size,
    const enum xdr_op op)
{
	switch (op) {
		case XDR_ENCODE:
			xdrs->x_ops = &xdrmem_encode_ops;
			break;
		case XDR_DECODE:
			xdrs->x_ops = &xdrmem_decode_ops;
			break;
		default:
			xdrs->x_ops = NULL; /* Let the caller know we failed */
			return;
	}

	xdrs->x_op = op;
	xdrs->x_addr = addr;
	xdrs->x_addr_end = addr + size;

	if (xdrs->x_addr_end < xdrs->x_addr) {
		xdrs->x_ops = NULL;
	}
}
EXPORT_SYMBOL(xdrmem_create);

static bool_t
xdrmem_control(XDR *xdrs, int req, void *info)
{
	struct xdr_bytesrec *rec = (struct xdr_bytesrec *)info;

	if (req != XDR_GET_BYTES_AVAIL)
		return (FALSE);

	rec->xc_is_last_record = TRUE; /* always TRUE in xdrmem streams */
	rec->xc_num_avail = xdrs->x_addr_end - xdrs->x_addr;

	return (TRUE);
}

static bool_t
xdrmem_enc_bytes(XDR *xdrs, caddr_t cp, const uint_t cnt)
{
	uint_t size = roundup(cnt, 4);
	uint_t pad;

	if (size < cnt)
		return (FALSE); /* Integer overflow */

	if (xdrs->x_addr > xdrs->x_addr_end)
		return (FALSE);

	if (xdrs->x_addr_end - xdrs->x_addr < size)
		return (FALSE);

	memcpy(xdrs->x_addr, cp, cnt);

	xdrs->x_addr += cnt;

	pad = size - cnt;
	if (pad > 0) {
		memset(xdrs->x_addr, 0, pad);
		xdrs->x_addr += pad;
	}

	return (TRUE);
}

static bool_t
xdrmem_dec_bytes(XDR *xdrs, caddr_t cp, const uint_t cnt)
{
	static uint32_t zero = 0;
	uint_t size = roundup(cnt, 4);
	uint_t pad;

	if (size < cnt)
		return (FALSE); /* Integer overflow */

	if (xdrs->x_addr > xdrs->x_addr_end)
		return (FALSE);

	if (xdrs->x_addr_end - xdrs->x_addr < size)
		return (FALSE);

	memcpy(cp, xdrs->x_addr, cnt);
	xdrs->x_addr += cnt;

	pad = size - cnt;
	if (pad > 0) {
		/* An inverted memchr() would be useful here... */
		if (memcmp(&zero, xdrs->x_addr, pad) != 0)
			return (FALSE);

		xdrs->x_addr += pad;
	}

	return (TRUE);
}

static bool_t
xdrmem_enc_uint32(XDR *xdrs, uint32_t val)
{
	if (xdrs->x_addr + sizeof (uint32_t) > xdrs->x_addr_end)
		return (FALSE);

	*((uint32_t *)xdrs->x_addr) = cpu_to_be32(val);

	xdrs->x_addr += sizeof (uint32_t);

	return (TRUE);
}

static bool_t
xdrmem_dec_uint32(XDR *xdrs, uint32_t *val)
{
	if (xdrs->x_addr + sizeof (uint32_t) > xdrs->x_addr_end)
		return (FALSE);

	*val = be32_to_cpu(*((uint32_t *)xdrs->x_addr));

	xdrs->x_addr += sizeof (uint32_t);

	return (TRUE);
}

static bool_t
xdrmem_enc_char(XDR *xdrs, char *cp)
{
	uint32_t val;

	BUILD_BUG_ON(sizeof (char) != 1);
	val = *((unsigned char *) cp);

	return (xdrmem_enc_uint32(xdrs, val));
}

static bool_t
xdrmem_dec_char(XDR *xdrs, char *cp)
{
	uint32_t val;

	BUILD_BUG_ON(sizeof (char) != 1);

	if (!xdrmem_dec_uint32(xdrs, &val))
		return (FALSE);

	/*
	 * If any of the 3 other bytes are non-zero then val will be greater
	 * than 0xff and we fail because according to the RFC, this block does
	 * not have a char encoded in it.
	 */
	if (val > 0xff)
		return (FALSE);

	*((unsigned char *) cp) = val;

	return (TRUE);
}

static bool_t
xdrmem_enc_ushort(XDR *xdrs, unsigned short *usp)
{
	BUILD_BUG_ON(sizeof (unsigned short) != 2);

	return (xdrmem_enc_uint32(xdrs, *usp));
}

static bool_t
xdrmem_dec_ushort(XDR *xdrs, unsigned short *usp)
{
	uint32_t val;

	BUILD_BUG_ON(sizeof (unsigned short) != 2);

	if (!xdrmem_dec_uint32(xdrs, &val))
		return (FALSE);

	/*
	 * Short ints are not in the RFC, but we assume similar logic as in
	 * xdrmem_dec_char().
	 */
	if (val > 0xffff)
		return (FALSE);

	*usp = val;

	return (TRUE);
}

static bool_t
xdrmem_enc_uint(XDR *xdrs, unsigned *up)
{
	BUILD_BUG_ON(sizeof (unsigned) != 4);

	return (xdrmem_enc_uint32(xdrs, *up));
}

static bool_t
xdrmem_dec_uint(XDR *xdrs, unsigned *up)
{
	BUILD_BUG_ON(sizeof (unsigned) != 4);

	return (xdrmem_dec_uint32(xdrs, (uint32_t *)up));
}

static bool_t
xdrmem_enc_ulonglong(XDR *xdrs, u_longlong_t *ullp)
{
	BUILD_BUG_ON(sizeof (u_longlong_t) != 8);

	if (!xdrmem_enc_uint32(xdrs, *ullp >> 32))
		return (FALSE);

	return (xdrmem_enc_uint32(xdrs, *ullp & 0xffffffff));
}

static bool_t
xdrmem_dec_ulonglong(XDR *xdrs, u_longlong_t *ullp)
{
	uint32_t low, high;

	BUILD_BUG_ON(sizeof (u_longlong_t) != 8);

	if (!xdrmem_dec_uint32(xdrs, &high))
		return (FALSE);
	if (!xdrmem_dec_uint32(xdrs, &low))
		return (FALSE);

	*ullp = ((u_longlong_t)high << 32) | low;

	return (TRUE);
}

static bool_t
xdr_enc_array(XDR *xdrs, caddr_t *arrp, uint_t *sizep, const uint_t maxsize,
    const uint_t elsize, const xdrproc_t elproc)
{
	uint_t i;
	caddr_t addr = *arrp;

	if (*sizep > maxsize || *sizep > UINT_MAX / elsize)
		return (FALSE);

	if (!xdrmem_enc_uint(xdrs, sizep))
		return (FALSE);

	for (i = 0; i < *sizep; i++) {
		if (!elproc(xdrs, addr))
			return (FALSE);
		addr += elsize;
	}

	return (TRUE);
}

static bool_t
xdr_dec_array(XDR *xdrs, caddr_t *arrp, uint_t *sizep, const uint_t maxsize,
    const uint_t elsize, const xdrproc_t elproc)
{
	uint_t i, size;
	bool_t alloc = FALSE;
	caddr_t addr;

	if (!xdrmem_dec_uint(xdrs, sizep))
		return (FALSE);

	size = *sizep;

	if (size > maxsize || size > UINT_MAX / elsize)
		return (FALSE);

	/*
	 * The Solaris man page says: "If *arrp is NULL when decoding,
	 * xdr_array() allocates memory and *arrp points to it".
	 */
	if (*arrp == NULL) {
		BUILD_BUG_ON(sizeof (uint_t) > sizeof (size_t));

		*arrp = kmem_alloc(size * elsize, KM_NOSLEEP);
		if (*arrp == NULL)
			return (FALSE);

		alloc = TRUE;
	}

	addr = *arrp;

	for (i = 0; i < size; i++) {
		if (!elproc(xdrs, addr)) {
			if (alloc)
				kmem_free(*arrp, size * elsize);
			return (FALSE);
		}
		addr += elsize;
	}

	return (TRUE);
}

static bool_t
xdr_enc_string(XDR *xdrs, char **sp, const uint_t maxsize)
{
	size_t slen = strlen(*sp);
	uint_t len;

	if (slen > maxsize)
		return (FALSE);

	len = slen;

	if (!xdrmem_enc_uint(xdrs, &len))
		return (FALSE);

	return (xdrmem_enc_bytes(xdrs, *sp, len));
}

static bool_t
xdr_dec_string(XDR *xdrs, char **sp, const uint_t maxsize)
{
	uint_t size;
	bool_t alloc = FALSE;

	if (!xdrmem_dec_uint(xdrs, &size))
		return (FALSE);

	if (size > maxsize || size > UINT_MAX - 1)
		return (FALSE);

	/*
	 * Solaris man page: "If *sp is NULL when decoding, xdr_string()
	 * allocates memory and *sp points to it".
	 */
	if (*sp == NULL) {
		BUILD_BUG_ON(sizeof (uint_t) > sizeof (size_t));

		*sp = kmem_alloc(size + 1, KM_NOSLEEP);
		if (*sp == NULL)
			return (FALSE);

		alloc = TRUE;
	}

	if (!xdrmem_dec_bytes(xdrs, *sp, size))
		goto fail;

	if (memchr(*sp, 0, size) != NULL)
		goto fail;

	(*sp)[size] = '\0';

	return (TRUE);

fail:
	if (alloc)
		kmem_free(*sp, size + 1);

	return (FALSE);
}

static struct xdr_ops xdrmem_encode_ops = {
	.xdr_control		= xdrmem_control,
	.xdr_char		= xdrmem_enc_char,
	.xdr_u_short		= xdrmem_enc_ushort,
	.xdr_u_int		= xdrmem_enc_uint,
	.xdr_u_longlong_t	= xdrmem_enc_ulonglong,
	.xdr_opaque		= xdrmem_enc_bytes,
	.xdr_string		= xdr_enc_string,
	.xdr_array		= xdr_enc_array
};

static struct xdr_ops xdrmem_decode_ops = {
	.xdr_control		= xdrmem_control,
	.xdr_char		= xdrmem_dec_char,
	.xdr_u_short		= xdrmem_dec_ushort,
	.xdr_u_int		= xdrmem_dec_uint,
	.xdr_u_longlong_t	= xdrmem_dec_ulonglong,
	.xdr_opaque		= xdrmem_dec_bytes,
	.xdr_string		= xdr_dec_string,
	.xdr_array		= xdr_dec_array
};
