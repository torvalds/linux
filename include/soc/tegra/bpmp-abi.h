/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2022, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef ABI_BPMP_ABI_H
#define ABI_BPMP_ABI_H

#if defined(LK) || defined(BPMP_ABI_HAVE_STDC)
#include <stddef.h>
#include <stdint.h>
#endif

#ifndef BPMP_ABI_PACKED
#ifdef __ABI_PACKED
#define BPMP_ABI_PACKED __ABI_PACKED
#else
#define BPMP_ABI_PACKED __attribute__((packed))
#endif
#endif

#ifdef NO_GCC_EXTENSIONS
#define BPMP_ABI_EMPTY char empty;
#define BPMP_ABI_EMPTY_ARRAY 1
#else
#define BPMP_ABI_EMPTY
#define BPMP_ABI_EMPTY_ARRAY 0
#endif

#ifndef BPMP_UNION_ANON
#ifdef __UNION_ANON
#define BPMP_UNION_ANON __UNION_ANON
#else
#define BPMP_UNION_ANON
#endif
#endif

/**
 * @file
 */

/**
 * @defgroup MRQ MRQ Messages
 * @brief Messages sent to/from BPMP via IPC
 * @{
 *   @defgroup MRQ_Format Message Format
 *   @defgroup MRQ_Codes Message Request (MRQ) Codes
 *   @defgroup MRQ_Payloads Message Payloads
 *   @defgroup Error_Codes Error Codes
 * @}
 */

/**
 * @addtogroup MRQ_Format
 * @{
 * The CPU requests the BPMP to perform a particular service by
 * sending it an IVC frame containing a single MRQ message. An MRQ
 * message consists of a @ref mrq_request followed by a payload whose
 * format depends on mrq_request::mrq.
 *
 * The BPMP processes the data and replies with an IVC frame (on the
 * same IVC channel) containing and MRQ response. An MRQ response
 * consists of a @ref mrq_response followed by a payload whose format
 * depends on the associated mrq_request::mrq.
 *
 * A well-defined subset of the MRQ messages that the CPU sends to the
 * BPMP can lead to BPMP eventually sending an MRQ message to the
 * CPU. For example, when the CPU uses an #MRQ_THERMAL message to set
 * a thermal trip point, the BPMP may eventually send a single
 * #MRQ_THERMAL message of its own to the CPU indicating that the trip
 * point has been crossed.
 * @}
 */

/**
 * @ingroup MRQ_Format
 * Request an answer from the peer.
 * This should be set in mrq_request::flags for all requests targetted
 * at BPMP. For requests originating in BPMP, this flag is optional except
 * for messages targeting MCE, for which the field must be set.
 * When this flag is not set, the remote peer must not send a response
 * back.
 */
#define BPMP_MAIL_DO_ACK	(1U << 0U)

/**
 * @ingroup MRQ_Format
 * Ring the sender's doorbell when responding. This should be set unless
 * the sender wants to poll the underlying communications layer directly.
 *
 * An optional direction that can be specified in mrq_request::flags.
 */
#define BPMP_MAIL_RING_DB	(1U << 1U)

/**
 * @ingroup MRQ_Format
 * CRC present
 */
#define BPMP_MAIL_CRC_PRESENT	(1U << 2U)

/**
 * @ingroup MRQ_Format
 * @brief Header for an MRQ message
 *
 * Provides the MRQ number for the MRQ message: #mrq. The remainder of
 * the MRQ message is a payload (immediately following the
 * mrq_request) whose format depends on mrq.
 */
struct mrq_request {
	/** @brief MRQ number of the request */
	uint32_t mrq;

	/**
	 * @brief 32bit word containing a number of fields as follows:
	 *
	 * 	struct {
	 * 		uint8_t options:4;
	 * 		uint8_t xid:4;
	 * 		uint8_t payload_length;
	 * 		uint16_t crc16;
	 * 	};
	 *
	 * **options** directions to the receiver and indicates CRC presence.
	 *
	 * #BPMP_MAIL_DO_ACK and  #BPMP_MAIL_RING_DB see documentation of respective options.
	 * #BPMP_MAIL_CRC_PRESENT is supported on T234 and later platforms. It indicates the
	 * crc16, xid and length fields are present when set.
	 * Some platform configurations, especially when targeted to applications requiring
	 * functional safety, mandate this option being set or otherwise will respond with
	 * -BPMP_EBADMSG and ignore the request.
	 *
	 * **xid** is a transaction ID.
	 *
	 * Only used when #BPMP_MAIL_CRC_PRESENT is set.
	 *
	 * **payload_length** of the message expressed in bytes without the size of this header.
	 * See table below for minimum accepted payload lengths for each MRQ.
	 * Note: For DMCE communication, this field expresses the length as a multiple of 4 bytes
	 * rather than bytes.
	 *
	 * Only used when #BPMP_MAIL_CRC_PRESENT is set.
	 *
	 * | MRQ                  | CMD                                  | minimum payload length
	 * | -------------------- | ------------------------------------ | ------------------------------------------ |
	 * | MRQ_PING             |                                      | 4                                          |
	 * | MRQ_THREADED_PING    |                                      | 4                                          |
	 * | MRQ_RESET            | any                                  | 8                                          |
	 * | MRQ_I2C              |                                      | 12 + cmd_i2c_xfer_request.data_size        |
	 * | MRQ_CLK              | CMD_CLK_GET_RATE                     | 4                                          |
	 * | MRQ_CLK              | CMD_CLK_SET_RATE                     | 16                                         |
	 * | MRQ_CLK              | CMD_CLK_ROUND_RATE                   | 16                                         |
	 * | MRQ_CLK              | CMD_CLK_GET_PARENT                   | 4                                          |
	 * | MRQ_CLK              | CMD_CLK_SET_PARENT                   | 8                                          |
	 * | MRQ_CLK              | CMD_CLK_ENABLE                       | 4                                          |
	 * | MRQ_CLK              | CMD_CLK_DISABLE                      | 4                                          |
	 * | MRQ_CLK              | CMD_CLK_IS_ENABLED                   | 4                                          |
	 * | MRQ_CLK              | CMD_CLK_GET_ALL_INFO                 | 4                                          |
	 * | MRQ_CLK              | CMD_CLK_GET_MAX_CLK_ID               | 4                                          |
	 * | MRQ_CLK              | CMD_CLK_GET_FMAX_AT_VMIN             | 4                                          |
	 * | MRQ_QUERY_ABI        |                                      | 4                                          |
	 * | MRQ_PG               | CMD_PG_QUERY_ABI                     | 12                                         |
	 * | MRQ_PG               | CMD_PG_SET_STATE                     | 12                                         |
	 * | MRQ_PG               | CMD_PG_GET_STATE                     | 8                                          |
	 * | MRQ_PG               | CMD_PG_GET_NAME                      | 8                                          |
	 * | MRQ_PG               | CMD_PG_GET_MAX_ID                    | 8                                          |
	 * | MRQ_THERMAL          | CMD_THERMAL_QUERY_ABI                | 8                                          |
	 * | MRQ_THERMAL          | CMD_THERMAL_GET_TEMP                 | 8                                          |
	 * | MRQ_THERMAL          | CMD_THERMAL_SET_TRIP                 | 20                                         |
	 * | MRQ_THERMAL          | CMD_THERMAL_GET_NUM_ZONES            | 4                                          |
	 * | MRQ_THERMAL          | CMD_THERMAL_GET_THERMTRIP            | 8                                          |
	 * | MRQ_CPU_VHINT        |                                      | 8                                          |
	 * | MRQ_ABI_RATCHET      |                                      | 2                                          |
	 * | MRQ_EMC_DVFS_LATENCY |                                      | 8                                          |
	 * | MRQ_EMC_DVFS_EMCHUB  |                                      | 8                                          |
	 * | MRQ_EMC_DISP_RFL     |                                      | 4                                          |
	 * | MRQ_BWMGR            | CMD_BWMGR_QUERY_ABI                  | 8                                          |
	 * | MRQ_BWMGR            | CMD_BWMGR_CALC_RATE                  | 8 + 8 * bwmgr_rate_req.num_iso_clients     |
	 * | MRQ_ISO_CLIENT       | CMD_ISO_CLIENT_QUERY_ABI             | 8                                          |
	 * | MRQ_ISO_CLIENT       | CMD_ISO_CLIENT_CALCULATE_LA          | 16                                         |
	 * | MRQ_ISO_CLIENT       | CMD_ISO_CLIENT_SET_LA                | 16                                         |
	 * | MRQ_ISO_CLIENT       | CMD_ISO_CLIENT_GET_MAX_BW            | 8                                          |
	 * | MRQ_CPU_NDIV_LIMITS  |                                      | 4                                          |
	 * | MRQ_CPU_AUTO_CC3     |                                      | 4                                          |
	 * | MRQ_RINGBUF_CONSOLE  | CMD_RINGBUF_CONSOLE_QUERY_ABI        | 8                                          |
	 * | MRQ_RINGBUF_CONSOLE  | CMD_RINGBUF_CONSOLE_READ             | 5                                          |
	 * | MRQ_RINGBUF_CONSOLE  | CMD_RINGBUF_CONSOLE_WRITE            | 5 + cmd_ringbuf_console_write_req.len      |
	 * | MRQ_RINGBUF_CONSOLE  | CMD_RINGBUF_CONSOLE_GET_FIFO         | 4                                          |
	 * | MRQ_STRAP            | STRAP_SET                            | 12                                         |
	 * | MRQ_UPHY             | CMD_UPHY_PCIE_LANE_MARGIN_CONTROL    | 24                                         |
	 * | MRQ_UPHY             | CMD_UPHY_PCIE_LANE_MARGIN_STATUS     | 4                                          |
	 * | MRQ_UPHY             | CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT | 5                                          |
	 * | MRQ_UPHY             | CMD_UPHY_PCIE_CONTROLLER_STATE       | 6                                          |
	 * | MRQ_UPHY             | CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF  | 5                                          |
	 * | MRQ_FMON             | CMD_FMON_GEAR_CLAMP                  | 16                                         |
	 * | MRQ_FMON             | CMD_FMON_GEAR_FREE                   | 4                                          |
	 * | MRQ_FMON             | CMD_FMON_GEAR_GET                    | 4                                          |
	 * | MRQ_FMON             | CMD_FMON_FAULT_STS_GET               | 8                                          |
	 * | MRQ_EC               | CMD_EC_STATUS_EX_GET                 | 12                                         |
	 * | MRQ_QUERY_FW_TAG     |                                      | 0                                          |
	 * | MRQ_DEBUG            | CMD_DEBUG_OPEN_RO                    | 4 + length of cmd_debug_fopen_request.name |
	 * | MRQ_DEBUG            | CMD_DEBUG_OPEN_WO                    | 4 + length of cmd_debug_fopen_request.name |
	 * | MRQ_DEBUG            | CMD_DEBUG_READ                       | 8                                          |
	 * | MRQ_DEBUG            | CMD_DEBUG_WRITE                      | 12 + cmd_debug_fwrite_request.datalen      |
	 * | MRQ_DEBUG            | CMD_DEBUG_CLOSE                      | 8                                          |
	 * | MRQ_TELEMETRY        |                                      | 8                                          |
	 * | MRQ_PWR_LIMIT        | CMD_PWR_LIMIT_QUERY_ABI              | 8                                          |
	 * | MRQ_PWR_LIMIT        | CMD_PWR_LIMIT_SET                    | 20                                         |
	 * | MRQ_PWR_LIMIT        | CMD_PWR_LIMIT_GET                    | 16                                         |
	 * | MRQ_PWR_LIMIT        | CMD_PWR_LIMIT_CURR_CAP               | 8                                          |
	 * | MRQ_GEARS            |                                      | 0                                          |
	 * | MRQ_BWMGR_INT        | CMD_BWMGR_INT_QUERY_ABI              | 8                                          |
	 * | MRQ_BWMGR_INT        | CMD_BWMGR_INT_CALC_AND_SET           | 16                                         |
	 * | MRQ_BWMGR_INT        | CMD_BWMGR_INT_CAP_SET                | 8                                          |
	 * | MRQ_OC_STATUS        |                                      | 0                                          |
	 *
	 * **crc16**
	 *
	 * CRC16 using polynomial x^16 + x^14 + x^12 + x^11 + x^8 + x^5 + x^4 + x^2 + 1
	 * and initialization value 0x4657. The CRC is calculated over all bytes of the message
	 * including this header. However the crc16 field is considered to be set to 0 when
	 * calculating the CRC. Only used when #BPMP_MAIL_CRC_PRESENT is set. If
	 * #BPMP_MAIL_CRC_PRESENT is set and this field does not match the CRC as
	 * calculated by BPMP, -BPMP_EBADMSG will be returned and the request will
	 * be ignored. See code snippet below on how to calculate the CRC.
	 *
	 * @code
	 *	uint16_t calc_crc_digest(uint16_t crc, uint8_t *data, size_t size)
	 *	{
	 *		for (size_t i = 0; i < size; i++) {
	 *			crc ^= data[i] << 8;
	 *			for (size_t j = 0; j < 8; j++) {
	 *				if ((crc & 0x8000) == 0x8000) {
	 *					crc = (crc << 1) ^ 0xAC9A;
	 *				} else {
	 *					crc = (crc << 1);
	 *				}
	 *			}
	 *		}
	 *		return crc;
	 *	}
	 *
	 *	uint16_t calc_crc(uint8_t *data, size_t size)
	 *	{
	 *		return calc_crc_digest(0x4657, data, size);
	 *	}
	 * @endcode
	 */
	uint32_t flags;
} BPMP_ABI_PACKED;

/**
 * @ingroup MRQ_Format
 * @brief Header for an MRQ response
 *
 *  Provides an error code for the associated MRQ message. The
 *  remainder of the MRQ response is a payload (immediately following
 *  the mrq_response) whose format depends on the associated
 *  mrq_request::mrq
 */
struct mrq_response {
	/** @brief Error code for the MRQ request itself */
	int32_t err;

	/**
	 * @brief 32bit word containing a number of fields as follows:
	 *
	 * 	struct {
	 * 		uint8_t options:4;
	 * 		uint8_t xid:4;
	 * 		uint8_t payload_length;
	 * 		uint16_t crc16;
	 * 	};
	 *
	 * **options** indicates CRC presence.
	 *
	 * #BPMP_MAIL_CRC_PRESENT is supported on T234 and later platforms and
	 * indicates the crc16 related fields are present when set.
	 *
	 * **xid** is the transaction ID as sent by the requestor.
	 *
	 * **length** of the message expressed in bytes without the size of this header.
	 * Note: For DMCE communication, this field expresses the length as a multiple of 4 bytes
	 * rather than bytes.
	 *
	 * **crc16**
	 *
	 * CRC16 using polynomial x^16 + x^14 + x^12 + x^11 + x^8 + x^5 + x^4 + x^2 + 1
	 * and initialization value 0x4657. The CRC is calculated over all bytes of the message
	 * including this header. However the crc16 field is considered to be set to 0 when
	 * calculating the CRC. Only used when #BPMP_MAIL_CRC_PRESENT is set.
	 */
	uint32_t flags;
} BPMP_ABI_PACKED;

/**
 * @ingroup MRQ_Format
 * Minimum needed size for an IPC message buffer
 */
#define MSG_MIN_SZ	128U
/**
 * @ingroup MRQ_Format
 *  Minimum size guaranteed for data in an IPC message buffer
 */
#define MSG_DATA_MIN_SZ	120U

/**
 * @ingroup MRQ_Codes
 * @name Legal MRQ codes
 * These are the legal values for mrq_request::mrq
 * @{
 */

#define MRQ_PING		0U
#define MRQ_QUERY_TAG		1U
#define MRQ_THREADED_PING	9U
#define MRQ_DEBUGFS		19U
#define MRQ_RESET		20U
#define MRQ_I2C			21U
#define MRQ_CLK			22U
#define MRQ_QUERY_ABI		23U
#define MRQ_THERMAL		27U
#define MRQ_CPU_VHINT		28U
#define MRQ_ABI_RATCHET		29U
#define MRQ_EMC_DVFS_LATENCY	31U
#define MRQ_RINGBUF_CONSOLE	65U
#define MRQ_PG			66U
#define MRQ_CPU_NDIV_LIMITS	67U
#define MRQ_STRAP               68U
#define MRQ_UPHY		69U
#define MRQ_CPU_AUTO_CC3	70U
#define MRQ_QUERY_FW_TAG	71U
#define MRQ_FMON		72U
#define MRQ_EC			73U
#define MRQ_DEBUG		75U
#define MRQ_EMC_DVFS_EMCHUB	76U
#define MRQ_BWMGR		77U
#define MRQ_ISO_CLIENT		78U
#define MRQ_EMC_DISP_RFL	79U
#define MRQ_TELEMETRY		80U
#define MRQ_PWR_LIMIT		81U
#define MRQ_GEARS		82U
#define MRQ_BWMGR_INT		83U
#define MRQ_OC_STATUS		84U

/** @cond DEPRECATED */
#define MRQ_RESERVED_2		2U
#define MRQ_RESERVED_3		3U
#define MRQ_RESERVED_4		4U
#define MRQ_RESERVED_5   	5U
#define MRQ_RESERVED_6		6U
#define MRQ_RESERVED_7		7U
#define MRQ_RESERVED_8		8U
#define MRQ_RESERVED_10		10U
#define MRQ_RESERVED_11		11U
#define MRQ_RESERVED_12		12U
#define MRQ_RESERVED_13		13U
#define MRQ_RESERVED_14		14U
#define MRQ_RESERVED_15		15U
#define MRQ_RESERVED_16		16U
#define MRQ_RESERVED_17		17U
#define MRQ_RESERVED_18		18U
#define MRQ_RESERVED_24		24U
#define MRQ_RESERVED_25		25U
#define MRQ_RESERVED_26		26U
#define MRQ_RESERVED_30		30U
#define MRQ_RESERVED_64		64U
#define MRQ_RESERVED_74		74U
/** @endcond DEPRECATED */

/** @} */

/**
 * @ingroup MRQ_Codes
 * @brief Maximum MRQ code to be sent by CPU software to
 * BPMP. Subject to change in future
 */
#define MAX_CPU_MRQ_ID		84U

/**
 * @addtogroup MRQ_Payloads
 * @{
 *   @defgroup Ping Ping
 *   @defgroup Query_Tag Query Tag
 *   @defgroup Module Loadable Modules
 *   @defgroup Trace Trace
 *   @defgroup Debugfs Debug File System
 *   @defgroup Reset Reset
 *   @defgroup I2C I2C
 *   @defgroup Clocks Clocks
 *   @defgroup ABI_info ABI Info
 *   @defgroup Powergating Power Gating
 *   @defgroup Thermal Thermal
 *   @defgroup OC_status OC status
 *   @defgroup Vhint CPU Voltage hint
 *   @defgroup EMC EMC
 *   @defgroup BWMGR BWMGR
 *   @defgroup ISO_CLIENT ISO_CLIENT
 *   @defgroup CPU NDIV Limits
 *   @defgroup RingbufConsole Ring Buffer Console
 *   @defgroup Strap Straps
 *   @defgroup UPHY UPHY
 *   @defgroup CC3 Auto-CC3
 *   @defgroup FMON FMON
 *   @defgroup EC EC
 *   @defgroup Telemetry Telemetry
 *   @defgroup Pwrlimit PWR_LIMIT
 *   @defgroup Gears Gears
 *   @defgroup BWMGR_INT Bandwidth Manager Integrated
 * @} MRQ_Payloads
 */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_PING
 * @brief A simple ping
 *
 * * Platforms: All
 * * Initiators: Any
 * * Targets: Any
 * * Request Payload: @ref mrq_ping_request
 * * Response Payload: @ref mrq_ping_response
 *
 * @ingroup MRQ_Codes
 * @def MRQ_THREADED_PING
 * @brief A deeper ping
 *
 * * Platforms: All
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_ping_request
 * * Response Payload: @ref mrq_ping_response
 *
 * Behavior is equivalent to a simple #MRQ_PING except that BPMP
 * responds from a thread context (providing a slightly more robust
 * sign of life).
 *
 */

/**
 * @ingroup Ping
 * @brief Request with #MRQ_PING
 *
 * Used by the sender of an #MRQ_PING message to request a pong from
 * recipient. The response from the recipient is computed based on
 * #challenge.
 */
struct mrq_ping_request {
/** @brief Arbitrarily chosen value */
	uint32_t challenge;
} BPMP_ABI_PACKED;

/**
 * @ingroup Ping
 * @brief Response to #MRQ_PING
 *
 * Sent in response to an #MRQ_PING message. #reply should be the
 * mrq_ping_request challenge left shifted by 1 with the carry-bit
 * dropped.
 *
 */
struct mrq_ping_response {
	/** @brief Response to the MRQ_PING challege */
	uint32_t reply;
} BPMP_ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_QUERY_TAG
 * @brief Query BPMP firmware's tag (i.e. unique identifer)
 *
 * @deprecated Use #MRQ_QUERY_FW_TAG instead.
 *
 * * Platforms: All
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_query_tag_request
 * * Response Payload: N/A
 *
 */

/**
 * @ingroup Query_Tag
 * @brief Request with #MRQ_QUERY_TAG
 *
 * @deprecated This structure will be removed in future version.
 * Use MRQ_QUERY_FW_TAG instead.
 */
struct mrq_query_tag_request {
  /** @brief Base address to store the firmware tag */
	uint32_t addr;
} BPMP_ABI_PACKED;


/**
 * @ingroup MRQ_Codes
 * @def MRQ_QUERY_FW_TAG
 * @brief Query BPMP firmware's tag (i.e. unique identifier)
 *
 * * Platforms: All
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: N/A
 * * Response Payload: @ref mrq_query_fw_tag_response
 *
 */

/**
 * @ingroup Query_Tag
 * @brief Response to #MRQ_QUERY_FW_TAG
 *
 * Sent in response to #MRQ_QUERY_FW_TAG message. #tag contains the unique
 * identifier for the version of firmware issuing the reply.
 *
 */
struct mrq_query_fw_tag_response {
  /** @brief Array to store tag information */
	uint8_t tag[32];
} BPMP_ABI_PACKED;

/** @private */
struct mrq_threaded_ping_request {
	uint32_t challenge;
} BPMP_ABI_PACKED;

/** @private */
struct mrq_threaded_ping_response {
	uint32_t reply;
} BPMP_ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_DEBUGFS
 * @brief Interact with BPMP's debugfs file nodes
 *
 * @deprecated use MRQ_DEBUG instead.
 *
 * * Platforms: T186, T194
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_debugfs_request
 * * Response Payload: @ref mrq_debugfs_response
 */

/**
 * @addtogroup Debugfs
 * @{
 *
 * The BPMP firmware implements a pseudo-filesystem called
 * debugfs. Any driver within the firmware may register with debugfs
 * to expose an arbitrary set of "files" in the filesystem. When
 * software on the CPU writes to a debugfs file, debugfs passes the
 * written data to a callback provided by the driver. When software on
 * the CPU reads a debugfs file, debugfs queries the driver for the
 * data to return to the CPU. The intention of the debugfs filesystem
 * is to provide information useful for debugging the system at
 * runtime.
 *
 * @note The files exposed via debugfs are not part of the
 * BPMP firmware's ABI. debugfs files may be added or removed in any
 * given version of the firmware. Typically the semantics of a debugfs
 * file are consistent from version to version but even that is not
 * guaranteed.
 *
 * @}
 */

/** @ingroup Debugfs */
enum mrq_debugfs_commands {
	/** @brief Perform read */
	CMD_DEBUGFS_READ = 1,
	/** @brief Perform write */
	CMD_DEBUGFS_WRITE = 2,
	/** @brief Perform dumping directory */
	CMD_DEBUGFS_DUMPDIR = 3,
	/** @brief Not a command */
	CMD_DEBUGFS_MAX
};

/**
 * @ingroup Debugfs
 * @brief Parameters for CMD_DEBUGFS_READ/WRITE command
 */
struct cmd_debugfs_fileop_request {
	/** @brief Physical address pointing at filename */
	uint32_t fnameaddr;
	/** @brief Length in bytes of filename buffer */
	uint32_t fnamelen;
	/** @brief Physical address pointing to data buffer */
	uint32_t dataaddr;
	/** @brief Length in bytes of data buffer */
	uint32_t datalen;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Parameters for CMD_DEBUGFS_READ/WRITE command
 */
struct cmd_debugfs_dumpdir_request {
	/** @brief Physical address pointing to data buffer */
	uint32_t dataaddr;
	/** @brief Length in bytes of data buffer */
	uint32_t datalen;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Response data for CMD_DEBUGFS_READ/WRITE command
 */
struct cmd_debugfs_fileop_response {
	/** @brief Always 0 */
	uint32_t reserved;
	/** @brief Number of bytes read from or written to data buffer */
	uint32_t nbytes;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Response data for CMD_DEBUGFS_DUMPDIR command
 */
struct cmd_debugfs_dumpdir_response {
	/** @brief Always 0 */
	uint32_t reserved;
	/** @brief Number of bytes read from or written to data buffer */
	uint32_t nbytes;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Request with #MRQ_DEBUGFS.
 *
 * The sender of an MRQ_DEBUGFS message uses #cmd to specify a debugfs
 * command to execute. Legal commands are the values of @ref
 * mrq_debugfs_commands. Each command requires a specific additional
 * payload of data.
 *
 * |command            |payload|
 * |-------------------|-------|
 * |CMD_DEBUGFS_READ   |fop    |
 * |CMD_DEBUGFS_WRITE  |fop    |
 * |CMD_DEBUGFS_DUMPDIR|dumpdir|
 */
struct mrq_debugfs_request {
	/** @brief Sub-command (@ref mrq_debugfs_commands) */
	uint32_t cmd;
	union {
		struct cmd_debugfs_fileop_request fop;
		struct cmd_debugfs_dumpdir_request dumpdir;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 */
struct mrq_debugfs_response {
	/** @brief Always 0 */
	int32_t reserved;
	union {
		/** @brief Response data for CMD_DEBUGFS_READ OR
		 * CMD_DEBUGFS_WRITE command
		 */
		struct cmd_debugfs_fileop_response fop;
		/** @brief Response data for CMD_DEBUGFS_DUMPDIR command */
		struct cmd_debugfs_dumpdir_response dumpdir;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @addtogroup Debugfs
 * @{
 */
#define DEBUGFS_S_ISDIR	(1 << 9)
#define DEBUGFS_S_IRUSR	(1 << 8)
#define DEBUGFS_S_IWUSR	(1 << 7)
/** @} Debugfs */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_DEBUG
 * @brief Interact with BPMP's debugfs file nodes. Use message payload
 * for exchanging data. This is functionally equivalent to
 * @ref MRQ_DEBUGFS. But the way in which data is exchanged is different.
 * When software running on CPU tries to read a debugfs file,
 * the file path and read data will be stored in message payload.
 * Since the message payload size is limited, a debugfs file
 * transaction might require multiple frames of data exchanged
 * between BPMP and CPU until the transaction completes.
 *
 * * Platforms: T194
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_debug_request
 * * Response Payload: @ref mrq_debug_response
 */

/** @ingroup Debugfs */
enum mrq_debug_commands {
	/** @brief Open required file for read operation */
	CMD_DEBUG_OPEN_RO = 0,
	/** @brief Open required file for write operation */
	CMD_DEBUG_OPEN_WO = 1,
	/** @brief Perform read */
	CMD_DEBUG_READ = 2,
	/** @brief Perform write */
	CMD_DEBUG_WRITE = 3,
	/** @brief Close file */
	CMD_DEBUG_CLOSE = 4,
	/** @brief Not a command */
	CMD_DEBUG_MAX
};

/**
 * @ingroup Debugfs
 * @brief Maximum number of files that can be open at a given time
 */
#define DEBUG_MAX_OPEN_FILES	1

/**
 * @ingroup Debugfs
 * @brief Maximum size of null-terminated file name string in bytes.
 * Value is derived from memory available in message payload while
 * using @ref cmd_debug_fopen_request
 * Value 4 corresponds to size of @ref mrq_debug_commands
 * in @ref mrq_debug_request.
 * 120 - 4 dbg_cmd(32bit)  = 116
 */
#define DEBUG_FNAME_MAX_SZ	(MSG_DATA_MIN_SZ - 4)

/**
 * @ingroup Debugfs
 * @brief Parameters for CMD_DEBUG_OPEN command
 */
struct cmd_debug_fopen_request {
	/** @brief File name - Null-terminated string with maximum
	 * length @ref DEBUG_FNAME_MAX_SZ
	 */
	char name[DEBUG_FNAME_MAX_SZ];
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Response data for CMD_DEBUG_OPEN_RO/WO command
 */
struct cmd_debug_fopen_response {
	/** @brief Identifier for file access */
	uint32_t fd;
	/** @brief Data length. File data size for READ command.
	 * Maximum allowed length for WRITE command
	 */
	uint32_t datalen;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Parameters for CMD_DEBUG_READ command
 */
struct cmd_debug_fread_request {
	/** @brief File access identifier received in response
	 * to CMD_DEBUG_OPEN_RO request
	 */
	uint32_t fd;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Maximum size of read data in bytes.
 * Value is derived from memory available in message payload while
 * using @ref cmd_debug_fread_response.
 */
#define DEBUG_READ_MAX_SZ	(MSG_DATA_MIN_SZ - 4)

/**
 * @ingroup Debugfs
 * @brief Response data for CMD_DEBUG_READ command
 */
struct cmd_debug_fread_response {
	/** @brief Size of data provided in this response in bytes */
	uint32_t readlen;
	/** @brief File data from seek position */
	char data[DEBUG_READ_MAX_SZ];
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Maximum size of write data in bytes.
 * Value is derived from memory available in message payload while
 * using @ref cmd_debug_fwrite_request.
 */
#define DEBUG_WRITE_MAX_SZ	(MSG_DATA_MIN_SZ - 12)

/**
 * @ingroup Debugfs
 * @brief Parameters for CMD_DEBUG_WRITE command
 */
struct cmd_debug_fwrite_request {
	/** @brief File access identifier received in response
	 * to CMD_DEBUG_OPEN_RO request
	 */
	uint32_t fd;
	/** @brief Size of write data in bytes */
	uint32_t datalen;
	/** @brief Data to be written */
	char data[DEBUG_WRITE_MAX_SZ];
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Parameters for CMD_DEBUG_CLOSE command
 */
struct cmd_debug_fclose_request {
	/** @brief File access identifier received in response
	 * to CMD_DEBUG_OPEN_RO request
	 */
	uint32_t fd;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Request with #MRQ_DEBUG.
 *
 * The sender of an MRQ_DEBUG message uses #cmd to specify a debugfs
 * command to execute. Legal commands are the values of @ref
 * mrq_debug_commands. Each command requires a specific additional
 * payload of data.
 *
 * |command            |payload|
 * |-------------------|-------|
 * |CMD_DEBUG_OPEN_RO  |fop    |
 * |CMD_DEBUG_OPEN_WO  |fop    |
 * |CMD_DEBUG_READ     |frd    |
 * |CMD_DEBUG_WRITE    |fwr    |
 * |CMD_DEBUG_CLOSE    |fcl    |
 */
struct mrq_debug_request {
	/** @brief Sub-command (@ref mrq_debug_commands) */
	uint32_t cmd;
	union {
		/** @brief Request payload for CMD_DEBUG_OPEN_RO/WO command */
		struct cmd_debug_fopen_request fop;
		/** @brief Request payload for CMD_DEBUG_READ command */
		struct cmd_debug_fread_request frd;
		/** @brief Request payload for CMD_DEBUG_WRITE command */
		struct cmd_debug_fwrite_request fwr;
		/** @brief Request payload for CMD_DEBUG_CLOSE command */
		struct cmd_debug_fclose_request fcl;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 */
struct mrq_debug_response {
	union {
		/** @brief Response data for CMD_DEBUG_OPEN_RO/WO command */
		struct cmd_debug_fopen_response fop;
		/** @brief Response data for CMD_DEBUG_READ command */
		struct cmd_debug_fread_response frd;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_RESET
 * @brief Reset an IP block
 *
 * * Platforms: T186, T194
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_reset_request
 * * Response Payload: @ref mrq_reset_response
 *
 * @addtogroup Reset
 * @{
 */

enum mrq_reset_commands {
	/**
	 * @brief Assert module reset
	 *
	 * mrq_response::err is 0 if the operation was successful, or @n
	 * -#BPMP_EINVAL if mrq_reset_request::reset_id is invalid @n
	 * -#BPMP_EACCES if mrq master is not an owner of target domain reset @n
	 * -#BPMP_ENOTSUP if target domain h/w state does not allow reset
	 */
	CMD_RESET_ASSERT = 1,
	/**
	 * @brief Deassert module reset
	 *
	 * mrq_response::err is 0 if the operation was successful, or @n
	 * -#BPMP_EINVAL if mrq_reset_request::reset_id is invalid @n
	 * -#BPMP_EACCES if mrq master is not an owner of target domain reset @n
	 * -#BPMP_ENOTSUP if target domain h/w state does not allow reset
	 */
	CMD_RESET_DEASSERT = 2,
	/**
	 * @brief Assert and deassert the module reset
	 *
	 * mrq_response::err is 0 if the operation was successful, or @n
	 * -#BPMP_EINVAL if mrq_reset_request::reset_id is invalid @n
	 * -#BPMP_EACCES if mrq master is not an owner of target domain reset @n
	 * -#BPMP_ENOTSUP if target domain h/w state does not allow reset
	 */
	CMD_RESET_MODULE = 3,
	/**
	 * @brief Get the highest reset ID
	 *
	 * mrq_response::err is 0 if the operation was successful, or @n
	 * -#BPMP_ENODEV if no reset domains are supported (number of IDs is 0)
	 */
	CMD_RESET_GET_MAX_ID = 4,

	/** @brief Not part of ABI and subject to change */
	CMD_RESET_MAX,
};

/**
 * @brief Request with MRQ_RESET
 *
 * Used by the sender of an #MRQ_RESET message to request BPMP to
 * assert or or deassert a given reset line.
 */
struct mrq_reset_request {
	/** @brief Reset action to perform (@ref mrq_reset_commands) */
	uint32_t cmd;
	/** @brief Id of the reset to affected */
	uint32_t reset_id;
} BPMP_ABI_PACKED;

/**
 * @brief Response for MRQ_RESET sub-command CMD_RESET_GET_MAX_ID. When
 * this sub-command is not supported, firmware will return -BPMP_EBADCMD
 * in mrq_response::err.
 */
struct cmd_reset_get_max_id_response {
	/** @brief Max reset id */
	uint32_t max_id;
} BPMP_ABI_PACKED;

/**
 * @brief Response with MRQ_RESET
 *
 * Each sub-command supported by @ref mrq_reset_request may return
 * sub-command-specific data. Some do and some do not as indicated
 * in the following table
 *
 * | sub-command          | payload          |
 * |----------------------|------------------|
 * | CMD_RESET_ASSERT     | -                |
 * | CMD_RESET_DEASSERT   | -                |
 * | CMD_RESET_MODULE     | -                |
 * | CMD_RESET_GET_MAX_ID | reset_get_max_id |
 */
struct mrq_reset_response {
	union {
		struct cmd_reset_get_max_id_response reset_get_max_id;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} Reset */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_I2C
 * @brief Issue an i2c transaction
 *
 * * Platforms: T186, T194
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_i2c_request
 * * Response Payload: @ref mrq_i2c_response
 *
 * @addtogroup I2C
 * @{
 */
#define TEGRA_I2C_IPC_MAX_IN_BUF_SIZE	(MSG_DATA_MIN_SZ - 12U)
#define TEGRA_I2C_IPC_MAX_OUT_BUF_SIZE	(MSG_DATA_MIN_SZ - 4U)

#define SERIALI2C_TEN           0x0010U
#define SERIALI2C_RD            0x0001U
#define SERIALI2C_STOP          0x8000U
#define SERIALI2C_NOSTART       0x4000U
#define SERIALI2C_REV_DIR_ADDR  0x2000U
#define SERIALI2C_IGNORE_NAK    0x1000U
#define SERIALI2C_NO_RD_ACK     0x0800U
#define SERIALI2C_RECV_LEN      0x0400U

enum {
	CMD_I2C_XFER = 1
};

/**
 * @brief Serializable i2c request
 *
 * Instances of this structure are packed (little-endian) into
 * cmd_i2c_xfer_request::data_buf. Each instance represents a single
 * transaction (or a portion of a transaction with repeated starts) on
 * an i2c bus.
 *
 * Because these structures are packed, some instances are likely to
 * be misaligned. Additionally because #data is variable length, it is
 * not possible to iterate through a serialized list of these
 * structures without inspecting #len in each instance.  It may be
 * easier to serialize or deserialize cmd_i2c_xfer_request::data_buf
 * manually rather than using this structure definition.
*/
struct serial_i2c_request {
	/** @brief I2C slave address */
	uint16_t addr;
	/** @brief Bitmask of SERIALI2C_ flags */
	uint16_t flags;
	/** @brief Length of I2C transaction in bytes */
	uint16_t len;
	/** @brief For write transactions only, #len bytes of data */
	uint8_t data[];
} BPMP_ABI_PACKED;

/**
 * @brief Trigger one or more i2c transactions
 */
struct cmd_i2c_xfer_request {
	/**
	 * @brief Tegra PWR_I2C bus identifier
	 *
	 * @cond (bpmp_t234 || bpmp_t239 || bpmp_t194)
	 * Must be set to 5.
	 * @endcond (bpmp_t234 || bpmp_t239 || bpmp_t194)
	 * @cond bpmp_th500
	 * Must be set to 1.
	 * @endcond bpmp_th500
	 *
	 */
	uint32_t bus_id;

	/** @brief Count of valid bytes in #data_buf*/
	uint32_t data_size;

	/** @brief Serialized packed instances of @ref serial_i2c_request*/
	uint8_t data_buf[TEGRA_I2C_IPC_MAX_IN_BUF_SIZE];
} BPMP_ABI_PACKED;

/**
 * @brief Container for data read from the i2c bus
 *
 * Processing an cmd_i2c_xfer_request::data_buf causes BPMP to execute
 * zero or more I2C reads. The data read from the bus is serialized
 * into #data_buf.
 */
struct cmd_i2c_xfer_response {
	/** @brief Count of valid bytes in #data_buf*/
	uint32_t data_size;
	/** @brief I2c read data */
	uint8_t data_buf[TEGRA_I2C_IPC_MAX_OUT_BUF_SIZE];
} BPMP_ABI_PACKED;

/**
 * @brief Request with #MRQ_I2C
 */
struct mrq_i2c_request {
	/** @brief Always CMD_I2C_XFER (i.e. 1) */
	uint32_t cmd;
	/** @brief Parameters of the transfer request */
	struct cmd_i2c_xfer_request xfer;
} BPMP_ABI_PACKED;

/**
 * @brief Response to #MRQ_I2C
 *
 * mrq_response:err is
 *  0: Success
 *  -#BPMP_EBADCMD: if mrq_i2c_request::cmd is other than 1
 *  -#BPMP_EINVAL: if cmd_i2c_xfer_request does not contain correctly formatted request
 *  -#BPMP_ENODEV: if cmd_i2c_xfer_request::bus_id is not supported by BPMP
 *  -#BPMP_EACCES: if i2c transaction is not allowed due to firewall rules
 *  -#BPMP_ETIMEDOUT: if i2c transaction times out
 *  -#BPMP_ENXIO: if i2c slave device does not reply with ACK to the transaction
 *  -#BPMP_EAGAIN: if ARB_LOST condition is detected by the i2c controller
 *  -#BPMP_EIO: any other i2c controller error code than NO_ACK or ARB_LOST
 */
struct mrq_i2c_response {
	struct cmd_i2c_xfer_response xfer;
} BPMP_ABI_PACKED;

/** @} I2C */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_CLK
 * @brief Perform a clock operation
 *
 * * Platforms: T186, T194
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_clk_request
 * * Response Payload: @ref mrq_clk_response
 *
 * @addtogroup Clocks
 * @{
 */
enum {
	CMD_CLK_GET_RATE = 1,
	CMD_CLK_SET_RATE = 2,
	CMD_CLK_ROUND_RATE = 3,
	CMD_CLK_GET_PARENT = 4,
	CMD_CLK_SET_PARENT = 5,
	CMD_CLK_IS_ENABLED = 6,
	CMD_CLK_ENABLE = 7,
	CMD_CLK_DISABLE = 8,
/** @cond DEPRECATED */
	CMD_CLK_PROPERTIES = 9,
	CMD_CLK_POSSIBLE_PARENTS = 10,
	CMD_CLK_NUM_POSSIBLE_PARENTS = 11,
	CMD_CLK_GET_POSSIBLE_PARENT = 12,
	CMD_CLK_RESET_REFCOUNTS = 13,
/** @endcond DEPRECATED */
	CMD_CLK_GET_ALL_INFO = 14,
	CMD_CLK_GET_MAX_CLK_ID = 15,
	CMD_CLK_GET_FMAX_AT_VMIN = 16,
	CMD_CLK_MAX,
};

#define BPMP_CLK_HAS_MUX	(1U << 0U)
#define BPMP_CLK_HAS_SET_RATE	(1U << 1U)
#define BPMP_CLK_IS_ROOT	(1U << 2U)
#define BPMP_CLK_IS_VAR_ROOT	(1U << 3U)
/**
 * @brief Protection against rate and parent changes
 *
 * #MRQ_CLK command #CMD_CLK_SET_RATE or #MRQ_CLK command #CMD_CLK_SET_PARENT will return
 * -#BPMP_EACCES.
 */
#define BPMP_CLK_RATE_PARENT_CHANGE_DENIED (1U << 30)

/**
 * @brief Protection against state changes
 *
 * #MRQ_CLK command #CMD_CLK_ENABLE or #MRQ_CLK command #CMD_CLK_DISABLE will return
 * -#BPMP_EACCES.
 */
#define BPMP_CLK_STATE_CHANGE_DENIED (1U << 31)

#define MRQ_CLK_NAME_MAXLEN	40U
#define MRQ_CLK_MAX_PARENTS	16U

/** @private */
struct cmd_clk_get_rate_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

struct cmd_clk_get_rate_response {
	int64_t rate;
} BPMP_ABI_PACKED;

struct cmd_clk_set_rate_request {
	int32_t unused;
	int64_t rate;
} BPMP_ABI_PACKED;

struct cmd_clk_set_rate_response {
	int64_t rate;
} BPMP_ABI_PACKED;

struct cmd_clk_round_rate_request {
	int32_t unused;
	int64_t rate;
} BPMP_ABI_PACKED;

struct cmd_clk_round_rate_response {
	int64_t rate;
} BPMP_ABI_PACKED;

/** @private */
struct cmd_clk_get_parent_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

struct cmd_clk_get_parent_response {
	uint32_t parent_id;
} BPMP_ABI_PACKED;

struct cmd_clk_set_parent_request {
	uint32_t parent_id;
} BPMP_ABI_PACKED;

struct cmd_clk_set_parent_response {
	uint32_t parent_id;
} BPMP_ABI_PACKED;

/** @private */
struct cmd_clk_is_enabled_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @brief Response data to #MRQ_CLK sub-command CMD_CLK_IS_ENABLED
 */
struct cmd_clk_is_enabled_response {
	/**
	 * @brief The state of the clock that has been successfully
	 * requested with CMD_CLK_ENABLE or CMD_CLK_DISABLE by the
	 * master invoking the command earlier.
	 *
	 * The state may not reflect the physical state of the clock
	 * if there are some other masters requesting it to be
	 * enabled.
	 *
	 * Value 0 is disabled, all other values indicate enabled.
	 */
	int32_t state;
} BPMP_ABI_PACKED;

/** @private */
struct cmd_clk_enable_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/** @private */
struct cmd_clk_enable_response {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/** @private */
struct cmd_clk_disable_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/** @private */
struct cmd_clk_disable_response {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/** @cond DEPRECATED */
/** @private */
struct cmd_clk_properties_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/** @todo flags need to be spelled out here */
struct cmd_clk_properties_response {
	uint32_t flags;
} BPMP_ABI_PACKED;

/** @private */
struct cmd_clk_possible_parents_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

struct cmd_clk_possible_parents_response {
	uint8_t num_parents;
	uint8_t reserved[3];
	uint32_t parent_id[MRQ_CLK_MAX_PARENTS];
} BPMP_ABI_PACKED;

/** @private */
struct cmd_clk_num_possible_parents_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

struct cmd_clk_num_possible_parents_response {
	uint8_t num_parents;
} BPMP_ABI_PACKED;

struct cmd_clk_get_possible_parent_request {
	uint8_t parent_idx;
} BPMP_ABI_PACKED;

struct cmd_clk_get_possible_parent_response {
	uint32_t parent_id;
} BPMP_ABI_PACKED;
/** @endcond DEPRECATED */

/** @private */
struct cmd_clk_get_all_info_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

struct cmd_clk_get_all_info_response {
	uint32_t flags;
	uint32_t parent;
	uint32_t parents[MRQ_CLK_MAX_PARENTS];
	uint8_t num_parents;
	uint8_t name[MRQ_CLK_NAME_MAXLEN];
} BPMP_ABI_PACKED;

/** @private */
struct cmd_clk_get_max_clk_id_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

struct cmd_clk_get_max_clk_id_response {
	uint32_t max_id;
} BPMP_ABI_PACKED;

/** @private */
struct cmd_clk_get_fmax_at_vmin_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

struct cmd_clk_get_fmax_at_vmin_response {
	int64_t rate;
} BPMP_ABI_PACKED;


/**
 * @ingroup Clocks
 * @brief Request with #MRQ_CLK
 *
 * Used by the sender of an #MRQ_CLK message to control clocks. The
 * clk_request is split into several sub-commands. Some sub-commands
 * require no additional data. Others have a sub-command specific
 * payload
 *
 * |sub-command                 |payload                |
 * |----------------------------|-----------------------|
 * |CMD_CLK_GET_RATE            |-                      |
 * |CMD_CLK_SET_RATE            |clk_set_rate           |
 * |CMD_CLK_ROUND_RATE          |clk_round_rate         |
 * |CMD_CLK_GET_PARENT          |-                      |
 * |CMD_CLK_SET_PARENT          |clk_set_parent         |
 * |CMD_CLK_IS_ENABLED          |-                      |
 * |CMD_CLK_ENABLE              |-                      |
 * |CMD_CLK_DISABLE             |-                      |
 * |CMD_CLK_GET_ALL_INFO        |-                      |
 * |CMD_CLK_GET_MAX_CLK_ID      |-                      |
 * |CMD_CLK_GET_FMAX_AT_VMIN    |-
 * |
 *
 */

/** @cond DEPRECATED
 *
 * Older versions of firmware also supported following sub-commands:
 * |CMD_CLK_PROPERTIES          |-                      |
 * |CMD_CLK_POSSIBLE_PARENTS    |-                      |
 * |CMD_CLK_NUM_POSSIBLE_PARENTS|-                      |
 * |CMD_CLK_GET_POSSIBLE_PARENT |clk_get_possible_parent|
 * |CMD_CLK_RESET_REFCOUNTS     |-                      |
 *
 * @endcond DEPRECATED */

struct mrq_clk_request {
	/** @brief Sub-command and clock id concatenated to 32-bit word.
	 * - bits[31..24] is the sub-cmd.
	 * - bits[23..0] is the clock id
	 */
	uint32_t cmd_and_id;

	union {
		/** @private */
		struct cmd_clk_get_rate_request clk_get_rate;
		struct cmd_clk_set_rate_request clk_set_rate;
		struct cmd_clk_round_rate_request clk_round_rate;
		/** @private */
		struct cmd_clk_get_parent_request clk_get_parent;
		struct cmd_clk_set_parent_request clk_set_parent;
		/** @private */
		struct cmd_clk_enable_request clk_enable;
		/** @private */
		struct cmd_clk_disable_request clk_disable;
		/** @private */
		struct cmd_clk_is_enabled_request clk_is_enabled;
		/** @cond DEPRECATED */
		/** @private */
		struct cmd_clk_properties_request clk_properties;
		/** @private */
		struct cmd_clk_possible_parents_request clk_possible_parents;
		/** @private */
		struct cmd_clk_num_possible_parents_request clk_num_possible_parents;
		struct cmd_clk_get_possible_parent_request clk_get_possible_parent;
		/** @endcond DEPRECATED */
		/** @private */
		struct cmd_clk_get_all_info_request clk_get_all_info;
		/** @private */
		struct cmd_clk_get_max_clk_id_request clk_get_max_clk_id;
		/** @private */
		struct cmd_clk_get_fmax_at_vmin_request clk_get_fmax_at_vmin;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @ingroup Clocks
 * @brief Response to MRQ_CLK
 *
 * Each sub-command supported by @ref mrq_clk_request may return
 * sub-command-specific data. Some do and some do not as indicated in
 * the following table
 *
 * |sub-command                 |payload                 |
 * |----------------------------|------------------------|
 * |CMD_CLK_GET_RATE            |clk_get_rate            |
 * |CMD_CLK_SET_RATE            |clk_set_rate            |
 * |CMD_CLK_ROUND_RATE          |clk_round_rate          |
 * |CMD_CLK_GET_PARENT          |clk_get_parent          |
 * |CMD_CLK_SET_PARENT          |clk_set_parent          |
 * |CMD_CLK_IS_ENABLED          |clk_is_enabled          |
 * |CMD_CLK_ENABLE              |-                       |
 * |CMD_CLK_DISABLE             |-                       |
 * |CMD_CLK_GET_ALL_INFO        |clk_get_all_info        |
 * |CMD_CLK_GET_MAX_CLK_ID      |clk_get_max_id          |
 * |CMD_CLK_GET_FMAX_AT_VMIN    |clk_get_fmax_at_vmin    |
 *
 */

/** @cond DEPRECATED
 *
 * Older versions of firmware also supported following sub-commands:
 * |CMD_CLK_PROPERTIES          |clk_properties          |
 * |CMD_CLK_POSSIBLE_PARENTS    |clk_possible_parents    |
 * |CMD_CLK_NUM_POSSIBLE_PARENTS|clk_num_possible_parents|
 * |CMD_CLK_GET_POSSIBLE_PARENT |clk_get_possible_parents|
 * |CMD_CLK_RESET_REFCOUNTS     |-                       |
 *
 * @endcond DEPRECATED */

struct mrq_clk_response {
	union {
		struct cmd_clk_get_rate_response clk_get_rate;
		struct cmd_clk_set_rate_response clk_set_rate;
		struct cmd_clk_round_rate_response clk_round_rate;
		struct cmd_clk_get_parent_response clk_get_parent;
		struct cmd_clk_set_parent_response clk_set_parent;
		/** @private */
		struct cmd_clk_enable_response clk_enable;
		/** @private */
		struct cmd_clk_disable_response clk_disable;
		struct cmd_clk_is_enabled_response clk_is_enabled;
		/** @cond DEPRECATED */
		struct cmd_clk_properties_response clk_properties;
		struct cmd_clk_possible_parents_response clk_possible_parents;
		struct cmd_clk_num_possible_parents_response clk_num_possible_parents;
		struct cmd_clk_get_possible_parent_response clk_get_possible_parent;
		/** @endcond DEPRECATED */
		struct cmd_clk_get_all_info_response clk_get_all_info;
		struct cmd_clk_get_max_clk_id_response clk_get_max_clk_id;
		struct cmd_clk_get_fmax_at_vmin_response clk_get_fmax_at_vmin;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} Clocks */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_QUERY_ABI
 * @brief Check if an MRQ is implemented
 *
 * * Platforms: All
 * * Initiators: Any
 * * Targets: Any except DMCE
 * * Request Payload: @ref mrq_query_abi_request
 * * Response Payload: @ref mrq_query_abi_response
 */

/**
 * @ingroup ABI_info
 * @brief Request with MRQ_QUERY_ABI
 *
 * Used by #MRQ_QUERY_ABI call to check if MRQ code #mrq is supported
 * by the recipient.
 */
struct mrq_query_abi_request {
	/** @brief MRQ code to query */
	uint32_t mrq;
} BPMP_ABI_PACKED;

/**
 * @ingroup ABI_info
 * @brief Response to MRQ_QUERY_ABI
 *
 * @note mrq_response::err of 0 indicates that the query was
 * successful, not that the MRQ itself is supported!
 */
struct mrq_query_abi_response {
	/** @brief 0 if queried MRQ is supported. Else, -#BPMP_ENODEV */
	int32_t status;
} BPMP_ABI_PACKED;

/**
 *
 * @ingroup MRQ_Codes
 * @def MRQ_PG
 * @brief Control power-gating state of a partition. In contrast to
 * MRQ_PG_UPDATE_STATE, operations that change the power partition
 * state are NOT reference counted
 *
 * @cond (bpmp_t194 || bpmp_t186)
 * @note On T194 and earlier BPMP-FW forcefully turns off some partitions as
 * part of SC7 entry because their state cannot be adequately restored on exit.
 * Therefore, it is recommended to power off all domains via MRQ_PG prior to SC7
 * entry.
 * See @ref bpmp_pdomain_ids for further detail.
 * @endcond (bpmp_t194 || bpmp_t186)
 *
 * * Platforms: T186, T194
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_pg_request
 * * Response Payload: @ref mrq_pg_response
 *
 * @addtogroup Powergating
 * @{
 */
enum mrq_pg_cmd {
	/**
	 * @brief Check whether the BPMP driver supports the specified
	 * request type
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_PG_QUERY_ABI = 0,

	/**
	 * @brief Set the current state of specified power domain. The
	 * possible values for power domains are defined in enum
	 * pg_states
	 *
	 * mrq_response:err is
	 * 0: Success
	 * -#BPMP_EINVAL: Invalid request parameters
	 */
	CMD_PG_SET_STATE = 1,

	/**
	 * @brief Get the current state of specified power domain. The
	 * possible values for power domains are defined in enum
	 * pg_states
	 *
	 * mrq_response:err is
	 * 0: Success
	 * -#BPMP_EINVAL: Invalid request parameters
	 */
	CMD_PG_GET_STATE = 2,

	/**
	 * @brief Get the name string of specified power domain id.
	 *
	 * mrq_response:err is
	 * 0: Success
	 * -#BPMP_EINVAL: Invalid request parameters
	 */
	CMD_PG_GET_NAME = 3,


	/**
	 * @brief Get the highest power domain id in the system. Not
	 * all IDs between 0 and max_id are valid IDs.
	 *
	 * mrq_response:err is
	 * 0: Success
	 * -#BPMP_EINVAL: Invalid request parameters
	 */
	CMD_PG_GET_MAX_ID = 4,
};

#define MRQ_PG_NAME_MAXLEN	40

enum pg_states {
	/** @brief Power domain is OFF */
	PG_STATE_OFF = 0,
	/** @brief Power domain is ON */
	PG_STATE_ON = 1,
	/**
	 * @brief a legacy state where power domain and the clock
	 * associated to the domain are ON.
	 * This state is only supported in T186, and the use of it is
	 * deprecated.
	 */
	PG_STATE_RUNNING = 2,
};

struct cmd_pg_query_abi_request {
	/** @ref mrq_pg_cmd */
	uint32_t type;
} BPMP_ABI_PACKED;

struct cmd_pg_set_state_request {
	/** @ref pg_states */
	uint32_t state;
} BPMP_ABI_PACKED;

/**
 * @brief Response data to #MRQ_PG sub command #CMD_PG_GET_STATE
 */
struct cmd_pg_get_state_response {
	/**
	 * @brief The state of the power partition that has been
	 * succesfuly requested by the master earlier using #MRQ_PG
	 * command #CMD_PG_SET_STATE.
	 *
	 * The state may not reflect the physical state of the power
	 * partition if there are some other masters requesting it to
	 * be enabled.
	 *
	 * See @ref pg_states for possible values
	 */
	uint32_t state;
} BPMP_ABI_PACKED;

struct cmd_pg_get_name_response {
	uint8_t name[MRQ_PG_NAME_MAXLEN];
} BPMP_ABI_PACKED;

struct cmd_pg_get_max_id_response {
	uint32_t max_id;
} BPMP_ABI_PACKED;

/**
 * @brief Request with #MRQ_PG
 *
 * Used by the sender of an #MRQ_PG message to control power
 * partitions. The pg_request is split into several sub-commands. Some
 * sub-commands require no additional data. Others have a sub-command
 * specific payload
 *
 * |sub-command                 |payload                |
 * |----------------------------|-----------------------|
 * |CMD_PG_QUERY_ABI            | query_abi             |
 * |CMD_PG_SET_STATE            | set_state             |
 * |CMD_PG_GET_STATE            | -                     |
 * |CMD_PG_GET_NAME             | -                     |
 * |CMD_PG_GET_MAX_ID           | -                     |
 *
 */
struct mrq_pg_request {
	uint32_t cmd;
	uint32_t id;
	union {
		struct cmd_pg_query_abi_request query_abi;
		struct cmd_pg_set_state_request set_state;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response to MRQ_PG
 *
 * Each sub-command supported by @ref mrq_pg_request may return
 * sub-command-specific data. Some do and some do not as indicated in
 * the following table
 *
 * |sub-command                 |payload                |
 * |----------------------------|-----------------------|
 * |CMD_PG_QUERY_ABI            | -                     |
 * |CMD_PG_SET_STATE            | -                     |
 * |CMD_PG_GET_STATE            | get_state             |
 * |CMD_PG_GET_NAME             | get_name              |
 * |CMD_PG_GET_MAX_ID           | get_max_id            |
 */
struct mrq_pg_response {
	union {
		struct cmd_pg_get_state_response get_state;
		struct cmd_pg_get_name_response get_name;
		struct cmd_pg_get_max_id_response get_max_id;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} Powergating */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_THERMAL
 * @brief Interact with BPMP thermal framework
 *
 * * Platforms: T186, T194
 * * Initiators: Any
 * * Targets: Any
 * * Request Payload: TODO
 * * Response Payload: TODO
 *
 * @addtogroup Thermal
 *
 * The BPMP firmware includes a thermal framework. Drivers within the
 * bpmp firmware register with the framework to provide thermal
 * zones. Each thermal zone corresponds to an entity whose temperature
 * can be measured. The framework also has a notion of trip points. A
 * trip point consists of a thermal zone id, a temperature, and a
 * callback routine. The framework invokes the callback when the zone
 * hits the indicated temperature. The BPMP firmware uses this thermal
 * framework interally to implement various temperature-dependent
 * functions.
 *
 * Software on the CPU can use #MRQ_THERMAL (with payload @ref
 * mrq_thermal_host_to_bpmp_request) to interact with the BPMP thermal
 * framework. The CPU must It can query the number of supported zones,
 * query zone temperatures, and set trip points.
 *
 * When a trip point set by the CPU gets crossed, BPMP firmware issues
 * an IPC to the CPU having mrq_request::mrq = #MRQ_THERMAL and a
 * payload of @ref mrq_thermal_bpmp_to_host_request.
 * @{
 */
enum mrq_thermal_host_to_bpmp_cmd {
	/**
	 * @brief Check whether the BPMP driver supports the specified
	 * request type.
	 *
	 * Host needs to supply request parameters.
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_THERMAL_QUERY_ABI = 0,

	/**
	 * @brief Get the current temperature of the specified zone.
	 *
	 * Host needs to supply request parameters.
	 *
	 * mrq_response::err is
	 * *  0: Temperature query succeeded.
	 * *  -#BPMP_EINVAL: Invalid request parameters.
	 * *  -#BPMP_ENOENT: No driver registered for thermal zone..
	 * *  -#BPMP_EFAULT: Problem reading temperature measurement.
	 */
	CMD_THERMAL_GET_TEMP = 1,

	/**
	 * @brief Enable or disable and set the lower and upper
	 *   thermal limits for a thermal trip point. Each zone has
	 *   one trip point.
	 *
	 * Host needs to supply request parameters. Once the
	 * temperature hits a trip point, the BPMP will send a message
	 * to the CPU having MRQ=MRQ_THERMAL and
	 * type=CMD_THERMAL_HOST_TRIP_REACHED
	 *
	 * mrq_response::err is
	 * *  0: Trip successfully set.
	 * *  -#BPMP_EINVAL: Invalid request parameters.
	 * *  -#BPMP_ENOENT: No driver registered for thermal zone.
	 * *  -#BPMP_EFAULT: Problem setting trip point.
	 */
	CMD_THERMAL_SET_TRIP = 2,

	/**
	 * @brief Get the number of supported thermal zones.
	 *
	 * No request parameters required.
	 *
	 * mrq_response::err is always 0, indicating success.
	 */
	CMD_THERMAL_GET_NUM_ZONES = 3,

	/**
	 * @brief Get the thermtrip of the specified zone.
	 *
	 * Host needs to supply request parameters.
	 *
	 * mrq_response::err is
	 * *  0: Valid zone information returned.
	 * *  -#BPMP_EINVAL: Invalid request parameters.
	 * *  -#BPMP_ENOENT: No driver registered for thermal zone.
	 * *  -#BPMP_ERANGE if thermtrip is invalid or disabled.
	 * *  -#BPMP_EFAULT: Problem reading zone information.
	 */
	CMD_THERMAL_GET_THERMTRIP = 4,

	/** @brief: number of supported host-to-bpmp commands. May
	 * increase in future
	 */
	CMD_THERMAL_HOST_TO_BPMP_NUM
};

enum mrq_thermal_bpmp_to_host_cmd {
	/**
	 * @brief Indication that the temperature for a zone has
	 *   exceeded the range indicated in the thermal trip point
	 *   for the zone.
	 *
	 * BPMP needs to supply request parameters. Host only needs to
	 * acknowledge.
	 */
	CMD_THERMAL_HOST_TRIP_REACHED = 100,

	/** @brief: number of supported bpmp-to-host commands. May
	 * increase in future
	 */
	CMD_THERMAL_BPMP_TO_HOST_NUM
};

/*
 * Host->BPMP request data for request type CMD_THERMAL_QUERY_ABI
 *
 * zone: Request type for which to check existence.
 */
struct cmd_thermal_query_abi_request {
	uint32_t type;
} BPMP_ABI_PACKED;

/*
 * Host->BPMP request data for request type CMD_THERMAL_GET_TEMP
 *
 * zone: Number of thermal zone.
 */
struct cmd_thermal_get_temp_request {
	uint32_t zone;
} BPMP_ABI_PACKED;

/*
 * BPMP->Host reply data for request CMD_THERMAL_GET_TEMP
 *
 * error: 0 if request succeeded.
 *	-BPMP_EINVAL if request parameters were invalid.
 *      -BPMP_ENOENT if no driver was registered for the specified thermal zone.
 *      -BPMP_EFAULT for other thermal zone driver errors.
 * temp: Current temperature in millicelsius.
 */
struct cmd_thermal_get_temp_response {
	int32_t temp;
} BPMP_ABI_PACKED;

/*
 * Host->BPMP request data for request type CMD_THERMAL_SET_TRIP
 *
 * zone: Number of thermal zone.
 * low: Temperature of lower trip point in millicelsius
 * high: Temperature of upper trip point in millicelsius
 * enabled: 1 to enable trip point, 0 to disable trip point
 */
struct cmd_thermal_set_trip_request {
	uint32_t zone;
	int32_t low;
	int32_t high;
	uint32_t enabled;
} BPMP_ABI_PACKED;

/*
 * BPMP->Host request data for request type CMD_THERMAL_HOST_TRIP_REACHED
 *
 * zone: Number of thermal zone where trip point was reached.
 */
struct cmd_thermal_host_trip_reached_request {
	uint32_t zone;
} BPMP_ABI_PACKED;

/*
 * BPMP->Host reply data for request type CMD_THERMAL_GET_NUM_ZONES
 *
 * num: Number of supported thermal zones. The thermal zones are indexed
 *      starting from zero.
 */
struct cmd_thermal_get_num_zones_response {
	uint32_t num;
} BPMP_ABI_PACKED;

/*
 * Host->BPMP request data for request type CMD_THERMAL_GET_THERMTRIP
 *
 * zone: Number of thermal zone.
 */
struct cmd_thermal_get_thermtrip_request {
	uint32_t zone;
} BPMP_ABI_PACKED;

/*
 * BPMP->Host reply data for request CMD_THERMAL_GET_THERMTRIP
 *
 * thermtrip: HW shutdown temperature in millicelsius.
 */
struct cmd_thermal_get_thermtrip_response {
	int32_t thermtrip;
} BPMP_ABI_PACKED;

/*
 * Host->BPMP request data.
 *
 * Reply type is union mrq_thermal_bpmp_to_host_response.
 *
 * type: Type of request. Values listed in enum mrq_thermal_type.
 * data: Request type specific parameters.
 */
struct mrq_thermal_host_to_bpmp_request {
	uint32_t type;
	union {
		struct cmd_thermal_query_abi_request query_abi;
		struct cmd_thermal_get_temp_request get_temp;
		struct cmd_thermal_set_trip_request set_trip;
		struct cmd_thermal_get_thermtrip_request get_thermtrip;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/*
 * BPMP->Host request data.
 *
 * type: Type of request. Values listed in enum mrq_thermal_type.
 * data: Request type specific parameters.
 */
struct mrq_thermal_bpmp_to_host_request {
	uint32_t type;
	union {
		struct cmd_thermal_host_trip_reached_request host_trip_reached;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/*
 * Data in reply to a Host->BPMP request.
 */
union mrq_thermal_bpmp_to_host_response {
	struct cmd_thermal_get_temp_response get_temp;
	struct cmd_thermal_get_thermtrip_response get_thermtrip;
	struct cmd_thermal_get_num_zones_response get_num_zones;
} BPMP_ABI_PACKED;

/** @} Thermal */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_OC_STATUS
 * @brief Query over current status
 *
 * * Platforms: T234
 * @cond bpmp_t234
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: N/A
 * * Response Payload: @ref mrq_oc_status_response
 *
 * @addtogroup OC_status
 * @{
 */

#define OC_STATUS_MAX_SIZE	24U

/*
 * @brief Response to #MRQ_OC_STATUS
 *
 * throt_en: Value for each OC alarm where zero signifies throttle is
 *           disabled, and non-zero throttle is enabled.
 * event_cnt: Total number of OC events for each OC alarm.
 *
 * mrq_response::err is 0 if the operation was successful and
 * -#BPMP_ENODEV otherwise.
 */
struct mrq_oc_status_response {
	uint8_t throt_en[OC_STATUS_MAX_SIZE];
	uint32_t event_cnt[OC_STATUS_MAX_SIZE];
} BPMP_ABI_PACKED;

/** @} OC_status */
/** @endcond bpmp_t234 */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_CPU_VHINT
 * @brief Query CPU voltage hint data
 *
 * * Platforms: T186
 * @cond bpmp_t186
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_cpu_vhint_request
 * * Response Payload: N/A
 *
 * @addtogroup Vhint
 * @{
 */

/**
 * @brief Request with #MRQ_CPU_VHINT
 *
 * Used by #MRQ_CPU_VHINT call by CCPLEX to retrieve voltage hint data
 * from BPMP to memory space pointed by #addr. CCPLEX is responsible
 * to allocate sizeof(cpu_vhint_data) sized block of memory and
 * appropriately map it for BPMP before sending the request.
 */
struct mrq_cpu_vhint_request {
	/** @brief IOVA address for the #cpu_vhint_data */
	uint32_t addr;
	/** @brief ID of the cluster whose data is requested */
	uint32_t cluster_id;
} BPMP_ABI_PACKED;

/**
 * @brief Description of the CPU v/f relation
 *
 * Used by #MRQ_CPU_VHINT call to carry data pointed by
 * #mrq_cpu_vhint_request::addr
 */
struct cpu_vhint_data {
	uint32_t ref_clk_hz; /**< reference frequency in Hz */
	uint16_t pdiv; /**< post divider value */
	uint16_t mdiv; /**< input divider value */
	uint16_t ndiv_max; /**< fMAX expressed with max NDIV value */
	/** table of ndiv values as a function of vINDEX (voltage index) */
	uint16_t ndiv[80];
	/** minimum allowed NDIV value */
	uint16_t ndiv_min;
	/** minimum allowed voltage hint value (as in vINDEX) */
	uint16_t vfloor;
	/** maximum allowed voltage hint value (as in vINDEX) */
	uint16_t vceil;
	/** post-multiplier for vindex value */
	uint16_t vindex_mult;
	/** post-divider for vindex value */
	uint16_t vindex_div;
	/** reserved for future use */
	uint16_t reserved[328];
} BPMP_ABI_PACKED;

/** @} Vhint */
/** @endcond bpmp_t186 */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_ABI_RATCHET
 * @brief ABI ratchet value query
 *
 * * Platforms: T186, T194
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_abi_ratchet_request
 * * Response Payload: @ref mrq_abi_ratchet_response
 * @addtogroup ABI_info
 * @{
 */

/**
 * @brief An ABI compatibility mechanism
 *
 * BPMP_ABI_RATCHET_VALUE may increase for various reasons in a future
 * revision of this header file.
 * 1. That future revision deprecates some MRQ
 * 2. That future revision introduces a breaking change to an existing
 *    MRQ or
 * 3. A bug is discovered in an existing implementation of the BPMP-FW
 *    (or possibly one of its clients) which warrants deprecating that
 *    implementation.
 */
#define BPMP_ABI_RATCHET_VALUE 3

/**
 * @brief Request with #MRQ_ABI_RATCHET.
 *
 * #ratchet should be #BPMP_ABI_RATCHET_VALUE from the ABI header
 * against which the requester was compiled.
 *
 * If ratchet is less than BPMP's #BPMP_ABI_RATCHET_VALUE, BPMP may
 * reply with mrq_response::err = -#BPMP_ERANGE to indicate that
 * BPMP-FW cannot interoperate correctly with the requester. Requester
 * should cease further communication with BPMP.
 *
 * Otherwise, err shall be 0.
 */
struct mrq_abi_ratchet_request {
	/** @brief Requester's ratchet value */
	uint16_t ratchet;
};

/**
 * @brief Response to #MRQ_ABI_RATCHET
 *
 * #ratchet shall be #BPMP_ABI_RATCHET_VALUE from the ABI header
 * against which BPMP firwmare was compiled.
 *
 * If #ratchet is less than the requester's #BPMP_ABI_RATCHET_VALUE,
 * the requster must either interoperate with BPMP according to an ABI
 * header version with BPMP_ABI_RATCHET_VALUE = ratchet or cease
 * communication with BPMP.
 *
 * If mrq_response::err is 0 and ratchet is greater than or equal to the
 * requester's BPMP_ABI_RATCHET_VALUE, the requester should continue
 * normal operation.
 */
struct mrq_abi_ratchet_response {
	/** @brief BPMP's ratchet value */
	uint16_t ratchet;
};

/** @} ABI_info */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_EMC_DVFS_LATENCY
 * @brief Query frequency dependent EMC DVFS latency
 *
 * * Platforms: T186, T194, T234
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: N/A
 * * Response Payload: @ref mrq_emc_dvfs_latency_response
 * @addtogroup EMC
 * @{
 */

/**
 * @brief Used by @ref mrq_emc_dvfs_latency_response
 */
struct emc_dvfs_latency {
	/** @brief EMC DVFS node frequency in kHz */
	uint32_t freq;
	/** @brief EMC DVFS latency in nanoseconds */
	uint32_t latency;
} BPMP_ABI_PACKED;

#define EMC_DVFS_LATENCY_MAX_SIZE	14
/**
 * @brief Response to #MRQ_EMC_DVFS_LATENCY
 */
struct mrq_emc_dvfs_latency_response {
	/** @brief The number valid entries in #pairs */
	uint32_t num_pairs;
	/** @brief EMC DVFS node <frequency, latency> information */
	struct emc_dvfs_latency pairs[EMC_DVFS_LATENCY_MAX_SIZE];
} BPMP_ABI_PACKED;

/** @} EMC */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_EMC_DVFS_EMCHUB
 * @brief Query EMC HUB frequencies
 *
 * * Platforms: T234 onwards
 * @cond (bpmp_t234 || bpmp_t239 || bpmp_th500)
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: N/A
 * * Response Payload: @ref mrq_emc_dvfs_emchub_response
 * @addtogroup EMC
 * @{
 */

/**
 * @brief Used by @ref mrq_emc_dvfs_emchub_response
 */
struct emc_dvfs_emchub {
	/** @brief EMC DVFS node frequency in kHz */
	uint32_t freq;
	/** @brief EMC HUB frequency in kHz */
	uint32_t hub_freq;
} BPMP_ABI_PACKED;

#define EMC_DVFS_EMCHUB_MAX_SIZE	EMC_DVFS_LATENCY_MAX_SIZE
/**
 * @brief Response to #MRQ_EMC_DVFS_EMCHUB
 */
struct mrq_emc_dvfs_emchub_response {
	/** @brief The number valid entries in #pairs */
	uint32_t num_pairs;
	/** @brief EMC DVFS node <frequency, hub frequency> information */
	struct emc_dvfs_emchub pairs[EMC_DVFS_EMCHUB_MAX_SIZE];
} BPMP_ABI_PACKED;

/** @} EMC */
/** @endcond (bpmp_t234 || bpmp_t239 || bpmp_th500) */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_EMC_DISP_RFL
 * @brief Set EMC display RFL handshake mode of operations
 *
 * * Platforms: T234 onwards
 * @cond (bpmp_t234 || bpmp_t239 || bpmp_th500)
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_emc_disp_rfl_request
 * * Response Payload: N/A
 *
 * @addtogroup EMC
 * @{
 */

enum mrq_emc_disp_rfl_mode {
	/** @brief EMC display RFL handshake disabled  */
	EMC_DISP_RFL_MODE_DISABLED = 0,
	/** @brief EMC display RFL handshake enabled  */
	EMC_DISP_RFL_MODE_ENABLED = 1,
};

/**
 * @ingroup EMC
 * @brief Request with #MRQ_EMC_DISP_RFL
 *
 * Used by the sender of an #MRQ_EMC_DISP_RFL message to
 * request the mode of EMC display RFL handshake.
 *
 * mrq_response::err is
 * * 0: RFL mode is set successfully
 * * -#BPMP_EINVAL: invalid mode requested
 * * -#BPMP_ENOSYS: RFL handshake is not supported
 * * -#BPMP_EACCES: Permission denied
 * * -#BPMP_ENODEV: if disp rfl mrq is not supported by BPMP-FW
 */
struct mrq_emc_disp_rfl_request {
	/** @brief EMC display RFL mode (@ref mrq_emc_disp_rfl_mode) */
	uint32_t mode;
} BPMP_ABI_PACKED;

/** @} EMC */
/** @endcond (bpmp_t234 || bpmp_t239 || bpmp_th500) */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_BWMGR
 * @brief bwmgr requests
 *
 * * Platforms: T234 onwards
 * @cond (bpmp_t234 || bpmp_t239 || bpmp_th500)
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_bwmgr_request
 * * Response Payload: @ref mrq_bwmgr_response
 *
 * @addtogroup BWMGR
 *
 * @{
 */

enum mrq_bwmgr_cmd {
	/**
	 * @brief Check whether the BPMP driver supports the specified
	 * request type
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_BWMGR_QUERY_ABI = 0,

	/**
	 * @brief Determine dram rate to satisfy iso/niso bw requests
	 *
	 * mrq_response::err is
	 * *  0: calc_rate succeeded.
	 * *  -#BPMP_EINVAL: Invalid request parameters.
	 * *  -#BPMP_ENOTSUP: Requested bw is not available.
	 */
	CMD_BWMGR_CALC_RATE = 1
};

/*
 * request data for request type CMD_BWMGR_QUERY_ABI
 *
 * type: Request type for which to check existence.
 */
struct cmd_bwmgr_query_abi_request {
	uint32_t type;
} BPMP_ABI_PACKED;

/**
 * @brief Used by @ref cmd_bwmgr_calc_rate_request
 */
struct iso_req {
	/* @brief bwmgr client ID @ref bpmp_bwmgr_ids */
	uint32_t id;
	/* @brief bw in kBps requested by client */
	uint32_t iso_bw;
} BPMP_ABI_PACKED;

#define MAX_ISO_CLIENTS		13U
/*
 * request data for request type CMD_BWMGR_CALC_RATE
 */
struct cmd_bwmgr_calc_rate_request {
	/* @brief total bw in kBps requested by all niso clients */
	uint32_t sum_niso_bw;
	/* @brief The number of iso clients */
	uint32_t num_iso_clients;
	/* @brief iso_req <id, iso_bw> information */
	struct iso_req isobw_reqs[MAX_ISO_CLIENTS];
} BPMP_ABI_PACKED;

/*
 * response data for request type CMD_BWMGR_CALC_RATE
 *
 * iso_rate_min: min dram data clk rate in kHz to satisfy all iso bw reqs
 * total_rate_min: min dram data clk rate in kHz to satisfy all bw reqs
 */
struct cmd_bwmgr_calc_rate_response {
	uint32_t iso_rate_min;
	uint32_t total_rate_min;
} BPMP_ABI_PACKED;

/*
 * @brief Request with #MRQ_BWMGR
 *
 *
 * |sub-command                 |payload                       |
 * |----------------------------|------------------------------|
 * |CMD_BWMGR_QUERY_ABI         | cmd_bwmgr_query_abi_request  |
 * |CMD_BWMGR_CALC_RATE         | cmd_bwmgr_calc_rate_request  |
 *
 */
struct mrq_bwmgr_request {
	uint32_t cmd;
	union {
		struct cmd_bwmgr_query_abi_request query_abi;
		struct cmd_bwmgr_calc_rate_request bwmgr_rate_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/*
 * @brief Response to MRQ_BWMGR
 *
 * |sub-command                 |payload                       |
 * |----------------------------|------------------------------|
 * |CMD_BWMGR_CALC_RATE         | cmd_bwmgr_calc_rate_response |
 */
struct mrq_bwmgr_response {
	union {
		struct cmd_bwmgr_calc_rate_response bwmgr_rate_resp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} BWMGR */
/** @endcond (bpmp_t234 || bpmp_t239 || bpmp_th500) */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_BWMGR_INT
 * @brief bpmp-integrated bwmgr requests
 *
 * * Platforms: T234 onwards
 * @cond (bpmp_t234 || bpmp_t239 || bpmp_th500)
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_bwmgr_int_request
 * * Response Payload: @ref mrq_bwmgr_int_response
 *
 * @addtogroup BWMGR_INT
 * @{
 */

enum mrq_bwmgr_int_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * request type
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_BWMGR_INT_QUERY_ABI = 1,

	/**
	 * @brief Determine and set dram rate to satisfy iso/niso bw request
	 *
	 * mrq_response::err is
	 * *  0: request succeeded.
	 * *  -#BPMP_EINVAL: Invalid request parameters.
	 *          set_frequency in @ref cmd_bwmgr_int_calc_and_set_response
	 *          will not be set.
	 * *  -#BPMP_ENOTSUP: Requested bw is not available.
	 *          set_frequency in @ref cmd_bwmgr_int_calc_and_set_response
	 *          will be current dram-clk rate.
	 */
	CMD_BWMGR_INT_CALC_AND_SET = 2,

	/**
	 * @brief Set a max DRAM frequency for the bandwidth-manager
	 *
	 * mrq_response::err is
	 * *  0: request succeeded.
	 * *  -#BPMP_ENOTSUP: Requested cap frequency is not possible.
	 */
	CMD_BWMGR_INT_CAP_SET = 3
};

/*
 * request structure for request type CMD_BWMGR_QUERY_ABI
 *
 * type: Request type for which to check existence.
 */
struct cmd_bwmgr_int_query_abi_request {
	/* @brief request type determined by @ref mrq_bwmgr_int_cmd */
	uint32_t type;
} BPMP_ABI_PACKED;

/**
 * @defgroup bwmgr_int_unit_type BWMGR_INT floor unit-types
 * @addtogroup bwmgr_int_unit_type
 * @{
 */
/** @brief kilobytes per second unit-type */
#define BWMGR_INT_UNIT_KBPS  0U
/** @brief kilohertz unit-type */
#define BWMGR_INT_UNIT_KHZ   1U

/** @} bwmgr_int_unit_type */

/*
 * request data for request type CMD_BWMGR_INT_CALC_AND_SET
 */
struct cmd_bwmgr_int_calc_and_set_request {
	/* @brief bwmgr client ID @ref bpmp_bwmgr_ids */
	uint32_t client_id;
	/* @brief average niso bw usage in kBps requested by client. */
	uint32_t niso_bw;
	/*
	 * @brief average iso bw usage in kBps requested by client.
	 *  Value is ignored if client is niso. Determined by client_id.
	 */
	uint32_t iso_bw;
	/*
	 * @brief memory clock floor requested by client.
	 *  Unit determined by floor_unit.
	 */
	uint32_t mc_floor;
	/*
	 * @brief toggle to determine the unit-type of floor value.
	 *  See @ref bwmgr_int_unit_type definitions for unit-type mappings.
	 */
	uint8_t floor_unit;
} BPMP_ABI_PACKED;

struct cmd_bwmgr_int_cap_set_request {
	/* @brief requested cap frequency in Hz. */
	uint64_t rate;
} BPMP_ABI_PACKED;

/*
 * response data for request type CMD_BWMGR_CALC_AND_SET
 */
struct cmd_bwmgr_int_calc_and_set_response {
	/* @brief current set memory clock frequency in Hz */
	uint64_t rate;
} BPMP_ABI_PACKED;

/*
 * @brief Request with #MRQ_BWMGR_INT
 *
 *
 * |sub-command                 |payload                            |
 * |----------------------------|-----------------------------------|
 * |CMD_BWMGR_INT_QUERY_ABI     | cmd_bwmgr_int_query_abi_request   |
 * |CMD_BWMGR_INT_CALC_AND_SET  | cmd_bwmgr_int_calc_and_set_request|
 * |CMD_BWMGR_INT_CAP_SET       | cmd_bwmgr_int_cap_set_request     |
 *
 */
struct mrq_bwmgr_int_request {
	uint32_t cmd;
	union {
		struct cmd_bwmgr_int_query_abi_request query_abi;
		struct cmd_bwmgr_int_calc_and_set_request bwmgr_calc_set_req;
		struct cmd_bwmgr_int_cap_set_request bwmgr_cap_set_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/*
 * @brief Response to MRQ_BWMGR_INT
 *
 * |sub-command                 |payload                                |
 * |----------------------------|---------------------------------------|
 * |CMD_BWMGR_INT_CALC_AND_SET  | cmd_bwmgr_int_calc_and_set_response   |
 */
struct mrq_bwmgr_int_response {
	union {
		struct cmd_bwmgr_int_calc_and_set_response bwmgr_calc_set_resp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} BWMGR_INT */
/** @endcond (bpmp_t234 || bpmp_t239 || bpmp_th500) */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_ISO_CLIENT
 * @brief ISO client requests
 *
 * * Platforms: T234 onwards
 * @cond (bpmp_t234 || bpmp_t239 || bpmp_th500)
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_iso_client_request
 * * Response Payload: @ref mrq_iso_client_response
 *
 * @addtogroup ISO_CLIENT
 * @{
 */

enum mrq_iso_client_cmd {
	/**
	 * @brief Check whether the BPMP driver supports the specified
	 * request type
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_ISO_CLIENT_QUERY_ABI = 0,

	/*
	 * @brief check for legal LA for the iso client. Without programming
	 * LA MC registers, calculate and ensure that legal LA is possible for
	 * iso bw requested by the ISO client.
	 *
	 * mrq_response::err is
	 * *  0: check la succeeded.
	 * *  -#BPMP_EINVAL: Invalid request parameters.
	 * *  -#BPMP_EFAULT: Legal LA is not possible for client requested iso_bw
	 */
	CMD_ISO_CLIENT_CALCULATE_LA = 1,

	/*
	 * @brief set LA for the iso client. Calculate and program the LA/PTSA
	 * MC registers corresponding to the client making bw request
	 *
	 * mrq_response::err is
	 * *  0: set la succeeded.
	 * *  -#BPMP_EINVAL: Invalid request parameters.
	 * *  -#BPMP_EFAULT: Failed to calculate or program MC registers.
	 */
	CMD_ISO_CLIENT_SET_LA = 2,

	/*
	 * @brief Get max possible bw for iso client
	 *
	 * mrq_response::err is
	 * *  0: get_max_bw succeeded.
	 * *  -#BPMP_EINVAL: Invalid request parameters.
	 */
	CMD_ISO_CLIENT_GET_MAX_BW = 3
};

/*
 * request data for request type CMD_ISO_CLIENT_QUERY_ABI
 *
 * type: Request type for which to check existence.
 */
struct cmd_iso_client_query_abi_request {
	uint32_t type;
} BPMP_ABI_PACKED;

/*
 * request data for request type CMD_ISO_CLIENT_CALCULATE_LA
 *
 * id: client ID in @ref bpmp_bwmgr_ids
 * bw: bw requested in kBps by client ID.
 * init_bw_floor: initial dram_bw_floor in kBps passed by client ID.
 * ISO client will perform mempool allocation and DVFS buffering based
 * on this dram_bw_floor.
 */
struct cmd_iso_client_calculate_la_request {
	uint32_t id;
	uint32_t bw;
	uint32_t init_bw_floor;
} BPMP_ABI_PACKED;

/*
 * request data for request type CMD_ISO_CLIENT_SET_LA
 *
 * id: client ID in @ref bpmp_bwmgr_ids
 * bw: bw requested in kBps by client ID.
 * final_bw_floor: final dram_bw_floor in kBps.
 * Sometimes the initial dram_bw_floor passed by ISO client may need to be
 * updated by considering higher dram freq's. This is the final dram_bw_floor
 * used to calculate and program MC registers.
 */
struct cmd_iso_client_set_la_request {
	uint32_t id;
	uint32_t bw;
	uint32_t final_bw_floor;
} BPMP_ABI_PACKED;

/*
 * request data for request type CMD_ISO_CLIENT_GET_MAX_BW
 *
 * id: client ID in @ref bpmp_bwmgr_ids
 */
struct cmd_iso_client_get_max_bw_request {
	uint32_t id;
} BPMP_ABI_PACKED;

/*
 * response data for request type CMD_ISO_CLIENT_CALCULATE_LA
 *
 * la_rate_floor: minimum dram_rate_floor in kHz at which a legal la is possible
 * iso_client_only_rate: Minimum dram freq in kHz required to satisfy this clients
 * iso bw request, assuming all other iso clients are inactive
 */
struct cmd_iso_client_calculate_la_response {
	uint32_t la_rate_floor;
	uint32_t iso_client_only_rate;
} BPMP_ABI_PACKED;

/**
 * @brief Used by @ref cmd_iso_client_get_max_bw_response
 */
struct iso_max_bw {
	/* @brief dram frequency in kHz */
	uint32_t freq;
	/* @brief max possible iso-bw in kBps */
	uint32_t iso_bw;
} BPMP_ABI_PACKED;

#define ISO_MAX_BW_MAX_SIZE	14U
/*
 * response data for request type CMD_ISO_CLIENT_GET_MAX_BW
 */
struct cmd_iso_client_get_max_bw_response {
	/* @brief The number valid entries in iso_max_bw pairs */
	uint32_t num_pairs;
	/* @brief max ISOBW <dram freq, max bw> information */
	struct iso_max_bw pairs[ISO_MAX_BW_MAX_SIZE];
} BPMP_ABI_PACKED;

/**
 * @brief Request with #MRQ_ISO_CLIENT
 *
 * Used by the sender of an #MRQ_ISO_CLIENT message.
 *
 * |sub-command                          |payload                                 |
 * |------------------------------------ |----------------------------------------|
 * |CMD_ISO_CLIENT_QUERY_ABI		 |cmd_iso_client_query_abi_request        |
 * |CMD_ISO_CLIENT_CALCULATE_LA		 |cmd_iso_client_calculate_la_request     |
 * |CMD_ISO_CLIENT_SET_LA		 |cmd_iso_client_set_la_request           |
 * |CMD_ISO_CLIENT_GET_MAX_BW		 |cmd_iso_client_get_max_bw_request       |
 *
 */

struct mrq_iso_client_request {
	/* Type of request. Values listed in enum mrq_iso_client_cmd */
	uint32_t cmd;
	union {
		struct cmd_iso_client_query_abi_request query_abi;
		struct cmd_iso_client_calculate_la_request calculate_la_req;
		struct cmd_iso_client_set_la_request set_la_req;
		struct cmd_iso_client_get_max_bw_request max_isobw_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response to MRQ_ISO_CLIENT
 *
 * Each sub-command supported by @ref mrq_iso_client_request may return
 * sub-command-specific data. Some do and some do not as indicated in
 * the following table
 *
 * |sub-command                  |payload                             |
 * |---------------------------- |------------------------------------|
 * |CMD_ISO_CLIENT_CALCULATE_LA  |cmd_iso_client_calculate_la_response|
 * |CMD_ISO_CLIENT_SET_LA        |N/A                                 |
 * |CMD_ISO_CLIENT_GET_MAX_BW    |cmd_iso_client_get_max_bw_response  |
 *
 */

struct mrq_iso_client_response {
	union {
		struct cmd_iso_client_calculate_la_response calculate_la_resp;
		struct cmd_iso_client_get_max_bw_response max_isobw_resp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} ISO_CLIENT */
/** @endcond (bpmp_t234 || bpmp_t239 || bpmp_th500) */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_CPU_NDIV_LIMITS
 * @brief CPU freq. limits in ndiv
 *
 * * Platforms: T194 onwards
 * @cond (bpmp_t194 || bpmp_t234 || bpmp_t239 || bpmp_th500)
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_cpu_ndiv_limits_request
 * * Response Payload: @ref mrq_cpu_ndiv_limits_response
 * @addtogroup CPU
 * @{
 */

/**
 * @brief Request for ndiv limits of a cluster
 */
struct mrq_cpu_ndiv_limits_request {
	/** @brief Enum cluster_id */
	uint32_t cluster_id;
} BPMP_ABI_PACKED;

/**
 * @brief Response to #MRQ_CPU_NDIV_LIMITS
 */
struct mrq_cpu_ndiv_limits_response {
	/** @brief Reference frequency in Hz */
	uint32_t ref_clk_hz;
	/** @brief Post divider value */
	uint16_t pdiv;
	/** @brief Input divider value */
	uint16_t mdiv;
	/** @brief FMAX expressed with max NDIV value */
	uint16_t ndiv_max;
	/** @brief Minimum allowed NDIV value */
	uint16_t ndiv_min;
} BPMP_ABI_PACKED;

/** @} CPU */
/** @endcond (bpmp_t194 || bpmp_t234 || bpmp_t239 || bpmp_th500) */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_CPU_AUTO_CC3
 * @brief Query CPU cluster auto-CC3 configuration
 *
 * * Platforms: T194
 * @cond bpmp_t194
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_cpu_auto_cc3_request
 * * Response Payload: @ref mrq_cpu_auto_cc3_response
 * @addtogroup CC3
 *
 * Queries from BPMP auto-CC3 configuration (allowed/not allowed) for a
 * specified cluster. CCPLEX s/w uses this information to override its own
 * device tree auto-CC3 settings, so that BPMP device tree is a single source of
 * auto-CC3 platform configuration.
 *
 * @{
 */

/**
 * @brief Request for auto-CC3 configuration of a cluster
 */
struct mrq_cpu_auto_cc3_request {
	/** @brief Enum cluster_id (logical cluster id, known to CCPLEX s/w) */
	uint32_t cluster_id;
} BPMP_ABI_PACKED;

/**
 * @brief Response to #MRQ_CPU_AUTO_CC3
 */
struct mrq_cpu_auto_cc3_response {
	/**
	 * @brief auto-CC3 configuration
	 *
	 * - bits[31..10] reserved.
	 * - bits[9..1] cc3 ndiv
	 * - bit [0] if "1" auto-CC3 is allowed, if "0" auto-CC3 is not allowed
	 */
	uint32_t auto_cc3_config;
} BPMP_ABI_PACKED;

/** @} CC3 */
/** @endcond bpmp_t194 */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_RINGBUF_CONSOLE
 * @brief A ring buffer debug console for BPMP
 * @addtogroup RingbufConsole
 *
 * The ring buffer debug console aims to be a substitute for the UART debug
 * console. The debug console is implemented with two ring buffers in the
 * BPMP-FW, the RX (receive) and TX (transmit) buffers. Characters can be read
 * and written to the buffers by the host via the MRQ interface.
 *
 * @{
 */

/**
 * @brief Maximum number of bytes transferred in a single write command to the
 * BPMP
 *
 * This is determined by the number of free bytes in the message struct,
 * rounded down to a multiple of four.
 */
#define MRQ_RINGBUF_CONSOLE_MAX_WRITE_LEN 112

/**
 * @brief Maximum number of bytes transferred in a single read command to the
 * BPMP
 *
 * This is determined by the number of free bytes in the message struct,
 * rounded down to a multiple of four.
 */
#define MRQ_RINGBUF_CONSOLE_MAX_READ_LEN 116

enum mrq_ringbuf_console_host_to_bpmp_cmd {
	/**
	 * @brief Check whether the BPMP driver supports the specified request
	 * type
	 *
	 * mrq_response::err is 0 if the specified request is supported and
	 * -#BPMP_ENODEV otherwise
	 */
	CMD_RINGBUF_CONSOLE_QUERY_ABI = 0,
	/**
	 * @brief Perform a read operation on the BPMP TX buffer
	 *
	 * mrq_response::err is 0
	 */
	CMD_RINGBUF_CONSOLE_READ = 1,
	/**
	 * @brief Perform a write operation on the BPMP RX buffer
	 *
	 * mrq_response::err is 0 if the operation was successful and
	 * -#BPMP_ENODEV otherwise
	 */
	CMD_RINGBUF_CONSOLE_WRITE = 2,
	/**
	 * @brief Get the length of the buffer and the physical addresses of
	 * the buffer data and the head and tail counters
	 *
	 * mrq_response::err is 0 if the operation was successful and
	 * -#BPMP_ENODEV otherwise
	 */
	CMD_RINGBUF_CONSOLE_GET_FIFO = 3,
};

/**
 * @ingroup RingbufConsole
 * @brief Host->BPMP request data for request type
 * #CMD_RINGBUF_CONSOLE_QUERY_ABI
 */
struct cmd_ringbuf_console_query_abi_req {
	/** @brief Command identifier to be queried */
	uint32_t cmd;
} BPMP_ABI_PACKED;

/** @private */
struct cmd_ringbuf_console_query_abi_resp {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief Host->BPMP request data for request type #CMD_RINGBUF_CONSOLE_READ
 */
struct cmd_ringbuf_console_read_req {
	/**
	 * @brief Number of bytes requested to be read from the BPMP TX buffer
	 */
	uint8_t len;
} BPMP_ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief BPMP->Host response data for request type #CMD_RINGBUF_CONSOLE_READ
 */
struct cmd_ringbuf_console_read_resp {
	/** @brief The actual data read from the BPMP TX buffer */
	uint8_t data[MRQ_RINGBUF_CONSOLE_MAX_READ_LEN];
	/** @brief Number of bytes in cmd_ringbuf_console_read_resp::data */
	uint8_t len;
} BPMP_ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief Host->BPMP request data for request type #CMD_RINGBUF_CONSOLE_WRITE
 */
struct cmd_ringbuf_console_write_req {
	/** @brief The actual data to be written to the BPMP RX buffer */
	uint8_t data[MRQ_RINGBUF_CONSOLE_MAX_WRITE_LEN];
	/** @brief Number of bytes in cmd_ringbuf_console_write_req::data */
	uint8_t len;
} BPMP_ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief BPMP->Host response data for request type #CMD_RINGBUF_CONSOLE_WRITE
 */
struct cmd_ringbuf_console_write_resp {
	/** @brief Number of bytes of available space in the BPMP RX buffer */
	uint32_t space_avail;
	/** @brief Number of bytes that were written to the BPMP RX buffer */
	uint8_t len;
} BPMP_ABI_PACKED;

/** @private */
struct cmd_ringbuf_console_get_fifo_req {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief BPMP->Host reply data for request type #CMD_RINGBUF_CONSOLE_GET_FIFO
 */
struct cmd_ringbuf_console_get_fifo_resp {
	/** @brief Physical address of the BPMP TX buffer */
	uint64_t bpmp_tx_buf_addr;
	/** @brief Physical address of the BPMP TX buffer head counter */
	uint64_t bpmp_tx_head_addr;
	/** @brief Physical address of the BPMP TX buffer tail counter */
	uint64_t bpmp_tx_tail_addr;
	/** @brief Length of the BPMP TX buffer */
	uint32_t bpmp_tx_buf_len;
} BPMP_ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief Host->BPMP request data.
 *
 * Reply type is union #mrq_ringbuf_console_bpmp_to_host_response .
 */
struct mrq_ringbuf_console_host_to_bpmp_request {
	/**
	 * @brief Type of request. Values listed in enum
	 * #mrq_ringbuf_console_host_to_bpmp_cmd.
	 */
	uint32_t type;
	/** @brief  request type specific parameters. */
	union {
		struct cmd_ringbuf_console_query_abi_req query_abi;
		struct cmd_ringbuf_console_read_req read;
		struct cmd_ringbuf_console_write_req write;
		struct cmd_ringbuf_console_get_fifo_req get_fifo;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief Host->BPMP reply data
 *
 * In response to struct #mrq_ringbuf_console_host_to_bpmp_request.
 */
union mrq_ringbuf_console_bpmp_to_host_response {
	struct cmd_ringbuf_console_query_abi_resp query_abi;
	struct cmd_ringbuf_console_read_resp read;
	struct cmd_ringbuf_console_write_resp write;
	struct cmd_ringbuf_console_get_fifo_resp get_fifo;
} BPMP_ABI_PACKED;

/** @} RingbufConsole */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_STRAP
 * @brief Set a strap value controlled by BPMP
 *
 * * Platforms: T194 onwards
 * @cond (bpmp_t194 || bpmp_t234 || bpmp_t239 || bpmp_th500)
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_strap_request
 * * Response Payload: N/A
 * @addtogroup Strap
 *
 * A strap is an input that is sampled by a hardware unit during the
 * unit's startup process. The sampled value of a strap affects the
 * behavior of the unit until the unit is restarted. Many hardware
 * units sample their straps at the instant that their resets are
 * deasserted.
 *
 * BPMP owns registers which act as straps to various units. It
 * exposes limited control of those straps via #MRQ_STRAP.
 *
 * @{
 */
enum mrq_strap_cmd {
	/** @private */
	STRAP_RESERVED = 0,
	/** @brief Set a strap value */
	STRAP_SET = 1
};

/**
 * @brief Request with #MRQ_STRAP
 */
struct mrq_strap_request {
	/** @brief @ref mrq_strap_cmd */
	uint32_t cmd;
	/** @brief Strap ID from @ref Strap_Identifiers */
	uint32_t id;
	/** @brief Desired value for strap (if cmd is #STRAP_SET) */
	uint32_t value;
} BPMP_ABI_PACKED;

/** @} Strap */
/** @endcond (bpmp_t194 || bpmp_t234 || bpmp_t239 || bpmp_th500) */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_UPHY
 * @brief Perform a UPHY operation
 *
 * * Platforms: T194 onwards
 * @cond (bpmp_t194 || bpmp_t234 || bpmp_t239 || bpmp_th500)
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_uphy_request
 * * Response Payload: @ref mrq_uphy_response
 *
 * @addtogroup UPHY
 * @{
 */
enum {
	CMD_UPHY_PCIE_LANE_MARGIN_CONTROL = 1,
	CMD_UPHY_PCIE_LANE_MARGIN_STATUS = 2,
	CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT = 3,
	CMD_UPHY_PCIE_CONTROLLER_STATE = 4,
	CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF = 5,
	CMD_UPHY_DISPLAY_PORT_INIT = 6,
	CMD_UPHY_DISPLAY_PORT_OFF = 7,
	CMD_UPHY_XUSB_DYN_LANES_RESTORE = 8,
	CMD_UPHY_MAX,
};

struct cmd_uphy_margin_control_request {
	/** @brief Enable margin */
	int32_t en;
	/** @brief Clear the number of error and sections */
	int32_t clr;
	/** @brief Set x offset (1's complement) for left/right margin type (y should be 0) */
	uint32_t x;
	/** @brief Set y offset (1's complement) for left/right margin type (x should be 0) */
	uint32_t y;
	/** @brief Set number of bit blocks for each margin section */
	uint32_t nblks;
} BPMP_ABI_PACKED;

struct cmd_uphy_margin_status_response {
	/** @brief Number of errors observed */
	uint32_t status;
} BPMP_ABI_PACKED;

struct cmd_uphy_ep_controller_pll_init_request {
	/** @brief EP controller number, T194 valid: 0, 4, 5; T234 valid: 5, 6, 7, 10; T239 valid: 0 */
	uint8_t ep_controller;
} BPMP_ABI_PACKED;

struct cmd_uphy_pcie_controller_state_request {
	/** @brief PCIE controller number, T194 valid: 0-4; T234 valid: 0-10; T239 valid: 0-3 */
	uint8_t pcie_controller;
	uint8_t enable;
} BPMP_ABI_PACKED;

struct cmd_uphy_ep_controller_pll_off_request {
	/** @brief EP controller number, T194 valid: 0, 4, 5; T234 valid: 5, 6, 7, 10; T239 valid: 0 */
	uint8_t ep_controller;
} BPMP_ABI_PACKED;

struct cmd_uphy_display_port_init_request {
	/** @brief DisplayPort link rate, T239 valid: 1620, 2700, 5400, 8100, 2160, 2430, 3240, 4320, 6750 */
	uint16_t link_rate;
	/** @brief 1: lane 0; 2: lane 1; 3: lane 0 and 1 */
	uint16_t lanes_bitmap;
} BPMP_ABI_PACKED;

struct cmd_uphy_xusb_dyn_lanes_restore_request {
	/** @brief 1: lane 0; 2: lane 1; 3: lane 0 and 1 */
	uint16_t lanes_bitmap;
} BPMP_ABI_PACKED;

/**
 * @ingroup UPHY
 * @brief Request with #MRQ_UPHY
 *
 * Used by the sender of an #MRQ_UPHY message to control UPHY.
 * The uphy_request is split into several sub-commands. CMD_UPHY_PCIE_LANE_MARGIN_STATUS
 * requires no additional data. Others have a sub-command specific payload. Below table
 * shows sub-commands with their corresponding payload data.
 *
 * |sub-command                          |payload                                 |
 * |------------------------------------ |----------------------------------------|
 * |CMD_UPHY_PCIE_LANE_MARGIN_CONTROL    |uphy_set_margin_control                 |
 * |CMD_UPHY_PCIE_LANE_MARGIN_STATUS     |                                        |
 * |CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT |cmd_uphy_ep_controller_pll_init_request |
 * |CMD_UPHY_PCIE_CONTROLLER_STATE       |cmd_uphy_pcie_controller_state_request  |
 * |CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF  |cmd_uphy_ep_controller_pll_off_request  |
 * |CMD_UPHY_PCIE_DISPLAY_PORT_INIT      |cmd_uphy_display_port_init_request      |
 * |CMD_UPHY_PCIE_DISPLAY_PORT_OFF       |                                        |
 * |CMD_UPHY_XUSB_DYN_LANES_RESTORE      |cmd_uphy_xusb_dyn_lanes_restore_request |
 *
 */

struct mrq_uphy_request {
	/** @brief Lane number. */
	uint16_t lane;
	/** @brief Sub-command id. */
	uint16_t cmd;

	union {
		struct cmd_uphy_margin_control_request uphy_set_margin_control;
		struct cmd_uphy_ep_controller_pll_init_request ep_ctrlr_pll_init;
		struct cmd_uphy_pcie_controller_state_request controller_state;
		struct cmd_uphy_ep_controller_pll_off_request ep_ctrlr_pll_off;
		struct cmd_uphy_display_port_init_request display_port_init;
		struct cmd_uphy_xusb_dyn_lanes_restore_request xusb_dyn_lanes_restore;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @ingroup UPHY
 * @brief Response to MRQ_UPHY
 *
 * Each sub-command supported by @ref mrq_uphy_request may return
 * sub-command-specific data. Some do and some do not as indicated in
 * the following table
 *
 * |sub-command                       |payload                 |
 * |----------------------------      |------------------------|
 * |CMD_UPHY_PCIE_LANE_MARGIN_CONTROL |                        |
 * |CMD_UPHY_PCIE_LANE_MARGIN_STATUS  |uphy_get_margin_status  |
 *
 */

struct mrq_uphy_response {
	union {
		struct cmd_uphy_margin_status_response uphy_get_margin_status;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} UPHY */
/** @endcond (bpmp_t194 || bpmp_t234 || bpmp_t239 || bpmp_th500) */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_FMON
 * @brief Perform a frequency monitor configuration operations
 *
 * * Platforms: T194 onwards
 * @cond (bpmp_t194 || bpmp_t234 || bpmp_t239 || bpmp_th500)
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_fmon_request
 * * Response Payload: @ref mrq_fmon_response
 * @endcond (bpmp_t194 || bpmp_t234 || bpmp_t239 || bpmp_th500)
 *
 * @addtogroup FMON
 * @{
 * @cond (bpmp_t194 || bpmp_t234)
 */
enum {
	/**
	 * @brief Clamp FMON configuration to specified rate.
	 *
	 * The monitored clock must be running for clamp to succeed. If
	 * clamped, FMON configuration is preserved when clock rate
	 * and/or state is changed.
	 *
	 * mrq_response::err is 0 if the operation was successful, or @n
	 * -#BPMP_EACCES: FMON access error @n
	 * -#BPMP_EBADCMD if subcommand is not supported @n
	 * -#BPMP_EBADSLT: clamp FMON on cluster with auto-CC3 enabled @n
	 * -#BPMP_EBUSY: fmon is already clamped at different rate @n
	 * -#BPMP_EFAULT: self-diagnostic error @n
	 * -#BPMP_EINVAL: invalid FMON configuration @n
	 * -#BPMP_EOPNOTSUPP: not in production mode @n
	 * -#BPMP_ENODEV: invalid clk_id @n
	 * -#BPMP_ENOENT: no calibration data, uninitialized @n
	 * -#BPMP_ENOTSUP: avfs config not set @n
	 * -#BPMP_ENOSYS: clamp FMON on cluster clock w/ no NAFLL @n
	 * -#BPMP_ETIMEDOUT: operation timed out @n
	 */
	CMD_FMON_GEAR_CLAMP = 1,
	/**
	 * @brief Release clamped FMON configuration.
	 *
	 * Allow FMON configuration to follow monitored clock rate
	 * and/or state changes.
	 *
	 * mrq_response::err is 0 if the operation was successful, or @n
	 * -#BPMP_EBADCMD if subcommand is not supported @n
	 * -#BPMP_ENODEV: invalid clk_id @n
	 * -#BPMP_ENOENT: no calibration data, uninitialized @n
	 * -#BPMP_ENOTSUP: avfs config not set @n
	 * -#BPMP_EOPNOTSUPP: not in production mode @n
	 */
	CMD_FMON_GEAR_FREE = 2,
	/**
	 * @brief Return rate FMON is clamped at, or 0 if FMON is not
	 *         clamped.
	 *
	 * Inherently racy, since clamp state can be changed
	 * concurrently. Useful for testing.
	 *
	 * mrq_response::err is 0 if the operation was successful, or @n
	 * -#BPMP_EBADCMD if subcommand is not supported @n
	 * -#BPMP_ENODEV: invalid clk_id @n
	 * -#BPMP_ENOENT: no calibration data, uninitialized @n
	 * -#BPMP_ENOTSUP: avfs config not set @n
	 * -#BPMP_EOPNOTSUPP: not in production mode @n
	 */
	CMD_FMON_GEAR_GET = 3,
	/**
	 * @brief Return current status of FMON faults detected by FMON
	 *         h/w or s/w since last invocation of this command.
	 *         Clears fault status.
	 *
	 * mrq_response::err is 0 if the operation was successful, or @n
	 * -#BPMP_EBADCMD if subcommand is not supported @n
	 * -#BPMP_EINVAL: invalid fault type @n
	 * -#BPMP_ENODEV: invalid clk_id @n
	 * -#BPMP_ENOENT: no calibration data, uninitialized @n
	 * -#BPMP_ENOTSUP: avfs config not set @n
	 * -#BPMP_EOPNOTSUPP: not in production mode @n
	 */
	CMD_FMON_FAULT_STS_GET = 4,
};

/**
 * @cond DEPRECATED
 * Kept for backward compatibility
 */
#define CMD_FMON_NUM		4

/** @endcond DEPRECATED */

/**
 * @defgroup fmon_fault_type FMON fault type
 * @addtogroup fmon_fault_type
 * @{
 */
/** @brief All detected FMON faults (h/w or s/w) */
#define FMON_FAULT_TYPE_ALL		0U
/** @brief FMON faults detected by h/w */
#define FMON_FAULT_TYPE_HW		1U
/** @brief FMON faults detected by s/w */
#define FMON_FAULT_TYPE_SW		2U

/** @} fmon_fault_type */


struct cmd_fmon_gear_clamp_request {
	int32_t unused;
	int64_t rate;
} BPMP_ABI_PACKED;

/** @private */
struct cmd_fmon_gear_clamp_response {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/** @private */
struct cmd_fmon_gear_free_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/** @private */
struct cmd_fmon_gear_free_response {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/** @private */
struct cmd_fmon_gear_get_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

struct cmd_fmon_gear_get_response {
	int64_t rate;
} BPMP_ABI_PACKED;

struct cmd_fmon_fault_sts_get_request {
	uint32_t fault_type;	/**< @ref fmon_fault_type */
} BPMP_ABI_PACKED;

struct cmd_fmon_fault_sts_get_response {
	uint32_t fault_sts;
} BPMP_ABI_PACKED;

/**
 * @ingroup FMON
 * @brief Request with #MRQ_FMON
 *
 * Used by the sender of an #MRQ_FMON message to configure clock
 * frequency monitors. The FMON request is split into several
 * sub-commands. Some sub-commands require no additional data.
 * Others have a sub-command specific payload
 *
 * |sub-command                 |payload                |
 * |----------------------------|-----------------------|
 * |CMD_FMON_GEAR_CLAMP         |fmon_gear_clamp        |
 * |CMD_FMON_GEAR_FREE          |-                      |
 * |CMD_FMON_GEAR_GET           |-                      |
 * |CMD_FMON_FAULT_STS_GET      |fmon_fault_sts_get     |
 *
 */
struct mrq_fmon_request {
	/** @brief Sub-command and clock id concatenated to 32-bit word.
	 * - bits[31..24] is the sub-cmd.
	 * - bits[23..0] is monitored clock id used to select target
	 *   FMON
	 */
	uint32_t cmd_and_id;

	union {
		struct cmd_fmon_gear_clamp_request fmon_gear_clamp;
		/** @private */
		struct cmd_fmon_gear_free_request fmon_gear_free;
		/** @private */
		struct cmd_fmon_gear_get_request fmon_gear_get;
		struct cmd_fmon_fault_sts_get_request fmon_fault_sts_get;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @ingroup FMON
 * @brief Response to MRQ_FMON
 *
 * Each sub-command supported by @ref mrq_fmon_request may
 * return sub-command-specific data as indicated below.
 *
 * |sub-command                 |payload                 |
 * |----------------------------|------------------------|
 * |CMD_FMON_GEAR_CLAMP         |-                       |
 * |CMD_FMON_GEAR_FREE          |-                       |
 * |CMD_FMON_GEAR_GET           |fmon_gear_get           |
 * |CMD_FMON_FAULT_STS_GET      |fmon_fault_sts_get      |
 *
 */

struct mrq_fmon_response {
	union {
		/** @private */
		struct cmd_fmon_gear_clamp_response fmon_gear_clamp;
		/** @private */
		struct cmd_fmon_gear_free_response fmon_gear_free;
		struct cmd_fmon_gear_get_response fmon_gear_get;
		struct cmd_fmon_fault_sts_get_response fmon_fault_sts_get;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @endcond (bpmp_t194 || bpmp_t234) */
/** @} FMON */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_EC
 * @brief Provide status information on faults reported by Error
 *        Collator (EC) to HSM.
 *
 * * Platforms: T194
 * @cond bpmp_t194
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_ec_request
 * * Response Payload: @ref mrq_ec_response
 *
 * @note This MRQ ABI is under construction, and subject to change
 *
 * @endcond bpmp_t194
 * @addtogroup EC
 * @{
 * @cond bpmp_t194
 */
enum {
	/**
	 * @cond DEPRECATED
	 * @brief Retrieve specified EC status.
	 *
	 * mrq_response::err is 0 if the operation was successful, or @n
	 * -#BPMP_ENODEV if target EC is not owned by BPMP @n
	 * -#BPMP_EACCES if target EC power domain is turned off @n
	 * -#BPMP_EBADCMD if subcommand is not supported
	 * @endcond DEPRECATED
	 */
	CMD_EC_STATUS_GET = 1,	/* deprecated */

	/**
	 * @brief Retrieve specified EC extended status (includes error
	 *        counter and user values).
	 *
	 * mrq_response::err is 0 if the operation was successful, or @n
	 * -#BPMP_ENODEV if target EC is not owned by BPMP @n
	 * -#BPMP_EACCES if target EC power domain is turned off @n
	 * -#BPMP_EBADCMD if subcommand is not supported
	 */
	CMD_EC_STATUS_EX_GET = 2,
	CMD_EC_NUM,
};

/** @brief BPMP ECs error types */
enum bpmp_ec_err_type {
	/** @brief Parity error on internal data path
	 *
	 *  Error descriptor @ref ec_err_simple_desc.
	 */
	EC_ERR_TYPE_PARITY_INTERNAL		= 1,

	/** @brief ECC SEC error on internal data path
	 *
	 *  Error descriptor @ref ec_err_simple_desc.
	 */
	EC_ERR_TYPE_ECC_SEC_INTERNAL		= 2,

	/** @brief ECC DED error on internal data path
	 *
	 *  Error descriptor @ref ec_err_simple_desc.
	 */
	EC_ERR_TYPE_ECC_DED_INTERNAL		= 3,

	/** @brief Comparator error
	 *
	 *  Error descriptor @ref ec_err_simple_desc.
	 */
	EC_ERR_TYPE_COMPARATOR			= 4,

	/** @brief Register parity error
	 *
	 *  Error descriptor @ref ec_err_reg_parity_desc.
	 */
	EC_ERR_TYPE_REGISTER_PARITY		= 5,

	/** @brief Parity error from on-chip SRAM/FIFO
	 *
	 *  Error descriptor @ref ec_err_simple_desc.
	 */
	EC_ERR_TYPE_PARITY_SRAM			= 6,

	/** @brief Clock Monitor error
	 *
	 *  Error descriptor @ref ec_err_fmon_desc.
	 */
	EC_ERR_TYPE_CLOCK_MONITOR		= 9,

	/** @brief Voltage Monitor error
	 *
	 *  Error descriptor @ref ec_err_vmon_desc.
	 */
	EC_ERR_TYPE_VOLTAGE_MONITOR		= 10,

	/** @brief SW Correctable error
	 *
	 *  Error descriptor @ref ec_err_sw_error_desc.
	 */
	EC_ERR_TYPE_SW_CORRECTABLE		= 16,

	/** @brief SW Uncorrectable error
	 *
	 *  Error descriptor @ref ec_err_sw_error_desc.
	 */
	EC_ERR_TYPE_SW_UNCORRECTABLE		= 17,

	/** @brief Other HW Correctable error
	 *
	 *  Error descriptor @ref ec_err_simple_desc.
	 */
	EC_ERR_TYPE_OTHER_HW_CORRECTABLE	= 32,

	/** @brief Other HW Uncorrectable error
	 *
	 *  Error descriptor @ref ec_err_simple_desc.
	 */
	EC_ERR_TYPE_OTHER_HW_UNCORRECTABLE	= 33,
};

/** @brief Group of registers with parity error. */
enum ec_registers_group {
	/** @brief Functional registers group */
	EC_ERR_GROUP_FUNC_REG		= 0U,
	/** @brief SCR registers group */
	EC_ERR_GROUP_SCR_REG		= 1U,
};

/**
 * @defgroup bpmp_ec_status_flags EC Status Flags
 * @addtogroup bpmp_ec_status_flags
 * @{
 */
/** @brief No EC error found flag */
#define EC_STATUS_FLAG_NO_ERROR		0x0001U
/** @brief Last EC error found flag */
#define EC_STATUS_FLAG_LAST_ERROR	0x0002U
/** @brief EC latent error flag */
#define EC_STATUS_FLAG_LATENT_ERROR	0x0004U

/** @} bpmp_ec_status_flags */

/**
 * @defgroup bpmp_ec_desc_flags EC Descriptor Flags
 * @addtogroup bpmp_ec_desc_flags
 * @{
 */
/** @brief EC descriptor error resolved flag */
#define EC_DESC_FLAG_RESOLVED		0x0001U
/** @brief EC descriptor failed to retrieve id flag */
#define EC_DESC_FLAG_NO_ID		0x0002U

/** @} bpmp_ec_desc_flags */

/**
 * |error type                       | fmon_clk_id values        |
 * |---------------------------------|---------------------------|
 * |@ref EC_ERR_TYPE_CLOCK_MONITOR   |@ref bpmp_clock_ids        |
 */
struct ec_err_fmon_desc {
	/** @brief Bitmask of @ref bpmp_ec_desc_flags  */
	uint16_t desc_flags;
	/** @brief FMON monitored clock id */
	uint16_t fmon_clk_id;
	/**
	 * @brief Bitmask of fault flags
	 *
	 * @ref bpmp_fmon_faults_flags
	 */
	uint32_t fmon_faults;
	/** @brief FMON faults access error */
	int32_t fmon_access_error;
} BPMP_ABI_PACKED;

/**
 * | error type                      | vmon_adc_id values        |
 * |---------------------------------|---------------------------|
 * |@ref EC_ERR_TYPE_VOLTAGE_MONITOR |@ref bpmp_adc_ids          |
 */
struct ec_err_vmon_desc {
	/** @brief Bitmask of @ref bpmp_ec_desc_flags  */
	uint16_t desc_flags;
	/** @brief VMON rail adc id */
	uint16_t vmon_adc_id;
	/** @brief Bitmask of bpmp_vmon_faults_flags */
	uint32_t vmon_faults;
	/** @brief VMON faults access error */
	int32_t vmon_access_error;
} BPMP_ABI_PACKED;

/**
 * |error type                       | reg_id values         |
 * |---------------------------------|-----------------------|
 * |@ref EC_ERR_TYPE_REGISTER_PARITY | bpmp_ec_registers_ids |
 */
struct ec_err_reg_parity_desc {
	/** @brief Bitmask of @ref bpmp_ec_desc_flags  */
	uint16_t desc_flags;
	/** @brief Register id */
	uint16_t reg_id;
	/** @brief Register group @ref ec_registers_group */
	uint16_t reg_group;
} BPMP_ABI_PACKED;

/**
 * |error type                        | err_source_id values |
 * |--------------------------------- |----------------------|
 * |@ref EC_ERR_TYPE_SW_CORRECTABLE   | bpmp_ec_ce_swd_ids   |
 * |@ref EC_ERR_TYPE_SW_UNCORRECTABLE | bpmp_ec_ue_swd_ids   |
 */
struct ec_err_sw_error_desc {
	/** @brief Bitmask of @ref bpmp_ec_desc_flags  */
	uint16_t desc_flags;
	/** @brief Error source id */
	uint16_t err_source_id;
	/** @brief Sw error data */
	uint32_t sw_error_data;
} BPMP_ABI_PACKED;

/**
 * |error type                              | err_source_id values   |
 * |----------------------------------------|------------------------|
 * |@ref EC_ERR_TYPE_PARITY_INTERNAL        |  bpmp_ec_ipath_ids     |
 * |@ref EC_ERR_TYPE_ECC_SEC_INTERNAL       |  bpmp_ec_ipath_ids     |
 * |@ref EC_ERR_TYPE_ECC_DED_INTERNAL       |  bpmp_ec_ipath_ids     |
 * |@ref EC_ERR_TYPE_COMPARATOR             |  bpmp_ec_comparator_ids|
 * |@ref EC_ERR_TYPE_OTHER_HW_CORRECTABLE   |  bpmp_ec_misc_hwd_ids  |
 * |@ref EC_ERR_TYPE_OTHER_HW_UNCORRECTABLE |  bpmp_ec_misc_hwd_ids  |
 * |@ref EC_ERR_TYPE_PARITY_SRAM            |  bpmp_clock_ids        |
 */
struct ec_err_simple_desc {
	/** @brief Bitmask of @ref bpmp_ec_desc_flags  */
	uint16_t desc_flags;
	/** @brief Error source id. Id space depends on error type. */
	uint16_t err_source_id;
} BPMP_ABI_PACKED;

/** @brief Union of EC error descriptors */
union ec_err_desc {
	struct ec_err_fmon_desc fmon_desc;
	struct ec_err_vmon_desc vmon_desc;
	struct ec_err_reg_parity_desc reg_parity_desc;
	struct ec_err_sw_error_desc sw_error_desc;
	struct ec_err_simple_desc simple_desc;
} BPMP_ABI_PACKED;

struct cmd_ec_status_get_request {
	/** @brief HSM error line number that identifies target EC. */
	uint32_t ec_hsm_id;
} BPMP_ABI_PACKED;

/** EC status maximum number of descriptors */
#define EC_ERR_STATUS_DESC_MAX_NUM	4U

/**
 * @cond DEPRECATED
 */
struct cmd_ec_status_get_response {
	/** @brief Target EC id (the same id received with request). */
	uint32_t ec_hsm_id;
	/**
	 * @brief Bitmask of @ref bpmp_ec_status_flags
	 *
	 * If NO_ERROR flag is set, error_ fields should be ignored
	 */
	uint32_t ec_status_flags;
	/** @brief Found EC error index. */
	uint32_t error_idx;
	/** @brief  Found EC error type @ref bpmp_ec_err_type. */
	uint32_t error_type;
	/** @brief  Number of returned EC error descriptors */
	uint32_t error_desc_num;
	/** @brief  EC error descriptors */
	union ec_err_desc error_descs[EC_ERR_STATUS_DESC_MAX_NUM];
} BPMP_ABI_PACKED;
/** @endcond DEPRECATED */

struct cmd_ec_status_ex_get_response {
	/** @brief Target EC id (the same id received with request). */
	uint32_t ec_hsm_id;
	/**
	 * @brief Bitmask of @ref bpmp_ec_status_flags
	 *
	 * If NO_ERROR flag is set, error_ fields should be ignored
	 */
	uint32_t ec_status_flags;
	/** @brief Found EC error index. */
	uint32_t error_idx;
	/** @brief  Found EC error type @ref bpmp_ec_err_type. */
	uint32_t error_type;
	/** @brief  Found EC mission error counter value */
	uint32_t error_counter;
	/** @brief  Found EC mission error user value */
	uint32_t error_uval;
	/** @brief  Reserved entry    */
	uint32_t reserved;
	/** @brief  Number of returned EC error descriptors */
	uint32_t error_desc_num;
	/** @brief  EC error descriptors */
	union ec_err_desc error_descs[EC_ERR_STATUS_DESC_MAX_NUM];
} BPMP_ABI_PACKED;

/**
 * @ingroup EC
 * @brief Request with #MRQ_EC
 *
 * Used by the sender of an #MRQ_EC message to access ECs owned
 * by BPMP.
 *
 * @cond DEPRECATED
 * |sub-command                 |payload                |
 * |----------------------------|-----------------------|
 * |@ref CMD_EC_STATUS_GET      |ec_status_get          |
 * @endcond DEPRECATED
 *
 * |sub-command                 |payload                |
 * |----------------------------|-----------------------|
 * |@ref CMD_EC_STATUS_EX_GET   |ec_status_get          |
 *
 */

struct mrq_ec_request {
	/** @brief Sub-command id. */
	uint32_t cmd_id;

	union {
		struct cmd_ec_status_get_request ec_status_get;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @ingroup EC
 * @brief Response to MRQ_EC
 *
 * Each sub-command supported by @ref mrq_ec_request may return
 * sub-command-specific data as indicated below.
 *
 * @cond DEPRECATED
 * |sub-command                 |payload                 |
 * |----------------------------|------------------------|
 * |@ref CMD_EC_STATUS_GET      |ec_status_get           |
 * @endcond DEPRECATED
 *
 * |sub-command                 |payload                 |
 * |----------------------------|------------------------|
 * |@ref CMD_EC_STATUS_EX_GET   |ec_status_ex_get        |
 *
 */

struct mrq_ec_response {
	union {
		/**
		 * @cond DEPRECATED
		 */
		struct cmd_ec_status_get_response ec_status_get;
		/** @endcond DEPRECATED */
		struct cmd_ec_status_ex_get_response ec_status_ex_get;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @endcond bpmp_t194 */
/** @} EC */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_TELEMETRY
 * @brief Get address of memory buffer refreshed with recently sampled
 *        telemetry data
 *
 * * Platforms: TH500 onwards
 * @cond bpmp_th500
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: N/A
 * * Response Payload: @ref mrq_telemetry_response
 * @addtogroup Telemetry
 * @{
 */

/**
 * @brief Response to #MRQ_TELEMETRY
 *
 * mrq_response::err is
 * * 0: Telemetry data is available at returned address
 * * -#BPMP_EACCES: MRQ master is not allowed to request buffer refresh
 * * -#BPMP_ENAVAIL: Telemetry buffer cannot be refreshed via this MRQ channel
 * * -#BPMP_ENOTSUP: Telemetry buffer is not supported by BPMP-FW
 * * -#BPMP_ENODEV: Telemetry mrq is not supported by BPMP-FW
 */
struct mrq_telemetry_response {
	/** @brief Physical address of telemetry data buffer */
	uint64_t data_buf_addr;	/**< see @ref bpmp_telemetry_layout */
} BPMP_ABI_PACKED;

/** @} Telemetry */
/** @endcond bpmp_th500 */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_PWR_LIMIT
 * @brief Control power limits.
 *
 * * Platforms: TH500 onwards
 * @cond bpmp_th500
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_pwr_limit_request
 * * Response Payload: @ref mrq_pwr_limit_response
 *
 * @addtogroup Pwrlimit
 * @{
 */
enum mrq_pwr_limit_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * command
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_PWR_LIMIT_QUERY_ABI = 0,

	/**
	 * @brief Set power limit
	 *
	 * mrq_response:err is
	 * * 0: Success
	 * * -#BPMP_ENODEV: Pwr limit mrq is not supported by BPMP-FW
	 * * -#BPMP_ENAVAIL: Invalid request parameters
	 * * -#BPMP_EACCES: Request is not accepted
	 */
	CMD_PWR_LIMIT_SET = 1,

	/**
	 * @brief Get power limit setting
	 *
	 * mrq_response:err is
	 * * 0: Success
	 * * -#BPMP_ENODEV: Pwr limit mrq is not supported by BPMP-FW
	 * * -#BPMP_ENAVAIL: Invalid request parameters
	 */
	CMD_PWR_LIMIT_GET = 2,

	/**
	 * @brief Get current power cap
	 *
	 * mrq_response:err is
	 * * 0: Success
	 * * -#BPMP_ENODEV: Pwr limit mrq is not supported by BPMP-FW
	 * * -#BPMP_ENAVAIL: Invalid request parameters
	 */
	CMD_PWR_LIMIT_CURR_CAP = 3,
};

/**
 * @defgroup bpmp_pwr_limit_type PWR_LIMIT TYPEs
 * @{
 */
/** @brief Limit value specifies traget cap */
#define PWR_LIMIT_TYPE_TARGET_CAP		0U
/** @brief Limit value specifies maximum possible target cap */
#define PWR_LIMIT_TYPE_BOUND_MAX		1U
/** @brief Limit value specifies minimum possible target cap */
#define PWR_LIMIT_TYPE_BOUND_MIN		2U
/** @brief Number of limit types supported by mrq interface */
#define PWR_LIMIT_TYPE_NUM			3U

/** @} bpmp_pwr_limit_type */

/**
 * @brief Request data for #MRQ_PWR_LIMIT command CMD_PWR_LIMIT_QUERY_ABI
 */
struct cmd_pwr_limit_query_abi_request {
	uint32_t cmd_code; /**< @ref mrq_pwr_limit_cmd */
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_PWR_LIMIT command CMD_PWR_LIMIT_SET
 *
 * Set specified limit of specified type from specified source. The success of
 * the request means that specified value is accepted as input to arbitration
 * with other sources settings for the same limit of the same type. Zero limit
 * is ignored by the arbitration (i.e., indicates "no limit set").
 */
struct cmd_pwr_limit_set_request {
	uint32_t limit_id;   /**< @ref bpmp_pwr_limit_id */
	uint32_t limit_src;  /**< @ref bpmp_pwr_limit_src */
	uint32_t limit_type; /**< @ref bpmp_pwr_limit_type */
	uint32_t limit_setting;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_PWR_LIMIT command CMD_PWR_LIMIT_GET
 *
 * Get previously set from specified source specified limit value of specified
 * type.
 */
struct cmd_pwr_limit_get_request {
	uint32_t limit_id;   /**< @ref bpmp_pwr_limit_id */
	uint32_t limit_src;  /**< @ref bpmp_pwr_limit_src */
	uint32_t limit_type; /**< @ref bpmp_pwr_limit_type */
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_PWR_LIMIT command CMD_PWR_LIMIT_GET
 */
struct cmd_pwr_limit_get_response {
	uint32_t limit_setting;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_PWR_LIMIT command CMD_PWR_LIMIT_CURR_CAP
 *
 * For specified limit get current power cap aggregated from all sources.
 */
struct cmd_pwr_limit_curr_cap_request {
	uint32_t limit_id;   /**< @ref bpmp_pwr_limit_id */
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_PWR_LIMIT command CMD_PWR_LIMIT_CURR_CAP
 */
struct cmd_pwr_limit_curr_cap_response {
	uint32_t curr_cap;
} BPMP_ABI_PACKED;

/**
 * @brief Request with #MRQ_PWR_LIMIT
 *
 * |sub-command                 |payload                          |
 * |----------------------------|---------------------------------|
 * |CMD_PWR_LIMIT_QUERY_ABI     | cmd_pwr_limit_query_abi_request |
 * |CMD_PWR_LIMIT_SET           | cmd_pwr_limit_set_request       |
 * |CMD_PWR_LIMIT_GET           | cmd_pwr_limit_get_request       |
 * |CMD_PWR_LIMIT_CURR_CAP      | cmd_pwr_limit_curr_cap_request  |
 */
struct mrq_pwr_limit_request {
	uint32_t cmd;
	union {
		struct cmd_pwr_limit_query_abi_request pwr_limit_query_abi_req;
		struct cmd_pwr_limit_set_request pwr_limit_set_req;
		struct cmd_pwr_limit_get_request pwr_limit_get_req;
		struct cmd_pwr_limit_curr_cap_request pwr_limit_curr_cap_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response to MRQ_PWR_LIMIT
 *
 * |sub-command                 |payload                          |
 * |----------------------------|---------------------------------|
 * |CMD_PWR_LIMIT_QUERY_ABI     | -                               |
 * |CMD_PWR_LIMIT_SET           | -                               |
 * |CMD_PWR_LIMIT_GET           | cmd_pwr_limit_get_response      |
 * |CMD_PWR_LIMIT_CURR_CAP      | cmd_pwr_limit_curr_cap_response |
 */
struct mrq_pwr_limit_response {
	union {
		struct cmd_pwr_limit_get_response pwr_limit_get_rsp;
		struct cmd_pwr_limit_curr_cap_response pwr_limit_curr_cap_rsp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} PwrLimit */
/** @endcond bpmp_th500 */


/**
 * @ingroup MRQ_Codes
 * @def MRQ_GEARS
 * @brief Get thresholds for NDIV offset switching
 *
 * * Platforms: TH500 onwards
 * @cond bpmp_th500
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: N/A
 * * Response Payload: @ref mrq_gears_response
 * @addtogroup Gears
 * @{
 */

/**
 * @brief Response to #MRQ_GEARS
 *
 * Used by the sender of an #MRQ_GEARS message to request thresholds
 * for NDIV offset switching.
 *
 * The mrq_gears_response::ncpu array defines four thresholds in units
 * of number of online CPUS to be used for choosing between five different
 * NDIV offset settings for CCPLEX cluster NAFLLs
 *
 * 1. If number of online CPUs < ncpu[0] use offset0
 * 2. If number of online CPUs < ncpu[1] use offset1
 * 3. If number of online CPUs < ncpu[2] use offset2
 * 4. If number of online CPUs < ncpu[3] use offset3
 * 5. If number of online CPUs >= ncpu[3] disable offsetting
 *
 * For TH500 mrq_gears_response::ncpu array has four valid entries.
 *
 * mrq_response::err is
 * * 0: gears defined and response data valid
 * * -#BPMP_ENODEV: MRQ is not supported by BPMP-FW
 * * -#BPMP_EACCES: Operation not permitted for the MRQ master
 * * -#BPMP_ENAVAIL: NDIV offsetting is disabled
 */
struct mrq_gears_response {
	/** @brief number of online CPUs for each gear */
	uint32_t ncpu[16];
} BPMP_ABI_PACKED;

/** @} Gears */
/** @endcond bpmp_th500 */

/**
 * @addtogroup Error_Codes
 * Negative values for mrq_response::err generally indicate some
 * error. The ABI defines the following error codes. Negating these
 * defines is an exercise left to the user.
 * @{
 */

/** @brief Operation not permitted */
#define BPMP_EPERM	1
/** @brief No such file or directory */
#define BPMP_ENOENT	2
/** @brief No MRQ handler */
#define BPMP_ENOHANDLER	3
/** @brief I/O error */
#define BPMP_EIO	5
/** @brief Bad sub-MRQ command */
#define BPMP_EBADCMD	6
/** @brief Resource temporarily unavailable */
#define BPMP_EAGAIN	11
/** @brief Not enough memory */
#define BPMP_ENOMEM	12
/** @brief Permission denied */
#define BPMP_EACCES	13
/** @brief Bad address */
#define BPMP_EFAULT	14
/** @brief Resource busy */
#define BPMP_EBUSY	16
/** @brief No such device */
#define BPMP_ENODEV	19
/** @brief Argument is a directory */
#define BPMP_EISDIR	21
/** @brief Invalid argument */
#define BPMP_EINVAL	22
/** @brief Timeout during operation */
#define BPMP_ETIMEDOUT  23
/** @brief Out of range */
#define BPMP_ERANGE	34
/** @brief Function not implemented */
#define BPMP_ENOSYS	38
/** @brief Invalid slot */
#define BPMP_EBADSLT	57
/** @brief Invalid message */
#define BPMP_EBADMSG	77
/** @brief Operation not supported */
#define BPMP_EOPNOTSUPP 95
/** @brief Targeted resource not available */
#define BPMP_ENAVAIL	119
/** @brief Not supported */
#define BPMP_ENOTSUP	134
/** @brief No such device or address */
#define BPMP_ENXIO	140

/** @} Error_Codes */

#if defined(BPMP_ABI_CHECKS)
#include "bpmp_abi_checks.h"
#endif

#endif
