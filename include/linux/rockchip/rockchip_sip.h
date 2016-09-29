/* Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __ROCKCHIP_SIP_H
#define __ROCKCHIP_SIP_H

#include <linux/arm-smccc.h>
#include <linux/io.h>

/* SMC function IDs for SiP Service queries */
#define SIP_SVC_CALL_COUNT		0x8200ff00
#define SIP_SVC_UID			0x8200ff01
#define SIP_SVC_VERSION			0x8200ff03

#define SIP_ATF_VERSION32		0x82000001
#define SIP_SUSPEND_MODE32		0x82000003
#define SIP_DDR_CFG32			0x82000008
#define SIP_SHARE_MEM32			0x82000009
#define SIP_SIP_VERSION32		0x8200000a

/* Share mem page types */
typedef enum {
	SHARE_PAGE_TYPE_INVALID = 0,
	SHARE_PAGE_TYPE_UARTDBG,
	SHARE_PAGE_TYPE_MAX,
} share_page_type_t;

/* Error return code */
#define SIP_RET_SUCCESS			0
#define SIP_RET_NOT_SUPPORTED		-1
#define SIP_RET_INVALID_PARAMS		-2
#define SIP_RET_INVALID_ADDRESS		-3
#define SIP_RET_DENIED			-4
#define SIP_RET_SMC_UNKNOWN		0xffffffff

/* Sip version */
#define SIP_IMPLEMENT_V1		(1)
#define SIP_IMPLEMENT_V2		(2)

#define RK_SIP_DISABLE_FIQ		0xc2000006
#define RK_SIP_ENABLE_FIQ		0xc2000007
#define PSCI_SIP_RKTF_VER		0x82000001
#define PSCI_SIP_ACCESS_REG		0x82000002
#define PSCI_SIP_ACCESS_REG64		0xc2000002
#define PSCI_SIP_SUSPEND_WR_CTRBITS	0x82000003
#define PSCI_SIP_PENDING_CPUS		0x82000004
#define PSCI_SIP_UARTDBG_CFG		0x82000005
#define PSCI_SIP_UARTDBG_CFG64		0xc2000005
#define PSCI_SIP_EL3FIQ_CFG		0x82000006
#define PSCI_SIP_SMEM_CONFIG		0x82000007

#define UARTDBG_CFG_INIT		0xf0
#define UARTDBG_CFG_OSHDL_TO_OS		0xf1
#define UARTDBG_CFG_OSHDL_CPUSW		0xf3
#define UARTDBG_CFG_OSHDL_DEBUG_ENABLE	0xf4
#define UARTDBG_CFG_OSHDL_DEBUG_DISABLE	0xf5
#define UARTDBG_CFG_PRINT_PORT		0xf7

/* struct arm_smccc_res: a0: error code; a1~a3: data */
/* SMC32 Calls */
int sip_smc_set_suspend_mode(uint32_t mode);
struct arm_smccc_res sip_smc_get_call_count(void);
struct arm_smccc_res sip_smc_get_atf_version(void);
struct arm_smccc_res sip_smc_get_sip_version(void);
struct arm_smccc_res sip_smc_ddr_cfg(uint32_t arg0, uint32_t arg1,
				     uint32_t arg2);
struct arm_smccc_res sip_smc_get_share_mem_page(uint32_t page_num,
						share_page_type_t page_type);

void psci_enable_fiq(void);
u32 rockchip_psci_smc_get_tf_ver(void);
void psci_fiq_debugger_uart_irq_tf_cb(u64 sp_el1, u64 offset);
void psci_fiq_debugger_uart_irq_tf_init(u32 irq_id, void *callback);
u32 psci_fiq_debugger_switch_cpu(u32 cpu);
void psci_fiq_debugger_enable_debug(bool val);
int psci_fiq_debugger_set_print_port(u32 port, u32 baudrate);

#endif
