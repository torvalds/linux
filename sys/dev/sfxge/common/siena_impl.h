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
 *
 * $FreeBSD$
 */

#ifndef _SYS_SIENA_IMPL_H
#define	_SYS_SIENA_IMPL_H

#include "efx.h"
#include "efx_regs.h"
#include "efx_mcdi.h"
#include "siena_flash.h"

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef EFX_TXQ_DC_SIZE
#define	EFX_TXQ_DC_SIZE 1 /* 16 descriptors */
#endif
#ifndef EFX_RXQ_DC_SIZE
#define	EFX_RXQ_DC_SIZE 3 /* 64 descriptors */
#endif
#define	EFX_TXQ_DC_NDESCS(_dcsize)	(8 << (_dcsize))

#define	SIENA_NVRAM_CHUNK 0x80


extern	__checkReturn	efx_rc_t
siena_nic_probe(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
siena_nic_reset(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
siena_nic_init(
	__in		efx_nic_t *enp);

#if EFSYS_OPT_DIAG

extern	efx_sram_pattern_fn_t	__efx_sram_pattern_fns[];

typedef struct siena_register_set_s {
	unsigned int		address;
	unsigned int		step;
	unsigned int		rows;
	efx_oword_t		mask;
} siena_register_set_t;

extern	__checkReturn	efx_rc_t
siena_nic_register_test(
	__in		efx_nic_t *enp);

#endif	/* EFSYS_OPT_DIAG */

extern			void
siena_nic_fini(
	__in		efx_nic_t *enp);

extern			void
siena_nic_unprobe(
	__in		efx_nic_t *enp);

#define	SIENA_SRAM_ROWS	0x12000

extern			void
siena_sram_init(
	__in		efx_nic_t *enp);

#if EFSYS_OPT_DIAG

extern	__checkReturn	efx_rc_t
siena_sram_test(
	__in		efx_nic_t *enp,
	__in		efx_sram_pattern_fn_t func);

#endif	/* EFSYS_OPT_DIAG */

#if EFSYS_OPT_MCDI

extern	__checkReturn	efx_rc_t
siena_mcdi_init(
	__in		efx_nic_t *enp,
	__in		const efx_mcdi_transport_t *mtp);

extern			void
siena_mcdi_send_request(
	__in			efx_nic_t *enp,
	__in_bcount(hdr_len)	void *hdrp,
	__in			size_t hdr_len,
	__in_bcount(sdu_len)	void *sdup,
	__in			size_t sdu_len);

extern	__checkReturn	boolean_t
siena_mcdi_poll_response(
	__in		efx_nic_t *enp);

extern			void
siena_mcdi_read_response(
	__in			efx_nic_t *enp,
	__out_bcount(length)	void *bufferp,
	__in			size_t offset,
	__in			size_t length);

extern			efx_rc_t
siena_mcdi_poll_reboot(
	__in		efx_nic_t *enp);

extern			void
siena_mcdi_fini(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
siena_mcdi_feature_supported(
	__in		efx_nic_t *enp,
	__in		efx_mcdi_feature_id_t id,
	__out		boolean_t *supportedp);

extern			void
siena_mcdi_get_timeout(
	__in		efx_nic_t *enp,
	__in		efx_mcdi_req_t *emrp,
	__out		uint32_t *timeoutp);

#endif /* EFSYS_OPT_MCDI */

#if EFSYS_OPT_NVRAM || EFSYS_OPT_VPD

extern	__checkReturn		efx_rc_t
siena_nvram_partn_lock(
	__in			efx_nic_t *enp,
	__in			uint32_t partn);

extern	__checkReturn		efx_rc_t
siena_nvram_partn_unlock(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out_opt		uint32_t *verify_resultp);

extern	__checkReturn		efx_rc_t
siena_nvram_get_dynamic_cfg(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			boolean_t vpd,
	__out			siena_mc_dynamic_config_hdr_t **dcfgp,
	__out			size_t *sizep);

#endif	/* EFSYS_OPT_VPD || EFSYS_OPT_NVRAM */

#if EFSYS_OPT_NVRAM

#if EFSYS_OPT_DIAG

extern	__checkReturn		efx_rc_t
siena_nvram_test(
	__in			efx_nic_t *enp);

#endif	/* EFSYS_OPT_DIAG */

extern	__checkReturn		efx_rc_t
siena_nvram_get_subtype(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out			uint32_t *subtypep);

extern	__checkReturn		efx_rc_t
siena_nvram_type_to_partn(
	__in			efx_nic_t *enp,
	__in			efx_nvram_type_t type,
	__out			uint32_t *partnp);

extern	__checkReturn		efx_rc_t
siena_nvram_partn_size(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out			size_t *sizep);

extern	__checkReturn		efx_rc_t
siena_nvram_partn_rw_start(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out			size_t *chunk_sizep);

extern	__checkReturn		efx_rc_t
siena_nvram_partn_read(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			unsigned int offset,
	__out_bcount(size)	caddr_t data,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
siena_nvram_partn_erase(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			unsigned int offset,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
siena_nvram_partn_write(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in			unsigned int offset,
	__out_bcount(size)	caddr_t data,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
siena_nvram_partn_rw_finish(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out_opt		uint32_t *verify_resultp);

extern	__checkReturn		efx_rc_t
siena_nvram_partn_get_version(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__out			uint32_t *subtypep,
	__out_ecount(4)		uint16_t version[4]);

extern	__checkReturn		efx_rc_t
siena_nvram_partn_set_version(
	__in			efx_nic_t *enp,
	__in			uint32_t partn,
	__in_ecount(4)		uint16_t version[4]);

#endif	/* EFSYS_OPT_NVRAM */

#if EFSYS_OPT_VPD

extern	__checkReturn		efx_rc_t
siena_vpd_init(
	__in			efx_nic_t *enp);

extern	__checkReturn		efx_rc_t
siena_vpd_size(
	__in			efx_nic_t *enp,
	__out			size_t *sizep);

extern	__checkReturn		efx_rc_t
siena_vpd_read(
	__in			efx_nic_t *enp,
	__out_bcount(size)	caddr_t data,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
siena_vpd_verify(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
siena_vpd_reinit(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size);

extern	__checkReturn		efx_rc_t
siena_vpd_get(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__inout			efx_vpd_value_t *evvp);

extern	__checkReturn		efx_rc_t
siena_vpd_set(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__in			efx_vpd_value_t *evvp);

extern	__checkReturn		efx_rc_t
siena_vpd_next(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size,
	__out			efx_vpd_value_t *evvp,
	__inout			unsigned int *contp);

extern __checkReturn		efx_rc_t
siena_vpd_write(
	__in			efx_nic_t *enp,
	__in_bcount(size)	caddr_t data,
	__in			size_t size);

extern				void
siena_vpd_fini(
	__in			efx_nic_t *enp);

#endif	/* EFSYS_OPT_VPD */

typedef struct siena_link_state_s {
	uint32_t		sls_adv_cap_mask;
	uint32_t		sls_lp_cap_mask;
	unsigned int		sls_fcntl;
	efx_link_mode_t		sls_link_mode;
#if EFSYS_OPT_LOOPBACK
	efx_loopback_type_t	sls_loopback;
#endif
	boolean_t		sls_mac_up;
} siena_link_state_t;

extern			void
siena_phy_link_ev(
	__in		efx_nic_t *enp,
	__in		efx_qword_t *eqp,
	__out		efx_link_mode_t *link_modep);

extern	__checkReturn	efx_rc_t
siena_phy_get_link(
	__in		efx_nic_t *enp,
	__out		siena_link_state_t *slsp);

extern	__checkReturn	efx_rc_t
siena_phy_power(
	__in		efx_nic_t *enp,
	__in		boolean_t on);

extern	__checkReturn	efx_rc_t
siena_phy_reconfigure(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
siena_phy_verify(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
siena_phy_oui_get(
	__in		efx_nic_t *enp,
	__out		uint32_t *ouip);

#if EFSYS_OPT_PHY_STATS

extern						void
siena_phy_decode_stats(
	__in					efx_nic_t *enp,
	__in					uint32_t vmask,
	__in_opt				efsys_mem_t *esmp,
	__out_opt				uint64_t *smaskp,
	__inout_ecount_opt(EFX_PHY_NSTATS)	uint32_t *stat);

extern	__checkReturn			efx_rc_t
siena_phy_stats_update(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__inout_ecount(EFX_PHY_NSTATS)	uint32_t *stat);

#endif	/* EFSYS_OPT_PHY_STATS */

#if EFSYS_OPT_BIST

extern	__checkReturn		efx_rc_t
siena_phy_bist_start(
	__in			efx_nic_t *enp,
	__in			efx_bist_type_t type);

extern	__checkReturn		efx_rc_t
siena_phy_bist_poll(
	__in			efx_nic_t *enp,
	__in			efx_bist_type_t type,
	__out			efx_bist_result_t *resultp,
	__out_opt __drv_when(count > 0, __notnull)
	uint32_t	*value_maskp,
	__out_ecount_opt(count)	__drv_when(count > 0, __notnull)
	unsigned long	*valuesp,
	__in			size_t count);

extern				void
siena_phy_bist_stop(
	__in			efx_nic_t *enp,
	__in			efx_bist_type_t type);

#endif	/* EFSYS_OPT_BIST */

extern	__checkReturn	efx_rc_t
siena_mac_poll(
	__in		efx_nic_t *enp,
	__out		efx_link_mode_t *link_modep);

extern	__checkReturn	efx_rc_t
siena_mac_up(
	__in		efx_nic_t *enp,
	__out		boolean_t *mac_upp);

extern	__checkReturn	efx_rc_t
siena_mac_reconfigure(
	__in	efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
siena_mac_pdu_get(
	__in	efx_nic_t *enp,
	__out	size_t *pdu);

#if EFSYS_OPT_LOOPBACK

extern	__checkReturn	efx_rc_t
siena_mac_loopback_set(
	__in		efx_nic_t *enp,
	__in		efx_link_mode_t link_mode,
	__in		efx_loopback_type_t loopback_type);

#endif	/* EFSYS_OPT_LOOPBACK */

#if EFSYS_OPT_MAC_STATS

extern	__checkReturn			efx_rc_t
siena_mac_stats_get_mask(
	__in				efx_nic_t *enp,
	__inout_bcount(mask_size)	uint32_t *maskp,
	__in				size_t mask_size);

extern	__checkReturn			efx_rc_t
siena_mac_stats_update(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__inout_ecount(EFX_MAC_NSTATS)	efsys_stat_t *stat,
	__inout_opt			uint32_t *generationp);

#endif	/* EFSYS_OPT_MAC_STATS */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SIENA_IMPL_H */
