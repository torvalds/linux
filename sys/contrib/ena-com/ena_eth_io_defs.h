/*-
 * BSD LICENSE
 *
 * Copyright (c) 2015-2017 Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the
 * distribution.
 * * Neither the name of copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ENA_ETH_IO_H_
#define _ENA_ETH_IO_H_

enum ena_eth_io_l3_proto_index {
	ENA_ETH_IO_L3_PROTO_UNKNOWN	= 0,

	ENA_ETH_IO_L3_PROTO_IPV4	= 8,

	ENA_ETH_IO_L3_PROTO_IPV6	= 11,

	ENA_ETH_IO_L3_PROTO_FCOE	= 21,

	ENA_ETH_IO_L3_PROTO_ROCE	= 22,
};

enum ena_eth_io_l4_proto_index {
	ENA_ETH_IO_L4_PROTO_UNKNOWN		= 0,

	ENA_ETH_IO_L4_PROTO_TCP			= 12,

	ENA_ETH_IO_L4_PROTO_UDP			= 13,

	ENA_ETH_IO_L4_PROTO_ROUTEABLE_ROCE	= 23,
};

struct ena_eth_io_tx_desc {
	/* 15:0 : length - Buffer length in bytes, must
	 *    include any packet trailers that the ENA supposed
	 *    to update like End-to-End CRC, Authentication GMAC
	 *    etc. This length must not include the
	 *    'Push_Buffer' length. This length must not include
	 *    the 4-byte added in the end for 802.3 Ethernet FCS
	 * 21:16 : req_id_hi - Request ID[15:10]
	 * 22 : reserved22 - MBZ
	 * 23 : meta_desc - MBZ
	 * 24 : phase
	 * 25 : reserved1 - MBZ
	 * 26 : first - Indicates first descriptor in
	 *    transaction
	 * 27 : last - Indicates last descriptor in
	 *    transaction
	 * 28 : comp_req - Indicates whether completion
	 *    should be posted, after packet is transmitted.
	 *    Valid only for first descriptor
	 * 30:29 : reserved29 - MBZ
	 * 31 : reserved31 - MBZ
	 */
	uint32_t len_ctrl;

	/* 3:0 : l3_proto_idx - L3 protocol. This field
	 *    required when l3_csum_en,l3_csum or tso_en are set.
	 * 4 : DF - IPv4 DF, must be 0 if packet is IPv4 and
	 *    DF flags of the IPv4 header is 0. Otherwise must
	 *    be set to 1
	 * 6:5 : reserved5
	 * 7 : tso_en - Enable TSO, For TCP only.
	 * 12:8 : l4_proto_idx - L4 protocol. This field need
	 *    to be set when l4_csum_en or tso_en are set.
	 * 13 : l3_csum_en - enable IPv4 header checksum.
	 * 14 : l4_csum_en - enable TCP/UDP checksum.
	 * 15 : ethernet_fcs_dis - when set, the controller
	 *    will not append the 802.3 Ethernet Frame Check
	 *    Sequence to the packet
	 * 16 : reserved16
	 * 17 : l4_csum_partial - L4 partial checksum. when
	 *    set to 0, the ENA calculates the L4 checksum,
	 *    where the Destination Address required for the
	 *    TCP/UDP pseudo-header is taken from the actual
	 *    packet L3 header. when set to 1, the ENA doesn't
	 *    calculate the sum of the pseudo-header, instead,
	 *    the checksum field of the L4 is used instead. When
	 *    TSO enabled, the checksum of the pseudo-header
	 *    must not include the tcp length field. L4 partial
	 *    checksum should be used for IPv6 packet that
	 *    contains Routing Headers.
	 * 20:18 : reserved18 - MBZ
	 * 21 : reserved21 - MBZ
	 * 31:22 : req_id_lo - Request ID[9:0]
	 */
	uint32_t meta_ctrl;

	uint32_t buff_addr_lo;

	/* address high and header size
	 * 15:0 : addr_hi - Buffer Pointer[47:32]
	 * 23:16 : reserved16_w2
	 * 31:24 : header_length - Header length. For Low
	 *    Latency Queues, this fields indicates the number
	 *    of bytes written to the headers' memory. For
	 *    normal queues, if packet is TCP or UDP, and longer
	 *    than max_header_size, then this field should be
	 *    set to the sum of L4 header offset and L4 header
	 *    size(without options), otherwise, this field
	 *    should be set to 0. For both modes, this field
	 *    must not exceed the max_header_size.
	 *    max_header_size value is reported by the Max
	 *    Queues Feature descriptor
	 */
	uint32_t buff_addr_hi_hdr_sz;
};

struct ena_eth_io_tx_meta_desc {
	/* 9:0 : req_id_lo - Request ID[9:0]
	 * 11:10 : reserved10 - MBZ
	 * 12 : reserved12 - MBZ
	 * 13 : reserved13 - MBZ
	 * 14 : ext_valid - if set, offset fields in Word2
	 *    are valid Also MSS High in Word 0 and bits [31:24]
	 *    in Word 3
	 * 15 : reserved15
	 * 19:16 : mss_hi
	 * 20 : eth_meta_type - 0: Tx Metadata Descriptor, 1:
	 *    Extended Metadata Descriptor
	 * 21 : meta_store - Store extended metadata in queue
	 *    cache
	 * 22 : reserved22 - MBZ
	 * 23 : meta_desc - MBO
	 * 24 : phase
	 * 25 : reserved25 - MBZ
	 * 26 : first - Indicates first descriptor in
	 *    transaction
	 * 27 : last - Indicates last descriptor in
	 *    transaction
	 * 28 : comp_req - Indicates whether completion
	 *    should be posted, after packet is transmitted.
	 *    Valid only for first descriptor
	 * 30:29 : reserved29 - MBZ
	 * 31 : reserved31 - MBZ
	 */
	uint32_t len_ctrl;

	/* 5:0 : req_id_hi
	 * 31:6 : reserved6 - MBZ
	 */
	uint32_t word1;

	/* 7:0 : l3_hdr_len
	 * 15:8 : l3_hdr_off
	 * 21:16 : l4_hdr_len_in_words - counts the L4 header
	 *    length in words. there is an explicit assumption
	 *    that L4 header appears right after L3 header and
	 *    L4 offset is based on l3_hdr_off+l3_hdr_len
	 * 31:22 : mss_lo
	 */
	uint32_t word2;

	uint32_t reserved;
};

struct ena_eth_io_tx_cdesc {
	/* Request ID[15:0] */
	uint16_t req_id;

	uint8_t status;

	/* flags
	 * 0 : phase
	 * 7:1 : reserved1
	 */
	uint8_t flags;

	uint16_t sub_qid;

	uint16_t sq_head_idx;
};

struct ena_eth_io_rx_desc {
	/* In bytes. 0 means 64KB */
	uint16_t length;

	/* MBZ */
	uint8_t reserved2;

	/* 0 : phase
	 * 1 : reserved1 - MBZ
	 * 2 : first - Indicates first descriptor in
	 *    transaction
	 * 3 : last - Indicates last descriptor in transaction
	 * 4 : comp_req
	 * 5 : reserved5 - MBO
	 * 7:6 : reserved6 - MBZ
	 */
	uint8_t ctrl;

	uint16_t req_id;

	/* MBZ */
	uint16_t reserved6;

	uint32_t buff_addr_lo;

	uint16_t buff_addr_hi;

	/* MBZ */
	uint16_t reserved16_w3;
};

/* 4-word format Note: all ethernet parsing information are valid only when
 * last=1
 */
struct ena_eth_io_rx_cdesc_base {
	/* 4:0 : l3_proto_idx
	 * 6:5 : src_vlan_cnt
	 * 7 : reserved7 - MBZ
	 * 12:8 : l4_proto_idx
	 * 13 : l3_csum_err - when set, either the L3
	 *    checksum error detected, or, the controller didn't
	 *    validate the checksum. This bit is valid only when
	 *    l3_proto_idx indicates IPv4 packet
	 * 14 : l4_csum_err - when set, either the L4
	 *    checksum error detected, or, the controller didn't
	 *    validate the checksum. This bit is valid only when
	 *    l4_proto_idx indicates TCP/UDP packet, and,
	 *    ipv4_frag is not set
	 * 15 : ipv4_frag - Indicates IPv4 fragmented packet
	 * 23:16 : reserved16
	 * 24 : phase
	 * 25 : l3_csum2 - second checksum engine result
	 * 26 : first - Indicates first descriptor in
	 *    transaction
	 * 27 : last - Indicates last descriptor in
	 *    transaction
	 * 29:28 : reserved28
	 * 30 : buffer - 0: Metadata descriptor. 1: Buffer
	 *    Descriptor was used
	 * 31 : reserved31
	 */
	uint32_t status;

	uint16_t length;

	uint16_t req_id;

	/* 32-bit hash result */
	uint32_t hash;

	uint16_t sub_qid;

	uint16_t reserved;
};

/* 8-word format */
struct ena_eth_io_rx_cdesc_ext {
	struct ena_eth_io_rx_cdesc_base base;

	uint32_t buff_addr_lo;

	uint16_t buff_addr_hi;

	uint16_t reserved16;

	uint32_t reserved_w6;

	uint32_t reserved_w7;
};

struct ena_eth_io_intr_reg {
	/* 14:0 : rx_intr_delay
	 * 29:15 : tx_intr_delay
	 * 30 : intr_unmask
	 * 31 : reserved
	 */
	uint32_t intr_control;
};

struct ena_eth_io_numa_node_cfg_reg {
	/* 7:0 : numa
	 * 30:8 : reserved
	 * 31 : enabled
	 */
	uint32_t numa_cfg;
};

/* tx_desc */
#define ENA_ETH_IO_TX_DESC_LENGTH_MASK GENMASK(15, 0)
#define ENA_ETH_IO_TX_DESC_REQ_ID_HI_SHIFT 16
#define ENA_ETH_IO_TX_DESC_REQ_ID_HI_MASK GENMASK(21, 16)
#define ENA_ETH_IO_TX_DESC_META_DESC_SHIFT 23
#define ENA_ETH_IO_TX_DESC_META_DESC_MASK BIT(23)
#define ENA_ETH_IO_TX_DESC_PHASE_SHIFT 24
#define ENA_ETH_IO_TX_DESC_PHASE_MASK BIT(24)
#define ENA_ETH_IO_TX_DESC_FIRST_SHIFT 26
#define ENA_ETH_IO_TX_DESC_FIRST_MASK BIT(26)
#define ENA_ETH_IO_TX_DESC_LAST_SHIFT 27
#define ENA_ETH_IO_TX_DESC_LAST_MASK BIT(27)
#define ENA_ETH_IO_TX_DESC_COMP_REQ_SHIFT 28
#define ENA_ETH_IO_TX_DESC_COMP_REQ_MASK BIT(28)
#define ENA_ETH_IO_TX_DESC_L3_PROTO_IDX_MASK GENMASK(3, 0)
#define ENA_ETH_IO_TX_DESC_DF_SHIFT 4
#define ENA_ETH_IO_TX_DESC_DF_MASK BIT(4)
#define ENA_ETH_IO_TX_DESC_TSO_EN_SHIFT 7
#define ENA_ETH_IO_TX_DESC_TSO_EN_MASK BIT(7)
#define ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_SHIFT 8
#define ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_MASK GENMASK(12, 8)
#define ENA_ETH_IO_TX_DESC_L3_CSUM_EN_SHIFT 13
#define ENA_ETH_IO_TX_DESC_L3_CSUM_EN_MASK BIT(13)
#define ENA_ETH_IO_TX_DESC_L4_CSUM_EN_SHIFT 14
#define ENA_ETH_IO_TX_DESC_L4_CSUM_EN_MASK BIT(14)
#define ENA_ETH_IO_TX_DESC_ETHERNET_FCS_DIS_SHIFT 15
#define ENA_ETH_IO_TX_DESC_ETHERNET_FCS_DIS_MASK BIT(15)
#define ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_SHIFT 17
#define ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_MASK BIT(17)
#define ENA_ETH_IO_TX_DESC_REQ_ID_LO_SHIFT 22
#define ENA_ETH_IO_TX_DESC_REQ_ID_LO_MASK GENMASK(31, 22)
#define ENA_ETH_IO_TX_DESC_ADDR_HI_MASK GENMASK(15, 0)
#define ENA_ETH_IO_TX_DESC_HEADER_LENGTH_SHIFT 24
#define ENA_ETH_IO_TX_DESC_HEADER_LENGTH_MASK GENMASK(31, 24)

/* tx_meta_desc */
#define ENA_ETH_IO_TX_META_DESC_REQ_ID_LO_MASK GENMASK(9, 0)
#define ENA_ETH_IO_TX_META_DESC_EXT_VALID_SHIFT 14
#define ENA_ETH_IO_TX_META_DESC_EXT_VALID_MASK BIT(14)
#define ENA_ETH_IO_TX_META_DESC_MSS_HI_SHIFT 16
#define ENA_ETH_IO_TX_META_DESC_MSS_HI_MASK GENMASK(19, 16)
#define ENA_ETH_IO_TX_META_DESC_ETH_META_TYPE_SHIFT 20
#define ENA_ETH_IO_TX_META_DESC_ETH_META_TYPE_MASK BIT(20)
#define ENA_ETH_IO_TX_META_DESC_META_STORE_SHIFT 21
#define ENA_ETH_IO_TX_META_DESC_META_STORE_MASK BIT(21)
#define ENA_ETH_IO_TX_META_DESC_META_DESC_SHIFT 23
#define ENA_ETH_IO_TX_META_DESC_META_DESC_MASK BIT(23)
#define ENA_ETH_IO_TX_META_DESC_PHASE_SHIFT 24
#define ENA_ETH_IO_TX_META_DESC_PHASE_MASK BIT(24)
#define ENA_ETH_IO_TX_META_DESC_FIRST_SHIFT 26
#define ENA_ETH_IO_TX_META_DESC_FIRST_MASK BIT(26)
#define ENA_ETH_IO_TX_META_DESC_LAST_SHIFT 27
#define ENA_ETH_IO_TX_META_DESC_LAST_MASK BIT(27)
#define ENA_ETH_IO_TX_META_DESC_COMP_REQ_SHIFT 28
#define ENA_ETH_IO_TX_META_DESC_COMP_REQ_MASK BIT(28)
#define ENA_ETH_IO_TX_META_DESC_REQ_ID_HI_MASK GENMASK(5, 0)
#define ENA_ETH_IO_TX_META_DESC_L3_HDR_LEN_MASK GENMASK(7, 0)
#define ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_SHIFT 8
#define ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_MASK GENMASK(15, 8)
#define ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_SHIFT 16
#define ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_MASK GENMASK(21, 16)
#define ENA_ETH_IO_TX_META_DESC_MSS_LO_SHIFT 22
#define ENA_ETH_IO_TX_META_DESC_MSS_LO_MASK GENMASK(31, 22)

/* tx_cdesc */
#define ENA_ETH_IO_TX_CDESC_PHASE_MASK BIT(0)

/* rx_desc */
#define ENA_ETH_IO_RX_DESC_PHASE_MASK BIT(0)
#define ENA_ETH_IO_RX_DESC_FIRST_SHIFT 2
#define ENA_ETH_IO_RX_DESC_FIRST_MASK BIT(2)
#define ENA_ETH_IO_RX_DESC_LAST_SHIFT 3
#define ENA_ETH_IO_RX_DESC_LAST_MASK BIT(3)
#define ENA_ETH_IO_RX_DESC_COMP_REQ_SHIFT 4
#define ENA_ETH_IO_RX_DESC_COMP_REQ_MASK BIT(4)

/* rx_cdesc_base */
#define ENA_ETH_IO_RX_CDESC_BASE_L3_PROTO_IDX_MASK GENMASK(4, 0)
#define ENA_ETH_IO_RX_CDESC_BASE_SRC_VLAN_CNT_SHIFT 5
#define ENA_ETH_IO_RX_CDESC_BASE_SRC_VLAN_CNT_MASK GENMASK(6, 5)
#define ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_SHIFT 8
#define ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_MASK GENMASK(12, 8)
#define ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_SHIFT 13
#define ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_MASK BIT(13)
#define ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_SHIFT 14
#define ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_MASK BIT(14)
#define ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_SHIFT 15
#define ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_MASK BIT(15)
#define ENA_ETH_IO_RX_CDESC_BASE_PHASE_SHIFT 24
#define ENA_ETH_IO_RX_CDESC_BASE_PHASE_MASK BIT(24)
#define ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM2_SHIFT 25
#define ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM2_MASK BIT(25)
#define ENA_ETH_IO_RX_CDESC_BASE_FIRST_SHIFT 26
#define ENA_ETH_IO_RX_CDESC_BASE_FIRST_MASK BIT(26)
#define ENA_ETH_IO_RX_CDESC_BASE_LAST_SHIFT 27
#define ENA_ETH_IO_RX_CDESC_BASE_LAST_MASK BIT(27)
#define ENA_ETH_IO_RX_CDESC_BASE_BUFFER_SHIFT 30
#define ENA_ETH_IO_RX_CDESC_BASE_BUFFER_MASK BIT(30)

/* intr_reg */
#define ENA_ETH_IO_INTR_REG_RX_INTR_DELAY_MASK GENMASK(14, 0)
#define ENA_ETH_IO_INTR_REG_TX_INTR_DELAY_SHIFT 15
#define ENA_ETH_IO_INTR_REG_TX_INTR_DELAY_MASK GENMASK(29, 15)
#define ENA_ETH_IO_INTR_REG_INTR_UNMASK_SHIFT 30
#define ENA_ETH_IO_INTR_REG_INTR_UNMASK_MASK BIT(30)

/* numa_node_cfg_reg */
#define ENA_ETH_IO_NUMA_NODE_CFG_REG_NUMA_MASK GENMASK(7, 0)
#define ENA_ETH_IO_NUMA_NODE_CFG_REG_ENABLED_SHIFT 31
#define ENA_ETH_IO_NUMA_NODE_CFG_REG_ENABLED_MASK BIT(31)

#if !defined(ENA_DEFS_LINUX_MAINLINE)
static inline uint32_t get_ena_eth_io_tx_desc_length(const struct ena_eth_io_tx_desc *p)
{
	return p->len_ctrl & ENA_ETH_IO_TX_DESC_LENGTH_MASK;
}

static inline void set_ena_eth_io_tx_desc_length(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->len_ctrl |= val & ENA_ETH_IO_TX_DESC_LENGTH_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_req_id_hi(const struct ena_eth_io_tx_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_DESC_REQ_ID_HI_MASK) >> ENA_ETH_IO_TX_DESC_REQ_ID_HI_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_req_id_hi(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_DESC_REQ_ID_HI_SHIFT) & ENA_ETH_IO_TX_DESC_REQ_ID_HI_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_meta_desc(const struct ena_eth_io_tx_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_DESC_META_DESC_MASK) >> ENA_ETH_IO_TX_DESC_META_DESC_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_meta_desc(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_DESC_META_DESC_SHIFT) & ENA_ETH_IO_TX_DESC_META_DESC_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_phase(const struct ena_eth_io_tx_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_DESC_PHASE_MASK) >> ENA_ETH_IO_TX_DESC_PHASE_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_phase(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_DESC_PHASE_SHIFT) & ENA_ETH_IO_TX_DESC_PHASE_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_first(const struct ena_eth_io_tx_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_DESC_FIRST_MASK) >> ENA_ETH_IO_TX_DESC_FIRST_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_first(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_DESC_FIRST_SHIFT) & ENA_ETH_IO_TX_DESC_FIRST_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_last(const struct ena_eth_io_tx_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_DESC_LAST_MASK) >> ENA_ETH_IO_TX_DESC_LAST_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_last(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_DESC_LAST_SHIFT) & ENA_ETH_IO_TX_DESC_LAST_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_comp_req(const struct ena_eth_io_tx_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_DESC_COMP_REQ_MASK) >> ENA_ETH_IO_TX_DESC_COMP_REQ_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_comp_req(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_DESC_COMP_REQ_SHIFT) & ENA_ETH_IO_TX_DESC_COMP_REQ_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_l3_proto_idx(const struct ena_eth_io_tx_desc *p)
{
	return p->meta_ctrl & ENA_ETH_IO_TX_DESC_L3_PROTO_IDX_MASK;
}

static inline void set_ena_eth_io_tx_desc_l3_proto_idx(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->meta_ctrl |= val & ENA_ETH_IO_TX_DESC_L3_PROTO_IDX_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_DF(const struct ena_eth_io_tx_desc *p)
{
	return (p->meta_ctrl & ENA_ETH_IO_TX_DESC_DF_MASK) >> ENA_ETH_IO_TX_DESC_DF_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_DF(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->meta_ctrl |= (val << ENA_ETH_IO_TX_DESC_DF_SHIFT) & ENA_ETH_IO_TX_DESC_DF_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_tso_en(const struct ena_eth_io_tx_desc *p)
{
	return (p->meta_ctrl & ENA_ETH_IO_TX_DESC_TSO_EN_MASK) >> ENA_ETH_IO_TX_DESC_TSO_EN_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_tso_en(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->meta_ctrl |= (val << ENA_ETH_IO_TX_DESC_TSO_EN_SHIFT) & ENA_ETH_IO_TX_DESC_TSO_EN_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_l4_proto_idx(const struct ena_eth_io_tx_desc *p)
{
	return (p->meta_ctrl & ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_MASK) >> ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_l4_proto_idx(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->meta_ctrl |= (val << ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_SHIFT) & ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_l3_csum_en(const struct ena_eth_io_tx_desc *p)
{
	return (p->meta_ctrl & ENA_ETH_IO_TX_DESC_L3_CSUM_EN_MASK) >> ENA_ETH_IO_TX_DESC_L3_CSUM_EN_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_l3_csum_en(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->meta_ctrl |= (val << ENA_ETH_IO_TX_DESC_L3_CSUM_EN_SHIFT) & ENA_ETH_IO_TX_DESC_L3_CSUM_EN_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_l4_csum_en(const struct ena_eth_io_tx_desc *p)
{
	return (p->meta_ctrl & ENA_ETH_IO_TX_DESC_L4_CSUM_EN_MASK) >> ENA_ETH_IO_TX_DESC_L4_CSUM_EN_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_l4_csum_en(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->meta_ctrl |= (val << ENA_ETH_IO_TX_DESC_L4_CSUM_EN_SHIFT) & ENA_ETH_IO_TX_DESC_L4_CSUM_EN_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_ethernet_fcs_dis(const struct ena_eth_io_tx_desc *p)
{
	return (p->meta_ctrl & ENA_ETH_IO_TX_DESC_ETHERNET_FCS_DIS_MASK) >> ENA_ETH_IO_TX_DESC_ETHERNET_FCS_DIS_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_ethernet_fcs_dis(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->meta_ctrl |= (val << ENA_ETH_IO_TX_DESC_ETHERNET_FCS_DIS_SHIFT) & ENA_ETH_IO_TX_DESC_ETHERNET_FCS_DIS_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_l4_csum_partial(const struct ena_eth_io_tx_desc *p)
{
	return (p->meta_ctrl & ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_MASK) >> ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_l4_csum_partial(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->meta_ctrl |= (val << ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_SHIFT) & ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_req_id_lo(const struct ena_eth_io_tx_desc *p)
{
	return (p->meta_ctrl & ENA_ETH_IO_TX_DESC_REQ_ID_LO_MASK) >> ENA_ETH_IO_TX_DESC_REQ_ID_LO_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_req_id_lo(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->meta_ctrl |= (val << ENA_ETH_IO_TX_DESC_REQ_ID_LO_SHIFT) & ENA_ETH_IO_TX_DESC_REQ_ID_LO_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_addr_hi(const struct ena_eth_io_tx_desc *p)
{
	return p->buff_addr_hi_hdr_sz & ENA_ETH_IO_TX_DESC_ADDR_HI_MASK;
}

static inline void set_ena_eth_io_tx_desc_addr_hi(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->buff_addr_hi_hdr_sz |= val & ENA_ETH_IO_TX_DESC_ADDR_HI_MASK;
}

static inline uint32_t get_ena_eth_io_tx_desc_header_length(const struct ena_eth_io_tx_desc *p)
{
	return (p->buff_addr_hi_hdr_sz & ENA_ETH_IO_TX_DESC_HEADER_LENGTH_MASK) >> ENA_ETH_IO_TX_DESC_HEADER_LENGTH_SHIFT;
}

static inline void set_ena_eth_io_tx_desc_header_length(struct ena_eth_io_tx_desc *p, uint32_t val)
{
	p->buff_addr_hi_hdr_sz |= (val << ENA_ETH_IO_TX_DESC_HEADER_LENGTH_SHIFT) & ENA_ETH_IO_TX_DESC_HEADER_LENGTH_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_req_id_lo(const struct ena_eth_io_tx_meta_desc *p)
{
	return p->len_ctrl & ENA_ETH_IO_TX_META_DESC_REQ_ID_LO_MASK;
}

static inline void set_ena_eth_io_tx_meta_desc_req_id_lo(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->len_ctrl |= val & ENA_ETH_IO_TX_META_DESC_REQ_ID_LO_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_ext_valid(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_META_DESC_EXT_VALID_MASK) >> ENA_ETH_IO_TX_META_DESC_EXT_VALID_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_ext_valid(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_META_DESC_EXT_VALID_SHIFT) & ENA_ETH_IO_TX_META_DESC_EXT_VALID_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_mss_hi(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_META_DESC_MSS_HI_MASK) >> ENA_ETH_IO_TX_META_DESC_MSS_HI_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_mss_hi(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_META_DESC_MSS_HI_SHIFT) & ENA_ETH_IO_TX_META_DESC_MSS_HI_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_eth_meta_type(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_META_DESC_ETH_META_TYPE_MASK) >> ENA_ETH_IO_TX_META_DESC_ETH_META_TYPE_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_eth_meta_type(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_META_DESC_ETH_META_TYPE_SHIFT) & ENA_ETH_IO_TX_META_DESC_ETH_META_TYPE_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_meta_store(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_META_DESC_META_STORE_MASK) >> ENA_ETH_IO_TX_META_DESC_META_STORE_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_meta_store(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_META_DESC_META_STORE_SHIFT) & ENA_ETH_IO_TX_META_DESC_META_STORE_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_meta_desc(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_META_DESC_META_DESC_MASK) >> ENA_ETH_IO_TX_META_DESC_META_DESC_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_meta_desc(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_META_DESC_META_DESC_SHIFT) & ENA_ETH_IO_TX_META_DESC_META_DESC_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_phase(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_META_DESC_PHASE_MASK) >> ENA_ETH_IO_TX_META_DESC_PHASE_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_phase(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_META_DESC_PHASE_SHIFT) & ENA_ETH_IO_TX_META_DESC_PHASE_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_first(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_META_DESC_FIRST_MASK) >> ENA_ETH_IO_TX_META_DESC_FIRST_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_first(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_META_DESC_FIRST_SHIFT) & ENA_ETH_IO_TX_META_DESC_FIRST_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_last(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_META_DESC_LAST_MASK) >> ENA_ETH_IO_TX_META_DESC_LAST_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_last(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_META_DESC_LAST_SHIFT) & ENA_ETH_IO_TX_META_DESC_LAST_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_comp_req(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->len_ctrl & ENA_ETH_IO_TX_META_DESC_COMP_REQ_MASK) >> ENA_ETH_IO_TX_META_DESC_COMP_REQ_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_comp_req(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->len_ctrl |= (val << ENA_ETH_IO_TX_META_DESC_COMP_REQ_SHIFT) & ENA_ETH_IO_TX_META_DESC_COMP_REQ_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_req_id_hi(const struct ena_eth_io_tx_meta_desc *p)
{
	return p->word1 & ENA_ETH_IO_TX_META_DESC_REQ_ID_HI_MASK;
}

static inline void set_ena_eth_io_tx_meta_desc_req_id_hi(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->word1 |= val & ENA_ETH_IO_TX_META_DESC_REQ_ID_HI_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_l3_hdr_len(const struct ena_eth_io_tx_meta_desc *p)
{
	return p->word2 & ENA_ETH_IO_TX_META_DESC_L3_HDR_LEN_MASK;
}

static inline void set_ena_eth_io_tx_meta_desc_l3_hdr_len(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->word2 |= val & ENA_ETH_IO_TX_META_DESC_L3_HDR_LEN_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_l3_hdr_off(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->word2 & ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_MASK) >> ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_l3_hdr_off(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->word2 |= (val << ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_SHIFT) & ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_l4_hdr_len_in_words(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->word2 & ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_MASK) >> ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_l4_hdr_len_in_words(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->word2 |= (val << ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_SHIFT) & ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_MASK;
}

static inline uint32_t get_ena_eth_io_tx_meta_desc_mss_lo(const struct ena_eth_io_tx_meta_desc *p)
{
	return (p->word2 & ENA_ETH_IO_TX_META_DESC_MSS_LO_MASK) >> ENA_ETH_IO_TX_META_DESC_MSS_LO_SHIFT;
}

static inline void set_ena_eth_io_tx_meta_desc_mss_lo(struct ena_eth_io_tx_meta_desc *p, uint32_t val)
{
	p->word2 |= (val << ENA_ETH_IO_TX_META_DESC_MSS_LO_SHIFT) & ENA_ETH_IO_TX_META_DESC_MSS_LO_MASK;
}

static inline uint8_t get_ena_eth_io_tx_cdesc_phase(const struct ena_eth_io_tx_cdesc *p)
{
	return p->flags & ENA_ETH_IO_TX_CDESC_PHASE_MASK;
}

static inline void set_ena_eth_io_tx_cdesc_phase(struct ena_eth_io_tx_cdesc *p, uint8_t val)
{
	p->flags |= val & ENA_ETH_IO_TX_CDESC_PHASE_MASK;
}

static inline uint8_t get_ena_eth_io_rx_desc_phase(const struct ena_eth_io_rx_desc *p)
{
	return p->ctrl & ENA_ETH_IO_RX_DESC_PHASE_MASK;
}

static inline void set_ena_eth_io_rx_desc_phase(struct ena_eth_io_rx_desc *p, uint8_t val)
{
	p->ctrl |= val & ENA_ETH_IO_RX_DESC_PHASE_MASK;
}

static inline uint8_t get_ena_eth_io_rx_desc_first(const struct ena_eth_io_rx_desc *p)
{
	return (p->ctrl & ENA_ETH_IO_RX_DESC_FIRST_MASK) >> ENA_ETH_IO_RX_DESC_FIRST_SHIFT;
}

static inline void set_ena_eth_io_rx_desc_first(struct ena_eth_io_rx_desc *p, uint8_t val)
{
	p->ctrl |= (val << ENA_ETH_IO_RX_DESC_FIRST_SHIFT) & ENA_ETH_IO_RX_DESC_FIRST_MASK;
}

static inline uint8_t get_ena_eth_io_rx_desc_last(const struct ena_eth_io_rx_desc *p)
{
	return (p->ctrl & ENA_ETH_IO_RX_DESC_LAST_MASK) >> ENA_ETH_IO_RX_DESC_LAST_SHIFT;
}

static inline void set_ena_eth_io_rx_desc_last(struct ena_eth_io_rx_desc *p, uint8_t val)
{
	p->ctrl |= (val << ENA_ETH_IO_RX_DESC_LAST_SHIFT) & ENA_ETH_IO_RX_DESC_LAST_MASK;
}

static inline uint8_t get_ena_eth_io_rx_desc_comp_req(const struct ena_eth_io_rx_desc *p)
{
	return (p->ctrl & ENA_ETH_IO_RX_DESC_COMP_REQ_MASK) >> ENA_ETH_IO_RX_DESC_COMP_REQ_SHIFT;
}

static inline void set_ena_eth_io_rx_desc_comp_req(struct ena_eth_io_rx_desc *p, uint8_t val)
{
	p->ctrl |= (val << ENA_ETH_IO_RX_DESC_COMP_REQ_SHIFT) & ENA_ETH_IO_RX_DESC_COMP_REQ_MASK;
}

static inline uint32_t get_ena_eth_io_rx_cdesc_base_l3_proto_idx(const struct ena_eth_io_rx_cdesc_base *p)
{
	return p->status & ENA_ETH_IO_RX_CDESC_BASE_L3_PROTO_IDX_MASK;
}

static inline void set_ena_eth_io_rx_cdesc_base_l3_proto_idx(struct ena_eth_io_rx_cdesc_base *p, uint32_t val)
{
	p->status |= val & ENA_ETH_IO_RX_CDESC_BASE_L3_PROTO_IDX_MASK;
}

static inline uint32_t get_ena_eth_io_rx_cdesc_base_src_vlan_cnt(const struct ena_eth_io_rx_cdesc_base *p)
{
	return (p->status & ENA_ETH_IO_RX_CDESC_BASE_SRC_VLAN_CNT_MASK) >> ENA_ETH_IO_RX_CDESC_BASE_SRC_VLAN_CNT_SHIFT;
}

static inline void set_ena_eth_io_rx_cdesc_base_src_vlan_cnt(struct ena_eth_io_rx_cdesc_base *p, uint32_t val)
{
	p->status |= (val << ENA_ETH_IO_RX_CDESC_BASE_SRC_VLAN_CNT_SHIFT) & ENA_ETH_IO_RX_CDESC_BASE_SRC_VLAN_CNT_MASK;
}

static inline uint32_t get_ena_eth_io_rx_cdesc_base_l4_proto_idx(const struct ena_eth_io_rx_cdesc_base *p)
{
	return (p->status & ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_MASK) >> ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_SHIFT;
}

static inline void set_ena_eth_io_rx_cdesc_base_l4_proto_idx(struct ena_eth_io_rx_cdesc_base *p, uint32_t val)
{
	p->status |= (val << ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_SHIFT) & ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_MASK;
}

static inline uint32_t get_ena_eth_io_rx_cdesc_base_l3_csum_err(const struct ena_eth_io_rx_cdesc_base *p)
{
	return (p->status & ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_MASK) >> ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_SHIFT;
}

static inline void set_ena_eth_io_rx_cdesc_base_l3_csum_err(struct ena_eth_io_rx_cdesc_base *p, uint32_t val)
{
	p->status |= (val << ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_SHIFT) & ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_MASK;
}

static inline uint32_t get_ena_eth_io_rx_cdesc_base_l4_csum_err(const struct ena_eth_io_rx_cdesc_base *p)
{
	return (p->status & ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_MASK) >> ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_SHIFT;
}

static inline void set_ena_eth_io_rx_cdesc_base_l4_csum_err(struct ena_eth_io_rx_cdesc_base *p, uint32_t val)
{
	p->status |= (val << ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_SHIFT) & ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_MASK;
}

static inline uint32_t get_ena_eth_io_rx_cdesc_base_ipv4_frag(const struct ena_eth_io_rx_cdesc_base *p)
{
	return (p->status & ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_MASK) >> ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_SHIFT;
}

static inline void set_ena_eth_io_rx_cdesc_base_ipv4_frag(struct ena_eth_io_rx_cdesc_base *p, uint32_t val)
{
	p->status |= (val << ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_SHIFT) & ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_MASK;
}

static inline uint32_t get_ena_eth_io_rx_cdesc_base_phase(const struct ena_eth_io_rx_cdesc_base *p)
{
	return (p->status & ENA_ETH_IO_RX_CDESC_BASE_PHASE_MASK) >> ENA_ETH_IO_RX_CDESC_BASE_PHASE_SHIFT;
}

static inline void set_ena_eth_io_rx_cdesc_base_phase(struct ena_eth_io_rx_cdesc_base *p, uint32_t val)
{
	p->status |= (val << ENA_ETH_IO_RX_CDESC_BASE_PHASE_SHIFT) & ENA_ETH_IO_RX_CDESC_BASE_PHASE_MASK;
}

static inline uint32_t get_ena_eth_io_rx_cdesc_base_l3_csum2(const struct ena_eth_io_rx_cdesc_base *p)
{
	return (p->status & ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM2_MASK) >> ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM2_SHIFT;
}

static inline void set_ena_eth_io_rx_cdesc_base_l3_csum2(struct ena_eth_io_rx_cdesc_base *p, uint32_t val)
{
	p->status |= (val << ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM2_SHIFT) & ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM2_MASK;
}

static inline uint32_t get_ena_eth_io_rx_cdesc_base_first(const struct ena_eth_io_rx_cdesc_base *p)
{
	return (p->status & ENA_ETH_IO_RX_CDESC_BASE_FIRST_MASK) >> ENA_ETH_IO_RX_CDESC_BASE_FIRST_SHIFT;
}

static inline void set_ena_eth_io_rx_cdesc_base_first(struct ena_eth_io_rx_cdesc_base *p, uint32_t val)
{
	p->status |= (val << ENA_ETH_IO_RX_CDESC_BASE_FIRST_SHIFT) & ENA_ETH_IO_RX_CDESC_BASE_FIRST_MASK;
}

static inline uint32_t get_ena_eth_io_rx_cdesc_base_last(const struct ena_eth_io_rx_cdesc_base *p)
{
	return (p->status & ENA_ETH_IO_RX_CDESC_BASE_LAST_MASK) >> ENA_ETH_IO_RX_CDESC_BASE_LAST_SHIFT;
}

static inline void set_ena_eth_io_rx_cdesc_base_last(struct ena_eth_io_rx_cdesc_base *p, uint32_t val)
{
	p->status |= (val << ENA_ETH_IO_RX_CDESC_BASE_LAST_SHIFT) & ENA_ETH_IO_RX_CDESC_BASE_LAST_MASK;
}

static inline uint32_t get_ena_eth_io_rx_cdesc_base_buffer(const struct ena_eth_io_rx_cdesc_base *p)
{
	return (p->status & ENA_ETH_IO_RX_CDESC_BASE_BUFFER_MASK) >> ENA_ETH_IO_RX_CDESC_BASE_BUFFER_SHIFT;
}

static inline void set_ena_eth_io_rx_cdesc_base_buffer(struct ena_eth_io_rx_cdesc_base *p, uint32_t val)
{
	p->status |= (val << ENA_ETH_IO_RX_CDESC_BASE_BUFFER_SHIFT) & ENA_ETH_IO_RX_CDESC_BASE_BUFFER_MASK;
}

static inline uint32_t get_ena_eth_io_intr_reg_rx_intr_delay(const struct ena_eth_io_intr_reg *p)
{
	return p->intr_control & ENA_ETH_IO_INTR_REG_RX_INTR_DELAY_MASK;
}

static inline void set_ena_eth_io_intr_reg_rx_intr_delay(struct ena_eth_io_intr_reg *p, uint32_t val)
{
	p->intr_control |= val & ENA_ETH_IO_INTR_REG_RX_INTR_DELAY_MASK;
}

static inline uint32_t get_ena_eth_io_intr_reg_tx_intr_delay(const struct ena_eth_io_intr_reg *p)
{
	return (p->intr_control & ENA_ETH_IO_INTR_REG_TX_INTR_DELAY_MASK) >> ENA_ETH_IO_INTR_REG_TX_INTR_DELAY_SHIFT;
}

static inline void set_ena_eth_io_intr_reg_tx_intr_delay(struct ena_eth_io_intr_reg *p, uint32_t val)
{
	p->intr_control |= (val << ENA_ETH_IO_INTR_REG_TX_INTR_DELAY_SHIFT) & ENA_ETH_IO_INTR_REG_TX_INTR_DELAY_MASK;
}

static inline uint32_t get_ena_eth_io_intr_reg_intr_unmask(const struct ena_eth_io_intr_reg *p)
{
	return (p->intr_control & ENA_ETH_IO_INTR_REG_INTR_UNMASK_MASK) >> ENA_ETH_IO_INTR_REG_INTR_UNMASK_SHIFT;
}

static inline void set_ena_eth_io_intr_reg_intr_unmask(struct ena_eth_io_intr_reg *p, uint32_t val)
{
	p->intr_control |= (val << ENA_ETH_IO_INTR_REG_INTR_UNMASK_SHIFT) & ENA_ETH_IO_INTR_REG_INTR_UNMASK_MASK;
}

static inline uint32_t get_ena_eth_io_numa_node_cfg_reg_numa(const struct ena_eth_io_numa_node_cfg_reg *p)
{
	return p->numa_cfg & ENA_ETH_IO_NUMA_NODE_CFG_REG_NUMA_MASK;
}

static inline void set_ena_eth_io_numa_node_cfg_reg_numa(struct ena_eth_io_numa_node_cfg_reg *p, uint32_t val)
{
	p->numa_cfg |= val & ENA_ETH_IO_NUMA_NODE_CFG_REG_NUMA_MASK;
}

static inline uint32_t get_ena_eth_io_numa_node_cfg_reg_enabled(const struct ena_eth_io_numa_node_cfg_reg *p)
{
	return (p->numa_cfg & ENA_ETH_IO_NUMA_NODE_CFG_REG_ENABLED_MASK) >> ENA_ETH_IO_NUMA_NODE_CFG_REG_ENABLED_SHIFT;
}

static inline void set_ena_eth_io_numa_node_cfg_reg_enabled(struct ena_eth_io_numa_node_cfg_reg *p, uint32_t val)
{
	p->numa_cfg |= (val << ENA_ETH_IO_NUMA_NODE_CFG_REG_ENABLED_SHIFT) & ENA_ETH_IO_NUMA_NODE_CFG_REG_ENABLED_MASK;
}

#endif /* !defined(ENA_DEFS_LINUX_MAINLINE) */
#endif /*_ENA_ETH_IO_H_ */
