#ifndef __MACH_ROCKCHIP_PMU_H
#define __MACH_ROCKCHIP_PMU_H

#define RK3188_PMU_WAKEUP_CFG0          0x00
#define RK3188_PMU_WAKEUP_CFG1          0x04
#define RK3188_PMU_PWRDN_CON            0x08
#define RK3188_PMU_PWRDN_ST             0x0c
#define RK3188_PMU_INT_CON              0x10
#define RK3188_PMU_INT_ST               0x14
#define RK3188_PMU_MISC_CON             0x18
#define RK3188_PMU_OSC_CNT              0x1c
#define RK3188_PMU_PLL_CNT              0x20
#define RK3188_PMU_PMU_CNT              0x24
#define RK3188_PMU_DDRIO_PWRON_CNT      0x28
#define RK3188_PMU_WAKEUP_RST_CLR_CNT   0x2c
#define RK3188_PMU_SCU_PWRDWN_CNT       0x30
#define RK3188_PMU_SCU_PWRUP_CNT        0x34
#define RK3188_PMU_MISC_CON1            0x38
#define RK3188_PMU_GPIO0_CON            0x3c
#define RK3188_PMU_SYS_REG0             0x40
#define RK3188_PMU_SYS_REG1             0x44
#define RK3188_PMU_SYS_REG2             0x48
#define RK3188_PMU_SYS_REG3             0x4c
#define RK3188_PMU_STOP_INT_DLY         0x60
#define RK3188_PMU_GPIO0A_PULL          0x64
#define RK3188_PMU_GPIO0B_PULL          0x68

#define RK3288_PMU_WAKEUP_CFG0          0x00
#define RK3288_PMU_WAKEUP_CFG1          0x04
#define RK3288_PMU_PWRDN_CON            0x08
#define RK3288_PMU_PWRDN_ST             0x0c
#define RK3288_PMU_IDLE_REQ             0x10
#define RK3288_PMU_IDLE_ST              0x14
#define RK3288_PMU_PWRMODE_CON          0x18
#define RK3288_PMU_PWR_STATE            0x1c
#define RK3288_PMU_OSC_CNT              0x20
#define RK3288_PMU_PLL_CNT              0x24
#define RK3288_PMU_STABL_CNT            0x28
#define RK3288_PMU_DDR0IO_PWRON_CNT     0x2c
#define RK3288_PMU_DDR1IO_PWRON_CNT     0x30
#define RK3288_PMU_CORE_PWRDWN_CNT      0x34
#define RK3288_PMU_CORE_PWRUP_CNT       0x38
#define RK3288_PMU_GPU_PWRDWN_CNT       0x3c
#define RK3288_PMU_GPU_PWRUP_CNT        0x40
#define RK3288_PMU_WAKEUP_RST_CLR_CNT   0x44
#define RK3288_PMU_SFT_CON              0x48
#define RK3288_PMU_DDR_SREF_ST          0x4c
#define RK3288_PMU_INT_CON              0x50
#define RK3288_PMU_INT_ST               0x54
#define RK3288_PMU_BOOT_ADDR_SEL        0x58
#define RK3288_PMU_GRF_CON              0x5c
#define RK3288_PMU_GPIO_SR              0x60
#define RK3288_PMU_GPIO0_A_PULL         0x64
#define RK3288_PMU_GPIO0_B_PULL         0x68
#define RK3288_PMU_GPIO0_C_PULL         0x6c
#define RK3288_PMU_GPIO0_A_DRV          0x70
#define RK3288_PMU_GPIO0_B_DRV          0x74
#define RK3288_PMU_GPIO0_C_DRV          0x78
#define RK3288_PMU_GPIO_OP              0x7c
#define RK3288_PMU_GPIO0_SEL18          0x80
#define RK3288_PMU_GPIO0_A_IOMUX        0x84
#define RK3288_PMU_GPIO0_B_IOMUX        0x88
#define RK3288_PMU_GPIO0_C_IOMUX        0x8c
#define RK3288_PMU_PWRMODE_CON1        0x90
#define RK3288_PMU_SYS_REG0             0x94
#define RK3288_PMU_SYS_REG1             0x98
#define RK3288_PMU_SYS_REG2             0x9c
#define RK3288_PMU_SYS_REG3             0xa0

#define RK312X_PMU_WAKEUP_CFG		0x00
#define RK312X_PMU_PWRDN_CON			0x04
#define RK312X_PMU_PWRDN_ST			0x08
#define RK312X_PMU_IDLE_REQ			0x0C
#define RK312X_PMU_IDLE_ST				0x10
#define RK312X_PMU_PWRMODE_CON		0x14
#define RK312X_PMU_PWR_STATE			0x18
#define RK312X_PMU_OSC_CNT			0x1C
#define RK312X_PMU_CORE_PWRDWN_CNT	0x20
#define RK312X_PMU_CORE_PWRUP_CNT	0x24
#define RK312X_PMU_SFT_CON			0x28
#define RK312X_PMU_DDR_SREF_ST		0x2C
#define RK312X_PMU_INT_CON			0x30
#define RK312X_PMU_INT_ST				0x34
#define RK312X_PMU_SYS_REG0			0x38
#define RK312X_PMU_SYS_REG1			0x3C
#define RK312X_PMU_SYS_REG2			0x40
#define RK312X_PMU_SYS_REG3			0x44

#define RK3368_PMU_PWRDN_CON		0x0c
#define RK3368_PMU_PWRDN_ST		0x10
#define RK3368_PMU_IDLE_REQ		0x3c
#define RK3368_PMU_IDLE_ST		0x40

enum pmu_power_domain {
	PD_BCPU,
	PD_BDSP,
	PD_BUS,
	PD_CPU_0,
	PD_CPU_1,
	PD_CPU_2,
	PD_CPU_3,
	PD_CS,
	PD_GPU,
	PD_HEVC,
	PD_PERI,
	PD_SCU,
	PD_VIDEO,
	PD_VIO,
	PD_GPU_0,
	PD_GPU_1,
};

enum pmu_idle_req {
	IDLE_REQ_ALIVE,
	IDLE_REQ_AP2BP,
	IDLE_REQ_BP2AP,
	IDLE_REQ_BUS,
	IDLE_REQ_CORE,
	IDLE_REQ_CPUP,
	IDLE_REQ_DMA,
	IDLE_REQ_GPU,
	IDLE_REQ_HEVC,
	IDLE_REQ_PERI,
	IDLE_REQ_VIDEO,
	IDLE_REQ_VIO,
	IDLE_REQ_SYS,
	IDLE_REQ_MSCH,
	IDLE_REQ_CRYPTO,
};

struct rockchip_pmu_operations {
	int (*set_power_domain)(enum pmu_power_domain pd, bool on);
	bool (*power_domain_is_on)(enum pmu_power_domain pd);
	int (*set_idle_request)(enum pmu_idle_req req, bool idle);
};

int rockchip_pmu_idle_request(struct device *dev, bool idle);
extern struct rockchip_pmu_operations rockchip_pmu_ops;

#endif
