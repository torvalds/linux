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
#include <linux/count_zeros.h>
#include <linux/byteorder/generic.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
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
		nbits -= count_leading_zeros(buffer[0]) - (BITS_PER_LONG - 8);

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
	unsigned int nbits, nbytes;
	MPI val;

	if (*ret_nread < 2)
		return ERR_PTR(-EINVAL);
	nbits = buffer[0] << 8 | buffer[1];

	if (nbits > MAX_EXTERN_MPI_BITS) {
		pr_info("MPI: mpi too large (%u bits)\n", nbits);
		return ERR_PTR(-EINVAL);
	}

	nbytes = DIV_ROUND_UP(nbits, 8);
	if (nbytes + 2 > *ret_nread) {
		pr_info("MPI: mpi larger than buffer nbytes=%u ret_nread=%u\n",
				nbytes, *ret_nread);
		return ERR_PTR(-EINVAL);
	}

	val = mpi_read_raw_data(buffer + 2, nbytes);
	if (!val)
		return ERR_PTR(-ENOMEM);

	*ret_nread = nbytes + 2;
	return val;
}
EXPORT_SYMBOL_GPL(mpi_read_from_buffer);

static int count_lzeros(MPI a)
{
	mpi_limb_t alimb;
	int i, lzeros = 0;

	for (i = a->nlimbs - 1; i >= 0; i--) {
		alimb = a->d[i];
		if (alimb == 0) {
			lzeros += sizeof(mpi_limb_t);
		} else {
			lzeros += count_leading_zeros(alimb) / 8;
			break;
		}
	}
	return lzeros;
}

/**
 * mpi_read_buffer() - read MPI to a bufer provided by user (msb first)
 *
 * @a:		a multi precision integer
 * @buf:	bufer to which the output will be written to. Needs to be at
 *		leaset mpi_get_size(a) long.
 * @buf_len:	size of the buf.
 * @nbytes:	receives the actual length of the data written on success and
 *		the data to-be-written on -EOVERFLOW in case buf_len was too
 *		small.
 * @sign:	if not NULL, it will be set to the sign of a.
 *
 * Return:	0 on success or error code in case of error
 */
int mpi_read_buffer(MPI a, uint8_t *buf, unsigned buf_len, unsigned *nbytes,
		    int *sign)
{
	uint8_t *p;
#if BYTES_PER_MPI_LIMB == 4
	__be32 alimb;
#elif BYTES_PER_MPI_LIMB == 8
	__be64 alimb;
#else
#error please implement for this limb size.
#endif
	unsigned int n = mpi_get_size(a);
	int i, lzeros;

	if (!buf || !nbytes)
		return -EINVAL;

	if (sign)
		*sign = a->sign;

	lzeros = count_lzeros(a);

	if (buf_len < n - lzeros) {
		*nbytes = n - lzeros;
		return -EOVERFLOW;
	}

	p = buf;
	*nbytes = n - lzeros;

	for (i = a->nlimbs - 1 - lzeros / BYTES_PER_MPI_LIMB,
			lzeros %= BYTES_PER_MPI_LIMB;
		i >= 0; i--) {
#if BYTES_PER_MPI_LIMB == 4
		alimb = cpu_to_be32(a->d[i]);
#elif BYTES_PER_MPI_LIMB == 8
		alimb = cpu_to_be64(a->d[i]);
#else
#error please implement for this limb size.
#endif
		memcpy(p, (u8 *)&alimb + lzeros, BYTES_PER_MPI_LIMB - lzeros);
		p += BYTES_PER_MPI_LIMB - lzeros;
		lzeros = 0;
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
	uint8_t *buf;
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
	return buf;
}
EXPORT_SYMBOL_GPL(mpi_get_buffer);

/**
 * mpi_write_to_sgl() - Funnction exports MPI to an sgl (msb first)
 *
 * This function works in the same way as the mpi_read_buffer, but it
 * takes an sgl instead of u8 * buf.
 *
 * @a:		a multi precision integer
 * @sgl:	scatterlist to write to. Needs to be at least
 *		mpi_get_size(a) long.
 * @nbytes:	the number of bytes to write.  Leading bytes will be
 *		filled with zero.
 * @sign:	if not NULL, it will be set to the sign of a.
 *
 * Return:	0 on success or error code in case of error
 */
int mpi_write_to_sgl(MPI a, struct scatterlist *sgl, unsigned nbytes,
		     int *sign)
{
	u8 *p, *p2;
#if BYTES_PER_MPI_LIMB == 4
	__be32 alimb;
#elif BYTES_PER_MPI_LIMB == 8
	__be64 alimb;
#else
#error please implement for this limb size.
#endif
	unsigned int n = mpi_get_size(a);
	struct sg_mapping_iter miter;
	int i, x, buf_len;
	int nents;

	if (sign)
		*sign = a->sign;

	if (nbytes < n)
		return -EOVERFLOW;

	nents = sg_nents_for_len(sgl, nbytes);
	if (nents < 0)
		return -EINVAL;

	sg_miter_start(&miter, sgl, nents, SG_MITER_ATOMIC | SG_MITER_TO_SG);
	sg_miter_next(&miter);
	buf_len = miter.length;
	p2 = miter.addr;

	while (nbytes > n) {
		i = min_t(unsigned, nbytes - n, buf_len);
		memset(p2, 0, i);
		p2 += i;
		nbytes -= i;

		buf_len -= i;
		if (!buf_len) {
			sg_miter_next(&miter);
			buf_len = miter.length;
			p2 = miter.addr;
		}
	}

	for (i = a->nlimbs - 1; i >= 0; i--) {
#if BYTES_PER_MPI_LIMB == 4
		alimb = a->d[i] ? cpu_to_be32(a->d[i]) : 0;
#elif BYTES_PER_MPI_LIMB == 8
		alimb = a->d[i] ? cpu_to_be64(a->d[i]) : 0;
#else
#error please implement for this limb size.
#endif
		p = (u8 *)&alimb;

		for (x = 0; x < sizeof(alimb); x++) {
			*p2++ = *p++;
			if (!--buf_len) {
				sg_miter_next(&miter);
				buf_len = miter.length;
				p2 = miter.addr;
			}
		}
	}

	sg_miter_stop(&miter);
	return 0;
}
EXPORT_SYMBOL_GPL(mpi_write_to_sgl);

/*
 * mpi_read_raw_from_sgl() - Function allocates an MPI and populates it with
 *			     data from the sgl
 *
 * This function works in the same way as the mpi_read_raw_data, but it
 * takes an sgl instead of void * buffer. i.e. it allocates
 * a new MPI and reads the content of the sgl to the MPI.
 *
 * @sgl:	scatterlist to read from
 * @nbytes:	number of bytes to read
 *
 * Return:	Pointer to a new MPI or NULL on error
 */
MPI mpi_read_raw_from_sgl(struct scatterlist *sgl, unsigned int nbytes)
{
	struct sg_mapping_iter miter;
	unsigned int nbits, nlimbs;
	int x, j, z, lzeros, ents;
	unsigned int len;
	const u8 *buff;
	mpi_limb_t a;
	MPI val = NULL;

	ents = sg_nents_for_len(sgl, nbytes);
	if (ents < 0)
		return NULL;

	sg_miter_start(&miter, sgl, ents, SG_MITER_ATOMIC | SG_MITER_FROM_SG);

	lzeros = 0;
	len = 0;
	while (nbytes > 0) {
		while (len && !*buff) {
			lzeros++;
			len--;
			buff++;
		}

		if (len && *buff)
			break;

		sg_miter_next(&miter);
		buff = miter.addr;
		len = miter.length;

		nbytes -= lzeros;
		lzeros = 0;
	}

	miter.consumed = lzeros;
	sg_miter_stop(&miter);

	nbytes -= lzeros;
	nbits = nbytes * 8;
	if (nbits > MAX_EXTERN_MPI_BITS) {
		pr_info("MPI: mpi too large (%u bits)\n", nbits);
		return NULL;
	}

	if (nbytes > 0)
		nbits -= count_leading_zeros(*buff) - (BITS_PER_LONG - 8);

	nlimbs = DIV_ROUND_UP(nbytes, BYTES_PER_MPI_LIMB);
	val = mpi_alloc(nlimbs);
	if (!val)
		return NULL;

	val->nbits = nbits;
	val->sign = 0;
	val->nlimbs = nlimbs;

	if (nbytes == 0)
		return val;

	j = nlimbs - 1;
	a = 0;
	z = BYTES_PER_MPI_LIMB - nbytes % BYTES_PER_MPI_LIMB;
	z %= BYTES_PER_MPI_LIMB;

	while (sg_miter_next(&miter)) {
		buff = miter.addr;
		len = miter.length;

		for (x = 0; x < len; x++) {
			a <<= 8;
			a |= *buff++;
			if (((z + x + 1) % BYTES_PER_MPI_LIMB) == 0) {
				val->d[j--] = a;
				a = 0;
			}
		}
		z += x;
	}

	return val;
}
EXPORT_SYMBOL_GPL(mpi_read_raw_from_sgl);
