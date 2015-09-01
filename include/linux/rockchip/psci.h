#ifndef __ROCKCHIP_PSCI_H
#define __ROCKCHIP_PSCI_H

#define SEC_REG_RW_SHT (0x0)
#define SEC_REG_RD (0x0)
#define SEC_REG_WR (0x1)

#define SEC_REG_BITS_SHT (0x1)
#define SEC_REG_32 (0x0)
#define SEC_REG_64 (0x2)

#define SEC_REG_RD_32 (SEC_REG_RD | SEC_REG_32)
#define SEC_REG_RD_64 (SEC_REG_RD | SEC_REG_64)
#define SEC_REG_WR_32 (SEC_REG_WR | SEC_REG_32)
#define SEC_REG_WR_64 (SEC_REG_WR | SEC_REG_64)

/*
 * trust firmware verison
 */
#define RKTF_VER_MAJOR(ver)		(((ver) >> 16) & 0xffff)
#define RKTF_VER_MINOR(ver)		((ver) & 0xffff)

/*
 * pcsi smc funciton id
 */
#define PSCI_SIP_RKTF_VER		(0x82000001)
#define PSCI_SIP_ACCESS_REG		(0x82000002)
#define PSCI_SIP_SUSPEND_WR_CTRBITS	(0x82000003)
#define PSCI_SIP_PENDING_CPUS		(0x82000004)
#define PSCI_SIP_UARTDBG_CFG		(0x82000005)
#define PSCI_SIP_EL3FIQ_CFG		(0x82000006)

/*
 * pcsi smc funciton err code
 */
#define PSCI_SMC_FUNC_UNK		0xffffffff

/*
 * define PSCI_SIP_UARTDBG_CFG call type
 */
#define UARTDBG_CFG_INIT		0xf0
#define UARTDBG_CFG_OSHDL_TO_OS		0xf1
#define UARTDBG_CFG_OSHDL_CPUSW		0xf3
#define UARTDBG_CFG_OSHDL_DEBUG_ENABLE	0xf4
#define UARTDBG_CFG_OSHDL_DEBUG_DISABLE	0xf5

/*
 * rockchip psci function call interface
 */
u32 rockchip_psci_smc_read(u64 function_id, u64 arg0, u64 arg1, u64 arg2,
			   u64 *val);
u32 rockchip_psci_smc_write(u64 function_id, u64 arg0, u64 arg1, u64 arg2);

u32 rockchip_psci_smc_get_tf_ver(void);
u32 rockchip_secure_reg_read32(u64 addr_phy);
u32 rockchip_secure_reg_write32(u64 addr_phy, u32 val);

u32 psci_fiq_debugger_switch_cpu(u32 cpu);
void psci_fiq_debugger_uart_irq_tf_cb(u64 sp_el1, u64 offset);
void psci_fiq_debugger_uart_irq_tf_init(u32 irq_id, void *callback);
void psci_fiq_debugger_enable_debug(bool val);

#endif /* __ROCKCHIP_PSCI_H */
