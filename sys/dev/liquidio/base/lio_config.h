/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

/*  \file  lio_config.h
 *  \brief Host Driver: Configuration data structures for the host driver.
 */

#ifndef __LIO_CONFIG_H__
#define __LIO_CONFIG_H__

/*--------------------------CONFIG VALUES------------------------*/

/*
 * The following macros affect the way the driver data structures
 * are generated for Octeon devices.
 * They can be modified.
 */

/*
 * Maximum octeon devices defined as LIO_MAX_IF to support
 * multiple(<= LIO_MAX_IF) Miniports
 */
#define LIO_MAX_IF			128
#define LIO_MAX_DEVICES			LIO_MAX_IF
#define LIO_MAX_MULTICAST_ADDR		32

/* CN23xx IQ configuration macros */
#define LIO_CN23XX_PF_MAX_RINGS		64

#define LIO_BR_SIZE			4096

#define LIO_CN23XX_PF_MAX_INPUT_QUEUES		LIO_CN23XX_PF_MAX_RINGS
#define LIO_CN23XX_MAX_IQ_DESCRIPTORS		2048
#define LIO_CN23XX_DEFAULT_IQ_DESCRIPTORS	512
#define LIO_CN23XX_MIN_IQ_DESCRIPTORS		128
#define LIO_CN23XX_DB_MIN			1
#define LIO_CN23XX_DB_TIMEOUT			1

#define LIO_CN23XX_PF_MAX_OUTPUT_QUEUES		LIO_CN23XX_PF_MAX_RINGS
#define LIO_CN23XX_MAX_OQ_DESCRIPTORS		2048
#define LIO_CN23XX_DEFAULT_OQ_DESCRIPTORS	512
#define LIO_CN23XX_MIN_OQ_DESCRIPTORS		128
#define LIO_CN23XX_OQ_BUF_SIZE			MCLBYTES
#define LIO_CN23XX_OQ_PKTS_PER_INTR		128
#define LIO_CN23XX_OQ_REFIL_THRESHOLD		16

#define LIO_CN23XX_OQ_INTR_PKT			64
#define LIO_CN23XX_OQ_INTR_TIME			100
#define LIO_CN23XX_DEFAULT_NUM_PORTS		1

#define LIO_CN23XX_CFG_IO_QUEUES		LIO_CN23XX_PF_MAX_RINGS

#define LIO_CN23XX_DEF_IQ_INTR_THRESHOLD	32
#define LIO_CN23XX_PKI_MAX_FRAME_SIZE		65535
#define LIO_CN23XX_RAW_FRONT_SIZE		48
/*
 * this is the max jabber value.Any packets greater than this size sent over
 * DPI will be truncated.
 */
#define LIO_CN23XX_MAX_INPUT_JABBER  (LIO_CN23XX_PKI_MAX_FRAME_SIZE - \
				      LIO_CN23XX_RAW_FRONT_SIZE)

/* common OCTEON configuration macros */
#define LIO_64BYTE_INSTR		64

#define LIO_MAX_TXQS_PER_INTF		8
#define LIO_MAX_RXQS_PER_INTF		8
#define LIO_DEF_TXQS_PER_INTF		4
#define LIO_DEF_RXQS_PER_INTF		4

/* Macros to get octeon config params */
#define LIO_GET_IQ_CFG(cfg)			((cfg)->iq)
#define LIO_GET_IQ_MAX_Q_CFG(cfg)		((cfg)->iq.max_iqs)
#define LIO_GET_IQ_INSTR_TYPE_CFG(cfg)		((cfg)->iq.instr_type)

#define LIO_GET_IQ_INTR_PKT_CFG(cfg)		((cfg)->iq.iq_intr_pkt)

#define LIO_GET_OQ_MAX_Q_CFG(cfg)		((cfg)->oq.max_oqs)
#define LIO_GET_OQ_PKTS_PER_INTR_CFG(cfg)	((cfg)->oq.pkts_per_intr)
#define LIO_GET_OQ_REFILL_THRESHOLD_CFG(cfg)	((cfg)->oq.refill_threshold)
#define LIO_GET_OQ_INTR_PKT_CFG(cfg)		((cfg)->oq.oq_intr_pkt)
#define LIO_GET_OQ_INTR_TIME_CFG(cfg)		((cfg)->oq.oq_intr_time)

#define LIO_GET_NUM_NIC_PORTS_CFG(cfg)		((cfg)->num_nic_ports)
#define LIO_GET_NUM_DEF_TX_DESCS_CFG(cfg)	((cfg)->num_def_tx_descs)
#define LIO_GET_NUM_DEF_RX_DESCS_CFG(cfg)	((cfg)->num_def_rx_descs)
#define LIO_GET_DEF_RX_BUF_SIZE_CFG(cfg)	((cfg)->def_rx_buf_size)

#define LIO_GET_NUM_RX_DESCS_NIC_IF_CFG(cfg, idx)	\
		((cfg)->nic_if_cfg[idx].num_rx_descs)
#define LIO_GET_NUM_TX_DESCS_NIC_IF_CFG(cfg, idx)	\
		((cfg)->nic_if_cfg[idx].num_tx_descs)
#define LIO_GET_NUM_RX_BUF_SIZE_NIC_IF_CFG(cfg, idx)	\
		((cfg)->nic_if_cfg[idx].rx_buf_size)

#define LIO_GET_IS_SLI_BP_ON_CFG(cfg)	((cfg)->misc.enable_sli_oq_bp)

/* Max IOQs per OCTEON Link */
#define LIO_MAX_IOQS_PER_NICIF			64

#define LIO_SET_NUM_RX_DESCS_NIC_IF(cfg, idx, value)		\
		((cfg)->nic_if_cfg[idx].num_rx_descs = value)
#define LIO_SET_NUM_TX_DESCS_NIC_IF(cfg, idx, value)		\
		((cfg)->nic_if_cfg[idx].num_tx_descs = value)

/* TX/RX process pkt budget */
#define LIO_DEFAULT_TX_PKTS_PROCESS_BUDGET	64
#define LIO_DEFAULT_RX_PKTS_PROCESS_BUDGET	64

enum lio_card_type {
	LIO_23XX	/* 23xx */
};

#define LIO_23XX_NAME  "23xx"

/*
 *  Structure to define the configuration attributes for each Input queue.
 *  Applicable to all Octeon processors
 */
struct lio_iq_config {
#if BYTE_ORDER == BIG_ENDIAN
	uint64_t	reserved:16;

	/* Tx interrupt packets. Applicable to 23xx only */
	uint64_t	iq_intr_pkt:16;

	/* Minimum ticks to wait before checking for pending instructions. */
	uint64_t	db_timeout:16;

	/*
	 *  Minimum number of commands pending to be posted to Octeon
	 *  before driver hits the Input queue doorbell.
	 */
	uint64_t	db_min:8;

	/* Command size - 32 or 64 bytes */
	uint64_t	instr_type:32;

	/*
	 *  Pending list size (usually set to the sum of the size of all Input
	 *  queues)
	 */
	uint64_t	pending_list_size:32;

	/* Max number of IQs available */
	uint64_t	max_iqs:8;

#else	/* BYTE_ORDER != BIG_ENDIAN */

	/* Max number of IQs available */
	uint64_t	max_iqs:8;

	/*
	 *  Pending list size (usually set to the sum of the size of all Input
	 *  queues)
	 */
	uint64_t	pending_list_size:32;

	/* Command size - 32 or 64 bytes */
	uint64_t	instr_type:32;

	/*
	 *  Minimum number of commands pending to be posted to Octeon
	 *  before driver hits the Input queue doorbell.
	 */
	uint64_t	db_min:8;

	/* Minimum ticks to wait before checking for pending instructions. */
	uint64_t	db_timeout:16;

	/* Tx interrupt packets. Applicable to 23xx only */
	uint64_t	iq_intr_pkt:16;

	uint64_t	reserved:16;

#endif	/* BYTE_ORDER == BIG_ENDIAN */
};

/*
 *  Structure to define the configuration attributes for each Output queue.
 *  Applicable to all Octeon processors
 */
struct lio_oq_config {
#if BYTE_ORDER == BIG_ENDIAN
	uint64_t	reserved:16;

	uint64_t	pkts_per_intr:16;

	/*
	 *  Interrupt Coalescing (Time Interval). Octeon will interrupt the
	 *  host if atleast one packet was sent in the time interval specified
	 *  by this field. The driver uses time interval interrupt coalescing
	 *  by default. The time is specified in microseconds.
	 */
	uint64_t	oq_intr_time:16;

	/*
	 *  Interrupt Coalescing (Packet Count). Octeon will interrupt the host
	 *  only if it sent as many packets as specified by this field.
	 *  The driver
	 *  usually does not use packet count interrupt coalescing.
	 */
	uint64_t	oq_intr_pkt:16;

	/*
	 *   The number of buffers that were consumed during packet processing by
	 *   the driver on this Output queue before the driver attempts to
	 *   replenish
	 *   the descriptor ring with new buffers.
	 */
	uint64_t	refill_threshold:16;

	/* Max number of OQs available */
	uint64_t	max_oqs:8;

#else	/* BYTE_ORDER != BIG_ENDIAN */

	/* Max number of OQs available */
	uint64_t	max_oqs:8;

	/*
	 *   The number of buffers that were consumed during packet processing by
	 *   the driver on this Output queue before the driver attempts to
	 *   replenish
	 *   the descriptor ring with new buffers.
	 */
	uint64_t	refill_threshold:16;

	/*
	 *  Interrupt Coalescing (Packet Count). Octeon will interrupt the host
	 *  only if it sent as many packets as specified by this field.
	 *  The driver
	 *  usually does not use packet count interrupt coalescing.
	 */
	uint64_t	oq_intr_pkt:16;

	/*
	 *  Interrupt Coalescing (Time Interval). Octeon will interrupt the
	 *  host if atleast one packet was sent in the time interval specified
	 *  by this field. The driver uses time interval interrupt coalescing
	 *  by default.  The time is specified in microseconds.
	 */
	uint64_t	oq_intr_time:16;

	uint64_t	pkts_per_intr:16;

	uint64_t	reserved:16;
#endif	/* BYTE_ORDER == BIG_ENDIAN */

};

/*
 *  This structure conatins the NIC link configuration attributes,
 *  common for all the OCTEON Modles.
 */
struct lio_nic_if_config {
#if BYTE_ORDER == BIG_ENDIAN
	uint64_t	reserved:56;

	uint64_t	base_queue:16;

	uint64_t	gmx_port_id:8;

	/*
	 * mbuf size, We need not change buf size even for Jumbo frames.
	 * Octeon can send jumbo frames in 4 consecutive descriptors,
	 */
	uint64_t	rx_buf_size:16;

	/* Num of desc for tx rings */
	uint64_t	num_tx_descs:16;

	/* Num of desc for rx rings */
	uint64_t	num_rx_descs:16;

	/* Actual configured value. Range could be: 1...max_rxqs */
	uint64_t	num_rxqs:16;

	/* Max Rxqs: Half for each of the two ports :max_oq/2  */
	uint64_t	max_rxqs:16;

	/* Actual configured value. Range could be: 1...max_txqs */
	uint64_t	num_txqs:16;

	/* Max Txqs: Half for each of the two ports :max_iq/2 */
	uint64_t	max_txqs:16;

#else	/* BYTE_ORDER != BIG_ENDIAN */

	/* Max Txqs: Half for each of the two ports :max_iq/2 */
	uint64_t	max_txqs:16;

	/* Actual configured value. Range could be: 1...max_txqs */
	uint64_t	num_txqs:16;

	/* Max Rxqs: Half for each of the two ports :max_oq/2  */
	uint64_t	max_rxqs:16;

	/* Actual configured value. Range could be: 1...max_rxqs */
	uint64_t	num_rxqs:16;

	/* Num of desc for rx rings */
	uint64_t	num_rx_descs:16;

	/* Num of desc for tx rings */
	uint64_t	num_tx_descs:16;

	/*
	 * mbuf size, We need not change buf size even for Jumbo frames.
	 * Octeon can send jumbo frames in 4 consecutive descriptors,
	 */
	uint64_t	rx_buf_size:16;

	uint64_t	gmx_port_id:8;

	uint64_t	base_queue:16;

	uint64_t	reserved:56;
#endif	/* BYTE_ORDER == BIG_ENDIAN */

};

/*
 *  Structure to define the configuration attributes for meta data.
 *  Applicable to all Octeon processors.
 */

struct lio_misc_config {
#if BYTE_ORDER == BIG_ENDIAN
	/* Host link status polling period */
	uint64_t	host_link_query_interval:32;
	/* Oct link status polling period */
	uint64_t	oct_link_query_interval:32;

	uint64_t	enable_sli_oq_bp:1;
	/* Control IQ Group */
	uint64_t	ctrlq_grp:4;

#else	/* BYTE_ORDER != BIG_ENDIAN */

	/* Control IQ Group */
	uint64_t	ctrlq_grp:4;
	/* BP for SLI OQ */
	uint64_t	enable_sli_oq_bp:1;
	/* Host link status polling period */
	uint64_t	oct_link_query_interval:32;
	/* Oct link status polling period */
	uint64_t	host_link_query_interval:32;

#endif	/* BYTE_ORDER == BIG_ENDIAN */
};

/* Structure to define the configuration for all OCTEON processors. */
struct lio_config {
	uint16_t	card_type;
	char		*card_name;

	/* Input Queue attributes. */
	struct lio_iq_config iq;

	/* Output Queue attributes. */
	struct lio_oq_config oq;

	/* NIC Port Configuration */
	struct lio_nic_if_config nic_if_cfg[LIO_MAX_IF];

	/* Miscellaneous attributes */
	struct lio_misc_config	misc;

	int		num_nic_ports;

	int		num_def_tx_descs;

	/* Num of desc for rx rings */
	int		num_def_rx_descs;

	int		def_rx_buf_size;

};

/* The following config values are fixed and should not be modified. */
/* Maximum address space to be mapped for Octeon's BAR1 index-based access. */
#define LIO_MAX_BAR1_MAP_INDEX		2

/*
 * Response lists - 1 ordered, 1 unordered-blocking, 1 unordered-nonblocking
 * NoResponse Lists are now maintained with each IQ. (Dec' 2007).
 */
#define LIO_MAX_RESPONSE_LISTS		4

/*
 * Opcode hash bits. The opcode is hashed on the lower 6-bits to lookup the
 * dispatch table.
 */
#define LIO_OPCODE_MASK_BITS		6

/* Mask for the 6-bit lookup hash */
#define LIO_OPCODE_MASK			0x3f

/* Size of the dispatch table. The 6-bit hash can index into 2^6 entries */
#define LIO_DISPATCH_LIST_SIZE		BIT(LIO_OPCODE_MASK_BITS)

#define LIO_MAX_INSTR_QUEUES(oct)	LIO_CN23XX_PF_MAX_INPUT_QUEUES
#define LIO_MAX_OUTPUT_QUEUES(oct)	LIO_CN23XX_PF_MAX_OUTPUT_QUEUES

#define LIO_MAX_POSSIBLE_INSTR_QUEUES	LIO_CN23XX_PF_MAX_INPUT_QUEUES
#define LIO_MAX_POSSIBLE_OUTPUT_QUEUES	LIO_CN23XX_PF_MAX_OUTPUT_QUEUES
#endif	/* __LIO_CONFIG_H__  */
