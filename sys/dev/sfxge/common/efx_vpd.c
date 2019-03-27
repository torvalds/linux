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

#if EFSYS_OPT_VPD

#define	TAG_TYPE_LBN 7
#define	TAG_TYPE_WIDTH 1
#define	TAG_TYPE_LARGE_ITEM_DECODE 1
#define	TAG_TYPE_SMALL_ITEM_DECODE 0

#define	TAG_SMALL_ITEM_NAME_LBN 3
#define	TAG_SMALL_ITEM_NAME_WIDTH 4
#define	TAG_SMALL_ITEM_SIZE_LBN 0
#define	TAG_SMALL_ITEM_SIZE_WIDTH 3

#define	TAG_LARGE_ITEM_NAME_LBN 0
#define	TAG_LARGE_ITEM_NAME_WIDTH 7

#define	TAG_NAME_END_DECODE 0x0f
#define	TAG_NAME_ID_STRING_DECODE 0x02
#define	TAG_NAME_VPD_R_DECODE 0x10
#define	TAG_NAME_VPD_W_DECODE 0x11

#if EFSYS_OPT_SIENA

static const efx_vpd_ops_t	__efx_vpd_siena_ops = {
	siena_vpd_init,		/* evpdo_init */
	siena_vpd_size,		/* evpdo_size */
	siena_vpd_read,		/* evpdo_read */
	siena_vpd_verify,	/* evpdo_verify */
	siena_vpd_reinit,	/* evpdo_reinit */
	siena_vpd_get,		/* evpdo_get */
	siena_vpd_set,		/* evpdo_set */
	siena_vpd_next,		/* evpdo_next */
	siena_vpd_write,	/* evpdo_write */
	siena_vpd_fini,		/* evpdo_fini */
};

#endif	/* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2

static const efx_vpd_ops_t	__efx_vpd_ef10_ops = {
	ef10_vpd_init,		/* evpdo_init */
	ef10_vpd_size,		/* evpdo_size */
	ef10_vpd_read,		/* evpdo_read */
	ef10_vpd_verify,	/* evpdo_verify */
	ef10_vpd_reinit,	/* evpdo_reinit */
	ef10_vpd_get,		/* evpdo_get */
	ef10_vpd_set,		/* evpdo_set */
	ef10_vpd_next,		/* evpdo_next */
	ef10_vpd_write,		/* evpdo_write */
	ef10_vpd_fini,		/* evpdo_fini */
};

#endif	/* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */

	__checkReturn		efx_rc_t
efx_vpd_init(
	__in			efx_nic_t *enp)
{
	const efx_vpd_ops_t *evpdop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT(!(enp->en_mod_flags & EFX_MOD_VPD));

	switch (enp->en_family) {
#if EFSYS_OPT_SIENA
	case EFX_FAMILY_SIENA:
		evpdop = &__efx_vpd_siena_ops;
		break;
#endif	/* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON
	case EFX_FAMILY_HUNTINGTON:
		evpdop = &__efx_vpd_ef10_ops;
		break;
#endif	/* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD
	case EFX_FAMILY_MEDFORD:
		evpdop = &__efx_vpd_ef10_ops;
		break;
#endif	/* EFSYS_OPT_MEDFORD */

#if EFSYS_OPT_MEDFORD2
	case EFX_FAMILY_MEDFORD2:
		evpdop = &__efx_vpd_ef10_ops;
		break;
#endif	/* EFSYS_OPT_MEDFORD2 */

	default:
		EFSYS_ASSERT(0);
		rc = ENOTSUP;
		goto fail1;
	}

	if (evpdop->evpdo_init != NULL) {
		if ((rc = evpdop->evpdo_init(enp)) != 0)
			goto fail2;
	}

	enp->en_evpdop = evpdop;
	enp->en_mod_flags |= EFX_MOD_VPD;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_size(
	__in			efx_nic_t *enp,
	__out			size_t *sizep)
{
	const efx_vpd_ops_t *evpdop = enp->en_evpdop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_VPD);

	if ((rc = evpdop->evpdo_size(enp, sizep)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_read(
	__in			efx_nic_t *enp,
	__out_bcount(size)	caddr_t data,
	__in			size_t size)
{
	const efx_vpd_ops_t *evpdop = enp->en_evpdop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_VPD);

	if ((rc = evpdop->evpdo_read(enp, data, size)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_verify(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size)
{
	const efx_vpd_ops_t *evpdop = enp->en_evpdop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_VPD);

	if ((rc = evpdop->evpdo_verify(enp, data, size)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_reinit(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size)
{
	const efx_vpd_ops_t *evpdop = enp->en_evpdop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_VPD);

	if (evpdop->evpdo_reinit == NULL) {
		rc = ENOTSUP;
		goto fail1;
	}

	if ((rc = evpdop->evpdo_reinit(enp, data, size)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_get(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__inout			efx_vpd_value_t *evvp)
{
	const efx_vpd_ops_t *evpdop = enp->en_evpdop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_VPD);

	if ((rc = evpdop->evpdo_get(enp, data, size, evvp)) != 0) {
		if (rc == ENOENT)
			return (rc);

		goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_set(
	__in			efx_nic_t *enp,
	__inout_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			efx_vpd_value_t *evvp)
{
	const efx_vpd_ops_t *evpdop = enp->en_evpdop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_VPD);

	if ((rc = evpdop->evpdo_set(enp, data, size, evvp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_next(
	__in			efx_nic_t *enp,
	__inout_bcount(size)	caddr_t data,
	__in			size_t size,
	__out			efx_vpd_value_t *evvp,
	__inout			unsigned int *contp)
{
	const efx_vpd_ops_t *evpdop = enp->en_evpdop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_VPD);

	if ((rc = evpdop->evpdo_next(enp, data, size, evvp, contp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_write(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size)
{
	const efx_vpd_ops_t *evpdop = enp->en_evpdop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_VPD);

	if ((rc = evpdop->evpdo_write(enp, data, size)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn		efx_rc_t
efx_vpd_next_tag(
	__in			caddr_t data,
	__in			size_t size,
	__inout			unsigned int *offsetp,
	__out			efx_vpd_tag_t *tagp,
	__out			uint16_t *lengthp)
{
	efx_byte_t byte;
	efx_word_t word;
	uint8_t name;
	uint16_t length;
	size_t headlen;
	efx_rc_t rc;

	if (*offsetp >= size) {
		rc = EFAULT;
		goto fail1;
	}

	EFX_POPULATE_BYTE_1(byte, EFX_BYTE_0, data[*offsetp]);

	switch (EFX_BYTE_FIELD(byte, TAG_TYPE)) {
	case TAG_TYPE_SMALL_ITEM_DECODE:
		headlen = 1;

		name = EFX_BYTE_FIELD(byte, TAG_SMALL_ITEM_NAME);
		length = (uint16_t)EFX_BYTE_FIELD(byte, TAG_SMALL_ITEM_SIZE);

		break;

	case TAG_TYPE_LARGE_ITEM_DECODE:
		headlen = 3;

		if (*offsetp + headlen > size) {
			rc = EFAULT;
			goto fail2;
		}

		name = EFX_BYTE_FIELD(byte, TAG_LARGE_ITEM_NAME);
		EFX_POPULATE_WORD_2(word,
				    EFX_BYTE_0, data[*offsetp + 1],
				    EFX_BYTE_1, data[*offsetp + 2]);
		length = EFX_WORD_FIELD(word, EFX_WORD_0);

		break;

	default:
		rc = EFAULT;
		goto fail2;
	}

	if (*offsetp + headlen + length > size) {
		rc = EFAULT;
		goto fail3;
	}

	EFX_STATIC_ASSERT(TAG_NAME_END_DECODE == EFX_VPD_END);
	EFX_STATIC_ASSERT(TAG_NAME_ID_STRING_DECODE == EFX_VPD_ID);
	EFX_STATIC_ASSERT(TAG_NAME_VPD_R_DECODE == EFX_VPD_RO);
	EFX_STATIC_ASSERT(TAG_NAME_VPD_W_DECODE == EFX_VPD_RW);
	if (name != EFX_VPD_END && name != EFX_VPD_ID &&
	    name != EFX_VPD_RO) {
		rc = EFAULT;
		goto fail4;
	}

	*tagp = name;
	*lengthp = length;
	*offsetp += headlen;

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

static	__checkReturn		efx_rc_t
efx_vpd_next_keyword(
	__in_bcount(size)	caddr_t tag,
	__in			size_t size,
	__in			unsigned int pos,
	__out			efx_vpd_keyword_t *keywordp,
	__out			uint8_t *lengthp)
{
	efx_vpd_keyword_t keyword;
	uint8_t length;
	efx_rc_t rc;

	if (pos + 3U > size) {
		rc = EFAULT;
		goto fail1;
	}

	keyword = EFX_VPD_KEYWORD(tag[pos], tag[pos + 1]);
	length = tag[pos + 2];

	if (length == 0 || pos + 3U + length > size) {
		rc = EFAULT;
		goto fail2;
	}

	*keywordp = keyword;
	*lengthp = length;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_hunk_length(
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__out			size_t *lengthp)
{
	efx_vpd_tag_t tag;
	unsigned int offset;
	uint16_t taglen;
	efx_rc_t rc;

	offset = 0;
	_NOTE(CONSTANTCONDITION)
	while (1) {
		if ((rc = efx_vpd_next_tag(data, size, &offset,
		    &tag, &taglen)) != 0)
			goto fail1;
		offset += taglen;
		if (tag == EFX_VPD_END)
			break;
	}

	*lengthp = offset;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_hunk_verify(
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__out_opt		boolean_t *cksummedp)
{
	efx_vpd_tag_t tag;
	efx_vpd_keyword_t keyword;
	unsigned int offset;
	unsigned int pos;
	unsigned int i;
	uint16_t taglen;
	uint8_t keylen;
	uint8_t cksum;
	boolean_t cksummed = B_FALSE;
	efx_rc_t rc;

	/*
	 * Parse every tag,keyword in the existing VPD. If the csum is present,
	 * the assert it is correct, and is the final keyword in the RO block.
	 */
	offset = 0;
	_NOTE(CONSTANTCONDITION)
	while (1) {
		if ((rc = efx_vpd_next_tag(data, size, &offset,
		    &tag, &taglen)) != 0)
			goto fail1;
		if (tag == EFX_VPD_END)
			break;
		else if (tag == EFX_VPD_ID)
			goto done;

		for (pos = 0; pos != taglen; pos += 3 + keylen) {
			/* RV keyword must be the last in the block */
			if (cksummed) {
				rc = EFAULT;
				goto fail2;
			}

			if ((rc = efx_vpd_next_keyword(data + offset,
			    taglen, pos, &keyword, &keylen)) != 0)
				goto fail3;

			if (keyword == EFX_VPD_KEYWORD('R', 'V')) {
				cksum = 0;
				for (i = 0; i < offset + pos + 4; i++)
					cksum += data[i];

				if (cksum != 0) {
					rc = EFAULT;
					goto fail4;
				}

				cksummed = B_TRUE;
			}
		}

	done:
		offset += taglen;
	}

	if (!cksummed) {
		rc = EFAULT;
		goto fail5;
	}

	if (cksummedp != NULL)
		*cksummedp = cksummed;

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

static	uint8_t	__efx_vpd_blank_pid[] = {
	/* Large resource type ID length 1 */
	0x82, 0x01, 0x00,
	/* Product name ' ' */
	0x32,
};

static uint8_t __efx_vpd_blank_r[] = {
	/* Large resource type VPD-R length 4 */
	0x90, 0x04, 0x00,
	/* RV keyword length 1 */
	'R', 'V', 0x01,
	/* RV payload checksum */
	0x00,
};

	__checkReturn		efx_rc_t
efx_vpd_hunk_reinit(
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			boolean_t wantpid)
{
	unsigned int offset = 0;
	unsigned int pos;
	efx_byte_t byte;
	uint8_t cksum;
	efx_rc_t rc;

	if (size < 0x100) {
		rc = ENOSPC;
		goto fail1;
	}

	if (wantpid) {
		memcpy(data + offset, __efx_vpd_blank_pid,
		    sizeof (__efx_vpd_blank_pid));
		offset += sizeof (__efx_vpd_blank_pid);
	}

	memcpy(data + offset, __efx_vpd_blank_r, sizeof (__efx_vpd_blank_r));
	offset += sizeof (__efx_vpd_blank_r);

	/* Update checksum */
	cksum = 0;
	for (pos = 0; pos < offset; pos++)
		cksum += data[pos];
	data[offset - 1] -= cksum;

	/* Append trailing tag */
	EFX_POPULATE_BYTE_3(byte,
			    TAG_TYPE, TAG_TYPE_SMALL_ITEM_DECODE,
			    TAG_SMALL_ITEM_NAME, TAG_NAME_END_DECODE,
			    TAG_SMALL_ITEM_SIZE, 0);
	data[offset] = EFX_BYTE_FIELD(byte, EFX_BYTE_0);
	offset++;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn			efx_rc_t
efx_vpd_hunk_next(
	__in_bcount(size)		caddr_t data,
	__in				size_t size,
	__out				efx_vpd_tag_t *tagp,
	__out				efx_vpd_keyword_t *keywordp,
	__out_opt			unsigned int *payloadp,
	__out_opt			uint8_t *paylenp,
	__inout				unsigned int *contp)
{
	efx_vpd_tag_t tag;
	efx_vpd_keyword_t keyword = 0;
	unsigned int offset;
	unsigned int pos;
	unsigned int index;
	uint16_t taglen;
	uint8_t keylen;
	uint8_t paylen;
	efx_rc_t rc;

	offset = index = 0;
	_NOTE(CONSTANTCONDITION)
	while (1) {
		if ((rc = efx_vpd_next_tag(data, size, &offset,
		    &tag, &taglen)) != 0)
			goto fail1;

		if (tag == EFX_VPD_END) {
			keyword = 0;
			paylen = 0;
			index = 0;
			break;
		}

		if (tag == EFX_VPD_ID) {
			if (index++ == *contp) {
				EFSYS_ASSERT3U(taglen, <, 0x100);
				keyword = 0;
				paylen = (uint8_t)MIN(taglen, 0xff);

				goto done;
			}
		} else {
			for (pos = 0; pos != taglen; pos += 3 + keylen) {
				if ((rc = efx_vpd_next_keyword(data + offset,
				    taglen, pos, &keyword, &keylen)) != 0)
					goto fail2;

				if (index++ == *contp) {
					offset += pos + 3;
					paylen = keylen;

					goto done;
				}
			}
		}

		offset += taglen;
	}

done:
	*tagp = tag;
	*keywordp = keyword;
	if (payloadp != NULL)
		*payloadp = offset;
	if (paylenp != NULL)
		*paylenp = paylen;

	*contp = index;
	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_hunk_get(
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			efx_vpd_tag_t tag,
	__in			efx_vpd_keyword_t keyword,
	__out			unsigned int *payloadp,
	__out			uint8_t *paylenp)
{
	efx_vpd_tag_t itag;
	efx_vpd_keyword_t ikeyword;
	unsigned int offset;
	unsigned int pos;
	uint16_t taglen;
	uint8_t keylen;
	efx_rc_t rc;

	offset = 0;
	_NOTE(CONSTANTCONDITION)
	while (1) {
		if ((rc = efx_vpd_next_tag(data, size, &offset,
		    &itag, &taglen)) != 0)
			goto fail1;
		if (itag == EFX_VPD_END)
			break;

		if (itag == tag) {
			if (itag == EFX_VPD_ID) {
				EFSYS_ASSERT3U(taglen, <, 0x100);

				*paylenp = (uint8_t)MIN(taglen, 0xff);
				*payloadp = offset;
				return (0);
			}

			for (pos = 0; pos != taglen; pos += 3 + keylen) {
				if ((rc = efx_vpd_next_keyword(data + offset,
				    taglen, pos, &ikeyword, &keylen)) != 0)
					goto fail2;

				if (ikeyword == keyword) {
					*paylenp = keylen;
					*payloadp = offset + pos + 3;
					return (0);
				}
			}
		}

		offset += taglen;
	}

	/* Not an error */
	return (ENOENT);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_vpd_hunk_set(
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			efx_vpd_value_t *evvp)
{
	efx_word_t word;
	efx_vpd_tag_t tag;
	efx_vpd_keyword_t keyword;
	unsigned int offset;
	unsigned int pos;
	unsigned int taghead;
	unsigned int source;
	unsigned int dest;
	unsigned int i;
	uint16_t taglen;
	uint8_t keylen;
	uint8_t cksum;
	size_t used;
	efx_rc_t rc;

	switch (evvp->evv_tag) {
	case EFX_VPD_ID:
		if (evvp->evv_keyword != 0) {
			rc = EINVAL;
			goto fail1;
		}

		/* Can't delete the ID keyword */
		if (evvp->evv_length == 0) {
			rc = EINVAL;
			goto fail1;
		}
		break;

	case EFX_VPD_RO:
		if (evvp->evv_keyword == EFX_VPD_KEYWORD('R', 'V')) {
			rc = EINVAL;
			goto fail1;
		}
		break;

	default:
		rc = EINVAL;
		goto fail1;
	}

	/* Determine total size of all current tags */
	if ((rc = efx_vpd_hunk_length(data, size, &used)) != 0)
		goto fail2;

	offset = 0;
	_NOTE(CONSTANTCONDITION)
	while (1) {
		taghead = offset;
		if ((rc = efx_vpd_next_tag(data, size, &offset,
		    &tag, &taglen)) != 0)
			goto fail3;
		if (tag == EFX_VPD_END)
			break;
		else if (tag != evvp->evv_tag) {
			offset += taglen;
			continue;
		}

		/* We only support modifying large resource tags */
		if (offset - taghead != 3) {
			rc = EINVAL;
			goto fail4;
		}

		/*
		 * Work out the offset of the byte immediately after the
		 * old (=source) and new (=dest) new keyword/tag
		 */
		pos = 0;
		if (tag == EFX_VPD_ID) {
			source = offset + taglen;
			dest = offset + evvp->evv_length;
			goto check_space;
		}

		EFSYS_ASSERT3U(tag, ==, EFX_VPD_RO);
		source = dest = 0;
		for (pos = 0; pos != taglen; pos += 3 + keylen) {
			if ((rc = efx_vpd_next_keyword(data + offset,
			    taglen, pos, &keyword, &keylen)) != 0)
				goto fail5;

			if (keyword == evvp->evv_keyword &&
			    evvp->evv_length == 0) {
				/* Deleting this keyword */
				source = offset + pos + 3 + keylen;
				dest = offset + pos;
				break;

			} else if (keyword == evvp->evv_keyword) {
				/* Adjusting this keyword */
				source = offset + pos + 3 + keylen;
				dest = offset + pos + 3 + evvp->evv_length;
				break;

			} else if (keyword == EFX_VPD_KEYWORD('R', 'V')) {
				/* The RV keyword must be at the end */
				EFSYS_ASSERT3U(pos + 3 + keylen, ==, taglen);

				/*
				 * The keyword doesn't already exist. If the
				 * user deleting a non-existant keyword then
				 * this is a no-op.
				 */
				if (evvp->evv_length == 0)
					return (0);

				/* Insert this keyword before the RV keyword */
				source = offset + pos;
				dest = offset + pos + 3 + evvp->evv_length;
				break;
			}
		}

	check_space:
		if (used + dest > size + source) {
			rc = ENOSPC;
			goto fail6;
		}

		/* Move trailing data */
		(void) memmove(data + dest, data + source, used - source);

		/* Copy contents */
		memcpy(data + dest - evvp->evv_length, evvp->evv_value,
		    evvp->evv_length);

		/* Insert new keyword header if required */
		if (tag != EFX_VPD_ID && evvp->evv_length > 0) {
			EFX_POPULATE_WORD_1(word, EFX_WORD_0,
					    evvp->evv_keyword);
			data[offset + pos + 0] =
			    EFX_WORD_FIELD(word, EFX_BYTE_0);
			data[offset + pos + 1] =
			    EFX_WORD_FIELD(word, EFX_BYTE_1);
			data[offset + pos + 2] = evvp->evv_length;
		}

		/* Modify tag length (large resource type) */
		taglen += (uint16_t)(dest - source);
		EFX_POPULATE_WORD_1(word, EFX_WORD_0, taglen);
		data[offset - 2] = EFX_WORD_FIELD(word, EFX_BYTE_0);
		data[offset - 1] = EFX_WORD_FIELD(word, EFX_BYTE_1);

		goto checksum;
	}

	/* Unable to find the matching tag */
	rc = ENOENT;
	goto fail7;

checksum:
	/* Find the RV tag, and update the checksum */
	offset = 0;
	_NOTE(CONSTANTCONDITION)
	while (1) {
		if ((rc = efx_vpd_next_tag(data, size, &offset,
		    &tag, &taglen)) != 0)
			goto fail8;
		if (tag == EFX_VPD_END)
			break;
		if (tag == EFX_VPD_RO) {
			for (pos = 0; pos != taglen; pos += 3 + keylen) {
				if ((rc = efx_vpd_next_keyword(data + offset,
				    taglen, pos, &keyword, &keylen)) != 0)
					goto fail9;

				if (keyword == EFX_VPD_KEYWORD('R', 'V')) {
					cksum = 0;
					for (i = 0; i < offset + pos + 3; i++)
						cksum += data[i];
					data[i] = -cksum;
					break;
				}
			}
		}

		offset += taglen;
	}

	/* Zero out the unused portion */
	(void) memset(data + offset + taglen, 0xff, size - offset - taglen);

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

				void
efx_vpd_fini(
	__in			efx_nic_t *enp)
{
	const efx_vpd_ops_t *evpdop = enp->en_evpdop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_VPD);

	if (evpdop->evpdo_fini != NULL)
		evpdop->evpdo_fini(enp);

	enp->en_evpdop = NULL;
	enp->en_mod_flags &= ~EFX_MOD_VPD;
}

#endif	/* EFSYS_OPT_VPD */
