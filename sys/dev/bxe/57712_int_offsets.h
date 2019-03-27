/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2007-2017 QLogic Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


static const struct iro e2_iro_arr[385] = {
	{     0x40,      0x0,      0x0,      0x0,      0x0},	// COMMON_SB_SIZE
	{     0x40,      0x0,      0x0,      0x0,      0x0},	// COMMON_SB_DATA_SIZE
	{     0x28,      0x0,      0x0,      0x0,      0x0},	// COMMON_SP_SB_SIZE
	{     0x10,      0x0,      0x0,      0x0,      0x0},	// COMMON_SP_SB_DATA_SIZE
	{     0x40,      0x0,      0x0,      0x0,      0x0},	// COMMON_DYNAMIC_HC_CONFIG_SIZE
	{     0x10,      0x0,      0x0,      0x0,      0x0},	// COMMON_ASM_ASSERT_MSG_SIZE
	{      0x8,      0x0,      0x0,      0x0,      0x0},	// COMMON_ASM_ASSERT_INDEX_SIZE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_ASM_INVALID_ASSERT_OPCODE
	{     0x3d,      0x0,      0x0,      0x0,      0x0},	// COMMON_RAM1_TEST_EVENT_ID
	{     0x3c,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_EVENT_ID
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_1_OFFSET
	{      0x8,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_2_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_3_OFFSET
	{      0xc,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_1_RESULT_OFFSET
	{      0xe,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_2_RESULT_OFFSET
	{      0x4,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_3_RESULT_OFFSET
	{     0x18,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_1_MASK
	{     0x1c,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_2_MASK
	{     0x1c,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_3_MASK
	{     0x13,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_TEST_AGG_INT
	{     0x3e,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_TEST_EVENTID
	{      0x1,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_PCI_READ_OPCODE
	{      0x2,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_LOAD_CONTEXT_OPCODE
	{      0x1,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_LOAD_CONTEXT_INCVAL
	{     0x10,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_LOAD_CONTEXT_REGION
	{     0x50,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_LOAD_CONTEXT_CID
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_LOAD_CONTEXT_RUN_PBF_ECHO_TEST
	{      0x3,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_QM_PAUSE_OPCODE
	{     0xab,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_TEST_UNUSED_FOCS_SUCCESS_OPCODE_VALUE
	{      0x8,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_TEST_UNUSED_FOCS_OPCODE_VALUE
	{   0xc000,     0x10,      0x0,      0x0,      0x8},	// XSTORM_SPQ_PAGE_BASE_OFFSET(funcId)
	{   0xc008,     0x10,      0x0,      0x0,      0x2},	// XSTORM_SPQ_PROD_OFFSET(funcId)
	{   0xc000,     0x10,      0x0,      0x0,     0x10},	// XSTORM_SPQ_DATA_OFFSET(funcId)
	{   0x9c08,      0x4,      0x0,      0x0,      0x4},	// XSTORM_HIGIG_HDR_LENGTH_OFFSET(portId)
	{   0xc080,     0x10,      0x0,      0x0,      0x4},	// XSTORM_VF_SPQ_PAGE_BASE_OFFSET(vfId)
	{   0xc088,     0x10,      0x0,      0x0,      0x2},	// XSTORM_VF_SPQ_PROD_OFFSET(vfId)
	{   0xc080,     0x10,      0x0,      0x0,     0x10},	// XSTORM_VF_SPQ_DATA_OFFSET(vfId)
	{   0x9338,      0x1,      0x4,      0x0,      0x1},	// XSTORM_JUMBO_SUPPORT_OFFSET(pfId)
	{   0x9340,      0x0,      0x0,      0x0,      0x2},	// XSTORM_COMMON_IP_ID_MASK_OFFSET
	{   0x9348,      0x0,      0x0,      0x0,      0x8},	// XSTORM_COMMON_RTC_PARAMS_OFFSET
	{   0x934c,      0x0,      0x0,      0x0,      0x2},	// XSTORM_COMMON_RTC_RESOLUTION_OFFSET
	{   0x9350,      0x0,      0x0,      0x0,      0x8},	// XSTORM_FW_VERSION_OFFSET
	{   0x9698,     0x40,      0x0,      0x0,     0x40},	// XSTORM_LICENSE_VALUES_OFFSET(pfId)
	{   0x9358,     0x80,      0x0,      0x0,     0x48},	// XSTORM_CMNG_PER_PORT_VARS_OFFSET(portId)
	{   0x9458,     0x40,      0x0,      0x0,      0x8},	// XSTORM_RATE_SHAPING_PER_VN_VARS_OFFSET(pfId)
	{   0x9468,     0x40,      0x0,      0x0,     0x18},	// XSTORM_FAIRNESS_PER_VN_VARS_OFFSET(pfId)
	{  0x63010,     0x28,      0x0,      0x0,     0x28},	// XSTORM_PER_QUEUE_STATS_OFFSET(xStatQueueId)
	{   0x9950,      0x8,      0x0,      0x0,      0x1},	// XSTORM_FUNC_EN_OFFSET(funcId)
	{   0x9951,      0x8,      0x0,      0x0,      0x1},	// XSTORM_VF_TO_PF_OFFSET(funcId)
	{   0x9952,      0x8,      0x0,      0x0,      0x1},	// XSTORM_RECORD_SLOW_PATH_OFFSET(funcId)
	{   0x2008,     0x10,      0x0,      0x0,     0x10},	// XSTORM_ASSERT_LIST_OFFSET(assertListEntry)
	{   0x2000,      0x0,      0x0,      0x0,      0x8},	// XSTORM_ASSERT_LIST_INDEX_OFFSET
	{   0x9bb8,      0x0,      0x0,      0x0,      0x8},	// XSTORM_TIME_SYNC_TEST_ADDRESS_OFFSET
	{      0x1,      0x0,      0x0,      0x0,      0x0},	// PCI_READ_KUKUE_CODE_OPPCOE
	{      0x2,      0x0,      0x0,      0x0,      0x0},	// LOAD_CONTEXT_KUKUE_CODE_OPPCOE
	{      0x3,      0x0,      0x0,      0x0,      0x0},	// QM_PAUSE_KUKUE_CODE_OPPCOE
	{      0x4,      0x0,      0x0,      0x0,      0x0},	// PAUSE_TEST_XOFF_PORT0_KUKUE_CODE_OPPCOE
	{      0x5,      0x0,      0x0,      0x0,      0x0},	// PAUSE_TEST_XON_PORT0_KUKUE_CODE_OPPCOE
	{      0x6,      0x0,      0x0,      0x0,      0x0},	// PAUSE_TEST_XOFF_PORT1_KUKUE_CODE_OPPCOE
	{      0x7,      0x0,      0x0,      0x0,      0x0},	// PAUSE_TEST_XON_PORT1_KUKUE_CODE_OPPCOE
	{      0x8,      0x0,      0x0,      0x0,      0x0},	// TEST_UNUSED_FOCS_KUKUE_CODE_OPPCOE
	{      0x9,      0x0,      0x0,      0x0,      0x0},	// PBF_ECHO_KUKUE_CODE_OPPCOE
	{      0xa,      0x0,      0x0,      0x0,      0x0},	// TIME_SYNC_PORT0_KUKUE_CODE_OPPCOE
	{      0xb,      0x0,      0x0,      0x0,      0x0},	// TIME_SYNC_PORT1_KUKUE_CODE_OPPCOE
	{      0xc,      0x0,      0x0,      0x0,      0x0},	// IGU_TEST_KUKUE_CODE_OPPCOE
	{      0x1,      0x0,      0x0,      0x0,      0x0},	// XSTORM_AGG_INT_INITIAL_CLEANUP_INDEX
	{      0x9,      0x0,      0x0,      0x0,      0x0},	// XSTORM_AGG_INT_FINAL_CLEANUP_INDEX
	{      0x2,      0x0,      0x0,      0x0,      0x0},	// XSTORM_AGG_INT_FINAL_CLEANUP_COMP_TYPE
	{   0xc4c0,      0x0,      0x0,      0x0,     0x20},	// XSTORM_ERROR_HANDLER_STATISTICS_RAM_OFFSET
	{   0xc4e6,      0x0,      0x0,      0x0,      0x1},	// XSTORM_LB_PHYSICAL_QUEUES_INFO_OFFSET
	{   0x6000,     0x20,      0x0,      0x0,     0x20},	// XSTORM_QUEUE_ZONE_OFFSET(queueId)
	{   0x7300,      0x8,      0x0,      0x0,      0x8},	// XSTORM_VF_ZONE_OFFSET(vfId)
	{   0x9bf0,      0x0,      0x0,      0x0,      0x1},	// XSTORM_FIVE_TUPLE_SRC_EN_OFFSET
	{   0x9b90,      0x0,      0x0,      0x0,      0x8},	// XSTORM_E2_INTEG_RAM_OFFSET
	{   0x9b93,      0x0,      0x0,      0x0,      0x1},	// XSTORM_QM_OPPORTUNISTIC_RAM_OFFSET
	{   0x9b91,      0x0,      0x0,      0x0,      0x1},	// XSTORM_SIDE_INFO_INPUT_LSB_OFFSET
	{   0x9b96,      0x0,      0x0,      0x0,      0x1},	// XSTORM_E2_INTEG_VLAN_ID_OFFSET
	{   0x9b97,      0x0,      0x0,      0x0,      0x0},	// XSTORM_E2_INTEG_VLAN_ID_EN_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_VFC_TEST_LINE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_VFC_TEST_RESULT_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_VFC_OP_GEN_VALUE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_INBOUND_INTERRUPT_TEST_VF_INFO_SIZE_IN_BYTES
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_1_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_2_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_3_INDEX
	{  0x60000,      0x0,      0x0,      0x0,     0x20},	// XSTORM_DPM_BUFFER_OFFSET
	{   0x9b98,      0x0,      0x0,      0x0,      0x1},	// XSTORM_KUKU_TEST_OPCODE_OFFSET
	{   0x9bd8,      0x0,      0x0,      0x0,      0x8},	// XSTORM_KUKU_LOAD_CONTEXT_TEST_OFFSET
	{     0x53,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_OP_GEN_VALUE
	{   0x9be0,      0x0,      0x0,      0x0,      0x2},	// XSTORM_QM_PAUSE_TEST_QUEUE_MASK_OFFSET
	{   0x9be4,      0x0,      0x0,      0x0,      0x1},	// XSTORM_QM_PAUSE_TEST_GROUP_OFFSET
	{   0x9be5,      0x0,      0x0,      0x0,      0x1},	// XSTORM_QM_PAUSE_TEST_PORT_OFFSET
	{      0x9,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_OPCODE
	{      0x1,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_INCVAL
	{     0x44,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_REGION
	{      0x1,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_RUN_PBF_ECHO_TEST
	{     0x50,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_CID
	{     0x89,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_SUCCESS_VALUE
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_TIME_SYNC_FLG_OFFSET(funcId)
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_INDIRECTION_TABLE_ENTRY_SIZE
	{   0x16c8,      0x0,      0x0,      0x0,      0x8},	// TSTORM_COMMON_RTC_PARAMS_OFFSET
	{   0x2008,     0x10,      0x0,      0x0,     0x10},	// TSTORM_ASSERT_LIST_OFFSET(assertListEntry)
	{   0x2000,      0x0,      0x0,      0x0,      0x8},	// TSTORM_ASSERT_LIST_INDEX_OFFSET
	{   0x1aa8,      0x0,      0x0,      0x0,     0x10},	// TSTORM_MEASURE_PCI_LATENCY_CTRL_OFFSET
	{   0x1ab8,      0x0,      0x0,      0x0,     0x10},	// TSTORM_MEASURE_PCI_LATENCY_DATA_OFFSET
	{      0x1,      0x0,      0x0,      0x0,      0x0},	// TSTORM_AGG_MEASURE_PCI_LATENCY_INDEX
	{      0x2,      0x0,      0x0,      0x0,      0x0},	// TSTORM_AGG_MEASURE_PCI_LATENCY_COMP_TYPE
	{   0x17e0,      0x8,      0x0,      0x0,      0x1},	// TSTORM_FUNC_EN_OFFSET(funcId)
	{   0x17e1,      0x8,      0x0,      0x0,      0x1},	// TSTORM_VF_TO_PF_OFFSET(funcId)
	{   0x17e2,      0x8,      0x0,      0x0,      0x1},	// TSTORM_RECORD_SLOW_PATH_OFFSET(funcId)
	{  0x62078,     0x38,      0x0,      0x0,     0x38},	// TSTORM_PER_QUEUE_STATS_OFFSET(tStatQueueId)
	{   0x16f0,      0x0,      0x0,      0x0,      0x2},	// TSTORM_COMMON_SAFC_WORKAROUND_ENABLE_OFFSET
	{   0x16f2,      0x0,      0x0,      0x0,      0x2},	// TSTORM_COMMON_SAFC_WORKAROUND_TIMEOUT_10USEC_OFFSET
	{   0xa040,      0x0,      0x0,      0x0,     0x20},	// TSTORM_ERROR_HANDLER_STATISTICS_RAM_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_VFC_TEST_RSS_KEY_OFFSET(portId)
	{   0xe000,     0x20,      0x0,      0x0,     0x20},	// TSTORM_QUEUE_ZONE_OFFSET(queueId)
	{   0xf300,      0x8,      0x0,      0x0,      0x8},	// TSTORM_VF_ZONE_OFFSET(vfId)
	{   0x1708,      0x0,      0x0,      0x0,     0xd8},	// TSTORM_E2_INTEG_RAM_OFFSET
	{   0x174f,      0x0,      0x0,      0x0,      0x1},	// TSTORM_LSB_SIDE_BAND_INFO_OFFSET
	{   0x1727,      0x0,      0x0,      0x0,      0x1},	// TSTORM_MSB_SIDE_BAND_INFO_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_VFC_TEST_LINE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_VFC_TEST_RESULT_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_VFC_OP_GEN_VALUE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_INBOUND_INTERRUPT_TEST_VF_INFO_SIZE_IN_BYTES
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_1_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_2_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_3_INDEX
	{   0x1788,      0x0,      0x0,      0x0,      0x1},	// TSTORM_KUKU_TEST_OPCODE_OFFSET
	{   0x17c8,      0x0,      0x0,      0x0,      0x8},	// TSTORM_KUKU_LOAD_CONTEXT_TEST_OFFSET
	{     0x51,      0x0,      0x0,      0x0,      0x0},	// TSTORM_KUKU_OP_GEN_VALUE
	{   0x17b0,      0x0,      0x0,      0x0,      0x4},	// TSTORM_PCI_READ_TEST_ADDRESS_LO_OFFSET
	{   0x17b4,      0x0,      0x0,      0x0,      0x4},	// TSTORM_PCI_READ_TEST_ADDRESS_HI_OFFSET
	{   0x17b8,      0x0,      0x0,      0x0,      0x4},	// TSTORM_PCI_READ_TEST_RAM_ADDRESS_OFFSET
	{   0x17bc,      0x0,      0x0,      0x0,      0x8},	// TSTORM_PCI_READ_TEST_PCI_ENTITY_OFFSET
	{   0x17a8,      0x0,      0x0,      0x0,      0x8},	// TSTORM_TIME_SYNC_TEST_ADDRESS_OFFSET
	{   0x17d8,      0x0,      0x0,      0x0,      0x2},	// TSTORM_KUKU_NIG_PAUSE_TEST_MASK_OFFSET
	{  0x60000,     0x40,      0x0,      0x0,     0x40},	// CSTORM_STATUS_BLOCK_OFFSET(sbId)
	{   0xc000,     0x40,      0x0,      0x0,     0x40},	// CSTORM_STATUS_BLOCK_DATA_OFFSET(sbId)
	{   0xc02e,     0x40,      0x0,      0x0,      0x1},	// CSTORM_STATUS_BLOCK_DATA_STATE_OFFSET(sbId)
	{   0xc000,     0x40,      0x2,      0x0,      0x1},	// CSTORM_STATUS_BLOCK_DATA_TIMEOUT_OFFSET(sbId,hcIndex)
	{   0xc001,     0x40,      0x2,      0x0,      0x0},	// CSTORM_STATUS_BLOCK_DATA_FLAGS_OFFSET(sbId,hcIndex)
	{   0xe200,     0x20,      0x0,      0x0,     0x20},	// CSTORM_SYNC_BLOCK_OFFSET(sbId)
	{   0xe204,      0x2,      0x8,     0x20,      0x2},	// CSTORM_HC_SYNC_LINE_INDEX_E2_OFFSET(hcIndex,sbId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_HC_SYNC_LINE_INDEX_E1X_OFFSET(hcIndex,sbId)
	{   0xe200,      0x8,     0x20,      0x0,      0x4},	// CSTORM_HC_SYNC_LINE_DHC_OFFSET(sbSyncLines,sbId)
	{   0xf500,     0x28,      0x0,      0x0,     0x28},	// CSTORM_SP_STATUS_BLOCK_OFFSET(pfId)
	{   0xf640,     0x10,      0x0,      0x0,     0x10},	// CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(pfId)
	{   0xf64a,     0x10,      0x0,      0x0,      0x1},	// CSTORM_SP_STATUS_BLOCK_DATA_STATE_OFFSET(pfId)
	{   0xf6c0,     0x20,      0x0,      0x0,     0x20},	// CSTORM_SP_SYNC_BLOCK_OFFSET(pfId)
	{   0xf6c0,      0x2,     0x20,      0x0,      0x2},	// CSTORM_SP_HC_SYNC_LINE_INDEX_OFFSET(hcSpIndex,pfId)
	{   0xf300,     0x40,      0x0,      0x0,     0x40},	// CSTORM_DYNAMIC_HC_CONFIG_OFFSET(pfId)
	{   0x2008,     0x10,      0x0,      0x0,     0x10},	// CSTORM_ASSERT_LIST_OFFSET(assertListEntry)
	{   0x2000,      0x0,      0x0,      0x0,      0x8},	// CSTORM_ASSERT_LIST_INDEX_OFFSET
	{   0x11a8,      0x8,      0x0,      0x0,      0x1},	// CSTORM_FUNC_EN_OFFSET(funcId)
	{   0x11a9,      0x8,      0x0,      0x0,      0x1},	// CSTORM_VF_TO_PF_OFFSET(funcId)
	{   0x11aa,      0x8,      0x0,      0x0,      0x1},	// CSTORM_RECORD_SLOW_PATH_OFFSET(funcId)
	{   0x4000,     0x20,      0x4,      0x0,     0x10},	// CSTORM_BYTE_COUNTER_OFFSET(sbId,dhcIndex)
	{   0x5900,     0x30,     0x18,      0x0,     0x10},	// CSTORM_EVENT_RING_DATA_OFFSET(pfId)
	{   0x5908,     0x30,     0x18,      0x0,      0x2},	// CSTORM_EVENT_RING_PROD_OFFSET(pfId)
	{   0x5700,      0x8,      0x0,      0x0,      0x1},	// CSTORM_VF_PF_CHANNEL_STATE_OFFSET(vfId)
	{   0x5701,      0x8,      0x0,      0x0,      0x1},	// CSTORM_VF_PF_CHANNEL_VALID_OFFSET(vfId)
	{   0x1158,      0x0,      0x0,      0x0,      0x1},	// CSTORM_IGU_MODE_OFFSET
	{   0x1160,      0x0,      0x0,      0x0,     0x10},	// CSTORM_ERROR_HANDLER_STATISTICS_RAM_OFFSET
	{   0x11ac,      0x8,      0x0,      0x0,      0x4},	// CSTORM_FINAL_CLEANUP_COMPLETE_OFFSET(funcId)
	{   0x4000,     0x20,      0x0,      0x0,     0x20},	// CSTORM_QUEUE_ZONE_OFFSET(queueId)
	{   0x5300,     0x10,      0x0,      0x0,     0x10},	// CSTORM_VF_ZONE_OFFSET(vfId)
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// CSTORM_INBOUND_INTERRUPT_TEST_VF_INFO_SIZE_IN_BYTES
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// CSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_1_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// CSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_2_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// CSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_3_INDEX
	{   0x1470,      0x0,      0x0,      0x0,      0x1},	// CSTORM_KUKU_TEST_OPCODE_OFFSET
	{   0x14b0,      0x0,      0x0,      0x0,      0x8},	// CSTORM_KUKU_LOAD_CONTEXT_TEST_OFFSET
	{     0x50,      0x0,      0x0,      0x0,      0x0},	// CSTORM_KUKU_OP_GEN_VALUE
	{   0x1478,      0x0,      0x0,      0x0,      0x4},	// CSTORM_IGU_TEST_PF_ID_OFFSET
	{   0x147c,      0x0,      0x0,      0x0,      0x4},	// CSTORM_IGU_TEST_VF_ID_OFFSET
	{   0x1480,      0x0,      0x0,      0x0,      0x4},	// CSTORM_IGU_TEST_VF_VALID_OFFSET
	{   0x1484,      0x0,      0x0,      0x0,      0x4},	// CSTORM_IGU_TEST_ADDRESS_OFFSET
	{   0x1488,      0x0,      0x0,      0x0,      0x8},	// CSTORM_IGU_TEST_IGU_COMMAND_OFFSET
	{   0x2af0,     0x80,      0x0,      0x0,     0x80},	// USTORM_INDIRECTION_TABLE_OFFSET(portId)
	{      0x1,      0x0,      0x0,      0x0,      0x0},	// USTORM_INDIRECTION_TABLE_ENTRY_SIZE
	{   0x2008,     0x10,      0x0,      0x0,     0x10},	// USTORM_ASSERT_LIST_OFFSET(assertListEntry)
	{   0x2000,      0x0,      0x0,      0x0,      0x8},	// USTORM_ASSERT_LIST_INDEX_OFFSET
	{   0x2c70,      0x8,      0x0,      0x0,      0x1},	// USTORM_FUNC_EN_OFFSET(funcId)
	{   0x2c71,      0x8,      0x0,      0x0,      0x1},	// USTORM_VF_TO_PF_OFFSET(funcId)
	{   0x2c72,      0x8,      0x0,      0x0,      0x1},	// USTORM_RECORD_SLOW_PATH_OFFSET(funcId)
	{   0x4158,     0x38,      0x0,      0x0,     0x38},	// USTORM_PER_QUEUE_STATS_OFFSET(uStatQueueId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(pfId)
	{   0x2c44,      0x8,      0x0,      0x0,      0x2},	// USTORM_ETH_PAUSE_ENABLED_OFFSET(portId)
	{   0x2c46,      0x8,      0x0,      0x0,      0x2},	// USTORM_TOE_PAUSE_ENABLED_OFFSET(portId)
	{   0x2c54,     0x10,      0x0,      0x0,      0x4},	// USTORM_MAX_PAUSE_TIME_USEC_OFFSET(portId)
	{   0x2eb0,      0x0,      0x0,      0x0,     0x20},	// USTORM_ERROR_HANDLER_STATISTICS_RAM_OFFSET
	{   0x6000,     0x20,      0x0,      0x0,     0x20},	// USTORM_QUEUE_ZONE_OFFSET(queueId)
	{   0x7300,      0x8,      0x0,      0x0,      0x8},	// USTORM_VF_ZONE_OFFSET(vfId)
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// USTORM_INBOUND_INTERRUPT_TEST_VF_INFO_SIZE_IN_BYTES
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// USTORM_INBOUND_INTERRUPT_TEST_AGG_INT_1_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// USTORM_INBOUND_INTERRUPT_TEST_AGG_INT_2_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// USTORM_INBOUND_INTERRUPT_TEST_AGG_INT_3_INDEX
	{   0x2f58,      0x0,      0x0,      0x0,      0x1},	// USTORM_KUKU_TEST_OPCODE_OFFSET
	{   0x2f98,      0x0,      0x0,      0x0,      0x8},	// USTORM_KUKU_LOAD_CONTEXT_TEST_OFFSET
	{     0x50,      0x0,      0x0,      0x0,      0x0},	// USTORM_KUKU_OP_GEN_VALUE
	{   0x2f80,      0x0,      0x0,      0x0,      0x4},	// USTORM_PCI_READ_TEST_ADDRESS_LO_OFFSET
	{   0x2f84,      0x0,      0x0,      0x0,      0x4},	// USTORM_PCI_READ_TEST_ADDRESS_HI_OFFSET
	{   0x2f88,      0x0,      0x0,      0x0,      0x4},	// USTORM_PCI_READ_TEST_RAM_ADDRESS_OFFSET
	{   0x2f8c,      0x0,      0x0,      0x0,      0x8},	// USTORM_PCI_READ_TEST_PCI_ENTITY_OFFSET
	{   0x2fa8,      0x0,      0x0,      0x0,      0x2},	// USTORM_KUKU_NIG_PAUSE_TEST_MASK_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(pfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_MAC_FILTER_CONFIG_OFFSET(pfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_APPROXIMATE_MATCH_MULTICAST_FILTERING_OFFSET(pfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_ACCEPT_CLASSIFY_FAILED_OFFSET
	{   0x3128,      0x8,      0x0,      0x0,      0x1},	// TSTORM_ACCEPT_CLASSIFY_FAIL_E2_ENABLE_OFFSET(portId)
	{   0x3129,      0x8,      0x0,      0x0,      0x1},	// TSTORM_ACCEPT_CLASSIFY_FAIL_E2_VNIC_OFFSET(portId)
	{  0x62a20,   0x2600,     0x40,      0x0,      0x8},	// USTORM_CQE_PAGE_NEXT_OFFSET(portId,clientId)
	{   0xa000,      0x0,      0x0,      0x0,   0x2000},	// USTORM_AGG_DATA_OFFSET
	{   0x40c1,      0x0,      0x0,      0x0,      0x1},	// USTORM_TPA_BTR_OFFSET
	{   0x40f0,      0x0,      0x0,      0x0,      0x2},	// USTORM_ETH_DYNAMIC_HC_PARAM_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_RX_PRODS_E1X_OFFSET(portId,clientId)
	{   0x6000,     0x20,      0x0,      0x0,      0x8},	// USTORM_RX_PRODS_E2_OFFSET(qzoneId)
	{   0x4000,      0x8,      0x0,      0x0,      0x1},	// XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_ENABLED_OFFSET(portId)
	{   0x4001,      0x8,      0x0,      0x0,      0x1},	// XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_MAX_COUNT_OFFSET(portId)
	{   0x4040,      0x8,      0x4,      0x0,      0x2},	// XSTORM_TCP_IPID_OFFSET(pfId)
	{   0x4060,      0x8,      0x4,      0x0,      0x4},	// XSTORM_TCP_TX_SWS_TIMER_VAL_OFFSET(pfId)
	{   0x4080,      0x8,      0x0,      0x0,      0x4},	// XSTORM_TCP_TX_SWITCHING_EN_OFFSET(portId)
	{   0x4000,      0x8,      0x0,      0x0,      0x4},	// TSTORM_TCP_DUPLICATE_ACK_THRESHOLD_OFFSET(pfId)
	{   0x4004,      0x8,      0x0,      0x0,      0x4},	// TSTORM_TCP_MAX_CWND_OFFSET(pfId)
	{   0x4040,      0x0,      0x0,      0x0,      0x8},	// TSTORM_TCP_GLOBAL_PARAMS_OFFSET
	{   0x4048,      0x0,      0x0,      0x0,      0x8},	// TSTORM_TCP_ISLES_ARRAY_DESCRIPTOR_OFFSET
	{   0x8000,      0x0,      0x0,      0x0,     0x10},	// TSTORM_TCP_ISLES_ARRAY_OFFSET
	{   0x5040,      0x1,      0x4,      0x0,      0x1},	// XSTORM_TOE_LLC_SNAP_ENABLED_OFFSET(pfId)
	{   0x5000,      0x0,      0x0,      0x0,     0x20},	// XSTORM_OUT_OCTETS_OFFSET
	{   0x5008,     0x10,      0x0,      0x0,      0x4},	// TSTORM_TOE_MAX_SEG_RETRANSMIT_OFFSET(pfId)
	{   0x500c,     0x10,      0x0,      0x0,      0x1},	// TSTORM_TOE_DOUBT_REACHABILITY_OFFSET(pfId)
	{   0x52c7,      0x0,      0x0,      0x0,      0x1},	// TSTORM_TOE_MAX_DOMINANCE_VALUE_OFFSET
	{   0x52c6,      0x0,      0x0,      0x0,      0x1},	// TSTORM_TOE_DOMINANCE_THRESHOLD_OFFSET
	{   0x3000,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_CQ_CONS_PTR_LO_OFFSET(rssId,portId)
	{   0x3004,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_CQ_CONS_PTR_HI_OFFSET(rssId,portId)
	{   0x3008,     0x40,     0x20,      0x0,      0x2},	// CSTORM_TOE_CQ_PROD_OFFSET(rssId,portId)
	{   0x300a,     0x40,     0x20,      0x0,      0x2},	// CSTORM_TOE_CQ_CONS_OFFSET(rssId,portId)
	{   0x300c,     0x40,     0x20,      0x0,      0x1},	// CSTORM_TOE_CQ_NXT_PAGE_ADDR_VALID_OFFSET(rssId,portId)
	{   0x300d,     0x40,     0x20,      0x0,      0x1},	// CSTORM_TOE_STATUS_BLOCK_ID_OFFSET(rssId,portId)
	{   0x300e,     0x40,     0x20,      0x0,      0x1},	// CSTORM_TOE_STATUS_BLOCK_INDEX_OFFSET(rssId,portId)
	{   0x3010,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_CQ_NEXT_PAGE_BASE_ADDR_LO_OFFSET(rssId,portId)
	{   0x3014,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_CQ_NEXT_PAGE_BASE_ADDR_HI_OFFSET(rssId,portId)
	{   0x3018,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_DYNAMIC_HC_PROD_OFFSET(rssId,portId)
	{   0x301c,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_DYNAMIC_HC_CONS_OFFSET(rssId,portId)
	{   0xc000,    0x100,     0x80,      0x8,      0x4},	// USTORM_GRQ_CACHE_BD_LO_OFFSET(rssId,portId,grqBdId)
	{   0xc004,    0x100,     0x80,      0x8,      0x4},	// USTORM_GRQ_CACHE_BD_HI_OFFSET(rssId,portId,grqBdId)
	{      0xa,      0x0,      0x0,      0x0,      0x0},	// USTORM_TOE_GRQ_CACHE_NUM_BDS
	{   0xc068,    0x100,     0x80,      0x0,      0x1},	// USTORM_TOE_GRQ_LOCAL_PROD_OFFSET(rssId,portId)
	{   0xc069,    0x100,     0x80,      0x0,      0x1},	// USTORM_TOE_GRQ_LOCAL_CONS_OFFSET(rssId,portId)
	{   0xc06c,    0x100,     0x80,      0x0,      0x2},	// USTORM_TOE_GRQ_CONS_OFFSET(rssId,portId)
	{   0xc06e,    0x100,     0x80,      0x0,      0x2},	// USTORM_TOE_GRQ_PROD_OFFSET(rssId,portId)
	{   0xc070,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_GRQ_CONS_PTR_LO_OFFSET(rssId,portId)
	{   0xc074,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_GRQ_CONS_PTR_HI_OFFSET(rssId,portId)
	{   0xc066,    0x100,     0x80,      0x0,      0x2},	// USTORM_TOE_GRQ_BUF_SIZE_OFFSET(rssId,portId)
	{   0xc064,    0x100,     0x80,      0x0,      0x1},	// USTORM_TOE_CQ_NXT_PAGE_ADDR_VALID_OFFSET(rssId,portId)
	{   0xc060,    0x100,     0x80,      0x0,      0x2},	// USTORM_TOE_CQ_CONS_OFFSET(rssId,portId)
	{   0xc062,    0x100,     0x80,      0x0,      0x2},	// USTORM_TOE_CQ_PROD_OFFSET(rssId,portId)
	{   0xc050,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_CQ_NEXT_PAGE_BASE_ADDR_LO_OFFSET(rssId,portId)
	{   0xc054,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_CQ_NEXT_PAGE_BASE_ADDR_HI_OFFSET(rssId,portId)
	{   0xc058,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_CQ_CONS_PTR_LO_OFFSET(rssId,portId)
	{   0xc05c,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_CQ_CONS_PTR_HI_OFFSET(rssId,portId)
	{   0xc07c,    0x100,     0x80,      0x0,      0x1},	// USTORM_TOE_STATUS_BLOCK_ID_OFFSET(rssId,portId)
	{   0xc07d,    0x100,     0x80,      0x0,      0x1},	// USTORM_TOE_STATUS_BLOCK_INDEX_OFFSET(rssId,portId)
	{   0x1018,     0x10,      0x0,      0x0,      0x4},	// USTORM_TOE_TCP_PUSH_TIMER_TICKS_OFFSET(pfId)
	{   0x1090,     0x10,      0x0,      0x0,      0x4},	// USTORM_TOE_GRQ_XOFF_COUNTER_OFFSET(pfId)
	{   0x1098,     0x10,      0x0,      0x0,      0x4},	// USTORM_TOE_RCQ_XOFF_COUNTER_OFFSET(pfId)
	{   0x1110,      0x0,      0x0,      0x0,      0x2},	// USTORM_TOE_CQ_THR_LOW_OFFSET
	{   0x1112,      0x0,      0x0,      0x0,      0x2},	// USTORM_TOE_GRQ_THR_LOW_OFFSET
	{   0x1114,      0x0,      0x0,      0x0,      0x2},	// USTORM_TOE_CQ_THR_HIGH_OFFSET
	{   0x1116,      0x0,      0x0,      0x0,      0x2},	// USTORM_TOE_GRQ_THR_HIGH_OFFSET
	{   0x6040,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(pfId)
	{   0x6042,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_TCP_VARS_LSB_LOCAL_MAC_ADDR_OFFSET(pfId)
	{   0x6044,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_TCP_VARS_MID_LOCAL_MAC_ADDR_OFFSET(pfId)
	{   0x6046,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_TCP_VARS_MSB_LOCAL_MAC_ADDR_OFFSET(pfId)
	{   0x6080,      0x8,      0x0,      0x0,      0x8},	// TSTORM_ISCSI_RQ_SIZE_OFFSET(pfId)
	{   0x6000,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId)
	{   0x6002,      0x8,      0x0,      0x0,      0x1},	// TSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId)
	{   0x6004,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId)
	{   0x60c0,      0x8,      0x0,      0x0,      0x8},	// TSTORM_ISCSI_ERROR_BITMAP_OFFSET(pfId)
	{   0x6100,      0x8,      0x0,      0x0,      0x4},	// TSTORM_ISCSI_L2_ISCSI_OOO_CID_TABLE_OFFSET(pfId)
	{   0x6104,      0x8,      0x0,      0x0,      0x1},	// TSTORM_ISCSI_L2_ISCSI_OOO_CLIENT_ID_TABLE_OFFSET(pfId)
	{   0x6140,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_L2_ISCSI_OOO_PROD_OFFSET(pfId)
	{   0x6144,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_L2_ISCSI_OOO_RX_BDS_THRSHLD_OFFSET(pfId)
	{   0x6142,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_L2_ISCSI_OOO_CONS_OFFSET(pfId)
	{   0x6180,      0x8,      0x0,      0x0,      0x4},	// TSTORM_ISCSI_TCP_LOCAL_ADV_WND_OFFSET(pfId)
	{   0x3000,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_PAGE_SIZE_OFFSET(pfId)
	{   0x3002,      0x8,      0x0,      0x0,      0x1},	// USTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId)
	{   0x3004,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId)
	{   0x3040,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_R2TQ_SIZE_OFFSET(pfId)
	{   0x3044,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_CQ_SIZE_OFFSET(pfId)
	{   0x3046,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_CQ_SQN_SIZE_OFFSET(pfId)
	{   0x3660,      0x8,      0x0,      0x0,      0x8},	// USTORM_ISCSI_GLOBAL_BUF_PHYS_ADDR_OFFSET(pfId)
	{   0x3080,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_RQ_BUFFER_SIZE_OFFSET(pfId)
	{   0x3084,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_RQ_SIZE_OFFSET(pfId)
	{   0x36a0,      0x8,      0x0,      0x0,      0x8},	// USTORM_ISCSI_ERROR_BITMAP_OFFSET(pfId)
	{   0x8040,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_TCP_VARS_TTL_OFFSET(pfId)
	{   0x8041,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_TCP_VARS_TOS_OFFSET(pfId)
	{   0x8042,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(pfId)
	{   0x8043,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_TCP_VARS_ADV_WND_SCL_OFFSET(pfId)
	{   0x8000,      0x8,      0x0,      0x0,      0x2},	// XSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId)
	{   0x8002,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId)
	{   0x8004,      0x8,      0x0,      0x0,      0x2},	// XSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId)
	{   0x80c0,      0x8,      0x0,      0x0,      0x2},	// XSTORM_ISCSI_HQ_SIZE_OFFSET(pfId)
	{   0x80c2,      0x8,      0x0,      0x0,      0x2},	// XSTORM_ISCSI_SQ_SIZE_OFFSET(pfId)
	{   0x80c4,      0x8,      0x0,      0x0,      0x2},	// XSTORM_ISCSI_R2TQ_SIZE_OFFSET(pfId)
	{   0x8080,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR0_OFFSET(pfId)
	{   0x8081,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR1_OFFSET(pfId)
	{   0x8082,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR2_OFFSET(pfId)
	{   0x8083,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR3_OFFSET(pfId)
	{   0x8084,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR4_OFFSET(pfId)
	{   0x8085,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR5_OFFSET(pfId)
	{   0x8086,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_VLAN_OFFSET(pfId)
	{   0x6000,      0x8,      0x0,      0x0,      0x2},	// CSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId)
	{   0x6002,      0x8,      0x0,      0x0,      0x1},	// CSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId)
	{   0x6004,      0x8,      0x0,      0x0,      0x2},	// CSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId)
	{   0x6042,     0xc0,     0x18,      0x0,      0x2},	// CSTORM_ISCSI_EQ_PROD_OFFSET(pfId,iscsiEqId)
	{   0x6040,     0xc0,     0x18,      0x0,      0x2},	// CSTORM_ISCSI_EQ_CONS_OFFSET(pfId,iscsiEqId)
	{   0x604c,     0xc0,     0x18,      0x0,      0x8},	// CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_OFFSET(pfId,iscsiEqId)
	{   0x6044,     0xc0,     0x18,      0x0,      0x8},	// CSTORM_ISCSI_EQ_NEXT_EQE_ADDR_OFFSET(pfId,iscsiEqId)
	{   0x6057,     0xc0,     0x18,      0x0,      0x1},	// CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_VALID_OFFSET(pfId,iscsiEqId)
	{   0x6054,     0xc0,     0x18,      0x0,      0x2},	// CSTORM_ISCSI_EQ_SB_NUM_OFFSET(pfId,iscsiEqId)
	{   0x6056,     0xc0,     0x18,      0x0,      0x1},	// CSTORM_ISCSI_EQ_SB_INDEX_OFFSET(pfId,iscsiEqId)
	{   0x6640,      0x8,      0x0,      0x0,      0x8},	// CSTORM_ISCSI_HQ_SIZE_OFFSET(pfId)
	{   0x6680,      0x8,      0x0,      0x0,      0x8},	// CSTORM_ISCSI_CQ_SIZE_OFFSET(pfId)
	{   0x66c0,      0x8,      0x0,      0x0,      0x8},	// CSTORM_ISCSI_CQ_SQN_SIZE_OFFSET(pfId)
	{   0xda82,     0x18,      0x0,      0x0,      0x2},	// USTORM_FCOE_EQ_PROD_OFFSET(pfId)
	{   0xdba0,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_TIMER_PARAM_OFFSET
	{   0xe000,      0x0,      0x0,      0x0,      0x4},	// USTORM_TIMER_ARRAY_OFFSET
	{   0xd100,      0x0,      0x0,      0x0,      0x4},	// USTORM_STAT_FC_CRC_CNT_OFFSET
	{   0xd104,      0x0,      0x0,      0x0,      0x4},	// USTORM_STAT_EOFA_DEL_CNT_OFFSET
	{   0xd108,      0x0,      0x0,      0x0,      0x4},	// USTORM_STAT_MISS_FRAME_CNT_OFFSET
	{   0xd10c,      0x0,      0x0,      0x0,      0x4},	// USTORM_STAT_SEQ_TIMEOUT_CNT_OFFSET
	{   0xd110,      0x0,      0x0,      0x0,      0x4},	// USTORM_STAT_DROP_SEQ_CNT_OFFSET
	{   0xd114,      0x0,      0x0,      0x0,      0x4},	// USTORM_STAT_FCOE_RX_DROP_PKT_CNT_OFFSET
	{   0xd118,      0x0,      0x0,      0x0,      0x4},	// USTORM_STAT_FCP_RX_PKT_CNT_OFFSET
	{   0xd100,      0x0,      0x0,      0x0,     0x20},	// USTORM_STAT_OFFSET
	{   0x9280,      0x0,      0x0,      0x0,      0x4},	// USTORM_DEBUG_DROP_PKT_CNT_OFFSET
	{   0x9280,      0x0,      0x0,      0x0,     0x28},	// USTORM_DEBUG_OFFSET
	{   0x8050,     0xa8,      0x0,      0x0,      0x1},	// USTORM_CACHED_TCE_MNG_INFO_DWORD_ONE_OFFSET(cached_tbl_size)
	{   0x8054,     0xa8,      0x0,      0x0,      0x1},	// USTORM_CACHED_TCE_MNG_INFO_DWORD_TWO_OFFSET(cached_tbl_size)
	{   0x8000,      0x0,      0x0,      0x0,     0x50},	// USTORM_CACHED_TCE_ENTRY_TCE_OFFSET
	{   0x8050,      0x0,      0x0,      0x0,     0x10},	// USTORM_CACHED_TCE_ENTRY_MNG_INFO_OFFSET
	{   0x9600,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_CACHED_TCE_TBL_BIT_MAP_OFFSET
	{   0x9400,      0x0,      0x0,      0x0,      0x4},	// USTORM_DEBUG_CACHED_TCE_WAIT_4_BD_READ_OFFSET
	{   0x9404,      0x0,      0x0,      0x0,      0x4},	// USTORM_DEBUG_CACHED_TCE_WAKE_ANOTHER_THREAD_DATA_OFFSET
	{   0x9408,      0x0,      0x0,      0x0,      0x4},	// USTORM_DEBUG_CACHED_TCE_WAKE_ANOTHER_THREAD_NON_DATA_OFFSET
	{   0x940c,      0x0,      0x0,      0x0,      0x4},	// USTORM_DEBUG_CACHED_TCE_WAKE_ANOTHER_THREAD_ERR_OFFSET
	{   0x9410,      0x0,      0x0,      0x0,      0x4},	// USTORM_DEBUG_CACHED_TCE_GLOBAL_TIMER_TASK_IN_USE_OFFSET
	{   0x9414,      0x0,      0x0,      0x0,      0x4},	// USTORM_DEBUG_CACHED_TCE_DEL_CACHED_TASK_OFFSET
	{   0x9418,      0x0,      0x0,      0x0,      0x4},	// USTORM_DEBUG_CACHED_TCE_SILENT_DROP_CACHED_TASK_OFFSET
	{   0x9400,      0x0,      0x0,      0x0,     0x40},	// USTORM_DEBUG_CACHED_TCE_OFFSET
	{   0x9420,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_CACHED_TCE_SEQ_CNT_ON_DROP_OFFSET
	{   0x9424,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_CACHED_TCE_SEQ_CNT_ON_CRC_ERROR_OFFSET
	{   0x9428,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_CACHED_TCE_SEQ_CNT_ON_ERROR_OFFSET
	{   0x941c,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_CACHED_TCE_PREVIOUS_THREAD_OFFSET
	{   0x9430,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_CACHED_TCE_CRC_ERR_DETECT_DATA_IN_OFFSET
	{   0x942c,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_CACHED_TCE_CRC_ERR_DETECT_READ_TCE_OFFSET
	{   0x9434,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_CACHED_TCE_CRC_ERR_DETECT_DROP_ERR_OFFSET
	{   0x9284,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_PARAMS_ERRORS_NUMBER_OFFSET
	{   0x9280,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_PARAMS_SILENT_DROP_NUMBER_OFFSET
	{   0x9290,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_PARAMS_SILENT_DROP_BITMAP_OFFSET
	{   0x92a4,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_PARAMS_ENABLE_CONN_RACE_OFFSET
	{   0x9438,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_PARAMS_TASK_IN_USE_OFFSET
	{   0x943c,      0x0,      0x0,      0x0,      0x4},	// USTORM_FCOE_DEBUG_PARAMS_CRC_ERROR_TASK_IN_USE_OFFSET
	{   0xb988,      0x0,      0x0,      0x0,      0x0},	// XSTORM_FCOE_TIMER_PARAM_OFFSET
	{   0xd000,      0x0,      0x0,      0x0,      0x4},	// XSTORM_TIMER_ARRAY_OFFSET
	{   0xb100,      0x0,      0x0,      0x0,      0x4},	// XSTORM_STAT_FCOE_TX_PKT_CNT_OFFSET
	{   0xb104,      0x0,      0x0,      0x0,      0x4},	// XSTORM_STAT_FCOE_TX_BYTE_CNT_OFFSET
	{   0xb108,      0x0,      0x0,      0x0,      0x4},	// XSTORM_STAT_FCP_TX_PKT_CNT_OFFSET
	{   0xb100,      0x0,      0x0,      0x0,     0x10},	// XSTORM_STAT_OFFSET
	{   0xbcb0,      0x0,      0x0,      0x0,      0x4},	// XSTORM_DEBUG_ABTS_BLOCK_SQ_CNT_OFFSET
	{   0xbcb4,      0x0,      0x0,      0x0,      0x4},	// XSTORM_DEBUG_CLEANUP_BLOCK_SQ_CNT_OFFSET
	{   0xbcb0,      0x0,      0x0,      0x0,     0x48},	// XSTORM_DEBUG_OFFSET
	{   0xd868,      0x0,      0x0,      0x0,      0x4},	// TSTORM_STAT_FCOE_VER_CNT_OFFSET
	{   0xd860,      0x0,      0x0,      0x0,      0x4},	// TSTORM_STAT_FCOE_RX_PKT_CNT_OFFSET
	{   0xd864,      0x0,      0x0,      0x0,      0x4},	// TSTORM_STAT_FCOE_RX_BYTE_CNT_OFFSET
	{   0xd86c,      0x0,      0x0,      0x0,      0x4},	// TSTORM_STAT_FCOE_RX_DROP_PKT_CNT_OFFSET
	{   0xd860,      0x0,      0x0,      0x0,     0x10},	// TSTORM_STAT_OFFSET
	{   0xd850,      0x0,      0x0,      0x0,      0x4},	// TSTORM_PORT_DEBUG_WAIT_FOR_YOUR_TURN_SP_CNT_OFFSET
	{   0xd854,      0x0,      0x0,      0x0,      0x4},	// TSTORM_PORT_DEBUG_AFEX_ERROR_PACKETS_OFFSET
	{   0xd850,      0x0,      0x0,      0x0,      0x8},	// TSTORM_PORT_DEBUG_OFFSET
	{   0xd4c8,      0x0,      0x0,      0x0,      0x8},	// TSTORM_REORDER_DATA_OFFSET
	{   0xd4d8,      0x0,      0x0,      0x0,     0x80},	// TSTORM_REORDER_WAITING_TABLE_OFFSET
	{     0x10,      0x0,      0x0,      0x0,      0x0},	// TSTORM_WAITING_LIST_SIZE
	{   0xd4d8,      0x0,      0x0,      0x0,      0x8},	// TSTORM_REORDER_WAITING_ENTRY_OFFSET
};
