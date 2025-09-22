/*-
 * Copyright (c) 2016 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_PV_NDIS_H_
#define _DEV_PV_NDIS_H_

/*
 * NDIS protocol version numbers
 */
#define	NDIS_VERSION_5_0			0x00050000
#define	NDIS_VERSION_5_1			0x00050001
#define	NDIS_VERSION_6_0			0x00060000
#define	NDIS_VERSION_6_1			0x00060001
#define	NDIS_VERSION_6_30			0x0006001e

#define	NDIS_MEDIA_STATE_CONNECTED	0
#define	NDIS_MEDIA_STATE_DISCONNECTED	1

#define	NDIS_OBJTYPE_DEFAULT		0x80
#define	NDIS_OBJTYPE_RSS_CAPS		0x88
#define	NDIS_OBJTYPE_RSS_PARAMS		0x89

struct ndis_object_hdr {
	uint8_t			ndis_type;	/* NDIS_OBJTYPE_ */
	uint8_t			ndis_rev;	/* type specific */
	uint16_t		ndis_size;	/* incl. this hdr */
};

/*
 * OID_TCP_OFFLOAD_PARAMETERS
 * ndis_type: NDIS_OBJTYPE_DEFAULT
 */
struct ndis_offload_params {
	struct ndis_object_hdr	ndis_hdr;
	uint8_t			ndis_ip4csum;	/* NDIS_OFFLOAD_PARAM_ */
	uint8_t			ndis_tcp4csum;	/* NDIS_OFFLOAD_PARAM_ */
	uint8_t			ndis_udp4csum;	/* NDIS_OFFLOAD_PARAM_ */
	uint8_t			ndis_tcp6csum;	/* NDIS_OFFLOAD_PARAM_ */
	uint8_t			ndis_udp6csum;	/* NDIS_OFFLOAD_PARAM_ */
	uint8_t			ndis_lsov1;	/* NDIS_OFFLOAD_PARAM_ */
	uint8_t			ndis_ipsecv1;	/* NDIS_OFFLOAD_IPSECV1_ */
	uint8_t			ndis_lsov2_ip4;	/* NDIS_OFFLOAD_LSOV2_ */
	uint8_t			ndis_lsov2_ip6;	/* NDIS_OFFLOAD_LSOV2_ */
	uint8_t			ndis_tcp4conn;	/* 0 */
	uint8_t			ndis_tcp6conn;	/* 0 */
	uint32_t		ndis_flags;	/* 0 */
	/* NDIS >= 6.1 */
	uint8_t			ndis_ipsecv2;	/* NDIS_OFFLOAD_IPSECV2_ */
	uint8_t			ndis_ipsecv2_ip4;/* NDIS_OFFLOAD_IPSECV2_ */
	/* NDIS >= 6.30 */
	uint8_t			ndis_rsc_ip4;	/* NDIS_OFFLOAD_RSC_ */
	uint8_t			ndis_rsc_ip6;	/* NDIS_OFFLOAD_RSC_ */
	uint8_t			ndis_encap;	/* NDIS_OFFLOAD_SET_ */
	uint8_t			ndis_encap_types;/* NDIS_ENCAP_TYPE_ */
};

#define	NDIS_OFFLOAD_PARAMS_SIZE	sizeof(struct ndis_offload_params)
#define	NDIS_OFFLOAD_PARAMS_SIZE_6_1	\
	offsetof(struct ndis_offload_params, ndis_rsc_ip4)

#define	NDIS_OFFLOAD_PARAMS_REV_2	2	/* NDIS 6.1 */
#define	NDIS_OFFLOAD_PARAMS_REV_3	3	/* NDIS 6.30 */

#define	NDIS_OFFLOAD_PARAM_NOCHG	0	/* common */
#define	NDIS_OFFLOAD_PARAM_OFF		1
#define	NDIS_OFFLOAD_PARAM_TX		2
#define	NDIS_OFFLOAD_PARAM_RX		3
#define	NDIS_OFFLOAD_PARAM_TXRX		4

/* NDIS_OFFLOAD_PARAM_NOCHG */
#define	NDIS_OFFLOAD_LSOV1_OFF		1
#define	NDIS_OFFLOAD_LSOV1_ON		2

/* NDIS_OFFLOAD_PARAM_NOCHG */
#define	NDIS_OFFLOAD_IPSECV1_OFF	1
#define	NDIS_OFFLOAD_IPSECV1_AH		2
#define	NDIS_OFFLOAD_IPSECV1_ESP	3
#define	NDIS_OFFLOAD_IPSECV1_AH_ESP	4

/* NDIS_OFFLOAD_PARAM_NOCHG */
#define	NDIS_OFFLOAD_LSOV2_OFF		1
#define	NDIS_OFFLOAD_LSOV2_ON		2

/* NDIS_OFFLOAD_PARAM_NOCHG */
#define	NDIS_OFFLOAD_IPSECV2_OFF	1
#define	NDIS_OFFLOAD_IPSECV2_AH		2
#define	NDIS_OFFLOAD_IPSECV2_ESP	3
#define	NDIS_OFFLOAD_IPSECV2_AH_ESP	4

/* NDIS_OFFLOAD_PARAM_NOCHG */
#define	NDIS_OFFLOAD_RSC_OFF		1
#define	NDIS_OFFLOAD_RSC_ON		2

/*
 * Per-packet-info
 */

/* VLAN */
#define	NDIS_VLAN_INFO_SIZE		sizeof(uint32_t)
#define	NDIS_VLAN_INFO_PRI_MASK		0x0007
#define	NDIS_VLAN_INFO_ID_MASK		0xfff0
#define	NDIS_VLAN_INFO(id, pri)			\
	(((pri) & NDIS_VLAN_INFO_PRI_MASK) | (((id) & 0xfff) << 4))
#define	NDIS_VLAN_INFO_ID(inf)		(((inf) & NDIS_VLAN_INFO_ID_MASK) >> 4)
#define	NDIS_VLAN_INFO_PRI(inf)		( (inf) & NDIS_VLAN_INFO_PRI_MASK)

/* Reception checksum */
#define	NDIS_RXCSUM_INFO_SIZE		sizeof(uint32_t)
#define	NDIS_RXCSUM_INFO_TCPCS_FAILED	0x0001
#define	NDIS_RXCSUM_INFO_UDPCS_FAILED	0x0002
#define	NDIS_RXCSUM_INFO_IPCS_FAILED	0x0004
#define	NDIS_RXCSUM_INFO_TCPCS_OK	0x0008
#define	NDIS_RXCSUM_INFO_UDPCS_OK	0x0010
#define	NDIS_RXCSUM_INFO_IPCS_OK	0x0020
#define	NDIS_RXCSUM_INFO_LOOPBACK	0x0040
#define	NDIS_RXCSUM_INFO_TCPCS_INVAL	0x0080
#define	NDIS_RXCSUM_INFO_IPCS_INVAL	0x0100

/* Transmission checksum */
#define	NDIS_TXCSUM_INFO_SIZE		sizeof(uint32_t)
#define	NDIS_TXCSUM_INFO_IPV4		0x00000001
#define	NDIS_TXCSUM_INFO_IPV6		0x00000002
#define	NDIS_TXCSUM_INFO_TCPCS		0x00000004
#define	NDIS_TXCSUM_INFO_UDPCS		0x00000008
#define	NDIS_TXCSUM_INFO_IPCS		0x00000010
#define	NDIS_TXCSUM_INFO_THOFF		0x03ff0000

#endif	/* _DEV_PV_NDIS_H_ */
