/* mpicoder.c  -  Coder for the external representation of MPIs
 * Copyright (C) 1998, 1999 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * GnuPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GnuPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <linux/bitops.h>
#include <asm-generic/bitops/count_zeros.h>
#include "mpi-internal.h"

#define MAX_EXTERN_MPI_BITS 16384

/**
 * mpi_read_raw_data - Read a raw byte stream as a positive integer
 * @xbuffer: The data to read
 * @nbytes: The amount of data to read
 */
MPI mpi_read_raw_data(const void *xbuffer, size_t nbytes)
{
	const uint8_t *buffer = xbuffer;
	int i, j;
	unsigned nbits, nlimbs;
	mpi_limb_t a;
	MPI val = NULL;

	while (nbytes > 0 && buffer[0] == 0) {
		buffer++;
		nbytes--;
	}

	nbits = nbytes * 8;
	if (nbits > MAX_EXTERN_MPI_BITS) {
		pr_info("MPI: mpi too large (%u bits)\n", nbits);
		return NULL;
	}
	if (nbytes > 0)
		nbits -= count_leading_zeros(buffer[0]);
	else
		nbits = 0;

	nlimbs = DIV_ROUND_UP(nbytes, BYTES_PER_MPI_LIMB);
	val = mpi_alloc(nlimbs);
	if (!val)
		return NULL;
	val->nbits = nbits;
	val->sign = 0;
	val->nlimbs = nlimbs;

	if (nbytes > 0) {
		i = BYTES_PER_MPI_LIMB - nbytes % BYTES_PER_MPI_LIMB;
		i %= BYTES_PER_MPI_LIMB;
		for (j = nlimbs; j > 0; j--) {
			a = 0;
			for (; i < BYTES_PER_MPI_LIMB; i++) {
				a <<= 8;
				a |= *buffer++;
			}
			i = 0;
			val->d[j - 1] = a;
		}
	}
	return val;
}
EXPORT_SYMBOL_GPL(mpi_read_raw_data);

MPI mpi_read_from_buffer(const void *xbuffer, unsigned *ret_nread)
{
	const uint8_t *buffer = xbuffer;
	int i, j;
	unsigned nbits, nbytes, nlimbs, nread = 0;
	mpi_limb_t a;
	MPI val = NULL;

	if (*ret_nread < 2)
		goto leave;
	nbits = buffer[0] << 8 | buffer[1];

	if (nbits > MAX_EXTERN_MPI_BITS) {
		pr_info("MPI: mpi too large (%u bits)\n", nbits);
		goto leave;
	}
	buffer += 2;
	nread = 2;

	nbytes = DIV_ROUND_UP(nbits, 8);
	nlimbs = DIV_ROUND_UP(nbytes, BYTES_PER_MPI_LIMB);
	val = mpi_alloc(nlimbs);
	if (!val)
		return NULL;
	i = BYTES_PER_MPI_LIMB - nbytes % BYTES_PER_MPI_LIMB;
	i %= BYTES_PER_MPI_LIMB;
	val->nbits = nbits;
	j = val->nlimbs = nlimbs;
	val->sign = 0;
	for (; j > 0; j--) {
		a = 0;
		for (; i < BYTES_PER_MPI_LIMB; i++) {
			if (++nread > *ret_nread) {
				printk
				    ("MPI: mpi larger than buffer nread=%d ret_nread=%d\n",
				     nread, *ret_nread);
				goto leave;
			}
			a <<= 8;
			a |= *buffer++;
		}
		i = 0;
		val->d[j - 1] = a;
	}

leave:
	*ret_nread = nread;
	return val;
}
EXPORT_SYMBOL_GPL(mpi_read_from_buffer);

/**
 * mpi_read_buffer() - read MPI to a bufer provided by user (msb first)
 *
 * @a:		a multi precision integer
 * @buf:	bufer to which the output will be written to. Needs to be at
 *		leaset mpi_get_size(a) long.
 * @buf_len:	size of the buf.
 * @nbytes:	receives the actual length of the data written.
 * @sign:	if not NULL, it will be set to the sign of a.
 *
 * Return:	0 on success or error code in case of error
 */
int mpi_read_buffer(MPI a, uint8_t *buf, unsigned buf_len, unsigned *nbytes,
		    int *sign)
{
	uint8_t *p;
	mpi_limb_t alimb;
	unsigned int n = mpi_get_size(a);
	int i;

	if (buf_len < n || !buf)
		return -EINVAL;

	if (sign)
		*sign = a->sign;

	if (nbytes)
		*nbytes = n;

	p = buf;

	for (i = a->nlimbs - 1; i >= 0; i--) {
		alimb = a->d[i];
#if BYTES_PER_MPI_LIMB == 4
		*p++ = alimb >> 24;
		*p++ = alimb >> 16;
		*p++ = alimb >> 8;
		*p++ = alimb;
#elif BYTES_PER_MPI_LIMB == 8
		*p++ = alimb >> 56;
		*p++ = alimb >> 48;
		*p++ = alimb >> 40;
		*p++ = alimb >> 32;
		*p++ = alimb >> 24;
		*p++ = alimb >> 16;
		*p++ = alimb >> 8;
		*p++ = alimb;
#else
#error please implement for this limb size.
#endif
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mpi_read_buffer);

/*
 * mpi_get_buffer() - Returns an allocated buffer with the MPI (msb first).
 * Caller must free the return string.
 * This function does return a 0 byte buffer with nbytes set to zero if the
 * value of A is zero.
 *
 * @a:		a multi precision integer.
 * @nbytes:	receives the length of this buffer.
 * @sign:	if not NULL, it will be set to the sign of the a.
 *
 * Return:	Pointer to MPI buffer or NULL on error
 */
void *mpi_get_buffer(MPI a, unsigned *nbytes, int *sign)
{
	uint8_t *buf, *p;
	unsigned int n;
	int ret;

	if (!nbytes)
		return NULL;

	n = mpi_get_size(a);

	if (!n)
		n++;

	buf = kmalloc(n, GFP_KERNEL);

	if (!buf)
		return NULL;

	ret = mpi_read_buffer(a, buf, n, nbytes, sign);

	if (ret) {
		kfree(buf);
		return NULL;
	}

	/* this is sub-optimal but we need to do the shift operation
	 * because the caller has to free the returned buffer */
	for (p = buf; !*p && *nbytes; p++, --*nbytes)
		;
	if (p != buf)
		memmove(buf, p, *nbytes);

	return buf;
}
EXPORT_SYMBOL_GPL(mpi_get_buffer);

/****************
 * Use BUFFER to update MPI.
 */
int mpi_set_buffer(MPI a, const void *xbuffer, unsigned nbytes, int sign)
{
	const uint8_t *buffer = xbuffer, *p;
	mpi_limb_t alimb;
	int nlimbs;
	int i;

	nlimbs = DIV_ROUND_UP(nbytes, BYTES_PER_MPI_LIMB);
	if (RESIZE_IF_NEEDED(a, nlimbs) < 0)
		return -ENOMEM;
	a->sign = sign;

	for (i = 0, p = buffer + nbytes - 1; p >= buffer + BYTES_PER_MPI_LIMB;) {
#if BYTES_PER_MPI_LIMB == 4
		alimb = (mpi_limb_t) *p--;
		alimb |= (mpi_limb_t) *p-- << 8;
		alimb |= (mpi_limb_t) *p-- << 16;
		alimb |= (mpi_limb_t) *p-- << 24;
#elif BYTES_PER_MPI_LIMB == 8
		alimb = (mpi_limb_t) *p--;
		alimb |= (mpi_limb_t) *p-- << 8;
		alimb |= (mpi_limb_t) *p-- << 16;
		alimb |= (mpi_limb_t) *p-- << 24;
		alimb |= (mpi_limb_t) *p-- << 32;
		alimb |= (mpi_limb_t) *p-- << 40;
		alimb |= (mpi_limb_t) *p-- << 48;
		alimb |= (mpi_limb_t) *p-- << 56;
#else
#error please implement for this limb size.
#endif
		a->d[i++] = alimb;
	}
	if (p >= buffer) {
#if BYTES_PER_MPI_LIMB == 4
		alimb = *p--;
		if (p >= buffer)
			alimb |= (mpi_limb_t) *p-- << 8;
		if (p >= buffer)
			alimb |= (mpi_limb_t) *p-- << 16;
		if (p >= buffer)
			alimb |= (mpi_limb_t) *p-- << 24;
#elif BYTES_PER_MPI_LIMB == 8
		alimb = (mpi_limb_t) *p--;
		if (p >= buffer)
			alimb |= (mpi_limb_t) *p-- << 8;
		if (p >= buffer)
			alimb |= (mpi_limb_t) *p-- << 16;
		if (p >= buffer)
			alimb |= (mpi_limb_t) *p-- << 24;
		if (p >= buffer)
			alimb |= (mpi_limb_t) *p-- << 32;
		if (p >= buffer)
			alimb |= (mpi_limb_t) *p-- << 40;
		if (p >= buffer)
			alimb |= (mpi_limb_t) *p-- << 48;
		if (p >= buffer)
			alimb |= (mpi_limb_t) *p-- << 56;
#else
#error please implement for this limb size.
#endif
		a->d[i++] = alimb;
	}
	a->nlimbs = i;

	if (i != nlimbs) {
		pr_emerg("MPI: mpi_set_buffer: Assertion failed (%d != %d)", i,
		       nlimbs);
		BUG();
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mpi_set_buffer);
