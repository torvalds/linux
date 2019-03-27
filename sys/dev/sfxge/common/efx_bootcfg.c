/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2016 Solarflare Communications Inc.
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

#if EFSYS_OPT_BOOTCFG

/*
 * Maximum size of BOOTCFG block across all nics as understood by SFCgPXE.
 * NOTE: This is larger than the Medford per-PF bootcfg sector.
 */
#define	BOOTCFG_MAX_SIZE 0x1000

/* Medford per-PF bootcfg sector */
#define	BOOTCFG_PER_PF   0x800
#define	BOOTCFG_PF_COUNT 16

#define	DHCP_OPT_HAS_VALUE(opt) \
	(((opt) > EFX_DHCP_PAD) && ((opt) < EFX_DHCP_END))

#define	DHCP_MAX_VALUE 255

#define	DHCP_ENCAPSULATOR(encap_opt) ((encap_opt) >> 8)
#define	DHCP_ENCAPSULATED(encap_opt) ((encap_opt) & 0xff)
#define	DHCP_IS_ENCAP_OPT(opt) DHCP_OPT_HAS_VALUE(DHCP_ENCAPSULATOR(opt))

typedef struct efx_dhcp_tag_hdr_s {
	uint8_t		tag;
	uint8_t		length;
} efx_dhcp_tag_hdr_t;

/*
 * Length calculations for tags with value field. PAD and END
 * have a fixed length of 1, with no length or value field.
 */
#define	DHCP_FULL_TAG_LENGTH(hdr) \
	(sizeof (efx_dhcp_tag_hdr_t) + (hdr)->length)

#define	DHCP_NEXT_TAG(hdr) \
	((efx_dhcp_tag_hdr_t *)(((uint8_t *)(hdr)) + \
	DHCP_FULL_TAG_LENGTH((hdr))))

#define	DHCP_CALC_TAG_LENGTH(payload_len) \
	((payload_len) + sizeof (efx_dhcp_tag_hdr_t))


/* Report the layout of bootcfg sectors in NVRAM partition. */
	__checkReturn		efx_rc_t
efx_bootcfg_sector_info(
	__in			efx_nic_t *enp,
	__in			uint32_t pf,
	__out_opt		uint32_t *sector_countp,
	__out			size_t *offsetp,
	__out			size_t *max_sizep)
{
	uint32_t count;
	size_t max_size;
	size_t offset;
	int rc;

	switch (enp->en_family) {
#if EFSYS_OPT_SIENA
	case EFX_FAMILY_SIENA:
		max_size = BOOTCFG_MAX_SIZE;
		offset = 0;
		count = 1;
		break;
#endif /* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON
	case EFX_FAMILY_HUNTINGTON:
		max_size = BOOTCFG_MAX_SIZE;
		offset = 0;
		count = 1;
		break;
#endif /* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD
	case EFX_FAMILY_MEDFORD: {
		/* Shared partition (array indexed by PF) */
		max_size = BOOTCFG_PER_PF;
		count = BOOTCFG_PF_COUNT;
		if (pf >= count) {
			rc = EINVAL;
			goto fail2;
		}
		offset = max_size * pf;
		break;
	}
#endif /* EFSYS_OPT_MEDFORD */

#if EFSYS_OPT_MEDFORD2
	case EFX_FAMILY_MEDFORD2: {
		/* Shared partition (array indexed by PF) */
		max_size = BOOTCFG_PER_PF;
		count = BOOTCFG_PF_COUNT;
		if (pf >= count) {
			rc = EINVAL;
			goto fail3;
		}
		offset = max_size * pf;
		break;
	}
#endif /* EFSYS_OPT_MEDFORD2 */

	default:
		EFSYS_ASSERT(0);
		rc = ENOTSUP;
		goto fail1;
	}
	EFSYS_ASSERT3U(max_size, <=, BOOTCFG_MAX_SIZE);

	if (sector_countp != NULL)
		*sector_countp = count;
	*offsetp = offset;
	*max_sizep = max_size;

	return (0);

#if EFSYS_OPT_MEDFORD2
fail3:
	EFSYS_PROBE(fail3);
#endif
#if EFSYS_OPT_MEDFORD
fail2:
	EFSYS_PROBE(fail2);
#endif
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}


	__checkReturn		uint8_t
efx_dhcp_csum(
	__in_bcount(size)	uint8_t const *data,
	__in			size_t size)
{
	unsigned int pos;
	uint8_t checksum = 0;

	for (pos = 0; pos < size; pos++)
		checksum += data[pos];
	return (checksum);
}

	__checkReturn		efx_rc_t
efx_dhcp_verify(
	__in_bcount(size)	uint8_t const *data,
	__in			size_t size,
	__out_opt		size_t *usedp)
{
	size_t offset = 0;
	size_t used = 0;
	efx_rc_t rc;

	/* Start parsing tags immediately after the checksum */
	for (offset = 1; offset < size; ) {
		uint8_t tag;
		uint8_t length;

		/* Consume tag */
		tag = data[offset];
		if (tag == EFX_DHCP_END) {
			offset++;
			used = offset;
			break;
		}
		if (tag == EFX_DHCP_PAD) {
			offset++;
			continue;
		}

		/* Consume length */
		if (offset + 1 >= size) {
			rc = ENOSPC;
			goto fail1;
		}
		length = data[offset + 1];

		/* Consume *length */
		if (offset + 1 + length >= size) {
			rc = ENOSPC;
			goto fail2;
		}

		offset += 2 + length;
		used = offset;
	}

	/* Checksum the entire sector, including bytes after any EFX_DHCP_END */
	if (efx_dhcp_csum(data, size) != 0) {
		rc = EINVAL;
		goto fail3;
	}

	if (usedp != NULL)
		*usedp = used;

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
 * Walk the entire tag set looking for option. The sought option may be
 * encapsulated. ENOENT indicates the walk completed without finding the
 * option. If we run out of buffer during the walk the function will return
 * ENOSPC.
 */
static	efx_rc_t
efx_dhcp_walk_tags(
	__deref_inout	uint8_t **tagpp,
	__inout		size_t *buffer_sizep,
	__in		uint16_t opt)
{
	efx_rc_t rc = 0;
	boolean_t is_encap = B_FALSE;

	if (DHCP_IS_ENCAP_OPT(opt)) {
		/*
		 * Look for the encapsulator and, if found, limit ourselves
		 * to its payload. If it's not found then the entire tag
		 * cannot be found, so the encapsulated opt search is
		 * skipped.
		 */
		rc = efx_dhcp_walk_tags(tagpp, buffer_sizep,
		    DHCP_ENCAPSULATOR(opt));
		if (rc == 0) {
			*buffer_sizep = ((efx_dhcp_tag_hdr_t *)*tagpp)->length;
			(*tagpp) += sizeof (efx_dhcp_tag_hdr_t);
		}
		opt = DHCP_ENCAPSULATED(opt);
		is_encap = B_TRUE;
	}

	EFSYS_ASSERT(!DHCP_IS_ENCAP_OPT(opt));

	while (rc == 0) {
		size_t size;

		if (*buffer_sizep == 0) {
			rc = ENOSPC;
			goto fail1;
		}

		if (DHCP_ENCAPSULATED(**tagpp) == opt)
			break;

		if ((**tagpp) == EFX_DHCP_END) {
			rc = ENOENT;
			break;
		} else if ((**tagpp) == EFX_DHCP_PAD) {
			size = 1;
		} else {
			if (*buffer_sizep < sizeof (efx_dhcp_tag_hdr_t)) {
				rc = ENOSPC;
				goto fail2;
			}

			size =
			    DHCP_FULL_TAG_LENGTH((efx_dhcp_tag_hdr_t *)*tagpp);
		}

		if (size > *buffer_sizep) {
			rc = ENOSPC;
			goto fail3;
		}

		(*tagpp) += size;
		(*buffer_sizep) -= size;

		if ((*buffer_sizep == 0) && is_encap) {
			/* Search within encapulator tag finished */
			rc = ENOENT;
			break;
		}
	}

	/*
	 * Returns 0 if found otherwise ENOENT indicating search finished
	 * correctly
	 */
	return (rc);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * Locate value buffer for option in the given buffer.
 * Returns 0 if found, ENOENT indicating search finished
 * correctly, otherwise search failed before completion.
 */
	__checkReturn	efx_rc_t
efx_dhcp_find_tag(
	__in_bcount(buffer_length)	uint8_t *bufferp,
	__in				size_t buffer_length,
	__in				uint16_t opt,
	__deref_out			uint8_t **valuepp,
	__out				size_t *value_lengthp)
{
	efx_rc_t rc;
	uint8_t *tagp = bufferp;
	size_t len = buffer_length;

	rc = efx_dhcp_walk_tags(&tagp, &len, opt);
	if (rc == 0) {
		efx_dhcp_tag_hdr_t *hdrp;

		hdrp = (efx_dhcp_tag_hdr_t *)tagp;
		*valuepp = (uint8_t *)(&hdrp[1]);
		*value_lengthp = hdrp->length;
	} else if (rc != ENOENT) {
		goto fail1;
	}

	return (rc);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * Locate the end tag in the given buffer.
 * Returns 0 if found, ENOENT indicating search finished
 * correctly but end tag was not found; otherwise search
 * failed before completion.
 */
	__checkReturn	efx_rc_t
efx_dhcp_find_end(
	__in_bcount(buffer_length)	uint8_t *bufferp,
	__in				size_t buffer_length,
	__deref_out			uint8_t **endpp)
{
	efx_rc_t rc;
	uint8_t *endp = bufferp;
	size_t len = buffer_length;

	rc = efx_dhcp_walk_tags(&endp, &len, EFX_DHCP_END);
	if (rc == 0)
		*endpp = endp;
	else if (rc != ENOENT)
		goto fail1;

	return (rc);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


/*
 * Delete the given tag from anywhere in the buffer. Copes with
 * encapsulated tags, and updates or deletes the encapsulating opt as
 * necessary.
 */
	__checkReturn	efx_rc_t
efx_dhcp_delete_tag(
	__inout_bcount(buffer_length)	uint8_t *bufferp,
	__in				size_t buffer_length,
	__in				uint16_t opt)
{
	efx_rc_t rc;
	efx_dhcp_tag_hdr_t *hdrp;
	size_t len;
	uint8_t *startp;
	uint8_t *endp;

	len = buffer_length;
	startp = bufferp;

	if (!DHCP_OPT_HAS_VALUE(DHCP_ENCAPSULATED(opt))) {
		rc = EINVAL;
		goto fail1;
	}

	rc = efx_dhcp_walk_tags(&startp, &len, opt);
	if (rc != 0)
		goto fail1;

	hdrp = (efx_dhcp_tag_hdr_t *)startp;

	if (DHCP_IS_ENCAP_OPT(opt)) {
		uint8_t tag_length = DHCP_FULL_TAG_LENGTH(hdrp);
		uint8_t *encapp = bufferp;
		efx_dhcp_tag_hdr_t *encap_hdrp;

		len = buffer_length;
		rc = efx_dhcp_walk_tags(&encapp, &len,
		    DHCP_ENCAPSULATOR(opt));
		if (rc != 0)
			goto fail2;

		encap_hdrp = (efx_dhcp_tag_hdr_t *)encapp;
		if (encap_hdrp->length > tag_length) {
			encap_hdrp->length = (uint8_t)(
			    (size_t)encap_hdrp->length - tag_length);
		} else {
			/* delete the encapsulating tag */
			hdrp = encap_hdrp;
		}
	}

	startp = (uint8_t *)hdrp;
	endp = (uint8_t *)DHCP_NEXT_TAG(hdrp);

	if (startp < bufferp) {
		rc = EINVAL;
		goto fail3;
	}

	if (endp > &bufferp[buffer_length]) {
		rc = EINVAL;
		goto fail4;
	}

	memmove(startp, endp,
		buffer_length - (endp - bufferp));

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

/*
 * Write the tag header into write_pointp and optionally copies the payload
 * into the space following.
 */
static	void
efx_dhcp_write_tag(
	__in		uint8_t *write_pointp,
	__in		uint16_t opt,
	__in_bcount_opt(value_length)
			uint8_t *valuep,
	__in		size_t value_length)
{
	efx_dhcp_tag_hdr_t *hdrp = (efx_dhcp_tag_hdr_t *)write_pointp;
	hdrp->tag = DHCP_ENCAPSULATED(opt);
	hdrp->length = (uint8_t)value_length;
	if ((value_length > 0) && (valuep != NULL))
		memcpy(&hdrp[1], valuep, value_length);
}

/*
 * Add the given tag to the end of the buffer. Copes with creating an
 * encapsulated tag, and updates or creates the encapsulating opt as
 * necessary.
 */
	__checkReturn	efx_rc_t
efx_dhcp_add_tag(
	__inout_bcount(buffer_length)	uint8_t *bufferp,
	__in				size_t buffer_length,
	__in				uint16_t opt,
	__in_bcount_opt(value_length)	uint8_t *valuep,
	__in				size_t value_length)
{
	efx_rc_t rc;
	efx_dhcp_tag_hdr_t *encap_hdrp = NULL;
	uint8_t *insert_pointp = NULL;
	uint8_t *endp;
	size_t available_space;
	size_t added_length;
	size_t search_size;
	uint8_t *searchp;

	if (!DHCP_OPT_HAS_VALUE(DHCP_ENCAPSULATED(opt))) {
		rc = EINVAL;
		goto fail1;
	}

	if (value_length > DHCP_MAX_VALUE) {
		rc = EINVAL;
		goto fail2;
	}

	if ((value_length > 0) && (valuep == NULL)) {
		rc = EINVAL;
		goto fail3;
	}

	endp = bufferp;
	available_space = buffer_length;
	rc = efx_dhcp_walk_tags(&endp, &available_space, EFX_DHCP_END);
	if (rc != 0)
		goto fail4;

	searchp = bufferp;
	search_size = buffer_length;
	if (DHCP_IS_ENCAP_OPT(opt)) {
		rc = efx_dhcp_walk_tags(&searchp, &search_size,
		    DHCP_ENCAPSULATOR(opt));
		if (rc == 0) {
			encap_hdrp = (efx_dhcp_tag_hdr_t *)searchp;

			/* Check encapsulated tag is not present */
			search_size = encap_hdrp->length;
			rc = efx_dhcp_walk_tags(&searchp, &search_size,
			    opt);
			if (rc != ENOENT) {
				rc = EINVAL;
				goto fail5;
			}

			/* Check encapsulator will not overflow */
			if (((size_t)encap_hdrp->length +
			    DHCP_CALC_TAG_LENGTH(value_length)) >
			    DHCP_MAX_VALUE) {
				rc = E2BIG;
				goto fail6;
			}

			/* Insert at start of existing encapsulator */
			insert_pointp = (uint8_t *)&encap_hdrp[1];
			opt = DHCP_ENCAPSULATED(opt);
		} else if (rc == ENOENT) {
			encap_hdrp = NULL;
		} else {
			goto fail7;
		}
	} else {
		/* Check unencapsulated tag is not present */
		rc = efx_dhcp_walk_tags(&searchp, &search_size,
		    opt);
		if (rc != ENOENT) {
			rc = EINVAL;
			goto fail8;
		}
	}

	if (insert_pointp == NULL) {
		/* Insert at end of existing tags */
		insert_pointp = endp;
	}

	/* Includes the new encapsulator tag hdr if required */
	added_length = DHCP_CALC_TAG_LENGTH(value_length) +
	    (DHCP_IS_ENCAP_OPT(opt) ? sizeof (efx_dhcp_tag_hdr_t) : 0);

	if (available_space <= added_length) {
		rc = ENOMEM;
		goto fail9;
	}

	memmove(insert_pointp + added_length, insert_pointp,
	    available_space - added_length);

	if (DHCP_IS_ENCAP_OPT(opt)) {
		/* Create new encapsulator header */
		added_length -= sizeof (efx_dhcp_tag_hdr_t);
		efx_dhcp_write_tag(insert_pointp,
		    DHCP_ENCAPSULATOR(opt), NULL, added_length);
		insert_pointp += sizeof (efx_dhcp_tag_hdr_t);
	} else if (encap_hdrp)
		/* Modify existing encapsulator header */
		encap_hdrp->length +=
		    ((uint8_t)DHCP_CALC_TAG_LENGTH(value_length));

	efx_dhcp_write_tag(insert_pointp, opt, valuep, value_length);

	return (0);

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

/*
 * Update an existing tag to the new value. Copes with encapsulated
 * tags, and updates the encapsulating opt as necessary.
 */
	__checkReturn	efx_rc_t
efx_dhcp_update_tag(
	__inout_bcount(buffer_length)	uint8_t *bufferp,
	__in				size_t buffer_length,
	__in				uint16_t opt,
	__in				uint8_t *value_locationp,
	__in_bcount_opt(value_length)	uint8_t *valuep,
	__in				size_t value_length)
{
	efx_rc_t rc;
	uint8_t *write_pointp = value_locationp - sizeof (efx_dhcp_tag_hdr_t);
	efx_dhcp_tag_hdr_t *hdrp = (efx_dhcp_tag_hdr_t *)write_pointp;
	efx_dhcp_tag_hdr_t *encap_hdrp = NULL;
	size_t old_length;

	if (!DHCP_OPT_HAS_VALUE(DHCP_ENCAPSULATED(opt))) {
		rc = EINVAL;
		goto fail1;
	}

	if (value_length > DHCP_MAX_VALUE) {
		rc = EINVAL;
		goto fail2;
	}

	if ((value_length > 0) && (valuep == NULL)) {
		rc = EINVAL;
		goto fail3;
	}

	old_length = hdrp->length;

	if (old_length < value_length) {
		uint8_t *endp = bufferp;
		size_t available_space = buffer_length;

		rc = efx_dhcp_walk_tags(&endp, &available_space,
		    EFX_DHCP_END);
		if (rc != 0)
			goto fail4;

		if (available_space < (value_length - old_length)) {
			rc = EINVAL;
			goto fail5;
		}
	}

	if (DHCP_IS_ENCAP_OPT(opt)) {
		uint8_t *encapp = bufferp;
		size_t following_encap = buffer_length;
		size_t new_length;

		rc = efx_dhcp_walk_tags(&encapp, &following_encap,
		    DHCP_ENCAPSULATOR(opt));
		if (rc != 0)
			goto fail6;

		encap_hdrp = (efx_dhcp_tag_hdr_t *)encapp;

		new_length = ((size_t)encap_hdrp->length +
		    value_length - old_length);
		/* Check encapsulator will not overflow */
		if (new_length > DHCP_MAX_VALUE) {
			rc = E2BIG;
			goto fail7;
		}

		encap_hdrp->length = (uint8_t)new_length;
	}

	/*
	 * Move the following data up/down to accommodate the new payload
	 * length.
	 */
	if (old_length != value_length) {
		uint8_t *destp = (uint8_t *)DHCP_NEXT_TAG(hdrp) +
		    value_length - old_length;
		size_t count = &bufferp[buffer_length] -
		    (uint8_t *)DHCP_NEXT_TAG(hdrp);

		memmove(destp, DHCP_NEXT_TAG(hdrp), count);
	}

	EFSYS_ASSERT(hdrp->tag == DHCP_ENCAPSULATED(opt));
	efx_dhcp_write_tag(write_pointp, opt, valuep, value_length);

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


/*
 * Copy bootcfg sector data to a target buffer which may differ in size.
 * Optionally corrects format errors in source buffer.
 */
				efx_rc_t
efx_bootcfg_copy_sector(
	__in			efx_nic_t *enp,
	__inout_bcount(sector_length)
				uint8_t *sector,
	__in			size_t sector_length,
	__out_bcount(data_size)	uint8_t *data,
	__in			size_t data_size,
	__in			boolean_t handle_format_errors)
{
	_NOTE(ARGUNUSED(enp))

	size_t used_bytes;
	efx_rc_t rc;

	/* Minimum buffer is checksum byte and EFX_DHCP_END terminator */
	if (data_size < 2) {
		rc = ENOSPC;
		goto fail1;
	}

	/* Verify that the area is correctly formatted and checksummed */
	rc = efx_dhcp_verify(sector, sector_length,
				    &used_bytes);

	if (!handle_format_errors) {
		if (rc != 0)
			goto fail2;

		if ((used_bytes < 2) ||
		    (sector[used_bytes - 1] != EFX_DHCP_END)) {
			/* Block too short, or EFX_DHCP_END missing */
			rc = ENOENT;
			goto fail3;
		}
	}

	/* Synthesize empty format on verification failure */
	if (rc != 0 || used_bytes == 0) {
		sector[0] = 0;
		sector[1] = EFX_DHCP_END;
		used_bytes = 2;
	}
	EFSYS_ASSERT(used_bytes >= 2);	/* checksum and EFX_DHCP_END */
	EFSYS_ASSERT(used_bytes <= sector_length);
	EFSYS_ASSERT(sector_length >= 2);

	/*
	 * Legacy bootcfg sectors don't terminate with an EFX_DHCP_END
	 * character. Modify the returned payload so it does.
	 * Reinitialise the sector if there isn't room for the character.
	 */
	if (sector[used_bytes - 1] != EFX_DHCP_END) {
		if (used_bytes >= sector_length) {
			sector[0] = 0;
			used_bytes = 1;
		}
		sector[used_bytes] = EFX_DHCP_END;
		++used_bytes;
	}

	/*
	 * Verify that the target buffer is large enough for the
	 * entire used bootcfg area, then copy into the target buffer.
	 */
	if (used_bytes > data_size) {
		rc = ENOSPC;
		goto fail4;
	}

	data[0] = 0; /* checksum, updated below */

	/* Copy all after the checksum to the target buffer */
	memcpy(data + 1, sector + 1, used_bytes - 1);

	/* Zero out the unused portion of the target buffer */
	if (used_bytes < data_size)
		(void) memset(data + used_bytes, 0, data_size - used_bytes);

	/*
	 * The checksum includes trailing data after any EFX_DHCP_END
	 * character, which we've just modified (by truncation or appending
	 * EFX_DHCP_END).
	 */
	data[0] -= efx_dhcp_csum(data, data_size);

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

				efx_rc_t
efx_bootcfg_read(
	__in			efx_nic_t *enp,
	__out_bcount(size)	uint8_t *data,
	__in			size_t size)
{
	uint8_t *payload = NULL;
	size_t used_bytes;
	size_t partn_length;
	size_t sector_length;
	size_t sector_offset;
	efx_rc_t rc;
	uint32_t sector_number;

	/* Minimum buffer is checksum byte and EFX_DHCP_END terminator */
	if (size < 2) {
		rc = ENOSPC;
		goto fail1;
	}

#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
	sector_number = enp->en_nic_cfg.enc_pf;
#else
	sector_number = 0;
#endif
	rc = efx_nvram_size(enp, EFX_NVRAM_BOOTROM_CFG, &partn_length);
	if (rc != 0)
		goto fail2;

	/* The bootcfg sector may be stored in a (larger) shared partition */
	rc = efx_bootcfg_sector_info(enp, sector_number,
	    NULL, &sector_offset, &sector_length);
	if (rc != 0)
		goto fail3;

	if (sector_length < 2) {
		rc = EINVAL;
		goto fail4;
	}

	if (sector_length > BOOTCFG_MAX_SIZE)
		sector_length = BOOTCFG_MAX_SIZE;

	if (sector_offset + sector_length > partn_length) {
		/* Partition is too small */
		rc = EFBIG;
		goto fail5;
	}

	/*
	 * We need to read the entire BOOTCFG sector to ensure we read all
	 * tags, because legacy bootcfg sectors are not guaranteed to end
	 * with an EFX_DHCP_END character. If the user hasn't supplied a
	 * sufficiently large buffer then use our own buffer.
	 */
	if (sector_length > size) {
		EFSYS_KMEM_ALLOC(enp->en_esip, sector_length, payload);
		if (payload == NULL) {
			rc = ENOMEM;
			goto fail6;
		}
	} else
		payload = (uint8_t *)data;

	if ((rc = efx_nvram_rw_start(enp, EFX_NVRAM_BOOTROM_CFG, NULL)) != 0)
		goto fail7;

	if ((rc = efx_nvram_read_chunk(enp, EFX_NVRAM_BOOTROM_CFG,
	    sector_offset, (caddr_t)payload, sector_length)) != 0) {
		(void) efx_nvram_rw_finish(enp, EFX_NVRAM_BOOTROM_CFG, NULL);
		goto fail8;
	}

	if ((rc = efx_nvram_rw_finish(enp, EFX_NVRAM_BOOTROM_CFG, NULL)) != 0)
		goto fail9;

	/* Verify that the area is correctly formatted and checksummed */
	rc = efx_dhcp_verify(payload, sector_length,
	    &used_bytes);
	if (rc != 0 || used_bytes == 0) {
		payload[0] = 0;
		payload[1] = EFX_DHCP_END;
		used_bytes = 2;
	}

	EFSYS_ASSERT(used_bytes >= 2);	/* checksum and EFX_DHCP_END */
	EFSYS_ASSERT(used_bytes <= sector_length);

	/*
	 * Legacy bootcfg sectors don't terminate with an EFX_DHCP_END
	 * character. Modify the returned payload so it does.
	 * BOOTCFG_MAX_SIZE is by definition large enough for any valid
	 * (per-port) bootcfg sector, so reinitialise the sector if there
	 * isn't room for the character.
	 */
	if (payload[used_bytes - 1] != EFX_DHCP_END) {
		if (used_bytes >= sector_length)
			used_bytes = 1;

		payload[used_bytes] = EFX_DHCP_END;
		++used_bytes;
	}

	/*
	 * Verify that the user supplied buffer is large enough for the
	 * entire used bootcfg area, then copy into the user supplied buffer.
	 */
	if (used_bytes > size) {
		rc = ENOSPC;
		goto fail10;
	}

	data[0] = 0; /* checksum, updated below */

	if (sector_length > size) {
		/* Copy all after the checksum to the target buffer */
		memcpy(data + 1, payload + 1, used_bytes - 1);
		EFSYS_KMEM_FREE(enp->en_esip, sector_length, payload);
	}

	/* Zero out the unused portion of the user buffer */
	if (used_bytes < size)
		(void) memset(data + used_bytes, 0, size - used_bytes);

	/*
	 * The checksum includes trailing data after any EFX_DHCP_END character,
	 * which we've just modified (by truncation or appending EFX_DHCP_END).
	 */
	data[0] -= efx_dhcp_csum(data, size);

	return (0);

fail10:
	EFSYS_PROBE(fail10);
fail9:
	EFSYS_PROBE(fail9);
fail8:
	EFSYS_PROBE(fail8);
fail7:
	EFSYS_PROBE(fail7);
	if (sector_length > size)
		EFSYS_KMEM_FREE(enp->en_esip, sector_length, payload);
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

				efx_rc_t
efx_bootcfg_write(
	__in			efx_nic_t *enp,
	__in_bcount(size)	uint8_t *data,
	__in			size_t size)
{
	uint8_t *partn_data;
	uint8_t checksum;
	size_t partn_length;
	size_t sector_length;
	size_t sector_offset;
	size_t used_bytes;
	efx_rc_t rc;
	uint32_t sector_number;

#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
	sector_number = enp->en_nic_cfg.enc_pf;
#else
	sector_number = 0;
#endif

	rc = efx_nvram_size(enp, EFX_NVRAM_BOOTROM_CFG, &partn_length);
	if (rc != 0)
		goto fail1;

	/* The bootcfg sector may be stored in a (larger) shared partition */
	rc = efx_bootcfg_sector_info(enp, sector_number,
	    NULL, &sector_offset, &sector_length);
	if (rc != 0)
		goto fail2;

	if (sector_length > BOOTCFG_MAX_SIZE)
		sector_length = BOOTCFG_MAX_SIZE;

	if (sector_offset + sector_length > partn_length) {
		/* Partition is too small */
		rc = EFBIG;
		goto fail3;
	}

	if ((rc = efx_dhcp_verify(data, size, &used_bytes)) != 0)
		goto fail4;

	/*
	 * The caller *must* terminate their block with a EFX_DHCP_END
	 * character
	 */
	if ((used_bytes < 2) || ((uint8_t)data[used_bytes - 1] !=
	    EFX_DHCP_END)) {
		/* Block too short or EFX_DHCP_END missing */
		rc = ENOENT;
		goto fail5;
	}

	/* Check that the hardware has support for this much data */
	if (used_bytes > MIN(sector_length, BOOTCFG_MAX_SIZE)) {
		rc = ENOSPC;
		goto fail6;
	}

	/*
	 * If the BOOTCFG sector is stored in a shared partition, then we must
	 * read the whole partition and insert the updated bootcfg sector at the
	 * correct offset.
	 */
	EFSYS_KMEM_ALLOC(enp->en_esip, partn_length, partn_data);
	if (partn_data == NULL) {
		rc = ENOMEM;
		goto fail7;
	}

	rc = efx_nvram_rw_start(enp, EFX_NVRAM_BOOTROM_CFG, NULL);
	if (rc != 0)
		goto fail8;

	/* Read the entire partition */
	rc = efx_nvram_read_chunk(enp, EFX_NVRAM_BOOTROM_CFG, 0,
				    (caddr_t)partn_data, partn_length);
	if (rc != 0)
		goto fail9;

	/*
	 * Insert the BOOTCFG sector into the partition, Zero out all data
	 * after the EFX_DHCP_END tag, and adjust the checksum.
	 */
	(void) memset(partn_data + sector_offset, 0x0, sector_length);
	(void) memcpy(partn_data + sector_offset, data, used_bytes);

	checksum = efx_dhcp_csum(data, used_bytes);
	partn_data[sector_offset] -= checksum;

	if ((rc = efx_nvram_erase(enp, EFX_NVRAM_BOOTROM_CFG)) != 0)
		goto fail10;

	if ((rc = efx_nvram_write_chunk(enp, EFX_NVRAM_BOOTROM_CFG,
		    0, (caddr_t)partn_data, partn_length)) != 0)
		goto fail11;

	if ((rc = efx_nvram_rw_finish(enp, EFX_NVRAM_BOOTROM_CFG, NULL)) != 0)
		goto fail12;

	EFSYS_KMEM_FREE(enp->en_esip, partn_length, partn_data);

	return (0);

fail12:
	EFSYS_PROBE(fail12);
fail11:
	EFSYS_PROBE(fail11);
fail10:
	EFSYS_PROBE(fail10);
fail9:
	EFSYS_PROBE(fail9);

	(void) efx_nvram_rw_finish(enp, EFX_NVRAM_BOOTROM_CFG, NULL);
fail8:
	EFSYS_PROBE(fail8);

	EFSYS_KMEM_FREE(enp->en_esip, partn_length, partn_data);
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

#endif	/* EFSYS_OPT_BOOTCFG */
