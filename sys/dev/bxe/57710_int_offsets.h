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


static const struct iro e1_iro_arr[385] = {
	{     0x40,      0x0,      0x0,      0x0,      0x0},	// COMMON_SB_SIZE
	{     0x40,      0x0,      0x0,      0x0,      0x0},	// COMMON_SB_DATA_SIZE
	{     0x28,      0x0,      0x0,      0x0,      0x0},	// COMMON_SP_SB_SIZE
	{     0x10,      0x0,      0x0,      0x0,      0x0},	// COMMON_SP_SB_DATA_SIZE
	{     0x40,      0x0,      0x0,      0x0,      0x0},	// COMMON_DYNAMIC_HC_CONFIG_SIZE
	{     0x10,      0x0,      0x0,      0x0,      0x0},	// COMMON_ASM_ASSERT_MSG_SIZE
	{      0x8,      0x0,      0x0,      0x0,      0x0},	// COMMON_ASM_ASSERT_INDEX_SIZE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_ASM_INVALID_ASSERT_OPCODE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_RAM1_TEST_EVENT_ID
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_EVENT_ID
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_1_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_2_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_3_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_1_RESULT_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_2_RESULT_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_3_RESULT_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_1_MASK
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_2_MASK
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_INBOUND_INTERRUPT_TEST_AGG_INT_3_MASK
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_TEST_AGG_INT
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_TEST_EVENTID
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_PCI_READ_OPCODE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_LOAD_CONTEXT_OPCODE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_LOAD_CONTEXT_INCVAL
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_LOAD_CONTEXT_REGION
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_LOAD_CONTEXT_CID
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_LOAD_CONTEXT_RUN_PBF_ECHO_TEST
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_QM_PAUSE_OPCODE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_TEST_UNUSED_FOCS_SUCCESS_OPCODE_VALUE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// COMMON_KUKU_TEST_UNUSED_FOCS_OPCODE_VALUE
	{   0x3320,     0x10,      0x0,      0x0,      0x8},	// XSTORM_SPQ_PAGE_BASE_OFFSET(funcId)
	{   0x3328,     0x10,      0x0,      0x0,      0x2},	// XSTORM_SPQ_PROD_OFFSET(funcId)
	{   0x3320,     0x10,      0x0,      0x0,     0x10},	// XSTORM_SPQ_DATA_OFFSET(funcId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_HIGIG_HDR_LENGTH_OFFSET(portId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_VF_SPQ_PAGE_BASE_OFFSET(vfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_VF_SPQ_PROD_OFFSET(vfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_VF_SPQ_DATA_OFFSET(vfId)
	{   0x3358,      0x1,      0x4,      0x0,      0x1},	// XSTORM_JUMBO_SUPPORT_OFFSET(pfId)
	{   0x3360,      0x0,      0x0,      0x0,      0x2},	// XSTORM_COMMON_IP_ID_MASK_OFFSET
	{   0x3368,      0x0,      0x0,      0x0,      0x8},	// XSTORM_COMMON_RTC_PARAMS_OFFSET
	{   0x336c,      0x0,      0x0,      0x0,      0x2},	// XSTORM_COMMON_RTC_RESOLUTION_OFFSET
	{   0x3920,      0x0,      0x0,      0x0,      0x8},	// XSTORM_FW_VERSION_OFFSET
	{   0x3af8,     0x40,      0x0,      0x0,     0x40},	// XSTORM_LICENSE_VALUES_OFFSET(pfId)
	{   0x3938,     0x80,      0x0,      0x0,     0x48},	// XSTORM_CMNG_PER_PORT_VARS_OFFSET(portId)
	{   0x3a38,     0x40,      0x0,      0x0,      0x8},	// XSTORM_RATE_SHAPING_PER_VN_VARS_OFFSET(pfId)
	{   0x3a48,     0x40,      0x0,      0x0,     0x18},	// XSTORM_FAIRNESS_PER_VN_VARS_OFFSET(pfId)
	{   0x3370,     0x28,      0x0,      0x0,     0x28},	// XSTORM_PER_QUEUE_STATS_OFFSET(xStatQueueId)
	{   0x3c20,      0x8,      0x0,      0x0,      0x1},	// XSTORM_FUNC_EN_OFFSET(funcId)
	{   0x3c21,      0x8,      0x0,      0x0,      0x1},	// XSTORM_VF_TO_PF_OFFSET(funcId)
	{   0x3c22,      0x8,      0x0,      0x0,      0x1},	// XSTORM_RECORD_SLOW_PATH_OFFSET(funcId)
	{   0x2008,     0x10,      0x0,      0x0,     0x10},	// XSTORM_ASSERT_LIST_OFFSET(assertListEntry)
	{   0x2000,      0x0,      0x0,      0x0,      0x8},	// XSTORM_ASSERT_LIST_INDEX_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_TIME_SYNC_TEST_ADDRESS_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// PCI_READ_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// LOAD_CONTEXT_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// QM_PAUSE_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// PAUSE_TEST_XOFF_PORT0_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// PAUSE_TEST_XON_PORT0_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// PAUSE_TEST_XOFF_PORT1_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// PAUSE_TEST_XON_PORT1_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TEST_UNUSED_FOCS_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// PBF_ECHO_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TIME_SYNC_PORT0_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TIME_SYNC_PORT1_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// IGU_TEST_KUKUE_CODE_OPPCOE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_AGG_INT_INITIAL_CLEANUP_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_AGG_INT_FINAL_CLEANUP_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_AGG_INT_FINAL_CLEANUP_COMP_TYPE
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_ERROR_HANDLER_STATISTICS_RAM_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_LB_PHYSICAL_QUEUES_INFO_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_QUEUE_ZONE_OFFSET(queueId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_VF_ZONE_OFFSET(vfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_FIVE_TUPLE_SRC_EN_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_E2_INTEG_RAM_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_QM_OPPORTUNISTIC_RAM_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_SIDE_INFO_INPUT_LSB_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_E2_INTEG_VLAN_ID_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_E2_INTEG_VLAN_ID_EN_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_VFC_TEST_LINE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_VFC_TEST_RESULT_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_VFC_OP_GEN_VALUE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_INBOUND_INTERRUPT_TEST_VF_INFO_SIZE_IN_BYTES
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_1_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_2_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_3_INDEX
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_DPM_BUFFER_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_TEST_OPCODE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_LOAD_CONTEXT_TEST_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_OP_GEN_VALUE
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_QM_PAUSE_TEST_QUEUE_MASK_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_QM_PAUSE_TEST_GROUP_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_QM_PAUSE_TEST_PORT_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_OPCODE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_INCVAL
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_REGION
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_RUN_PBF_ECHO_TEST
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_CID
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_PBF_ECHO_SUCCESS_VALUE
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_KUKU_TIME_SYNC_FLG_OFFSET(funcId)
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_INDIRECTION_TABLE_ENTRY_SIZE
	{   0x19c8,      0x0,      0x0,      0x0,      0x8},	// TSTORM_COMMON_RTC_PARAMS_OFFSET
	{   0x2008,     0x10,      0x0,      0x0,     0x10},	// TSTORM_ASSERT_LIST_OFFSET(assertListEntry)
	{   0x2000,      0x0,      0x0,      0x0,      0x8},	// TSTORM_ASSERT_LIST_INDEX_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_MEASURE_PCI_LATENCY_CTRL_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_MEASURE_PCI_LATENCY_DATA_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_AGG_MEASURE_PCI_LATENCY_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_AGG_MEASURE_PCI_LATENCY_COMP_TYPE
	{   0x4870,      0x8,      0x0,      0x0,      0x1},	// TSTORM_FUNC_EN_OFFSET(funcId)
	{   0x4871,      0x8,      0x0,      0x0,      0x1},	// TSTORM_VF_TO_PF_OFFSET(funcId)
	{   0x4872,      0x8,      0x0,      0x0,      0x1},	// TSTORM_RECORD_SLOW_PATH_OFFSET(funcId)
	{   0x4040,     0x38,      0x0,      0x0,     0x38},	// TSTORM_PER_QUEUE_STATS_OFFSET(tStatQueueId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_COMMON_SAFC_WORKAROUND_ENABLE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_COMMON_SAFC_WORKAROUND_TIMEOUT_10USEC_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_ERROR_HANDLER_STATISTICS_RAM_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_VFC_TEST_RSS_KEY_OFFSET(portId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_QUEUE_ZONE_OFFSET(queueId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_VF_ZONE_OFFSET(vfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_E2_INTEG_RAM_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_LSB_SIDE_BAND_INFO_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_MSB_SIDE_BAND_INFO_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_VFC_TEST_LINE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_VFC_TEST_RESULT_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_VFC_OP_GEN_VALUE
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_INBOUND_INTERRUPT_TEST_VF_INFO_SIZE_IN_BYTES
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_1_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_2_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_3_INDEX
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_KUKU_TEST_OPCODE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_KUKU_LOAD_CONTEXT_TEST_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_KUKU_OP_GEN_VALUE
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_PCI_READ_TEST_ADDRESS_LO_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_PCI_READ_TEST_ADDRESS_HI_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_PCI_READ_TEST_RAM_ADDRESS_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_PCI_READ_TEST_PCI_ENTITY_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_TIME_SYNC_TEST_ADDRESS_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_KUKU_NIG_PAUSE_TEST_MASK_OFFSET
	{   0x4000,     0x40,      0x0,      0x0,     0x40},	// CSTORM_STATUS_BLOCK_OFFSET(sbId)
	{   0x4800,     0x40,      0x0,      0x0,     0x40},	// CSTORM_STATUS_BLOCK_DATA_OFFSET(sbId)
	{   0x482e,     0x40,      0x0,      0x0,      0x1},	// CSTORM_STATUS_BLOCK_DATA_STATE_OFFSET(sbId)
	{   0x4800,     0x40,      0x2,      0x0,      0x1},	// CSTORM_STATUS_BLOCK_DATA_TIMEOUT_OFFSET(sbId,hcIndex)
	{   0x4801,     0x40,      0x2,      0x0,      0x0},	// CSTORM_STATUS_BLOCK_DATA_FLAGS_OFFSET(sbId,hcIndex)
	{   0x3000,     0x40,      0x0,      0x0,     0x40},	// CSTORM_SYNC_BLOCK_OFFSET(sbId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_HC_SYNC_LINE_INDEX_E2_OFFSET(hcIndex,sbId)
	{   0x3000,      0x8,     0x40,      0x0,      0x4},	// CSTORM_HC_SYNC_LINE_INDEX_E1X_OFFSET(hcIndex,sbId)
	{   0x3004,      0x8,     0x40,      0x0,      0x4},	// CSTORM_HC_SYNC_LINE_DHC_OFFSET(sbSyncLines,sbId)
	{   0x3b80,     0x28,      0x0,      0x0,     0x28},	// CSTORM_SP_STATUS_BLOCK_OFFSET(pfId)
	{   0x3bd0,     0x10,      0x0,      0x0,     0x10},	// CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(pfId)
	{   0x3bda,     0x10,      0x0,      0x0,      0x1},	// CSTORM_SP_STATUS_BLOCK_DATA_STATE_OFFSET(pfId)
	{   0x3800,     0x80,      0x0,      0x0,     0x80},	// CSTORM_SP_SYNC_BLOCK_OFFSET(pfId)
	{   0x3800,      0x8,     0x80,      0x0,      0x2},	// CSTORM_SP_HC_SYNC_LINE_INDEX_OFFSET(hcSpIndex,pfId)
	{   0x3900,     0x40,      0x0,      0x0,     0x40},	// CSTORM_DYNAMIC_HC_CONFIG_OFFSET(pfId)
	{   0x2008,     0x10,      0x0,      0x0,     0x10},	// CSTORM_ASSERT_LIST_OFFSET(assertListEntry)
	{   0x2000,      0x0,      0x0,      0x0,      0x8},	// CSTORM_ASSERT_LIST_INDEX_OFFSET
	{   0x5198,      0x8,      0x0,      0x0,      0x1},	// CSTORM_FUNC_EN_OFFSET(funcId)
	{   0x5199,      0x8,      0x0,      0x0,      0x1},	// CSTORM_VF_TO_PF_OFFSET(funcId)
	{   0x519a,      0x8,      0x0,      0x0,      0x1},	// CSTORM_RECORD_SLOW_PATH_OFFSET(funcId)
	{   0x3980,     0x10,      0x4,      0x0,      0x4},	// CSTORM_BYTE_COUNTER_OFFSET(sbId,dhcIndex)
	{   0x51a8,     0x30,     0x18,      0x0,     0x10},	// CSTORM_EVENT_RING_DATA_OFFSET(pfId)
	{   0x51b0,     0x30,     0x18,      0x0,      0x2},	// CSTORM_EVENT_RING_PROD_OFFSET(pfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_VF_PF_CHANNEL_STATE_OFFSET(vfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_VF_PF_CHANNEL_VALID_OFFSET(vfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_IGU_MODE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_ERROR_HANDLER_STATISTICS_RAM_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_FINAL_CLEANUP_COMPLETE_OFFSET(funcId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_QUEUE_ZONE_OFFSET(queueId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_VF_ZONE_OFFSET(vfId)
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// CSTORM_INBOUND_INTERRUPT_TEST_VF_INFO_SIZE_IN_BYTES
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// CSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_1_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// CSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_2_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// CSTORM_INBOUND_INTERRUPT_TEST_AGG_INT_3_INDEX
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_KUKU_TEST_OPCODE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_KUKU_LOAD_CONTEXT_TEST_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// CSTORM_KUKU_OP_GEN_VALUE
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_IGU_TEST_PF_ID_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_IGU_TEST_VF_ID_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_IGU_TEST_VF_VALID_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_IGU_TEST_ADDRESS_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// CSTORM_IGU_TEST_IGU_COMMAND_OFFSET
	{   0x23e8,     0x80,      0x0,      0x0,     0x80},	// USTORM_INDIRECTION_TABLE_OFFSET(portId)
	{      0x1,      0x0,      0x0,      0x0,      0x0},	// USTORM_INDIRECTION_TABLE_ENTRY_SIZE
	{   0x2008,     0x10,      0x0,      0x0,     0x10},	// USTORM_ASSERT_LIST_OFFSET(assertListEntry)
	{   0x2000,      0x0,      0x0,      0x0,      0x8},	// USTORM_ASSERT_LIST_INDEX_OFFSET
	{   0x2e70,      0x8,      0x0,      0x0,      0x1},	// USTORM_FUNC_EN_OFFSET(funcId)
	{   0x2e71,      0x8,      0x0,      0x0,      0x1},	// USTORM_VF_TO_PF_OFFSET(funcId)
	{   0x2e72,      0x8,      0x0,      0x0,      0x1},	// USTORM_RECORD_SLOW_PATH_OFFSET(funcId)
	{   0x24e8,     0x38,      0x0,      0x0,     0x38},	// USTORM_PER_QUEUE_STATS_OFFSET(uStatQueueId)
	{   0x2dd0,      0x8,      0x0,      0x0,      0x8},	// USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(pfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_ETH_PAUSE_ENABLED_OFFSET(portId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_TOE_PAUSE_ENABLED_OFFSET(portId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_MAX_PAUSE_TIME_USEC_OFFSET(portId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_ERROR_HANDLER_STATISTICS_RAM_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_QUEUE_ZONE_OFFSET(queueId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_VF_ZONE_OFFSET(vfId)
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// USTORM_INBOUND_INTERRUPT_TEST_VF_INFO_SIZE_IN_BYTES
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// USTORM_INBOUND_INTERRUPT_TEST_AGG_INT_1_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// USTORM_INBOUND_INTERRUPT_TEST_AGG_INT_2_INDEX
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// USTORM_INBOUND_INTERRUPT_TEST_AGG_INT_3_INDEX
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_KUKU_TEST_OPCODE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_KUKU_LOAD_CONTEXT_TEST_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// USTORM_KUKU_OP_GEN_VALUE
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_PCI_READ_TEST_ADDRESS_LO_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_PCI_READ_TEST_ADDRESS_HI_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_PCI_READ_TEST_RAM_ADDRESS_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_PCI_READ_TEST_PCI_ENTITY_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_KUKU_NIG_PAUSE_TEST_MASK_OFFSET
	{   0x2500,     0x40,      0x0,      0x0,      0x8},	// TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(pfId)
	{   0x2508,     0x40,      0x0,      0x0,     0x20},	// TSTORM_MAC_FILTER_CONFIG_OFFSET(pfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_APPROXIMATE_MATCH_MULTICAST_FILTERING_OFFSET(pfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_ACCEPT_CLASSIFY_FAILED_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_ACCEPT_CLASSIFY_FAIL_E2_ENABLE_OFFSET(portId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_ACCEPT_CLASSIFY_FAIL_E2_VNIC_OFFSET(portId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_CQE_PAGE_NEXT_OFFSET(portId,clientId)
	{   0x3000,      0x0,      0x0,      0x0,   0x1000},	// USTORM_AGG_DATA_OFFSET
	{   0x50a1,      0x0,      0x0,      0x0,      0x1},	// USTORM_TPA_BTR_OFFSET
	{   0x50b8,      0x0,      0x0,      0x0,      0x2},	// USTORM_ETH_DYNAMIC_HC_PARAM_OFFSET
	{   0x50c8,     0x90,      0x8,      0x0,      0x8},	// USTORM_RX_PRODS_E1X_OFFSET(portId,clientId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_RX_PRODS_E2_OFFSET(qzoneId)
	{   0x2960,      0x8,      0x0,      0x0,      0x1},	// XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_ENABLED_OFFSET(portId)
	{   0x2961,      0x8,      0x0,      0x0,      0x1},	// XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_MAX_COUNT_OFFSET(portId)
	{   0x2970,      0x8,      0x4,      0x0,      0x2},	// XSTORM_TCP_IPID_OFFSET(pfId)
	{   0x2978,      0x8,      0x4,      0x0,      0x4},	// XSTORM_TCP_TX_SWS_TIMER_VAL_OFFSET(pfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_TCP_TX_SWITCHING_EN_OFFSET(portId)
	{   0x2fb0,      0x8,      0x0,      0x0,      0x4},	// TSTORM_TCP_DUPLICATE_ACK_THRESHOLD_OFFSET(pfId)
	{   0x2fb4,      0x8,      0x0,      0x0,      0x4},	// TSTORM_TCP_MAX_CWND_OFFSET(pfId)
	{   0x2fc0,      0x0,      0x0,      0x0,      0x8},	// TSTORM_TCP_GLOBAL_PARAMS_OFFSET
	{   0x2fc8,      0x0,      0x0,      0x0,      0x8},	// TSTORM_TCP_ISLES_ARRAY_DESCRIPTOR_OFFSET
	{   0x3000,      0x0,      0x0,      0x0,     0x10},	// TSTORM_TCP_ISLES_ARRAY_OFFSET
	{   0x5040,      0x1,      0x1,      0x0,      0x1},	// XSTORM_TOE_LLC_SNAP_ENABLED_OFFSET(pfId)
	{   0x5000,      0x0,      0x0,      0x0,     0x20},	// XSTORM_OUT_OCTETS_OFFSET
	{    0x808,     0x10,      0x0,      0x0,      0x4},	// TSTORM_TOE_MAX_SEG_RETRANSMIT_OFFSET(pfId)
	{    0x80c,     0x10,      0x0,      0x0,      0x1},	// TSTORM_TOE_DOUBT_REACHABILITY_OFFSET(pfId)
	{    0x8b7,      0x0,      0x0,      0x0,      0x1},	// TSTORM_TOE_MAX_DOMINANCE_VALUE_OFFSET
	{    0x8b6,      0x0,      0x0,      0x0,      0x1},	// TSTORM_TOE_DOMINANCE_THRESHOLD_OFFSET
	{   0x1000,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_CQ_CONS_PTR_LO_OFFSET(rssId,portId)
	{   0x1004,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_CQ_CONS_PTR_HI_OFFSET(rssId,portId)
	{   0x1008,     0x40,     0x20,      0x0,      0x2},	// CSTORM_TOE_CQ_PROD_OFFSET(rssId,portId)
	{   0x100a,     0x40,     0x20,      0x0,      0x2},	// CSTORM_TOE_CQ_CONS_OFFSET(rssId,portId)
	{   0x100c,     0x40,     0x20,      0x0,      0x1},	// CSTORM_TOE_CQ_NXT_PAGE_ADDR_VALID_OFFSET(rssId,portId)
	{   0x100d,     0x40,     0x20,      0x0,      0x1},	// CSTORM_TOE_STATUS_BLOCK_ID_OFFSET(rssId,portId)
	{   0x100e,     0x40,     0x20,      0x0,      0x1},	// CSTORM_TOE_STATUS_BLOCK_INDEX_OFFSET(rssId,portId)
	{   0x1010,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_CQ_NEXT_PAGE_BASE_ADDR_LO_OFFSET(rssId,portId)
	{   0x1014,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_CQ_NEXT_PAGE_BASE_ADDR_HI_OFFSET(rssId,portId)
	{   0x1018,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_DYNAMIC_HC_PROD_OFFSET(rssId,portId)
	{   0x101c,     0x40,     0x20,      0x0,      0x4},	// CSTORM_TOE_DYNAMIC_HC_CONS_OFFSET(rssId,portId)
	{   0x3000,    0x100,     0x80,      0x8,      0x4},	// USTORM_GRQ_CACHE_BD_LO_OFFSET(rssId,portId,grqBdId)
	{   0x3004,    0x100,     0x80,      0x8,      0x4},	// USTORM_GRQ_CACHE_BD_HI_OFFSET(rssId,portId,grqBdId)
	{      0xa,      0x0,      0x0,      0x0,      0x0},	// USTORM_TOE_GRQ_CACHE_NUM_BDS
	{   0x3068,    0x100,     0x80,      0x0,      0x1},	// USTORM_TOE_GRQ_LOCAL_PROD_OFFSET(rssId,portId)
	{   0x3069,    0x100,     0x80,      0x0,      0x1},	// USTORM_TOE_GRQ_LOCAL_CONS_OFFSET(rssId,portId)
	{   0x306c,    0x100,     0x80,      0x0,      0x2},	// USTORM_TOE_GRQ_CONS_OFFSET(rssId,portId)
	{   0x306e,    0x100,     0x80,      0x0,      0x2},	// USTORM_TOE_GRQ_PROD_OFFSET(rssId,portId)
	{   0x3070,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_GRQ_CONS_PTR_LO_OFFSET(rssId,portId)
	{   0x3074,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_GRQ_CONS_PTR_HI_OFFSET(rssId,portId)
	{   0x3066,    0x100,     0x80,      0x0,      0x2},	// USTORM_TOE_GRQ_BUF_SIZE_OFFSET(rssId,portId)
	{   0x3064,    0x100,     0x80,      0x0,      0x1},	// USTORM_TOE_CQ_NXT_PAGE_ADDR_VALID_OFFSET(rssId,portId)
	{   0x3060,    0x100,     0x80,      0x0,      0x2},	// USTORM_TOE_CQ_CONS_OFFSET(rssId,portId)
	{   0x3062,    0x100,     0x80,      0x0,      0x2},	// USTORM_TOE_CQ_PROD_OFFSET(rssId,portId)
	{   0x3050,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_CQ_NEXT_PAGE_BASE_ADDR_LO_OFFSET(rssId,portId)
	{   0x3054,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_CQ_NEXT_PAGE_BASE_ADDR_HI_OFFSET(rssId,portId)
	{   0x3058,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_CQ_CONS_PTR_LO_OFFSET(rssId,portId)
	{   0x305c,    0x100,     0x80,      0x0,      0x4},	// USTORM_TOE_CQ_CONS_PTR_HI_OFFSET(rssId,portId)
	{   0x307c,    0x100,     0x80,      0x0,      0x1},	// USTORM_TOE_STATUS_BLOCK_ID_OFFSET(rssId,portId)
	{   0x307d,    0x100,     0x80,      0x0,      0x1},	// USTORM_TOE_STATUS_BLOCK_INDEX_OFFSET(rssId,portId)
	{   0x1c18,     0x10,      0x0,      0x0,      0x4},	// USTORM_TOE_TCP_PUSH_TIMER_TICKS_OFFSET(pfId)
	{   0x1c30,     0x10,      0x0,      0x0,      0x4},	// USTORM_TOE_GRQ_XOFF_COUNTER_OFFSET(pfId)
	{   0x1c38,     0x10,      0x0,      0x0,      0x4},	// USTORM_TOE_RCQ_XOFF_COUNTER_OFFSET(pfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_TOE_CQ_THR_LOW_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_TOE_GRQ_THR_LOW_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_TOE_CQ_THR_HIGH_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_TOE_GRQ_THR_HIGH_OFFSET
	{   0x4c10,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(pfId)
	{   0x4c12,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_TCP_VARS_LSB_LOCAL_MAC_ADDR_OFFSET(pfId)
	{   0x4c14,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_TCP_VARS_MID_LOCAL_MAC_ADDR_OFFSET(pfId)
	{   0x4c16,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_TCP_VARS_MSB_LOCAL_MAC_ADDR_OFFSET(pfId)
	{   0x4c20,      0x8,      0x0,      0x0,      0x8},	// TSTORM_ISCSI_RQ_SIZE_OFFSET(pfId)
	{   0x4c00,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId)
	{   0x4c02,      0x8,      0x0,      0x0,      0x1},	// TSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId)
	{   0x4c04,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId)
	{   0x4c30,      0x8,      0x0,      0x0,      0x8},	// TSTORM_ISCSI_ERROR_BITMAP_OFFSET(pfId)
	{   0x4c40,      0x8,      0x0,      0x0,      0x4},	// TSTORM_ISCSI_L2_ISCSI_OOO_CID_TABLE_OFFSET(pfId)
	{   0x4c44,      0x8,      0x0,      0x0,      0x1},	// TSTORM_ISCSI_L2_ISCSI_OOO_CLIENT_ID_TABLE_OFFSET(pfId)
	{   0x4c50,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_L2_ISCSI_OOO_PROD_OFFSET(pfId)
	{   0x4c54,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_L2_ISCSI_OOO_RX_BDS_THRSHLD_OFFSET(pfId)
	{   0x4c52,      0x8,      0x0,      0x0,      0x2},	// TSTORM_ISCSI_L2_ISCSI_OOO_CONS_OFFSET(pfId)
	{   0x4c60,      0x8,      0x0,      0x0,      0x4},	// TSTORM_ISCSI_TCP_LOCAL_ADV_WND_OFFSET(pfId)
	{   0x1400,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_PAGE_SIZE_OFFSET(pfId)
	{   0x1402,      0x8,      0x0,      0x0,      0x1},	// USTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId)
	{   0x1404,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId)
	{   0x1410,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_R2TQ_SIZE_OFFSET(pfId)
	{   0x1414,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_CQ_SIZE_OFFSET(pfId)
	{   0x1416,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_CQ_SQN_SIZE_OFFSET(pfId)
	{   0x19b8,      0x8,      0x0,      0x0,      0x8},	// USTORM_ISCSI_GLOBAL_BUF_PHYS_ADDR_OFFSET(pfId)
	{   0x1420,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_RQ_BUFFER_SIZE_OFFSET(pfId)
	{   0x1424,      0x8,      0x0,      0x0,      0x2},	// USTORM_ISCSI_RQ_SIZE_OFFSET(pfId)
	{   0x19c8,      0x8,      0x0,      0x0,      0x8},	// USTORM_ISCSI_ERROR_BITMAP_OFFSET(pfId)
	{   0x2c10,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_TCP_VARS_TTL_OFFSET(pfId)
	{   0x2c11,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_TCP_VARS_TOS_OFFSET(pfId)
	{   0x2c12,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(pfId)
	{   0x2c13,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_TCP_VARS_ADV_WND_SCL_OFFSET(pfId)
	{   0x2c00,      0x8,      0x0,      0x0,      0x2},	// XSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId)
	{   0x2c02,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId)
	{   0x2c04,      0x8,      0x0,      0x0,      0x2},	// XSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId)
	{   0x2c30,      0x8,      0x0,      0x0,      0x2},	// XSTORM_ISCSI_HQ_SIZE_OFFSET(pfId)
	{   0x2c32,      0x8,      0x0,      0x0,      0x2},	// XSTORM_ISCSI_SQ_SIZE_OFFSET(pfId)
	{   0x2c34,      0x8,      0x0,      0x0,      0x2},	// XSTORM_ISCSI_R2TQ_SIZE_OFFSET(pfId)
	{   0x2c20,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR0_OFFSET(pfId)
	{   0x2c21,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR1_OFFSET(pfId)
	{   0x2c22,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR2_OFFSET(pfId)
	{   0x2c23,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR3_OFFSET(pfId)
	{   0x2c24,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR4_OFFSET(pfId)
	{   0x2c25,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_MAC_ADDR5_OFFSET(pfId)
	{   0x2c26,      0x8,      0x0,      0x0,      0x1},	// XSTORM_ISCSI_LOCAL_VLAN_OFFSET(pfId)
	{   0x1480,      0x8,      0x0,      0x0,      0x2},	// CSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId)
	{   0x1482,      0x8,      0x0,      0x0,      0x1},	// CSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId)
	{   0x1484,      0x8,      0x0,      0x0,      0x2},	// CSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId)
	{   0x1492,     0xc0,     0x18,      0x0,      0x2},	// CSTORM_ISCSI_EQ_PROD_OFFSET(pfId,iscsiEqId)
	{   0x1490,     0xc0,     0x18,      0x0,      0x2},	// CSTORM_ISCSI_EQ_CONS_OFFSET(pfId,iscsiEqId)
	{   0x149c,     0xc0,     0x18,      0x0,      0x8},	// CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_OFFSET(pfId,iscsiEqId)
	{   0x1494,     0xc0,     0x18,      0x0,      0x8},	// CSTORM_ISCSI_EQ_NEXT_EQE_ADDR_OFFSET(pfId,iscsiEqId)
	{   0x14a7,     0xc0,     0x18,      0x0,      0x1},	// CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_VALID_OFFSET(pfId,iscsiEqId)
	{   0x14a4,     0xc0,     0x18,      0x0,      0x2},	// CSTORM_ISCSI_EQ_SB_NUM_OFFSET(pfId,iscsiEqId)
	{   0x14a6,     0xc0,     0x18,      0x0,      0x1},	// CSTORM_ISCSI_EQ_SB_INDEX_OFFSET(pfId,iscsiEqId)
	{   0x1610,      0x8,      0x0,      0x0,      0x8},	// CSTORM_ISCSI_HQ_SIZE_OFFSET(pfId)
	{   0x1620,      0x8,      0x0,      0x0,      0x8},	// CSTORM_ISCSI_CQ_SIZE_OFFSET(pfId)
	{   0x1630,      0x8,      0x0,      0x0,      0x8},	// CSTORM_ISCSI_CQ_SQN_SIZE_OFFSET(pfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_EQ_PROD_OFFSET(pfId)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_TIMER_PARAM_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_TIMER_ARRAY_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_STAT_FC_CRC_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_STAT_EOFA_DEL_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_STAT_MISS_FRAME_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_STAT_SEQ_TIMEOUT_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_STAT_DROP_SEQ_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_STAT_FCOE_RX_DROP_PKT_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_STAT_FCP_RX_PKT_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_STAT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_DEBUG_DROP_PKT_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_DEBUG_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_CACHED_TCE_MNG_INFO_DWORD_ONE_OFFSET(cached_tbl_size)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_CACHED_TCE_MNG_INFO_DWORD_TWO_OFFSET(cached_tbl_size)
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_CACHED_TCE_ENTRY_TCE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_CACHED_TCE_ENTRY_MNG_INFO_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_CACHED_TCE_TBL_BIT_MAP_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_DEBUG_CACHED_TCE_WAIT_4_BD_READ_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_DEBUG_CACHED_TCE_WAKE_ANOTHER_THREAD_DATA_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_DEBUG_CACHED_TCE_WAKE_ANOTHER_THREAD_NON_DATA_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_DEBUG_CACHED_TCE_WAKE_ANOTHER_THREAD_ERR_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_DEBUG_CACHED_TCE_GLOBAL_TIMER_TASK_IN_USE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_DEBUG_CACHED_TCE_DEL_CACHED_TASK_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_DEBUG_CACHED_TCE_SILENT_DROP_CACHED_TASK_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_DEBUG_CACHED_TCE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_CACHED_TCE_SEQ_CNT_ON_DROP_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_CACHED_TCE_SEQ_CNT_ON_CRC_ERROR_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_CACHED_TCE_SEQ_CNT_ON_ERROR_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_CACHED_TCE_PREVIOUS_THREAD_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_CACHED_TCE_CRC_ERR_DETECT_DATA_IN_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_CACHED_TCE_CRC_ERR_DETECT_READ_TCE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_CACHED_TCE_CRC_ERR_DETECT_DROP_ERR_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_PARAMS_ERRORS_NUMBER_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_PARAMS_SILENT_DROP_NUMBER_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_PARAMS_SILENT_DROP_BITMAP_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_PARAMS_ENABLE_CONN_RACE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_PARAMS_TASK_IN_USE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// USTORM_FCOE_DEBUG_PARAMS_CRC_ERROR_TASK_IN_USE_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_FCOE_TIMER_PARAM_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_TIMER_ARRAY_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_STAT_FCOE_TX_PKT_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_STAT_FCOE_TX_BYTE_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_STAT_FCP_TX_PKT_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_STAT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_DEBUG_ABTS_BLOCK_SQ_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_DEBUG_CLEANUP_BLOCK_SQ_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// XSTORM_DEBUG_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_STAT_FCOE_VER_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_STAT_FCOE_RX_PKT_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_STAT_FCOE_RX_BYTE_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_STAT_FCOE_RX_DROP_PKT_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_STAT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_PORT_DEBUG_WAIT_FOR_YOUR_TURN_SP_CNT_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_PORT_DEBUG_AFEX_ERROR_PACKETS_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_PORT_DEBUG_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_REORDER_DATA_OFFSET
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_REORDER_WAITING_TABLE_OFFSET
	{      0x0,      0x0,      0x0,      0x0,      0x0},	// TSTORM_WAITING_LIST_SIZE
	{UNDEF_IRO,      0x0,      0x0,      0x0,      0x0},	// TSTORM_REORDER_WAITING_ENTRY_OFFSET
};
