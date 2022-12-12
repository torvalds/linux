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
#define SIP_BUS_CFG			0x8200000d
#define SIP_LAST_LOG			0x8200000e
#define SIP_SCMI_AGENT0			0x82000010
#define SIP_SCMI_AGENT1			0x82000011
#define SIP_SCMI_AGENT2			0x82000012
#define SIP_SCMI_AGENT3			0x82000013
#define SIP_SCMI_AGENT4			0x82000014
#define SIP_SCMI_AGENT5			0x82000015
#define SIP_SCMI_AGENT6			0x82000016
#define SIP_SCMI_AGENT7			0x82000017
#define SIP_SCMI_AGENT8			0x82000018
#define SIP_SCMI_AGENT9			0x82000019
#define SIP_SCMI_AGENT10		0x8200001a
#define SIP_SCMI_AGENT11		0x8200001b
#define SIP_SCMI_AGENT12		0x8200001c
#define SIP_SCMI_AGENT13		0x8200001d
#define SIP_SCMI_AGENT14		0x8200001e
#define SIP_SCMI_AGENT15		0x8200001f
#define SIP_SDEI_FIQ_DBG_SWITCH_CPU	0x82000020
#define SIP_SDEI_FIQ_DBG_GET_EVENT_ID	0x82000021
#define RK_SIP_AMP_CFG			0x82000022
#define RK_SIP_FIQ_CTRL			0x82000024
#define SIP_HDCP_CONFIG			0x82000025
#define SIP_WDT_CFG			0x82000026

#define TRUSTED_OS_HDCPKEY_INIT		0xB7000003

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
#define LINUX_PM_STATE			0x09
#define SUSPEND_IO_RET_CONFIG		0x0a

/* SIP_REMOTECTL_CFG call types */
#define	REMOTECTL_SET_IRQ		0xf0
#define REMOTECTL_SET_PWM_CH		0xf1
#define REMOTECTL_SET_PWRKEY		0xf2
#define REMOTECTL_GET_WAKEUP_STATE	0xf3
#define REMOTECTL_ENABLE		0xf4
/* wakeup state */
#define REMOTECTL_PWRKEY_WAKEUP		0xdeadbeaf

struct dram_addrmap_info {
	u64 ch_mask[2];
	u64 bk_mask[4];
	u64 bg_mask[2];
	u64 cs_mask[2];
	u32 reserved[20];
	u32 bank_bit_first;
	u32 bank_bit_mask;
};

/* AMP Ctrl */
enum {
	RK_AMP_SUB_FUNC_CFG_MODE = 0,
	RK_AMP_SUB_FUNC_BOOT_ARG01,
	RK_AMP_SUB_FUNC_BOOT_ARG23,
	RK_AMP_SUB_FUNC_REQ_CPU_OFF,
	RK_AMP_SUB_FUNC_GET_CPU_STATUS,
	RK_AMP_SUB_FUNC_RSV, /* for RTOS */
	RK_AMP_SUB_FUNC_CPU_ON,
	RK_AMP_SUB_FUNC_END,
};

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
	SHARE_PAGE_TYPE_DDRDBG,
	SHARE_PAGE_TYPE_DDRECC,
	SHARE_PAGE_TYPE_DDRFSP,
	SHARE_PAGE_TYPE_DDR_ADDRMAP,
	SHARE_PAGE_TYPE_LAST_LOG,
	SHARE_PAGE_TYPE_HDCP,
	SHARE_PAGE_TYPE_MAX,
} share_page_type_t;

/* fiq control sub func */
enum {
	RK_SIP_FIQ_CTRL_FIQ_EN = 1,
	RK_SIP_FIQ_CTRL_FIQ_DIS,
	RK_SIP_FIQ_CTRL_SET_AFF
};

/* hdcp function types */
enum {
	HDCP_FUNC_STORAGE_INCRYPT = 1,
	HDCP_FUNC_KEY_LOAD,
	HDCP_FUNC_ENCRYPT_MODE
};

/* support hdcp device list */
enum {
	DP_TX0,
	DP_TX1,
	EDP_TX0,
	EDP_TX1,
	HDMI_TX0,
	HDMI_TX1,
	HDMI_RX,
	MAX_DEVICE,
};

/* SIP_WDT_CONFIG call types  */
enum {
	WDT_START = 0,
	WDT_STOP = 1,
	WDT_PING = 2,
};

/*
 * Rules: struct arm_smccc_res contains result and data, details:
 *
 * a0: error code(0: success, !0: error);
 * a1~a3: data
 */
#if IS_REACHABLE(CONFIG_ROCKCHIP_SIP)
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
struct arm_smccc_res sip_smc_bus_config(u32 arg0, u32 arg1, u32 arg2);
struct dram_addrmap_info *sip_smc_get_dram_map(void);
int sip_smc_amp_config(u32 sub_func_id, u32 arg1, u32 arg2, u32 arg3);
struct arm_smccc_res sip_smc_get_amp_info(u32 sub_func_id, u32 arg1);

void __iomem *sip_hdcp_request_share_memory(int id);
struct arm_smccc_res sip_hdcp_config(u32 arg0, u32 arg1, u32 arg2);
ulong sip_cpu_logical_map_mpidr(u32 cpu);
/***************************fiq debugger **************************************/
void sip_fiq_debugger_enable_fiq(bool enable, uint32_t tgt_cpu);
void sip_fiq_debugger_enable_debug(bool enable);
int sip_fiq_debugger_uart_irq_tf_init(u32 irq_id, void *callback_fn);
int sip_fiq_debugger_set_print_port(u32 port_phyaddr, u32 baudrate);
int sip_fiq_debugger_request_share_memory(void);
int sip_fiq_debugger_get_target_cpu(void);
int sip_fiq_debugger_switch_cpu(u32 cpu);
int sip_fiq_debugger_sdei_switch_cpu(u32 cur_cpu, u32 target_cpu, u32 flag);
int sip_fiq_debugger_is_enabled(void);
int sip_fiq_debugger_sdei_get_event_id(u32 *fiq, u32 *sw_cpu, u32 *flag);
int sip_fiq_control(u32 sub_func, u32 irq, unsigned long data);
int sip_wdt_config(u32 sub_func, u32 arg1, u32 arg2, u32 arg3);
int sip_hdcpkey_init(u32 hdcp_id);
#else
static inline struct arm_smccc_res sip_smc_get_atf_version(void)
{
	struct arm_smccc_res tmp = { .a0 = SIP_RET_NOT_SUPPORTED };
	return tmp;
}

static inline struct arm_smccc_res sip_smc_get_sip_version(void)
{
	struct arm_smccc_res tmp = { .a0 = SIP_RET_NOT_SUPPORTED };
	return tmp;
}

static inline struct arm_smccc_res sip_smc_dram(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res tmp = { .a0 = SIP_RET_NOT_SUPPORTED };
	return tmp;
}

static inline struct arm_smccc_res sip_smc_request_share_mem
			(u32 page_num, share_page_type_t page_type)
{
	struct arm_smccc_res tmp = { .a0 = SIP_RET_NOT_SUPPORTED };
	return tmp;
}

static inline struct arm_smccc_res sip_smc_mcu_el3fiq
			(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res tmp = { .a0 = SIP_RET_NOT_SUPPORTED };
	return tmp;
}

static inline struct arm_smccc_res
sip_smc_vpu_reset(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res tmp = { .a0 = SIP_RET_NOT_SUPPORTED };
	return tmp;
}

static inline struct arm_smccc_res sip_smc_get_suspend_info(u32 info)
{
	struct arm_smccc_res tmp = { .a0 = SIP_RET_NOT_SUPPORTED };
	return tmp;
}

static inline struct arm_smccc_res sip_smc_lastlog_request(void)
{
	struct arm_smccc_res tmp = { .a0 = SIP_RET_NOT_SUPPORTED };
	return tmp;
}

static inline int sip_smc_set_suspend_mode(u32 ctrl, u32 config1, u32 config2)
{
	return 0;
}

static inline int sip_smc_virtual_poweroff(void) { return 0; }
static inline int sip_smc_remotectl_config(u32 func, u32 data) { return 0; }
static inline int sip_smc_secure_reg_write(u32 addr_phy, u32 val) { return 0; }
static inline u32 sip_smc_secure_reg_read(u32 addr_phy) { return 0; }

static inline struct arm_smccc_res sip_smc_bus_config(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res tmp = { .a0 = SIP_RET_NOT_SUPPORTED };
	return tmp;
}

static inline struct dram_addrmap_info *sip_smc_get_dram_map(void)
{
	return NULL;
}

static inline int sip_smc_amp_config(u32 sub_func_id,
				     u32 arg1,
				     u32 arg2,
				     u32 arg3)
{
	return 0;
}

static inline struct arm_smccc_res sip_smc_get_amp_info(u32 sub_func_id,
							u32 arg1)
{
	struct arm_smccc_res tmp = { .a0 = SIP_RET_NOT_SUPPORTED, };

	return tmp;
}

static inline void __iomem *sip_hdcp_request_share_memory(int id)
{
	return NULL;
}

static inline struct arm_smccc_res sip_hdcp_config(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res tmp = { .a0 = SIP_RET_NOT_SUPPORTED };

	return tmp;
}

static inline ulong sip_cpu_logical_map_mpidr(u32 cpu) { return 0; }

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
static inline int sip_fiq_debugger_sdei_switch_cpu(u32 cur_cpu, u32 target_cpu,
						   u32 flag) { return 0; }
static inline int sip_fiq_debugger_is_enabled(void) { return 0; }
static inline int sip_fiq_debugger_sdei_get_event_id(u32 *fiq, u32 *sw_cpu, u32 *flag)
{
	return SIP_RET_NOT_SUPPORTED;
}

static inline int sip_fiq_control(u32 sub_func, u32 irq, unsigned long data)
{
	return 0;
}

static inline int sip_wdt_config(u32 sub_func,
				 u32 arg1,
				 u32 arg2,
				 u32 arg3)
{
	return 0;
}

static inline int sip_hdcpkey_init(u32 hdcp_id)
{
	return 0;
}
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
