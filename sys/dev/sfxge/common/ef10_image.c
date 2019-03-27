/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017-2018 Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "efx.h"
#include "efx_impl.h"

#if EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2

#if EFSYS_OPT_IMAGE_LAYOUT

/*
 * Utility routines to support limited parsing of ASN.1 tags. This is not a
 * general purpose ASN.1 parser, but is sufficient to locate the required
 * objects in a signed image with CMS headers.
 */

/* DER encodings for ASN.1 tags (see ITU-T X.690) */
#define	ASN1_TAG_INTEGER	    (0x02)
#define	ASN1_TAG_OCTET_STRING	    (0x04)
#define	ASN1_TAG_OBJ_ID		    (0x06)
#define	ASN1_TAG_SEQUENCE	    (0x30)
#define	ASN1_TAG_SET		    (0x31)

#define	ASN1_TAG_IS_PRIM(tag)	    ((tag & 0x20) == 0)

#define	ASN1_TAG_PRIM_CONTEXT(n)    (0x80 + (n))
#define	ASN1_TAG_CONS_CONTEXT(n)    (0xA0 + (n))

typedef struct efx_asn1_cursor_s {
	uint8_t		*buffer;
	uint32_t	length;

	uint8_t		tag;
	uint32_t	hdr_size;
	uint32_t	val_size;
} efx_asn1_cursor_t;


/* Parse header of DER encoded ASN.1 TLV and match tag */
static	__checkReturn	efx_rc_t
efx_asn1_parse_header_match_tag(
	__inout		efx_asn1_cursor_t	*cursor,
	__in		uint8_t			tag)
{
	efx_rc_t rc;

	if (cursor == NULL || cursor->buffer == NULL || cursor->length < 2) {
		rc = EINVAL;
		goto fail1;
	}

	cursor->tag = cursor->buffer[0];
	if (cursor->tag != tag) {
		/* Tag not matched */
		rc = ENOENT;
		goto fail2;
	}

	if ((cursor->tag & 0x1F) == 0x1F) {
		/* Long tag format not used in CMS syntax */
		rc = EINVAL;
		goto fail3;
	}

	if ((cursor->buffer[1] & 0x80) == 0) {
		/* Short form: length is 0..127 */
		cursor->hdr_size = 2;
		cursor->val_size = cursor->buffer[1];
	} else {
		/* Long form: length encoded as [0x80+nbytes][length bytes] */
		uint32_t nbytes = cursor->buffer[1] & 0x7F;
		uint32_t offset;

		if (nbytes == 0) {
			/* Indefinite length not allowed in DER encoding */
			rc = EINVAL;
			goto fail4;
		}
		if (2 + nbytes > cursor->length) {
			/* Header length overflows image buffer */
			rc = EINVAL;
			goto fail6;
		}
		if (nbytes > sizeof (uint32_t)) {
			/* Length encoding too big */
			rc = E2BIG;
			goto fail5;
		}
		cursor->hdr_size = 2 + nbytes;
		cursor->val_size = 0;
		for (offset = 2; offset < cursor->hdr_size; offset++) {
			cursor->val_size =
			    (cursor->val_size << 8) | cursor->buffer[offset];
		}
	}

	if ((cursor->hdr_size + cursor->val_size) > cursor->length) {
		/* Length overflows image buffer */
		rc = E2BIG;
		goto fail7;
	}

	return (0);

fail7:
	EFSYS_PROBE(fail7);
fail6:
	EFSYS_PROBE(fail6);
fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/* Enter nested ASN.1 TLV (contained in value of current TLV) */
static	__checkReturn	efx_rc_t
efx_asn1_enter_tag(
	__inout		efx_asn1_cursor_t	*cursor,
	__in		uint8_t			tag)
{
	efx_rc_t rc;

	if (cursor == NULL) {
		rc = EINVAL;
		goto fail1;
	}

	if (ASN1_TAG_IS_PRIM(tag)) {
		/* Cannot enter a primitive tag */
		rc = ENOTSUP;
		goto fail2;
	}
	rc = efx_asn1_parse_header_match_tag(cursor, tag);
	if (rc != 0) {
		/* Invalid TLV or wrong tag */
		goto fail3;
	}

	/* Limit cursor range to nested TLV */
	cursor->buffer += cursor->hdr_size;
	cursor->length = cursor->val_size;

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * Check that the current ASN.1 TLV matches the given tag and value.
 * Advance cursor to next TLV on a successful match.
 */
static	__checkReturn	efx_rc_t
efx_asn1_match_tag_value(
	__inout		efx_asn1_cursor_t	*cursor,
	__in		uint8_t			tag,
	__in		const void		*valp,
	__in		uint32_t		val_size)
{
	efx_rc_t rc;

	if (cursor == NULL) {
		rc = EINVAL;
		goto fail1;
	}
	rc = efx_asn1_parse_header_match_tag(cursor, tag);
	if (rc != 0) {
		/* Invalid TLV or wrong tag */
		goto fail2;
	}
	if (cursor->val_size != val_size) {
		/* Value size is different */
		rc = EINVAL;
		goto fail3;
	}
	if (memcmp(cursor->buffer + cursor->hdr_size, valp, val_size) != 0) {
		/* Value content is different */
		rc = EINVAL;
		goto fail4;
	}
	cursor->buffer += cursor->hdr_size + cursor->val_size;
	cursor->length -= cursor->hdr_size + cursor->val_size;

	return (0);

fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/* Advance cursor to next TLV */
static	__checkReturn	efx_rc_t
efx_asn1_skip_tag(
	__inout		efx_asn1_cursor_t	*cursor,
	__in		uint8_t			tag)
{
	efx_rc_t rc;

	if (cursor == NULL) {
		rc = EINVAL;
		goto fail1;
	}

	rc = efx_asn1_parse_header_match_tag(cursor, tag);
	if (rc != 0) {
		/* Invalid TLV or wrong tag */
		goto fail2;
	}
	cursor->buffer += cursor->hdr_size + cursor->val_size;
	cursor->length -= cursor->hdr_size + cursor->val_size;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/* Return pointer to value octets and value size from current TLV */
static	__checkReturn	efx_rc_t
efx_asn1_get_tag_value(
	__inout		efx_asn1_cursor_t	*cursor,
	__in		uint8_t			tag,
	__out		uint8_t			**valp,
	__out		uint32_t		*val_sizep)
{
	efx_rc_t rc;

	if (cursor == NULL || valp == NULL || val_sizep == NULL) {
		rc = EINVAL;
		goto fail1;
	}

	rc = efx_asn1_parse_header_match_tag(cursor, tag);
	if (rc != 0) {
		/* Invalid TLV or wrong tag */
		goto fail2;
	}
	*valp = cursor->buffer + cursor->hdr_size;
	*val_sizep = cursor->val_size;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


/*
 * Utility routines for parsing CMS headers (see RFC2315, PKCS#7)
 */

/* OID 1.2.840.113549.1.7.2 */
static const uint8_t PKCS7_SignedData[] =
{ 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x02 };

/* OID 1.2.840.113549.1.7.1 */
static const uint8_t PKCS7_Data[] =
{ 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x01 };

/* SignedData structure version */
static const uint8_t SignedData_Version[] =
{ 0x03 };

/*
 * Check for a valid image in signed image format. This uses CMS syntax
 * (see RFC2315, PKCS#7) to provide signatures, and certificates required
 * to validate the signatures. The encapsulated content is in unsigned image
 * format (reflash header, image code, trailer checksum).
 */
static	__checkReturn	efx_rc_t
efx_check_signed_image_header(
	__in		void		*bufferp,
	__in		uint32_t	buffer_size,
	__out		uint32_t	*content_offsetp,
	__out		uint32_t	*content_lengthp)
{
	efx_asn1_cursor_t cursor;
	uint8_t *valp;
	uint32_t val_size;
	efx_rc_t rc;

	if (content_offsetp == NULL || content_lengthp == NULL) {
		rc = EINVAL;
		goto fail1;
	}
	cursor.buffer = (uint8_t *)bufferp;
	cursor.length = buffer_size;

	/* ContextInfo */
	rc = efx_asn1_enter_tag(&cursor, ASN1_TAG_SEQUENCE);
	if (rc != 0)
		goto fail2;

	/* ContextInfo.contentType */
	rc = efx_asn1_match_tag_value(&cursor, ASN1_TAG_OBJ_ID,
	    PKCS7_SignedData, sizeof (PKCS7_SignedData));
	if (rc != 0)
		goto fail3;

	/* ContextInfo.content */
	rc = efx_asn1_enter_tag(&cursor, ASN1_TAG_CONS_CONTEXT(0));
	if (rc != 0)
		goto fail4;

	/* SignedData */
	rc = efx_asn1_enter_tag(&cursor, ASN1_TAG_SEQUENCE);
	if (rc != 0)
		goto fail5;

	/* SignedData.version */
	rc = efx_asn1_match_tag_value(&cursor, ASN1_TAG_INTEGER,
	    SignedData_Version, sizeof (SignedData_Version));
	if (rc != 0)
		goto fail6;

	/* SignedData.digestAlgorithms */
	rc = efx_asn1_skip_tag(&cursor, ASN1_TAG_SET);
	if (rc != 0)
		goto fail7;

	/* SignedData.encapContentInfo */
	rc = efx_asn1_enter_tag(&cursor, ASN1_TAG_SEQUENCE);
	if (rc != 0)
		goto fail8;

	/* SignedData.encapContentInfo.econtentType */
	rc = efx_asn1_match_tag_value(&cursor, ASN1_TAG_OBJ_ID,
	    PKCS7_Data, sizeof (PKCS7_Data));
	if (rc != 0)
		goto fail9;

	/* SignedData.encapContentInfo.econtent */
	rc = efx_asn1_enter_tag(&cursor, ASN1_TAG_CONS_CONTEXT(0));
	if (rc != 0)
		goto fail10;

	/*
	 * The octet string contains the image header, image code bytes and
	 * image trailer CRC (same as unsigned image layout).
	 */
	valp = NULL;
	val_size = 0;
	rc = efx_asn1_get_tag_value(&cursor, ASN1_TAG_OCTET_STRING,
	    &valp, &val_size);
	if (rc != 0)
		goto fail11;

	if ((valp == NULL) || (val_size == 0)) {
		rc = EINVAL;
		goto fail12;
	}
	if (valp < (uint8_t *)bufferp) {
		rc = EINVAL;
		goto fail13;
	}
	if ((valp + val_size) > ((uint8_t *)bufferp + buffer_size)) {
		rc = EINVAL;
		goto fail14;
	}

	*content_offsetp = (uint32_t)(valp - (uint8_t *)bufferp);
	*content_lengthp = val_size;

	return (0);

fail14:
	EFSYS_PROBE(fail14);
fail13:
	EFSYS_PROBE(fail13);
fail12:
	EFSYS_PROBE(fail12);
fail11:
	EFSYS_PROBE(fail11);
fail10:
	EFSYS_PROBE(fail10);
fail9:
	EFSYS_PROBE(fail9);
fail8:
	EFSYS_PROBE(fail8);
fail7:
	EFSYS_PROBE(fail7);
fail6:
	EFSYS_PROBE(fail6);
fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_check_unsigned_image(
	__in		void		*bufferp,
	__in		uint32_t	buffer_size)
{
	efx_image_header_t *header;
	efx_image_trailer_t *trailer;
	uint32_t crc;
	efx_rc_t rc;

	EFX_STATIC_ASSERT(sizeof (*header) == EFX_IMAGE_HEADER_SIZE);
	EFX_STATIC_ASSERT(sizeof (*trailer) == EFX_IMAGE_TRAILER_SIZE);

	/* Must have at least enough space for required image header fields */
	if (buffer_size < (EFX_FIELD_OFFSET(efx_image_header_t, eih_size) +
		sizeof (header->eih_size))) {
		rc = ENOSPC;
		goto fail1;
	}
	header = (efx_image_header_t *)bufferp;

	if (header->eih_magic != EFX_IMAGE_HEADER_MAGIC) {
		rc = EINVAL;
		goto fail2;
	}

	/*
	 * Check image header version is same or higher than lowest required
	 * version.
	 */
	if (header->eih_version < EFX_IMAGE_HEADER_VERSION) {
		rc = EINVAL;
		goto fail3;
	}

	/* Buffer must have space for image header, code and image trailer. */
	if (buffer_size < (header->eih_size + header->eih_code_size +
		EFX_IMAGE_TRAILER_SIZE)) {
		rc = ENOSPC;
		goto fail4;
	}

	/* Check CRC from image buffer matches computed CRC. */
	trailer = (efx_image_trailer_t *)((uint8_t *)header +
	    header->eih_size + header->eih_code_size);

	crc = efx_crc32_calculate(0, (uint8_t *)header,
	    (header->eih_size + header->eih_code_size));

	if (trailer->eit_crc != crc) {
		rc = EINVAL;
		goto fail5;
	}

	return (0);

fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_check_reflash_image(
	__in		void			*bufferp,
	__in		uint32_t		buffer_size,
	__out		efx_image_info_t	*infop)
{
	efx_image_format_t format = EFX_IMAGE_FORMAT_NO_IMAGE;
	uint32_t image_offset;
	uint32_t image_size;
	void *imagep;
	efx_rc_t rc;


	EFSYS_ASSERT(infop != NULL);
	if (infop == NULL) {
		rc = EINVAL;
		goto fail1;
	}
	memset(infop, 0, sizeof (*infop));

	if (bufferp == NULL || buffer_size == 0) {
		rc = EINVAL;
		goto fail2;
	}

	/*
	 * Check if the buffer contains an image in signed format, and if so,
	 * locate the image header.
	 */
	rc = efx_check_signed_image_header(bufferp, buffer_size,
	    &image_offset, &image_size);
	if (rc == 0) {
		/*
		 * Buffer holds signed image format. Check that the encapsulated
		 * content is in unsigned image format.
		 */
		format = EFX_IMAGE_FORMAT_SIGNED;
	} else {
		/* Check if the buffer holds image in unsigned image format */
		format = EFX_IMAGE_FORMAT_UNSIGNED;
		image_offset = 0;
		image_size = buffer_size;
	}
	if (image_offset + image_size > buffer_size) {
		rc = E2BIG;
		goto fail3;
	}
	imagep = (uint8_t *)bufferp + image_offset;

	/* Check unsigned image layout (image header, code, image trailer) */
	rc = efx_check_unsigned_image(imagep, image_size);
	if (rc != 0)
		goto fail4;

	/* Return image details */
	infop->eii_format = format;
	infop->eii_imagep = bufferp;
	infop->eii_image_size = buffer_size;
	infop->eii_headerp = (efx_image_header_t *)imagep;

	return (0);

fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
	infop->eii_format = EFX_IMAGE_FORMAT_INVALID;
	infop->eii_imagep = NULL;
	infop->eii_image_size = 0;

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_build_signed_image_write_buffer(
	__out_bcount(buffer_size)
			uint8_t			*bufferp,
	__in		uint32_t		buffer_size,
	__in		efx_image_info_t	*infop,
	__out		efx_image_header_t	**headerpp)
{
	signed_image_chunk_hdr_t chunk_hdr;
	uint32_t hdr_offset;
	struct {
		uint32_t offset;
		uint32_t size;
	} cms_header, image_header, code, image_trailer, signature;
	efx_rc_t rc;

	EFSYS_ASSERT((infop != NULL) && (headerpp != NULL));

	if ((bufferp == NULL) || (buffer_size == 0) ||
	    (infop == NULL) || (headerpp == NULL)) {
		/* Invalid arguments */
		rc = EINVAL;
		goto fail1;
	}
	if ((infop->eii_format != EFX_IMAGE_FORMAT_SIGNED) ||
	    (infop->eii_imagep == NULL) ||
	    (infop->eii_headerp == NULL) ||
	    ((uint8_t *)infop->eii_headerp < (uint8_t *)infop->eii_imagep) ||
	    (infop->eii_image_size < EFX_IMAGE_HEADER_SIZE) ||
	    ((size_t)((uint8_t *)infop->eii_headerp - infop->eii_imagep) >
	    (infop->eii_image_size - EFX_IMAGE_HEADER_SIZE))) {
		/* Invalid image info */
		rc = EINVAL;
		goto fail2;
	}

	/* Locate image chunks in original signed image */
	cms_header.offset = 0;
	cms_header.size =
	    (uint32_t)((uint8_t *)infop->eii_headerp - infop->eii_imagep);
	if ((cms_header.size > buffer_size) ||
	    (cms_header.offset > (buffer_size - cms_header.size))) {
		rc = EINVAL;
		goto fail3;
	}

	image_header.offset = cms_header.offset + cms_header.size;
	image_header.size = infop->eii_headerp->eih_size;
	if ((image_header.size > buffer_size) ||
	    (image_header.offset > (buffer_size - image_header.size))) {
		rc = EINVAL;
		goto fail4;
	}

	code.offset = image_header.offset + image_header.size;
	code.size = infop->eii_headerp->eih_code_size;
	if ((code.size > buffer_size) ||
	    (code.offset > (buffer_size - code.size))) {
		rc = EINVAL;
		goto fail5;
	}

	image_trailer.offset = code.offset + code.size;
	image_trailer.size = EFX_IMAGE_TRAILER_SIZE;
	if ((image_trailer.size > buffer_size) ||
	    (image_trailer.offset > (buffer_size - image_trailer.size))) {
		rc = EINVAL;
		goto fail6;
	}

	signature.offset = image_trailer.offset + image_trailer.size;
	signature.size = (uint32_t)(infop->eii_image_size - signature.offset);
	if ((signature.size > buffer_size) ||
	    (signature.offset > (buffer_size - signature.size))) {
		rc = EINVAL;
		goto fail7;
	}

	EFSYS_ASSERT3U(infop->eii_image_size, ==, cms_header.size +
	    image_header.size + code.size + image_trailer.size +
	    signature.size);

	/* BEGIN CSTYLED */
	/*
	 * Build signed image partition, inserting chunk headers.
	 *
	 *  Signed Image:                  Image in NVRAM partition:
	 *
	 *  +-----------------+            +-----------------+
	 *  | CMS header      |            |  mcfw.update    |<----+
	 *  +-----------------+            |                 |     |
	 *  | reflash header  |            +-----------------+     |
	 *  +-----------------+            | chunk header:   |-->--|-+
	 *  | mcfw.update     |            | REFLASH_TRAILER |     | |
	 *  |                 |            +-----------------+     | |
	 *  +-----------------+        +-->| CMS header      |     | |
	 *  | reflash trailer |        |   +-----------------+     | |
	 *  +-----------------+        |   | chunk header:   |->-+ | |
	 *  | signature       |        |   | REFLASH_HEADER  |   | | |
	 *  +-----------------+        |   +-----------------+   | | |
	 *                             |   | reflash header  |<--+ | |
	 *                             |   +-----------------+     | |
	 *                             |   | chunk header:   |-->--+ |
	 *                             |   | IMAGE           |       |
	 *                             |   +-----------------+       |
	 *                             |   | reflash trailer |<------+
	 *                             |   +-----------------+
	 *                             |   | chunk header:   |
	 *                             |   | SIGNATURE       |->-+
	 *                             |   +-----------------+   |
	 *                             |   | signature       |<--+
	 *                             |   +-----------------+
	 *                             |   | ...unused...    |
	 *                             |   +-----------------+
	 *                             +-<-| chunk header:   |
	 *                             >-->| CMS_HEADER      |
	 *                                 +-----------------+
	 *
	 * Each chunk header gives the partition offset and length of the image
	 * chunk's data. The image chunk data is immediately followed by the
	 * chunk header for the next chunk.
	 *
	 * The data chunk for the firmware code must be at the start of the
	 * partition (needed for the bootloader). The first chunk header in the
	 * chain (for the CMS header) is stored at the end of the partition. The
	 * chain of chunk headers maintains the same logical order of image
	 * chunks as the original signed image file. This set of constraints
	 * results in the layout used for the data chunks and chunk headers.
	 */
	/* END CSTYLED */
	memset(bufferp, 0xFF, buffer_size);

	EFX_STATIC_ASSERT(sizeof (chunk_hdr) == SIGNED_IMAGE_CHUNK_HDR_LEN);
	memset(&chunk_hdr, 0, SIGNED_IMAGE_CHUNK_HDR_LEN);

	/*
	 * CMS header
	 */
	if (buffer_size < SIGNED_IMAGE_CHUNK_HDR_LEN) {
		rc = ENOSPC;
		goto fail8;
	}
	hdr_offset = buffer_size - SIGNED_IMAGE_CHUNK_HDR_LEN;

	chunk_hdr.magic		= SIGNED_IMAGE_CHUNK_HDR_MAGIC;
	chunk_hdr.version	= SIGNED_IMAGE_CHUNK_HDR_VERSION;
	chunk_hdr.id		= SIGNED_IMAGE_CHUNK_CMS_HEADER;
	chunk_hdr.offset	= code.size + SIGNED_IMAGE_CHUNK_HDR_LEN;
	chunk_hdr.len		= cms_header.size;

	memcpy(bufferp + hdr_offset, &chunk_hdr, sizeof (chunk_hdr));

	if ((chunk_hdr.len > buffer_size) ||
	    (chunk_hdr.offset > (buffer_size - chunk_hdr.len))) {
		rc = ENOSPC;
		goto fail9;
	}
	memcpy(bufferp + chunk_hdr.offset,
	    infop->eii_imagep + cms_header.offset,
	    cms_header.size);

	/*
	 * Image header
	 */
	hdr_offset = chunk_hdr.offset + chunk_hdr.len;
	if (hdr_offset > (buffer_size - SIGNED_IMAGE_CHUNK_HDR_LEN)) {
		rc = ENOSPC;
		goto fail10;
	}
	chunk_hdr.magic		= SIGNED_IMAGE_CHUNK_HDR_MAGIC;
	chunk_hdr.version	= SIGNED_IMAGE_CHUNK_HDR_VERSION;
	chunk_hdr.id		= SIGNED_IMAGE_CHUNK_REFLASH_HEADER;
	chunk_hdr.offset	= hdr_offset + SIGNED_IMAGE_CHUNK_HDR_LEN;
	chunk_hdr.len		= image_header.size;

	memcpy(bufferp + hdr_offset, &chunk_hdr, SIGNED_IMAGE_CHUNK_HDR_LEN);

	if ((chunk_hdr.len > buffer_size) ||
	    (chunk_hdr.offset > (buffer_size - chunk_hdr.len))) {
		rc = ENOSPC;
		goto fail11;
	}
	memcpy(bufferp + chunk_hdr.offset,
	    infop->eii_imagep + image_header.offset,
	    image_header.size);

	*headerpp = (efx_image_header_t *)(bufferp + chunk_hdr.offset);

	/*
	 * Firmware code
	 */
	hdr_offset = chunk_hdr.offset + chunk_hdr.len;
	if (hdr_offset > (buffer_size - SIGNED_IMAGE_CHUNK_HDR_LEN)) {
		rc = ENOSPC;
		goto fail12;
	}
	chunk_hdr.magic		= SIGNED_IMAGE_CHUNK_HDR_MAGIC;
	chunk_hdr.version	= SIGNED_IMAGE_CHUNK_HDR_VERSION;
	chunk_hdr.id		= SIGNED_IMAGE_CHUNK_IMAGE;
	chunk_hdr.offset	= 0;
	chunk_hdr.len		= code.size;

	memcpy(bufferp + hdr_offset, &chunk_hdr, SIGNED_IMAGE_CHUNK_HDR_LEN);

	if ((chunk_hdr.len > buffer_size) ||
	    (chunk_hdr.offset > (buffer_size - chunk_hdr.len))) {
		rc = ENOSPC;
		goto fail13;
	}
	memcpy(bufferp + chunk_hdr.offset,
	    infop->eii_imagep + code.offset,
	    code.size);

	/*
	 * Image trailer (CRC)
	 */
	chunk_hdr.magic		= SIGNED_IMAGE_CHUNK_HDR_MAGIC;
	chunk_hdr.version	= SIGNED_IMAGE_CHUNK_HDR_VERSION;
	chunk_hdr.id		= SIGNED_IMAGE_CHUNK_REFLASH_TRAILER;
	chunk_hdr.offset	= hdr_offset + SIGNED_IMAGE_CHUNK_HDR_LEN;
	chunk_hdr.len		= image_trailer.size;

	hdr_offset = code.size;
	if (hdr_offset > (buffer_size - SIGNED_IMAGE_CHUNK_HDR_LEN)) {
		rc = ENOSPC;
		goto fail14;
	}

	memcpy(bufferp + hdr_offset, &chunk_hdr, SIGNED_IMAGE_CHUNK_HDR_LEN);

	if ((chunk_hdr.len > buffer_size) ||
	    (chunk_hdr.offset > (buffer_size - chunk_hdr.len))) {
		rc = ENOSPC;
		goto fail15;
	}
	memcpy((uint8_t *)bufferp + chunk_hdr.offset,
	    infop->eii_imagep + image_trailer.offset,
	    image_trailer.size);

	/*
	 * Signature
	 */
	hdr_offset = chunk_hdr.offset + chunk_hdr.len;
	if (hdr_offset > (buffer_size - SIGNED_IMAGE_CHUNK_HDR_LEN)) {
		rc = ENOSPC;
		goto fail16;
	}
	chunk_hdr.magic		= SIGNED_IMAGE_CHUNK_HDR_MAGIC;
	chunk_hdr.version	= SIGNED_IMAGE_CHUNK_HDR_VERSION;
	chunk_hdr.id		= SIGNED_IMAGE_CHUNK_SIGNATURE;
	chunk_hdr.offset	= chunk_hdr.offset + SIGNED_IMAGE_CHUNK_HDR_LEN;
	chunk_hdr.len		= signature.size;

	memcpy(bufferp + hdr_offset, &chunk_hdr, SIGNED_IMAGE_CHUNK_HDR_LEN);

	if ((chunk_hdr.len > buffer_size) ||
	    (chunk_hdr.offset > (buffer_size - chunk_hdr.len))) {
		rc = ENOSPC;
		goto fail17;
	}
	memcpy(bufferp + chunk_hdr.offset,
	    infop->eii_imagep + signature.offset,
	    signature.size);

	return (0);

fail17:
	EFSYS_PROBE(fail17);
fail16:
	EFSYS_PROBE(fail16);
fail15:
	EFSYS_PROBE(fail15);
fail14:
	EFSYS_PROBE(fail14);
fail13:
	EFSYS_PROBE(fail13);
fail12:
	EFSYS_PROBE(fail12);
fail11:
	EFSYS_PROBE(fail11);
fail10:
	EFSYS_PROBE(fail10);
fail9:
	EFSYS_PROBE(fail9);
fail8:
	EFSYS_PROBE(fail8);
fail7:
	EFSYS_PROBE(fail7);
fail6:
	EFSYS_PROBE(fail6);
fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}



#endif	/* EFSYS_OPT_IMAGE_LAYOUT */

#endif	/* EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */
