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

#if EFSYS_OPT_SIENA

static	__checkReturn			efx_rc_t
siena_vpd_get_static(
	__in				efx_nic_t *enp,
	__in				uint32_t partn,
	__deref_out_bcount_opt(*sizep)	caddr_t *svpdp,
	__out				size_t *sizep)
{
	siena_mc_static_config_hdr_t *scfg;
	caddr_t svpd;
	size_t size;
	uint8_t cksum;
	unsigned int vpd_offset;
	unsigned int vpd_length;
	unsigned int hdr_length;
	unsigned int pos;
	unsigned int region;
	efx_rc_t rc;

	EFSYS_ASSERT(partn == MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT0 ||
		    partn == MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT1);

	/* Allocate sufficient memory for the entire static cfg area */
	if ((rc = siena_nvram_partn_size(enp, partn, &size)) != 0)
		goto fail1;

	if (size < SIENA_NVRAM_CHUNK) {
		rc = EINVAL;
		goto fail2;
	}

	EFSYS_KMEM_ALLOC(enp->en_esip, size, scfg);
	if (scfg == NULL) {
		rc = ENOMEM;
		goto fail3;
	}

	if ((rc = siena_nvram_partn_read(enp, partn, 0,
	    (caddr_t)scfg, SIENA_NVRAM_CHUNK)) != 0)
		goto fail4;

	/* Verify the magic number */
	if (EFX_DWORD_FIELD(scfg->magic, EFX_DWORD_0) !=
	    SIENA_MC_STATIC_CONFIG_MAGIC) {
		rc = EINVAL;
		goto fail5;
	}

	/* All future versions of the structure must be backwards compatible */
	EFX_STATIC_ASSERT(SIENA_MC_STATIC_CONFIG_VERSION == 0);

	hdr_length = EFX_WORD_FIELD(scfg->length, EFX_WORD_0);
	vpd_offset = EFX_DWORD_FIELD(scfg->static_vpd_offset, EFX_DWORD_0);
	vpd_length = EFX_DWORD_FIELD(scfg->static_vpd_length, EFX_DWORD_0);

	/* Verify the hdr doesn't overflow the sector size */
	if (hdr_length > size || vpd_offset > size || vpd_length > size ||
	    vpd_length + vpd_offset > size) {
		rc = EINVAL;
		goto fail6;
	}

	/* Read the remainder of scfg + static vpd */
	region = vpd_offset + vpd_length;
	if (region > SIENA_NVRAM_CHUNK) {
		if ((rc = siena_nvram_partn_read(enp, partn, SIENA_NVRAM_CHUNK,
		    (caddr_t)scfg + SIENA_NVRAM_CHUNK,
		    region - SIENA_NVRAM_CHUNK)) != 0)
			goto fail7;
	}

	/* Verify checksum */
	cksum = 0;
	for (pos = 0; pos < hdr_length; pos++)
		cksum += ((uint8_t *)scfg)[pos];
	if (cksum != 0) {
		rc = EINVAL;
		goto fail8;
	}

	if (vpd_length == 0)
		svpd = NULL;
	else {
		/* Copy the vpd data out */
		EFSYS_KMEM_ALLOC(enp->en_esip, vpd_length, svpd);
		if (svpd == NULL) {
			rc = ENOMEM;
			goto fail9;
		}
		memcpy(svpd, (caddr_t)scfg + vpd_offset, vpd_length);
	}

	EFSYS_KMEM_FREE(enp->en_esip, size, scfg);

	*svpdp = svpd;
	*sizep = vpd_length;

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

	EFSYS_KMEM_FREE(enp->en_esip, size, scfg);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
siena_vpd_init(
	__in			efx_nic_t *enp)
{
	efx_mcdi_iface_t *emip = &(enp->en_mcdi.em_emip);
	caddr_t svpd = NULL;
	unsigned int partn;
	size_t size = 0;
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	partn = (emip->emi_port == 1)
		? MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT0
		: MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT1;

	/*
	 * We need the static VPD sector to present a unified static+dynamic
	 * VPD, that is, basically on every read, write, verify cycle. Since
	 * it should *never* change we can just cache it here.
	 */
	if ((rc = siena_vpd_get_static(enp, partn, &svpd, &size)) != 0)
		goto fail1;

	if (svpd != NULL && size > 0) {
		if ((rc = efx_vpd_hunk_verify(svpd, size, NULL)) != 0)
			goto fail2;
	}

	enp->en_u.siena.enu_svpd = svpd;
	enp->en_u.siena.enu_svpd_length = size;

	return (0);

fail2:
	EFSYS_PROBE(fail2);

	EFSYS_KMEM_FREE(enp->en_esip, size, svpd);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
siena_vpd_size(
	__in			efx_nic_t *enp,
	__out			size_t *sizep)
{
	efx_mcdi_iface_t *emip = &(enp->en_mcdi.em_emip);
	uint32_t partn;
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	/*
	 * This function returns the total size the user should allocate
	 * for all VPD operations. We've already cached the static vpd,
	 * so we just need to return an upper bound on the dynamic vpd.
	 * Since the dynamic_config structure can change under our feet,
	 * (as version numbers are inserted), just be safe and return the
	 * total size of the dynamic_config *sector*
	 */
	partn = (emip->emi_port == 1)
		? MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT0
		: MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT1;

	if ((rc = siena_nvram_partn_size(enp, partn, sizep)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
siena_vpd_read(
	__in			efx_nic_t *enp,
	__out_bcount(size)	caddr_t data,
	__in			size_t size)
{
	efx_mcdi_iface_t *emip = &(enp->en_mcdi.em_emip);
	siena_mc_dynamic_config_hdr_t *dcfg = NULL;
	unsigned int vpd_length;
	unsigned int vpd_offset;
	unsigned int dcfg_partn;
	size_t dcfg_size;
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	dcfg_partn = (emip->emi_port == 1)
		? MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT0
		: MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT1;

	if ((rc = siena_nvram_get_dynamic_cfg(enp, dcfg_partn,
	    B_TRUE, &dcfg, &dcfg_size)) != 0)
		goto fail1;

	vpd_length = EFX_DWORD_FIELD(dcfg->dynamic_vpd_length, EFX_DWORD_0);
	vpd_offset = EFX_DWORD_FIELD(dcfg->dynamic_vpd_offset, EFX_DWORD_0);

	if (vpd_length > size) {
		rc = EFAULT;	/* Invalid dcfg: header bigger than sector */
		goto fail2;
	}

	EFSYS_ASSERT3U(vpd_length, <=, size);
	memcpy(data, (caddr_t)dcfg + vpd_offset, vpd_length);

	/* Pad data with all-1s, consistent with update operations */
	memset(data + vpd_length, 0xff, size - vpd_length);

	EFSYS_KMEM_FREE(enp->en_esip, dcfg_size, dcfg);

	return (0);

fail2:
	EFSYS_PROBE(fail2);

	EFSYS_KMEM_FREE(enp->en_esip, dcfg_size, dcfg);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
siena_vpd_verify(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size)
{
	efx_vpd_tag_t stag;
	efx_vpd_tag_t dtag;
	efx_vpd_keyword_t skey;
	efx_vpd_keyword_t dkey;
	unsigned int scont;
	unsigned int dcont;

	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	/*
	 * Strictly you could take the view that dynamic vpd is optional.
	 * Instead, to conform more closely to the read/verify/reinit()
	 * paradigm, we require dynamic vpd. siena_vpd_reinit() will
	 * reinitialize it as required.
	 */
	if ((rc = efx_vpd_hunk_verify(data, size, NULL)) != 0)
		goto fail1;

	/*
	 * Verify that there is no duplication between the static and
	 * dynamic cfg sectors.
	 */
	if (enp->en_u.siena.enu_svpd_length == 0)
		goto done;

	dcont = 0;
	_NOTE(CONSTANTCONDITION)
	while (1) {
		if ((rc = efx_vpd_hunk_next(data, size, &dtag,
		    &dkey, NULL, NULL, &dcont)) != 0)
			goto fail2;
		if (dcont == 0)
			break;

		/*
		 * Skip the RV keyword. It should be present in both the static
		 * and dynamic cfg sectors.
		 */
		if (dtag == EFX_VPD_RO && dkey == EFX_VPD_KEYWORD('R', 'V'))
			continue;

		scont = 0;
		_NOTE(CONSTANTCONDITION)
		while (1) {
			if ((rc = efx_vpd_hunk_next(
			    enp->en_u.siena.enu_svpd,
			    enp->en_u.siena.enu_svpd_length, &stag, &skey,
			    NULL, NULL, &scont)) != 0)
				goto fail3;
			if (scont == 0)
				break;

			if (stag == dtag && skey == dkey) {
				rc = EEXIST;
				goto fail4;
			}
		}
	}

done:
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

	__checkReturn		efx_rc_t
siena_vpd_reinit(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size)
{
	boolean_t wantpid;
	efx_rc_t rc;

	/*
	 * Only create a PID if the dynamic cfg doesn't have one
	 */
	if (enp->en_u.siena.enu_svpd_length == 0)
		wantpid = B_TRUE;
	else {
		unsigned int offset;
		uint8_t length;

		rc = efx_vpd_hunk_get(enp->en_u.siena.enu_svpd,
				    enp->en_u.siena.enu_svpd_length,
				    EFX_VPD_ID, 0, &offset, &length);
		if (rc == 0)
			wantpid = B_FALSE;
		else if (rc == ENOENT)
			wantpid = B_TRUE;
		else
			goto fail1;
	}

	if ((rc = efx_vpd_hunk_reinit(data, size, wantpid)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
siena_vpd_get(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__inout			efx_vpd_value_t *evvp)
{
	unsigned int offset;
	uint8_t length;
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	/* Attempt to satisfy the request from svpd first */
	if (enp->en_u.siena.enu_svpd_length > 0) {
		if ((rc = efx_vpd_hunk_get(enp->en_u.siena.enu_svpd,
		    enp->en_u.siena.enu_svpd_length, evvp->evv_tag,
		    evvp->evv_keyword, &offset, &length)) == 0) {
			evvp->evv_length = length;
			memcpy(evvp->evv_value,
			    enp->en_u.siena.enu_svpd + offset, length);
			return (0);
		} else if (rc != ENOENT)
			goto fail1;
	}

	/* And then from the provided data buffer */
	if ((rc = efx_vpd_hunk_get(data, size, evvp->evv_tag,
	    evvp->evv_keyword, &offset, &length)) != 0) {
		if (rc == ENOENT)
			return (rc);

		goto fail2;
	}

	evvp->evv_length = length;
	memcpy(evvp->evv_value, data + offset, length);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
siena_vpd_set(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			efx_vpd_value_t *evvp)
{
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	/* If the provided (tag,keyword) exists in svpd, then it is readonly */
	if (enp->en_u.siena.enu_svpd_length > 0) {
		unsigned int offset;
		uint8_t length;

		if ((rc = efx_vpd_hunk_get(enp->en_u.siena.enu_svpd,
		    enp->en_u.siena.enu_svpd_length, evvp->evv_tag,
		    evvp->evv_keyword, &offset, &length)) == 0) {
			rc = EACCES;
			goto fail1;
		}
	}

	if ((rc = efx_vpd_hunk_set(data, size, evvp)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
siena_vpd_next(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__out			efx_vpd_value_t *evvp,
	__inout			unsigned int *contp)
{
	_NOTE(ARGUNUSED(enp, data, size, evvp, contp))

	return (ENOTSUP);
}

	__checkReturn		efx_rc_t
siena_vpd_write(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size)
{
	efx_mcdi_iface_t *emip = &(enp->en_mcdi.em_emip);
	siena_mc_dynamic_config_hdr_t *dcfg = NULL;
	unsigned int vpd_offset;
	unsigned int dcfg_partn;
	unsigned int hdr_length;
	unsigned int pos;
	uint8_t cksum;
	size_t partn_size, dcfg_size;
	size_t vpd_length;
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	/* Determine total length of all tags */
	if ((rc = efx_vpd_hunk_length(data, size, &vpd_length)) != 0)
		goto fail1;

	/* Lock dynamic config sector for write, and read structure only */
	dcfg_partn = (emip->emi_port == 1)
		? MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT0
		: MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT1;

	if ((rc = siena_nvram_partn_size(enp, dcfg_partn, &partn_size)) != 0)
		goto fail2;

	if ((rc = siena_nvram_partn_lock(enp, dcfg_partn)) != 0)
		goto fail3;

	if ((rc = siena_nvram_get_dynamic_cfg(enp, dcfg_partn,
	    B_FALSE, &dcfg, &dcfg_size)) != 0)
		goto fail4;

	hdr_length = EFX_WORD_FIELD(dcfg->length, EFX_WORD_0);

	/* Allocated memory should have room for the new VPD */
	if (hdr_length + vpd_length > dcfg_size) {
		rc = ENOSPC;
		goto fail5;
	}

	/* Copy in new vpd and update header */
	vpd_offset = dcfg_size - vpd_length;
	EFX_POPULATE_DWORD_1(dcfg->dynamic_vpd_offset, EFX_DWORD_0, vpd_offset);
	memcpy((caddr_t)dcfg + vpd_offset, data, vpd_length);
	EFX_POPULATE_DWORD_1(dcfg->dynamic_vpd_length, EFX_DWORD_0, vpd_length);

	/* Update the checksum */
	cksum = 0;
	for (pos = 0; pos < hdr_length; pos++)
		cksum += ((uint8_t *)dcfg)[pos];
	dcfg->csum.eb_u8[0] -= cksum;

	/* Erase and write the new sector */
	if ((rc = siena_nvram_partn_erase(enp, dcfg_partn, 0, partn_size)) != 0)
		goto fail6;

	/* Write out the new structure to nvram */
	if ((rc = siena_nvram_partn_write(enp, dcfg_partn, 0, (caddr_t)dcfg,
	    vpd_offset + vpd_length)) != 0)
		goto fail7;

	EFSYS_KMEM_FREE(enp->en_esip, dcfg_size, dcfg);

	siena_nvram_partn_unlock(enp, dcfg_partn, NULL);

	return (0);

fail7:
	EFSYS_PROBE(fail7);
fail6:
	EFSYS_PROBE(fail6);
fail5:
	EFSYS_PROBE(fail5);

	EFSYS_KMEM_FREE(enp->en_esip, dcfg_size, dcfg);
fail4:
	EFSYS_PROBE(fail4);

	siena_nvram_partn_unlock(enp, dcfg_partn, NULL);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

				void
siena_vpd_fini(
	__in			efx_nic_t *enp)
{
	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	if (enp->en_u.siena.enu_svpd_length > 0) {
		EFSYS_KMEM_FREE(enp->en_esip, enp->en_u.siena.enu_svpd_length,
				enp->en_u.siena.enu_svpd);

		enp->en_u.siena.enu_svpd = NULL;
		enp->en_u.siena.enu_svpd_length = 0;
	}
}

#endif	/* EFSYS_OPT_SIENA */

#endif	/* EFSYS_OPT_VPD */
