/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ABI_BPMP_ABI_H_
#define _ABI_BPMP_ABI_H_

#ifdef LK
#include <stdint.h>
#endif

#ifndef __ABI_PACKED
#define __ABI_PACKED __attribute__((packed))
#endif

#ifdef NO_GCC_EXTENSIONS
#define EMPTY char empty;
#define EMPTY_ARRAY 1
#else
#define EMPTY
#define EMPTY_ARRAY 0
#endif

#ifndef __UNION_ANON
#define __UNION_ANON
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
 * @addtogroup MRQ_Format Message Format
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
 * @brief header for an MRQ message
 *
 * Provides the MRQ number for the MRQ message: #mrq. The remainder of
 * the MRQ message is a payload (immediately following the
 * mrq_request) whose format depends on mrq.
 */
struct mrq_request {
	/** @brief MRQ number of the request */
	uint32_t mrq;
	/**
	 * @brief flags providing follow up directions to the receiver
	 *
	 * | Bit | Description                                |
	 * |-----|--------------------------------------------|
	 * | 1   | ring the sender's doorbell when responding |
	 * | 0   | should be 1                                |
	 */
	uint32_t flags;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Format
 * @brief header for an MRQ response
 *
 *  Provides an error code for the associated MRQ message. The
 *  remainder of the MRQ response is a payload (immediately following
 *  the mrq_response) whose format depends on the associated
 *  mrq_request::mrq
 */
struct mrq_response {
	/** @brief error code for the MRQ request itself */
	int32_t err;
	/** @brief reserved for future use */
	uint32_t flags;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Format
 * Minimum needed size for an IPC message buffer
 */
#define MSG_MIN_SZ	128
/**
 * @ingroup MRQ_Format
 *  Minimum size guaranteed for data in an IPC message buffer
 */
#define MSG_DATA_MIN_SZ	120

/**
 * @ingroup MRQ_Codes
 * @name Legal MRQ codes
 * These are the legal values for mrq_request::mrq
 * @{
 */

#define MRQ_PING		0
#define MRQ_QUERY_TAG		1
#define MRQ_MODULE_LOAD		4
#define MRQ_MODULE_UNLOAD	5
#define MRQ_TRACE_MODIFY	7
#define MRQ_WRITE_TRACE		8
#define MRQ_THREADED_PING	9
#define MRQ_MODULE_MAIL		11
#define MRQ_DEBUGFS		19
#define MRQ_RESET		20
#define MRQ_I2C			21
#define MRQ_CLK			22
#define MRQ_QUERY_ABI		23
#define MRQ_PG_READ_STATE	25
#define MRQ_PG_UPDATE_STATE	26
#define MRQ_THERMAL		27
#define MRQ_CPU_VHINT		28
#define MRQ_ABI_RATCHET		29
#define MRQ_EMC_DVFS_LATENCY	31
#define MRQ_TRACE_ITER		64
#define MRQ_RINGBUF_CONSOLE	65
#define MRQ_PG			66

/** @} */

/**
 * @ingroup MRQ_Codes
 * @brief Maximum MRQ code to be sent by CPU software to
 * BPMP. Subject to change in future
 */
#define MAX_CPU_MRQ_ID		66

/**
 * @addtogroup MRQ_Payloads Message Payloads
 * @{
 *   @defgroup Ping
 *   @defgroup Query_Tag Query Tag
 *   @defgroup Module Loadable Modules
 *   @defgroup Trace
 *   @defgroup Debugfs
 *   @defgroup Reset
 *   @defgroup I2C
 *   @defgroup Clocks
 *   @defgroup ABI_info ABI Info
 *   @defgroup MC_Flush MC Flush
 *   @defgroup Powergating
 *   @defgroup Thermal
 *   @defgroup Vhint CPU Voltage hint
 *   @defgroup MRQ_Deprecated Deprecated MRQ messages
 *   @defgroup EMC
 *   @defgroup RingbufConsole
 * @}
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
 * @brief request with #MRQ_PING
 *
 * Used by the sender of an #MRQ_PING message to request a pong from
 * recipient. The response from the recipient is computed based on
 * #challenge.
 */
struct mrq_ping_request {
/** @brief arbitrarily chosen value */
	uint32_t challenge;
} __ABI_PACKED;

/**
 * @ingroup Ping
 * @brief response to #MRQ_PING
 *
 * Sent in response to an #MRQ_PING message. #reply should be the
 * mrq_ping_request challenge left shifted by 1 with the carry-bit
 * dropped.
 *
 */
struct mrq_ping_response {
	/** @brief response to the MRQ_PING challege */
	uint32_t reply;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_QUERY_TAG
 * @brief Query BPMP firmware's tag (i.e. version information)
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
 * @brief request with #MRQ_QUERY_TAG
 *
 * Used by #MRQ_QUERY_TAG call to ask BPMP to fill in the memory
 * pointed by #addr with BPMP firmware header.
 *
 * The sender is reponsible for ensuring that #addr is mapped in to
 * the recipient's address map.
 */
struct mrq_query_tag_request {
  /** @brief base address to store the firmware header */
	uint32_t addr;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_MODULE_LOAD
 * @brief dynamically load a BPMP code module
 *
 * * Platforms: All
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_module_load_request
 * * Response Payload: @ref mrq_module_load_response
 *
 * @note This MRQ is disabled on production systems
 *
 */

/**
 * @ingroup Module
 * @brief request with #MRQ_MODULE_LOAD
 *
 * Used by #MRQ_MODULE_LOAD calls to ask the recipient to dynamically
 * load the code located at #phys_addr and having size #size
 * bytes. #phys_addr is treated as a void pointer.
 *
 * The recipient copies the code from #phys_addr to locally allocated
 * memory prior to responding to this message.
 *
 * @todo document the module header format
 *
 * The sender is responsible for ensuring that the code is mapped in
 * the recipient's address map.
 *
 */
struct mrq_module_load_request {
	/** @brief base address of the code to load. Treated as (void *) */
	uint32_t phys_addr; /* (void *) */
	/** @brief size in bytes of code to load */
	uint32_t size;
} __ABI_PACKED;

/**
 * @ingroup Module
 * @brief response to #MRQ_MODULE_LOAD
 *
 * @todo document mrq_response::err
 */
struct mrq_module_load_response {
	/** @brief handle to the loaded module */
	uint32_t base;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_MODULE_UNLOAD
 * @brief unload a previously loaded code module
 *
 * * Platforms: All
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_module_unload_request
 * * Response Payload: N/A
 *
 * @note This MRQ is disabled on production systems
 */

/**
 * @ingroup Module
 * @brief request with #MRQ_MODULE_UNLOAD
 *
 * Used by #MRQ_MODULE_UNLOAD calls to request that a previously loaded
 * module be unloaded.
 */
struct mrq_module_unload_request {
	/** @brief handle of the module to unload */
	uint32_t base;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_TRACE_MODIFY
 * @brief modify the set of enabled trace events
 *
 * * Platforms: All
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_trace_modify_request
 * * Response Payload: @ref mrq_trace_modify_response
 *
 * @note This MRQ is disabled on production systems
 */

/**
 * @ingroup Trace
 * @brief request with #MRQ_TRACE_MODIFY
 *
 * Used by %MRQ_TRACE_MODIFY calls to enable or disable specify trace
 * events.  #set takes precedence for any bit set in both #set and
 * #clr.
 */
struct mrq_trace_modify_request {
	/** @brief bit mask of trace events to disable */
	uint32_t clr;
	/** @brief bit mask of trace events to enable */
	uint32_t set;
} __ABI_PACKED;

/**
 * @ingroup Trace
 * @brief response to #MRQ_TRACE_MODIFY
 *
 * Sent in repsonse to an #MRQ_TRACE_MODIFY message. #mask reflects the
 * state of which events are enabled after the recipient acted on the
 * message.
 *
 */
struct mrq_trace_modify_response {
	/** @brief bit mask of trace event enable states */
	uint32_t mask;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_WRITE_TRACE
 * @brief Write trace data to a buffer
 *
 * * Platforms: All
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_write_trace_request
 * * Response Payload: @ref mrq_write_trace_response
 *
 * mrq_response::err depends on the @ref mrq_write_trace_request field
 * values. err is -#BPMP_EINVAL if size is zero or area is NULL or
 * area is in an illegal range. A positive value for err indicates the
 * number of bytes written to area.
 *
 * @note This MRQ is disabled on production systems
 */

/**
 * @ingroup Trace
 * @brief request with #MRQ_WRITE_TRACE
 *
 * Used by MRQ_WRITE_TRACE calls to ask the recipient to copy trace
 * data from the recipient's local buffer to the output buffer. #area
 * is treated as a byte-aligned pointer in the recipient's address
 * space.
 *
 * The sender is responsible for ensuring that the output
 * buffer is mapped in the recipient's address map. The recipient is
 * responsible for protecting its own code and data from accidental
 * overwrites.
 */
struct mrq_write_trace_request {
	/** @brief base address of output buffer */
	uint32_t area;
	/** @brief size in bytes of the output buffer */
	uint32_t size;
} __ABI_PACKED;

/**
 * @ingroup Trace
 * @brief response to #MRQ_WRITE_TRACE
 *
 * Once this response is sent, the respondent will not access the
 * output buffer further.
 */
struct mrq_write_trace_response {
	/**
	 * @brief flag whether more data remains in local buffer
	 *
	 * Value is 1 if the entire local trace buffer has been
	 * drained to the outputbuffer. Value is 0 otherwise.
	 */
	uint32_t eof;
} __ABI_PACKED;

/** @private */
struct mrq_threaded_ping_request {
	uint32_t challenge;
} __ABI_PACKED;

/** @private */
struct mrq_threaded_ping_response {
	uint32_t reply;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_MODULE_MAIL
 * @brief send a message to a loadable module
 *
 * * Platforms: All
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_module_mail_request
 * * Response Payload: @ref mrq_module_mail_response
 *
 * @note This MRQ is disabled on production systems
 */

/**
 * @ingroup Module
 * @brief request with #MRQ_MODULE_MAIL
 */
struct mrq_module_mail_request {
	/** @brief handle to the previously loaded module */
	uint32_t base;
	/** @brief module-specific mail payload
	 *
	 * The length of data[ ] is unknown to the BPMP core firmware
	 * but it is limited to the size of an IPC message.
	 */
	uint8_t data[EMPTY_ARRAY];
} __ABI_PACKED;

/**
 * @ingroup Module
 * @brief response to #MRQ_MODULE_MAIL
 */
struct mrq_module_mail_response {
	/** @brief module-specific mail payload
	 *
	 * The length of data[ ] is unknown to the BPMP core firmware
	 * but it is limited to the size of an IPC message.
	 */
	uint8_t data[EMPTY_ARRAY];
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_DEBUGFS
 * @brief Interact with BPMP's debugfs file nodes
 *
 * * Platforms: T186
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
	CMD_DEBUGFS_READ = 1,
	CMD_DEBUGFS_WRITE = 2,
	CMD_DEBUGFS_DUMPDIR = 3,
	CMD_DEBUGFS_MAX
};

/**
 * @ingroup Debugfs
 * @brief parameters for CMD_DEBUGFS_READ/WRITE command
 */
struct cmd_debugfs_fileop_request {
	/** @brief physical address pointing at filename */
	uint32_t fnameaddr;
	/** @brief length in bytes of filename buffer */
	uint32_t fnamelen;
	/** @brief physical address pointing to data buffer */
	uint32_t dataaddr;
	/** @brief length in bytes of data buffer */
	uint32_t datalen;
} __ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief parameters for CMD_DEBUGFS_READ/WRITE command
 */
struct cmd_debugfs_dumpdir_request {
	/** @brief physical address pointing to data buffer */
	uint32_t dataaddr;
	/** @brief length in bytes of data buffer */
	uint32_t datalen;
} __ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief response data for CMD_DEBUGFS_READ/WRITE command
 */
struct cmd_debugfs_fileop_response {
	/** @brief always 0 */
	uint32_t reserved;
	/** @brief number of bytes read from or written to data buffer */
	uint32_t nbytes;
} __ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief response data for CMD_DEBUGFS_DUMPDIR command
 */
struct cmd_debugfs_dumpdir_response {
	/** @brief always 0 */
	uint32_t reserved;
	/** @brief number of bytes read from or written to data buffer */
	uint32_t nbytes;
} __ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief request with #MRQ_DEBUGFS.
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
	uint32_t cmd;
	union {
		struct cmd_debugfs_fileop_request fop;
		struct cmd_debugfs_dumpdir_request dumpdir;
	} __UNION_ANON;
} __ABI_PACKED;

/**
 * @ingroup Debugfs
 */
struct mrq_debugfs_response {
	/** @brief always 0 */
	int32_t reserved;
	union {
		/** @brief response data for CMD_DEBUGFS_READ OR
		 * CMD_DEBUGFS_WRITE command
		 */
		struct cmd_debugfs_fileop_response fop;
		/** @brief response data for CMD_DEBUGFS_DUMPDIR command */
		struct cmd_debugfs_dumpdir_response dumpdir;
	} __UNION_ANON;
} __ABI_PACKED;

/**
 * @addtogroup Debugfs
 * @{
 */
#define DEBUGFS_S_ISDIR	(1 << 9)
#define DEBUGFS_S_IRUSR	(1 << 8)
#define DEBUGFS_S_IWUSR	(1 << 7)
/** @} */


/**
 * @ingroup MRQ_Codes
 * @def MRQ_RESET
 * @brief reset an IP block
 *
 * * Platforms: T186
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_reset_request
 * * Response Payload: @ref mrq_reset_response
 */

/**
 * @ingroup Reset
 */
enum mrq_reset_commands {
	CMD_RESET_ASSERT = 1,
	CMD_RESET_DEASSERT = 2,
	CMD_RESET_MODULE = 3,
	CMD_RESET_GET_MAX_ID = 4,
	CMD_RESET_MAX, /* not part of ABI and subject to change */
};

/**
 * @ingroup Reset
 * @brief request with MRQ_RESET
 *
 * Used by the sender of an #MRQ_RESET message to request BPMP to
 * assert or or deassert a given reset line.
 */
struct mrq_reset_request {
	/** @brief reset action to perform (@enum mrq_reset_commands) */
	uint32_t cmd;
	/** @brief id of the reset to affected */
	uint32_t reset_id;
} __ABI_PACKED;

/**
 * @ingroup Reset
 * @brief Response for MRQ_RESET sub-command CMD_RESET_GET_MAX_ID. When
 * this sub-command is not supported, firmware will return -BPMP_EBADCMD
 * in mrq_response::err.
 */
struct cmd_reset_get_max_id_response {
	/** @brief max reset id */
	uint32_t max_id;
} __ABI_PACKED;

/**
 * @ingroup Reset
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
	} __UNION_ANON;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_I2C
 * @brief issue an i2c transaction
 *
 * * Platforms: T186
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_i2c_request
 * * Response Payload: @ref mrq_i2c_response
 */

/**
 * @addtogroup I2C
 * @{
 */
#define TEGRA_I2C_IPC_MAX_IN_BUF_SIZE	(MSG_DATA_MIN_SZ - 12)
#define TEGRA_I2C_IPC_MAX_OUT_BUF_SIZE	(MSG_DATA_MIN_SZ - 4)
/** @} */

/**
 * @ingroup I2C
 * @name Serial I2C flags
 * Use these flags with serial_i2c_request::flags
 * @{
 */
#define SERIALI2C_TEN           0x0010
#define SERIALI2C_RD            0x0001
#define SERIALI2C_STOP          0x8000
#define SERIALI2C_NOSTART       0x4000
#define SERIALI2C_REV_DIR_ADDR  0x2000
#define SERIALI2C_IGNORE_NAK    0x1000
#define SERIALI2C_NO_RD_ACK     0x0800
#define SERIALI2C_RECV_LEN      0x0400
/** @} */
/** @ingroup I2C */
enum {
	CMD_I2C_XFER = 1
};

/**
 * @ingroup I2C
 * @brief serializable i2c request
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
	/** @brief bitmask of SERIALI2C_ flags */
	uint16_t flags;
	/** @brief length of I2C transaction in bytes */
	uint16_t len;
	/** @brief for write transactions only, #len bytes of data */
	uint8_t data[];
} __ABI_PACKED;

/**
 * @ingroup I2C
 * @brief trigger one or more i2c transactions
 */
struct cmd_i2c_xfer_request {
	/** @brief valid bus number from mach-t186/i2c-t186.h*/
	uint32_t bus_id;

	/** @brief count of valid bytes in #data_buf*/
	uint32_t data_size;

	/** @brief serialized packed instances of @ref serial_i2c_request*/
	uint8_t data_buf[TEGRA_I2C_IPC_MAX_IN_BUF_SIZE];
} __ABI_PACKED;

/**
 * @ingroup I2C
 * @brief container for data read from the i2c bus
 *
 * Processing an cmd_i2c_xfer_request::data_buf causes BPMP to execute
 * zero or more I2C reads. The data read from the bus is serialized
 * into #data_buf.
 */
struct cmd_i2c_xfer_response {
	/** @brief count of valid bytes in #data_buf*/
	uint32_t data_size;
	/** @brief i2c read data */
	uint8_t data_buf[TEGRA_I2C_IPC_MAX_OUT_BUF_SIZE];
} __ABI_PACKED;

/**
 * @ingroup I2C
 * @brief request with #MRQ_I2C
 */
struct mrq_i2c_request {
	/** @brief always CMD_I2C_XFER (i.e. 1) */
	uint32_t cmd;
	/** @brief parameters of the transfer request */
	struct cmd_i2c_xfer_request xfer;
} __ABI_PACKED;

/**
 * @ingroup I2C
 * @brief response to #MRQ_I2C
 */
struct mrq_i2c_response {
	struct cmd_i2c_xfer_response xfer;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_CLK
 *
 * * Platforms: T186
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_clk_request
 * * Response Payload: @ref mrq_clk_response
 * @addtogroup Clocks
 * @{
 */

/**
 * @name MRQ_CLK sub-commands
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
	CMD_CLK_GET_ALL_INFO = 14,
	CMD_CLK_GET_MAX_CLK_ID = 15,
	CMD_CLK_MAX,
};
/** @} */

/**
 * @name MRQ_CLK properties
 * Flag bits for cmd_clk_properties_response::flags and
 * cmd_clk_get_all_info_response::flags
 * @{
 */
#define BPMP_CLK_HAS_MUX	(1 << 0)
#define BPMP_CLK_HAS_SET_RATE	(1 << 1)
#define BPMP_CLK_IS_ROOT	(1 << 2)
/** @} */

#define MRQ_CLK_NAME_MAXLEN	40
#define MRQ_CLK_MAX_PARENTS	16

/** @private */
struct cmd_clk_get_rate_request {
	EMPTY
} __ABI_PACKED;

struct cmd_clk_get_rate_response {
	int64_t rate;
} __ABI_PACKED;

struct cmd_clk_set_rate_request {
	int32_t unused;
	int64_t rate;
} __ABI_PACKED;

struct cmd_clk_set_rate_response {
	int64_t rate;
} __ABI_PACKED;

struct cmd_clk_round_rate_request {
	int32_t unused;
	int64_t rate;
} __ABI_PACKED;

struct cmd_clk_round_rate_response {
	int64_t rate;
} __ABI_PACKED;

/** @private */
struct cmd_clk_get_parent_request {
	EMPTY
} __ABI_PACKED;

struct cmd_clk_get_parent_response {
	uint32_t parent_id;
} __ABI_PACKED;

struct cmd_clk_set_parent_request {
	uint32_t parent_id;
} __ABI_PACKED;

struct cmd_clk_set_parent_response {
	uint32_t parent_id;
} __ABI_PACKED;

/** @private */
struct cmd_clk_is_enabled_request {
	EMPTY
} __ABI_PACKED;

struct cmd_clk_is_enabled_response {
	int32_t state;
} __ABI_PACKED;

/** @private */
struct cmd_clk_enable_request {
	EMPTY
} __ABI_PACKED;

/** @private */
struct cmd_clk_enable_response {
	EMPTY
} __ABI_PACKED;

/** @private */
struct cmd_clk_disable_request {
	EMPTY
} __ABI_PACKED;

/** @private */
struct cmd_clk_disable_response {
	EMPTY
} __ABI_PACKED;

/** @private */
struct cmd_clk_get_all_info_request {
	EMPTY
} __ABI_PACKED;

struct cmd_clk_get_all_info_response {
	uint32_t flags;
	uint32_t parent;
	uint32_t parents[MRQ_CLK_MAX_PARENTS];
	uint8_t num_parents;
	uint8_t name[MRQ_CLK_NAME_MAXLEN];
} __ABI_PACKED;

/** @private */
struct cmd_clk_get_max_clk_id_request {
	EMPTY
} __ABI_PACKED;

struct cmd_clk_get_max_clk_id_response {
	uint32_t max_id;
} __ABI_PACKED;
/** @} */

/**
 * @ingroup Clocks
 * @brief request with #MRQ_CLK
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
 *
 */

struct mrq_clk_request {
	/** @brief sub-command and clock id concatenated to 32-bit word.
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
		/** @private */
		struct cmd_clk_get_all_info_request clk_get_all_info;
		/** @private */
		struct cmd_clk_get_max_clk_id_request clk_get_max_clk_id;
	} __UNION_ANON;
} __ABI_PACKED;

/**
 * @ingroup Clocks
 * @brief response to MRQ_CLK
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
 *
 */

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
		struct cmd_clk_get_all_info_response clk_get_all_info;
		struct cmd_clk_get_max_clk_id_response clk_get_max_clk_id;
	} __UNION_ANON;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_QUERY_ABI
 * @brief check if an MRQ is implemented
 *
 * * Platforms: All
 * * Initiators: Any
 * * Targets: Any except DMCE
 * * Request Payload: @ref mrq_query_abi_request
 * * Response Payload: @ref mrq_query_abi_response
 */

/**
 * @ingroup ABI_info
 * @brief request with MRQ_QUERY_ABI
 *
 * Used by #MRQ_QUERY_ABI call to check if MRQ code #mrq is supported
 * by the recipient.
 */
struct mrq_query_abi_request {
	/** @brief MRQ code to query */
	uint32_t mrq;
} __ABI_PACKED;

/**
 * @ingroup ABI_info
 * @brief response to MRQ_QUERY_ABI
 *
 * @note mrq_response::err of 0 indicates that the query was
 * successful, not that the MRQ itself is supported!
 */
struct mrq_query_abi_response {
	/** @brief 0 if queried MRQ is supported. Else, -#BPMP_ENODEV */
	int32_t status;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_PG_READ_STATE
 * @brief read the power-gating state of a partition
 *
 * * Platforms: T186
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_pg_read_state_request
 * * Response Payload: @ref mrq_pg_read_state_response
 * @addtogroup Powergating
 * @{
 */

/**
 * @brief request with #MRQ_PG_READ_STATE
 *
 * Used by MRQ_PG_READ_STATE call to read the current state of a
 * partition.
 */
struct mrq_pg_read_state_request {
	/** @brief ID of partition */
	uint32_t partition_id;
} __ABI_PACKED;

/**
 * @brief response to MRQ_PG_READ_STATE
 * @todo define possible errors.
 */
struct mrq_pg_read_state_response {
	/** @brief read as don't care */
	uint32_t sram_state;
	/** @brief state of power partition
	 * * 0 : off
	 * * 1 : on
	 */
	uint32_t logic_state;
} __ABI_PACKED;

/** @} */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_PG_UPDATE_STATE
 * @brief modify the power-gating state of a partition. In contrast to
 * MRQ_PG calls, the operations that change state (on/off) of power
 * partition are reference counted.
 *
 * * Platforms: T186
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_pg_update_state_request
 * * Response Payload: N/A
 * @addtogroup Powergating
 * @{
 */

/**
 * @brief request with mrq_pg_update_state_request
 *
 * Used by #MRQ_PG_UPDATE_STATE call to request BPMP to change the
 * state of a power partition #partition_id.
 */
struct mrq_pg_update_state_request {
	/** @brief ID of partition */
	uint32_t partition_id;
	/** @brief secondary control of power partition
	 *  @details Ignored by many versions of the BPMP
	 *  firmware. For maximum compatibility, set the value
	 *  according to @logic_state
	 * *  0x1: power ON partition (@ref logic_state == 0x3)
	 * *  0x3: power OFF partition (@ref logic_state == 0x1)
	 */
	uint32_t sram_state;
	/** @brief controls state of power partition, legal values are
	 * *  0x1 : power OFF partition
	 * *  0x3 : power ON partition
	 */
	uint32_t logic_state;
	/** @brief change state of clocks of the power partition, legal values
	 * *  0x0 : do not change clock state
	 * *  0x1 : disable partition clocks (only applicable when
	 *          @ref logic_state == 0x1)
	 * *  0x3 : enable partition clocks (only applicable when
	 *          @ref logic_state == 0x3)
	 */
	uint32_t clock_state;
} __ABI_PACKED;
/** @} */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_PG
 * @brief Control power-gating state of a partition. In contrast to
 * MRQ_PG_UPDATE_STATE, operations that change the power partition
 * state are NOT reference counted
 *
 * * Platforms: T186
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_pg_request
 * * Response Payload: @ref mrq_pg_response
 * @addtogroup Powergating
 * @{
 */

/**
 * @name MRQ_PG sub-commands
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
	 * @brief get the name string of specified power domain id.
	 *
	 * mrq_response:err is
	 * 0: Success
	 * -#BPMP_EINVAL: Invalid request parameters
	 */
	CMD_PG_GET_NAME = 3,


	/**
	 * @brief get the highest power domain id in the system. Not
	 * all IDs between 0 and max_id are valid IDs.
	 *
	 * mrq_response:err is
	 * 0: Success
	 * -#BPMP_EINVAL: Invalid request parameters
	 */
	CMD_PG_GET_MAX_ID = 4,
};
/** @} */

#define MRQ_PG_NAME_MAXLEN	40

/**
 * @brief possible power domain states in
 * cmd_pg_set_state_request:state and cmd_pg_get_state_response:state.
 *  PG_STATE_OFF: power domain is OFF
 *  PG_STATE_ON: power domain is ON
 *  PG_STATE_RUNNING: power domain is ON and made into directly usable
 *                    state by turning on the clocks associated with
 *                    the domain
 */
enum pg_states {
	PG_STATE_OFF = 0,
	PG_STATE_ON = 1,
	PG_STATE_RUNNING = 2,
};

struct cmd_pg_query_abi_request {
	uint32_t type; /* enum mrq_pg_cmd */
} __ABI_PACKED;

struct cmd_pg_set_state_request {
	uint32_t state; /* enum pg_states */
} __ABI_PACKED;

struct cmd_pg_get_state_response {
	uint32_t state; /* enum pg_states */
} __ABI_PACKED;

struct cmd_pg_get_name_response {
	uint8_t name[MRQ_PG_NAME_MAXLEN];
} __ABI_PACKED;

struct cmd_pg_get_max_id_response {
	uint32_t max_id;
} __ABI_PACKED;

/**
 * @ingroup Powergating
 * @brief request with #MRQ_PG
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
	} __UNION_ANON;
} __ABI_PACKED;

/**
 * @ingroup Powergating
 * @brief response to MRQ_PG
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
 *
 */

struct mrq_pg_response {
	union {
		struct cmd_pg_get_state_response get_state;
		struct cmd_pg_get_name_response get_name;
		struct cmd_pg_get_max_id_response get_max_id;
	} __UNION_ANON;
} __ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_THERMAL
 * @brief interact with BPMP thermal framework
 *
 * * Platforms: T186
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
} __ABI_PACKED;

/*
 * Host->BPMP request data for request type CMD_THERMAL_GET_TEMP
 *
 * zone: Number of thermal zone.
 */
struct cmd_thermal_get_temp_request {
	uint32_t zone;
} __ABI_PACKED;

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
} __ABI_PACKED;

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
} __ABI_PACKED;

/*
 * BPMP->Host request data for request type CMD_THERMAL_HOST_TRIP_REACHED
 *
 * zone: Number of thermal zone where trip point was reached.
 */
struct cmd_thermal_host_trip_reached_request {
	uint32_t zone;
} __ABI_PACKED;

/*
 * BPMP->Host reply data for request type CMD_THERMAL_GET_NUM_ZONES
 *
 * num: Number of supported thermal zones. The thermal zones are indexed
 *      starting from zero.
 */
struct cmd_thermal_get_num_zones_response {
	uint32_t num;
} __ABI_PACKED;

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
	} __UNION_ANON;
} __ABI_PACKED;

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
	} __UNION_ANON;
} __ABI_PACKED;

/*
 * Data in reply to a Host->BPMP request.
 */
union mrq_thermal_bpmp_to_host_response {
	struct cmd_thermal_get_temp_response get_temp;
	struct cmd_thermal_get_num_zones_response get_num_zones;
} __ABI_PACKED;
/** @} */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_CPU_VHINT
 * @brief Query CPU voltage hint data
 *
 * * Platforms: T186
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_cpu_vhint_request
 * * Response Payload: N/A
 *
 * @addtogroup Vhint CPU Voltage hint
 * @{
 */

/**
 * @brief request with #MRQ_CPU_VHINT
 *
 * Used by #MRQ_CPU_VHINT call by CCPLEX to retrieve voltage hint data
 * from BPMP to memory space pointed by #addr. CCPLEX is responsible
 * to allocate sizeof(cpu_vhint_data) sized block of memory and
 * appropriately map it for BPMP before sending the request.
 */
struct mrq_cpu_vhint_request {
	/** @brief IOVA address for the #cpu_vhint_data */
	uint32_t addr; /* struct cpu_vhint_data * */
	/** @brief ID of the cluster whose data is requested */
	uint32_t cluster_id; /* enum cluster_id */
} __ABI_PACKED;

/**
 * @brief description of the CPU v/f relation
 *
 * Used by #MRQ_CPU_VHINT call to carry data pointed by #addr of
 * struct mrq_cpu_vhint_request
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
} __ABI_PACKED;

/** @} */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_ABI_RATCHET
 * @brief ABI ratchet value query
 *
 * * Platforms: T186
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_abi_ratchet_request
 * * Response Payload: @ref mrq_abi_ratchet_response
 * @addtogroup ABI_info
 * @{
 */

/**
 * @brief an ABI compatibility mechanism
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
 * @brief request with #MRQ_ABI_RATCHET.
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
	/** @brief requester's ratchet value */
	uint16_t ratchet;
};

/**
 * @brief response to #MRQ_ABI_RATCHET
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
/** @} */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_EMC_DVFS_LATENCY
 * @brief query frequency dependent EMC DVFS latency
 *
 * * Platforms: T186
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: N/A
 * * Response Payload: @ref mrq_emc_dvfs_latency_response
 * @addtogroup EMC
 * @{
 */

/**
 * @brief used by @ref mrq_emc_dvfs_latency_response
 */
struct emc_dvfs_latency {
	/** @brief EMC frequency in kHz */
	uint32_t freq;
	/** @brief EMC DVFS latency in nanoseconds */
	uint32_t latency;
} __ABI_PACKED;

#define EMC_DVFS_LATENCY_MAX_SIZE	14
/**
 * @brief response to #MRQ_EMC_DVFS_LATENCY
 */
struct mrq_emc_dvfs_latency_response {
	/** @brief the number valid entries in #pairs */
	uint32_t num_pairs;
	/** @brief EMC <frequency, latency> information */
	struct emc_dvfs_latency pairs[EMC_DVFS_LATENCY_MAX_SIZE];
} __ABI_PACKED;

/** @} */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_TRACE_ITER
 * @brief manage the trace iterator
 *
 * * Platforms: All
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: N/A
 * * Response Payload: @ref mrq_trace_iter_request
 * @addtogroup Trace
 * @{
 */
enum {
	/** @brief (re)start the tracing now. Ignore older events */
	TRACE_ITER_INIT = 0,
	/** @brief clobber all events in the trace buffer */
	TRACE_ITER_CLEAN = 1
};

/**
 * @brief request with #MRQ_TRACE_ITER
 */
struct mrq_trace_iter_request {
	/** @brief TRACE_ITER_INIT or TRACE_ITER_CLEAN */
	uint32_t cmd;
} __ABI_PACKED;

/** @} */

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
} __ABI_PACKED;

/** @private */
struct cmd_ringbuf_console_query_abi_resp {
	EMPTY
} __ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief Host->BPMP request data for request type #CMD_RINGBUF_CONSOLE_READ
 */
struct cmd_ringbuf_console_read_req {
	/**
	 * @brief Number of bytes requested to be read from the BPMP TX buffer
	 */
	uint8_t len;
} __ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief BPMP->Host response data for request type #CMD_RINGBUF_CONSOLE_READ
 */
struct cmd_ringbuf_console_read_resp {
	/** @brief The actual data read from the BPMP TX buffer */
	uint8_t data[MRQ_RINGBUF_CONSOLE_MAX_READ_LEN];
	/** @brief Number of bytes in cmd_ringbuf_console_read_resp::data */
	uint8_t len;
} __ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief Host->BPMP request data for request type #CMD_RINGBUF_CONSOLE_WRITE
 */
struct cmd_ringbuf_console_write_req {
	/** @brief The actual data to be written to the BPMP RX buffer */
	uint8_t data[MRQ_RINGBUF_CONSOLE_MAX_WRITE_LEN];
	/** @brief Number of bytes in cmd_ringbuf_console_write_req::data */
	uint8_t len;
} __ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief BPMP->Host response data for request type #CMD_RINGBUF_CONSOLE_WRITE
 */
struct cmd_ringbuf_console_write_resp {
	/** @brief Number of bytes of available space in the BPMP RX buffer */
	uint32_t space_avail;
	/** @brief Number of bytes that were written to the BPMP RX buffer */
	uint8_t len;
} __ABI_PACKED;

/** @private */
struct cmd_ringbuf_console_get_fifo_req {
	EMPTY
} __ABI_PACKED;

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
} __ABI_PACKED;

/**
 * @ingroup RingbufConsole
 * @brief Host->BPMP request data.
 *
 * Reply type is union #mrq_ringbuf_console_bpmp_to_host_response .
 */
struct mrq_ringbuf_console_host_to_bpmp_request {
	/**
	 * @brief type of request. Values listed in enum
	 * #mrq_ringbuf_console_host_to_bpmp_cmd.
	 */
	uint32_t type;
	/** @brief  request type specific parameters. */
	union {
		struct cmd_ringbuf_console_query_abi_req query_abi;
		struct cmd_ringbuf_console_read_req read;
		struct cmd_ringbuf_console_write_req write;
		struct cmd_ringbuf_console_get_fifo_req get_fifo;
	} __UNION_ANON;
} __ABI_PACKED;

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
} __ABI_PACKED;
/** @} */

/*
 *  4. Enumerations
 */

/*
 *   4.1 CPU enumerations
 *
 * See <mach-t186/system-t186.h>
 *
 *   4.2 CPU Cluster enumerations
 *
 * See <mach-t186/system-t186.h>
 *
 *   4.3 System low power state enumerations
 *
 * See <mach-t186/system-t186.h>
 */

/*
 *   4.4 Clock enumerations
 *
 * For clock enumerations, see <mach-t186/clk-t186.h>
 */

/*
 *   4.5 Reset enumerations
 *
 * For reset enumerations, see <mach-t186/reset-t186.h>
 */

/*
 *   4.6 Thermal sensor enumerations
 *
 * For thermal sensor enumerations, see <mach-t186/thermal-t186.h>
 */

/**
 * @defgroup Error_Codes
 * Negative values for mrq_response::err generally indicate some
 * error. The ABI defines the following error codes. Negating these
 * defines is an exercise left to the user.
 * @{
 */
/** @brief No such file or directory */
#define BPMP_ENOENT	2
/** @brief No MRQ handler */
#define BPMP_ENOHANDLER	3
/** @brief I/O error */
#define BPMP_EIO	5
/** @brief Bad sub-MRQ command */
#define BPMP_EBADCMD	6
/** @brief Not enough memory */
#define BPMP_ENOMEM	12
/** @brief Permission denied */
#define BPMP_EACCES	13
/** @brief Bad address */
#define BPMP_EFAULT	14
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
/** @} */
/** @} */
#endif
