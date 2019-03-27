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

#ifndef _SYS_EFX_MCDI_H
#define	_SYS_EFX_MCDI_H

#include "efx.h"
#include "efx_regs_mcdi.h"

#if EFSYS_OPT_NAMES
#include "efx_regs_mcdi_strs.h"
#endif /* EFSYS_OPT_NAMES */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * A reboot/assertion causes the MCDI status word to be set after the
 * command word is set or a REBOOT event is sent. If we notice a reboot
 * via these mechanisms then wait 10ms for the status word to be set.
 */
#define	EFX_MCDI_STATUS_SLEEP_US	10000

struct efx_mcdi_req_s {
	boolean_t	emr_quiet;
	/* Inputs: Command #, input buffer and length */
	unsigned int	emr_cmd;
	uint8_t		*emr_in_buf;
	size_t		emr_in_length;
	/* Outputs: retcode, buffer, length, and length used */
	efx_rc_t	emr_rc;
	uint8_t		*emr_out_buf;
	size_t		emr_out_length;
	size_t		emr_out_length_used;
	/* Internals: low level transport details */
	unsigned int	emr_err_code;
	unsigned int	emr_err_arg;
#if EFSYS_OPT_MCDI_PROXY_AUTH
	uint32_t	emr_proxy_handle;
#endif
};

typedef struct efx_mcdi_iface_s {
	unsigned int		emi_port;
	unsigned int		emi_max_version;
	unsigned int		emi_seq;
	efx_mcdi_req_t		*emi_pending_req;
	boolean_t		emi_ev_cpl;
	boolean_t		emi_new_epoch;
	int			emi_aborted;
	uint32_t		emi_poll_cnt;
	uint32_t		emi_mc_reboot_status;
} efx_mcdi_iface_t;

extern			void
efx_mcdi_execute(
	__in		efx_nic_t *enp,
	__inout		efx_mcdi_req_t *emrp);

extern			void
efx_mcdi_execute_quiet(
	__in		efx_nic_t *enp,
	__inout		efx_mcdi_req_t *emrp);

extern			void
efx_mcdi_ev_cpl(
	__in		efx_nic_t *enp,
	__in		unsigned int seq,
	__in		unsigned int outlen,
	__in		int errcode);

#if EFSYS_OPT_MCDI_PROXY_AUTH
extern	__checkReturn	efx_rc_t
efx_mcdi_get_proxy_handle(
	__in		efx_nic_t *enp,
	__in		efx_mcdi_req_t *emrp,
	__out		uint32_t *handlep);

extern			void
efx_mcdi_ev_proxy_response(
	__in		efx_nic_t *enp,
	__in		unsigned int handle,
	__in		unsigned int status);
#endif

extern			void
efx_mcdi_ev_death(
	__in		efx_nic_t *enp,
	__in		int rc);

extern	__checkReturn	efx_rc_t
efx_mcdi_request_errcode(
	__in		unsigned int err);

extern			void
efx_mcdi_raise_exception(
	__in		efx_nic_t *enp,
	__in_opt	efx_mcdi_req_t *emrp,
	__in		int rc);

typedef enum efx_mcdi_boot_e {
	EFX_MCDI_BOOT_PRIMARY,
	EFX_MCDI_BOOT_SECONDARY,
	EFX_MCDI_BOOT_ROM,
} efx_mcdi_boot_t;

extern	__checkReturn		efx_rc_t
efx_mcdi_version(
	__in			efx_nic_t *enp,
	__out_ecount_opt(4)	uint16_t versionp[4],
	__out_opt		uint32_t *buildp,
	__out_opt		efx_mcdi_boot_t *statusp);

extern	__checkReturn	efx_rc_t
efx_mcdi_get_capabilities(
	__in		efx_nic_t *enp,
	__out_opt	uint32_t *flagsp,
	__out_opt	uint16_t *rx_dpcpu_fw_idp,
	__out_opt	uint16_t *tx_dpcpu_fw_idp,
	__out_opt	uint32_t *flags2p,
	__out_opt	uint32_t *tso2ncp);

extern	__checkReturn		efx_rc_t
efx_mcdi_read_assertion(
	__in			efx_nic_t *enp);

extern	__checkReturn		efx_rc_t
efx_mcdi_exit_assertion_handler(
	__in			efx_nic_t *enp);

extern	__checkReturn		efx_rc_t
efx_mcdi_drv_attach(
	__in			efx_nic_t *enp,
	__in			boolean_t attach);

extern	__checkReturn		efx_rc_t
efx_mcdi_get_board_cfg(
	__in			efx_nic_t *enp,
	__out_opt		uint32_t *board_typep,
	__out_opt		efx_dword_t *capabilitiesp,
	__out_ecount_opt(6)	uint8_t mac_addrp[6]);

extern	__checkReturn		efx_rc_t
efx_mcdi_get_phy_cfg(
	__in			efx_nic_t *enp);

extern	__checkReturn		efx_rc_t
efx_mcdi_firmware_update_supported(
	__in			efx_nic_t *enp,
	__out			boolean_t *supportedp);

extern	__checkReturn		efx_rc_t
efx_mcdi_macaddr_change_supported(
	__in			efx_nic_t *enp,
	__out			boolean_t *supportedp);

extern	__checkReturn		efx_rc_t
efx_mcdi_link_control_supported(
	__in			efx_nic_t *enp,
	__out			boolean_t *supportedp);

extern	__checkReturn		efx_rc_t
efx_mcdi_mac_spoofing_supported(
	__in			efx_nic_t *enp,
	__out			boolean_t *supportedp);


#if EFSYS_OPT_BIST
#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
extern	__checkReturn		efx_rc_t
efx_mcdi_bist_enable_offline(
	__in			efx_nic_t *enp);
#endif /* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */
extern	__checkReturn		efx_rc_t
efx_mcdi_bist_start(
	__in			efx_nic_t *enp,
	__in			efx_bist_type_t type);
#endif /* EFSYS_OPT_BIST */

extern	__checkReturn		efx_rc_t
efx_mcdi_get_resource_limits(
	__in			efx_nic_t *enp,
	__out_opt		uint32_t *nevqp,
	__out_opt		uint32_t *nrxqp,
	__out_opt		uint32_t *ntxqp);

extern	__checkReturn	efx_rc_t
efx_mcdi_log_ctrl(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
efx_mcdi_mac_stats_clear(
	__in		efx_nic_t *enp);

extern	__checkReturn	efx_rc_t
efx_mcdi_mac_stats_upload(
	__in		efx_nic_t *enp,
	__in		efsys_mem_t *esmp);

extern	__checkReturn	efx_rc_t
efx_mcdi_mac_stats_periodic(
	__in		efx_nic_t *enp,
	__in		efsys_mem_t *esmp,
	__in		uint16_t period_ms,
	__in		boolean_t events);


#if EFSYS_OPT_LOOPBACK
extern	__checkReturn	efx_rc_t
efx_mcdi_get_loopback_modes(
	__in		efx_nic_t *enp);
#endif /* EFSYS_OPT_LOOPBACK */

extern	__checkReturn	efx_rc_t
efx_mcdi_phy_module_get_info(
	__in			efx_nic_t *enp,
	__in			uint8_t dev_addr,
	__in			size_t offset,
	__in			size_t len,
	__out_bcount(len)	uint8_t *data);

#define	MCDI_IN(_emr, _type, _ofst)					\
	((_type *)((_emr).emr_in_buf + (_ofst)))

#define	MCDI_IN2(_emr, _type, _ofst)					\
	MCDI_IN(_emr, _type, MC_CMD_ ## _ofst ## _OFST)

#define	MCDI_IN_SET_BYTE(_emr, _ofst, _value)				\
	EFX_POPULATE_BYTE_1(*MCDI_IN2(_emr, efx_byte_t, _ofst),		\
		EFX_BYTE_0, _value)

#define	MCDI_IN_SET_WORD(_emr, _ofst, _value)				\
	EFX_POPULATE_WORD_1(*MCDI_IN2(_emr, efx_word_t, _ofst),		\
		EFX_WORD_0, _value)

#define	MCDI_IN_SET_DWORD(_emr, _ofst, _value)				\
	EFX_POPULATE_DWORD_1(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		EFX_DWORD_0, _value)

#define	MCDI_IN_SET_DWORD_FIELD(_emr, _ofst, _field, _value)		\
	EFX_SET_DWORD_FIELD(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		MC_CMD_ ## _field, _value)

#define	MCDI_IN_POPULATE_DWORD_1(_emr, _ofst, _field1, _value1)		\
	EFX_POPULATE_DWORD_1(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		MC_CMD_ ## _field1, _value1)

#define	MCDI_IN_POPULATE_DWORD_2(_emr, _ofst, _field1, _value1,		\
		_field2, _value2)					\
	EFX_POPULATE_DWORD_2(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		MC_CMD_ ## _field1, _value1,				\
		MC_CMD_ ## _field2, _value2)

#define	MCDI_IN_POPULATE_DWORD_3(_emr, _ofst, _field1, _value1,		\
		_field2, _value2, _field3, _value3)			\
	EFX_POPULATE_DWORD_3(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		MC_CMD_ ## _field1, _value1,				\
		MC_CMD_ ## _field2, _value2,				\
		MC_CMD_ ## _field3, _value3)

#define	MCDI_IN_POPULATE_DWORD_4(_emr, _ofst, _field1, _value1,		\
		_field2, _value2, _field3, _value3, _field4, _value4)	\
	EFX_POPULATE_DWORD_4(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		MC_CMD_ ## _field1, _value1,				\
		MC_CMD_ ## _field2, _value2,				\
		MC_CMD_ ## _field3, _value3,				\
		MC_CMD_ ## _field4, _value4)

#define	MCDI_IN_POPULATE_DWORD_5(_emr, _ofst, _field1, _value1,		\
		_field2, _value2, _field3, _value3, _field4, _value4,	\
		_field5, _value5)					\
	EFX_POPULATE_DWORD_5(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		MC_CMD_ ## _field1, _value1,				\
		MC_CMD_ ## _field2, _value2,				\
		MC_CMD_ ## _field3, _value3,				\
		MC_CMD_ ## _field4, _value4,				\
		MC_CMD_ ## _field5, _value5)

#define	MCDI_IN_POPULATE_DWORD_6(_emr, _ofst, _field1, _value1,		\
		_field2, _value2, _field3, _value3, _field4, _value4,	\
		_field5, _value5, _field6, _value6)			\
	EFX_POPULATE_DWORD_6(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		MC_CMD_ ## _field1, _value1,				\
		MC_CMD_ ## _field2, _value2,				\
		MC_CMD_ ## _field3, _value3,				\
		MC_CMD_ ## _field4, _value4,				\
		MC_CMD_ ## _field5, _value5,				\
		MC_CMD_ ## _field6, _value6)

#define	MCDI_IN_POPULATE_DWORD_7(_emr, _ofst, _field1, _value1,		\
		_field2, _value2, _field3, _value3, _field4, _value4,	\
		_field5, _value5, _field6, _value6, _field7, _value7)	\
	EFX_POPULATE_DWORD_7(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		MC_CMD_ ## _field1, _value1,				\
		MC_CMD_ ## _field2, _value2,				\
		MC_CMD_ ## _field3, _value3,				\
		MC_CMD_ ## _field4, _value4,				\
		MC_CMD_ ## _field5, _value5,				\
		MC_CMD_ ## _field6, _value6,				\
		MC_CMD_ ## _field7, _value7)

#define	MCDI_IN_POPULATE_DWORD_8(_emr, _ofst, _field1, _value1,		\
		_field2, _value2, _field3, _value3, _field4, _value4,	\
		_field5, _value5, _field6, _value6, _field7, _value7,	\
		_field8, _value8)					\
	EFX_POPULATE_DWORD_8(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		MC_CMD_ ## _field1, _value1,				\
		MC_CMD_ ## _field2, _value2,				\
		MC_CMD_ ## _field3, _value3,				\
		MC_CMD_ ## _field4, _value4,				\
		MC_CMD_ ## _field5, _value5,				\
		MC_CMD_ ## _field6, _value6,				\
		MC_CMD_ ## _field7, _value7,				\
		MC_CMD_ ## _field8, _value8)

#define	MCDI_IN_POPULATE_DWORD_9(_emr, _ofst, _field1, _value1,		\
		_field2, _value2, _field3, _value3, _field4, _value4,	\
		_field5, _value5, _field6, _value6, _field7, _value7,	\
		_field8, _value8, _field9, _value9)			\
	EFX_POPULATE_DWORD_9(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		MC_CMD_ ## _field1, _value1,				\
		MC_CMD_ ## _field2, _value2,				\
		MC_CMD_ ## _field3, _value3,				\
		MC_CMD_ ## _field4, _value4,				\
		MC_CMD_ ## _field5, _value5,				\
		MC_CMD_ ## _field6, _value6,				\
		MC_CMD_ ## _field7, _value7,				\
		MC_CMD_ ## _field8, _value8,				\
		MC_CMD_ ## _field9, _value9)

#define	MCDI_IN_POPULATE_DWORD_10(_emr, _ofst, _field1, _value1,	\
		_field2, _value2, _field3, _value3, _field4, _value4,	\
		_field5, _value5, _field6, _value6, _field7, _value7,	\
		_field8, _value8, _field9, _value9, _field10, _value10)	\
	EFX_POPULATE_DWORD_10(*MCDI_IN2(_emr, efx_dword_t, _ofst),	\
		MC_CMD_ ## _field1, _value1,				\
		MC_CMD_ ## _field2, _value2,				\
		MC_CMD_ ## _field3, _value3,				\
		MC_CMD_ ## _field4, _value4,				\
		MC_CMD_ ## _field5, _value5,				\
		MC_CMD_ ## _field6, _value6,				\
		MC_CMD_ ## _field7, _value7,				\
		MC_CMD_ ## _field8, _value8,				\
		MC_CMD_ ## _field9, _value9,				\
		MC_CMD_ ## _field10, _value10)

#define	MCDI_OUT(_emr, _type, _ofst)					\
	((_type *)((_emr).emr_out_buf + (_ofst)))

#define	MCDI_OUT2(_emr, _type, _ofst)					\
	MCDI_OUT(_emr, _type, MC_CMD_ ## _ofst ## _OFST)

#define	MCDI_OUT_BYTE(_emr, _ofst)					\
	EFX_BYTE_FIELD(*MCDI_OUT2(_emr, efx_byte_t, _ofst),		\
		    EFX_BYTE_0)

#define	MCDI_OUT_WORD(_emr, _ofst)					\
	EFX_WORD_FIELD(*MCDI_OUT2(_emr, efx_word_t, _ofst),		\
		    EFX_WORD_0)

#define	MCDI_OUT_WORD_FIELD(_emr, _ofst, _field)			\
	EFX_WORD_FIELD(*MCDI_OUT2(_emr, efx_word_t, _ofst),		\
		       MC_CMD_ ## _field)

#define	MCDI_OUT_DWORD(_emr, _ofst)					\
	EFX_DWORD_FIELD(*MCDI_OUT2(_emr, efx_dword_t, _ofst),		\
			EFX_DWORD_0)

#define	MCDI_OUT_DWORD_FIELD(_emr, _ofst, _field)			\
	EFX_DWORD_FIELD(*MCDI_OUT2(_emr, efx_dword_t, _ofst),		\
			MC_CMD_ ## _field)

#define	MCDI_EV_FIELD(_eqp, _field)					\
	EFX_QWORD_FIELD(*_eqp, MCDI_EVENT_ ## _field)

#define	MCDI_CMD_DWORD_FIELD(_edp, _field)				\
	EFX_DWORD_FIELD(*_edp, MC_CMD_ ## _field)

#define	EFX_MCDI_HAVE_PRIVILEGE(mask, priv)				\
	(((mask) & (MC_CMD_PRIVILEGE_MASK_IN_GRP_ ## priv)) ==		\
	(MC_CMD_PRIVILEGE_MASK_IN_GRP_ ## priv))

/*
 * The buffer size must be a multiple of dword to ensure that MCDI works
 * properly with Siena based boards (which use on-chip buffer). Also, it
 * should be at minimum the size of two dwords to allow space for extended
 * error responses if the request/response buffer sizes are smaller.
 */
#define EFX_MCDI_DECLARE_BUF(_name, _in_len, _out_len)			\
	uint8_t _name[P2ROUNDUP(MAX(MAX(_in_len, _out_len),		\
				    (2 * sizeof (efx_dword_t))),	\
				sizeof (efx_dword_t))] = {0}

typedef enum efx_mcdi_feature_id_e {
	EFX_MCDI_FEATURE_FW_UPDATE = 0,
	EFX_MCDI_FEATURE_LINK_CONTROL,
	EFX_MCDI_FEATURE_MACADDR_CHANGE,
	EFX_MCDI_FEATURE_MAC_SPOOFING,
	EFX_MCDI_FEATURE_NIDS
} efx_mcdi_feature_id_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_EFX_MCDI_H */
