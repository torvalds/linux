/*-
 * Copyright (c) 2016-2017 Microsoft Corp.
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
 *
 * $FreeBSD$
 */

#ifndef _NET_NDIS_H_
#define _NET_NDIS_H_

#define	NDIS_MEDIA_STATE_CONNECTED	0
#define	NDIS_MEDIA_STATE_DISCONNECTED	1

#define	NDIS_NETCHANGE_TYPE_POSSIBLE	1
#define	NDIS_NETCHANGE_TYPE_DEFINITE	2
#define	NDIS_NETCHANGE_TYPE_FROMMEDIA	3

#define	NDIS_OFFLOAD_SET_NOCHG		0
#define	NDIS_OFFLOAD_SET_ON		1
#define	NDIS_OFFLOAD_SET_OFF		2

/* a.k.a GRE MAC */
#define	NDIS_ENCAP_TYPE_NVGRE		0x00000001

#define	NDIS_HASH_FUNCTION_MASK		0x000000FF	/* see hash function */
#define	NDIS_HASH_TYPE_MASK		0x00FFFF00	/* see hash type */

/* hash function */
#define	NDIS_HASH_FUNCTION_TOEPLITZ	0x00000001

/* hash type */
#define	NDIS_HASH_IPV4			0x00000100
#define	NDIS_HASH_TCP_IPV4		0x00000200
#define	NDIS_HASH_IPV6			0x00000400
#define	NDIS_HASH_IPV6_EX		0x00000800
#define	NDIS_HASH_TCP_IPV6		0x00001000
#define	NDIS_HASH_TCP_IPV6_EX		0x00002000
#define	NDIS_HASH_UDP_IPV4_X		0x00004000	/* XXX non-standard */

#define	NDIS_HASH_ALL			(NDIS_HASH_IPV4 |	\
					 NDIS_HASH_TCP_IPV4 |	\
					 NDIS_HASH_IPV6 |	\
					 NDIS_HASH_IPV6_EX |	\
					 NDIS_HASH_TCP_IPV6 |	\
					 NDIS_HASH_TCP_IPV6_EX |\
					 NDIS_HASH_UDP_IPV4_X)

#define	NDIS_HASH_STD			(NDIS_HASH_IPV4 |	\
					 NDIS_HASH_TCP_IPV4 |	\
					 NDIS_HASH_IPV6 |	\
					 NDIS_HASH_IPV6_EX |	\
					 NDIS_HASH_TCP_IPV6 |	\
					 NDIS_HASH_TCP_IPV6_EX)

/* Hash description for use with printf(9) %b identifier. */
#define	NDIS_HASH_BITS			\
	"\20\1TOEPLITZ\11IP4\12TCP4\13IP6\14IP6EX\15TCP6\16TCP6EX\17UDP4_X"

#define	NDIS_HASH_KEYSIZE_TOEPLITZ	40
#define	NDIS_HASH_INDCNT		128

#define	NDIS_OBJTYPE_DEFAULT		0x80
#define	NDIS_OBJTYPE_RSS_CAPS		0x88
#define	NDIS_OBJTYPE_RSS_PARAMS		0x89
#define	NDIS_OBJTYPE_OFFLOAD		0xa7

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
	__offsetof(struct ndis_offload_params, ndis_rsc_ip4)

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
 * OID_GEN_RECEIVE_SCALE_CAPABILITIES
 * ndis_type: NDIS_OBJTYPE_RSS_CAPS
 */
struct ndis_rss_caps {
	struct ndis_object_hdr		ndis_hdr;
	uint32_t			ndis_caps;	/* NDIS_RSS_CAP_ */
	uint32_t			ndis_nmsi;	/* # of MSIs */
	uint32_t			ndis_nrxr;	/* # of RX rings */
	/* NDIS >= 6.30 */
	uint16_t			ndis_nind;	/* # of indtbl ent. */
	uint16_t			ndis_pad;
};

#define	NDIS_RSS_CAPS_SIZE		\
	__offsetof(struct ndis_rss_caps, ndis_pad)
#define	NDIS_RSS_CAPS_SIZE_6_0		\
	__offsetof(struct ndis_rss_caps, ndis_nind)

#define	NDIS_RSS_CAPS_REV_1		1	/* NDIS 6.{0,1,20} */
#define	NDIS_RSS_CAPS_REV_2		2	/* NDIS 6.30 */

#define	NDIS_RSS_CAP_MSI		0x01000000
#define	NDIS_RSS_CAP_CLASSIFY_ISR	0x02000000
#define	NDIS_RSS_CAP_CLASSIFY_DPC	0x04000000
#define	NDIS_RSS_CAP_MSIX		0x08000000
#define	NDIS_RSS_CAP_IPV4		0x00000100
#define	NDIS_RSS_CAP_IPV6		0x00000200
#define	NDIS_RSS_CAP_IPV6_EX		0x00000400
#define	NDIS_RSS_CAP_HASH_TOEPLITZ	NDIS_HASH_FUNCTION_TOEPLITZ
#define	NDIS_RSS_CAP_HASHFUNC_MASK	NDIS_HASH_FUNCTION_MASK

/*
 * OID_GEN_RECEIVE_SCALE_PARAMETERS
 * ndis_type: NDIS_OBJTYPE_RSS_PARAMS
 */
struct ndis_rss_params {
	struct ndis_object_hdr		ndis_hdr;
	uint16_t			ndis_flags;	/* NDIS_RSS_FLAG_ */
	uint16_t			ndis_bcpu;	/* base cpu 0 */
	uint32_t			ndis_hash;	/* NDIS_HASH_ */
	uint16_t			ndis_indsize;	/* indirect table */
	uint32_t			ndis_indoffset;
	uint16_t			ndis_keysize;	/* hash key */
	uint32_t			ndis_keyoffset;
	/* NDIS >= 6.20 */
	uint32_t			ndis_cpumaskoffset;
	uint32_t			ndis_cpumaskcnt;
	uint32_t			ndis_cpumaskentsz;
};

#define	NDIS_RSS_PARAMS_SIZE		sizeof(struct ndis_rss_params)
#define	NDIS_RSS_PARAMS_SIZE_6_0	\
	__offsetof(struct ndis_rss_params, ndis_cpumaskoffset)

#define	NDIS_RSS_PARAMS_REV_1		1	/* NDIS 6.0 */
#define	NDIS_RSS_PARAMS_REV_2		2	/* NDIS 6.20 */

#define	NDIS_RSS_FLAG_NONE		0x0000
#define	NDIS_RSS_FLAG_BCPU_UNCHG	0x0001
#define	NDIS_RSS_FLAG_HASH_UNCHG	0x0002
#define	NDIS_RSS_FLAG_IND_UNCHG		0x0004
#define	NDIS_RSS_FLAG_KEY_UNCHG		0x0008
#define	NDIS_RSS_FLAG_DISABLE		0x0010

/* non-standard convenient struct */
struct ndis_rssprm_toeplitz {
	struct ndis_rss_params		rss_params;
	/* Toeplitz hash key */
	uint8_t				rss_key[NDIS_HASH_KEYSIZE_TOEPLITZ];
	/* Indirect table */
	uint32_t			rss_ind[NDIS_HASH_INDCNT];
};

#define	NDIS_RSSPRM_TOEPLITZ_SIZE(nind)	\
	__offsetof(struct ndis_rssprm_toeplitz, rss_ind[nind])

/*
 * OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES
 * ndis_type: NDIS_OBJTYPE_OFFLOAD
 */

#define	NDIS_OFFLOAD_ENCAP_NONE		0x0000
#define	NDIS_OFFLOAD_ENCAP_NULL		0x0001
#define	NDIS_OFFLOAD_ENCAP_8023		0x0002
#define	NDIS_OFFLOAD_ENCAP_8023PQ	0x0004
#define	NDIS_OFFLOAD_ENCAP_8023PQ_OOB	0x0008
#define	NDIS_OFFLOAD_ENCAP_RFC1483	0x0010

struct ndis_csum_offload {
	uint32_t			ndis_ip4_txenc;	/*NDIS_OFFLOAD_ENCAP_*/
	uint32_t			ndis_ip4_txcsum;
#define	NDIS_TXCSUM_CAP_IP4OPT		0x001
#define	NDIS_TXCSUM_CAP_TCP4OPT		0x004
#define	NDIS_TXCSUM_CAP_TCP4		0x010
#define	NDIS_TXCSUM_CAP_UDP4		0x040
#define	NDIS_TXCSUM_CAP_IP4		0x100
	uint32_t			ndis_ip4_rxenc;	/*NDIS_OFFLOAD_ENCAP_*/
	uint32_t			ndis_ip4_rxcsum;
#define	NDIS_RXCSUM_CAP_IP4OPT		0x001
#define	NDIS_RXCSUM_CAP_TCP4OPT		0x004
#define	NDIS_RXCSUM_CAP_TCP4		0x010
#define	NDIS_RXCSUM_CAP_UDP4		0x040
#define	NDIS_RXCSUM_CAP_IP4		0x100
	uint32_t			ndis_ip6_txenc;	/*NDIS_OFFLOAD_ENCAP_*/
	uint32_t			ndis_ip6_txcsum;
#define	NDIS_TXCSUM_CAP_IP6EXT		0x001
#define	NDIS_TXCSUM_CAP_TCP6OPT		0x004
#define	NDIS_TXCSUM_CAP_TCP6		0x010
#define	NDIS_TXCSUM_CAP_UDP6		0x040
	uint32_t			ndis_ip6_rxenc;	/*NDIS_OFFLOAD_ENCAP_*/
	uint32_t			ndis_ip6_rxcsum;
#define	NDIS_RXCSUM_CAP_IP6EXT		0x001
#define	NDIS_RXCSUM_CAP_TCP6OPT		0x004
#define	NDIS_RXCSUM_CAP_TCP6		0x010
#define	NDIS_RXCSUM_CAP_UDP6		0x040
};

struct ndis_lsov1_offload {
	uint32_t			ndis_encap;	/*NDIS_OFFLOAD_ENCAP_*/
	uint32_t			ndis_maxsize;
	uint32_t			ndis_minsegs;
	uint32_t			ndis_opts;
};

struct ndis_ipsecv1_offload {
	uint32_t			ndis_encap;	/*NDIS_OFFLOAD_ENCAP_*/
	uint32_t			ndis_ah_esp;
	uint32_t			ndis_xport_tun;
	uint32_t			ndis_ip4_opts;
	uint32_t			ndis_flags;
	uint32_t			ndis_ip4_ah;
	uint32_t			ndis_ip4_esp;
};

struct ndis_lsov2_offload {
	uint32_t			ndis_ip4_encap;	/*NDIS_OFFLOAD_ENCAP_*/
	uint32_t			ndis_ip4_maxsz;
	uint32_t			ndis_ip4_minsg;
	uint32_t			ndis_ip6_encap;	/*NDIS_OFFLOAD_ENCAP_*/
	uint32_t			ndis_ip6_maxsz;
	uint32_t			ndis_ip6_minsg;
	uint32_t			ndis_ip6_opts;
#define	NDIS_LSOV2_CAP_IP6EXT		0x001
#define	NDIS_LSOV2_CAP_TCP6OPT		0x004
};

struct ndis_ipsecv2_offload {
	uint32_t			ndis_encap;	/*NDIS_OFFLOAD_ENCAP_*/
	uint16_t			ndis_ip6;
	uint16_t			ndis_ip4opt;
	uint16_t			ndis_ip6ext;
	uint16_t			ndis_ah;
	uint16_t			ndis_esp;
	uint16_t			ndis_ah_esp;
	uint16_t			ndis_xport;
	uint16_t			ndis_tun;
	uint16_t			ndis_xport_tun;
	uint16_t			ndis_lso;
	uint16_t			ndis_extseq;
	uint32_t			ndis_udp_esp;
	uint32_t			ndis_auth;
	uint32_t			ndis_crypto;
	uint32_t			ndis_sa_caps;
};

struct ndis_rsc_offload {
	uint16_t			ndis_ip4;
	uint16_t			ndis_ip6;
};

struct ndis_encap_offload {
	uint32_t			ndis_flags;
	uint32_t			ndis_maxhdr;
};

struct ndis_offload {
	struct ndis_object_hdr		ndis_hdr;
	struct ndis_csum_offload	ndis_csum;
	struct ndis_lsov1_offload	ndis_lsov1;
	struct ndis_ipsecv1_offload	ndis_ipsecv1;
	struct ndis_lsov2_offload	ndis_lsov2;
	uint32_t			ndis_flags;
	/* NDIS >= 6.1 */
	struct ndis_ipsecv2_offload	ndis_ipsecv2;
	/* NDIS >= 6.30 */
	struct ndis_rsc_offload		ndis_rsc;
	struct ndis_encap_offload	ndis_encap_gre;
};

#define	NDIS_OFFLOAD_SIZE		sizeof(struct ndis_offload)
#define	NDIS_OFFLOAD_SIZE_6_0		\
	__offsetof(struct ndis_offload, ndis_ipsecv2)
#define	NDIS_OFFLOAD_SIZE_6_1		\
	__offsetof(struct ndis_offload, ndis_rsc)

#define	NDIS_OFFLOAD_REV_1		1	/* NDIS 6.0 */
#define	NDIS_OFFLOAD_REV_2		2	/* NDIS 6.1 */
#define	NDIS_OFFLOAD_REV_3		3	/* NDIS 6.30 */

/*
 * Per-packet-info
 */

/* VLAN */
#define	NDIS_VLAN_INFO_SIZE		sizeof(uint32_t)
#define	NDIS_VLAN_INFO_PRI_MASK		0x0007
#define	NDIS_VLAN_INFO_CFI_MASK		0x0008
#define	NDIS_VLAN_INFO_ID_MASK		0xfff0
#define	NDIS_VLAN_INFO_MAKE(id, pri, cfi)	\
        (((pri) & NDIS_VLAN_INFO_PRI_MASK) |	\
	 (((cfi) & 0x1) << 3) | (((id) & 0xfff) << 4))
#define	NDIS_VLAN_INFO_ID(inf)		(((inf) & NDIS_VLAN_INFO_ID_MASK) >> 4)
#define	NDIS_VLAN_INFO_CFI(inf)		(((inf) & NDIS_VLAN_INFO_CFI_MASK) >> 3)
#define	NDIS_VLAN_INFO_PRI(inf)		((inf) & NDIS_VLAN_INFO_PRI_MASK)

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

/* LSOv2 */
#define	NDIS_LSO2_INFO_SIZE		sizeof(uint32_t)
#define	NDIS_LSO2_INFO_MSS_MASK		0x000fffff
#define	NDIS_LSO2_INFO_THOFF_MASK	0x3ff00000
#define	NDIS_LSO2_INFO_ISLSO2		0x40000000
#define	NDIS_LSO2_INFO_ISIPV6		0x80000000

#define	NDIS_LSO2_INFO_MAKE(thoff, mss)				\
	((((uint32_t)(mss)) & NDIS_LSO2_INFO_MSS_MASK) |	\
	 ((((uint32_t)(thoff)) & 0x3ff) << 20) |		\
	 NDIS_LSO2_INFO_ISLSO2)

#define	NDIS_LSO2_INFO_MAKEIPV4(thoff, mss)			\
	NDIS_LSO2_INFO_MAKE((thoff), (mss))

#define	NDIS_LSO2_INFO_MAKEIPV6(thoff, mss)			\
	(NDIS_LSO2_INFO_MAKE((thoff), (mss)) | NDIS_LSO2_INFO_ISIPV6)

/* Transmission checksum */
#define	NDIS_TXCSUM_INFO_SIZE		sizeof(uint32_t)
#define	NDIS_TXCSUM_INFO_IPV4		0x00000001
#define	NDIS_TXCSUM_INFO_IPV6		0x00000002
#define	NDIS_TXCSUM_INFO_TCPCS		0x00000004
#define	NDIS_TXCSUM_INFO_UDPCS		0x00000008
#define	NDIS_TXCSUM_INFO_IPCS		0x00000010
#define	NDIS_TXCSUM_INFO_THOFF		0x03ff0000

#define	NDIS_TXCSUM_INFO_MKL4CS(thoff, flag)			\
	((((uint32_t)(thoff)) << 16) | (flag))

#define	NDIS_TXCSUM_INFO_MKTCPCS(thoff)				\
	NDIS_TXCSUM_INFO_MKL4CS((thoff), NDIS_TXCSUM_INFO_TCPCS)

#define	NDIS_TXCSUM_INFO_MKUDPCS(thoff)				\
	NDIS_TXCSUM_INFO_MKL4CS((thoff), NDIS_TXCSUM_INFO_UDPCS)

#endif	/* !_NET_NDIS_H_ */
