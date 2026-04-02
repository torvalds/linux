/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (c) 2014-2025, NVIDIA CORPORATION.  All rights reserved.
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
 *
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
 *
 * Ring the sender's doorbell when responding. This should be set unless
 * the sender wants to poll the underlying communications layer directly.
 *
 * An optional direction that can be specified in mrq_request::flags.
 */
#define BPMP_MAIL_RING_DB	(1U << 1U)

/**
 * @ingroup MRQ_Format
 *
 * This is set in mrq_request::flags for requests that have CRC present and
 * correspondingly in mrq_response::flags for responses that have CRC present.
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
	 * -#BPMP_EBADMSG and ignore the request.
	 *
	 * **xid** is a transaction ID.
	 *
	 * Only used when #BPMP_MAIL_CRC_PRESENT is set.
	 *
	 * **payload_length** of the message expressed in bytes without the size of this header.
	 * See tables below for minimum accepted payload lengths for each MRQ.
	 *
	 * Only used when #BPMP_MAIL_CRC_PRESENT is set.
	 *
	 * | MRQ                   | Sub-command                           | Minimum payload length
	 * | --------------------- | ------------------------------------  | ------------------------------------------------------- |
	 * | #MRQ_PING             | -                                     | 4                                                       |
	 * | #MRQ_THREADED_PING    | -                                     | 4                                                       |
	 * | #MRQ_RESET            | any                                   | 8                                                       |
	 * | #MRQ_I2C              | -                                     | 12 + cmd_i2c_xfer_request.data_size                     |
	 * | #MRQ_CLK              | #CMD_CLK_GET_RATE                     | 4                                                       |
	 * | #MRQ_CLK              | #CMD_CLK_SET_RATE                     | 16                                                      |
	 * | #MRQ_CLK              | #CMD_CLK_ROUND_RATE                   | 16                                                      |
	 * | #MRQ_CLK              | #CMD_CLK_GET_PARENT                   | 4                                                       |
	 * | #MRQ_CLK              | #CMD_CLK_SET_PARENT                   | 8                                                       |
	 * | #MRQ_CLK              | #CMD_CLK_ENABLE                       | 4                                                       |
	 * | #MRQ_CLK              | #CMD_CLK_DISABLE                      | 4                                                       |
	 * | #MRQ_CLK              | #CMD_CLK_IS_ENABLED                   | 4                                                       |
	 * | #MRQ_CLK              | #CMD_CLK_GET_ALL_INFO                 | 4                                                       |
	 * | #MRQ_CLK              | #CMD_CLK_GET_MAX_CLK_ID               | 4                                                       |
	 * | #MRQ_CLK              | #CMD_CLK_GET_FMAX_AT_VMIN             | 4                                                       |
	 * | #MRQ_QUERY_ABI        | -                                     | 4                                                       |
	 * | #MRQ_PG               | #CMD_PG_QUERY_ABI                     | 12                                                      |
	 * | #MRQ_PG               | #CMD_PG_SET_STATE                     | 12                                                      |
	 * | #MRQ_PG               | #CMD_PG_GET_STATE                     | 8                                                       |
	 * | #MRQ_PG               | #CMD_PG_GET_NAME                      | 8                                                       |
	 * | #MRQ_PG               | #CMD_PG_GET_MAX_ID                    | 8                                                       |
	 * | #MRQ_THERMAL          | #CMD_THERMAL_QUERY_ABI                | 8                                                       |
	 * | #MRQ_THERMAL          | #CMD_THERMAL_GET_TEMP                 | 8                                                       |
	 * | #MRQ_THERMAL          | #CMD_THERMAL_GET_NUM_ZONES            | 4                                                       |
	 * | #MRQ_THERMAL          | #CMD_THERMAL_GET_THERMTRIP            | 8                                                       |
	 * | #MRQ_ABI_RATCHET      | -                                     | 2                                                       |
	 * | #MRQ_EMC_DVFS_LATENCY | -                                     | 8                                                       |
	 * | #MRQ_QUERY_FW_TAG     | -                                     | 0                                                       |
	 * | #MRQ_DEBUG            | #CMD_DEBUG_OPEN_RO                    | 4 + length of cmd_debug_fopen_request.name              |
	 * | #MRQ_DEBUG            | #CMD_DEBUG_OPEN_WO                    | 4 + length of cmd_debug_fopen_request.name              |
	 * | #MRQ_DEBUG            | #CMD_DEBUG_READ                       | 8                                                       |
	 * | #MRQ_DEBUG            | #CMD_DEBUG_WRITE                      | 12 + cmd_debug_fwrite_request.datalen                   |
	 * | #MRQ_DEBUG            | #CMD_DEBUG_CLOSE                      | 8                                                       |
	 *
	 * @cond (bpmp_t186)
	 * The following additional MRQ is supported on T186 -platform:
	 *
	 * | MRQ                   | Sub-command                           | Minimum payload length                |
	 * | --------------------- | ------------------------------------- | ------------------------------------- |
	 * | #MRQ_CPU_VHINT        | -                                     | 8                                     |
	 * | #MRQ_THERMAL          | #CMD_THERMAL_SET_TRIP                 | 20                                    |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_QUERY_ABI        | 8                                     |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_READ             | 5                                     |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_WRITE            | 5 + cmd_ringbuf_console_write_req.len |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_GET_FIFO         | 4                                     |
	 * @endcond
	 *
	 * @cond (bpmp_t194)
	 * The following additional MRQs are supported on T194 -platform:
	 *
	 * | MRQ                   | Sub-command                           | Minimum payload length                |
	 * | --------------------- | ------------------------------------- | ------------------------------------- |
	 * | #MRQ_CPU_NDIV_LIMITS  | -                                     | 4                                     |
	 * | #MRQ_STRAP            | #STRAP_SET                            | 12                                    |
	 * | #MRQ_CPU_AUTO_CC3     | -                                     | 4                                     |
	 * | #MRQ_EC               | #CMD_EC_STATUS_EX_GET                 | 12                                    |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_CLAMP                  | 16                                    |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_FREE                   | 4                                     |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_GET                    | 4                                     |
	 * | #MRQ_FMON             | #CMD_FMON_FAULT_STS_GET               | 8                                     |
	 * | #MRQ_THERMAL          | #CMD_THERMAL_SET_TRIP                 | 20                                    |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_QUERY_ABI        | 8                                     |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_READ             | 5                                     |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_WRITE            | 5 + cmd_ringbuf_console_write_req.len |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_GET_FIFO         | 4                                     |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_LANE_MARGIN_CONTROL    | 24                                    |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_LANE_MARGIN_STATUS     | 4                                     |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT | 5                                     |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_CONTROLLER_STATE       | 6                                     |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF  | 5                                     |
	 * @endcond
	 *
	 * @cond (bpmp_safe && bpmp_t234)
	 * The following additional MRQ is supported on functional-safety
	 * builds for the T234 platform:
	 *
	 * | MRQ                   | Sub-command                           | Minimum payload length                |
	 * | --------------------- | ------------------------------------- | ------------------------------------- |
	 * | #MRQ_CPU_NDIV_LIMITS  | -                                     | 4                                     |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_QUERY_ABI        | 8                                     |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_READ             | 5                                     |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_WRITE            | 5 + cmd_ringbuf_console_write_req.len |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_GET_FIFO         | 4                                     |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_LANE_MARGIN_CONTROL    | 24                                    |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_LANE_MARGIN_STATUS     | 4                                     |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT | 5                                     |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_CONTROLLER_STATE       | 6                                     |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF  | 5                                     |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_CLAMP                  | 16                                    |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_FREE                   | 4                                     |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_GET                    | 4                                     |
	 * | #MRQ_FMON             | #CMD_FMON_FAULT_STS_GET               | 8                                     |
	 * | #MRQ_EMC_DVFS_EMCHUB  | -                                     | 8                                     |
	 * | #MRQ_EMC_DISP_RFL     | -                                     | 4                                     |
	 *
	 * @endcond
	 *
	 * @cond (!bpmp_safe && bpmp_t234)
	 *
	 * The following additional MRQs are supported on non-functional-safety
	 * builds for the T234 and T238 -platforms:
	 *
	 * | MRQ                   | Sub-command                           | Minimum payload length                              |
	 * | --------------------- | ------------------------------------- | --------------------------------------------------- |
	 * | #MRQ_CPU_NDIV_LIMITS  | -                                     | 4                                                   |
	 * | #MRQ_STRAP            | #STRAP_SET                            | 12                                                  |
	 * | #MRQ_THERMAL          | #CMD_THERMAL_SET_TRIP                 | 20                                                  |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_QUERY_ABI        | 8                                                   |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_READ             | 5                                                   |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_WRITE            | 5 + cmd_ringbuf_console_write_req.len               |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_GET_FIFO         | 4                                                   |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_LANE_MARGIN_CONTROL    | 24                                                  |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_LANE_MARGIN_STATUS     | 4                                                   |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT | 5                                                   |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_CONTROLLER_STATE       | 6                                                   |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF  | 5                                                   |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_CLAMP                  | 16                                                  |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_FREE                   | 4                                                   |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_GET                    | 4                                                   |
	 * | #MRQ_FMON             | #CMD_FMON_FAULT_STS_GET               | 8                                                   |
	 * | #MRQ_EMC_DVFS_EMCHUB  | -                                     | 8                                                   |
	 * | #MRQ_EMC_DISP_RFL     | -                                     | 4                                                   |
	 * | #MRQ_BWMGR            | #CMD_BWMGR_QUERY_ABI                  | 8                                                   |
	 * | #MRQ_BWMGR            | #CMD_BWMGR_CALC_RATE                  | 8 + 8 * cmd_bwmgr_calc_rate_request.num_iso_clients |
	 * | #MRQ_ISO_CLIENT       | #CMD_ISO_CLIENT_QUERY_ABI             | 8                                                   |
	 * | #MRQ_ISO_CLIENT       | #CMD_ISO_CLIENT_CALCULATE_LA          | 16                                                  |
	 * | #MRQ_ISO_CLIENT       | #CMD_ISO_CLIENT_SET_LA                | 16                                                  |
	 * | #MRQ_ISO_CLIENT       | #CMD_ISO_CLIENT_GET_MAX_BW            | 8                                                   |
	 * | #MRQ_BWMGR_INT        | #CMD_BWMGR_INT_QUERY_ABI              | 8                                                   |
	 * | #MRQ_BWMGR_INT        | #CMD_BWMGR_INT_CALC_AND_SET           | 16                                                  |
	 * | #MRQ_BWMGR_INT        | #CMD_BWMGR_INT_CAP_SET                | 8                                                   |
	 * | #MRQ_BWMGR_INT        | #CMD_BWMGR_INT_GET_LAST_REQUEST       | 9                                                   |
	 * | #MRQ_OC_STATUS        | -                                     | 0                                                   |
	 * @endcond
	 *
	 * @cond bpmp_t238
	 * The following additional MRQs are supported on T238 platform:
	 *
	 * | MRQ                   | Sub-command                           | Minimum payload length                              |
	 * | --------------------- | ------------------------------------- | --------------------------------------------------- |
	 * | #MRQ_CPU_NDIV_LIMITS  | -                                     | 4                                                   |
	 * | #MRQ_STRAP            | #STRAP_SET                            | 12                                                  |
	 * | #MRQ_THERMAL          | #CMD_THERMAL_SET_TRIP                 | 20                                                  |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_QUERY_ABI        | 8                                                   |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_READ             | 5                                                   |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_WRITE            | 5 + cmd_ringbuf_console_write_req.len               |
	 * | #MRQ_RINGBUF_CONSOLE  | #CMD_RINGBUF_CONSOLE_GET_FIFO         | 4                                                   |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_LANE_MARGIN_CONTROL    | 24                                                  |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_LANE_MARGIN_STATUS     | 4                                                   |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT | 5                                                   |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_CONTROLLER_STATE       | 6                                                   |
	 * | #MRQ_UPHY             | #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF  | 5                                                   |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_CLAMP                  | 16                                                  |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_FREE                   | 4                                                   |
	 * | #MRQ_FMON             | #CMD_FMON_GEAR_GET                    | 4                                                   |
	 * | #MRQ_FMON             | #CMD_FMON_FAULT_STS_GET               | 8                                                   |
	 * | #MRQ_EMC_DVFS_EMCHUB  | -                                     | 8                                                   |
	 * | #MRQ_EMC_DISP_RFL     | -                                     | 4                                                   |
	 * | #MRQ_BWMGR            | #CMD_BWMGR_QUERY_ABI                  | 8                                                   |
	 * | #MRQ_BWMGR            | #CMD_BWMGR_CALC_RATE                  | 8 + 8 * cmd_bwmgr_calc_rate_request.num_iso_clients |
	 * | #MRQ_ISO_CLIENT       | #CMD_ISO_CLIENT_QUERY_ABI             | 8                                                   |
	 * | #MRQ_ISO_CLIENT       | #CMD_ISO_CLIENT_CALCULATE_LA          | 16                                                  |
	 * | #MRQ_ISO_CLIENT       | #CMD_ISO_CLIENT_SET_LA                | 16                                                  |
	 * | #MRQ_ISO_CLIENT       | #CMD_ISO_CLIENT_GET_MAX_BW            | 8                                                   |
	 * | #MRQ_BWMGR_INT        | #CMD_BWMGR_INT_QUERY_ABI              | 8                                                   |
	 * | #MRQ_BWMGR_INT        | #CMD_BWMGR_INT_CALC_AND_SET           | 16                                                  |
	 * | #MRQ_BWMGR_INT        | #CMD_BWMGR_INT_CAP_SET                | 8                                                   |
	 * | #MRQ_BWMGR_INT        | #CMD_BWMGR_INT_GET_LAST_REQUEST       | 9                                                   |
	 * | #MRQ_OC_STATUS        | -                                     | 0                                                   |
	 * | #MRQ_THROTTLE         | #CMD_THROTTLE_SET_OC_CONFIG           | 5                                                   |
	 * @endcond
	 *
	 * @cond (bpmp_th500)
	 * The following additional MRQs are supported on TH500 -platform:
	 *
	 * | MRQ                  | Sub-command                           | Minimum payload length |
	 * | -------------------- | ------------------------------------- | ---------------------- |
	 * | #MRQ_CPU_NDIV_LIMITS | -                                     | 4                      |
	 * | #MRQ_THERMAL         | #CMD_THERMAL_SET_TRIP                 | 20                     |
	 * | #MRQ_STRAP           | #STRAP_SET                            | 12                     |
	 * | #MRQ_SHUTDOWN        | -                                     | 4                      |
	 * | #MRQ_UPHY            | #CMD_UPHY_PCIE_LANE_MARGIN_CONTROL    | 24                     |
	 * | #MRQ_UPHY            | #CMD_UPHY_PCIE_LANE_MARGIN_STATUS     | 4                      |
	 * | #MRQ_UPHY            | #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT | 5                      |
	 * | #MRQ_UPHY            | #CMD_UPHY_PCIE_CONTROLLER_STATE       | 6                      |
	 * | #MRQ_UPHY            | #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF  | 5                      |
	 * | #MRQ_UPHY            | #CMD_UPHY_PCIE_CONFIG_VDM             | 3                      |
	 * | #MRQ_TELEMETRY       | -                                     | 8                      |
	 * | #MRQ_PWR_LIMIT       | #CMD_PWR_LIMIT_QUERY_ABI              | 8                      |
	 * | #MRQ_PWR_LIMIT       | #CMD_PWR_LIMIT_SET                    | 20                     |
	 * | #MRQ_PWR_LIMIT       | #CMD_PWR_LIMIT_GET                    | 16                     |
	 * | #MRQ_PWR_LIMIT       | #CMD_PWR_LIMIT_CURR_CAP               | 8                      |
	 * | #MRQ_GEARS           | -                                     | 0                      |
	 * | #MRQ_C2C             | #CMD_C2C_QUERY_ABI                    | 8                      |
	 * | #MRQ_C2C             | #CMD_C2C_START_INITIALIZATION         | 5                      |
	 * | #MRQ_C2C             | #CMD_C2C_GET_STATUS                   | 4                      |
	 * | #MRQ_C2C             | #CMD_C2C_HOTRESET_PREP                | 5                      |
	 * | #MRQ_C2C             | #CMD_C2C_START_HOTRESET               | 5                      |
	 * | #MRQ_THROTTLE        | #CMD_THROTTLE_QUERY_ABI               | 4                      |
	 * | #MRQ_THROTTLE        | #CMD_THROTTLE_GET_CHIPTHROT_STATUS    | 4                      |
	 * | #MRQ_PWRMODEL        | #CMD_PWRMODEL_QUERY_ABI               | 8                      |
	 * | #MRQ_PWRMODEL        | #CMD_PWRMODEL_PWR_GET                 | 16                     |
	 * | #MRQ_PWR_CNTRL       | #CMD_PWR_CNTRL_QUERY_ABI              | 8                      |
	 * | #MRQ_PWR_CNTRL       | #CMD_PWR_CNTRL_BYPASS_SET             | 12                     |
	 * | #MRQ_PWR_CNTRL       | #CMD_PWR_CNTRL_BYPASS_GET             | 8                      |
	 * @endcond
	 *
	 * @cond (bpmp_tb500)
	 * The following additional MRQs are supported on TB500 -platform:
	 *
	 * | MRQ                  | Sub-command                              | Minimum payload length |
	 * | -------------------- | ---------------------------------------- | ---------------------- |
	 * | #MRQ_PWR_LIMIT       | #CMD_PWR_LIMIT_QUERY_ABI                 | 8                      |
	 * | #MRQ_PWR_LIMIT       | #CMD_PWR_LIMIT_SET                       | 20                     |
	 * | #MRQ_PWR_LIMIT       | #CMD_PWR_LIMIT_GET                       | 16                     |
	 * | #MRQ_PWR_LIMIT       | #CMD_PWR_LIMIT_CURR_CAP                  | 8                      |
	 * | #MRQ_TELEMETRY_EX    | #CMD_TELEMETRY_EX_QUERY_ABI              | 8                      |
	 * | #MRQ_TELEMETRY_EX    | #CMD_TELEMETRY_EX_BASE_SZ_GET            | 12                     |
	 * | #MRQ_THROTTLE        | #CMD_THROTTLE_GET_CHIPTHROT_STATUS       | 4                      |
	 * | #MRQ_C2C             | #CMD_C2C_QUERY_ABI                       | 8                      |
	 * | #MRQ_C2C             | #CMD_C2C_START_INITIALIZATION            | 5                      |
	 * | #MRQ_C2C             | #CMD_C2C_GET_STATUS                      | 4                      |
	 * | #MRQ_C2C             | #CMD_C2C_HOTRESET_PREP                   | 5                      |
	 * | #MRQ_C2C             | #CMD_C2C_START_HOTRESET                  | 5                      |
	 * | MRQ_HWPM             | CMD_HWPM_QUERY_ABI                       | 4                      |
	 * | MRQ_HWPM             | CMD_HWPM_IPMU_SET_TRIGGERS               | 120                    |
	 * | MRQ_HWPM             | CMD_HWPM_IPMU_SET_PAYLOADS_SHIFTS        | 120                    |
	 * | MRQ_HWPM             | CMD_HWPM_IPMU_GET_MAX_PAYLOADS           | 0                      |
	 * | MRQ_HWPM             | CMD_HWPM_NVTHERM_SET_SAMPLE_RATE         | 4                      |
	 * | MRQ_HWPM             | CMD_HWPM_NVTHERM_SET_BUBBLE_INTERVAL     | 4                      |
	 * | MRQ_HWPM             | CMD_HWPM_NVTHERM_SET_FLEX_CHANNELS       | 120                    |
	 * | MRQ_HWPM             | CMD_HWPM_ISENSE_GET_SENSOR_NAME          | 4                      |
	 * | MRQ_HWPM             | CMD_HWPM_ISENSE_GET_SENSOR_CHANNEL       | 4                      |
	 * | MRQ_HWPM             | CMD_HWPM_ISENSE_GET_SENSOR_SCALE_FACTOR  | 4                      |
	 * | MRQ_HWPM             | CMD_HWPM_ISENSE_GET_SENSOR_OFFSET        | 4                      |
	 * | MRQ_HWPM             | CMD_HWPM_ISENSE_GET_SUM_BLOCK_NAME       | 4                      |
	 * | MRQ_HWPM             | CMD_HWPM_ISENSE_GET_SUM_BLOCK_INPUTS     | 4                      |
	 * | MRQ_DVFS             | CMD_DVFS_QUERY_ABI                       | 4                      |
	 * | MRQ_DVFS             | CMD_DVFS_SET_CTRL_STATE                  | 8                      |
	 * | MRQ_DVFS             | CMD_DVFS_SET_MGR_STATE                   | 8                      |
	 * | MRQ_PPP_PROFILE      | CMD_PPP_PROFILE_QUERY_ABI                | 8                      |
	 * | MRQ_PPP_PROFILE      | CMD_PPP_PROFILE_QUERY_MASKS              | 8                      |
	 * | MRQ_PPP_PROFILE      | CMD_PPP_CORE_QUERY_CPU_MASK              | 8                      |
	 * | MRQ_PPP_PROFILE      | CMD_PPP_AVAILABLE_QUERY                  | 4                      |
	 * @endcond
	 *
	 * @cond (bpmp_safe && bpmp_t264)
	 * The following additional MRQ is supported on functional-safety
	 * builds for the T264 platform:
	 *
	 * | MRQ                  | Sub-command                       | Minimum payload length |
	 * | -------------------- | --------------------------------- | ---------------------- |
	 * | #MRQ_CPU_NDIV_LIMITS | -                                 | 4                      |
	 * | #MRQ_STRAP           | #STRAP_SET                        | 12                     |
	 * | #MRQ_SHUTDOWN        | -                                 | 4                      |
	 * | #MRQ_FMON            | #CMD_FMON_GEAR_CLAMP              | 16                     |
	 * | #MRQ_FMON            | #CMD_FMON_GEAR_FREE               | 4                      |
	 * | #MRQ_FMON            | #CMD_FMON_GEAR_GET                | 4                      |
	 * | #MRQ_FMON            | #CMD_FMON_FAULT_STS_GET           | 8                      |
	 * | #MRQ_PCIE            | #CMD_PCIE_EP_CONTROLLER_INIT      | 5                      |
	 * | #MRQ_PCIE            | #CMD_PCIE_EP_CONTROLLER_OFF       | 5                      |
	 * | #MRQ_CR7             | #CMD_CR7_ENTRY                    | 12                     |
	 * | #MRQ_CR7             | #CMD_CR7_EXIT                     | 12                     |
	 * | #MRQ_SLC             | #CMD_SLC_QUERY_ABI                | 8                      |
	 * | #MRQ_SLC             | #CMD_SLC_BYPASS_SET               | 8                      |
	 * | #MRQ_SLC             | #CMD_SLC_BYPASS_GET               | 4                      |
	 * @endcond
	 *
	 * @cond (!bpmp_safe && bpmp_t264)
	 * The following additional MRQs are supported on non-functional-safety
	 * builds for the T264 -platform:
	 *
	 * | MRQ                  | Sub-command                       | Minimum payload length |
	 * | -------------------- | --------------------------------- | ---------------------- |
	 * | #MRQ_CPU_NDIV_LIMITS | -                                 | 4                      |
	 * | #MRQ_STRAP           | #STRAP_SET                        | 12                     |
	 * | #MRQ_SHUTDOWN        | -                                 | 4                      |
	 * | #MRQ_FMON            | #CMD_FMON_GEAR_CLAMP              | 16                     |
	 * | #MRQ_FMON            | #CMD_FMON_GEAR_FREE               | 4                      |
	 * | #MRQ_FMON            | #CMD_FMON_GEAR_GET                | 4                      |
	 * | #MRQ_FMON            | #CMD_FMON_FAULT_STS_GET           | 8                      |
	 * | #MRQ_OC_STATUS       | -                                 | 0                      |
	 * | #MRQ_PCIE            | #CMD_PCIE_EP_CONTROLLER_INIT      | 5                      |
	 * | #MRQ_PCIE            | #CMD_PCIE_EP_CONTROLLER_OFF       | 5                      |
	 * | #MRQ_PCIE            | #CMD_PCIE_RP_CONTROLLER_OFF       | 5                      |
	 * | #MRQ_CR7             | #CMD_CR7_ENTRY                    | 12                     |
	 * | #MRQ_CR7             | #CMD_CR7_EXIT                     | 12                     |
	 * | #MRQ_SLC             | #CMD_SLC_QUERY_ABI                | 8                      |
	 * | #MRQ_SLC             | #CMD_SLC_BYPASS_SET               | 8                      |
	 * | #MRQ_SLC             | #CMD_SLC_BYPASS_GET               | 4                      |
	 * | #MRQ_ISO_CLIENT      | #CMD_ISO_CLIENT_QUERY_ABI         | 8                      |
	 * | #MRQ_ISO_CLIENT      | #CMD_ISO_CLIENT_CALCULATE_LA      | 16                     |
	 * | #MRQ_ISO_CLIENT      | #CMD_ISO_CLIENT_SET_LA            | 16                     |
	 * | #MRQ_ISO_CLIENT      | #CMD_ISO_CLIENT_GET_MAX_BW        | 8                      |
	 * | #MRQ_BWMGR_INT       | #CMD_BWMGR_INT_QUERY_ABI          | 8                      |
	 * | #MRQ_BWMGR_INT       | #CMD_BWMGR_INT_CALC_AND_SET       | 16                     |
	 * | #MRQ_BWMGR_INT       | #CMD_BWMGR_INT_CAP_SET            | 8                      |
	 * | #MRQ_BWMGR_INT       | #CMD_BWMGR_INT_CURR_AVAILABLE_BW  | 8                      |
	 * | #MRQ_BWMGR_INT       | #CMD_BWMGR_INT_GET_LAST_REQUEST   | 9                      |
	 * @endcond
	 *
	 * **crc16**
	 *
	 * CRC16 using polynomial x^16 + x^14 + x^12 + x^11 + x^8 + x^5 + x^4 + x^2 + 1
	 * and initialization value 0x4657. The CRC is calculated over all bytes of the message
	 * including this header. However the crc16 field is considered to be set to 0 when
	 * calculating the CRC. Only used when #BPMP_MAIL_CRC_PRESENT is set. If
	 * #BPMP_MAIL_CRC_PRESENT is set and this field does not match the CRC as
	 * calculated by BPMP, -#BPMP_EBADMSG will be returned and the request will
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
//adoc: tag::bpmp_dmce_mrq_shutdown[]
#define MRQ_SHUTDOWN		49U
//adoc: end::bpmp_dmce_mrq_shutdown[]
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
#define MRQ_C2C			85U
#define MRQ_THROTTLE		86U
#define MRQ_PWRMODEL		87U
#define MRQ_PCIE		88U
#define MRQ_PWR_CNTRL		89U
#define MRQ_CR7			90U
#define MRQ_SLC			91U
#define MRQ_TELEMETRY_EX	92U
#define MRQ_HWPM		93U
#define MRQ_DVFS		94U
#define MRQ_PPP_PROFILE		95U

/**
 * @brief Maximum MRQ code to be sent by CPU software to
 * BPMP. Subject to change in future
 */
#define MAX_CPU_MRQ_ID		95U

/** @} */

/**
 * @addtogroup MRQ_Payloads
 * @{
 *   @defgroup Ping Ping
 *   @defgroup Query_Tag Query Tag
 *   @defgroup Debugfs Debug File System
 *   @defgroup Reset Reset
 *   @defgroup I2C I2C
 *   @defgroup Clocks Clocks
 *   @defgroup ABI_info ABI Info
 *   @defgroup Powergating Power Gating
 *   @defgroup Thermal Thermal
 *   @defgroup Throttle Throttle
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
 *   @defgroup Shutdown Shutdown
 *   @defgroup BWMGR_INT Bandwidth Manager Integrated
 *   @defgroup C2C C2C
 *   @defgroup Pwrmodel Power Model
 *   @defgroup Pwrcntrl Power Controllers
 * @cond bpmp_t264
 * *  @defgroup PCIE PCIE
 * *  @defgroup CR7 CR7
 * *  @defgroup Slc Slc
 * @endcond
 * @cond bpmp_tb500
 * *  @defgroup Telemetry_ex Telemetry Expanded
 * *  @defgroup HWPM Hardware Performance Monitoring
 * *  @defgroup DVFS Dynamic Voltage and Frequency Scaling
 * *  @defgroup PPP power/performance profiles
 * @endcond
 * @} MRQ_Payloads
 */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_PING
 * @brief A simple ping
 *
 * * Initiators: Any
 * * Targets: Any
 * * Request Payload: @ref mrq_ping_request
 * * Response Payload: @ref mrq_ping_response
 *
 * @ingroup MRQ_Codes
 * @def MRQ_THREADED_PING
 * @brief A deeper ping
 *
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
 * recipient. The response from the recipient is computed based on the
 * mrq_ping_request::challenge -value.
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
 * @details
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
 * Use #MRQ_QUERY_FW_TAG instead.
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
 * @deprecated Use #MRQ_DEBUG instead.
 *
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
 * @brief Request with #MRQ_DEBUG.
 *
 * The sender of an MRQ_DEBUG message uses #cmd to specify a debugfs
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
 * @brief Interact with BPMP-FW debugfs file nodes. Use message payload
 * for exchanging data. This is functionally equivalent to
 * the deprecated MRQ_DEBUGFS but the way in which data is exchanged is
 * different. When software running on CPU tries to read a debugfs file,
 * the file path and read data will be stored in message payload.
 * Since the message payload size is limited, a debugfs file
 * transaction might require multiple frames of data exchanged
 * between BPMP and CPU until the transaction completes.
 *
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_debug_request
 * * Response Payload: @ref mrq_debug_response
 */

/** @ingroup Debugfs */
enum mrq_debug_commands {
	/**
	 * @brief Open file represented by the path in
	 * cmd_debug_fopen_request::name for read operation
	 */
	CMD_DEBUG_OPEN_RO = 0,
	/**
	 * @brief Open file represented by the path in
	 * cmd_debug_fopen_request::name for write operation
	 */
	CMD_DEBUG_OPEN_WO = 1,
	/**
	 * @brief Perform read on a previously opened file handle represented
	 *        by the cmd_debug_fread_request::fd -value.
	 */
	CMD_DEBUG_READ = 2,
	/**
	 * @brief Perform write on a previously opened file handle represented
	 *        by the cmd_debug_fwrite_request::fd -value.
	 */
	CMD_DEBUG_WRITE = 3,
	/**
	 * @brief Close previously opened file handle.
	 */
	CMD_DEBUG_CLOSE = 4,
	/**
	 * @brief Not a command, represents maximum number of supported
	 *        sub-commands
	 */
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
 * @brief Parameters for #CMD_DEBUG_OPEN_RO and #CMD_DEBUG_OPEN_WO -commands
 */
struct cmd_debug_fopen_request {
	/**
	 * @brief File name - Null-terminated string with maximum
	 *        length including the terminator defined by the
	 *        #DEBUG_FNAME_MAX_SZ -preprocessor constant.
	 */
	char name[DEBUG_FNAME_MAX_SZ];
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Response data for #CMD_DEBUG_OPEN_RO and #CMD_DEBUG_OPEN_WO commands
 */
struct cmd_debug_fopen_response {
	/** @brief Identifier for file access */
	uint32_t fd;
	/** @brief Data length. File data size for READ command.
	 *         Maximum allowed length for WRITE command
	 */
	uint32_t datalen;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Parameters for #CMD_DEBUG_READ command
 */
struct cmd_debug_fread_request {
	/**
	 * @brief File access identifier received in response
	 *        to #CMD_DEBUG_OPEN_RO request
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
 * @brief Response data for #CMD_DEBUG_READ command
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
 * @brief Parameters for #CMD_DEBUG_WRITE command
 */
struct cmd_debug_fwrite_request {
	/** @brief File access identifier received in response
	 *         to prior #CMD_DEBUG_OPEN_RO -request
	 */
	uint32_t fd;
	/** @brief Size of write data in bytes */
	uint32_t datalen;
	/** @brief Data to be written */
	char data[DEBUG_WRITE_MAX_SZ];
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Parameters for #CMD_DEBUG_CLOSE command
 */
struct cmd_debug_fclose_request {
	/**
	 * @brief File access identifier received in prior response
	 *        to #CMD_DEBUG_OPEN_RO or #CMD_DEBUG_OPEN_WO -request.
	 */
	uint32_t fd;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 * @brief Request with #MRQ_DEBUG.
 *
 * The sender of an #MRQ_DEBUG message uses mrq_debug_request::cmd to specify
 * which debugfs sub-command to execute. Legal sub-commands are the values
 * specified in the @ref mrq_debug_commands -enumeration. Each sub-command
 * requires a specific additional payload of data according to the following
 * table:
 *
 * |Sub-command         |Payload structure          |
 * |--------------------|---------------------------|
 * |#CMD_DEBUG_OPEN_RO  |cmd_debug_fopen_request    |
 * |#CMD_DEBUG_OPEN_WO  |cmd_debug_fopen_request    |
 * |#CMD_DEBUG_READ     |cmd_debug_fread_request    |
 * |#CMD_DEBUG_WRITE    |cmd_debug_fwrite_request   |
 * |#CMD_DEBUG_CLOSE    |cmd_debug_fclose_request   |
 */
struct mrq_debug_request {
	/** @brief Sub-command identifier from @ref mrq_debug_commands */
	uint32_t cmd;
	union {
		/**
		 * @brief Request payload for #CMD_DEBUG_OPEN_RO and
		 *        #CMD_DEBUG_OPEN_WO sub-commands
		 */
		struct cmd_debug_fopen_request fop;
		/** @brief Request payload for #CMD_DEBUG_READ sub-command */
		struct cmd_debug_fread_request frd;
		/** @brief Request payload for #CMD_DEBUG_WRITE sub-command */
		struct cmd_debug_fwrite_request fwr;
		/** @brief Request payload for #CMD_DEBUG_CLOSE sub-command */
		struct cmd_debug_fclose_request fcl;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @ingroup Debugfs
 */
struct mrq_debug_response {
	union {
		/**
		 * @brief Response data for the #CMD_DEBUG_OPEN_RO and
		 *        #CMD_DEBUG_OPEN_WO sub-commands
		 */
		struct cmd_debug_fopen_response fop;
		/** @brief Response data for the #CMD_DEBUG_READ sub-command */
		struct cmd_debug_fread_response frd;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @ingroup MRQ_Codes
 * @def MRQ_RESET
 * @brief Reset an IP block
 *
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_reset_request
 * * Response Payload: @ref mrq_reset_response
 *
 * @addtogroup Reset
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_RESET
 */
enum mrq_reset_commands {
	/**
	 * @brief Assert module reset
	 *
	 * mrq_response::err is
	 * * 0 if the operation was successful
	 * * -#BPMP_EINVAL if mrq_reset_request::reset_id is invalid
	 * * -#BPMP_EACCES if mrq master is not an owner of target domain reset
	 * * -#BPMP_ENOTSUP if target domain h/w state does not allow reset
	 */
	CMD_RESET_ASSERT = 1,
	/**
	 * @brief Deassert module reset
	 *
	 * mrq_response::err is
	 * * 0 if the operation was successful
	 * * -#BPMP_EINVAL if mrq_reset_request::reset_id is invalid
	 * * -#BPMP_EACCES if mrq master is not an owner of target domain reset
	 * * -#BPMP_ENOTSUP if target domain h/w state does not allow reset
	 */
	CMD_RESET_DEASSERT = 2,
	/**
	 * @brief Assert and deassert the module reset
	 *
	 * mrq_response::err is
	 * * 0 if the operation was successful
	 * * -#BPMP_EINVAL if mrq_reset_request::reset_id is invalid
	 * * -#BPMP_EACCES if mrq master is not an owner of target domain reset
	 * * -#BPMP_ENOTSUP if target domain h/w state does not allow reset
	 */
	CMD_RESET_MODULE = 3,
	/**
	 * @brief Get the highest reset ID
	 *
	 * mrq_response::err is
	 * * 0 if the operation was successful
	 * * -#BPMP_ENODEV if no reset domains are supported (number of IDs is 0)
	 */
	CMD_RESET_GET_MAX_ID = 4,

	/** @brief Not part of ABI and subject to change */
	CMD_RESET_MAX,
};

/**
 * @brief Request with #MRQ_RESET
 *
 * Used by the sender of an #MRQ_RESET message to request BPMP to
 * assert or deassert a given reset line.
 */
struct mrq_reset_request {
	/** @brief Reset action to perform, from @ref mrq_reset_commands */
	uint32_t cmd;
	/** @brief ID of the reset to affected, from @ref bpmp_reset_ids */
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
 * in the following table:
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
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_i2c_request
 * * Response Payload: @ref mrq_i2c_response
 *
 * @addtogroup I2C
 * @{
 */

/**
 * @brief Size of the cmd_i2c_xfer_request::data_buf -member array in bytes.
 */
#define TEGRA_I2C_IPC_MAX_IN_BUF_SIZE	(MSG_DATA_MIN_SZ - 12U)

/**
 * @brief Size of the cmd_i2c_xfer_response::data_buf -member array in bytes.
 */
#define TEGRA_I2C_IPC_MAX_OUT_BUF_SIZE	(MSG_DATA_MIN_SZ - 4U)

/**
 * @defgroup seriali2c_flags I2C flags
 *
 * @brief I2C transaction modifier flags for each transaction segment
 * in #MRQ_I2C subcommand CMD_I2C_XFER
 */

/**
 * @addtogroup seriali2c_flags
 * @{
 */

/** @brief when set, use 10-bit I2C slave address */
#define SERIALI2C_TEN           0x0010U
/** @brief when set, perform a Read transaction */
#define SERIALI2C_RD            0x0001U
/**
 * @brief when set, no repeated START is issued between the segments
 * of transaction. This flag is ignored for the first segment as any
 * transaction always starts with a START condition
 */
#define SERIALI2C_NOSTART       0x4000U
/**
 * @brief when set, a no-ACK from slave device is ignored and treated
 * always as success
 */
#define SERIALI2C_IGNORE_NAK    0x1000U
/** @} seriali2c_flags */

/** brief Unused flag. Retained for backwards compatibility. */
#define SERIALI2C_STOP          0x8000U
/** brief Unused flag. Retained for backwards compatibility. */
#define SERIALI2C_REV_DIR_ADDR  0x2000U
/** brief Unused flag. Retained for backwards compatibility. */
#define SERIALI2C_NO_RD_ACK     0x0800U
/** brief Unused flag. Retained for backwards compatibility. */
#define SERIALI2C_RECV_LEN      0x0400U

/**
 * @brief Supported I2C sub-command identifiers
 */
enum mrq_i2c_commands {
	/** @brief Perform an I2C transaction */
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
	/** @brief Bitmask of @ref seriali2c_flags */
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
	 * @cond (bpmp_t186 || bpmp_t194 || bpmp_t234 || bpmp_t238 || bpmp_t264)
	 * Must be set to 5.
	 * @endcond
	 *
	 * @cond (bpmp_th500)
	 * Must be set to 1.
	 * @endcond
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
	/** @brief I2C read data */
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
 * mrq_response::err value for this response is defined as:
 *
 * | Value              | Description                                                         |
 * |--------------------|---------------------------------------------------------------------|
 * | 0                  | Success                                                             |
 * | -#BPMP_EBADCMD     | mrq_i2c_request::cmd is other than 1                                |
 * | -#BPMP_EINVAL      | cmd_i2c_xfer_request does not contain correctly formatted request   |
 * | -#BPMP_ENODEV      | cmd_i2c_xfer_request::bus_id is not supported by BPMP               |
 * | -#BPMP_EACCES      | I2C transaction is not allowed due to firewall rules                |
 * | -#BPMP_ETIMEDOUT   | I2C transaction times out                                           |
 * | -#BPMP_ENXIO       | I2C slave device does not reply with ACK to the transaction         |
 * | -#BPMP_EAGAIN      | ARB_LOST condition is detected by the I2C controller                |
 * | -#BPMP_EIO         | Any other I2C controller error code than NO_ACK or ARB_LOST         |
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
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_clk_request
 * * Response Payload: @ref mrq_clk_response
 *
 * @addtogroup Clocks
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_CLK
 */
enum mrq_clk_commands {
	/** Get clock rate */
	CMD_CLK_GET_RATE = 1,

	/** Set clock rate */
	CMD_CLK_SET_RATE = 2,

	/** Get attainable clock rate closer to a given rate */
	CMD_CLK_ROUND_RATE = 3,

	/** Get parent clock identifier for a given clock */
	CMD_CLK_GET_PARENT = 4,

	/** Change clock parent */
	CMD_CLK_SET_PARENT = 5,

	/** Get clock enable status */
	CMD_CLK_IS_ENABLED = 6,

	/** Enable a clock */
	CMD_CLK_ENABLE = 7,

	/** Disable a clock */
	CMD_CLK_DISABLE = 8,

	/** Get all information about a clock */
	CMD_CLK_GET_ALL_INFO = 14,

	/** Get largest supported clock identifier */
	CMD_CLK_GET_MAX_CLK_ID = 15,

	/** Get clock maximum rate at VMIN */
	CMD_CLK_GET_FMAX_AT_VMIN = 16,

	/** Largest supported #MRQ_CLK sub-command identifier + 1 */
	CMD_CLK_MAX,
};

/**
 * Flag bit set in cmd_clk_get_all_info_response::flags -field when clock
 * supports changing of the parent clock at runtime.
 */
#define BPMP_CLK_HAS_MUX	(1U << 0U)

/**
 * Flag bit set in cmd_clk_get_all_info_response::flags -field when clock
 * supports changing the clock rate at runtime.
 */
#define BPMP_CLK_HAS_SET_RATE	(1U << 1U)

/**
 * Flag bit set in cmd_clk_get_all_info_response::flags -field when clock is a
 * root clock without visible parents.
 */
#define BPMP_CLK_IS_ROOT	(1U << 2U)

#define BPMP_CLK_IS_VAR_ROOT	(1U << 3U)

/**
 * @brief Protection against rate and parent changes
 *
 * #MRQ_CLK command #CMD_CLK_SET_RATE or #MRQ_CLK command #CMD_CLK_SET_PARENT
 * will return -#BPMP_EACCES.
 */
#define BPMP_CLK_RATE_PARENT_CHANGE_DENIED (1U << 30)

/**
 * @brief Protection against state changes
 *
 * #MRQ_CLK command #CMD_CLK_ENABLE or #MRQ_CLK command #CMD_CLK_DISABLE
 * will return -#BPMP_EACCES.
 */
#define BPMP_CLK_STATE_CHANGE_DENIED (1U << 31)

/**
 * Size of the cmd_clk_get_all_info_response::name -array in number
 * of elements.
 */
#define MRQ_CLK_NAME_MAXLEN	40U

/**
 * @brief Maximum number of elements in parent_id arrays of clock info responses.
 */
#define MRQ_CLK_MAX_PARENTS	16U

/**
 * @brief Request payload for #MRQ_CLK sub-command #CMD_CLK_GET_RATE
 *
 * This structure is an empty placeholder for future expansion of this
 * sub-command.
 */
struct cmd_clk_get_rate_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_CLK sub-command #CMD_CLK_GET_RATE
 */
struct cmd_clk_get_rate_response {
	/**
	 * Current rate of the given clock in Hz if mrq_response::err is 0 to
	 * indicate successful #CMD_CLK_GET_RATE -request.
	 */
	int64_t rate;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_CLK sub-command #CMD_CLK_SET_RATE
 */
struct cmd_clk_set_rate_request {
	/** Unused / reserved field. */
	int32_t unused;

	/** Requested rate of the clock in Hz. */
	int64_t rate;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_CLK sub-command #CMD_CLK_SET_RATE
 */
struct cmd_clk_set_rate_response {
	/**
	 * If request was successful (mrq_response::err is 0), set to the new
	 * rate of the given clock in Hz.
	 */
	int64_t rate;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_CLK sub-command #CMD_CLK_ROUND_RATE
 */
struct cmd_clk_round_rate_request {
	/** Unused / reserved field. */
	int32_t unused;

	/** Target rate for the clock */
	int64_t rate;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_CLK sub-command #CMD_CLK_ROUND_RATE
 */
struct cmd_clk_round_rate_response {
	/**
	 * The attainable rate if request was successful
	 * (mrq_response::err is 0).
	 */
	int64_t rate;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_CLK sub-command #CMD_CLK_GET_PARENT
 *
 * This structure is an empty placeholder for future expansion of this
 * sub-command.
 */
struct cmd_clk_get_parent_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_CLK sub-command #CMD_CLK_GET_PARENT
 */
struct cmd_clk_get_parent_response {
	/**
	 * The clock identifier of the parent clock if request was successful
	 * (mrq_response::err is 0).
	 */
	uint32_t parent_id;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_CLK sub-command #CMD_CLK_SET_PARENT
 */
struct cmd_clk_set_parent_request {
	/**
	 * The clock identifier of the new parent clock.
	 */
	uint32_t parent_id;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_CLK sub-command #CMD_CLK_SET_PARENT
 */
struct cmd_clk_set_parent_response {
	/**
	 * The clock identifier of the new parent clock if request was
	 * successful (mrq_response::err is 0).
	 */
	uint32_t parent_id;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #CMD_CLK_IS_ENABLED -sub-command
 *
 * This structure is an empty placeholder for future expansion of this
 * sub-command.
 */
struct cmd_clk_is_enabled_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_CLK sub-command #CMD_CLK_IS_ENABLED
 */
struct cmd_clk_is_enabled_response {
	/**
	 * @brief The state of the clock that has been successfully
	 * requested with #CMD_CLK_ENABLE or #CMD_CLK_DISABLE by the
	 * master invoking the command earlier.
	 *
	 * The state may not reflect the physical state of the clock
	 * if there are some other masters requesting it to be
	 * enabled. Valid values:
	 *
	 * * Value 0: The clock is disabled,
	 * * Value 1: The clock is enabled.
	 */
	int32_t state;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_CLK sub-command #CMD_CLK_ENABLE
 *
 * This structure is an empty placeholder for future expansion of this
 * sub-command.
 */
struct cmd_clk_enable_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_CLK sub-command #CMD_CLK_ENABLE
 *
 * This structure is an empty placeholder for future expansion of this
 * sub-command.
 */
struct cmd_clk_enable_response {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_CLK sub-command #CMD_CLK_DISABLE
 *
 * This structure is an empty placeholder for future expansion of this
 * sub-command.
 */
struct cmd_clk_disable_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_CLK sub-command #CMD_CLK_DISABLE
 *
 * This structure is an empty placeholder for future expansion of this
 * sub-command.
 */
struct cmd_clk_disable_response {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_CLK sub-command #CMD_CLK_GET_ALL_INFO
 *
 * This structure is an empty placeholder for future expansion of this
 * sub-command.
 */
struct cmd_clk_get_all_info_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;


/**
 * @brief Response payload for #MRQ_CLK sub-command #CMD_CLK_GET_ALL_INFO
 *
 * The values in the response are only set and valid if request status in
 * mrq_response::err is 0.
 */
struct cmd_clk_get_all_info_response {
	/**
	 * State / informational flags for the clock:
	 *
	 * | Flag bit               | Description                              |
	 * |------------------------|------------------------------------------|
	 * | #BPMP_CLK_IS_ROOT      | Clock is a root clock.                   |
	 * | #BPMP_CLK_HAS_MUX      | Clock supports changing of parent clock. |
	 * | #BPMP_CLK_HAS_SET_RATE | Clock supports changing clock rate.      |
	 */
	uint32_t flags;

	/**
	 * Current parent clock identifier.
	 */
	uint32_t parent;

	/**
	 * Array of possible parent clock identifiers.
	 */
	uint32_t parents[MRQ_CLK_MAX_PARENTS];

	/**
	 * Number of identifiers in the #parents -array.
	 */
	uint8_t num_parents;

	/**
	 * Friendly name of the clock, truncated to fit the array
	 * and null-terminated.
	 */
	uint8_t name[MRQ_CLK_NAME_MAXLEN];
} BPMP_ABI_PACKED;


/**
 * @brief Request payload for #MRQ_CLK sub-command #CMD_CLK_GET_MAX_CLK_ID
 *
 * This structure is an empty placeholder for future expansion of this
 * sub-command.
 */
struct cmd_clk_get_max_clk_id_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_CLK sub-command #CMD_CLK_GET_MAX_CLK_ID
 */
struct cmd_clk_get_max_clk_id_response {
	/** @brief Largest supported clock identifier. */
	uint32_t max_id;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_CLK sub-command #CMD_CLK_GET_FMAX_AT_VMIN
 *
 * This structure is an empty placeholder for future expansion of this
 * sub-command.
 */
struct cmd_clk_get_fmax_at_vmin_request {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_CLK sub-command #CMD_CLK_GET_FMAX_AT_VMIN
 */
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
 * |Sub-command                 |Payload                      |
 * |----------------------------|-----------------------------|
 * |#CMD_CLK_GET_RATE           |-                            |
 * |#CMD_CLK_SET_RATE           |#cmd_clk_set_rate_request    |
 * |#CMD_CLK_ROUND_RATE         |#cmd_clk_round_rate_request  |
 * |#CMD_CLK_GET_PARENT         |-                            |
 * |#CMD_CLK_SET_PARENT         |#cmd_clk_set_parent_request  |
 * |#CMD_CLK_IS_ENABLED         |-                            |
 * |#CMD_CLK_ENABLE             |-                            |
 * |#CMD_CLK_DISABLE            |-                            |
 * |#CMD_CLK_GET_ALL_INFO       |-                            |
 * |#CMD_CLK_GET_MAX_CLK_ID     |-                            |
 * |#CMD_CLK_GET_FMAX_AT_VMIN   |-                            |
 */

struct mrq_clk_request {
	/** @brief Sub-command and clock id concatenated to 32-bit word.
	 *
	 * - bits[31..24] is the sub-command ID from @ref mrq_clk_commands.
	 * - bits[23..0] is the clock identifier from @ref bpmp_clock_ids.
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
 * the following table:
 *
 * |Sub-command                 |Payload                            |
 * |----------------------------|-----------------------------------|
 * |#CMD_CLK_GET_RATE           |#cmd_clk_get_rate_response         |
 * |#CMD_CLK_SET_RATE           |#cmd_clk_set_rate_response         |
 * |#CMD_CLK_ROUND_RATE         |#cmd_clk_round_rate_response       |
 * |#CMD_CLK_GET_PARENT         |#cmd_clk_get_parent_response       |
 * |#CMD_CLK_SET_PARENT         |#cmd_clk_set_parent_response       |
 * |#CMD_CLK_IS_ENABLED         |#cmd_clk_is_enabled_response       |
 * |#CMD_CLK_ENABLE             |-                                  |
 * |#CMD_CLK_DISABLE            |-                                  |
 * |#CMD_CLK_GET_ALL_INFO       |#cmd_clk_get_all_info_response     |
 * |#CMD_CLK_GET_MAX_CLK_ID     |#cmd_clk_get_max_clk_id_response   |
 * |#CMD_CLK_GET_FMAX_AT_VMIN   |#cmd_clk_get_fmax_at_vmin_response |
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
		struct cmd_clk_get_fmax_at_vmin_response clk_get_fmax_at_vmin;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} Clocks */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_QUERY_ABI
 * @brief Check if an MRQ is implemented
 *
 * * Initiators: Any
 * * Targets: Any except DMCE
 * * Request Payload: @ref mrq_query_abi_request
 * * Response Payload: @ref mrq_query_abi_response
 */

/**
 * @ingroup ABI_info
 * @brief Request with #MRQ_QUERY_ABI
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
	/**
	 * This response field is set to:
	 * - 0 if queried MRQ is supported, or
	 * - -#BPMP_ENODEV if queried MRQ is not supported
	 */
	int32_t status;
} BPMP_ABI_PACKED;

/**
 *
 * @ingroup MRQ_Codes
 * @def MRQ_PG
 * @brief Control power-gating state of a partition.
 *
 * @cond (bpmp_t194 || bpmp_t186)
 * @note On T194 and earlier BPMP-FW forcefully turns off some partitions as
 * part of SC7 entry because their state cannot be adequately restored on exit.
 * Therefore, it is recommended to power off all domains via MRQ_PG prior to SC7
 * entry.
 * See @ref bpmp_pdomain_ids for further detail.
 * @endcond
 *
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_pg_request
 * * Response Payload: @ref mrq_pg_response
 *
 * @addtogroup Powergating
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_PG -command.
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
	 * mrq_response:err for this sub-command is defined as:
	 *
	 * | Value          | Description                                                              |
	 * | -------------- | ------------------------------------------------------------------------ |
	 * | 0              | Request was successful.                                                  |
	 * | -#BPMP_EINVAL  | Invalid request parameters were provided.                                |
	 * | -#BPMP_EACCES  | Permission denied or always-off partition was attempted to be turned on. |
	 * | Any other <0   | Internal error while performing the operation.                           |
	 */
	CMD_PG_SET_STATE = 1,

	/**
	 * @brief Get the current state of specified power domain. The
	 * possible values for power domains are defined in enum
	 * pg_states
	 *
	 * mrq_response:err for this sub-command is defined as:
	 *
	 * | Value          | Description                                    |
	 * | -------------- | ---------------------------------------------- |
	 * | 0              | Request was successful.                        |
	 * | -#BPMP_EINVAL  | Invalid request parameters were provided.      |
	 * | Any other <0   | Internal error while performing the operation. |
	 */
	CMD_PG_GET_STATE = 2,

	/**
	 * @brief Get the name string of specified power domain id.
	 *
	 * mrq_response:err for this sub-command is defined as:
	 *
	 * | Value          | Description                                    |
	 * | -------------- | ---------------------------------------------- |
	 * | 0              | Request was successful.                        |
	 * | -#BPMP_EINVAL  | Invalid request parameters were provided.      |
	 * | Any other <0   | Internal error while performing the operation. |
	 */
	CMD_PG_GET_NAME = 3,


	/**
	 * @brief Get the highest power domain id in the system. Not
	 * all IDs between 0 and max_id are valid IDs.
	 *
	 * mrq_response:err for this sub-command is defined as:
	 *
	 * | Value          | Description                                    |
	 * | -------------- | ---------------------------------------------- |
	 * | 0              | Request was successful.                        |
	 * | -#BPMP_EINVAL  | Invalid request parameters were provided.      |
	 * | Any other <0   | Internal error while performing the operation. |
	 */
	CMD_PG_GET_MAX_ID = 4,
};

#define MRQ_PG_NAME_MAXLEN	40

/**
 * @brief State value for the cmd_pg_set_state_request::state -field.
 */
enum pg_states {
	/** @brief Power domain is OFF */
	PG_STATE_OFF = 0,
	/** @brief Power domain is ON */
	PG_STATE_ON = 1,

	/** @cond bpmp_t186 */
	/**
	 * @brief a legacy state where power domain and the clock
	 * associated to the domain are ON.
	 * This state is only supported in T186, and the use of it is
	 * deprecated.
	 */
	PG_STATE_RUNNING = 2,
	/** @endcond */
};

struct cmd_pg_query_abi_request {
	/** #MRQ_PG sub-command identifier from @ref mrq_pg_cmd */
	uint32_t type;
} BPMP_ABI_PACKED;

struct cmd_pg_set_state_request {
	/** One of the state values from @ref pg_states */
	uint32_t state;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_PG sub-command #CMD_PG_GET_STATE
 */
struct cmd_pg_get_state_response {
	/**
	 * @brief The state of the power partition that has been
	 * successfully requested by the master earlier using #MRQ_PG
	 * command #CMD_PG_SET_STATE.
	 *
	 * The state may not reflect the physical state of the power
	 * partition if there are some other masters requesting it to
	 * be enabled.
	 *
	 * See @ref pg_states for possible values.
	 */
	uint32_t state;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_PG sub-command #CMD_PG_GET_NAME
 */
struct cmd_pg_get_name_response {
	/**
	 * @brief On successful response contains the null-terminated
	 *        friendly name of the requested power-domain.
	 */
	uint8_t name[MRQ_PG_NAME_MAXLEN];
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_PG sub-command #CMD_PG_GET_MAX_ID
 */
struct cmd_pg_get_max_id_response {
	uint32_t max_id;
} BPMP_ABI_PACKED;

/**
 * @brief Request with #MRQ_PG
 *
 * Used by the sender of an #MRQ_PG message to control power
 * partitions. The expected payload depends on the sub-command identifier.
 * Some sub-commands require no additional data while others have a sub-command
 * specific payload:
 *
 * |Sub-command                 |Payload                    |
 * |----------------------------|---------------------------|
 * |#CMD_PG_QUERY_ABI           | #cmd_pg_query_abi_request |
 * |#CMD_PG_SET_STATE           | #cmd_pg_set_state_request |
 * |#CMD_PG_GET_STATE           | -                         |
 * |#CMD_PG_GET_NAME            | -                         |
 * |#CMD_PG_GET_MAX_ID          | -                         |
 *
 */
struct mrq_pg_request {
	/** @brief Sub-command identifier from @ref mrq_pg_cmd. */
	uint32_t cmd;

	/**
	 * @brief Power-domain identifier
	 */
	uint32_t id;

	union {
		struct cmd_pg_query_abi_request query_abi;
		struct cmd_pg_set_state_request set_state;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response to #MRQ_PG
 *
 * Some of the #MRQ_PG sub-commands return a sub-command -specific payload
 * as specified in the following table:
 *
 * |Sub-command         |Payload                       |
 * |--------------------|------------------------------|
 * |#CMD_PG_QUERY_ABI   | -                            |
 * |#CMD_PG_SET_STATE   | -                            |
 * |#CMD_PG_GET_STATE   | #cmd_pg_get_state_response   |
 * |#CMD_PG_GET_NAME    | #cmd_pg_get_name_response    |
 * |#CMD_PG_GET_MAX_ID  | #cmd_pg_get_max_id_response  |
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
 * * Initiators: Any
 * * Targets: Any
 * * Request Payload: #mrq_thermal_host_to_bpmp_request
 * * Response Payload: #mrq_thermal_bpmp_to_host_response
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

/**
 * @brief Sub-command identifiers for Host->BPMP #MRQ_THERMAL -command.
 */
enum mrq_thermal_host_to_bpmp_cmd {
	/**
	 * @brief Check whether BPMP-FW supports the specified
	 *        #MRQ_THERMAL sub-command.
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
	 * mrq_response::err value for this sub-command is:
	 *
	 * | Value          | Description                               |
	 * | -------------- | ----------------------------------------- |
	 * | 0              | Temperature query succeeded.              |
	 * | -#BPMP_EINVAL  | Invalid request parameters.               |
	 * | -#BPMP_ENOENT  | No driver registered for thermal zone.    |
	 * | -#BPMP_EFAULT  | Problem reading temperature measurement.  |
	 */
	CMD_THERMAL_GET_TEMP = 1,

	/**
	 * @cond (!bpmp_safe && !bpmp_t264)
	 * @brief Enable or disable and set the lower and upper
	 *   thermal limits for a thermal trip point. Each zone has
	 *   one trip point.
	 *
	 * Host needs to supply request parameters. Once the
	 * temperature hits a trip point, the BPMP will send a message
	 * to the CPU having MRQ command identifier equal to #MRQ_THERMAL and
	 * sub-command identifier equal to #CMD_THERMAL_HOST_TRIP_REACHED.
	 *
	 * If #CMD_THERMAL_SET_TRIP -sub-command is issued for a
	 * thermal zone that is currently power gated and unable to
	 * report temperature, a temperature of -256C is used as
	 * temperature for evaluation of the trip.
	 *
	 * mrq_response::err for this sub-command is defined as:
	 *
	 * | Value           | Description                            |
	 * | --------------- | -------------------------------------- |
	 * | 0               | Trip successfully set.                 |
	 * | -#BPMP_EINVAL   | Invalid request parameters.            |
	 * | -#BPMP_ENOENT   | No driver registered for thermal zone. |
	 * | -#BPMP_EFAULT   | Problem setting trip point.            |
	 */
	CMD_THERMAL_SET_TRIP = 2,
	 /** @endcond */

	/**
	 * @brief Get the number of supported thermal zones.
	 *
	 * No request parameters required.
	 *
	 * mrq_response::err is always 0, indicating success.
	 */
	CMD_THERMAL_GET_NUM_ZONES = 3,

	/**
	 * @brief Get the thermal trip value of the specified zone.
	 *
	 * Host needs to supply request parameters.
	 *
	 * mrq_response::err for this sub-command is defined as:
	 *
	 * | Value           | Description                            |
	 * | --------------- | -------------------------------------- |
	 * | 0               | Valid zone information returned.       |
	 * | -#BPMP_EINVAL   | Invalid request parameters.            |
	 * | -#BPMP_ENOENT   | No driver registered for thermal zone. |
	 * | -#BPMP_ERANGE   | Thermal trip is invalid or disabled.   |
	 * | -#BPMP_EFAULT   | Problem reading zone information.      |
	 */
	CMD_THERMAL_GET_THERMTRIP = 4,

	/**
	 * @brief Number of supported host-to-bpmp commands.
	 */
	CMD_THERMAL_HOST_TO_BPMP_NUM
};

/**
 * @brief Sub-command identifiers for BPMP->host #MRQ_THERMAL -command
 */
enum mrq_thermal_bpmp_to_host_cmd {
	/**
	 * @brief Indication that the temperature for a zone has
	 *        exceeded the range indicated in the thermal trip point
	 *        for the zone.
	 *
	 * BPMP-FW needs to supply request parameters. Host only needs to
	 * acknowledge.
	 */
	CMD_THERMAL_HOST_TRIP_REACHED = 100,

	/**
	 * @brief: Number of supported bpmp-to-host commands. May
	 * increase in future.
	 */
	CMD_THERMAL_BPMP_TO_HOST_NUM
};

/**
 * Host->BPMP request payload for the #CMD_THERMAL_QUERY_ABI sub-command
 */
struct cmd_thermal_query_abi_request {
	/**
	 * Request type for which to check whether supported by BPMP-FW.
	 *
	 * Valid identifiers are available at #mrq_thermal_host_to_bpmp_cmd
	 */
	uint32_t type;
} BPMP_ABI_PACKED;

/**
 * Host->BPMP request payload for the #CMD_THERMAL_GET_TEMP sub-command
 */
struct cmd_thermal_get_temp_request {
	/** Thermal zone identifier from @ref bpmp_thermal_ids. */
	uint32_t zone;
} BPMP_ABI_PACKED;

/**
 * BPMP->Host response payload for the #CMD_THERMAL_GET_TEMP sub-command.
 *
 * mrq_response::err is defined as:
 *
 * | Value         | Description                                              |
 * | ------------- | -------------------------------------------------------- |
 * | 0             | Request succeeded.                                       |
 * | -#BPMP_EINVAL | Request parameters were invalid.                         |
 * | -#BPMP_ENOENT | No driver was registered for the specified thermal zone. |
 * | -#BPMP_EFAULT | For other BPMP-FW internal thermal zone driver errors.   |
 */
struct cmd_thermal_get_temp_response {
	/** @brief Current temperature in millicelsius. */
	int32_t temp;
} BPMP_ABI_PACKED;

/**
 * @cond (!bpmp_safe && !bpmp_t264)
 *
 * Host->BPMP request payload for the #CMD_THERMAL_SET_TRIP sub-command.
 */
struct cmd_thermal_set_trip_request {
	/** @brief Thermal zone identifier from @ref bpmp_thermal_ids. */
	uint32_t zone;
	/** @brief Temperature of lower trip point in millicelsius */
	int32_t low;
	/** @brief Temperature of upper trip point in millicelsius */
	int32_t high;
	/** 1 to enable trip point, 0 to disable trip point */
	uint32_t enabled;
} BPMP_ABI_PACKED;

/**
 * BPMP->Host request payload for the #CMD_THERMAL_HOST_TRIP_REACHED sub-command.
 */
struct cmd_thermal_host_trip_reached_request {
	/**
	 * @brief ID of the thermal zone where trip point was reached,
	 *        from @ref bpmp_thermal_ids.
	 */
	uint32_t zone;
} BPMP_ABI_PACKED;
/** @endcond */

/**
 * BPMP->Host response payload for the #CMD_THERMAL_GET_NUM_ZONES sub-command.
 */
struct cmd_thermal_get_num_zones_response {
	/**
	 * @brief Number of supported thermal zones.
	 *
	 * The thermal zones are indexed starting from zero.
	 */
	uint32_t num;
} BPMP_ABI_PACKED;

/**
 * Host->BPMP request payload for the #CMD_THERMAL_GET_THERMTRIP sub-command.
 */
struct cmd_thermal_get_thermtrip_request {
	/** @brief Thermal zone identifier from @ref bpmp_thermal_ids. */
	uint32_t zone;
} BPMP_ABI_PACKED;

/**
 * BPMP->Host response payload for the #CMD_THERMAL_GET_THERMTRIP sub-command.
 */
struct cmd_thermal_get_thermtrip_response {
	/** @brief HW shutdown temperature in millicelsius. */
	int32_t thermtrip;
} BPMP_ABI_PACKED;

/**
 * Host->BPMP #MRQ_THERMAL request payload.
 *
 * Response payload type is #mrq_thermal_bpmp_to_host_response.
 */
struct mrq_thermal_host_to_bpmp_request {
	/**
	 * Request sub-command identifier from @ref mrq_thermal_host_to_bpmp_cmd.
	 */
	uint32_t type;

	union {
		struct cmd_thermal_query_abi_request query_abi;
		struct cmd_thermal_get_temp_request get_temp;
		struct cmd_thermal_set_trip_request set_trip;
		struct cmd_thermal_get_thermtrip_request get_thermtrip;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for the BPMP->Host #MRQ_THERMAL command.
 */
struct mrq_thermal_bpmp_to_host_request {
	/**
	 * Request sub-command identifier from @ref mrq_thermal_bpmp_to_host_cmd.
	 */
	uint32_t type;

	union {
		struct cmd_thermal_host_trip_reached_request host_trip_reached;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the Host->BPMP #MRQ_THERMAL command.
 */
union mrq_thermal_bpmp_to_host_response {
	struct cmd_thermal_get_temp_response get_temp;
	struct cmd_thermal_get_thermtrip_response get_thermtrip;
	struct cmd_thermal_get_num_zones_response get_num_zones;
} BPMP_ABI_PACKED;

/** @} Thermal */

/** @cond (!bpmp_safe && (bpmp_t234 || bpmp_t238 || bpmp_t264))
 * @ingroup MRQ_Codes
 * @def MRQ_OC_STATUS
 * @brief Query overcurrent status
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: N/A
 * * Response Payload: @ref mrq_oc_status_response
 *
 * @addtogroup OC_status
 * @{
 */

/**
 * @brief Size of the mrq_oc_status_response::throt_en and
 *        mrq_oc_status_response::event_cnt -arrays.
 */
#define OC_STATUS_MAX_SIZE	24U

/**
 * @brief Response payload for the #MRQ_OC_STATUS -command.
 *
 * mrq_response::err is 0 if the operation was successful and
 * -#BPMP_ENODEV otherwise.
 */
struct mrq_oc_status_response {
	/**
	 * @brief Value for each overcurrent alarm where zero signifies
	 * throttle is disabled, and non-zero throttle is enabled.
	 */
	uint8_t throt_en[OC_STATUS_MAX_SIZE];

	/**
	 * @brief Total number of overcurrent events for each overcurrent alarm.
	 */
	uint32_t event_cnt[OC_STATUS_MAX_SIZE];
} BPMP_ABI_PACKED;

/** @} OC_status */
/** @endcond */

/** @cond (bpmp_th500 || bpmp_tb500 || bpmp_t238)
 * @ingroup MRQ_Codes
 * @def MRQ_THROTTLE
 * @brief Overcurrent throttling
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_throttle_request
 * * Response Payload: @ref mrq_throttle_response
 * @addtogroup Throttle
 * @{
 */
enum mrq_throttle_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * #MRQ_THROTTLE sub-command.
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_THROTTLE_QUERY_ABI = 0,

	/**
	 * @cond (bpmp_th500 || bpmp_tb500)
	 * @brief query chipthrot status
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                                  |
	 * |----------------|--------------------------------------------------------------|
	 * | 0              | Success                                                      |
	 * | -#BPMP_ENODEV  | CMD_THROTTLE_GET_CHIPTHROT_STATUS is not supported by BPMP-FW|
	 */
	CMD_THROTTLE_GET_CHIPTHROT_STATUS = 1,
	/** @endcond */

	/**
	 * @cond bpmp_t238
	 * @brief program OC throttle configuration
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                                  |
	 * |----------------|--------------------------------------------------------------|
	 * | 0              | Success                                                      |
	 * | -#BPMP_EINVAL  | ID out of range or alarm for this ID not enabled at boot     |
	 * | -#BPMP_ENODEV  | CMD_THROTTLE_SET_OC_CONFIG is not supported by BPMP-FW       |
	 */
	CMD_THROTTLE_SET_OC_CONFIG = 2,
	/** @endcond */
};

/**
 * @brief Request payload for #MRQ_THROTTLE sub-command #CMD_THROTTLE_QUERY_ABI
 */
struct cmd_throttle_query_abi_request {
	uint32_t cmd; /**< @ref mrq_throttle_cmd */
} BPMP_ABI_PACKED;

/**
 * @cond bpmp_th500
 * @brief Response payload for #MRQ_THROTTLE sub-command
 * #CMD_THROTTLE_GET_CHIPTHROT_STATUS
 *
 * Bit-mask of all h/w throttling actions that have been engaged since
 * last invocation of this command
 * Bit 0...11  : HW throttling status of the thermal zones.
 * Bit 12...23 : Reserved for future thermal zone events.
 * Bit 24...25 : HW throttling status of the Over current Alarms OC1 & OC2.
 * Bit 26...31 : Reserved for future Over current alarm events.
 * Bit 32...63 : Reserved for future use.
 * @endcond
 * @cond bpmp_tb500
 * @brief Response payload for #MRQ_THROTTLE sub-command
 * #CMD_THROTTLE_GET_CHIPTHROT_STATUS
 *
 * Bit-mask of all h/w throttling actions that have been engaged since
 * last invocation of this command
 * Bit 0       : HW throttling status of the TB500C_TJ_MAX thermal zone.
 * Bit 1...63  : Reserved for future use.
 * @endcond
 * @cond (bpmp_th500 || bpmp_tb500)
 */
struct cmd_throttle_get_chipthrot_status_response {
	uint64_t status;
} BPMP_ABI_PACKED;
/** @endcond */

/**
 * @cond bpmp_t238
 * @brief Request payload for #MRQ_THROTTLE sub-command
 * #CMD_THROTTLE_SET_OC_CONFIG
 *
 * Only alarms that have been configured as enabled in BPMP-DTB at boot can
 * be reconfigured with this MRQ.
 */
struct cmd_throttle_set_oc_config_request {
	/** @brief valid OC alarm ID from @ref bpmp_soctherm_edp_oc_ids */
	uint32_t id;
	/** @brief Throttling enable/disable
	 *
	 * Set to 1 to enable throttling, or 0 to disable. Other values are
	 * disallowed.
	 */
	uint8_t en_throttle;
} BPMP_ABI_PACKED;
/** @endcond */

/**
 * @brief Request payload for the #MRQ_THROTTLE -command
 *
 * | Sub-command                        | Request payload                  |
 * |------------------------------------|----------------------------------|
 * | #CMD_THROTTLE_QUERY_ABI            | #cmd_throttle_query_abi_request  |
 *
 * @cond bpmp_th500
 * The following additional sub-commands are supported on TH500 platforms:
 * | Sub-command                        | Request payload                  |
 * |------------------------------------|----------------------------------|
 * | #CMD_THROTTLE_GET_CHIPTHROT_STATUS | -                                |
 * @endcond
 *
 * @cond bpmp_tb500
 * The following additional sub-commands are supported on TB500 platforms:
 * | Sub-command                        | Request payload                  |
 * |------------------------------------|----------------------------------|
 * | #CMD_THROTTLE_GET_CHIPTHROT_STATUS | -                                |
 * @endcond
 *
 * @cond bpmp_t238
 * The following additional sub-commands are supported on T238 platforms:
 * | Sub-command                        | Request payload                     |
 * |------------------------------------|-------------------------------------|
 * | #CMD_THROTTLE_SET_OC_CONFIG        | #cmd_throttle_set_oc_config_request |
 * @endcond
 */
struct mrq_throttle_request {
	uint32_t cmd;
	union {
		struct cmd_throttle_query_abi_request throttle_query_abi_req;
		/** @cond bpmp_t238 */
		struct cmd_throttle_set_oc_config_request throttle_set_oc_config_req;
		/** @endcond */
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_THROTTLE -command.
 *
 * | Sub-command                        | Response payload                           |
 * |------------------------------------|--------------------------------------------|
 * | #CMD_THROTTLE_QUERY_ABI            | -                                          |
 *
 * @cond bpmp_th500
 * The following additional sub-commands are supported on TH500 platforms:
 * | Sub-command                        | Response payload                           |
 * |------------------------------------|--------------------------------------------|
 * | #CMD_THROTTLE_GET_CHIPTHROT_STATUS | #cmd_throttle_get_chipthrot_status_response|
 * @endcond
 *
 * @cond bpmp_tb500
 * The following additional sub-commands are supported on TB500 platforms:
 * | Sub-command                        | Response payload                           |
 * |------------------------------------|--------------------------------------------|
 * | #CMD_THROTTLE_GET_CHIPTHROT_STATUS | #cmd_throttle_get_chipthrot_status_response|
 * @endcond
 *
 * @cond bpmp_t238
 * The following additional sub-commands are supported on T238 platforms:
 * | Sub-command                        | Response payload                           |
 * |------------------------------------|--------------------------------------------|
 * | #CMD_THROTTLE_SET_OC_CONFIG        | -                                          |
 * @endcond
 */
struct mrq_throttle_response {
	union {
		/** @cond (bpmp_th500 || bpmp_tb500) */
		struct cmd_throttle_get_chipthrot_status_response throttle_get_chipthrot_status_resp;
		/** @endcond */
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;
/** @} Throttle */
/** @endcond */


/** @cond bpmp_t186
 * @ingroup MRQ_Codes
 * @def MRQ_CPU_VHINT
 * @brief Query CPU voltage hint data
 *
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
/** @endcond */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_ABI_RATCHET
 * @brief ABI ratchet value query
 *
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
 * #BPMP_ABI_RATCHET_VALUE may increase for various reasons in a future
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
 * header version with #BPMP_ABI_RATCHET_VALUE = ratchet or cease
 * communication with BPMP.
 *
 * If mrq_response::err is 0 and ratchet is greater than or equal to the
 * requester's #BPMP_ABI_RATCHET_VALUE, the requester should continue
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
 * On T264 and onwards, this MRQ service is available only when
 * BPMP-FW has valid DRAM timing table passed by earlier boot stages.
 *
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
	/**
	 * @brief The number valid entries in #pairs
	 *
	 * Valid range is [0, #EMC_DVFS_LATENCY_MAX_SIZE]
	 */
	uint32_t num_pairs;
	/** @brief EMC DVFS node <frequency, latency> information */
	struct emc_dvfs_latency pairs[EMC_DVFS_LATENCY_MAX_SIZE];
} BPMP_ABI_PACKED;

/** @} EMC */

/** @cond (bpmp_t234)
 * @ingroup MRQ_Codes
 * @def MRQ_EMC_DVFS_EMCHUB
 * @brief Query EMC HUB frequencies
 *
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
/** @endcond */

/** @cond (bpmp_t234)
 * @ingroup MRQ_Codes
 * @def MRQ_EMC_DISP_RFL
 * @brief Set EMC display RFL handshake mode of operations
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_emc_disp_rfl_request
 * * Response Payload: N/A
 *
 * @addtogroup EMC
 * @{
 */

/**
 * @brief Allowed mode values for the mrq_emc_disp_rfl_request::mode -field.
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
 * mrq_response::err for this request is defined as:
 *
 * | Value          | Description                                   |
 * | -------------- | --------------------------------------------- |
 * | 0              | RFL mode is set successfully.                 |
 * | -#BPMP_EINVAL  | Invalid mode requested.                       |
 * | -#BPMP_ENOSYS  | RFL handshake is not supported.               |
 * | -#BPMP_EACCES  | Permission denied.                            |
 * | -#BPMP_ENODEV  | if disp rfl mrq is not supported by BPMP-FW.  |
 */
struct mrq_emc_disp_rfl_request {
	/** @brief EMC display RFL mode from @ref mrq_emc_disp_rfl_mode */
	uint32_t mode;
} BPMP_ABI_PACKED;

/** @} EMC */
/** @endcond */

/** @cond (!bpmp_safe && (bpmp_t234 || bpmp_t238))
 * @ingroup MRQ_Codes
 * @def MRQ_BWMGR
 * @brief Bandwidth manager (BWMGR) commands
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_bwmgr_request
 * * Response Payload: @ref mrq_bwmgr_response
 *
 * @addtogroup BWMGR
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_BWMGR
 */
enum mrq_bwmgr_cmd {
	/**
	 * @brief Check whether BPMP-FW supports the specified
	 * #MRQ_BWMGR sub-command.
	 *
	 * mrq_response::err is defined to be:
	 *
	 * | Value          | Description
	 * |----------------|----------------------------
	 * | 0              | Specified sub-command is supported.
	 * | -#BPMP_ENODEV  | Specified sub-command is not supported.
	 */
	CMD_BWMGR_QUERY_ABI = 0,

	/**
	 * @brief Determine DRAM rate to satisfy ISO/NISO bandwidth requests
	 *
	 * mrq_response::err is defined to be:
	 *
	 * | Value          | Description
	 * |----------------|----------------------------
	 * | 0              | Rate calculation succeeded.
	 * | -#BPMP_EINVAL  | Invalid request parameters.
	 * | -#BPMP_ENOTSUP | Requested bandwidth is not available.
	 * | <0             | Any other internal error.
	 */
	CMD_BWMGR_CALC_RATE = 1
};

/**
 * @brief Request payload for #MRQ_BWMGR sub-command #CMD_BWMGR_QUERY_ABI
 */
struct cmd_bwmgr_query_abi_request {
	/** @brief Sub-command identifier from @ref mrq_bwmgr_cmd. */
	uint32_t type;
} BPMP_ABI_PACKED;

/**
 * @brief Used by @ref cmd_bwmgr_calc_rate_request
 */
struct iso_req {
	/** @brief BWMGR client ID from @ref bpmp_bwmgr_ids */
	uint32_t id;
	/** @brief Bandwidth in kBps requested by client */
	uint32_t iso_bw;
} BPMP_ABI_PACKED;

/**
 * @brief Size of the cmd_bwmgr_calc_rate_request::isobw_reqs -array.
 */
#define MAX_ISO_CLIENTS		13U

/**
 * @brief Request payload for #MRQ_BWMGR sub-command #CMD_BWMGR_CALC_RATE
 */
struct cmd_bwmgr_calc_rate_request {
	/** @brief Total bandwidth in kBps requested by all NISO clients. */
	uint32_t sum_niso_bw;
	/** @brief The number of ISO client requests in #isobw_reqs -array */
	uint32_t num_iso_clients;
	/** @brief iso_req <id, iso_bw> information */
	struct iso_req isobw_reqs[MAX_ISO_CLIENTS];
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_BWMGR sub-command #CMD_BWMGR_CALC_RATE
 */
struct cmd_bwmgr_calc_rate_response {
	/**
	 * @brief Minimum DRAM data clock rate in kHz to satisfy all ISO client
	 *        bandwidth requests.
	 */
	uint32_t iso_rate_min;

	/**
	 * @brief Minimum DRAM data clock rate in kHz to satisfy all
	 *        bandwidth requests.
	 */
	uint32_t total_rate_min;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for the #MRQ_BWMGR -command.
 *
 * |Sub-command           |Payload                      |
 * |----------------------|-----------------------------|
 * |#CMD_BWMGR_QUERY_ABI  |#cmd_bwmgr_query_abi_request |
 * |#CMD_BWMGR_CALC_RATE  |#cmd_bwmgr_calc_rate_request |
 */
struct mrq_bwmgr_request {
	/** @brief Sub-command identifier from @ref mrq_bwmgr_cmd. */
	uint32_t cmd;
	union {
		struct cmd_bwmgr_query_abi_request query_abi;
		struct cmd_bwmgr_calc_rate_request bwmgr_rate_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_BWMGR -command.
 *
 * |Sub-command           |Payload                       |
 * |----------------------|------------------------------|
 * |#CMD_BWMGR_CALC_RATE  |#cmd_bwmgr_calc_rate_response |
 */
struct mrq_bwmgr_response {
	union {
		struct cmd_bwmgr_calc_rate_response bwmgr_rate_resp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} BWMGR */
/** @endcond */

/** @cond (!bpmp_safe && (bpmp_t234 || bpmp_t238 || bpmp_t264))
 * @ingroup MRQ_Codes
 * @def MRQ_BWMGR_INT
 * @brief BPMP-FW integrated BWMGR requests
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_bwmgr_int_request
 * * Response Payload: @ref mrq_bwmgr_int_response
 *
 * @addtogroup BWMGR_INT
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_BWMGR_INT
 */
enum mrq_bwmgr_int_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * sub-command.
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_BWMGR_INT_QUERY_ABI = 1,

	/**
	 * @brief Determine and set DRAM rate to satisfy ISO/NISO bandwidth requests.
	 *
	 * mrq_response::err is defined as:
	 *
	 * |Value            |Description                                                                                                     |
	 * |-----------------|----------------------------------------------------------------------------------------------------------------|
	 * |0                |Request succeeded.                                                                                              |
	 * |-#BPMP_EINVAL    |Invalid request parameters, cmd_bwmgr_int_calc_and_set_response::rate is not set.                               |
	 * |-#BPMP_ENOTSUP   |Requested bandwidth is not available, cmd_bwmgr_int_calc_and_set_response::rate is the current DRAM clock rate. |
	 * |<0               |Any other internal error.                                                                                       |
	 */
	CMD_BWMGR_INT_CALC_AND_SET = 2,

	/**
	 * @brief Set a max DRAM frequency for the bandwidth manager.
	 *
	 * mrq_response::err is defined as:
	 *
	 * |Value            |Description                               |
	 * |-----------------|------------------------------------------|
	 * |0                |Request succeeded.                        |
	 * |-#BPMP_ENOTSUP   |Requested cap frequency is not possible.  |
	 * |<0               |Any other internal error.                 |
	 */
	CMD_BWMGR_INT_CAP_SET = 3,

	/**
	 * @brief Obtain the maximum amount of bandwidth currently allocatable
	 * to the requesting client.
	 *
	 * mrq_response::err is defined as:
	 *
	 * |Value            |Description                               |
	 * |-----------------|------------------------------------------|
	 * |0                |Request succeeded.                        |
	 * |-#BPMP_EINVAL    |Invalid request parameters.               |
	 * |<0               |Any other internal error.                 |
	 */
	CMD_BWMGR_INT_CURR_AVAILABLE_BW = 4,
	/**
	 * @brief Get the last request made by the client.
	 *
	 * mrq_response::err is defined as:
	 *
	 * |Value            |Description                               |
	 * |-----------------|------------------------------------------|
	 * |0                |Request succeeded.                        |
	 * |-#BPMP_EINVAL    |Invalid request parameters.               |
	 * |<0               |Any other internal error.                 |
	 */
	CMD_BWMGR_INT_GET_LAST_REQUEST = 5,
};

/**
 * @brief Request payload for #MRQ_BWMGR_INT sub-command #CMD_BWMGR_INT_QUERY_ABI
 */
struct cmd_bwmgr_int_query_abi_request {
	/** @brief Sub-command identifier from @ref mrq_bwmgr_int_cmd. */
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

/**
 * @brief Request payload for #MRQ_BWMGR_INT sub-command #CMD_BWMGR_INT_CALC_AND_SET
 */
struct cmd_bwmgr_int_calc_and_set_request {
	/** @brief BWGMR client ID from @ref bpmp_bwmgr_ids */
	uint32_t client_id;
	/** @brief Average NISO bandwidth usage in kBps requested by client. */
	uint32_t niso_bw;
	/**
	 * @brief Average ISO bandwidth usage in kBps requested by client.
	 *
	 * Value is ignored if client is NISO as determined by #client_id.
	 */
	uint32_t iso_bw;
	/**
	 * @brief Memory clock floor requested by client, unit of the value
	 *        is determined by #floor_unit -field.
	 */
	uint32_t mc_floor;
	/**
	 * @brief Value set to determine the unit of the #mc_floor value:
	 *
	 * | Value                 | Unit                 |
	 * |-----------------------|----------------------|
	 * | #BWMGR_INT_UNIT_KBPS  | Kilobytes per second |
	 * | #BWMGR_INT_UNIT_KHZ   | Kilohertz            |
	 */
	uint8_t floor_unit;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_BWMGR_INT sub-command #CMD_BWMGR_INT_CALC_AND_SET
 */
struct cmd_bwmgr_int_calc_and_set_response {
	/** @brief Currently set memory clock frequency in Hz */
	uint64_t rate;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_BWMGR_INT sub-command #CMD_BWMGR_INT_CAP_SET
 */
struct cmd_bwmgr_int_cap_set_request {
	/** @brief Requested cap frequency in Hz. */
	uint64_t rate;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_BWMGR_INT sub-command #CMD_BWMGR_INT_CURR_AVAILABLE_BW
 */
struct cmd_bwmgr_int_curr_available_bw_request {
	/** @brief BWMGR client ID from @ref bpmp_bwmgr_ids */
	uint32_t id;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_BWMGR_INT sub-command #CMD_BWMGR_INT_CURR_AVAILABLE_BW
 */
struct cmd_bwmgr_int_curr_available_bw_response {
	/** @brief Current cap frequency in KHz. */
	uint64_t cap_rate;
	/** @brief Currently available bandwidth for the requesting client
	 * to allocate in KBps.
	 */
	uint64_t available_bw;
} BPMP_ABI_PACKED;

struct cmd_bwmgr_int_get_last_request_request {
	/** @brief BWMGR client ID from @ref bpmp_bwmgr_ids */
	uint32_t id;
	/**
	 * @brief Value set to determine the unit of the returned mc_floor value:
	 *
	 * | Value                 | Unit                 |
	 * |-----------------------|----------------------|
	 * | #BWMGR_INT_UNIT_KBPS  | Kilobytes per second |
	 * | #BWMGR_INT_UNIT_KHZ   | Kilohertz            |
	 */
	uint8_t floor_unit;
} BPMP_ABI_PACKED;

struct cmd_bwmgr_int_get_last_request_response {
	/** @brief BWGMR client ID from @ref bpmp_bwmgr_ids */
	uint32_t client_id;
	/** @brief Average NISO bandwidth usage in kBps requested by client. */
	uint32_t niso_bw;
	/**
	 * @brief Average ISO bandwidth usage in kBps requested by client.
	 */
	uint32_t iso_bw;
	/**
	 * @brief Memory clock floor requested by client, unit of the value
	 *        is determined by #floor_unit -field.
	 */
	uint32_t mc_floor;
	/**
	 * @brief Value set to determine the unit of the #mc_floor value:
	 *
	 * | Value                 | Unit                 |
	 * |-----------------------|----------------------|
	 * | #BWMGR_INT_UNIT_KBPS  | Kilobytes per second |
	 * | #BWMGR_INT_UNIT_KHZ   | Kilohertz            |
	 */
	uint8_t floor_unit;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for the #MRQ_BWMGR_INT -command.
 *
 * |Sub-command                      |Payload                                  |
 * |---------------------------------|-----------------------------------------|
 * |#CMD_BWMGR_INT_QUERY_ABI         |#cmd_bwmgr_int_query_abi_request         |
 * |#CMD_BWMGR_INT_CALC_AND_SET      |#cmd_bwmgr_int_calc_and_set_request      |
 * |#CMD_BWMGR_INT_CAP_SET           |#cmd_bwmgr_int_cap_set_request           |
 * |#CMD_BWMGR_INT_GET_LAST_REQUEST  |#cmd_bwmgr_int_get_last_request_request  |
 *
 * The following additional sub-commands are supported on T264 platforms:
 *
 * |Sub-command                      |Payload                                  |
 * |---------------------------------|-----------------------------------------|
 * |#CMD_BWMGR_INT_CURR_AVAILABLE_BW |#cmd_bwmgr_int_curr_available_bw_request |
 */
struct mrq_bwmgr_int_request {
	/** @brief Sub-command identifier from @ref mrq_bwmgr_int_cmd. */
	uint32_t cmd;
	union {
		struct cmd_bwmgr_int_query_abi_request query_abi;
		struct cmd_bwmgr_int_calc_and_set_request bwmgr_calc_set_req;
		struct cmd_bwmgr_int_cap_set_request bwmgr_cap_set_req;
		struct cmd_bwmgr_int_curr_available_bw_request bwmgr_curr_available_bw_req;
		struct cmd_bwmgr_int_get_last_request_request bwmgr_get_last_request_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_BWMGR_INT -command.
 *
 * |Sub-command                      |Payload                                   |
 * |---------------------------------|------------------------------------------|
 * |#CMD_BWMGR_INT_CALC_AND_SET      |#cmd_bwmgr_int_calc_and_set_response      |
 * |#CMD_BWMGR_INT_GET_LAST_REQUEST  |#cmd_bwmgr_int_get_last_request_response  |
 *
 * The following additional sub-commands are supported on T264 platforms:
 * |Sub-command                      |Payload                                   |
 * |---------------------------------|------------------------------------------|
 * |#CMD_BWMGR_INT_CURR_AVAILABLE_BW |#cmd_bwmgr_int_curr_available_bw_response |
 */
struct mrq_bwmgr_int_response {
	union {
		struct cmd_bwmgr_int_calc_and_set_response bwmgr_calc_set_resp;
		struct cmd_bwmgr_int_curr_available_bw_response bwmgr_curr_available_bw_resp;
		struct cmd_bwmgr_int_get_last_request_response bwmgr_get_last_request_resp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} BWMGR_INT */
/** @endcond */

/** @cond (!bpmp_safe && (bpmp_t234 || bpmp_t238 || bpmp_t264))
 * @ingroup MRQ_Codes
 * @def MRQ_ISO_CLIENT
 * @brief ISO client requests
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_iso_client_request
 * * Response Payload: @ref mrq_iso_client_response
 *
 * @addtogroup ISO_CLIENT
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_ISO_CLIENT.
 */
enum mrq_iso_client_cmd {
	/**
	 * @brief Check whether BPMP-FW supports a specified
	 *        #MRQ_ISO_CLIENT sub-command.
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_ISO_CLIENT_QUERY_ABI = 0,

	/**
	 * @brief Determine legal LA for ISO client.
	 *
	 * Without programming LA MC registers, calculate and ensure that
	 * a legal LA is possible for the ISO bandwidth requested by the
	 * ISO client.
	 *
	 * mrq_response::err for this sub-command is defined as:
	 *
	 * | Value         | Description                                                  |
	 * |---------------|--------------------------------------------------------------|
	 * | 0             | Request successful and legal LA is possible.                 |
	 * | -#BPMP_EINVAL | Invalid request parameters.                                  |
	 * | -#BPMP_EFAULT | Legal LA is not possible for client requested ISO bandwidth. |
	 * | <0            | Any other internal error.                                    |
	 */
	CMD_ISO_CLIENT_CALCULATE_LA = 1,

	/**
	 * @brief Set LA for ISO client.
	 *
	 * Calculate and program the LA/PTSA MC registers corresponding to the
	 * ISO client making the bandwidth request.
	 *
	 * mrq_response::err for this sub-command is defined as:
	 *
	 * | Value         | Description                                  |
	 * |---------------|----------------------------------------------|
	 * | 0             | Setting LA succeeded.                        |
	 * | -#BPMP_EINVAL | Invalid request parameters.                  |
	 * | -#BPMP_EFAULT | Failed to calculate or program MC registers. |
	 * | <0            | Any other internal error.                    |
	 */
	CMD_ISO_CLIENT_SET_LA = 2,

	/**
	 * @brief Get maximum possible bandwidth for ISO client.
	 *
	 * mrq_response::err for this sub-command is defined as:
	 *
	 * | Value         | Description                                  |
	 * |---------------|----------------------------------------------|
	 * | 0             | Operation successful.                        |
	 * | -#BPMP_EINVAL | Invalid request parameters.                  |
	 * | <0            | Any other internal error.                    |
	 */
	CMD_ISO_CLIENT_GET_MAX_BW = 3
};

/**
 * @brief Request payload for #MRQ_ISO_CLIENT sub-command #CMD_ISO_CLIENT_QUERY_ABI
 */
struct cmd_iso_client_query_abi_request {
	/**
	 * @brief Sub-command identifier from @ref mrq_iso_client_cmd
	 *        for which to check existence.
	 */
	uint32_t type;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload #MRQ_ISO_CLIENT sub-command #CMD_ISO_CLIENT_CALCULATE_LA
 */
struct cmd_iso_client_calculate_la_request {
	/** @brief BWMGR client ID from @ref bpmp_bwmgr_ids */
	uint32_t id;
	/** @brief Bandwidth requested in kBps for the client specified in #id. */
	uint32_t bw;
	/**
	 * @brief Initial DRAM bandwidth floor in kBps for the ISO client specified in #id.
	 *
	 * ISO client will perform mempool allocation and DVFS buffering based
	 * on this value.
	 */
	uint32_t init_bw_floor;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_ISO_CLIENT sub-command #CMD_ISO_CLIENT_CALCULATE_LA
 */
struct cmd_iso_client_calculate_la_response {
	/** @brief Minimum DRAM rate in kHz at which a legal LA is possible */
	uint32_t la_rate_floor;
	/**
	 * Minimum DRAM frequency in kHz required to satisfy this clients
	 * ISO bandwidth request, assuming all other ISO clients are inactive.
	 */
	uint32_t iso_client_only_rate;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_ISO_CLIENT sub-command #CMD_ISO_CLIENT_SET_LA
 */
struct cmd_iso_client_set_la_request {
	/** @brief BMWGR client ID from @ref bpmp_bwmgr_ids */
	uint32_t id;
	/** @brief Bandwidth requested in kBps for the client specified in #id. */
	uint32_t bw;
	/**
	 * @brief Final DRAM bandwidth floor in kBps.
	 *
	 * Sometimes the initial cmd_iso_client_calculate_la_request::dram_bw_floor
	 * passed by ISO client may need to be updated by considering higher
	 * DRAM frequencies. This is the final DRAM bandwidth floor value used
	 * to calculate and program MC registers.
	 */
	uint32_t final_bw_floor;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_ISO_CLIENT sub-command #CMD_ISO_CLIENT_GET_MAX_BW
 */
struct cmd_iso_client_get_max_bw_request {
	/** @brief BWMGR client ID from @ref bpmp_bwmgr_ids */
	uint32_t id;
} BPMP_ABI_PACKED;

/**
 * @brief Used by @ref cmd_iso_client_get_max_bw_response
 */
struct iso_max_bw {
	/** @brief dram frequency in kHz */
	uint32_t freq;
	/** @brief max possible iso-bw in kBps */
	uint32_t iso_bw;
} BPMP_ABI_PACKED;

/**
 * @brief Size of the cmd_iso_client_get_max_bw_response::pairs -array.
 */
#define ISO_MAX_BW_MAX_SIZE	14U

/**
 * @brief Response payload for #MRQ_ISO_CLIENT sub-command #CMD_ISO_CLIENT_GET_MAX_BW
 */
struct cmd_iso_client_get_max_bw_response {
	/** @brief The number valid entries in iso_max_bw pairs */
	uint32_t num_pairs;
	/** @brief max ISOBW <dram freq, max bw> information */
	struct iso_max_bw pairs[ISO_MAX_BW_MAX_SIZE];
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_ISO_CLIENT command.
 *
 * Each #MRQ_ISO_CLIENT -command is expected to include a sub-command specific
 * payload as defined in table below:
 *
 * |Sub-command                  |Request payload                       |
 * |-----------------------------|--------------------------------------|
 * |#CMD_ISO_CLIENT_QUERY_ABI    |#cmd_iso_client_query_abi_request     |
 * |#CMD_ISO_CLIENT_CALCULATE_LA |#cmd_iso_client_calculate_la_request  |
 * |#CMD_ISO_CLIENT_SET_LA       |#cmd_iso_client_set_la_request        |
 * |#CMD_ISO_CLIENT_GET_MAX_BW   |#cmd_iso_client_get_max_bw_request    |
 */
struct mrq_iso_client_request {
	/** @brief Sub-command identifier from @ref mrq_iso_client_cmd. */
	uint32_t cmd;

	union {
		struct cmd_iso_client_query_abi_request query_abi;
		struct cmd_iso_client_calculate_la_request calculate_la_req;
		struct cmd_iso_client_set_la_request set_la_req;
		struct cmd_iso_client_get_max_bw_request max_isobw_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_ISO_CLIENT command.
 *
 * Some of the sub-commands supported by #MRQ_ISO_CLIENT may return
 * a sub-command -specific payload in the MRQ response as defined in table
 * below:
 *
 * |Sub-command                  |Response payload                      |
 * |---------------------------- |--------------------------------------|
 * |#CMD_ISO_CLIENT_QUERY_ABI    |-                                     |
 * |#CMD_ISO_CLIENT_CALCULATE_LA |#cmd_iso_client_calculate_la_response |
 * |#CMD_ISO_CLIENT_SET_LA       |-                                     |
 * |#CMD_ISO_CLIENT_GET_MAX_BW   |#cmd_iso_client_get_max_bw_response   |
 *
 */
struct mrq_iso_client_response {
	union {
		struct cmd_iso_client_calculate_la_response calculate_la_resp;
		struct cmd_iso_client_get_max_bw_response max_isobw_resp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} ISO_CLIENT */
/** @endcond */

/** @cond (!bpmp_t186)
 * @ingroup MRQ_Codes
 * @def MRQ_CPU_NDIV_LIMITS
 * @brief Return CPU cluster NDIV limits
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_cpu_ndiv_limits_request
 * * Response Payload: @ref mrq_cpu_ndiv_limits_response
 * @addtogroup CPU
 * @{
 */

/**
 * @brief Request payload for the #MRQ_CPU_NDIV_LIMITS -command
 */
struct mrq_cpu_ndiv_limits_request {
	/** @brief Logical CPU cluster identifier */
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
/** @endcond */


/** @cond (bpmp_t194)
 * @ingroup MRQ_Codes
 * @def MRQ_CPU_AUTO_CC3
 * @brief Query CPU cluster auto-CC3 configuration
 *
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
 * @brief Request payload for the #MRQ_CPU_AUTO_CC3 -command
 */
struct mrq_cpu_auto_cc3_request {
	/** @brief Logical CPU cluster ID */
	uint32_t cluster_id;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_CPU_AUTO_CC3 -command
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
/** @endcond */

/** @cond (bpmp_t186 || bpmp_t194 || bpmp_t234)
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
	 * @brief Number of bytes requested to be read from the BPMP TX buffer.
	 *
	 * Valid range is [0, #MRQ_RINGBUF_CONSOLE_MAX_READ_LEN]
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
	/**
	 * @brief Number of bytes in cmd_ringbuf_console_read_resp::data
	 *
	 * Valid range is [0, #MRQ_RINGBUF_CONSOLE_MAX_WRITE_LEN]
	 */
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
/** @endcond */

/** @cond (!bpmp_t186 && !(bpmp_safe && bpmp_t234))
 * @ingroup MRQ_Codes
 * @def MRQ_STRAP
 * @brief Set a strap value controlled by BPMP
 *
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
 * exposes limited control of those registers via #MRQ_STRAP.
 *
 * @{
 */

/**
 * @brief Sub-command identifiers for the #MRQ_STRAP -command.
 */
enum mrq_strap_cmd {
	/** @private */
	STRAP_RESERVED = 0,
	/** @brief Set a strap value */
	STRAP_SET = 1
};

/**
 * @brief Request payload for the #MRQ_STRAP -command.
 */
struct mrq_strap_request {
	/** @brief Sub-command identifier from @ref mrq_strap_cmd */
	uint32_t cmd;
	/**
	 * @if (bpmp_t234 || bpmp_th500 || bpmp_t264)
	 * @brief Strap ID from @ref bpmp_strap_ids
	 * @else
	 * @brief Strap ID (undefined)
	 * @endif
	 */
	uint32_t id;
	/** @brief Desired value for strap (if #cmd is #STRAP_SET) */
	uint32_t value;
} BPMP_ABI_PACKED;

/** @} Strap */
/** @endcond */

/** @cond (bpmp_t194 || bpmp_t234 || bpmp_th500)
 * @ingroup MRQ_Codes
 * @def MRQ_UPHY
 * @brief Perform a UPHY operation
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_uphy_request
 * * Response Payload: @ref mrq_uphy_response
 *
 * @addtogroup UPHY
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_UPHY.
 */
enum mrq_uphy_cmd {
	/** @brief Trigger PCIE lane margining procedure. */
	CMD_UPHY_PCIE_LANE_MARGIN_CONTROL = 1,
	/** @brief Return PCIE lane margining status. */
	CMD_UPHY_PCIE_LANE_MARGIN_STATUS = 2,
	/** @brief Initialize PCIE EP PLL controller. */
	CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT = 3,
	/** @brief Set state of the PCIE RP/EP controller. */
	CMD_UPHY_PCIE_CONTROLLER_STATE = 4,
	/** @brief Disable PCIE EP PLL controller. */
	CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF = 5,

	/**
	 * @cond bpmp_t238
	 * @brief Initialize and enable UPHY display port.
	 */
	CMD_UPHY_DISPLAY_PORT_INIT = 6,
	/** @brief Disable UPHY display port. */
	CMD_UPHY_DISPLAY_PORT_OFF = 7,
	/** @brief Trigger sequence to restore XUSB DYN lanes during SC7 exit. */
	CMD_UPHY_XUSB_DYN_LANES_RESTORE = 8,
	/** @endcond */

	/**
	 * @cond bpmp_th500
	 * @brief Perform UPHY Lane EOM scan.
	 */
	CMD_UPHY_LANE_EOM_SCAN = 9,
	/** @brief Config PCIe VDM with a given BDF ID. */
	CMD_UPHY_PCIE_CONFIG_VDM = 10,
	/** @endcond */

	CMD_UPHY_MAX,
};

/**
 * @brief Request payload for #MRQ_UPHY sub-command #CMD_UPHY_PCIE_LANE_MARGIN_CONTROL.
 */
struct cmd_uphy_margin_control_request {
	/**
	 * @brief Enable margin.
	 *
	 * Valid values:
	 * * Value 0 disables margin,
	 * * Value 1 enables margin.
	 */
	int32_t en;
	/**
	 * @brief Clear the number of error and sections.
	 *
	 * Valid values:
	 *
	 * * Value 0: Skip clear,
	 * * Value 1: Perform clear.
	 */
	int32_t clr;
	/**
	 * @brief Set x offset (1's complement) for left/right margin type (y should be 0).
	 *
	 * Valid range is [0, 127]
	 */
	uint32_t x;
	/**
	 * @brief Set y offset (1's complement) for left/right margin type (x should be 0)
	 *
	 * Valid range is [0, 63]
	 */
	uint32_t y;
	/**
	 * @brief Set number of bit blocks for each margin section.
	 *
	 * Valid range is [0, 15]
	 */
	uint32_t nblks;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_UPHY sub-command #CMD_UPHY_PCIE_LANE_MARGIN_STATUS.
 */
struct cmd_uphy_margin_status_response {
	/** @brief Number of errors observed */
	uint32_t status;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_UPHY sub-command #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT.
 */
struct cmd_uphy_ep_controller_pll_init_request {
	/** @brief EP controller number, T194 valid: 0, 4, 5; T234 valid: 5, 6, 7, 10; T238 valid: 0 */
	uint8_t ep_controller;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_UPHY sub-command #CMD_UPHY_PCIE_CONTROLLER_STATE.
 */
struct cmd_uphy_pcie_controller_state_request {
	/** @brief PCIE controller number, T194 valid: 0-4; T234 valid: 0-10; T238 valid: 0-3 */
	uint8_t pcie_controller;
	/** @brief Nonzero value to enable controller, zero value to disable */
	uint8_t enable;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_UPHY sub-command #CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF.
 */
struct cmd_uphy_ep_controller_pll_off_request {
	/** @brief EP controller number, T194 valid: 0, 4, 5; T234 valid: 5, 6, 7, 10; T238 valid: 0 */
	uint8_t ep_controller;
} BPMP_ABI_PACKED;

/**
 * @cond bpmp_t238
 * @brief Request payload for #MRQ_UPHY sub-command #CMD_UPHY_DISPLAY_PORT_INIT.
 */
struct cmd_uphy_display_port_init_request {
	/** @brief DisplayPort link rate, T238 valid: 1620, 2700, 5400, 8100, 2160, 2430, 3240, 4320, 6750 */
	uint16_t link_rate;
	/** @brief 1: lane 0; 2: lane 1; 3: lane 0 and 1 */
	uint16_t lanes_bitmap;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_UPHY sub-command #CMD_UPHY_XUSB_DYN_LANES_RESTORE.
 */
struct cmd_uphy_xusb_dyn_lanes_restore_request {
	/** @brief 1: lane 0; 2: lane 1; 3: lane 0 and 1 */
	uint16_t lanes_bitmap;
} BPMP_ABI_PACKED;
/** @endcond */

/**
 * @cond bpmp_th500
 * @brief Request payload for #MRQ_UPHY sub-command #CMD_UPHY_LANE_EOM_SCAN
 */
struct cmd_uphy_lane_eom_scan_request {
	/** @brief UPHY brick number, valid: 0-5 */
	uint32_t brick;
	/** @brief UPHY lane number, valid: 0-15 for UPHY0-UPHY3, 0-1 for UPHY4-UPHY5 */
	uint32_t lane;
	/** @brief Perform EOM for PCIE GEN5 link: 1 for yes, 0 for no. */
	uint32_t pcie_gen5;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_UPHY sub-command #CMD_UPHY_LANE_EOM_SCAN
 */
struct cmd_uphy_lane_eom_scan_response {
	uint32_t data;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_UPHY sub-command #CMD_UPHY_PCIE_CONFIG_VDM
 */
struct cmd_uphy_pcie_config_vdm_request {
	uint8_t pcie_controller;
	/**
	 * @brief Bus/Dev/Func ID to be programmed for VDM.
	 *
	 * - bits[15..8] Bus
	 * - bits[7..3]  Dev
	 * - bit [2..0]  Func
	 */
	uint16_t bdf;
} BPMP_ABI_PACKED;
/** @endcond */

/**
 * @ingroup UPHY
 * @brief Request payload for the #MRQ_UPHY -command.
 *
 * Used by the sender of an #MRQ_UPHY message to control UPHY.
 * The uphy_request is split into several sub-commands. CMD_UPHY_PCIE_LANE_MARGIN_STATUS
 * requires no additional data. Others have a sub-command specific payload. Below table
 * shows sub-commands with their corresponding payload data.
 *
 * |sub-command                           |payload                                  |
 * |--------------------------------------|-----------------------------------------|
 * |#CMD_UPHY_PCIE_LANE_MARGIN_CONTROL    |#cmd_uphy_margin_control_request         |
 * |#CMD_UPHY_PCIE_LANE_MARGIN_STATUS     |-                                        |
 * |#CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT |#cmd_uphy_ep_controller_pll_init_request |
 * |#CMD_UPHY_PCIE_CONTROLLER_STATE       |#cmd_uphy_pcie_controller_state_request  |
 * |#CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF  |#cmd_uphy_ep_controller_pll_off_request  |
 *
 * @cond bpmp_t238
 * The following additional sub-commands are supported on T238 platforms:
 *
 * |sub-command                           |payload                                  |
 * |--------------------------------------|-----------------------------------------|
 * |#CMD_UPHY_DISPLAY_PORT_INIT           |#cmd_uphy_display_port_init_request      |
 * |#CMD_UPHY_DISPLAY_PORT_OFF            |-                                        |
 * |#CMD_UPHY_XUSB_DYN_LANES_RESTORE      |#cmd_uphy_xusb_dyn_lanes_restore_request |
 * @endcond
 *
 * @cond bpmp_th500
 * The following additional sub-commands are supported on TH500 platforms:
 * |sub-command                           |payload                                  |
 * |--------------------------------------|-----------------------------------------|
 * |#CMD_UPHY_LANE_EOM_SCAN               |#cmd_uphy_lane_eom_scan_request          |
 * |#CMD_UPHY_PCIE_CONFIG_VDM             |#cmd_uphy_pcie_config_vdm_request        |
 * @endcond
 */
struct mrq_uphy_request {
	/** @brief Lane number. */
	uint16_t lane;
	/** @brief Sub-command ID from @ref mrq_uphy_cmd. */
	uint16_t cmd;

	union {
		struct cmd_uphy_margin_control_request uphy_set_margin_control;
		struct cmd_uphy_ep_controller_pll_init_request ep_ctrlr_pll_init;
		struct cmd_uphy_pcie_controller_state_request controller_state;
		struct cmd_uphy_ep_controller_pll_off_request ep_ctrlr_pll_off;
		/** @cond bpmp_t238 */
		struct cmd_uphy_display_port_init_request display_port_init;
		struct cmd_uphy_xusb_dyn_lanes_restore_request xusb_dyn_lanes_restore;
		/** @endcond */
		/** @cond bpmp_th500 */
		struct cmd_uphy_lane_eom_scan_request lane_eom_scan;
		struct cmd_uphy_pcie_config_vdm_request pcie_vdm;
		/** @endcond */
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @ingroup UPHY
 * @brief Response payload for the #MRQ_UPHY -command.
 *
 * Each sub-command supported by @ref mrq_uphy_request may return
 * sub-command-specific data. Some do and some do not as indicated in
 * the following table
 *
 * |sub-command                        |payload                          |
 * |-----------------------------------|---------------------------------|
 * |#CMD_UPHY_PCIE_LANE_MARGIN_CONTROL |-                                |
 * |#CMD_UPHY_PCIE_LANE_MARGIN_STATUS  |#cmd_uphy_margin_status_response |
 *
 * @cond bpmp_th500
 * The following additional sub-commands are supported on TH500 platforms:
 * |sub-command                        |payload                          |
 * |-----------------------------------|---------------------------------|
 * |#CMD_UPHY_LANE_EOM_SCAN            |#cmd_uphy_lane_eom_scan_response |
 * |#CMD_UPHY_PCIE_CONFIG_VDM          |-                                |
 * @endcond
 */
struct mrq_uphy_response {
	union {
		struct cmd_uphy_margin_status_response uphy_get_margin_status;
		/** @cond bpmp_th500 */
		struct cmd_uphy_lane_eom_scan_response eom_status;
		/** @endcond */
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} UPHY */
/** @endcond */

/** @cond (bpmp_t194 || bpmp_t234 || bpmp_t238 || bpmp_t264)
 * @ingroup MRQ_Codes
 * @def MRQ_FMON
 * @brief Perform a frequency monitor configuration operation
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_fmon_request
 * * Response Payload: @ref mrq_fmon_response
 *
 * @addtogroup FMON
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_FMON
 */
enum mrq_fmon_cmd {
	/**
	 * @brief Clamp FMON configuration to specified rate.
	 *
	 * The monitored clock must be running for clamp to succeed. If
	 * clamped, FMON configuration is preserved when clock rate
	 * and/or state is changed.
	 *
	 * mrq_response::err for this sub-command is defined to be:
	 *
	 * | Value             | Description                                       |
	 * |-------------------|---------------------------------------------------|
	 * | 0                 | Operation was successful.                         |
	 * | -#BPMP_EBADCMD    | Subcommand is not supported.                      |
	 * | -#BPMP_EACCES     | FMON access error.                                |
	 * | -#BPMP_EBADSLT    | Clamp FMON on cluster with auto-CC3 enabled.      |
	 * | -#BPMP_EBUSY      | FMON is already clamped at different rate.        |
	 * | -#BPMP_EFAULT     | Self-diagnostic error detected.                   |
	 * | -#BPMP_EINVAL     | Invalid FMON configuration.                       |
	 * | -#BPMP_EOPNOTSUPP | Not in production mode.                           |
	 * | -#BPMP_ENODEV     | Invalid clock ID in mrq_fmon_request::cmd_and_id. |
	 * | -#BPMP_ENOENT     | No calibration data, uninitialized.               |
	 * | -#BPMP_ENOTSUP    | AVFS config not set.                              |
	 * | -#BPMP_ENOSYS     | Clamp FMON on cluster clock w/ no NAFLL.          |
	 * | -#BPMP_ETIMEDOUT  | Operation timed out.                              |
	 */
	CMD_FMON_GEAR_CLAMP = 1,

	/**
	 * @brief Release clamped FMON configuration.
	 *
	 * Allow FMON configuration to follow monitored clock rate
	 * and/or state changes.
	 *
	 * mrq_response::err for this sub-command is defined to be:
	 *
	 * | Value             | Description                                       |
	 * |-------------------|---------------------------------------------------|
	 * | 0                 | Operation was successful.                         |
	 * | -#BPMP_EBADCMD    | Subcommand is not supported.                      |
	 * | -#BPMP_ENODEV     | Invalid clock ID in mrq_fmon_request::cmd_and_id. |
	 * | -#BPMP_ENOENT     | No calibration data, uninitialized.               |
	 * | -#BPMP_ENOTSUP    | AVFS config not set.                              |
	 * | -#BPMP_EOPNOTSUPP | Not in production mode.                           |
	 */
	CMD_FMON_GEAR_FREE = 2,

	/**
	 * @brief Return rate FMON is clamped at, or 0 if FMON is not clamped.
	 *
	 * Inherently racy, since clamp state can be changed concurrently,
	 * only provided and useful for testing purposes.
	 *
	 * mrq_response::err for this sub-command is defined to be:
	 *
	 * | Value             | Description                                       |
	 * |-------------------|---------------------------------------------------|
	 * | 0                 | Operation was successful.                         |
	 * | -#BPMP_EBADCMD    | Subcommand is not supported.                      |
	 * | -#BPMP_ENODEV     | Invalid clock ID in mrq_fmon_request::cmd_and_id. |
	 * | -#BPMP_ENOENT     | No calibration data, uninitialized.               |
	 * | -#BPMP_ENOTSUP    | AVFS config not set.                              |
	 * | -#BPMP_EOPNOTSUPP | Not in production mode.                           |
	 */
	CMD_FMON_GEAR_GET = 3,

	/**
	 * @brief Return current status of FMON faults detected by FMON
	 *        HW or SW since last invocation of this sub-command.
	 *        Clears fault status.
	 *
	 * mrq_response::err for this sub-command is defined to be:
	 *
	 * | Value             | Description                                       |
	 * |-------------------|---------------------------------------------------|
	 * | 0                 | Operation was successful.                         |
	 * | -#BPMP_EBADCMD    | Subcommand is not supported.                      |
	 * | -#BPMP_ENODEV     | Invalid clock ID in mrq_fmon_request::cmd_and_id. |
	 * | -#BPMP_ENOENT     | No calibration data, uninitialized.               |
	 * | -#BPMP_ENOTSUP    | AVFS config not set.                              |
	 * | -#BPMP_EOPNOTSUPP | Not in production mode.                           |
	 * | -#BPMP_EINVAL     | Invalid fault type.                               |
	 */
	CMD_FMON_FAULT_STS_GET = 4,
};

/**
 * @cond DEPRECATED
 * Kept for backward compatibility
 */
#define CMD_FMON_NUM		4

/** @endcond */

/**
 * @defgroup fmon_fault_type FMON fault types
 * @addtogroup fmon_fault_type
 * @{
 */
/** @brief All detected FMON faults (HW or SW) */
#define FMON_FAULT_TYPE_ALL		0U
/** @brief FMON faults detected by HW */
#define FMON_FAULT_TYPE_HW		1U
/** @brief FMON faults detected by SW */
#define FMON_FAULT_TYPE_SW		2U

/** @} fmon_fault_type */

/**
 * @brief Request payload for #MRQ_FMON sub-command #CMD_FMON_GEAR_CLAMP.
 */
struct cmd_fmon_gear_clamp_request {
	/** @brief Unused / reserved */
	int32_t unused;

	/** @brief Target rate in Hz. Valid range for the rate is [1, INT64_MAX] */
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

/**
 * @brief Response payload for #MRQ_FMON sub-command #CMD_FMON_GEAR_GET.
 */
struct cmd_fmon_gear_get_response {
	int64_t rate;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_FMON sub-command #CMD_FMON_FAULT_STS_GET
 */
struct cmd_fmon_fault_sts_get_request {
	/**
	 * @brief Which fault types to return in response:
	 *
	 * | Value                | Description                             |
	 * |----------------------|-----------------------------------------|
	 * | #FMON_FAULT_TYPE_ALL | Return all detected faults (HW and SW). |
	 * | #FMON_FAULT_TYPE_HW  | Return only HW detected faults.         |
	 * | #FMON_FAULT_TYPE_SW  | Return only SW detected faults.         |
	 */
	uint32_t fault_type;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_FMON sub-command #CMD_FMON_FAULT_STS_GET
 */
struct cmd_fmon_fault_sts_get_response {
	/**
	 * Bitmask of detected HW / SW specific faults, or 0 if no faults have
	 * been detected since last invocation of #CMD_FMON_FAULT_STS_GET.
	 */
	uint32_t fault_sts;
} BPMP_ABI_PACKED;

/**
 * @ingroup FMON
 * @brief Request payload for the #MRQ_FMON -command.
 *
 * Used by the sender of an #MRQ_FMON message to configure clock
 * frequency monitors. The FMON request is split into several
 * sub-commands. Sub-command specific payloads are defined in
 * the following table:
 *
 * |Sub-command             |Payload                         |
 * |------------------------|--------------------------------|
 * |#CMD_FMON_GEAR_CLAMP    |#cmd_fmon_gear_clamp_request    |
 * |#CMD_FMON_GEAR_FREE     |-                               |
 * |#CMD_FMON_GEAR_GET      |-                               |
 * |#CMD_FMON_FAULT_STS_GET |#cmd_fmon_fault_sts_get_request |
 *
 */
struct mrq_fmon_request {
	/**
	 * @brief Sub-command and clock id concatenated to 32-bit word.
	 *
	 * - bits[31..24] -> Sub-command identifier from @ref mrq_fmon_cmd.
	 * - bits[23..0] -> Monitored clock identifier used to select target FMON.
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
 * @brief Response payload for the #MRQ_FMON -command.
 *
 * Each sub-command supported by @ref mrq_fmon_request may
 * return sub-command-specific data as indicated below.
 *
 * |Sub-command             |Payload                          |
 * |------------------------|---------------------------------|
 * |#CMD_FMON_GEAR_CLAMP    |-                                |
 * |#CMD_FMON_GEAR_FREE     |-                                |
 * |#CMD_FMON_GEAR_GET      |#cmd_fmon_gear_get_response      |
 * |#CMD_FMON_FAULT_STS_GET |#cmd_fmon_fault_sts_get_response |
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

/** @} FMON */
/** @endcond */

/** @cond (bpmp_t194)
 * @ingroup MRQ_Codes
 * @def MRQ_EC
 * @brief Provide status information on faults reported by Error
 *        Collator (EC) to HSM.
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_ec_request
 * * Response Payload: @ref mrq_ec_response
 *
 * @note This MRQ ABI is under construction, and subject to change
 *
 * @addtogroup EC
 * @{
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
	 * @endcond
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
/** @endcond */

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
 * @endcond
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
 * @endcond
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
		/** @endcond */
		struct cmd_ec_status_ex_get_response ec_status_ex_get;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} EC */
/** @endcond */

/** @cond (bpmp_th500)
 * @ingroup MRQ_Codes
 * @def MRQ_TELEMETRY
 * @brief Get address of memory buffer refreshed with recently sampled
 *        telemetry data
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: N/A
 * * Response Payload: @ref mrq_telemetry_response
 * @addtogroup Telemetry
 * @{
 */

/**
 * @brief Response payload for the #MRQ_TELEMETRY -command
 *
 * mrq_response::err is defined as:
 *
 * | Value           | Description                                                |
 * |-----------------|------------------------------------------------------------|
 * | 0               | Telemetry data is available at returned address.           |
 * | -#BPMP_EACCES   | MRQ master is not allowed to request buffer refresh.       |
 * | -#BPMP_ENAVAIL  | Telemetry buffer cannot be refreshed via this MRQ channel. |
 * | -#BPMP_ENOTSUP  | Telemetry buffer is not supported by BPMP-FW.              |
 * | -#BPMP_ENODEV   | Telemetry MRQ is not supported by BPMP-FW.                 |
 */
struct mrq_telemetry_response {
	/** @brief Physical address of telemetry data buffer */
	uint64_t data_buf_addr;	/**< see @ref bpmp_telemetry_layout */
} BPMP_ABI_PACKED;

/** @} Telemetry */
/** @endcond */

/** @cond (bpmp_tb500)
 * @ingroup MRQ_Codes
 * @def MRQ_TELEMETRY_EX
 * @brief Get telemetry configuration settings.
 *
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_telemetry_ex_request
 * * Response Payload: @ref mrq_telemetry_ex_response
 *
 * @addtogroup Telemetry_ex
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_TELEMETRY_EX.
 */
enum mrq_telemetry_ex_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * #MRQ_TELEMETRY_EX sub-command.
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_TELEMETRY_EX_QUERY_ABI = 0,

	/**
	 * @brief Get telemetry buffer base address and data size
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                    |
	 * |----------------|------------------------------------------------|
	 * | 0              | Success                                        |
	 * | -#BPMP_ENODEV  | #MRQ_TELEMETRY_EX is not supported by BPMP-FW. |
	 */
	CMD_TELEMETRY_EX_BASE_SZ_GET = 1,
};

/**
 * @brief Request data for #MRQ_TELEMETRY_EX sub-command
 *        #CMD_TELEMETRY_EX_QUERY_ABI
 */
struct cmd_telemetry_ex_query_abi_request {
	/** @brief Sub-command identifier from @ref mrq_telemetry_ex_cmd */
	uint32_t cmd_code;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_TELEMETRY_EX sub-command
 *        #CMD_TELEMETRY_EX_BASE_SZ_GET
 */
struct cmd_telemetry_ex_base_sz_get_response {
	/**
	 *  @brief Physical address of telemetry data buffer
	 *
	 *  0 if no buffer is allocated for the initiator sending MRQ.
	 */
	uint64_t buf_base_addr;
	/** @brief Telemetry data size in bytes */
	uint32_t buf_size;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for the #MRQ_TELEMETRY_EX -command
 *
 * | Sub-command                   | Request payload                        |
 * |-------------------------------|----------------------------------------|
 * | #CMD_TELEMETRY_EX_QUERY_ABI   | #cmd_telemetry_ex_query_abi_request    |
 * | #CMD_TELEMETRY_EX_BASE_SZ_GET | -                                      |
 */
struct mrq_telemetry_ex_request {
	/** @brief Sub-command ID from @ref mrq_telemetry_ex_cmd. */
	uint32_t cmd;
	union {
		struct cmd_telemetry_ex_query_abi_request
			telemetry_ex_query_abi_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_TELEMETRY_EX -command.
 *
 * | Sub-command                   | Response payload                       |
 * |-------------------------------|----------------------------------------|
 * | #CMD_TELEMETRY_EX_QUERY_ABI   | -                                      |
 * | #CMD_TELEMETRY_EX_BASE_SZ_GET | #cmd_telemetry_ex_base_sz_get_response |
 */
struct mrq_telemetry_ex_response {
	union {
		struct cmd_telemetry_ex_base_sz_get_response
			telemetry_ex_base_sz_get_rsp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} Telemetry_ex */
/** @endcond */

/** @cond (bpmp_th500 || bpmp_tb500)
 * @ingroup MRQ_Codes
 * @def MRQ_PWR_LIMIT
 * @brief Control power limits.
 *
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_pwr_limit_request
 * * Response Payload: @ref mrq_pwr_limit_response
 *
 * @addtogroup Pwrlimit
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_PWR_LIMIT.
 */
enum mrq_pwr_limit_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * #MRQ_PWR_LIMIT sub-command.
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_PWR_LIMIT_QUERY_ABI = 0,

	/**
	 * @brief Set power limit
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * |----------------|---------------------------------------------|
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_PWR_LIMIT is not supported by BPMP-FW. |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 * | -#BPMP_EACCES  | Request is not accepted.                    |
	 */
	CMD_PWR_LIMIT_SET = 1,

	/**
	 * @brief Get power limit setting
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * |----------------|---------------------------------------------|
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_PWR_LIMIT is not supported by BPMP-FW. |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_PWR_LIMIT_GET = 2,

	/**
	 * @brief Get current aggregated power cap
	 *
	 * Get currently applied power cap for the specified limit id
	 * aggregated across all limit sources and types.
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * |----------------|---------------------------------------------|
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_PWR_LIMIT is not supported by BPMP-FW. |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_PWR_LIMIT_CURR_CAP = 3,
};

/**
 * @defgroup bpmp_pwr_limit_type PWR_LIMIT TYPEs
 * @{
 */
/** @brief Limit value specifies target cap */
#define PWR_LIMIT_TYPE_TARGET_CAP		0U
/** @brief Limit value specifies maximum possible target cap */
#define PWR_LIMIT_TYPE_BOUND_MAX		1U
/** @brief Limit value specifies minimum possible target cap */
#define PWR_LIMIT_TYPE_BOUND_MIN		2U
/** @brief Number of limit types supported by #MRQ_PWR_LIMIT command */
#define PWR_LIMIT_TYPE_NUM			3U

/** @} bpmp_pwr_limit_type */

/**
 * @brief Request data for #MRQ_PWR_LIMIT command CMD_PWR_LIMIT_QUERY_ABI
 */
struct cmd_pwr_limit_query_abi_request {
	/** @brief Sub-command identifier from @ref mrq_pwr_limit_cmd */
	uint32_t cmd_code;
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
	/** @brief Power limit identifier from @ref bpmp_pwr_limit_id */
	uint32_t limit_id;
	/** @brief Power limit source identifier from @ref bpmp_pwr_limit_src */
	uint32_t limit_src;  /**< @ref bpmp_pwr_limit_src */
	/** @brief Power limit type from @ref bpmp_pwr_limit_type */
	uint32_t limit_type;
	/** @brief New power limit value */
	uint32_t limit_setting;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_PWR_LIMIT sub-command #CMD_PWR_LIMIT_GET
 *
 * Get previously set from specified source specified limit value of specified
 * type.
 */
struct cmd_pwr_limit_get_request {
	/** @brief Power limit identifier from @ref bpmp_pwr_limit_id */
	uint32_t limit_id;
	/** @brief Power limit source identifier from @ref bpmp_pwr_limit_src */
	uint32_t limit_src;  /**< @ref bpmp_pwr_limit_src */
	/** @brief Power limit type from @ref bpmp_pwr_limit_type */
	uint32_t limit_type;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_PWR_LIMIT sub-command #CMD_PWR_LIMIT_GET
 */
struct cmd_pwr_limit_get_response {
	/** @brief Power limit value */
	uint32_t limit_setting;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_PWR_LIMIT sub-command #CMD_PWR_LIMIT_CURR_CAP
 *
 * For specified limit get current power cap aggregated from all sources.
 */
struct cmd_pwr_limit_curr_cap_request {
	/** @brief Power limit identifier from @ref bpmp_pwr_limit_id */
	uint32_t limit_id;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_PWR_LIMIT sub-command #CMD_PWR_LIMIT_CURR_CAP
 */
struct cmd_pwr_limit_curr_cap_response {
	/** @brief Current power cap value */
	uint32_t curr_cap;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for the #MRQ_PWR_LIMIT -command
 *
 * | Sub-command              | Request payload                  |
 * |--------------------------|----------------------------------|
 * | #CMD_PWR_LIMIT_QUERY_ABI | #cmd_pwr_limit_query_abi_request |
 * | #CMD_PWR_LIMIT_SET       | #cmd_pwr_limit_set_request       |
 * | #CMD_PWR_LIMIT_GET       | #cmd_pwr_limit_get_request       |
 * | #CMD_PWR_LIMIT_CURR_CAP  | #cmd_pwr_limit_curr_cap_request  |
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
 * @brief Response payload for the #MRQ_PWR_LIMIT -command.
 *
 * | Sub-command              | Response payload                 |
 * |--------------------------|----------------------------------|
 * | #CMD_PWR_LIMIT_QUERY_ABI | -                                |
 * | #CMD_PWR_LIMIT_SET       | -                                |
 * | #CMD_PWR_LIMIT_GET       | #cmd_pwr_limit_get_response      |
 * | #CMD_PWR_LIMIT_CURR_CAP  | #cmd_pwr_limit_curr_cap_response |
 */
struct mrq_pwr_limit_response {
	union {
		struct cmd_pwr_limit_get_response pwr_limit_get_rsp;
		struct cmd_pwr_limit_curr_cap_response pwr_limit_curr_cap_rsp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} Pwrlimit */
/** @endcond */



/** @cond (bpmp_th500)
 * @ingroup MRQ_Codes
 * @def MRQ_PWRMODEL
 * @brief Retrieve power evaluted by SoC power model.
 *
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_pwrmodel_request
 * * Response Payload: @ref mrq_pwrmodel_response
 *
 * @addtogroup Pwrmodel
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_PWRMODEL.
 */
enum mrq_pwrmodel_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * #MRQ_PWRMODEL sub-command.
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_PWRMODEL_QUERY_ABI = 0,

	/**
	 * @brief Get power model output power
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * |----------------|---------------------------------------------|
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_PWRMODEL is not supported by BPMP-FW.  |
	 * | -#BPMP_ERANGE  | Power model calculation overflow.           |
	 */
	CMD_PWRMODEL_PWR_GET = 1,
};

/**
 * @brief Request data for #MRQ_PWRMODEL sub-command #CMD_PWRMODEL_QUERY_ABI
 */
struct cmd_pwrmodel_query_abi_request {
	/** @brief Sub-command identifier from @ref mrq_pwrmodel_cmd */
	uint32_t cmd_code;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_PWRMODEL sub-command #CMD_PWRMODEL_PWR_GET
 *
 * Rerieve power evaluated by power model for specified work-load factor,
 * temperature, and cpu iso frequency for all cores.
 */
struct cmd_pwrmodel_pwr_get_request {
	/** @brief Unitless work load factor to evaluate power model at */
	uint32_t work_load_factor;
	/** @brief CPU frequency in kHz to evaluate power model at */
	uint32_t cpu_frequency;
	/** @brief Temperature in mC to evaluate power model at */
	int32_t temperature;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_PWRMODEL sub-command #CMD_PWRMODEL_PWR_GET
 */
struct cmd_pwrmodel_pwr_get_response {
	/** @brief Power model output in mW */
	uint32_t power;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for the #MRQ_PWRMODEL -command
 *
 * | Sub-command              | Request payload                  |
 * |--------------------------|----------------------------------|
 * | #CMD_PWRMODEL_QUERY_ABI  | #cmd_pwrmodel_query_abi_request  |
 * | #CMD_PWRMODEL_PWR_GET    | #cmd_pwrmodel_pwr_get_request    |
 */
struct mrq_pwrmodel_request {
	uint32_t cmd;
	union {
		struct cmd_pwrmodel_query_abi_request pwrmodel_query_abi_req;
		struct cmd_pwrmodel_pwr_get_request pwrmodel_pwr_get_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_PWRMODEL -command.
 *
 * | Sub-command              | Response payload                 |
 * |--------------------------|----------------------------------|
 * | #CMD_PWRMODEL_QUERY_ABI  | -                                |
 * | #CMD_PWRMODEL_PWR_GET    | #cmd_pwrmodel_pwr_get_response   |
 */
struct mrq_pwrmodel_response {
	union {
		struct cmd_pwrmodel_pwr_get_response pwrmodel_pwr_get_rsp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} Pwrmodel */
/** @endcond */


/** @cond (bpmp_th500)
 * @ingroup MRQ_Codes
 * @def MRQ_PWR_CNTRL
 * @brief Configure power controllers.
 *
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_pwr_cntrl_request
 * * Response Payload: @ref mrq_pwr_cntrl_response
 *
 * @addtogroup Pwrcntrl
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_PWR_CNTRL.
 */
enum mrq_pwr_cntrl_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * #MRQ_PWR_CNTRL sub-command.
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_PWR_CNTRL_QUERY_ABI = 0,

	/**
	 * @brief Switch power controller to/out of bypass mode
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * |----------------|---------------------------------------------|
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_PWR_CNTRL is not supported by BPMP-FW. |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 * | -#BPMP_ENOTSUP | Bypass mode is not supported.               |
	 */
	CMD_PWR_CNTRL_BYPASS_SET = 1,

	/**
	 * @brief Get power controller bypass mode status
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * |----------------|---------------------------------------------|
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_PWR_CNTRL is not supported by BPMP-FW. |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_PWR_CNTRL_BYPASS_GET = 2,
};

/**
 * @brief Request data for #MRQ_PWR_CNTRL sub-command #CMD_PWR_CNTRL_QUERY_ABI
 */
struct cmd_pwr_cntrl_query_abi_request {
	/** @brief Sub-command identifier from @ref mrq_pwr_cntrl_cmd */
	uint32_t cmd_code;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_PWR_CNTRL sub-command #CMD_PWR_CNTRL_BYPASS_SET
 *
 * Switch specified power controller to / out of bypass mode provided such
 * mode is supported by the controller.
 */
struct cmd_pwr_cntrl_bypass_set_request {
	/** @brief Power controller identifier from @ref bpmp_pwr_cntrl_id */
	uint32_t cntrl_id;
	/**
	 * @brief Bypass setting.
	 *
	 * Valid values:
	 *
	 * * 1 to enter bypass mode,
	 * * 0 to exit bypass mode.
	 */
	uint32_t bypass_setting;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_PWR_CNTRL sub-command #CMD_PWR_CNTRL_BYPASS_GET
 *
 * Get bypass mode status of the specified power controller.
 */
struct cmd_pwr_cntrl_bypass_get_request {
	/** @brief Power controller identifier from @ref bpmp_pwr_cntrl_id */
	uint32_t cntrl_id;
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_PWR_CNTRL sub-command #CMD_PWR_CNTRL_BYPASS_GET
 *
 * Get current bypass mode status if such mode is supported by the controller.
 * Otherwise, return "out of bypass" .
 */
struct cmd_pwr_cntrl_bypass_get_response {
	/**
	 * @brief Bypass mode status: 1 controller is in bypass,
	 * 0 controller is out of bypass.
	 */
	uint32_t bypass_status;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for the #MRQ_PWR_CNTRL -command
 *
 * | Sub-command               | Request payload                   |
 * |---------------------------|-----------------------------------|
 * | #CMD_PWR_CNTRL_QUERY_ABI  | #cmd_pwr_cntrl_query_abi_request  |
 * | #CMD_PWR_CNTRL_BYPASS_SET | #cmd_pwr_cntrl_bypass_set_request |
 * | #CMD_PWR_CNTRL_BYPASS_GET | #cmd_pwr_cntrl_bypass_get_request |
 */
struct mrq_pwr_cntrl_request {
	uint32_t cmd;
	union {
		struct cmd_pwr_cntrl_query_abi_request pwr_cntrl_query_abi_req;
		struct cmd_pwr_cntrl_bypass_set_request pwr_cntrl_bypass_set_req;
		struct cmd_pwr_cntrl_bypass_get_request pwr_cntrl_bypass_get_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_PWR_CNTRL -command.
 *
 * | Sub-command               | Response payload                  |
 * |---------------------------|-----------------------------------|
 * | #CMD_PWR_CNTRL_QUERY_ABI  | -                                 |
 * | #CMD_PWR_CNTRL_BYPASS_SET | -                                 |
 * | #CMD_PWR_CNTRL_BYPASS_GET | #cmd_pwr_cntrl_bypass_get_response|
 */
struct mrq_pwr_cntrl_response {
	union {
		struct cmd_pwr_cntrl_bypass_get_response pwr_cntrl_bypass_get_rsp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} Pwrcntrl */
/** @endcond */


/** @cond (bpmp_t264)
 * @ingroup MRQ_Codes
 * @def MRQ_SLC
 * @brief Configure SLC state.
 *
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_slc_request
 * * Response Payload: @ref mrq_slc_response
 *
 * @addtogroup Slc
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_SLC.
 */
enum mrq_slc_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * #MRQ_SLC sub-command.
	 *
	 * mrq_response::err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_SLC_QUERY_ABI = 0,

	/**
	 * @brief Switch SLC to/out of bypass mode
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * |----------------|---------------------------------------------|
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_SLC is not supported by BPMP-FW.       |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 * | -#BPMP_ENOTSUP | Bypass mode is not supported.               |
	 */
	CMD_SLC_BYPASS_SET = 1,

	/**
	 * @brief Get SLC bypass mode status
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * |----------------|---------------------------------------------|
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_SLC is not supported by BPMP-FW.       |
	 */
	CMD_SLC_BYPASS_GET = 2,
};

/**
 * @brief Request data for #MRQ_SLC sub-command #CMD_SLC_QUERY_ABI
 */
struct cmd_slc_query_abi_request {
	/** @brief Sub-command identifier from @ref mrq_slc_cmd */
	uint32_t cmd_code;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_SLC sub-command #CMD_SLC_BYPASS_SET
 *
 * Switch SLC to / out of bypass mode provided such
 * mode is supported by the SLC.
 */
struct cmd_slc_bypass_set_request {
	/**
	 * @brief Bypass setting.
	 *
	 * Valid values:
	 *
	 * * 1 to enter bypass mode,
	 * * 0 to exit bypass mode.
	 */
	uint32_t bypass_setting;
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_SLC sub-command #CMD_SLC_BYPASS_GET
 *
 * Get current bypass mode status if such mode is supported by the SLC.
 * Otherwise, return "out of bypass" .
 */
struct cmd_slc_bypass_get_response {
	/**
	 * @brief Bypass mode status: 1 SLC is in bypass,
	 * 0 SLC is out of bypass.
	 */
	uint32_t bypass_status;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for the #MRQ_SLC -command
 *
 * | Sub-command               | Request payload                   |
 * |---------------------------|-----------------------------------|
 * | #CMD_SLC_QUERY_ABI        | #cmd_slc_query_abi_request        |
 * | #CMD_SLC_BYPASS_SET       | #cmd_slc_bypass_set_request       |
 * | #CMD_SLC_BYPASS_GET       | -       |
 */
struct mrq_slc_request {
	uint32_t cmd;
	union {
		struct cmd_slc_query_abi_request slc_query_abi_req;
		struct cmd_slc_bypass_set_request slc_bypass_set_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_SLC -command.
 *
 * | Sub-command               | Response payload                  |
 * |---------------------------|-----------------------------------|
 * | #CMD_SLC_QUERY_ABI        | -                                 |
 * | #CMD_SLC_BYPASS_SET       | -                                 |
 * | #CMD_SLC_BYPASS_GET       | #cmd_slc_bypass_get_response      |
 */
struct mrq_slc_response {
	union {
		struct cmd_slc_bypass_get_response slc_bypass_get_rsp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} Slc */
/** @endcond */

/** @cond (bpmp_th500)
 * @ingroup MRQ_Codes
 * @def MRQ_GEARS
 * @brief Get thresholds for NDIV offset switching
 *
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
	uint32_t ncpu[8];
	/** @brief ndiv offset for each gear */
	uint32_t ndiv_offset[8];
	/** @brief voltage below which gears are disabled */
	uint32_t uv_threshold;
} BPMP_ABI_PACKED;

/** @} Gears */
/** @endcond */


/**
 * @ingroup MRQ_Codes
 * @def MRQ_SHUTDOWN
 * @brief System shutdown
 *
 * This message indicates system shutdown or reboot request. BPMP will
 * initiate system shutdown/reboot after receiving this message, it
 * may include turning off some rails in sequence and programming
 * PMIC.
 *
 * * Initiators: CPU_S, MCE
 * * Targets: BPMP
 * * Request Payload: @ref mrq_shutdown_request
 * * Response Payload: N/A
 * @addtogroup Shutdown
 * @{
 */

/**
 * @brief Request with #MRQ_SHUTDOWN
 */
struct mrq_shutdown_request {
	/**
	 * @brief Shutdown state ID
	 *
	 * Legal values:
	 * *  0 - Power off
	 * *  1 - Reboot
	 * @cond bpmp_t264
	 * *  2 - Suspend
	 * @endcond
	 */
	uint32_t state;
} BPMP_ABI_PACKED;

/** @} Shutdown */

/** @cond (bpmp_th500 || bpmp_tb500)
 * @defgroup bpmp_c2c_status C2C link status
 * @addtogroup bpmp_c2c_status
 * @{
 */
/** @brief initial status code */
#define BPMP_C2C_STATUS_INIT_NOT_STARTED		0
/** @brief Invalid speedo code */
#define BPMP_C2C_STATUS_C2C_INVALID_SPEEDO_CODE		7
/** @brief Invalid frequency */
#define BPMP_C2C_STATUS_C2C_INVALID_FREQ		8
/** @brief Invalid link */
#define BPMP_C2C_STATUS_C2C_INVALID_LINK		9
/** @brief refpll lock polling times out - partition 0 */
#define BPMP_C2C_STATUS_C2C0_REFPLL_FAIL		10
/** @brief refpll lock polling times out - partition 1 */
#define BPMP_C2C_STATUS_C2C1_REFPLL_FAIL		11
/** @brief PLL cal times out - partition 0 */
#define BPMP_C2C_STATUS_C2C0_PLLCAL_FAIL		12
/** @brief PLL cal times out - partition 1 */
#define BPMP_C2C_STATUS_C2C1_PLLCAL_FAIL		13
/** @brief clock detection times out - partition 0 */
#define BPMP_C2C_STATUS_C2C0_CLKDET_FAIL		14
/** @brief clock detection times out - partition 1 */
#define BPMP_C2C_STATUS_C2C1_CLKDET_FAIL		15
/** @brief Final trainings fail partition 0 */
#define BPMP_C2C_STATUS_C2C0_TR_FAIL			16
/** @brief Final trainings fail partition 1 */
#define BPMP_C2C_STATUS_C2C1_TR_FAIL			17
/** @brief C2C FW init done */
#define NV_GFW_GLOBAL_DEVINIT_C2C_STATUS_C2C_FW_INIT_DONE	20
/** @brief C2C FW init failed partition 0 */
#define NV_GFW_GLOBAL_DEVINIT_C2C_STATUS_C2C0_FW_INIT_FAIL	21
/** @brief C2C FW init failed partition 1 */
#define NV_GFW_GLOBAL_DEVINIT_C2C_STATUS_C2C1_FW_INIT_FAIL	22
/** @brief no failure seen, c2c init was successful */
#define BPMP_C2C_STATUS_C2C_LINK_TRAIN_PASS		255
/** @} bpmp_c2c_status */

/**
 * @ingroup MRQ_Codes
 * @def MRQ_C2C
 * @brief Control C2C partitions initialization.
 *
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_c2c_request
 * * Response Payload: @ref mrq_c2c_response
 *
 * @addtogroup C2C
 * @{
 */
enum mrq_c2c_cmd {
	/**
	 * @brief Check whether the BPMP driver supports the specified request
	 * type
	 *
	 * mrq_response:: err is 0 if the specified request is supported and
	 * -#BPMP_ENODEV otherwise
	 */
	CMD_C2C_QUERY_ABI = 0,

	/**
	 * @brief Start C2C initialization
	 *
	 * mrq_response:err is
	 * * 0: Success
	 * * -#BPMP_ENODEV: MRQ_C2C is not supported by BPMP-FW
	 * * -#BPMP_ENAVAIL: Invalid request parameters
	 * * -#BPMP_EACCES: Request is not accepted
	 */
	CMD_C2C_START_INITIALIZATION = 1,

	/**
	 * @brief Command to query current C2C training status
	 *
	 * This command will return the result of the latest C2C re-training that is initiated with
	 * MRQ_C2C.CMD_C2C_START_INITIALIZATION or MRQ_C2C.CMD_C2C_START_HOTRESET calls.
	 * If no training has been initiated yet, the command will return code BPMP_C2C_STATUS_INIT_NOT_STARTED.
	 *
	 * mrq_response:err is
	 * * 0: Success
	 * * -#BPMP_ENODEV: MRQ_C2C is not supported by BPMP-FW
	 * * -#BPMP_EACCES: Request is not accepted
	 */
	CMD_C2C_GET_STATUS = 2,
	/**
	 * @brief C2C hot-reset precondition
	 *
	 * mrq_response:err is
	 * * 0: Success
	 * * -#BPMP_ENODEV: MRQ_C2C is not supported by BPMP-FW
	 * * -#BPMP_ENAVAIL: Invalid request parameters
	 * * -#BPMP_EACCES: Request is not accepted
	 */
	CMD_C2C_HOTRESET_PREP = 3,
	/**
	 * @brief Start C2C hot-reset
	 *
	 * mrq_response:err is
	 * * 0: Success
	 * * -#BPMP_ENODEV: MRQ_C2C is not supported by BPMP-FW
	 * * -#BPMP_ENAVAIL: Invalid request parameters
	 * * -#BPMP_EACCES: Request is not accepted
	 */
	CMD_C2C_START_HOTRESET = 4,

	CMD_C2C_MAX
};

/**
 * @brief Request data for #MRQ_C2C command CMD_C2C_QUERY_ABI
 */
struct cmd_c2c_query_abi_request {
	/** @brief Command identifier to be queried */
	uint32_t cmd;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_C2C command CMD_C2C_START_INITIALIZATION
 */
struct cmd_c2c_start_init_request {
	/** @brief 1: partition 0; 2: partition 1; 3: partition 0 and 1; */
	uint8_t partitions;
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_C2C command CMD_C2C_START_INITIALIZATION
 */
struct cmd_c2c_start_init_response {
	/** @brief Refer to @ref bpmp_c2c_status */
	uint8_t status;
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_C2C command CMD_C2C_GET_STATUS
 */
struct cmd_c2c_get_status_response {
	/** @brief Refer to @ref bpmp_c2c_status */
	uint8_t status;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_C2C command CMD_C2C_HOTRESET_PREP
 */
struct cmd_c2c_hotreset_prep_request {
	/** @brief 1: partition 0; 2: partition 1; 3: partition 0 and 1; */
	uint8_t partitions;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_C2C command CMD_C2C_START_HOTRESET
 */
struct cmd_c2c_start_hotreset_request {
	/** @brief 1: partition 0; 2: partition 1; 3: partition 0 and 1; */
	uint8_t partitions;
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_C2C command CMD_C2C_START_HOTRESET
 */
struct cmd_c2c_start_hotreset_response {
	/** @brief Refer to @ref bpmp_c2c_status */
	uint8_t status;
} BPMP_ABI_PACKED;

/**
 * @brief Request with #MRQ_C2C
 *
 * |sub-command                  |payload                        |
 * |-----------------------------|-------------------------------|
 * |CMD_C2C_QUERY_ABI            |cmd_c2c_query_abi_request      |
 * |CMD_C2C_START_INITIALIZATION |cmd_c2c_start_init_request     |
 * |CMD_C2C_GET_STATUS           |                               |
 * |CMD_C2C_HOTRESET_PREP        |cmd_c2c_hotreset_prep_request  |
 * |CMD_C2C_START_HOTRESET       |cmd_c2c_start_hotreset_request |

 */
struct mrq_c2c_request {
	uint32_t cmd;
	union {
		struct cmd_c2c_query_abi_request c2c_query_abi_req;
		struct cmd_c2c_start_init_request c2c_start_init_req;
		struct cmd_c2c_hotreset_prep_request c2c_hotreset_prep_req;
		struct cmd_c2c_start_hotreset_request c2c_start_hotreset_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response to MRQ_C2C
 *
 * |sub-command                  |payload                         |
 * |-----------------------------|--------------------------------|
 * |CMD_C2C_QUERY_ABI            |                                |
 * |CMD_C2C_START_INITIALIZATION |cmd_c2c_start_init_response     |
 * |CMD_C2C_GET_STATUS           |cmd_c2c_get_status_response     |
 * |CMD_C2C_HOTRESET_PREP        |                                |
 * |CMD_C2C_START_HOTRESET       |cmd_c2c_start_hotreset_response |
 */
struct mrq_c2c_response {
	union {
		struct cmd_c2c_start_init_response c2c_start_init_resp;
		struct cmd_c2c_get_status_response c2c_get_status_resp;
		struct cmd_c2c_start_hotreset_response c2c_start_hotreset_resp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;
/** @} */
/** @endcond */


/** @cond (bpmp_t264)
 * @ingroup MRQ_Codes
 * @def MRQ_PCIE
 * @brief Perform a PCIE operation
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_pcie_request
 *
 * @addtogroup PCIE
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_PCIE.
 */
enum mrq_pcie_cmd {
	/** @brief Initialize PCIE EP controller. */
	CMD_PCIE_EP_CONTROLLER_INIT = 0,
	/** @brief Disable PCIE EP controller. */
	CMD_PCIE_EP_CONTROLLER_OFF = 1,

	/** @brief Disable PCIE RP controller. */
	CMD_PCIE_RP_CONTROLLER_OFF = 100,

	CMD_PCIE_MAX,
};

/**
 * @brief Request payload for #MRQ_PCIE sub-command #CMD_PCIE_EP_CONTROLLER_INIT.
 */
struct cmd_pcie_ep_controller_init_request {
	/**
	 * @brief PCIe EP controller number.
	 * Valid entries for T264 are 2, 4 and 5.
	 */
	uint8_t ep_controller;
	/**
	 * @brief PCIe EP function programming interface code.
	 * Valid range in HW is [0, 0xFFU], BPMP-FW programs the input value without any check.
	 * It is up to the requester to send valid input as documented in "PCI CODE AND ID
	 * ASSIGNMENT SPECIFICATION".
	 */
	uint8_t progif_code;
	/**
	 * @brief PCIe EP function sub-class code.
	 * Valid range in HW is [0, 0xFFU], BPMP-FW programs the input value without any check.
	 * It is up to the requester to send valid input as documented in "PCI CODE AND ID
	 * ASSIGNMENT SPECIFICATION".
	 */
	uint8_t subclass_code;
	/**
	 * @brief PCIe EP function base class code.
	 * Valid range in HW is [0, 0xFFU], BPMP-FW programs the input value without any check.
	 * It is up to the requester to send valid input as documented in "PCI CODE AND ID
	 * ASSIGNMENT SPECIFICATION".
	 */
	uint8_t baseclass_code;
	/**
	 * @brief PCIe EP function device id.
	 * Valid range is [0, 0x7FU], only LSB 7 bits are writable in 16-bit PCI device id.
	 * Valid range check is done on input value and returns -BPMP_EINVAL on failure.
	 */
	uint8_t deviceid;
	/**
	 * @brief PCIe EP EP BAR1 size.
	 * Valid range is [6U, 16U], which translate to [64MB, 64GB] size.
	 * Valid range check is done on input value and returns -BPMP_EINVAL on failure.
	 */
	uint8_t bar1_size;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_PCIE sub-command #CMD_PCIE_EP_CONTROLLER_OFF.
 */
struct cmd_pcie_ep_controller_off_request {
	/** @brief EP controller number, T264 valid: 2, 4, 5. */
	uint8_t ep_controller;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for #MRQ_PCIE sub-command #CMD_PCIE_RP_CONTROLLER_OFF.
 */
struct cmd_pcie_rp_controller_off_request {
	/** @brief RP controller number, T264 valid: 1-5 */
	uint8_t rp_controller;
} BPMP_ABI_PACKED;

/**
 * @ingroup PCIE
 * @brief Request payload for the #MRQ_PCIE command.
 *
 * Used by the sender of an #MRQ_PCIE message to control PCIE.
 * Below table shows sub-commands with their corresponding payload data.
 *
 * |sub-command                           |payload                                  |
 * |--------------------------------------|-----------------------------------------|
 * |#CMD_PCIE_EP_CONTROLLER_INIT          |#cmd_pcie_ep_controller_init_request     |
 * |#CMD_PCIE_EP_CONTROLLER_OFF           |#cmd_pcie_ep_controller_off_request      |
 *
 * @cond (!bpmp_safe)
 *
 * The following additional MRQs are supported on non-functional-safety
 * builds:
 * |sub-command                           |payload                                  |
 * |--------------------------------------|-----------------------------------------|
 * |#CMD_PCIE_RP_CONTROLLER_OFF           |#cmd_pcie_rp_controller_off_request      |
 *
 * @endcond
 *
 */
struct mrq_pcie_request {
	/** @brief Sub-command ID from @ref mrq_pcie_cmd. */
	uint32_t cmd;

	union {
		struct cmd_pcie_ep_controller_init_request ep_ctrlr_init;
		struct cmd_pcie_ep_controller_off_request ep_ctrlr_off;
		/** @cond (!bpmp_safe) */
		struct cmd_pcie_rp_controller_off_request rp_ctrlr_off;
		/** @endcond */
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} PCIE */
/** @endcond */

/** @cond (bpmp_t264)
 * @ingroup MRQ_Codes
 * @def MRQ_CR7
 * @brief Perform a CR7 operation
 *
 * * Initiators: CPU_S
 * * Targets: BPMP
 * * Request Payload: @ref mrq_cr7_request
 *
 * @addtogroup CR7
 * @{
 */

/**
 * @brief Payload for #MRQ_CR7
 * 2 fields for future parameters are provided. These must be 0 currently.
 */
struct cmd_cr7_request {
	uint32_t fld0;
	uint32_t fld1;
} BPMP_ABI_PACKED;

struct cmd_cr7_query_abi_request {
	/** #MRQ_CR7 sub-command identifier from @ref mrq_cr7_cmd */
	uint32_t type;
} BPMP_ABI_PACKED;

/**
 * @brief Sub-command identifiers for #MRQ_CR7.
 */
enum mrq_cr7_cmd {
	/**
	 * @brief Check whether the BPMP driver supports the specified request
	 * type
	 *
	 * mrq_response:: err is 0 if the specified request is supported and
	 * -#BPMP_ENODEV otherwise
	 */
	CMD_CR7_QUERY_ABI = 0,

	/** @brief Enter CR7 state on the package BPMP-FW is running on. */
	CMD_CR7_ENTRY = 1,
	/** @brief Exit CR7 state on the package BPMP-FW is running on. */
	CMD_CR7_EXIT = 2,

	CMD_CR7_MAX,
};

/**
 * @ingroup CR7
 * @brief #MRQ_CR7 structure
 *
 * |Sub-command                 |Payload                    |
 * |----------------------------|---------------------------|
 * |#CMD_CR7_QUERY_ABI          | #cmd_cr7_query_abi_request|
 * |#CMD_CR7_ENTRY              | #cmd_cr7_request	    |
 * |#CMD_CR7_EXIT               | #cmd_cr7_request	    |

 */
struct mrq_cr7_request {
	/** @brief Sub-command ID from @ref mrq_cr7_cmd. */
	uint32_t cmd;
	union {
		struct cmd_cr7_query_abi_request query_abi;
		struct cmd_cr7_request cr7_request;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} CR7 */
/** @endcond */

/** @cond (bpmp_tb500)
 * @ingroup MRQ_Codes
 * @def MRQ_HWPM
 * @brief Configure and query HWPM functionality
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_hwpm_request
 * * Response Payload: @ref mrq_hwpm_response
 *
 * @addtogroup HWPM
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_HWPM.
 */
enum mrq_hwpm_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * #MRQ_HWPM sub-command.
	 *
	 * mrq_response:err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_HWPM_QUERY_ABI = 1,

	/**
	 * @brief Configure IPMU triggers
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_HWPM_IPMU_SET_TRIGGERS = 2,

	/**
	 * @brief Configure IPMU payloads and shifts
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_HWPM_IPMU_SET_PAYLOADS_SHIFTS = 3,

	/**
	 * @brief Get maximum number of payloads
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 */
	CMD_HWPM_IPMU_GET_MAX_PAYLOADS = 4,

	/**
	 * @brief Configure NVTHERM sample rate
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_HWPM_NVTHERM_SET_SAMPLE_RATE = 5,

	/**
	 * @brief Set NVTHERM bubble interval
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_HWPM_NVTHERM_SET_BUBBLE_INTERVAL = 6,

	/**
	 * @brief Configure NVTHERM DG flexible channels
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_HWPM_NVTHERM_SET_FLEX_CHANNELS = 7,

	/**
	 * @brief Get ISENSE sensor name
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_HWPM_ISENSE_GET_SENSOR_NAME = 8,

	/**
	 * @brief Get ISENSE sensor channel
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_HWPM_ISENSE_GET_SENSOR_CHANNEL = 9,

	/**
	 * @brief Get ISENSE sensor scale factor
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_HWPM_ISENSE_GET_SENSOR_SCALE_FACTOR = 10,

	/**
	 * @brief Get ISENSE sensor offset
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 * | -#BPMP_ENODATA | No sensor offset.                           |
	 */
	CMD_HWPM_ISENSE_GET_SENSOR_OFFSET = 11,

	/**
	 * @brief Get ISENSE sum block name
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_HWPM_ISENSE_GET_SUM_BLOCK_NAME = 12,

	/**
	 * @brief Get ISENSE sum input sensor IDs
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_HWPM is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_HWPM_ISENSE_GET_SUM_BLOCK_INPUTS = 13,

	/**
	 * @brief Largest supported #MRQ_HWPM sub-command identifier + 1
	 */
	CMD_HWPM_MAX,
};

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_QUERY_ABI
 */
struct cmd_hwpm_query_abi_req {
	/** @brief Sub-command identifier from @ref mrq_hwpm_cmd */
	uint32_t cmd_code;
} BPMP_ABI_PACKED;

/**
 * @brief Maximum array length for IPMU trigger bitmask
 */
#define HWPM_IPMU_TRIGGER_ARR_LEN	28U

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_IPMU_SET_TRIGGERS
 */
struct cmd_hwpm_ipmu_set_triggers_req {
	/** @brief IPMU physical ID
	 *
	 * @note Valid range from [0, MAX_CPU_CORES), see @ref bpmp_hwpm_core_config
	 */
	uint32_t ipmu_phys_id;
	/** @brief Trigger bitmask, see @ref bpmp_ipmu_trigger_ids
	 *
	 * @note Setting a trigger bit will cause the associated trigger to
	 *       generate an output packet from IPMU to the HWPM perfmux.
	 * @note Up to a maximum possible 896 triggers
	 */
	uint32_t triggers[HWPM_IPMU_TRIGGER_ARR_LEN];
} BPMP_ABI_PACKED;

/**
 * @brief Array length for IPMU payload bitmask
 */
#define HWPM_IPMU_PAYLOAD_ARR_LEN	26U

/**
 * @brief Array length for IPMU payload shift bitmask
 */
#define HWPM_IPMU_SHIFT_ARR_LEN	2U

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_IPMU_SET_PAYLOADS_SHIFTS
 */
struct cmd_hwpm_ipmu_set_payloads_shifts_req {
	/** @brief IPMU physical ID
	 *
	 * @note Valid range from [0, MAX_CPU_CORES), see @ref bpmp_hwpm_core_config
	 */
	uint32_t ipmu_phys_id;
	/** @brief Payload bitmask, see @ref bpmp_ipmu_payload_ids
	 *
	 * @note Setting a payload bit will add the associated payload to the
	 *       IPMU output packet.
	 * @note The maximum number of payloads is platform dependent,
	 *       @see #CMD_HWPM_IPMU_GET_MAX_PAYLOADS
	 * @note To disable IPMU streaming on this instance, set all payload bits to 0.
	 * @note Up to a maximum of 832 available payloads
	 */
	uint32_t payloads[HWPM_IPMU_PAYLOAD_ARR_LEN];
	/**
	 * @brief Payload shift mask
	 *
	 * @note Setting the i-th shift bit will right-shift the
	 *       i-th enabled payload by 1 bit.
	 * @note Up to a maximum of 64 simultaneous emitted payloads
	 */
	uint32_t shifts[HWPM_IPMU_SHIFT_ARR_LEN];
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_IPMU_GET_MAX_PAYLOADS
 */
struct cmd_hwpm_ipmu_get_max_payloads_req {
	BPMP_ABI_EMPTY
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_NVTHERM_SET_SAMPLE_RATE
 */
struct cmd_hwpm_nvtherm_set_sample_rate_req {
	/** @brief Sample rate in microseconds
	 *
	 * @note Requesting a sample rate of 0 will disable NVTHERM streaming.
	 */
	uint32_t sample_rate;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_NVTHERM_SET_BUBBLE_INTERVAL
 */
struct cmd_hwpm_nvtherm_set_bubble_interval_req {
	/** @brief Bubble interval in microseconds */
	uint32_t bubble_interval;
} BPMP_ABI_PACKED;

/**
 * @brief Maximum array length for NVTHERM flexible channel bitmask
 */
#define HWPM_NVTHERM_FLEX_CHANNEL_ARR_LEN	29U

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_NVTHERM_SET_FLEX_CHANNELS
 */
struct cmd_hwpm_nvtherm_set_flex_channels_req {
	/** @brief NVTHERM flexible channel bitmask
	 *
	 * @see #bpmp_nvtherm_flex_channel_ids
	 *
	 * @note Up to a maximum of 928 flexible channels
	 */
	uint32_t channels[HWPM_NVTHERM_FLEX_CHANNEL_ARR_LEN];
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SENSOR_NAME
 */
struct cmd_hwpm_isense_get_sensor_name_req {
	/** @brief Sensor ID from @ref bpmp_isense_sensor_ids */
	uint32_t sensor_id;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SENSOR_CHANNEL
 */
struct cmd_hwpm_isense_get_sensor_channel_req {
	/** @brief Sensor ID from @ref bpmp_isense_sensor_ids */
	uint32_t sensor_id;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SENSOR_SCALE_FACTOR
 */
struct cmd_hwpm_isense_get_sensor_scale_factor_req {
	/** @brief Sensor ID from @ref bpmp_isense_sensor_ids */
	uint32_t sensor_id;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SENSOR_OFFSET
 */
struct cmd_hwpm_isense_get_sensor_offset_req {
	/** @brief Sensor ID from @ref bpmp_isense_sensor_ids */
	uint32_t sensor_id;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SUM_BLOCK_NAME
 */
struct cmd_hwpm_isense_get_sum_block_name_req {
	/** @brief Sum block index */
	uint32_t sum_block_index;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SUM_BLOCK_INPUTS
 */
struct cmd_hwpm_isense_get_sum_block_inputs_req {
	/** @brief Sum block index */
	uint32_t sum_block_index;
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_HWPM sub-command #CMD_HWPM_IPMU_GET_MAX_PAYLOADS
 */
struct cmd_hwpm_ipmu_get_max_payloads_resp {
	/** @brief Maximum number of payloads */
	uint32_t max_payloads;
} BPMP_ABI_PACKED;

/**
 * @brief Maximum array length for ISENSE sensor name
 */
#define HWPM_ISENSE_SENSOR_MAX_NAME_LEN		64U

/**
 * @brief Response data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SENSOR_NAME
 */
struct cmd_hwpm_isense_get_sensor_name_resp {
	/** @brief Sensor name */
	char sensor_name[HWPM_ISENSE_SENSOR_MAX_NAME_LEN];
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SENSOR_CHANNEL
 */
struct cmd_hwpm_isense_get_sensor_channel_resp {
	/** @brief Physical channel index */
	uint32_t channel_index;
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SENSOR_SCALE_FACTOR
 */
struct cmd_hwpm_isense_get_sensor_scale_factor_resp {
	/** @brief Scale factor */
	float scale_factor;
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SENSOR_OFFSET
 */
struct cmd_hwpm_isense_get_sensor_offset_resp {
	/** @brief Offset sensor ID */
	uint32_t offset_sensor_id;
} BPMP_ABI_PACKED;

/**
 * @brief Maximum array length for ISENSE sum name
 */
#define HWPM_ISENSE_SUM_BLOCK_MAX_NAME_LEN	64U

/**
 * @brief Response data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SUM_BLOCK_NAME
 */
struct cmd_hwpm_isense_get_sum_block_name_resp {
	/** @brief Sum block name */
	char sum_block_name[HWPM_ISENSE_SUM_BLOCK_MAX_NAME_LEN];
} BPMP_ABI_PACKED;

/**
 * @brief Maximum array length for ISENSE sum block input sensor IDs
 */
#define HWPM_ISENSE_SUM_BLOCK_INPUTS_MAX	16U

/**
 * @brief Response data for #MRQ_HWPM sub-command #CMD_HWPM_ISENSE_GET_SUM_BLOCK_INPUTS
 */
struct cmd_hwpm_isense_get_sum_block_inputs_resp {
	/** @brief Input channel indices; negative if no input is applied */
	int32_t input_channel_idx[HWPM_ISENSE_SUM_BLOCK_INPUTS_MAX];
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for the #MRQ_HWPM -command
 *
 * | Sub-command                              | Request payload                              |
 * | ---------------------------------------- | -------------------------------------------- |
 * | #CMD_HWPM_QUERY_ABI                      | #cmd_hwpm_query_abi_req                      |
 * | #CMD_HWPM_IPMU_SET_TRIGGERS              | #cmd_hwpm_ipmu_set_triggers_req              |
 * | #CMD_HWPM_IPMU_SET_PAYLOADS_SHIFTS       | #cmd_hwpm_ipmu_set_payloads_shifts_req       |
 * | #CMD_HWPM_IPMU_GET_MAX_PAYLOADS          | #cmd_hwpm_ipmu_get_max_payloads_req          |
 * | #CMD_HWPM_NVTHERM_SET_SAMPLE_RATE        | #cmd_hwpm_nvtherm_set_sample_rate_req        |
 * | #CMD_HWPM_NVTHERM_SET_BUBBLE_INTERVAL    | #cmd_hwpm_nvtherm_set_bubble_interval_req    |
 * | #CMD_HWPM_NVTHERM_SET_FLEX_CHANNELS      | #cmd_hwpm_nvtherm_set_flex_channels_req      |
 * | #CMD_HWPM_ISENSE_GET_SENSOR_CHANNEL      | #cmd_hwpm_isense_get_sensor_channel_req      |
 * | #CMD_HWPM_ISENSE_GET_SENSOR_SCALE_FACTOR | #cmd_hwpm_isense_get_sensor_scale_factor_req |
 * | #CMD_HWPM_ISENSE_GET_SENSOR_OFFSET       | #cmd_hwpm_isense_get_sensor_offset_req       |
 * | #CMD_HWPM_ISENSE_GET_SUM_BLOCK_NAME      | #cmd_hwpm_isense_get_sum_block_name_req      |
 * | #CMD_HWPM_ISENSE_GET_SUM_BLOCK_INPUTS    | #cmd_hwpm_isense_get_sum_block_inputs_req    |
 */
struct mrq_hwpm_request {
	uint32_t cmd;
	union {
		struct cmd_hwpm_query_abi_req query_abi;
		struct cmd_hwpm_ipmu_set_triggers_req ipmu_set_triggers;
		struct cmd_hwpm_ipmu_set_payloads_shifts_req ipmu_set_payloads_shifts;
		struct cmd_hwpm_ipmu_get_max_payloads_req ipmu_get_max_payloads;
		struct cmd_hwpm_nvtherm_set_sample_rate_req nvtherm_set_sample_rate;
		struct cmd_hwpm_nvtherm_set_bubble_interval_req nvtherm_set_bubble_interval;
		struct cmd_hwpm_nvtherm_set_flex_channels_req nvtherm_set_flex_channels;
		struct cmd_hwpm_isense_get_sensor_name_req isense_get_sensor_name;
		struct cmd_hwpm_isense_get_sensor_channel_req isense_get_sensor_channel;
		struct cmd_hwpm_isense_get_sensor_scale_factor_req isense_get_sensor_scale_factor;
		struct cmd_hwpm_isense_get_sensor_offset_req isense_get_sensor_offset;
		struct cmd_hwpm_isense_get_sum_block_name_req isense_get_sum_block_name;
		struct cmd_hwpm_isense_get_sum_block_inputs_req isense_get_sum_block_inputs;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_HWPM -command
 *
 * | Sub-command                              | Response payload                              |
 * | ---------------------------------------- | --------------------------------------------- |
 * | #CMD_HWPM_QUERY_ABI                      | -                                             |
 * | #CMD_HWPM_IPMU_SET_TRIGGERS              | -                                             |
 * | #CMD_HWPM_IPMU_SET_PAYLOADS_SHIFTS       | -                                             |
 * | #CMD_HWPM_IPMU_GET_MAX_PAYLOADS          | #cmd_hwpm_ipmu_get_max_payloads_resp          |
 * | #CMD_HWPM_NVTHERM_SET_SAMPLE_RATE        | -                                             |
 * | #CMD_HWPM_NVTHERM_SET_BUBBLE_INTERVAL    | -                                             |
 * | #CMD_HWPM_NVTHERM_SET_FLEX_CHANNELS      | -                                             |
 * | #CMD_HWPM_ISENSE_GET_SENSOR_NAME         | #cmd_hwpm_isense_get_sensor_name_resp         |
 * | #CMD_HWPM_ISENSE_GET_SENSOR_CHANNEL      | #cmd_hwpm_isense_get_sensor_channel_resp      |
 * | #CMD_HWPM_ISENSE_GET_SENSOR_SCALE_FACTOR | #cmd_hwpm_isense_get_sensor_scale_factor_resp |
 * | #CMD_HWPM_ISENSE_GET_SENSOR_OFFSET       | #cmd_hwpm_isense_get_sensor_offset_resp       |
 * | #CMD_HWPM_ISENSE_GET_SUM_BLOCK_NAME      | #cmd_hwpm_isense_get_sum_block_name_resp      |
 * | #CMD_HWPM_ISENSE_GET_SUM_BLOCK_INPUTS    | #cmd_hwpm_isense_get_sum_block_inputs_resp    |
 */
struct mrq_hwpm_response {
	uint32_t err;
	union {
		struct cmd_hwpm_ipmu_get_max_payloads_resp ipmu_get_max_payloads;
		struct cmd_hwpm_isense_get_sensor_name_resp isense_get_sensor_name;
		struct cmd_hwpm_isense_get_sensor_channel_resp isense_get_sensor_channel;
		struct cmd_hwpm_isense_get_sensor_scale_factor_resp isense_get_sensor_scale_factor;
		struct cmd_hwpm_isense_get_sensor_offset_resp isense_get_sensor_offset;
		struct cmd_hwpm_isense_get_sum_block_name_resp isense_get_sum_block_name;
		struct cmd_hwpm_isense_get_sum_block_inputs_resp isense_get_sum_block_inputs;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} HWPM */
/** @endcond */

/** @cond (bpmp_tb500)
 * @ingroup MRQ_Codes
 * @def MRQ_DVFS
 * @brief Configure DVFS functionality
 *
 * * Initiators: CCPLEX
 * * Targets: BPMP
 * * Request Payload: @ref mrq_dvfs_request
 *
 * @addtogroup DVFS
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_DVFS.
 */
enum mrq_dvfs_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * #MRQ_DVFS sub-command.
	 *
	 * mrq_response:err is 0 if the specified request is
	 * supported and -#BPMP_ENODEV otherwise.
	 */
	CMD_DVFS_QUERY_ABI = 1,

	/**
	 * @brief Configure DVFS controller
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_DVFS is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_DVFS_SET_CTRL_STATE = 2,

	/**
	 * @brief Configure DVFS manager
	 *
	 * mrq_response:err is defined as:
	 *
	 * | Value          | Description                                 |
	 * | -------------- | ------------------------------------------- |
	 * | 0              | Success                                     |
	 * | -#BPMP_ENODEV  | #MRQ_DVFS is not supported by BPMP-FW.      |
	 * | -#BPMP_ENOTSUP | Subcommand is not supported by BPMP-FW.     |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                 |
	 */
	CMD_DVFS_SET_MGR_STATE = 3,

	/**
	 * @brief Largest supported #MRQ_DVFS sub-command identifier + 1
	 */
	CMD_DVFS_MAX,
};

/**
 * @brief Request data for #MRQ_DVFS sub-command #CMD_DVFS_QUERY_ABI
 */
struct cmd_dvfs_query_abi_req {
	/** @brief Sub-command identifier from @ref mrq_dvfs_cmd */
	uint32_t cmd_code;
} BPMP_ABI_PACKED;

struct cmd_dvfs_set_ctrl_state_req {
	/** @brief Controller ID from @ref bpmp_dvfs_ctrl_ids */
	uint32_t ctrl_id;
	/** @brief Controller enable state */
	uint32_t enable;
} BPMP_ABI_PACKED;

struct cmd_dvfs_set_mgr_state_req {
	/** @brief Manager ID from @ref bpmp_dvfs_mgr_ids */
	uint32_t mgr_id;
	/** @brief Manager enable state */
	uint32_t enable;
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for the #MRQ_DVFS -command
 *
 * | Sub-command                              | Request payload                              |
 * | ---------------------------------------- | -------------------------------------------- |
 * | #CMD_DVFS_QUERY_ABI                      | #cmd_dvfs_query_abi_req                      |
 * | #CMD_DVFS_SET_CTRL_STATE                 | #cmd_dvfs_set_ctrl_state_req                 |
 * | #CMD_DVFS_SET_MGR_STATE                  | #cmd_dvfs_set_mgr_state_req                  |
 */
struct mrq_dvfs_request {
	uint32_t cmd;
	union {
		struct cmd_dvfs_query_abi_req query_abi;
		struct cmd_dvfs_set_ctrl_state_req set_ctrl_state;
		struct cmd_dvfs_set_mgr_state_req set_mgr_state;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} DVFS */
/** @endcond */

/** @cond (bpmp_tb500)
 * @ingroup MRQ_Codes
 * @def MRQ_PPP_PROFILE
 * @brief Get power/performance profile configuration settings.
 *
 * * Initiators: Any
 * * Targets: BPMP
 * * Request Payload: @ref mrq_ppp_profile_request
 * * Response Payload: @ref mrq_ppp_profile_response
 *
 * @addtogroup PPP
 * @{
 */

/**
 * @brief Sub-command identifiers for #MRQ_PPP_PROFILE.
 */
enum mrq_ppp_profile_cmd {
	/**
	 * @brief Check whether the BPMP-FW supports the specified
	 * #MRQ_PPP_PROFILE sub-command.
	 *
	 * mrq_ppp_profile_response:err is 0 if the specified request is
	 * supported and -#BPMP_ENOTSUP otherwise.
	 */

	CMD_PPP_PROFILE_QUERY_ABI = 0,
	/**
	 * @brief Query the BPMP for the CPU core and SLC slice configuration associated
	 * with a given Power/Performance Profile (PPP).
	 *
	 * mrq_ppp_profile_response:err is defined as:
	 *
	 * | Value	    | Description                                    |
	 * |----------------|------------------------------------------------|
	 * | 0		    | Success                                        |
	 * | -#BPMP_ENOTSUP | #MRQ_PPP_PROFILE is not supported by BPMP-FW.  |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                    |
	 */
	CMD_PPP_PROFILE_QUERY_MASKS = 1,
	/**
	 * @brief Query BPMP for the CPU mask corresponding to a requested
	 * number of active CPU cores.
	 *
	 * mrq_ppp_profile_response:err is defined as:
	 *
	 * | Value          | Description                                    |
	 * |----------------|------------------------------------------------|
	 * | 0              | Success                                        |
	 * | -#BPMP_ENOTSUP | #MRQ_PPP_PROFILE is not supported by BPMP-FW.  |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                    |
	 */
	CMD_PPP_CORE_QUERY_CPU_MASK = 2,
	/**
	 * @brief Query BPMP-FW for the currently available Power/Performance Profiles.
	 *
	 * mrq_ppp_profile_response:err is defined as:
	 *
	 * | Value          | Description                                    |
	 * |----------------|------------------------------------------------|
	 * | 0              | Success                                        |
	 * | -#BPMP_ENOTSUP | #MRQ_PPP_PROFILE is not supported by BPMP-FW.  |
	 * | -#BPMP_EINVAL  | Invalid request parameters.                    |
	 */
	CMD_PPP_AVAILABLE_QUERY = 3,
};

/**
 * @brief Request data for #MRQ_PPP_PROFILE sub-command
 *        #CMD_PPP_PROFILE_QUERY_ABI
 */
struct cmd_ppp_profile_query_abi_req {
	/** @brief Sub-command identifier from @ref mrq_ppp_profile_cmd */
	uint32_t cmd_code;
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_PPP_PROFILE sub-command
 *        #CMD_PPP_AVAILABLE_QUERY
 */
struct cmd_ppp_available_query_resp {
	/**
	 * @brief Bitmask of available profiles.
	 * Bit N = 1 ⇒ profile N is available
	 */
	uint32_t avail_ppp_mask;
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_PPP_PROFILE sub-command
 *        #CMD_PPP_PROFILE_QUERY_MASKS
 */
struct cmd_ppp_profile_query_masks_req {
	/** @brief power/perf profile identifier */
	uint32_t profile_id;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for #MRQ_PPP_PROFILE sub-command
 *        #CMD_PPP_PROFILE_QUERY_MASKS
 */
struct cmd_ppp_profile_query_masks_resp {
	/** @brief Enabled cores in this profile */
	uint32_t  num_active_cores;
	/** @brief Enabled SLC slices in this profile */
	uint32_t  num_active_slcs;
	/** @brief Number of valid words in active_core_masks array */
	uint32_t  max_num_core_words;
	/** @brief Number of valid words in active_slc_masks array */
	uint32_t  max_num_slc_words;
	/** @brief Enabled cores bit mask (bit N = 1 => core N enabled) */
	uint32_t  active_core_masks[8];
	/** @brief Enabled SLC slices bit mask (bit N = 1 => SLC slice N enabled) */
	uint32_t  active_slc_masks[8];
} BPMP_ABI_PACKED;

/**
 * @brief Request data for #MRQ_PPP_PROFILE sub-command
 *        #CMD_PPP_CORE_QUERY_CPU_MASK
 */
struct cmd_ppp_core_query_cpu_mask_req {
	/** @brief Requested number of active cores */
	uint32_t num_cores;
} BPMP_ABI_PACKED;

/**
 * @brief Response data for #MRQ_PPP_PROFILE sub-command
 *        #CMD_PPP_CORE_QUERY_CPU_MASK
 */
struct cmd_ppp_core_query_cpu_mask_resp {
	/** @brief Number of valid words in active_core_masks array */
	uint32_t max_num_words;
	/** @brief Enabled CPU core bitmask (bit N = 1 ⇒ core N enabled) */
	uint32_t active_core_masks[8];
} BPMP_ABI_PACKED;

/**
 * @brief Request payload for the #MRQ_PPP_PROFILE -command
 *
 * | Sub-command                   | Request payload                        |
 * |-------------------------------|----------------------------------------|
 * | #CMD_PPP_PROFILE_QUERY_ABI    | #cmd_ppp_profile_query_abi_req         |
 * | #CMD_PPP_PROFILE_QUERY_MASKS  | #cmd_ppp_profile_query_masks_req       |
 * | #CMD_PPP_CORE_QUERY_CPU_MASK  | #cmd_ppp_core_query_cpu_mask_req           |
 * | #CMD_PPP_AVAILABLE_QUERY      | -                                      |
 */
struct mrq_ppp_profile_request {
	/** @brief Sub-command ID from @ref mrq_ppp_profile_cmd. */
	uint32_t cmd;
	union {
		struct cmd_ppp_profile_query_abi_req query_abi;
		struct cmd_ppp_profile_query_masks_req ppp_profile_masks_req;
		struct cmd_ppp_core_query_cpu_mask_req ppp_core_mask_req;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/**
 * @brief Response payload for the #MRQ_PPP_PROFILE -command.
 *
 * | Sub-command                   | Response payload                       |
 * |-------------------------------|----------------------------------------|
 * | #CMD_PPP_PROFILE_QUERY_ABI    | -                                      |
 * | #CMD_PPP_PROFILE_QUERY_MASKS  | #cmd_ppp_profile_query_masks_resp      |
 * | #CMD_PPP_CORE_QUERY_CPU_MASK  | #cmd_ppp_core_query_cpu_mask_resp          |
 * | #CMD_PPP_AVAILABLE_QUERY      | #cmd_ppp_available_query_resp          |
 */
struct mrq_ppp_profile_response {
	uint32_t err;
	union {
		struct cmd_ppp_profile_query_masks_resp ppp_profile_masks_resp;
		struct cmd_ppp_core_query_cpu_mask_resp ppp_core_mask_resp;
		struct cmd_ppp_available_query_resp ppp_avail_query_resp;
	} BPMP_UNION_ANON;
} BPMP_ABI_PACKED;

/** @} PPP */
/** @endcond */

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
/** @brief No data */
#define BPMP_ENODATA	61
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
