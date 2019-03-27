/*
   FastLZ - lightning-fast lossless compression library

   Copyright (C) 2007 Ariya Hidayat (ariya@kde.org)
   Copyright (C) 2006 Ariya Hidayat (ariya@kde.org)
   Copyright (C) 2005 Ariya Hidayat (ariya@kde.org)

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
   */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "osdep.h"
#include "cudbg.h"
#include "cudbg_lib_common.h"
#include "fastlz.h"

static unsigned char sixpack_magic[8] = {137, '6', 'P', 'K', 13, 10, 26, 10};

#define CUDBG_BLOCK_SIZE      (63*1024)
#define CUDBG_CHUNK_BUF_LEN   16
#define CUDBG_MIN_COMPR_LEN   32	/*min data length for applying compression*/

/* for Adler-32 checksum algorithm, see RFC 1950 Section 8.2 */

#define ADLER32_BASE 65521

static inline unsigned long update_adler32(unsigned long checksum,
					   const void *buf, int len)
{
	const unsigned char *ptr = (const unsigned char *)buf;
	unsigned long s1 = checksum & 0xffff;
	unsigned long s2 = (checksum >> 16) & 0xffff;

	while (len > 0) {
		unsigned k = len < 5552 ? len : 5552;
		len -= k;

		while (k >= 8) {
			s1 += *ptr++; s2 += s1;
			s1 += *ptr++; s2 += s1;
			s1 += *ptr++; s2 += s1;
			s1 += *ptr++; s2 += s1;
			s1 += *ptr++; s2 += s1;
			s1 += *ptr++; s2 += s1;
			s1 += *ptr++; s2 += s1;
			s1 += *ptr++; s2 += s1;
			k -= 8;
		}

		while (k-- > 0) {
			s1 += *ptr++; s2 += s1;
		}
		s1 = s1 % ADLER32_BASE;
		s2 = s2 % ADLER32_BASE;
	}
	return (s2 << 16) + s1;
}

int write_magic(struct cudbg_buffer *_out_buff)
{
	int rc;

	rc = write_to_buf(_out_buff->data, _out_buff->size, &_out_buff->offset,
			  sixpack_magic, 8);

	return rc;
}

int write_to_buf(void *out_buf, u32 out_buf_size, u32 *offset, void *in_buf,
		 u32 in_buf_size)
{
	int rc = 0;

	if (*offset >= out_buf_size) {
		rc = CUDBG_STATUS_OUTBUFF_OVERFLOW;
		goto err;
	}

	memcpy((char *)out_buf + *offset, in_buf, in_buf_size);
	*offset = *offset + in_buf_size;

err:
	return rc;
}

int read_from_buf(void *in_buf, u32 in_buf_size, u32 *offset, void *out_buf,
		  u32 out_buf_size)
{
	if (in_buf_size - *offset < out_buf_size)
		return 0;

	memcpy((char *)out_buf, (char *)in_buf + *offset, out_buf_size);
	*offset =  *offset + out_buf_size;
	return out_buf_size;
}

int write_chunk_header(struct cudbg_buffer *_outbuf, int id, int options,
		       unsigned long size, unsigned long checksum,
		       unsigned long extra)
{
	unsigned char buffer[CUDBG_CHUNK_BUF_LEN];
	int rc = 0;

	buffer[0] = id & 255;
	buffer[1] = (unsigned char)(id >> 8);
	buffer[2] = options & 255;
	buffer[3] = (unsigned char)(options >> 8);
	buffer[4] = size & 255;
	buffer[5] = (size >> 8) & 255;
	buffer[6] = (size >> 16) & 255;
	buffer[7] = (size >> 24) & 255;
	buffer[8] = checksum & 255;
	buffer[9] = (checksum >> 8) & 255;
	buffer[10] = (checksum >> 16) & 255;
	buffer[11] = (checksum >> 24) & 255;
	buffer[12] = extra & 255;
	buffer[13] = (extra >> 8) & 255;
	buffer[14] = (extra >> 16) & 255;
	buffer[15] = (extra >> 24) & 255;

	rc = write_to_buf(_outbuf->data, _outbuf->size, &_outbuf->offset,
			  buffer, 16);

	return rc;
}

int write_compression_hdr(struct cudbg_buffer *pin_buff,
			  struct cudbg_buffer *pout_buff)
{
	struct cudbg_buffer tmp_buffer;
	unsigned long fsize = pin_buff->size;
	unsigned char *buffer;
	unsigned long checksum;
	int rc;
	char *shown_name = "abc";

	/* Always release inner scratch buffer, before releasing outer. */
	rc = get_scratch_buff(pout_buff, 10, &tmp_buffer);

	if (rc)
		goto err;

	buffer = (unsigned char *)tmp_buffer.data;

	rc = write_magic(pout_buff);

	if (rc)
		goto err1;

	/* chunk for File Entry */
	buffer[0] = fsize & 255;
	buffer[1] = (fsize >> 8) & 255;
	buffer[2] = (fsize >> 16) & 255;
	buffer[3] = (fsize >> 24) & 255;
	buffer[4] = 0;
	buffer[5] = 0;
	buffer[6] = 0;
	buffer[7] = 0;
	buffer[8] = (strlen(shown_name)+1) & 255;
	buffer[9] = (unsigned char)((strlen(shown_name)+1) >> 8);
	checksum = 1L;
	checksum = update_adler32(checksum, buffer, 10);
	checksum = update_adler32(checksum, shown_name,
				  (int)strlen(shown_name)+1);

	rc = write_chunk_header(pout_buff, 1, 0,
				10+(unsigned long)strlen(shown_name)+1,
				checksum, 0);

	if (rc)
		goto err1;

	rc = write_to_buf(pout_buff->data, pout_buff->size,
			  &(pout_buff->offset), buffer, 10);

	if (rc)
		goto err1;

	rc = write_to_buf(pout_buff->data, pout_buff->size,
			   &(pout_buff->offset), shown_name,
			   (u32)strlen(shown_name)+1);

	if (rc)
		goto err1;

err1:
	release_scratch_buff(&tmp_buffer, pout_buff);
err:
	return rc;
}

int compress_buff(struct cudbg_buffer *pin_buff, struct cudbg_buffer *pout_buff)
{
	struct cudbg_buffer tmp_buffer;
	struct cudbg_hdr *cudbg_hdr;
	unsigned long checksum;
	unsigned char *result;
	unsigned int bytes_read;
	int chunk_size, level = 2, rc = 0;
	int compress_method = 1;

	bytes_read = pin_buff->size;
	rc = get_scratch_buff(pout_buff, CUDBG_BLOCK_SIZE, &tmp_buffer);

	if (rc)
		goto err;

	result = (unsigned char *)tmp_buffer.data;

	if (bytes_read < 32)
		compress_method = 0;

	cudbg_hdr = (struct cudbg_hdr *)  pout_buff->data;

	switch (compress_method) {
	case 1:
		chunk_size = fastlz_compress_level(level, pin_buff->data,
						   bytes_read, result);

		checksum = update_adler32(1L, result, chunk_size);

		if ((chunk_size > 62000) && (cudbg_hdr->reserved[7] < (u32)
		    chunk_size))   /* 64512 */
			cudbg_hdr->reserved[7] = (u32) chunk_size;

		rc = write_chunk_header(pout_buff, 17, 1, chunk_size, checksum,
					bytes_read);

		if (rc)
			goto err_put_buff;

		rc = write_to_buf(pout_buff->data, pout_buff->size,
				  &pout_buff->offset, result, chunk_size);

		if (rc)
			goto err_put_buff;

		break;

		/* uncompressed, also fallback method */
	case 0:
	default:
		checksum = update_adler32(1L, pin_buff->data, bytes_read);

		rc = write_chunk_header(pout_buff, 17, 0, bytes_read, checksum,
					bytes_read);

		if (rc)
			goto err_put_buff;

		rc = write_to_buf(pout_buff->data, pout_buff->size,
				  &pout_buff->offset, pin_buff->data,
				  bytes_read);
		if (rc)
			goto err_put_buff;

		break;
	}

err_put_buff:
	release_scratch_buff(&tmp_buffer, pout_buff);
err:
	return rc;
}

/* return non-zero if magic sequence is detected */
/* warning: reset the read pointer to the beginning of the file */
int detect_magic(struct cudbg_buffer *_c_buff)
{
	unsigned char buffer[8];
	size_t bytes_read;
	int c;

	bytes_read = read_from_buf(_c_buff->data, _c_buff->size,
				   &_c_buff->offset, buffer, 8);

	if (bytes_read < 8)
		return 0;

	for (c = 0; c < 8; c++)
		if (buffer[c] != sixpack_magic[c])
			return 0;

	return -1;
}

static inline unsigned long readU16(const unsigned char *ptr)
{
	return ptr[0]+(ptr[1]<<8);
}

static inline unsigned long readU32(const unsigned char *ptr)
{
	return ptr[0]+(ptr[1]<<8)+(ptr[2]<<16)+(ptr[3]<<24);
}

int read_chunk_header(struct cudbg_buffer *pc_buff, int *pid, int *poptions,
		      unsigned long *psize, unsigned long *pchecksum,
		      unsigned long *pextra)
{
	unsigned char buffer[CUDBG_CHUNK_BUF_LEN];
	int byte_r = read_from_buf(pc_buff->data, pc_buff->size,
				   &pc_buff->offset, buffer, 16);
	if (byte_r == 0)
		return 0;

	*pid = readU16(buffer) & 0xffff;
	*poptions = readU16(buffer+2) & 0xffff;
	*psize = readU32(buffer+4) & 0xffffffff;
	*pchecksum = readU32(buffer+8) & 0xffffffff;
	*pextra = readU32(buffer+12) & 0xffffffff;
	return 0;
}

int validate_buffer(struct cudbg_buffer *compressed_buffer)
{
	if (!detect_magic(compressed_buffer))
		return CUDBG_STATUS_INVALID_BUFF;

	return 0;
}

int decompress_buffer(struct cudbg_buffer *pc_buff,
		      struct cudbg_buffer *pd_buff)
{
	struct cudbg_buffer tmp_compressed_buffer;
	struct cudbg_buffer tmp_decompressed_buffer;
	unsigned char *compressed_buffer;
	unsigned char *decompressed_buffer;
	unsigned char buffer[CUDBG_MIN_COMPR_LEN];
	unsigned long chunk_size;
	unsigned long chunk_checksum;
	unsigned long chunk_extra;
	unsigned long checksum;
	unsigned long total_extracted = 0;
	unsigned long r;
	unsigned long remaining;
	unsigned long bytes_read;
	u32 decompressed_size = 0;
	int chunk_id, chunk_options, rc;

	if (pd_buff->size < 2 * CUDBG_BLOCK_SIZE)
		return CUDBG_STATUS_SMALL_BUFF;

	rc = get_scratch_buff(pd_buff, CUDBG_BLOCK_SIZE,
			      &tmp_compressed_buffer);

	if (rc)
		goto err_cbuff;

	rc = get_scratch_buff(pd_buff, CUDBG_BLOCK_SIZE,
			      &tmp_decompressed_buffer);
	if (rc)
		goto err_dcbuff;

	compressed_buffer = (unsigned char *)tmp_compressed_buffer.data;
	decompressed_buffer = (unsigned char *)tmp_decompressed_buffer.data;

	/* main loop */

	for (;;) {
		if (pc_buff->offset > pc_buff->size)
			break;

		rc =  read_chunk_header(pc_buff, &chunk_id, &chunk_options,
					&chunk_size, &chunk_checksum,
					&chunk_extra);
		if (rc != 0)
			break;

		/* skip 8+16 */
		if ((chunk_id == 1) && (chunk_size > 10) &&
		    (chunk_size < CUDBG_BLOCK_SIZE)) {

			bytes_read = read_from_buf(pc_buff->data, pc_buff->size,
						   &pc_buff->offset, buffer,
						   chunk_size);

			if (bytes_read == 0)
				return 0;

			checksum = update_adler32(1L, buffer, chunk_size);
			if (checksum != chunk_checksum)
				return CUDBG_STATUS_CHKSUM_MISSMATCH;

			decompressed_size = (u32)readU32(buffer);

			if (pd_buff->size < decompressed_size) {

				pd_buff->size = 2 * CUDBG_BLOCK_SIZE +
						decompressed_size;
				pc_buff->offset -= chunk_size + 16;
				return CUDBG_STATUS_SMALL_BUFF;
			}
			total_extracted = 0;

		}

		if (chunk_size > CUDBG_BLOCK_SIZE) {
			/* Release old allocated memory */
			release_scratch_buff(&tmp_decompressed_buffer, pd_buff);
			release_scratch_buff(&tmp_compressed_buffer, pd_buff);

			/* allocate new memory with chunk_size size */
			rc = get_scratch_buff(pd_buff, chunk_size,
					      &tmp_compressed_buffer);
			if (rc)
				goto err_cbuff;

			rc = get_scratch_buff(pd_buff, chunk_size,
					      &tmp_decompressed_buffer);
			if (rc)
				goto err_dcbuff;

			compressed_buffer = (unsigned char *)tmp_compressed_buffer.data;
			decompressed_buffer = (unsigned char *)tmp_decompressed_buffer.data;
		}

		if ((chunk_id == 17) && decompressed_size) {
			/* uncompressed */
			switch (chunk_options) {
				/* stored, simply copy to output */
			case 0:
				total_extracted += chunk_size;
				remaining = chunk_size;
				checksum = 1L;
				for (;;) {
					/* Write a funtion for this */
					r = (CUDBG_BLOCK_SIZE < remaining) ?
					    CUDBG_BLOCK_SIZE : remaining;
					bytes_read =
					read_from_buf(pc_buff->data,
						      pc_buff->size,
						      &pc_buff->offset, buffer,
						      r);

					if (bytes_read == 0)
						return 0;

					write_to_buf(pd_buff->data,
						     pd_buff->size,
						     &pd_buff->offset, buffer,
						     bytes_read);
					checksum = update_adler32(checksum,
								  buffer,
								  bytes_read);
					remaining -= bytes_read;

					/* verify everything is written
					 * correctly */
					if (checksum != chunk_checksum)
						return
						CUDBG_STATUS_CHKSUM_MISSMATCH;
				}

				break;

				/* compressed using FastLZ */
			case 1:
				bytes_read = read_from_buf(pc_buff->data,
							   pc_buff->size,
							   &pc_buff->offset,
							   compressed_buffer,
							   chunk_size);

				if (bytes_read == 0)
					return 0;

				checksum = update_adler32(1L, compressed_buffer,
							  chunk_size);
				total_extracted += chunk_extra;

				/* verify that the chunk data is correct */
				if (checksum != chunk_checksum) {
					return CUDBG_STATUS_CHKSUM_MISSMATCH;
				} else {
					/* decompress and verify */
					remaining =
					fastlz_decompress(compressed_buffer,
							  chunk_size,
							  decompressed_buffer,
							  chunk_extra);

					if (remaining != chunk_extra) {
						rc =
						CUDBG_STATUS_DECOMPRESS_FAIL;
						goto err;
					} else {
						write_to_buf(pd_buff->data,
							     pd_buff->size,
							     &pd_buff->offset,
							     decompressed_buffer,
							     chunk_extra);
					}
				}
				break;

			default:
				break;
			}

		}

	}

err:
	release_scratch_buff(&tmp_decompressed_buffer, pd_buff);
err_dcbuff:
	release_scratch_buff(&tmp_compressed_buffer, pd_buff);

err_cbuff:
	return rc;
}

