/*-
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

#if EFSYS_OPT_LICENSING

#include "ef10_tlv_layout.h"
#if EFSYS_OPT_SIENA
#include "efx_regs_mcdi_aoe.h"
#endif

#if EFSYS_OPT_SIENA | EFSYS_OPT_HUNTINGTON

	__checkReturn		efx_rc_t
efx_lic_v1v2_find_start(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__out			uint32_t *startp);

	__checkReturn		efx_rc_t
efx_lic_v1v2_find_end(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *endp);

	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_v1v2_find_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *startp,
	__out			uint32_t *lengthp);

	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_v1v2_validate_key(
	__in			efx_nic_t *enp,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length);

	__checkReturn		efx_rc_t
efx_lic_v1v2_read_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__out_bcount_part(key_max_size, *lengthp)
				caddr_t keyp,
	__in			size_t key_max_size,
	__out			uint32_t *lengthp);

	__checkReturn		efx_rc_t
efx_lic_v1v2_write_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length,
	__out			uint32_t *lengthp);

	__checkReturn		efx_rc_t
efx_lic_v1v2_delete_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__in			uint32_t end,
	__out			uint32_t *deltap);

	__checkReturn		efx_rc_t
efx_lic_v1v2_create_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size);

	__checkReturn		efx_rc_t
efx_lic_v1v2_finish_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size);

#endif	/* EFSYS_OPT_HUNTINGTON | EFSYS_OPT_SIENA */


#if EFSYS_OPT_SIENA

static	__checkReturn	efx_rc_t
efx_mcdi_fc_license_update_license(
	__in		efx_nic_t *enp);

static	__checkReturn	efx_rc_t
efx_mcdi_fc_license_get_key_stats(
	__in		efx_nic_t *enp,
	__out		efx_key_stats_t *eksp);

static const efx_lic_ops_t	__efx_lic_v1_ops = {
	efx_mcdi_fc_license_update_license,	/* elo_update_licenses */
	efx_mcdi_fc_license_get_key_stats,	/* elo_get_key_stats */
	NULL,					/* elo_app_state */
	NULL,					/* elo_get_id */
	efx_lic_v1v2_find_start,		/* elo_find_start */
	efx_lic_v1v2_find_end,			/* elo_find_end */
	efx_lic_v1v2_find_key,			/* elo_find_key */
	efx_lic_v1v2_validate_key,		/* elo_validate_key */
	efx_lic_v1v2_read_key,			/* elo_read_key */
	efx_lic_v1v2_write_key,			/* elo_write_key */
	efx_lic_v1v2_delete_key,		/* elo_delete_key */
	efx_lic_v1v2_create_partition,		/* elo_create_partition */
	efx_lic_v1v2_finish_partition,		/* elo_finish_partition */
};

#endif	/* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_update_licenses(
	__in		efx_nic_t *enp);

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_get_key_stats(
	__in		efx_nic_t *enp,
	__out		efx_key_stats_t *eksp);

static	__checkReturn	efx_rc_t
efx_mcdi_licensed_app_state(
	__in		efx_nic_t *enp,
	__in		uint64_t app_id,
	__out		boolean_t *licensedp);

static const efx_lic_ops_t	__efx_lic_v2_ops = {
	efx_mcdi_licensing_update_licenses,	/* elo_update_licenses */
	efx_mcdi_licensing_get_key_stats,	/* elo_get_key_stats */
	efx_mcdi_licensed_app_state,		/* elo_app_state */
	NULL,					/* elo_get_id */
	efx_lic_v1v2_find_start,		/* elo_find_start */
	efx_lic_v1v2_find_end,			/* elo_find_end */
	efx_lic_v1v2_find_key,			/* elo_find_key */
	efx_lic_v1v2_validate_key,		/* elo_validate_key */
	efx_lic_v1v2_read_key,			/* elo_read_key */
	efx_lic_v1v2_write_key,			/* elo_write_key */
	efx_lic_v1v2_delete_key,		/* elo_delete_key */
	efx_lic_v1v2_create_partition,		/* elo_create_partition */
	efx_lic_v1v2_finish_partition,		/* elo_finish_partition */
};

#endif	/* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_v3_update_licenses(
	__in		efx_nic_t *enp);

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_v3_report_license(
	__in		efx_nic_t *enp,
	__out		efx_key_stats_t *eksp);

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_v3_app_state(
	__in		efx_nic_t *enp,
	__in		uint64_t app_id,
	__out		boolean_t *licensedp);

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_v3_get_id(
	__in		efx_nic_t *enp,
	__in		size_t buffer_size,
	__out		uint32_t *typep,
	__out		size_t *lengthp,
	__out_bcount_part_opt(buffer_size, *lengthp)
			uint8_t *bufferp);

	__checkReturn		efx_rc_t
efx_lic_v3_find_start(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__out			uint32_t *startp);

	__checkReturn		efx_rc_t
efx_lic_v3_find_end(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *endp);

	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_v3_find_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *startp,
	__out			uint32_t *lengthp);

	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_v3_validate_key(
	__in			efx_nic_t *enp,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length);

	__checkReturn		efx_rc_t
efx_lic_v3_read_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__out_bcount_part(key_max_size, *lengthp)
				caddr_t keyp,
	__in			size_t key_max_size,
	__out			uint32_t *lengthp);

	__checkReturn		efx_rc_t
efx_lic_v3_write_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length,
	__out			uint32_t *lengthp);

	__checkReturn		efx_rc_t
efx_lic_v3_delete_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__in			uint32_t end,
	__out			uint32_t *deltap);

	__checkReturn		efx_rc_t
efx_lic_v3_create_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size);

	__checkReturn		efx_rc_t
efx_lic_v3_finish_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size);

static const efx_lic_ops_t	__efx_lic_v3_ops = {
	efx_mcdi_licensing_v3_update_licenses,	/* elo_update_licenses */
	efx_mcdi_licensing_v3_report_license,	/* elo_get_key_stats */
	efx_mcdi_licensing_v3_app_state,	/* elo_app_state */
	efx_mcdi_licensing_v3_get_id,		/* elo_get_id */
	efx_lic_v3_find_start,			/* elo_find_start */
	efx_lic_v3_find_end,			/* elo_find_end */
	efx_lic_v3_find_key,			/* elo_find_key */
	efx_lic_v3_validate_key,		/* elo_validate_key */
	efx_lic_v3_read_key,			/* elo_read_key */
	efx_lic_v3_write_key,			/* elo_write_key */
	efx_lic_v3_delete_key,			/* elo_delete_key */
	efx_lic_v3_create_partition,		/* elo_create_partition */
	efx_lic_v3_finish_partition,		/* elo_finish_partition */
};

#endif	/* EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */


/* V1 Licensing - used in Siena Modena only */

#if EFSYS_OPT_SIENA

static	__checkReturn	efx_rc_t
efx_mcdi_fc_license_update_license(
	__in		efx_nic_t *enp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_FC_IN_LICENSE_LEN, 0);
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	req.emr_cmd = MC_CMD_FC;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_FC_IN_LICENSE_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = 0;

	MCDI_IN_SET_DWORD(req, FC_IN_CMD,
	    MC_CMD_FC_OP_LICENSE);

	MCDI_IN_SET_DWORD(req, FC_IN_LICENSE_OP,
	    MC_CMD_FC_IN_LICENSE_UPDATE_LICENSE);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used != 0) {
		rc = EIO;
		goto fail2;
	}

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_mcdi_fc_license_get_key_stats(
	__in		efx_nic_t *enp,
	__out		efx_key_stats_t *eksp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_FC_IN_LICENSE_LEN,
		MC_CMD_FC_OUT_LICENSE_LEN);
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	req.emr_cmd = MC_CMD_FC;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_FC_IN_LICENSE_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_FC_OUT_LICENSE_LEN;

	MCDI_IN_SET_DWORD(req, FC_IN_CMD,
	    MC_CMD_FC_OP_LICENSE);

	MCDI_IN_SET_DWORD(req, FC_IN_LICENSE_OP,
	    MC_CMD_FC_IN_LICENSE_GET_KEY_STATS);

	efx_mcdi_execute_quiet(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_FC_OUT_LICENSE_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	eksp->eks_valid =
		MCDI_OUT_DWORD(req, FC_OUT_LICENSE_VALID_KEYS);
	eksp->eks_invalid =
		MCDI_OUT_DWORD(req, FC_OUT_LICENSE_INVALID_KEYS);
	eksp->eks_blacklisted =
		MCDI_OUT_DWORD(req, FC_OUT_LICENSE_BLACKLISTED_KEYS);
	eksp->eks_unverifiable = 0;
	eksp->eks_wrong_node = 0;
	eksp->eks_licensed_apps_lo = 0;
	eksp->eks_licensed_apps_hi = 0;
	eksp->eks_licensed_features_lo = 0;
	eksp->eks_licensed_features_hi = 0;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#endif	/* EFSYS_OPT_SIENA */

/* V1 and V2 Partition format - based on a 16-bit TLV format */

#if EFSYS_OPT_SIENA | EFSYS_OPT_HUNTINGTON

/*
 * V1/V2 format - defined in SF-108542-TC section 4.2:
 *  Type (T):   16bit - revision/HMAC algorithm
 *  Length (L): 16bit - value length in bytes
 *  Value (V):  L bytes - payload
 */
#define	EFX_LICENSE_V1V2_PAYLOAD_LENGTH_MAX	(256)
#define	EFX_LICENSE_V1V2_HEADER_LENGTH		(2 * sizeof (uint16_t))

	__checkReturn		efx_rc_t
efx_lic_v1v2_find_start(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__out			uint32_t *startp)
{
	_NOTE(ARGUNUSED(enp, bufferp, buffer_size))

	*startp = 0;
	return (0);
}

	__checkReturn		efx_rc_t
efx_lic_v1v2_find_end(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *endp)
{
	_NOTE(ARGUNUSED(enp, bufferp, buffer_size))

	*endp = offset + EFX_LICENSE_V1V2_HEADER_LENGTH;
	return (0);
}

	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_v1v2_find_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *startp,
	__out			uint32_t *lengthp)
{
	boolean_t found;
	uint16_t tlv_type;
	uint16_t tlv_length;

	_NOTE(ARGUNUSED(enp))

	if ((size_t)buffer_size - offset < EFX_LICENSE_V1V2_HEADER_LENGTH)
		goto fail1;

	tlv_type = __LE_TO_CPU_16(((uint16_t *)&bufferp[offset])[0]);
	tlv_length = __LE_TO_CPU_16(((uint16_t *)&bufferp[offset])[1]);
	if ((tlv_length > EFX_LICENSE_V1V2_PAYLOAD_LENGTH_MAX) ||
	    (tlv_type == 0 && tlv_length == 0)) {
		found = B_FALSE;
	} else {
		*startp = offset;
		*lengthp = tlv_length + EFX_LICENSE_V1V2_HEADER_LENGTH;
		found = B_TRUE;
	}
	return (found);

fail1:
	EFSYS_PROBE1(fail1, boolean_t, B_FALSE);

	return (B_FALSE);
}

	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_v1v2_validate_key(
	__in			efx_nic_t *enp,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length)
{
	uint16_t tlv_type;
	uint16_t tlv_length;

	_NOTE(ARGUNUSED(enp))

	if (length < EFX_LICENSE_V1V2_HEADER_LENGTH) {
		goto fail1;
	}

	tlv_type = __LE_TO_CPU_16(((uint16_t *)keyp)[0]);
	tlv_length = __LE_TO_CPU_16(((uint16_t *)keyp)[1]);

	if (tlv_length > EFX_LICENSE_V1V2_PAYLOAD_LENGTH_MAX) {
		goto fail2;
	}
	if (tlv_type == 0) {
		goto fail3;
	}
	if ((tlv_length + EFX_LICENSE_V1V2_HEADER_LENGTH) != length) {
		goto fail4;
	}

	return (B_TRUE);

fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, boolean_t, B_FALSE);

	return (B_FALSE);
}


	__checkReturn		efx_rc_t
efx_lic_v1v2_read_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__out_bcount_part(key_max_size, *lengthp)
				caddr_t keyp,
	__in			size_t key_max_size,
	__out			uint32_t *lengthp)
{
	efx_rc_t rc;

	_NOTE(ARGUNUSED(enp, buffer_size))
	EFSYS_ASSERT(length <= (EFX_LICENSE_V1V2_PAYLOAD_LENGTH_MAX +
	    EFX_LICENSE_V1V2_HEADER_LENGTH));

	if (key_max_size < length) {
		rc = ENOSPC;
		goto fail1;
	}
	memcpy(keyp, &bufferp[offset], length);

	*lengthp = length;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_lic_v1v2_write_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length,
	__out			uint32_t *lengthp)
{
	efx_rc_t rc;

	_NOTE(ARGUNUSED(enp))
	EFSYS_ASSERT(length <= (EFX_LICENSE_V1V2_PAYLOAD_LENGTH_MAX +
	    EFX_LICENSE_V1V2_HEADER_LENGTH));

	/* Ensure space for terminator remains */
	if ((offset + length) >
	    (buffer_size - EFX_LICENSE_V1V2_HEADER_LENGTH)) {
		rc = ENOSPC;
		goto fail1;
	}

	memcpy(bufferp + offset, keyp, length);

	*lengthp = length;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_lic_v1v2_delete_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__in			uint32_t end,
	__out			uint32_t *deltap)
{
	uint32_t move_start = offset + length;
	uint32_t move_length = end - move_start;

	_NOTE(ARGUNUSED(enp, buffer_size))
	EFSYS_ASSERT(end <= buffer_size);

	/* Shift everything after the key down */
	memmove(bufferp + offset, bufferp + move_start, move_length);

	*deltap = length;

	return (0);
}

	__checkReturn		efx_rc_t
efx_lic_v1v2_create_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size)
{
	_NOTE(ARGUNUSED(enp, buffer_size))
	EFSYS_ASSERT(EFX_LICENSE_V1V2_HEADER_LENGTH <= buffer_size);

	/* Write terminator */
	memset(bufferp, '\0', EFX_LICENSE_V1V2_HEADER_LENGTH);
	return (0);
}


	__checkReturn		efx_rc_t
efx_lic_v1v2_finish_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size)
{
	_NOTE(ARGUNUSED(enp, bufferp, buffer_size))

	return (0);
}

#endif	/* EFSYS_OPT_HUNTINGTON | EFSYS_OPT_SIENA */


/* V2 Licensing - used by Huntington family only. See SF-113611-TC */

#if EFSYS_OPT_HUNTINGTON

static	__checkReturn	efx_rc_t
efx_mcdi_licensed_app_state(
	__in		efx_nic_t *enp,
	__in		uint64_t app_id,
	__out		boolean_t *licensedp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_LICENSED_APP_STATE_IN_LEN,
		MC_CMD_GET_LICENSED_APP_STATE_OUT_LEN);
	uint32_t app_state;
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON);

	/* V2 licensing supports 32bit app id only */
	if ((app_id >> 32) != 0) {
		rc = EINVAL;
		goto fail1;
	}

	req.emr_cmd = MC_CMD_GET_LICENSED_APP_STATE;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_LICENSED_APP_STATE_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_LICENSED_APP_STATE_OUT_LEN;

	MCDI_IN_SET_DWORD(req, GET_LICENSED_APP_STATE_IN_APP_ID,
		    app_id & 0xffffffff);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail2;
	}

	if (req.emr_out_length_used < MC_CMD_GET_LICENSED_APP_STATE_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail3;
	}

	app_state = (MCDI_OUT_DWORD(req, GET_LICENSED_APP_STATE_OUT_STATE));
	if (app_state != MC_CMD_GET_LICENSED_APP_STATE_OUT_NOT_LICENSED) {
		*licensedp = B_TRUE;
	} else {
		*licensedp = B_FALSE;
	}

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_update_licenses(
	__in		efx_nic_t *enp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_LICENSING_IN_LEN, 0);
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON);

	req.emr_cmd = MC_CMD_LICENSING;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_LICENSING_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = 0;

	MCDI_IN_SET_DWORD(req, LICENSING_IN_OP,
	    MC_CMD_LICENSING_IN_OP_UPDATE_LICENSE);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used != 0) {
		rc = EIO;
		goto fail2;
	}

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_get_key_stats(
	__in		efx_nic_t *enp,
	__out		efx_key_stats_t *eksp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_LICENSING_IN_LEN,
		MC_CMD_LICENSING_OUT_LEN);
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON);

	req.emr_cmd = MC_CMD_LICENSING;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_LICENSING_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_LICENSING_OUT_LEN;

	MCDI_IN_SET_DWORD(req, LICENSING_IN_OP,
	    MC_CMD_LICENSING_IN_OP_GET_KEY_STATS);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_LICENSING_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	eksp->eks_valid =
		MCDI_OUT_DWORD(req, LICENSING_OUT_VALID_APP_KEYS);
	eksp->eks_invalid =
		MCDI_OUT_DWORD(req, LICENSING_OUT_INVALID_APP_KEYS);
	eksp->eks_blacklisted =
		MCDI_OUT_DWORD(req, LICENSING_OUT_BLACKLISTED_APP_KEYS);
	eksp->eks_unverifiable =
		MCDI_OUT_DWORD(req, LICENSING_OUT_UNVERIFIABLE_APP_KEYS);
	eksp->eks_wrong_node =
		MCDI_OUT_DWORD(req, LICENSING_OUT_WRONG_NODE_APP_KEYS);
	eksp->eks_licensed_apps_lo = 0;
	eksp->eks_licensed_apps_hi = 0;
	eksp->eks_licensed_features_lo = 0;
	eksp->eks_licensed_features_hi = 0;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#endif	/* EFSYS_OPT_HUNTINGTON */

/* V3 Licensing - used starting from Medford family. See SF-114884-SW */

#if EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_v3_update_licenses(
	__in		efx_nic_t *enp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_LICENSING_V3_IN_LEN, 0);
	efx_rc_t rc;

	EFSYS_ASSERT((enp->en_family == EFX_FAMILY_MEDFORD) ||
	    (enp->en_family == EFX_FAMILY_MEDFORD2));

	req.emr_cmd = MC_CMD_LICENSING_V3;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_LICENSING_V3_IN_LEN;
	req.emr_out_buf = NULL;
	req.emr_out_length = 0;

	MCDI_IN_SET_DWORD(req, LICENSING_V3_IN_OP,
	    MC_CMD_LICENSING_V3_IN_OP_UPDATE_LICENSE);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_v3_report_license(
	__in		efx_nic_t *enp,
	__out		efx_key_stats_t *eksp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_LICENSING_V3_IN_LEN,
		MC_CMD_LICENSING_V3_OUT_LEN);
	efx_rc_t rc;

	EFSYS_ASSERT((enp->en_family == EFX_FAMILY_MEDFORD) ||
	    (enp->en_family == EFX_FAMILY_MEDFORD2));

	req.emr_cmd = MC_CMD_LICENSING_V3;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_LICENSING_V3_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_LICENSING_V3_OUT_LEN;

	MCDI_IN_SET_DWORD(req, LICENSING_V3_IN_OP,
	    MC_CMD_LICENSING_V3_IN_OP_REPORT_LICENSE);

	efx_mcdi_execute_quiet(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_LICENSING_V3_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	eksp->eks_valid =
		MCDI_OUT_DWORD(req, LICENSING_V3_OUT_VALID_KEYS);
	eksp->eks_invalid =
		MCDI_OUT_DWORD(req, LICENSING_V3_OUT_INVALID_KEYS);
	eksp->eks_blacklisted = 0;
	eksp->eks_unverifiable =
		MCDI_OUT_DWORD(req, LICENSING_V3_OUT_UNVERIFIABLE_KEYS);
	eksp->eks_wrong_node =
		MCDI_OUT_DWORD(req, LICENSING_V3_OUT_WRONG_NODE_KEYS);
	eksp->eks_licensed_apps_lo =
		MCDI_OUT_DWORD(req, LICENSING_V3_OUT_LICENSED_APPS_LO);
	eksp->eks_licensed_apps_hi =
		MCDI_OUT_DWORD(req, LICENSING_V3_OUT_LICENSED_APPS_HI);
	eksp->eks_licensed_features_lo =
		MCDI_OUT_DWORD(req, LICENSING_V3_OUT_LICENSED_FEATURES_LO);
	eksp->eks_licensed_features_hi =
		MCDI_OUT_DWORD(req, LICENSING_V3_OUT_LICENSED_FEATURES_HI);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_v3_app_state(
	__in		efx_nic_t *enp,
	__in		uint64_t app_id,
	__out		boolean_t *licensedp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_LICENSED_V3_APP_STATE_IN_LEN,
		MC_CMD_GET_LICENSED_V3_APP_STATE_OUT_LEN);
	uint32_t app_state;
	efx_rc_t rc;

	EFSYS_ASSERT((enp->en_family == EFX_FAMILY_MEDFORD) ||
	    (enp->en_family == EFX_FAMILY_MEDFORD2));

	req.emr_cmd = MC_CMD_GET_LICENSED_V3_APP_STATE;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_LICENSED_V3_APP_STATE_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_LICENSED_V3_APP_STATE_OUT_LEN;

	MCDI_IN_SET_DWORD(req, GET_LICENSED_V3_APP_STATE_IN_APP_ID_LO,
		    app_id & 0xffffffff);
	MCDI_IN_SET_DWORD(req, GET_LICENSED_V3_APP_STATE_IN_APP_ID_HI,
		    app_id >> 32);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used <
	    MC_CMD_GET_LICENSED_V3_APP_STATE_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	app_state = (MCDI_OUT_DWORD(req, GET_LICENSED_V3_APP_STATE_OUT_STATE));
	if (app_state != MC_CMD_GET_LICENSED_V3_APP_STATE_OUT_NOT_LICENSED) {
		*licensedp = B_TRUE;
	} else {
		*licensedp = B_FALSE;
	}

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_mcdi_licensing_v3_get_id(
	__in		efx_nic_t *enp,
	__in		size_t buffer_size,
	__out		uint32_t *typep,
	__out		size_t *lengthp,
	__out_bcount_part_opt(buffer_size, *lengthp)
			uint8_t *bufferp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_LICENSING_GET_ID_V3_IN_LEN,
		MC_CMD_LICENSING_GET_ID_V3_OUT_LENMAX);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_LICENSING_GET_ID_V3;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_LICENSING_GET_ID_V3_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_LICENSING_GET_ID_V3_OUT_LENMAX;

	efx_mcdi_execute_quiet(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_LICENSING_GET_ID_V3_OUT_LENMIN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	*typep = MCDI_OUT_DWORD(req, LICENSING_GET_ID_V3_OUT_LICENSE_TYPE);
	*lengthp =
	    MCDI_OUT_DWORD(req, LICENSING_GET_ID_V3_OUT_LICENSE_ID_LENGTH);

	if (bufferp != NULL) {
		memcpy(bufferp,
		    payload + MC_CMD_LICENSING_GET_ID_V3_OUT_LICENSE_ID_OFST,
		    MIN(buffer_size, *lengthp));
	}

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/* V3 format uses Huntington TLV format partition. See SF-108797-SW */
#define	EFX_LICENSE_V3_KEY_LENGTH_MIN	(64)
#define	EFX_LICENSE_V3_KEY_LENGTH_MAX	(160)

	__checkReturn		efx_rc_t
efx_lic_v3_find_start(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__out			uint32_t *startp)
{
	_NOTE(ARGUNUSED(enp))

	return (ef10_nvram_buffer_find_item_start(bufferp, buffer_size,
	    startp));
}

	__checkReturn		efx_rc_t
efx_lic_v3_find_end(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *endp)
{
	_NOTE(ARGUNUSED(enp))

	return (ef10_nvram_buffer_find_end(bufferp, buffer_size, offset, endp));
}

	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_v3_find_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *startp,
	__out			uint32_t *lengthp)
{
	_NOTE(ARGUNUSED(enp))

	return ef10_nvram_buffer_find_item(bufferp, buffer_size,
	    offset, startp, lengthp);
}

	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_v3_validate_key(
	__in			efx_nic_t *enp,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length)
{
	/* Check key is a valid V3 key */
	uint8_t key_type;
	uint8_t key_length;

	_NOTE(ARGUNUSED(enp))

	if (length < EFX_LICENSE_V3_KEY_LENGTH_MIN) {
		goto fail1;
	}

	if (length > EFX_LICENSE_V3_KEY_LENGTH_MAX) {
		goto fail2;
	}

	key_type = ((uint8_t *)keyp)[0];
	key_length = ((uint8_t *)keyp)[1];

	if (key_type < 3) {
		goto fail3;
	}
	if (key_length > length) {
		goto fail4;
	}
	return (B_TRUE);

fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, boolean_t, B_FALSE);

	return (B_FALSE);
}

	__checkReturn		efx_rc_t
efx_lic_v3_read_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__out_bcount_part(key_max_size, *lengthp)
				caddr_t keyp,
	__in			size_t key_max_size,
	__out			uint32_t *lengthp)
{
	uint32_t tag;

	_NOTE(ARGUNUSED(enp))

	return ef10_nvram_buffer_get_item(bufferp, buffer_size,
		    offset, length, &tag, keyp, key_max_size, lengthp);
}

	__checkReturn		efx_rc_t
efx_lic_v3_write_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length,
	__out			uint32_t *lengthp)
{
	_NOTE(ARGUNUSED(enp))
	EFSYS_ASSERT(length <= EFX_LICENSE_V3_KEY_LENGTH_MAX);

	return ef10_nvram_buffer_insert_item(bufferp, buffer_size,
		    offset, TLV_TAG_LICENSE, keyp, length, lengthp);
}

	__checkReturn		efx_rc_t
efx_lic_v3_delete_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__in			uint32_t end,
	__out			uint32_t *deltap)
{
	efx_rc_t rc;

	_NOTE(ARGUNUSED(enp))

	if ((rc = ef10_nvram_buffer_delete_item(bufferp,
			buffer_size, offset, length, end)) != 0) {
		goto fail1;
	}

	*deltap = length;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_lic_v3_create_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size)
{
	efx_rc_t rc;

	_NOTE(ARGUNUSED(enp))

	/* Construct empty partition */
	if ((rc = ef10_nvram_buffer_create(
	    NVRAM_PARTITION_TYPE_LICENSE,
	    bufferp, buffer_size)) != 0) {
		rc = EFAULT;
		goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_lic_v3_finish_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size)
{
	efx_rc_t rc;

	_NOTE(ARGUNUSED(enp))

	if ((rc = ef10_nvram_buffer_finish(bufferp,
			buffer_size)) != 0) {
		goto fail1;
	}

	/* Validate completed partition */
	if ((rc = ef10_nvram_buffer_validate(
					NVRAM_PARTITION_TYPE_LICENSE,
					bufferp, buffer_size)) != 0) {
		goto fail2;
	}

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


#endif	/* EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */

	__checkReturn		efx_rc_t
efx_lic_init(
	__in			efx_nic_t *enp)
{
	const efx_lic_ops_t *elop;
	efx_key_stats_t eks;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT(!(enp->en_mod_flags & EFX_MOD_LIC));

	switch (enp->en_family) {

#if EFSYS_OPT_SIENA
	case EFX_FAMILY_SIENA:
		elop = &__efx_lic_v1_ops;
		break;
#endif	/* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON
	case EFX_FAMILY_HUNTINGTON:
		elop = &__efx_lic_v2_ops;
		break;
#endif	/* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD
	case EFX_FAMILY_MEDFORD:
		elop = &__efx_lic_v3_ops;
		break;
#endif	/* EFSYS_OPT_MEDFORD */

#if EFSYS_OPT_MEDFORD2
	case EFX_FAMILY_MEDFORD2:
		elop = &__efx_lic_v3_ops;
		break;
#endif	/* EFSYS_OPT_MEDFORD2 */

	default:
		EFSYS_ASSERT(0);
		rc = ENOTSUP;
		goto fail1;
	}

	enp->en_elop = elop;
	enp->en_mod_flags |= EFX_MOD_LIC;

	/* Probe for support */
	if (efx_lic_get_key_stats(enp, &eks) == 0) {
		enp->en_licensing_supported = B_TRUE;
	} else {
		enp->en_licensing_supported = B_FALSE;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

extern	__checkReturn	boolean_t
efx_lic_check_support(
	__in			efx_nic_t *enp)
{
	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	return (enp->en_licensing_supported);
}

				void
efx_lic_fini(
	__in			efx_nic_t *enp)
{
	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	enp->en_elop = NULL;
	enp->en_mod_flags &= ~EFX_MOD_LIC;
}


	__checkReturn	efx_rc_t
efx_lic_update_licenses(
	__in		efx_nic_t *enp)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	if ((rc = elop->elo_update_licenses(enp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_lic_get_key_stats(
	__in		efx_nic_t *enp,
	__out		efx_key_stats_t *eksp)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	if ((rc = elop->elo_get_key_stats(enp, eksp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_lic_app_state(
	__in		efx_nic_t *enp,
	__in		uint64_t app_id,
	__out		boolean_t *licensedp)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	if (elop->elo_app_state == NULL)
		return (ENOTSUP);

	if ((rc = elop->elo_app_state(enp, app_id, licensedp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_lic_get_id(
	__in		efx_nic_t *enp,
	__in		size_t buffer_size,
	__out		uint32_t *typep,
	__out		size_t *lengthp,
	__out_opt	uint8_t *bufferp)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	if (elop->elo_get_id == NULL)
		return (ENOTSUP);

	if ((rc = elop->elo_get_id(enp, buffer_size, typep,
				    lengthp, bufferp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * Buffer management API - abstracts varying TLV format used for License
 * partition.
 */

	__checkReturn		efx_rc_t
efx_lic_find_start(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__out			uint32_t *startp)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	if ((rc = elop->elo_find_start(enp, bufferp, buffer_size, startp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_lic_find_end(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *endp)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	rc = elop->elo_find_end(enp, bufferp, buffer_size, offset, endp);
	if (rc != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_find_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__out			uint32_t *startp,
	__out			uint32_t *lengthp)
{
	const efx_lic_ops_t *elop = enp->en_elop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	EFSYS_ASSERT(bufferp);
	EFSYS_ASSERT(startp);
	EFSYS_ASSERT(lengthp);

	return (elop->elo_find_key(enp, bufferp, buffer_size, offset,
				    startp, lengthp));
}


/*
 * Validate that the buffer contains a single key in a recognised format.
 * An empty or terminator buffer is not accepted as a valid key.
 */
	__checkReturn	__success(return != B_FALSE)	boolean_t
efx_lic_validate_key(
	__in			efx_nic_t *enp,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	boolean_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	if ((rc = elop->elo_validate_key(enp, keyp, length)) == B_FALSE)
		goto fail1;

	return (B_TRUE);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_lic_read_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__out_bcount_part(key_max_size, *lengthp)
				caddr_t keyp,
	__in			size_t key_max_size,
	__out			uint32_t *lengthp)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	if ((rc = elop->elo_read_key(enp, bufferp, buffer_size, offset,
				    length, keyp, key_max_size, lengthp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_lic_write_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in_bcount(length)	caddr_t keyp,
	__in			uint32_t length,
	__out			uint32_t *lengthp)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	if ((rc = elop->elo_write_key(enp, bufferp, buffer_size, offset,
				    keyp, length, lengthp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_lic_delete_key(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size,
	__in			uint32_t offset,
	__in			uint32_t length,
	__in			uint32_t end,
	__out			uint32_t *deltap)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	if ((rc = elop->elo_delete_key(enp, bufferp, buffer_size, offset,
				    length, end, deltap)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_lic_create_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	if ((rc = elop->elo_create_partition(enp, bufferp, buffer_size)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


	__checkReturn		efx_rc_t
efx_lic_finish_partition(
	__in			efx_nic_t *enp,
	__in_bcount(buffer_size)
				caddr_t bufferp,
	__in			size_t buffer_size)
{
	const efx_lic_ops_t *elop = enp->en_elop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_LIC);

	if ((rc = elop->elo_finish_partition(enp, bufferp, buffer_size)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#endif	/* EFSYS_OPT_LICENSING */
