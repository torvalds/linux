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

/* SMC function IDs for SiP Service queries, compatible with kernel-3.10 */
#define SIP_ATF_VERSION			0x82000001
#define SIP_ACCESS_REG			0x82000002
#define SIP_SUSPEND_MODE		0x82000003
#define SIP_PENDING_CPUS		0x82000004
#define SIP_UARTDBG_CFG			0x82000005
#define SIP_UARTDBG_CFG64		0xc2000005
#define SIP_MCU_EL3FIQ_CFG		0x82000006
#define SIP_ACCESS_CHIP_STATE64		0xc2000006
#define SIP_SECURE_MEM_CONFIG		0x82000007
#define SIP_ACCESS_CHIP_EXTRA_STATE64	0xc2000007
#define SIP_DRAM_CONFIG			0x82000008
#define SIP_SHARE_MEM			0x82000009
#define SIP_SIP_VERSION			0x8200000a
#define SIP_REMOTECTL_CFG		0x8200000b
#define PSCI_SIP_VPU_RESET		0x8200000c
#define RK_SIP_SOC_BUS_DIV		0x8200000d
#define SIP_LAST_LOG			0x8200000e

/* Rockchip Sip version */
#define SIP_IMPLEMENT_V1                (1)
#define SIP_IMPLEMENT_V2                (2)

/* Trust firmware version */
#define ATF_VER_MAJOR(ver)		(((ver) >> 16) & 0xffff)
#define ATF_VER_MINOR(ver)		(((ver) >> 0) & 0xffff)

/* SIP_ACCESS_REG: read or write */
#define SECURE_REG_RD			0x0
#define SECURE_REG_WR			0x1

/* Fiq debugger share memory: 8KB enough */
#define FIQ_UARTDBG_PAGE_NUMS		2
#define FIQ_UARTDBG_SHARE_MEM_SIZE	((FIQ_UARTDBG_PAGE_NUMS) * 4096)

/* Error return code */
#define IS_SIP_ERROR(x)			(!!(x))

#define SIP_RET_SUCCESS			0
#define SIP_RET_SMC_UNKNOWN		-1
#define SIP_RET_NOT_SUPPORTED		-2
#define SIP_RET_INVALID_PARAMS		-3
#define SIP_RET_INVALID_ADDRESS		-4
#define SIP_RET_DENIED			-5
#define SIP_RET_SET_RATE_TIMEOUT	-6

/* SIP_UARTDBG_CFG64 call types */
#define UARTDBG_CFG_INIT		0xf0
#define UARTDBG_CFG_OSHDL_TO_OS		0xf1
#define UARTDBG_CFG_OSHDL_CPUSW		0xf3
#define UARTDBG_CFG_OSHDL_DEBUG_ENABLE	0xf4
#define UARTDBG_CFG_OSHDL_DEBUG_DISABLE	0xf5
#define UARTDBG_CFG_PRINT_PORT		0xf7
#define UARTDBG_CFG_FIQ_ENABEL		0xf8
#define UARTDBG_CFG_FIQ_DISABEL		0xf9

/* SIP_SUSPEND_MODE32 call types */
#define SUSPEND_MODE_CONFIG		0x01
#define WKUP_SOURCE_CONFIG		0x02
#define PWM_REGULATOR_CONFIG		0x03
#define GPIO_POWER_CONFIG		0x04
#define SUSPEND_DEBUG_ENABLE		0x05
#define APIOS_SUSPEND_CONFIG		0x06
#define VIRTUAL_POWEROFF		0x07
#define SUSPEND_WFI_TIME_MS		0x08

/* SIP_REMOTECTL_CFG call types */
#define	REMOTECTL_SET_IRQ		0xf0
#define REMOTECTL_SET_PWM_CH		0xf1
#define REMOTECTL_SET_PWRKEY		0xf2
#define REMOTECTL_GET_WAKEUP_STATE	0xf3
#define REMOTECTL_ENABLE		0xf4
/* wakeup state */
#define REMOTECTL_PWRKEY_WAKEUP		0xdeadbeaf

enum {
	FIRMWARE_NONE,
	FIRMWARE_TEE_32BIT,
	FIRMWARE_ATF_32BIT,
	FIRMWARE_ATF_64BIT,
	FIRMWARE_END,
};

/* Share mem page types */
typedef enum {
	SHARE_PAGE_TYPE_INVALID = 0,
	SHARE_PAGE_TYPE_UARTDBG,
	SHARE_PAGE_TYPE_DDR,
	SHARE_PAGE_TYPE_MAX,
} share_page_type_t;

/*
 * Rules: struct arm_smccc_res contains result and data, details:
 *
 * a0: error code(0: success, !0: error);
 * a1~a3: data
 */
#ifdef CONFIG_ROCKCHIP_SIP
struct arm_smccc_res sip_smc_get_atf_version(void);
struct arm_smccc_res sip_smc_get_sip_version(void);
struct arm_smccc_res sip_smc_dram(u32 arg0, u32 arg1, u32 arg2);
struct arm_smccc_res sip_smc_request_share_mem(u32 page_num,
					       share_page_type_t page_type);
struct arm_smccc_res sip_smc_mcu_el3fiq(u32 arg0, u32 arg1, u32 arg2);
struct arm_smccc_res sip_smc_vpu_reset(u32 arg0, u32 arg1, u32 arg2);
struct arm_smccc_res sip_smc_get_suspend_info(u32 info);
struct arm_smccc_res sip_smc_lastlog_request(void);

int sip_smc_set_suspend_mode(u32 ctrl, u32 config1, u32 config2);
int sip_smc_virtual_poweroff(void);
int sip_smc_remotectl_config(u32 func, u32 data);

int sip_smc_secure_reg_write(u32 addr_phy, u32 val);
u32 sip_smc_secure_reg_read(u32 addr_phy);
struct arm_smccc_res sip_smc_soc_bus_div(u32 arg0, u32 arg1, u32 arg2);

/***************************fiq debugger **************************************/
void sip_fiq_debugger_enable_fiq(bool enable, uint32_t tgt_cpu);
void sip_fiq_debugger_enable_debug(bool enable);
int sip_fiq_debugger_uart_irq_tf_init(u32 irq_id, void *callback_fn);
int sip_fiq_debugger_set_print_port(u32 port_phyaddr, u32 baudrate);
int sip_fiq_debugger_request_share_memory(void);
int sip_fiq_debugger_get_target_cpu(void);
int sip_fiq_debugger_switch_cpu(u32 cpu);
int sip_fiq_debugger_is_enabled(void);
#else
static inline struct arm_smccc_res sip_smc_get_atf_version(void)
{
	struct arm_smccc_res tmp = {0};
	return tmp;
}

static inline struct arm_smccc_res sip_smc_get_sip_version(void)
{
	struct arm_smccc_res tmp = {0};
	return tmp;
}

static inline struct arm_smccc_res sip_smc_dram(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res tmp = {0};
	return tmp;
}

static inline struct arm_smccc_res sip_smc_request_share_mem
			(u32 page_num, share_page_type_t page_type)
{
	struct arm_smccc_res tmp = {0};
	return tmp;
}

static inline struct arm_smccc_res sip_smc_mcu_el3fiq
			(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res tmp = {0};
	return tmp;
}

static inline struct arm_smccc_res
sip_smc_vpu_reset(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res tmp = {0};
	return tmp;
}

static inline struct arm_smccc_res sip_smc_lastlog_request(void)
{
	struct arm_smccc_res tmp = {0};
	return tmp;
}

static inline int sip_smc_set_suspend_mode(u32 ctrl, u32 config1, u32 config2)
{
	return 0;
}

static inline int sip_smc_get_suspend_info(u32 info)
{
	return 0;
}

static inline int sip_smc_virtual_poweroff(void) { return 0; }
static inline int sip_smc_remotectl_config(u32 func, u32 data) { return 0; }
static inline u32 sip_smc_secure_reg_read(u32 addr_phy) { return 0; }
static inline int sip_smc_secure_reg_write(u32 addr_phy, u32 val) { return 0; }
static inline int sip_smc_soc_bus_div(u32 arg0, u32 arg1, u32 arg2)
{
	return 0;
}

/***************************fiq debugger **************************************/
static inline void sip_fiq_debugger_enable_fiq
			(bool enable, uint32_t tgt_cpu) { return; }

static inline void sip_fiq_debugger_enable_debug(bool enable) { return; }
static inline int sip_fiq_debugger_uart_irq_tf_init(u32 irq_id,
						    void *callback_fn)
{
	return 0;
}

static inline int sip_fiq_debugger_set_print_port(u32 port_phyaddr,
						  u32 baudrate)
{
	return 0;
}

static inline int sip_fiq_debugger_request_share_memory(void) { return 0; }
static inline int sip_fiq_debugger_get_target_cpu(void) { return 0; }
static inline int sip_fiq_debugger_switch_cpu(u32 cpu) { return 0; }
static inline int sip_fiq_debugger_is_enabled(void) { return 0; }
#endif

/* 32-bit OP-TEE context, never change order of members! */
struct sm_nsec_ctx {
	u32 usr_sp;
	u32 usr_lr;
	u32 irq_spsr;
	u32 irq_sp;
	u32 irq_lr;
	u32 fiq_spsr;
	u32 fiq_sp;
	u32 fiq_lr;
	u32 svc_spsr;
	u32 svc_sp;
	u32 svc_lr;
	u32 abt_spsr;
	u32 abt_sp;
	u32 abt_lr;
	u32 und_spsr;
	u32 und_sp;
	u32 und_lr;
	u32 mon_lr;
	u32 mon_spsr;
	u32 r4;
	u32 r5;
	u32 r6;
	u32 r7;
	u32 r8;
	u32 r9;
	u32 r10;
	u32 r11;
	u32 r12;
	u32 r0;
	u32 r1;
	u32 r2;
	u32 r3;
};

/* 64-bit ATF context, never change order of members! */
struct gp_regs_ctx {
	u64 x0;
	u64 x1;
	u64 x2;
	u64 x3;
	u64 x4;
	u64 x5;
	u64 x6;
	u64 x7;
	u64 x8;
	u64 x9;
	u64 x10;
	u64 x11;
	u64 x12;
	u64 x13;
	u64 x14;
	u64 x15;
	u64 x16;
	u64 x17;
	u64 x18;
	u64 x19;
	u64 x20;
	u64 x21;
	u64 x22;
	u64 x23;
	u64 x24;
	u64 x25;
	u64 x26;
	u64 x27;
	u64 x28;
	u64 x29;
	u64 lr;
	u64 sp_el0;
	u64 scr_el3;
	u64 runtime_sp;
	u64 spsr_el3;
	u64 elr_el3;
};

#endif
