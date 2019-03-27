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


#if EFSYS_OPT_TUNNEL

#if EFSYS_OPT_SIENA || EFSYS_OPT_HUNTINGTON
static const efx_tunnel_ops_t	__efx_tunnel_dummy_ops = {
	NULL,	/* eto_udp_encap_supported */
	NULL,	/* eto_reconfigure */
};
#endif /* EFSYS_OPT_SIENA || EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
static	__checkReturn	boolean_t
ef10_udp_encap_supported(
	__in		efx_nic_t *enp);

static	__checkReturn	efx_rc_t
ef10_tunnel_reconfigure(
	__in		efx_nic_t *enp);

static const efx_tunnel_ops_t	__efx_tunnel_ef10_ops = {
	ef10_udp_encap_supported,	/* eto_udp_encap_supported */
	ef10_tunnel_reconfigure,	/* eto_reconfigure */
};
#endif /* EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */

static	__checkReturn		efx_rc_t
efx_mcdi_set_tunnel_encap_udp_ports(
	__in			efx_nic_t *enp,
	__in			efx_tunnel_cfg_t *etcp,
	__in			boolean_t unloading,
	__out			boolean_t *resetting)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload,
		MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_LENMAX,
		MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_OUT_LEN);
	efx_word_t flags;
	efx_rc_t rc;
	unsigned int i;
	unsigned int entries_num;

	if (etcp == NULL)
		entries_num = 0;
	else
		entries_num = etcp->etc_udp_entries_num;

	req.emr_cmd = MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS;
	req.emr_in_buf = payload;
	req.emr_in_length =
	    MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_LEN(entries_num);
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_OUT_LEN;

	EFX_POPULATE_WORD_1(flags,
	    MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_UNLOADING,
	    (unloading == B_TRUE) ? 1 : 0);
	MCDI_IN_SET_WORD(req, SET_TUNNEL_ENCAP_UDP_PORTS_IN_FLAGS,
	    EFX_WORD_FIELD(flags, EFX_WORD_0));

	MCDI_IN_SET_WORD(req, SET_TUNNEL_ENCAP_UDP_PORTS_IN_NUM_ENTRIES,
	    entries_num);

	for (i = 0; i < entries_num; ++i) {
		uint16_t mcdi_udp_protocol;

		switch (etcp->etc_udp_entries[i].etue_protocol) {
		case EFX_TUNNEL_PROTOCOL_VXLAN:
			mcdi_udp_protocol = TUNNEL_ENCAP_UDP_PORT_ENTRY_VXLAN;
			break;
		case EFX_TUNNEL_PROTOCOL_GENEVE:
			mcdi_udp_protocol = TUNNEL_ENCAP_UDP_PORT_ENTRY_GENEVE;
			break;
		default:
			rc = EINVAL;
			goto fail1;
		}

		/*
		 * UDP port is MCDI native little-endian in the request
		 * and EFX_POPULATE_DWORD cares about conversion from
		 * host/CPU byte order to little-endian.
		 */
		EFX_STATIC_ASSERT(sizeof (efx_dword_t) ==
		    TUNNEL_ENCAP_UDP_PORT_ENTRY_LEN);
		EFX_POPULATE_DWORD_2(
		    MCDI_IN2(req, efx_dword_t,
			SET_TUNNEL_ENCAP_UDP_PORTS_IN_ENTRIES)[i],
		    TUNNEL_ENCAP_UDP_PORT_ENTRY_UDP_PORT,
		    etcp->etc_udp_entries[i].etue_port,
		    TUNNEL_ENCAP_UDP_PORT_ENTRY_PROTOCOL,
		    mcdi_udp_protocol);
	}

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail2;
	}

	if (req.emr_out_length_used !=
	    MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail3;
	}

	*resetting = MCDI_OUT_WORD_FIELD(req,
	    SET_TUNNEL_ENCAP_UDP_PORTS_OUT_FLAGS,
	    SET_TUNNEL_ENCAP_UDP_PORTS_OUT_RESETTING);

	return (0);

fail3:
	EFSYS_PROBE(fail3);

fail2:
	EFSYS_PROBE(fail2);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_tunnel_init(
	__in		efx_nic_t *enp)
{
	efx_tunnel_cfg_t *etcp = &enp->en_tunnel_cfg;
	const efx_tunnel_ops_t *etop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT(!(enp->en_mod_flags & EFX_MOD_TUNNEL));

	EFX_STATIC_ASSERT(EFX_TUNNEL_MAXNENTRIES ==
	    MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_ENTRIES_MAXNUM);

	switch (enp->en_family) {
#if EFSYS_OPT_SIENA
	case EFX_FAMILY_SIENA:
		etop = &__efx_tunnel_dummy_ops;
		break;
#endif /* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON
	case EFX_FAMILY_HUNTINGTON:
		etop = &__efx_tunnel_dummy_ops;
		break;
#endif /* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD
	case EFX_FAMILY_MEDFORD:
		etop = &__efx_tunnel_ef10_ops;
		break;
#endif /* EFSYS_OPT_MEDFORD */

#if EFSYS_OPT_MEDFORD2
	case EFX_FAMILY_MEDFORD2:
		etop = &__efx_tunnel_ef10_ops;
		break;
#endif /* EFSYS_OPT_MEDFORD2 */

	default:
		EFSYS_ASSERT(0);
		rc = ENOTSUP;
		goto fail1;
	}

	memset(etcp->etc_udp_entries, 0, sizeof (etcp->etc_udp_entries));
	etcp->etc_udp_entries_num = 0;

	enp->en_etop = etop;
	enp->en_mod_flags |= EFX_MOD_TUNNEL;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	enp->en_etop = NULL;
	enp->en_mod_flags &= ~EFX_MOD_TUNNEL;

	return (rc);
}

			void
efx_tunnel_fini(
	__in		efx_nic_t *enp)
{
	boolean_t resetting;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_TUNNEL);

	if ((enp->en_etop->eto_udp_encap_supported != NULL) &&
	    enp->en_etop->eto_udp_encap_supported(enp)) {
		/*
		 * The UNLOADING flag allows the MC to suppress the datapath
		 * reset if it was set on the last call to
		 * MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS by all functions
		 */
		(void) efx_mcdi_set_tunnel_encap_udp_ports(enp, NULL, B_TRUE,
		    &resetting);
	}

	enp->en_etop = NULL;
	enp->en_mod_flags &= ~EFX_MOD_TUNNEL;
}

static	__checkReturn	efx_rc_t
efx_tunnel_config_find_udp_tunnel_entry(
	__in		efx_tunnel_cfg_t *etcp,
	__in		uint16_t port,
	__out		unsigned int *entryp)
{
	unsigned int i;

	for (i = 0; i < etcp->etc_udp_entries_num; ++i) {
		efx_tunnel_udp_entry_t *p = &etcp->etc_udp_entries[i];

		if (p->etue_port == port) {
			*entryp = i;
			return (0);
		}
	}

	return (ENOENT);
}

	__checkReturn	efx_rc_t
efx_tunnel_config_udp_add(
	__in		efx_nic_t *enp,
	__in		uint16_t port /* host/cpu-endian */,
	__in		efx_tunnel_protocol_t protocol)
{
	const efx_nic_cfg_t *encp = &enp->en_nic_cfg;
	efx_tunnel_cfg_t *etcp = &enp->en_tunnel_cfg;
	efsys_lock_state_t state;
	efx_rc_t rc;
	unsigned int entry;

	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_TUNNEL);

	if (protocol >= EFX_TUNNEL_NPROTOS) {
		rc = EINVAL;
		goto fail1;
	}

	if ((encp->enc_tunnel_encapsulations_supported &
	    (1u << protocol)) == 0) {
		rc = ENOTSUP;
		goto fail2;
	}

	EFSYS_LOCK(enp->en_eslp, state);

	rc = efx_tunnel_config_find_udp_tunnel_entry(etcp, port, &entry);
	if (rc == 0) {
		rc = EEXIST;
		goto fail3;
	}

	if (etcp->etc_udp_entries_num ==
	    encp->enc_tunnel_config_udp_entries_max) {
		rc = ENOSPC;
		goto fail4;
	}

	etcp->etc_udp_entries[etcp->etc_udp_entries_num].etue_port = port;
	etcp->etc_udp_entries[etcp->etc_udp_entries_num].etue_protocol =
	    protocol;

	etcp->etc_udp_entries_num++;

	EFSYS_UNLOCK(enp->en_eslp, state);

	return (0);

fail4:
	EFSYS_PROBE(fail4);

fail3:
	EFSYS_PROBE(fail3);
	EFSYS_UNLOCK(enp->en_eslp, state);

fail2:
	EFSYS_PROBE(fail2);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_tunnel_config_udp_remove(
	__in		efx_nic_t *enp,
	__in		uint16_t port /* host/cpu-endian */,
	__in		efx_tunnel_protocol_t protocol)
{
	efx_tunnel_cfg_t *etcp = &enp->en_tunnel_cfg;
	efsys_lock_state_t state;
	unsigned int entry;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_TUNNEL);

	EFSYS_LOCK(enp->en_eslp, state);

	rc = efx_tunnel_config_find_udp_tunnel_entry(etcp, port, &entry);
	if (rc != 0)
		goto fail1;

	if (etcp->etc_udp_entries[entry].etue_protocol != protocol) {
		rc = EINVAL;
		goto fail2;
	}

	EFSYS_ASSERT3U(etcp->etc_udp_entries_num, >, 0);
	etcp->etc_udp_entries_num--;

	if (entry < etcp->etc_udp_entries_num) {
		memmove(&etcp->etc_udp_entries[entry],
		    &etcp->etc_udp_entries[entry + 1],
		    (etcp->etc_udp_entries_num - entry) *
		    sizeof (etcp->etc_udp_entries[0]));
	}

	memset(&etcp->etc_udp_entries[etcp->etc_udp_entries_num], 0,
	    sizeof (etcp->etc_udp_entries[0]));

	EFSYS_UNLOCK(enp->en_eslp, state);

	return (0);

fail2:
	EFSYS_PROBE(fail2);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	EFSYS_UNLOCK(enp->en_eslp, state);

	return (rc);
}

			void
efx_tunnel_config_clear(
	__in			efx_nic_t *enp)
{
	efx_tunnel_cfg_t *etcp = &enp->en_tunnel_cfg;
	efsys_lock_state_t state;

	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_TUNNEL);

	EFSYS_LOCK(enp->en_eslp, state);

	etcp->etc_udp_entries_num = 0;
	memset(etcp->etc_udp_entries, 0, sizeof (etcp->etc_udp_entries));

	EFSYS_UNLOCK(enp->en_eslp, state);
}

	__checkReturn	efx_rc_t
efx_tunnel_reconfigure(
	__in		efx_nic_t *enp)
{
	const efx_tunnel_ops_t *etop = enp->en_etop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_TUNNEL);

	if (etop->eto_reconfigure == NULL) {
		rc = ENOTSUP;
		goto fail1;
	}

	if ((rc = enp->en_etop->eto_reconfigure(enp)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#if EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
static	__checkReturn		boolean_t
ef10_udp_encap_supported(
	__in		efx_nic_t *enp)
{
	const efx_nic_cfg_t *encp = &enp->en_nic_cfg;
	uint32_t udp_tunnels_mask = 0;

	udp_tunnels_mask |= (1u << EFX_TUNNEL_PROTOCOL_VXLAN);
	udp_tunnels_mask |= (1u << EFX_TUNNEL_PROTOCOL_GENEVE);

	return ((encp->enc_tunnel_encapsulations_supported &
	    udp_tunnels_mask) == 0 ? B_FALSE : B_TRUE);
}

static	__checkReturn	efx_rc_t
ef10_tunnel_reconfigure(
	__in		efx_nic_t *enp)
{
	efx_tunnel_cfg_t *etcp = &enp->en_tunnel_cfg;
	efx_rc_t rc;
	boolean_t resetting;
	efsys_lock_state_t state;
	efx_tunnel_cfg_t etc;

	EFSYS_LOCK(enp->en_eslp, state);
	memcpy(&etc, etcp, sizeof (etc));
	EFSYS_UNLOCK(enp->en_eslp, state);

	if (ef10_udp_encap_supported(enp) == B_FALSE) {
		/*
		 * It is OK to apply empty UDP tunnel ports when UDP
		 * tunnel encapsulations are not supported - just nothing
		 * should be done.
		 */
		if (etc.etc_udp_entries_num == 0)
			return (0);
		rc = ENOTSUP;
		goto fail1;
	} else {
		/*
		 * All PCI functions can see a reset upon the
		 * MCDI request completion
		 */
		rc = efx_mcdi_set_tunnel_encap_udp_ports(enp, &etc, B_FALSE,
		    &resetting);
		if (rc != 0)
			goto fail2;

		/*
		 * Although the caller should be able to handle MC reboot,
		 * it might come in handy to report the impending reboot
		 * by returning EAGAIN
		 */
		return ((resetting) ? EAGAIN : 0);
	}
fail2:
	EFSYS_PROBE(fail2);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}
#endif /* EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */

#endif /* EFSYS_OPT_TUNNEL */
