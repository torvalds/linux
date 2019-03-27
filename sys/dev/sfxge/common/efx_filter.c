/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2016 Solarflare Communications Inc.
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


#if EFSYS_OPT_FILTER

#if EFSYS_OPT_SIENA

static	__checkReturn	efx_rc_t
siena_filter_init(
	__in		efx_nic_t *enp);

static			void
siena_filter_fini(
	__in		efx_nic_t *enp);

static	__checkReturn	efx_rc_t
siena_filter_restore(
	__in		efx_nic_t *enp);

static	__checkReturn	efx_rc_t
siena_filter_add(
	__in		efx_nic_t *enp,
	__inout		efx_filter_spec_t *spec,
	__in		boolean_t may_replace);

static	__checkReturn	efx_rc_t
siena_filter_delete(
	__in		efx_nic_t *enp,
	__inout		efx_filter_spec_t *spec);

static	__checkReturn	efx_rc_t
siena_filter_supported_filters(
	__in				efx_nic_t *enp,
	__out_ecount(buffer_length)	uint32_t *buffer,
	__in				size_t buffer_length,
	__out				size_t *list_lengthp);

#endif /* EFSYS_OPT_SIENA */

#if EFSYS_OPT_SIENA
static const efx_filter_ops_t	__efx_filter_siena_ops = {
	siena_filter_init,		/* efo_init */
	siena_filter_fini,		/* efo_fini */
	siena_filter_restore,		/* efo_restore */
	siena_filter_add,		/* efo_add */
	siena_filter_delete,		/* efo_delete */
	siena_filter_supported_filters,	/* efo_supported_filters */
	NULL,				/* efo_reconfigure */
};
#endif /* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
static const efx_filter_ops_t	__efx_filter_ef10_ops = {
	ef10_filter_init,		/* efo_init */
	ef10_filter_fini,		/* efo_fini */
	ef10_filter_restore,		/* efo_restore */
	ef10_filter_add,		/* efo_add */
	ef10_filter_delete,		/* efo_delete */
	ef10_filter_supported_filters,	/* efo_supported_filters */
	ef10_filter_reconfigure,	/* efo_reconfigure */
};
#endif /* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */

	__checkReturn	efx_rc_t
efx_filter_insert(
	__in		efx_nic_t *enp,
	__inout		efx_filter_spec_t *spec)
{
	const efx_filter_ops_t *efop = enp->en_efop;
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_FILTER);
	EFSYS_ASSERT3P(spec, !=, NULL);
	EFSYS_ASSERT3U(spec->efs_flags, &, EFX_FILTER_FLAG_RX);

	if ((spec->efs_flags & EFX_FILTER_FLAG_ACTION_MARK) &&
	    !encp->enc_filter_action_mark_supported) {
		rc = ENOTSUP;
		goto fail1;
	}

	if ((spec->efs_flags & EFX_FILTER_FLAG_ACTION_FLAG) &&
	    !encp->enc_filter_action_flag_supported) {
		rc = ENOTSUP;
		goto fail2;
	}

	return (efop->efo_add(enp, spec, B_FALSE));

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_filter_remove(
	__in		efx_nic_t *enp,
	__inout		efx_filter_spec_t *spec)
{
	const efx_filter_ops_t *efop = enp->en_efop;

	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_FILTER);
	EFSYS_ASSERT3P(spec, !=, NULL);
	EFSYS_ASSERT3U(spec->efs_flags, &, EFX_FILTER_FLAG_RX);

	return (efop->efo_delete(enp, spec));
}

	__checkReturn	efx_rc_t
efx_filter_restore(
	__in		efx_nic_t *enp)
{
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_FILTER);

	if ((rc = enp->en_efop->efo_restore(enp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_filter_init(
	__in		efx_nic_t *enp)
{
	const efx_filter_ops_t *efop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT(!(enp->en_mod_flags & EFX_MOD_FILTER));

	switch (enp->en_family) {
#if EFSYS_OPT_SIENA
	case EFX_FAMILY_SIENA:
		efop = &__efx_filter_siena_ops;
		break;
#endif /* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON
	case EFX_FAMILY_HUNTINGTON:
		efop = &__efx_filter_ef10_ops;
		break;
#endif /* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD
	case EFX_FAMILY_MEDFORD:
		efop = &__efx_filter_ef10_ops;
		break;
#endif /* EFSYS_OPT_MEDFORD */

#if EFSYS_OPT_MEDFORD2
	case EFX_FAMILY_MEDFORD2:
		efop = &__efx_filter_ef10_ops;
		break;
#endif /* EFSYS_OPT_MEDFORD2 */

	default:
		EFSYS_ASSERT(0);
		rc = ENOTSUP;
		goto fail1;
	}

	if ((rc = efop->efo_init(enp)) != 0)
		goto fail2;

	enp->en_efop = efop;
	enp->en_mod_flags |= EFX_MOD_FILTER;
	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	enp->en_efop = NULL;
	enp->en_mod_flags &= ~EFX_MOD_FILTER;
	return (rc);
}

			void
efx_filter_fini(
	__in		efx_nic_t *enp)
{
	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_FILTER);

	enp->en_efop->efo_fini(enp);

	enp->en_efop = NULL;
	enp->en_mod_flags &= ~EFX_MOD_FILTER;
}

/*
 * Query the possible combinations of match flags which can be filtered on.
 * These are returned as a list, of which each 32 bit element is a bitmask
 * formed of EFX_FILTER_MATCH flags.
 *
 * The combinations are ordered in priority from highest to lowest.
 *
 * If the provided buffer is too short to hold the list, the call with fail with
 * ENOSPC and *list_lengthp will be set to the buffer length required.
 */
	__checkReturn	efx_rc_t
efx_filter_supported_filters(
	__in				efx_nic_t *enp,
	__out_ecount(buffer_length)	uint32_t *buffer,
	__in				size_t buffer_length,
	__out				size_t *list_lengthp)
{
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_FILTER);
	EFSYS_ASSERT(enp->en_efop->efo_supported_filters != NULL);

	if (buffer == NULL) {
		rc = EINVAL;
		goto fail1;
	}

	rc = enp->en_efop->efo_supported_filters(enp, buffer, buffer_length,
						    list_lengthp);
	if (rc != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_filter_reconfigure(
	__in				efx_nic_t *enp,
	__in_ecount(6)			uint8_t const *mac_addr,
	__in				boolean_t all_unicst,
	__in				boolean_t mulcst,
	__in				boolean_t all_mulcst,
	__in				boolean_t brdcst,
	__in_ecount(6*count)		uint8_t const *addrs,
	__in				uint32_t count)
{
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_FILTER);

	if (enp->en_efop->efo_reconfigure != NULL) {
		if ((rc = enp->en_efop->efo_reconfigure(enp, mac_addr,
							all_unicst, mulcst,
							all_mulcst, brdcst,
							addrs, count)) != 0)
			goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

		void
efx_filter_spec_init_rx(
	__out		efx_filter_spec_t *spec,
	__in		efx_filter_priority_t priority,
	__in		efx_filter_flags_t flags,
	__in		efx_rxq_t *erp)
{
	EFSYS_ASSERT3P(spec, !=, NULL);
	EFSYS_ASSERT3P(erp, !=, NULL);
	EFSYS_ASSERT((flags & ~(EFX_FILTER_FLAG_RX_RSS |
				EFX_FILTER_FLAG_RX_SCATTER)) == 0);

	memset(spec, 0, sizeof (*spec));
	spec->efs_priority = priority;
	spec->efs_flags = EFX_FILTER_FLAG_RX | flags;
	spec->efs_rss_context = EFX_RSS_CONTEXT_DEFAULT;
	spec->efs_dmaq_id = (uint16_t)erp->er_index;
}

		void
efx_filter_spec_init_tx(
	__out		efx_filter_spec_t *spec,
	__in		efx_txq_t *etp)
{
	EFSYS_ASSERT3P(spec, !=, NULL);
	EFSYS_ASSERT3P(etp, !=, NULL);

	memset(spec, 0, sizeof (*spec));
	spec->efs_priority = EFX_FILTER_PRI_REQUIRED;
	spec->efs_flags = EFX_FILTER_FLAG_TX;
	spec->efs_dmaq_id = (uint16_t)etp->et_index;
}


/*
 *  Specify IPv4 host, transport protocol and port in a filter specification
 */
__checkReturn		efx_rc_t
efx_filter_spec_set_ipv4_local(
	__inout		efx_filter_spec_t *spec,
	__in		uint8_t proto,
	__in		uint32_t host,
	__in		uint16_t port)
{
	EFSYS_ASSERT3P(spec, !=, NULL);

	spec->efs_match_flags |=
		EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_IP_PROTO |
		EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_LOC_PORT;
	spec->efs_ether_type = EFX_ETHER_TYPE_IPV4;
	spec->efs_ip_proto = proto;
	spec->efs_loc_host.eo_u32[0] = host;
	spec->efs_loc_port = port;
	return (0);
}

/*
 * Specify IPv4 hosts, transport protocol and ports in a filter specification
 */
__checkReturn		efx_rc_t
efx_filter_spec_set_ipv4_full(
	__inout		efx_filter_spec_t *spec,
	__in		uint8_t proto,
	__in		uint32_t lhost,
	__in		uint16_t lport,
	__in		uint32_t rhost,
	__in		uint16_t rport)
{
	EFSYS_ASSERT3P(spec, !=, NULL);

	spec->efs_match_flags |=
		EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_IP_PROTO |
		EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_LOC_PORT |
		EFX_FILTER_MATCH_REM_HOST | EFX_FILTER_MATCH_REM_PORT;
	spec->efs_ether_type = EFX_ETHER_TYPE_IPV4;
	spec->efs_ip_proto = proto;
	spec->efs_loc_host.eo_u32[0] = lhost;
	spec->efs_loc_port = lport;
	spec->efs_rem_host.eo_u32[0] = rhost;
	spec->efs_rem_port = rport;
	return (0);
}

/*
 * Specify local Ethernet address and/or VID in filter specification
 */
__checkReturn		efx_rc_t
efx_filter_spec_set_eth_local(
	__inout		efx_filter_spec_t *spec,
	__in		uint16_t vid,
	__in		const uint8_t *addr)
{
	EFSYS_ASSERT3P(spec, !=, NULL);
	EFSYS_ASSERT3P(addr, !=, NULL);

	if (vid == EFX_FILTER_SPEC_VID_UNSPEC && addr == NULL)
		return (EINVAL);

	if (vid != EFX_FILTER_SPEC_VID_UNSPEC) {
		spec->efs_match_flags |= EFX_FILTER_MATCH_OUTER_VID;
		spec->efs_outer_vid = vid;
	}
	if (addr != NULL) {
		spec->efs_match_flags |= EFX_FILTER_MATCH_LOC_MAC;
		memcpy(spec->efs_loc_mac, addr, EFX_MAC_ADDR_LEN);
	}
	return (0);
}

			void
efx_filter_spec_set_ether_type(
	__inout		efx_filter_spec_t *spec,
	__in		uint16_t ether_type)
{
	EFSYS_ASSERT3P(spec, !=, NULL);

	spec->efs_ether_type = ether_type;
	spec->efs_match_flags |= EFX_FILTER_MATCH_ETHER_TYPE;
}

/*
 * Specify matching otherwise-unmatched unicast in a filter specification
 */
__checkReturn		efx_rc_t
efx_filter_spec_set_uc_def(
	__inout		efx_filter_spec_t *spec)
{
	EFSYS_ASSERT3P(spec, !=, NULL);

	spec->efs_match_flags |= EFX_FILTER_MATCH_UNKNOWN_UCAST_DST;
	return (0);
}

/*
 * Specify matching otherwise-unmatched multicast in a filter specification
 */
__checkReturn		efx_rc_t
efx_filter_spec_set_mc_def(
	__inout		efx_filter_spec_t *spec)
{
	EFSYS_ASSERT3P(spec, !=, NULL);

	spec->efs_match_flags |= EFX_FILTER_MATCH_UNKNOWN_MCAST_DST;
	return (0);
}


__checkReturn		efx_rc_t
efx_filter_spec_set_encap_type(
	__inout		efx_filter_spec_t *spec,
	__in		efx_tunnel_protocol_t encap_type,
	__in		efx_filter_inner_frame_match_t inner_frame_match)
{
	uint32_t match_flags = EFX_FILTER_MATCH_ENCAP_TYPE;
	uint8_t ip_proto;
	efx_rc_t rc;

	EFSYS_ASSERT3P(spec, !=, NULL);

	switch (encap_type) {
	case EFX_TUNNEL_PROTOCOL_VXLAN:
	case EFX_TUNNEL_PROTOCOL_GENEVE:
		ip_proto = EFX_IPPROTO_UDP;
		break;
	case EFX_TUNNEL_PROTOCOL_NVGRE:
		ip_proto = EFX_IPPROTO_GRE;
		break;
	default:
		EFSYS_ASSERT(0);
		rc = EINVAL;
		goto fail1;
	}

	switch (inner_frame_match) {
	case EFX_FILTER_INNER_FRAME_MATCH_UNKNOWN_MCAST_DST:
		match_flags |= EFX_FILTER_MATCH_IFRM_UNKNOWN_MCAST_DST;
		break;
	case EFX_FILTER_INNER_FRAME_MATCH_UNKNOWN_UCAST_DST:
		match_flags |= EFX_FILTER_MATCH_IFRM_UNKNOWN_UCAST_DST;
		break;
	case EFX_FILTER_INNER_FRAME_MATCH_OTHER:
		/* This is for when specific inner frames are to be matched. */
		break;
	default:
		EFSYS_ASSERT(0);
		rc = EINVAL;
		goto fail2;
	}

	spec->efs_encap_type = encap_type;
	spec->efs_ip_proto = ip_proto;
	spec->efs_match_flags |= (match_flags | EFX_FILTER_MATCH_IP_PROTO);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * Specify inner and outer Ethernet address and VNI or VSID in tunnel filter
 * specification.
 */
static	__checkReturn	efx_rc_t
efx_filter_spec_set_tunnel(
	__inout	efx_filter_spec_t *spec,
	__in		efx_tunnel_protocol_t encap_type,
	__in		const uint8_t *vni_or_vsid,
	__in		const uint8_t *inner_addr,
	__in		const uint8_t *outer_addr)
{
	efx_rc_t rc;

	EFSYS_ASSERT3P(spec, !=, NULL);
	EFSYS_ASSERT3P(vni_or_vsid, !=, NULL);
	EFSYS_ASSERT3P(inner_addr, !=, NULL);
	EFSYS_ASSERT3P(outer_addr, !=, NULL);

	switch (encap_type) {
	case EFX_TUNNEL_PROTOCOL_VXLAN:
	case EFX_TUNNEL_PROTOCOL_GENEVE:
	case EFX_TUNNEL_PROTOCOL_NVGRE:
		break;
	default:
		rc = EINVAL;
		goto fail1;
	}

	if ((inner_addr == NULL) && (outer_addr == NULL)) {
		rc = EINVAL;
		goto fail2;
	}

	if (vni_or_vsid != NULL) {
		spec->efs_match_flags |= EFX_FILTER_MATCH_VNI_OR_VSID;
		memcpy(spec->efs_vni_or_vsid, vni_or_vsid, EFX_VNI_OR_VSID_LEN);
	}
	if (outer_addr != NULL) {
		spec->efs_match_flags |= EFX_FILTER_MATCH_LOC_MAC;
		memcpy(spec->efs_loc_mac, outer_addr, EFX_MAC_ADDR_LEN);
	}
	if (inner_addr != NULL) {
		spec->efs_match_flags |= EFX_FILTER_MATCH_IFRM_LOC_MAC;
		memcpy(spec->efs_ifrm_loc_mac, inner_addr, EFX_MAC_ADDR_LEN);
	}

	spec->efs_match_flags |= EFX_FILTER_MATCH_ENCAP_TYPE;
	spec->efs_encap_type = encap_type;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * Specify inner and outer Ethernet address and VNI in VXLAN filter
 * specification.
 */
__checkReturn		efx_rc_t
efx_filter_spec_set_vxlan(
	__inout		efx_filter_spec_t *spec,
	__in		const uint8_t *vni,
	__in		const uint8_t *inner_addr,
	__in		const uint8_t *outer_addr)
{
	return efx_filter_spec_set_tunnel(spec, EFX_TUNNEL_PROTOCOL_VXLAN,
	    vni, inner_addr, outer_addr);
}

/*
 * Specify inner and outer Ethernet address and VNI in Geneve filter
 * specification.
 */
__checkReturn		efx_rc_t
efx_filter_spec_set_geneve(
	__inout		efx_filter_spec_t *spec,
	__in		const uint8_t *vni,
	__in		const uint8_t *inner_addr,
	__in		const uint8_t *outer_addr)
{
	return efx_filter_spec_set_tunnel(spec, EFX_TUNNEL_PROTOCOL_GENEVE,
	    vni, inner_addr, outer_addr);
}

/*
 * Specify inner and outer Ethernet address and vsid in NVGRE filter
 * specification.
 */
__checkReturn		efx_rc_t
efx_filter_spec_set_nvgre(
	__inout		efx_filter_spec_t *spec,
	__in		const uint8_t *vsid,
	__in		const uint8_t *inner_addr,
	__in		const uint8_t *outer_addr)
{
	return efx_filter_spec_set_tunnel(spec, EFX_TUNNEL_PROTOCOL_NVGRE,
	    vsid, inner_addr, outer_addr);
}

#if EFSYS_OPT_RX_SCALE
	__checkReturn	efx_rc_t
efx_filter_spec_set_rss_context(
	__inout		efx_filter_spec_t *spec,
	__in		uint32_t rss_context)
{
	efx_rc_t rc;

	EFSYS_ASSERT3P(spec, !=, NULL);

	/* The filter must have been created with EFX_FILTER_FLAG_RX_RSS. */
	if ((spec->efs_flags & EFX_FILTER_FLAG_RX_RSS) == 0) {
		rc = EINVAL;
		goto fail1;
	}

	spec->efs_rss_context = rss_context;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}
#endif

#if EFSYS_OPT_SIENA

/*
 * "Fudge factors" - difference between programmed value and actual depth.
 * Due to pipelined implementation we need to program H/W with a value that
 * is larger than the hop limit we want.
 */
#define	FILTER_CTL_SRCH_FUDGE_WILD 3
#define	FILTER_CTL_SRCH_FUDGE_FULL 1

/*
 * Hard maximum hop limit.  Hardware will time-out beyond 200-something.
 * We also need to avoid infinite loops in efx_filter_search() when the
 * table is full.
 */
#define	FILTER_CTL_SRCH_MAX 200

static	__checkReturn	efx_rc_t
siena_filter_spec_from_gen_spec(
	__out		siena_filter_spec_t *sf_spec,
	__in		efx_filter_spec_t *gen_spec)
{
	efx_rc_t rc;
	boolean_t is_full = B_FALSE;

	if (gen_spec->efs_flags & EFX_FILTER_FLAG_TX)
		EFSYS_ASSERT3U(gen_spec->efs_flags, ==, EFX_FILTER_FLAG_TX);
	else
		EFSYS_ASSERT3U(gen_spec->efs_flags, &, EFX_FILTER_FLAG_RX);

	/* Siena only has one RSS context */
	if ((gen_spec->efs_flags & EFX_FILTER_FLAG_RX_RSS) &&
	    gen_spec->efs_rss_context != EFX_RSS_CONTEXT_DEFAULT) {
		rc = EINVAL;
		goto fail1;
	}

	sf_spec->sfs_flags = gen_spec->efs_flags;
	sf_spec->sfs_dmaq_id = gen_spec->efs_dmaq_id;

	switch (gen_spec->efs_match_flags) {
	case EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_IP_PROTO |
	    EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_LOC_PORT |
	    EFX_FILTER_MATCH_REM_HOST | EFX_FILTER_MATCH_REM_PORT:
		is_full = B_TRUE;
		/* Fall through */
	case EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_IP_PROTO |
	    EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_LOC_PORT: {
		uint32_t rhost, host1, host2;
		uint16_t rport, port1, port2;

		if (gen_spec->efs_ether_type != EFX_ETHER_TYPE_IPV4) {
			rc = ENOTSUP;
			goto fail2;
		}
		if (gen_spec->efs_loc_port == 0 ||
		    (is_full && gen_spec->efs_rem_port == 0)) {
			rc = EINVAL;
			goto fail3;
		}
		switch (gen_spec->efs_ip_proto) {
		case EFX_IPPROTO_TCP:
			if (gen_spec->efs_flags & EFX_FILTER_FLAG_TX) {
				sf_spec->sfs_type = (is_full ?
				    EFX_SIENA_FILTER_TX_TCP_FULL :
				    EFX_SIENA_FILTER_TX_TCP_WILD);
			} else {
				sf_spec->sfs_type = (is_full ?
				    EFX_SIENA_FILTER_RX_TCP_FULL :
				    EFX_SIENA_FILTER_RX_TCP_WILD);
			}
			break;
		case EFX_IPPROTO_UDP:
			if (gen_spec->efs_flags & EFX_FILTER_FLAG_TX) {
				sf_spec->sfs_type = (is_full ?
				    EFX_SIENA_FILTER_TX_UDP_FULL :
				    EFX_SIENA_FILTER_TX_UDP_WILD);
			} else {
				sf_spec->sfs_type = (is_full ?
				    EFX_SIENA_FILTER_RX_UDP_FULL :
				    EFX_SIENA_FILTER_RX_UDP_WILD);
			}
			break;
		default:
			rc = ENOTSUP;
			goto fail4;
		}
		/*
		 * The filter is constructed in terms of source and destination,
		 * with the odd wrinkle that the ports are swapped in a UDP
		 * wildcard filter. We need to convert from local and remote
		 * addresses (zero for a wildcard).
		 */
		rhost = is_full ? gen_spec->efs_rem_host.eo_u32[0] : 0;
		rport = is_full ? gen_spec->efs_rem_port : 0;
		if (gen_spec->efs_flags & EFX_FILTER_FLAG_TX) {
			host1 = gen_spec->efs_loc_host.eo_u32[0];
			host2 = rhost;
		} else {
			host1 = rhost;
			host2 = gen_spec->efs_loc_host.eo_u32[0];
		}
		if (gen_spec->efs_flags & EFX_FILTER_FLAG_TX) {
			if (sf_spec->sfs_type ==
			    EFX_SIENA_FILTER_TX_UDP_WILD) {
				port1 = rport;
				port2 = gen_spec->efs_loc_port;
			} else {
				port1 = gen_spec->efs_loc_port;
				port2 = rport;
			}
		} else {
			if (sf_spec->sfs_type ==
			    EFX_SIENA_FILTER_RX_UDP_WILD) {
				port1 = gen_spec->efs_loc_port;
				port2 = rport;
			} else {
				port1 = rport;
				port2 = gen_spec->efs_loc_port;
			}
		}
		sf_spec->sfs_dword[0] = (host1 << 16) | port1;
		sf_spec->sfs_dword[1] = (port2 << 16) | (host1 >> 16);
		sf_spec->sfs_dword[2] = host2;
		break;
	}

	case EFX_FILTER_MATCH_LOC_MAC | EFX_FILTER_MATCH_OUTER_VID:
		is_full = B_TRUE;
		/* Fall through */
	case EFX_FILTER_MATCH_LOC_MAC:
		if (gen_spec->efs_flags & EFX_FILTER_FLAG_TX) {
			sf_spec->sfs_type = (is_full ?
			    EFX_SIENA_FILTER_TX_MAC_FULL :
			    EFX_SIENA_FILTER_TX_MAC_WILD);
		} else {
			sf_spec->sfs_type = (is_full ?
			    EFX_SIENA_FILTER_RX_MAC_FULL :
			    EFX_SIENA_FILTER_RX_MAC_WILD);
		}
		sf_spec->sfs_dword[0] = is_full ? gen_spec->efs_outer_vid : 0;
		sf_spec->sfs_dword[1] =
		    gen_spec->efs_loc_mac[2] << 24 |
		    gen_spec->efs_loc_mac[3] << 16 |
		    gen_spec->efs_loc_mac[4] <<  8 |
		    gen_spec->efs_loc_mac[5];
		sf_spec->sfs_dword[2] =
		    gen_spec->efs_loc_mac[0] << 8 |
		    gen_spec->efs_loc_mac[1];
		break;

	default:
		EFSYS_ASSERT(B_FALSE);
		rc = ENOTSUP;
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

/*
 * The filter hash function is LFSR polynomial x^16 + x^3 + 1 of a 32-bit
 * key derived from the n-tuple.
 */
static			uint16_t
siena_filter_tbl_hash(
	__in		uint32_t key)
{
	uint16_t tmp;

	/* First 16 rounds */
	tmp = 0x1fff ^ (uint16_t)(key >> 16);
	tmp = tmp ^ tmp >> 3 ^ tmp >> 6;
	tmp = tmp ^ tmp >> 9;

	/* Last 16 rounds */
	tmp = tmp ^ tmp << 13 ^ (uint16_t)(key & 0xffff);
	tmp = tmp ^ tmp >> 3 ^ tmp >> 6;
	tmp = tmp ^ tmp >> 9;

	return (tmp);
}

/*
 * To allow for hash collisions, filter search continues at these
 * increments from the first possible entry selected by the hash.
 */
static			uint16_t
siena_filter_tbl_increment(
	__in		uint32_t key)
{
	return ((uint16_t)(key * 2 - 1));
}

static	__checkReturn	boolean_t
siena_filter_test_used(
	__in		siena_filter_tbl_t *sftp,
	__in		unsigned int index)
{
	EFSYS_ASSERT3P(sftp->sft_bitmap, !=, NULL);
	return ((sftp->sft_bitmap[index / 32] & (1 << (index % 32))) != 0);
}

static			void
siena_filter_set_used(
	__in		siena_filter_tbl_t *sftp,
	__in		unsigned int index)
{
	EFSYS_ASSERT3P(sftp->sft_bitmap, !=, NULL);
	sftp->sft_bitmap[index / 32] |= (1 << (index % 32));
	++sftp->sft_used;
}

static			void
siena_filter_clear_used(
	__in		siena_filter_tbl_t *sftp,
	__in		unsigned int index)
{
	EFSYS_ASSERT3P(sftp->sft_bitmap, !=, NULL);
	sftp->sft_bitmap[index / 32] &= ~(1 << (index % 32));

	--sftp->sft_used;
	EFSYS_ASSERT3U(sftp->sft_used, >=, 0);
}


static			siena_filter_tbl_id_t
siena_filter_tbl_id(
	__in		siena_filter_type_t type)
{
	siena_filter_tbl_id_t tbl_id;

	switch (type) {
	case EFX_SIENA_FILTER_RX_TCP_FULL:
	case EFX_SIENA_FILTER_RX_TCP_WILD:
	case EFX_SIENA_FILTER_RX_UDP_FULL:
	case EFX_SIENA_FILTER_RX_UDP_WILD:
		tbl_id = EFX_SIENA_FILTER_TBL_RX_IP;
		break;

	case EFX_SIENA_FILTER_RX_MAC_FULL:
	case EFX_SIENA_FILTER_RX_MAC_WILD:
		tbl_id = EFX_SIENA_FILTER_TBL_RX_MAC;
		break;

	case EFX_SIENA_FILTER_TX_TCP_FULL:
	case EFX_SIENA_FILTER_TX_TCP_WILD:
	case EFX_SIENA_FILTER_TX_UDP_FULL:
	case EFX_SIENA_FILTER_TX_UDP_WILD:
		tbl_id = EFX_SIENA_FILTER_TBL_TX_IP;
		break;

	case EFX_SIENA_FILTER_TX_MAC_FULL:
	case EFX_SIENA_FILTER_TX_MAC_WILD:
		tbl_id = EFX_SIENA_FILTER_TBL_TX_MAC;
		break;

	default:
		EFSYS_ASSERT(B_FALSE);
		tbl_id = EFX_SIENA_FILTER_NTBLS;
		break;
	}
	return (tbl_id);
}

static			void
siena_filter_reset_search_depth(
	__inout		siena_filter_t *sfp,
	__in		siena_filter_tbl_id_t tbl_id)
{
	switch (tbl_id) {
	case EFX_SIENA_FILTER_TBL_RX_IP:
		sfp->sf_depth[EFX_SIENA_FILTER_RX_TCP_FULL] = 0;
		sfp->sf_depth[EFX_SIENA_FILTER_RX_TCP_WILD] = 0;
		sfp->sf_depth[EFX_SIENA_FILTER_RX_UDP_FULL] = 0;
		sfp->sf_depth[EFX_SIENA_FILTER_RX_UDP_WILD] = 0;
		break;

	case EFX_SIENA_FILTER_TBL_RX_MAC:
		sfp->sf_depth[EFX_SIENA_FILTER_RX_MAC_FULL] = 0;
		sfp->sf_depth[EFX_SIENA_FILTER_RX_MAC_WILD] = 0;
		break;

	case EFX_SIENA_FILTER_TBL_TX_IP:
		sfp->sf_depth[EFX_SIENA_FILTER_TX_TCP_FULL] = 0;
		sfp->sf_depth[EFX_SIENA_FILTER_TX_TCP_WILD] = 0;
		sfp->sf_depth[EFX_SIENA_FILTER_TX_UDP_FULL] = 0;
		sfp->sf_depth[EFX_SIENA_FILTER_TX_UDP_WILD] = 0;
		break;

	case EFX_SIENA_FILTER_TBL_TX_MAC:
		sfp->sf_depth[EFX_SIENA_FILTER_TX_MAC_FULL] = 0;
		sfp->sf_depth[EFX_SIENA_FILTER_TX_MAC_WILD] = 0;
		break;

	default:
		EFSYS_ASSERT(B_FALSE);
		break;
	}
}

static			void
siena_filter_push_rx_limits(
	__in		efx_nic_t *enp)
{
	siena_filter_t *sfp = enp->en_filter.ef_siena_filter;
	efx_oword_t oword;

	EFX_BAR_READO(enp, FR_AZ_RX_FILTER_CTL_REG, &oword);

	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TCP_FULL_SRCH_LIMIT,
	    sfp->sf_depth[EFX_SIENA_FILTER_RX_TCP_FULL] +
	    FILTER_CTL_SRCH_FUDGE_FULL);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TCP_WILD_SRCH_LIMIT,
	    sfp->sf_depth[EFX_SIENA_FILTER_RX_TCP_WILD] +
	    FILTER_CTL_SRCH_FUDGE_WILD);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_UDP_FULL_SRCH_LIMIT,
	    sfp->sf_depth[EFX_SIENA_FILTER_RX_UDP_FULL] +
	    FILTER_CTL_SRCH_FUDGE_FULL);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_UDP_WILD_SRCH_LIMIT,
	    sfp->sf_depth[EFX_SIENA_FILTER_RX_UDP_WILD] +
	    FILTER_CTL_SRCH_FUDGE_WILD);

	if (sfp->sf_tbl[EFX_SIENA_FILTER_TBL_RX_MAC].sft_size) {
		EFX_SET_OWORD_FIELD(oword,
		    FRF_CZ_ETHERNET_FULL_SEARCH_LIMIT,
		    sfp->sf_depth[EFX_SIENA_FILTER_RX_MAC_FULL] +
		    FILTER_CTL_SRCH_FUDGE_FULL);
		EFX_SET_OWORD_FIELD(oword,
		    FRF_CZ_ETHERNET_WILDCARD_SEARCH_LIMIT,
		    sfp->sf_depth[EFX_SIENA_FILTER_RX_MAC_WILD] +
		    FILTER_CTL_SRCH_FUDGE_WILD);
	}

	EFX_BAR_WRITEO(enp, FR_AZ_RX_FILTER_CTL_REG, &oword);
}

static			void
siena_filter_push_tx_limits(
	__in		efx_nic_t *enp)
{
	siena_filter_t *sfp = enp->en_filter.ef_siena_filter;
	efx_oword_t oword;

	EFX_BAR_READO(enp, FR_AZ_TX_CFG_REG, &oword);

	if (sfp->sf_tbl[EFX_SIENA_FILTER_TBL_TX_IP].sft_size != 0) {
		EFX_SET_OWORD_FIELD(oword,
		    FRF_CZ_TX_TCPIP_FILTER_FULL_SEARCH_RANGE,
		    sfp->sf_depth[EFX_SIENA_FILTER_TX_TCP_FULL] +
		    FILTER_CTL_SRCH_FUDGE_FULL);
		EFX_SET_OWORD_FIELD(oword,
		    FRF_CZ_TX_TCPIP_FILTER_WILD_SEARCH_RANGE,
		    sfp->sf_depth[EFX_SIENA_FILTER_TX_TCP_WILD] +
		    FILTER_CTL_SRCH_FUDGE_WILD);
		EFX_SET_OWORD_FIELD(oword,
		    FRF_CZ_TX_UDPIP_FILTER_FULL_SEARCH_RANGE,
		    sfp->sf_depth[EFX_SIENA_FILTER_TX_UDP_FULL] +
		    FILTER_CTL_SRCH_FUDGE_FULL);
		EFX_SET_OWORD_FIELD(oword,
		    FRF_CZ_TX_UDPIP_FILTER_WILD_SEARCH_RANGE,
		    sfp->sf_depth[EFX_SIENA_FILTER_TX_UDP_WILD] +
		    FILTER_CTL_SRCH_FUDGE_WILD);
	}

	if (sfp->sf_tbl[EFX_SIENA_FILTER_TBL_TX_MAC].sft_size != 0) {
		EFX_SET_OWORD_FIELD(
			oword, FRF_CZ_TX_ETH_FILTER_FULL_SEARCH_RANGE,
			sfp->sf_depth[EFX_SIENA_FILTER_TX_MAC_FULL] +
			FILTER_CTL_SRCH_FUDGE_FULL);
		EFX_SET_OWORD_FIELD(
			oword, FRF_CZ_TX_ETH_FILTER_WILD_SEARCH_RANGE,
			sfp->sf_depth[EFX_SIENA_FILTER_TX_MAC_WILD] +
			FILTER_CTL_SRCH_FUDGE_WILD);
	}

	EFX_BAR_WRITEO(enp, FR_AZ_TX_CFG_REG, &oword);
}

/* Build a filter entry and return its n-tuple key. */
static	__checkReturn	uint32_t
siena_filter_build(
	__out		efx_oword_t *filter,
	__in		siena_filter_spec_t *spec)
{
	uint32_t dword3;
	uint32_t key;
	uint8_t  type  = spec->sfs_type;
	uint32_t flags = spec->sfs_flags;

	switch (siena_filter_tbl_id(type)) {
	case EFX_SIENA_FILTER_TBL_RX_IP: {
		boolean_t is_udp = (type == EFX_SIENA_FILTER_RX_UDP_FULL ||
		    type == EFX_SIENA_FILTER_RX_UDP_WILD);
		EFX_POPULATE_OWORD_7(*filter,
		    FRF_BZ_RSS_EN,
		    (flags & EFX_FILTER_FLAG_RX_RSS) ? 1 : 0,
		    FRF_BZ_SCATTER_EN,
		    (flags & EFX_FILTER_FLAG_RX_SCATTER) ? 1 : 0,
		    FRF_AZ_TCP_UDP, is_udp,
		    FRF_AZ_RXQ_ID, spec->sfs_dmaq_id,
		    EFX_DWORD_2, spec->sfs_dword[2],
		    EFX_DWORD_1, spec->sfs_dword[1],
		    EFX_DWORD_0, spec->sfs_dword[0]);
		dword3 = is_udp;
		break;
	}

	case EFX_SIENA_FILTER_TBL_RX_MAC: {
		boolean_t is_wild = (type == EFX_SIENA_FILTER_RX_MAC_WILD);
		EFX_POPULATE_OWORD_7(*filter,
		    FRF_CZ_RMFT_RSS_EN,
		    (flags & EFX_FILTER_FLAG_RX_RSS) ? 1 : 0,
		    FRF_CZ_RMFT_SCATTER_EN,
		    (flags & EFX_FILTER_FLAG_RX_SCATTER) ? 1 : 0,
		    FRF_CZ_RMFT_RXQ_ID, spec->sfs_dmaq_id,
		    FRF_CZ_RMFT_WILDCARD_MATCH, is_wild,
		    FRF_CZ_RMFT_DEST_MAC_DW1, spec->sfs_dword[2],
		    FRF_CZ_RMFT_DEST_MAC_DW0, spec->sfs_dword[1],
		    FRF_CZ_RMFT_VLAN_ID, spec->sfs_dword[0]);
		dword3 = is_wild;
		break;
	}

	case EFX_SIENA_FILTER_TBL_TX_IP: {
		boolean_t is_udp = (type == EFX_SIENA_FILTER_TX_UDP_FULL ||
		    type == EFX_SIENA_FILTER_TX_UDP_WILD);
		EFX_POPULATE_OWORD_5(*filter,
		    FRF_CZ_TIFT_TCP_UDP, is_udp,
		    FRF_CZ_TIFT_TXQ_ID, spec->sfs_dmaq_id,
		    EFX_DWORD_2, spec->sfs_dword[2],
		    EFX_DWORD_1, spec->sfs_dword[1],
		    EFX_DWORD_0, spec->sfs_dword[0]);
		dword3 = is_udp | spec->sfs_dmaq_id << 1;
		break;
	}

	case EFX_SIENA_FILTER_TBL_TX_MAC: {
		boolean_t is_wild = (type == EFX_SIENA_FILTER_TX_MAC_WILD);
		EFX_POPULATE_OWORD_5(*filter,
		    FRF_CZ_TMFT_TXQ_ID, spec->sfs_dmaq_id,
		    FRF_CZ_TMFT_WILDCARD_MATCH, is_wild,
		    FRF_CZ_TMFT_SRC_MAC_DW1, spec->sfs_dword[2],
		    FRF_CZ_TMFT_SRC_MAC_DW0, spec->sfs_dword[1],
		    FRF_CZ_TMFT_VLAN_ID, spec->sfs_dword[0]);
		dword3 = is_wild | spec->sfs_dmaq_id << 1;
		break;
	}

	default:
		EFSYS_ASSERT(B_FALSE);
		EFX_ZERO_OWORD(*filter);
		return (0);
	}

	key =
	    spec->sfs_dword[0] ^
	    spec->sfs_dword[1] ^
	    spec->sfs_dword[2] ^
	    dword3;

	return (key);
}

static	__checkReturn		efx_rc_t
siena_filter_push_entry(
	__inout			efx_nic_t *enp,
	__in			siena_filter_type_t type,
	__in			int index,
	__in			efx_oword_t *eop)
{
	efx_rc_t rc;

	switch (type) {
	case EFX_SIENA_FILTER_RX_TCP_FULL:
	case EFX_SIENA_FILTER_RX_TCP_WILD:
	case EFX_SIENA_FILTER_RX_UDP_FULL:
	case EFX_SIENA_FILTER_RX_UDP_WILD:
		EFX_BAR_TBL_WRITEO(enp, FR_AZ_RX_FILTER_TBL0, index,
		    eop, B_TRUE);
		break;

	case EFX_SIENA_FILTER_RX_MAC_FULL:
	case EFX_SIENA_FILTER_RX_MAC_WILD:
		EFX_BAR_TBL_WRITEO(enp, FR_CZ_RX_MAC_FILTER_TBL0, index,
		    eop, B_TRUE);
		break;

	case EFX_SIENA_FILTER_TX_TCP_FULL:
	case EFX_SIENA_FILTER_TX_TCP_WILD:
	case EFX_SIENA_FILTER_TX_UDP_FULL:
	case EFX_SIENA_FILTER_TX_UDP_WILD:
		EFX_BAR_TBL_WRITEO(enp, FR_CZ_TX_FILTER_TBL0, index,
		    eop, B_TRUE);
		break;

	case EFX_SIENA_FILTER_TX_MAC_FULL:
	case EFX_SIENA_FILTER_TX_MAC_WILD:
		EFX_BAR_TBL_WRITEO(enp, FR_CZ_TX_MAC_FILTER_TBL0, index,
		    eop, B_TRUE);
		break;

	default:
		EFSYS_ASSERT(B_FALSE);
		rc = ENOTSUP;
		goto fail1;
	}
	return (0);

fail1:
	return (rc);
}


static	__checkReturn	boolean_t
siena_filter_equal(
	__in		const siena_filter_spec_t *left,
	__in		const siena_filter_spec_t *right)
{
	siena_filter_tbl_id_t tbl_id;

	tbl_id = siena_filter_tbl_id(left->sfs_type);


	if (left->sfs_type != right->sfs_type)
		return (B_FALSE);

	if (memcmp(left->sfs_dword, right->sfs_dword,
		sizeof (left->sfs_dword)))
		return (B_FALSE);

	if ((tbl_id == EFX_SIENA_FILTER_TBL_TX_IP ||
		tbl_id == EFX_SIENA_FILTER_TBL_TX_MAC) &&
	    left->sfs_dmaq_id != right->sfs_dmaq_id)
		return (B_FALSE);

	return (B_TRUE);
}

static	__checkReturn	efx_rc_t
siena_filter_search(
	__in		siena_filter_tbl_t *sftp,
	__in		siena_filter_spec_t *spec,
	__in		uint32_t key,
	__in		boolean_t for_insert,
	__out		int *filter_index,
	__out		unsigned int *depth_required)
{
	unsigned int hash, incr, filter_idx, depth;

	hash = siena_filter_tbl_hash(key);
	incr = siena_filter_tbl_increment(key);

	filter_idx = hash & (sftp->sft_size - 1);
	depth = 1;

	for (;;) {
		/*
		 * Return success if entry is used and matches this spec
		 * or entry is unused and we are trying to insert.
		 */
		if (siena_filter_test_used(sftp, filter_idx) ?
		    siena_filter_equal(spec,
		    &sftp->sft_spec[filter_idx]) :
		    for_insert) {
			*filter_index = filter_idx;
			*depth_required = depth;
			return (0);
		}

		/* Return failure if we reached the maximum search depth */
		if (depth == FILTER_CTL_SRCH_MAX)
			return (for_insert ? EBUSY : ENOENT);

		filter_idx = (filter_idx + incr) & (sftp->sft_size - 1);
		++depth;
	}
}

static			void
siena_filter_clear_entry(
	__in		efx_nic_t *enp,
	__in		siena_filter_tbl_t *sftp,
	__in		int index)
{
	efx_oword_t filter;

	if (siena_filter_test_used(sftp, index)) {
		siena_filter_clear_used(sftp, index);

		EFX_ZERO_OWORD(filter);
		siena_filter_push_entry(enp,
		    sftp->sft_spec[index].sfs_type,
		    index, &filter);

		memset(&sftp->sft_spec[index],
		    0, sizeof (sftp->sft_spec[0]));
	}
}

			void
siena_filter_tbl_clear(
	__in		efx_nic_t *enp,
	__in		siena_filter_tbl_id_t tbl_id)
{
	siena_filter_t *sfp = enp->en_filter.ef_siena_filter;
	siena_filter_tbl_t *sftp = &sfp->sf_tbl[tbl_id];
	int index;
	efsys_lock_state_t state;

	EFSYS_LOCK(enp->en_eslp, state);

	for (index = 0; index < sftp->sft_size; ++index) {
		siena_filter_clear_entry(enp, sftp, index);
	}

	if (sftp->sft_used == 0)
		siena_filter_reset_search_depth(sfp, tbl_id);

	EFSYS_UNLOCK(enp->en_eslp, state);
}

static	__checkReturn	efx_rc_t
siena_filter_init(
	__in		efx_nic_t *enp)
{
	siena_filter_t *sfp;
	siena_filter_tbl_t *sftp;
	int tbl_id;
	efx_rc_t rc;

	EFSYS_KMEM_ALLOC(enp->en_esip, sizeof (siena_filter_t), sfp);

	if (!sfp) {
		rc = ENOMEM;
		goto fail1;
	}

	enp->en_filter.ef_siena_filter = sfp;

	switch (enp->en_family) {
	case EFX_FAMILY_SIENA:
		sftp = &sfp->sf_tbl[EFX_SIENA_FILTER_TBL_RX_IP];
		sftp->sft_size = FR_AZ_RX_FILTER_TBL0_ROWS;

		sftp = &sfp->sf_tbl[EFX_SIENA_FILTER_TBL_RX_MAC];
		sftp->sft_size = FR_CZ_RX_MAC_FILTER_TBL0_ROWS;

		sftp = &sfp->sf_tbl[EFX_SIENA_FILTER_TBL_TX_IP];
		sftp->sft_size = FR_CZ_TX_FILTER_TBL0_ROWS;

		sftp = &sfp->sf_tbl[EFX_SIENA_FILTER_TBL_TX_MAC];
		sftp->sft_size = FR_CZ_TX_MAC_FILTER_TBL0_ROWS;
		break;

	default:
		rc = ENOTSUP;
		goto fail2;
	}

	for (tbl_id = 0; tbl_id < EFX_SIENA_FILTER_NTBLS; tbl_id++) {
		unsigned int bitmap_size;

		sftp = &sfp->sf_tbl[tbl_id];
		if (sftp->sft_size == 0)
			continue;

		EFX_STATIC_ASSERT(sizeof (sftp->sft_bitmap[0]) ==
		    sizeof (uint32_t));
		bitmap_size =
		    (sftp->sft_size + (sizeof (uint32_t) * 8) - 1) / 8;

		EFSYS_KMEM_ALLOC(enp->en_esip, bitmap_size, sftp->sft_bitmap);
		if (!sftp->sft_bitmap) {
			rc = ENOMEM;
			goto fail3;
		}

		EFSYS_KMEM_ALLOC(enp->en_esip,
		    sftp->sft_size * sizeof (*sftp->sft_spec),
		    sftp->sft_spec);
		if (!sftp->sft_spec) {
			rc = ENOMEM;
			goto fail4;
		}
		memset(sftp->sft_spec, 0,
		    sftp->sft_size * sizeof (*sftp->sft_spec));
	}

	return (0);

fail4:
	EFSYS_PROBE(fail4);

fail3:
	EFSYS_PROBE(fail3);

fail2:
	EFSYS_PROBE(fail2);
	siena_filter_fini(enp);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}

static			void
siena_filter_fini(
	__in		efx_nic_t *enp)
{
	siena_filter_t *sfp = enp->en_filter.ef_siena_filter;
	siena_filter_tbl_id_t tbl_id;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);

	if (sfp == NULL)
		return;

	for (tbl_id = 0; tbl_id < EFX_SIENA_FILTER_NTBLS; tbl_id++) {
		siena_filter_tbl_t *sftp = &sfp->sf_tbl[tbl_id];
		unsigned int bitmap_size;

		EFX_STATIC_ASSERT(sizeof (sftp->sft_bitmap[0]) ==
		    sizeof (uint32_t));
		bitmap_size =
		    (sftp->sft_size + (sizeof (uint32_t) * 8) - 1) / 8;

		if (sftp->sft_bitmap != NULL) {
			EFSYS_KMEM_FREE(enp->en_esip, bitmap_size,
			    sftp->sft_bitmap);
			sftp->sft_bitmap = NULL;
		}

		if (sftp->sft_spec != NULL) {
			EFSYS_KMEM_FREE(enp->en_esip, sftp->sft_size *
			    sizeof (*sftp->sft_spec), sftp->sft_spec);
			sftp->sft_spec = NULL;
		}
	}

	EFSYS_KMEM_FREE(enp->en_esip, sizeof (siena_filter_t),
	    enp->en_filter.ef_siena_filter);
}

/* Restore filter state after a reset */
static	__checkReturn	efx_rc_t
siena_filter_restore(
	__in		efx_nic_t *enp)
{
	siena_filter_t *sfp = enp->en_filter.ef_siena_filter;
	siena_filter_tbl_id_t tbl_id;
	siena_filter_tbl_t *sftp;
	siena_filter_spec_t *spec;
	efx_oword_t filter;
	int filter_idx;
	efsys_lock_state_t state;
	uint32_t key;
	efx_rc_t rc;

	EFSYS_LOCK(enp->en_eslp, state);

	for (tbl_id = 0; tbl_id < EFX_SIENA_FILTER_NTBLS; tbl_id++) {
		sftp = &sfp->sf_tbl[tbl_id];
		for (filter_idx = 0;
			filter_idx < sftp->sft_size;
			filter_idx++) {
			if (!siena_filter_test_used(sftp, filter_idx))
				continue;

			spec = &sftp->sft_spec[filter_idx];
			if ((key = siena_filter_build(&filter, spec)) == 0) {
				rc = EINVAL;
				goto fail1;
			}
			if ((rc = siena_filter_push_entry(enp,
				    spec->sfs_type, filter_idx, &filter)) != 0)
				goto fail2;
		}
	}

	siena_filter_push_rx_limits(enp);
	siena_filter_push_tx_limits(enp);

	EFSYS_UNLOCK(enp->en_eslp, state);

	return (0);

fail2:
	EFSYS_PROBE(fail2);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	EFSYS_UNLOCK(enp->en_eslp, state);

	return (rc);
}

static	 __checkReturn	efx_rc_t
siena_filter_add(
	__in		efx_nic_t *enp,
	__inout		efx_filter_spec_t *spec,
	__in		boolean_t may_replace)
{
	efx_rc_t rc;
	siena_filter_spec_t sf_spec;
	siena_filter_t *sfp = enp->en_filter.ef_siena_filter;
	siena_filter_tbl_id_t tbl_id;
	siena_filter_tbl_t *sftp;
	siena_filter_spec_t *saved_sf_spec;
	efx_oword_t filter;
	int filter_idx;
	unsigned int depth;
	efsys_lock_state_t state;
	uint32_t key;


	EFSYS_ASSERT3P(spec, !=, NULL);

	if ((rc = siena_filter_spec_from_gen_spec(&sf_spec, spec)) != 0)
		goto fail1;

	tbl_id = siena_filter_tbl_id(sf_spec.sfs_type);
	sftp = &sfp->sf_tbl[tbl_id];

	if (sftp->sft_size == 0) {
		rc = EINVAL;
		goto fail2;
	}

	key = siena_filter_build(&filter, &sf_spec);

	EFSYS_LOCK(enp->en_eslp, state);

	rc = siena_filter_search(sftp, &sf_spec, key, B_TRUE,
	    &filter_idx, &depth);
	if (rc != 0)
		goto fail3;

	EFSYS_ASSERT3U(filter_idx, <, sftp->sft_size);
	saved_sf_spec = &sftp->sft_spec[filter_idx];

	if (siena_filter_test_used(sftp, filter_idx)) {
		if (may_replace == B_FALSE) {
			rc = EEXIST;
			goto fail4;
		}
	}
	siena_filter_set_used(sftp, filter_idx);
	*saved_sf_spec = sf_spec;

	if (sfp->sf_depth[sf_spec.sfs_type] < depth) {
		sfp->sf_depth[sf_spec.sfs_type] = depth;
		if (tbl_id == EFX_SIENA_FILTER_TBL_TX_IP ||
		    tbl_id == EFX_SIENA_FILTER_TBL_TX_MAC)
			siena_filter_push_tx_limits(enp);
		else
			siena_filter_push_rx_limits(enp);
	}

	siena_filter_push_entry(enp, sf_spec.sfs_type,
	    filter_idx, &filter);

	EFSYS_UNLOCK(enp->en_eslp, state);
	return (0);

fail4:
	EFSYS_PROBE(fail4);

fail3:
	EFSYS_UNLOCK(enp->en_eslp, state);
	EFSYS_PROBE(fail3);

fail2:
	EFSYS_PROBE(fail2);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}

static	 __checkReturn	efx_rc_t
siena_filter_delete(
	__in		efx_nic_t *enp,
	__inout		efx_filter_spec_t *spec)
{
	efx_rc_t rc;
	siena_filter_spec_t sf_spec;
	siena_filter_t *sfp = enp->en_filter.ef_siena_filter;
	siena_filter_tbl_id_t tbl_id;
	siena_filter_tbl_t *sftp;
	efx_oword_t filter;
	int filter_idx;
	unsigned int depth;
	efsys_lock_state_t state;
	uint32_t key;

	EFSYS_ASSERT3P(spec, !=, NULL);

	if ((rc = siena_filter_spec_from_gen_spec(&sf_spec, spec)) != 0)
		goto fail1;

	tbl_id = siena_filter_tbl_id(sf_spec.sfs_type);
	sftp = &sfp->sf_tbl[tbl_id];

	key = siena_filter_build(&filter, &sf_spec);

	EFSYS_LOCK(enp->en_eslp, state);

	rc = siena_filter_search(sftp, &sf_spec, key, B_FALSE,
	    &filter_idx, &depth);
	if (rc != 0)
		goto fail2;

	siena_filter_clear_entry(enp, sftp, filter_idx);
	if (sftp->sft_used == 0)
		siena_filter_reset_search_depth(sfp, tbl_id);

	EFSYS_UNLOCK(enp->en_eslp, state);
	return (0);

fail2:
	EFSYS_UNLOCK(enp->en_eslp, state);
	EFSYS_PROBE(fail2);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}

#define	SIENA_MAX_SUPPORTED_MATCHES 4

static	__checkReturn	efx_rc_t
siena_filter_supported_filters(
	__in				efx_nic_t *enp,
	__out_ecount(buffer_length)	uint32_t *buffer,
	__in				size_t buffer_length,
	__out				size_t *list_lengthp)
{
	uint32_t index = 0;
	uint32_t rx_matches[SIENA_MAX_SUPPORTED_MATCHES];
	size_t list_length;
	efx_rc_t rc;

	rx_matches[index++] =
	    EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_IP_PROTO |
	    EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_LOC_PORT |
	    EFX_FILTER_MATCH_REM_HOST | EFX_FILTER_MATCH_REM_PORT;

	rx_matches[index++] =
	    EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_IP_PROTO |
	    EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_LOC_PORT;

	if (enp->en_features & EFX_FEATURE_MAC_HEADER_FILTERS) {
		rx_matches[index++] =
		    EFX_FILTER_MATCH_OUTER_VID | EFX_FILTER_MATCH_LOC_MAC;

		rx_matches[index++] = EFX_FILTER_MATCH_LOC_MAC;
	}

	EFSYS_ASSERT3U(index, <=, SIENA_MAX_SUPPORTED_MATCHES);
	list_length = index;

	*list_lengthp = list_length;

	if (buffer_length < list_length) {
		rc = ENOSPC;
		goto fail1;
	}

	memcpy(buffer, rx_matches, list_length * sizeof (rx_matches[0]));

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#undef MAX_SUPPORTED

#endif /* EFSYS_OPT_SIENA */

#endif /* EFSYS_OPT_FILTER */
