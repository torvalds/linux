#ifndef __MACH_ROCKCHIP_COMMON_H
#define __MACH_ROCKCHIP_COMMON_H

#include <linux/notifier.h>

#define RK_DEVICE(VIRT, PHYS, SIZE) \
	{ \
		.virtual	= (unsigned long)(VIRT), \
		.pfn		= __phys_to_pfn(PHYS), \
		.length		= SIZE, \
		.type		= MT_DEVICE, \
	}

extern bool rockchip_jtag_enabled;
extern unsigned long rockchip_boot_fn;
extern struct smp_operations rockchip_smp_ops;

struct ddr_bw_info {
	u32 ddr_wr;
	u32 ddr_rd;
	u32 ddr_act;
	u32 ddr_time;
	u32 ddr_total;
	u32 ddr_percent;

	u32 cpum;
	u32 gpu;
	u32 peri;
	u32 video;
	u32 vio0;
	u32 vio1;
	u32 vio2;
};
extern void (*ddr_bandwidth_get)(struct ddr_bw_info *ddr_bw_ch0,
				 struct ddr_bw_info *ddr_bw_ch1);
extern int (*ddr_change_freq)(uint32_t mhz);
extern long (*ddr_round_rate)(uint32_t mhz);
extern void (*ddr_set_auto_self_refresh)(bool en);
extern int (*ddr_recalc_rate)(void);

int rockchip_cpu_kill(unsigned int cpu);
void rockchip_cpu_die(unsigned int cpu);
int rockchip_cpu_disable(unsigned int cpu);

#define BOOT_MODE_NORMAL		0
#define BOOT_MODE_FACTORY2		1
#define BOOT_MODE_RECOVERY		2
#define BOOT_MODE_CHARGE		3
#define BOOT_MODE_POWER_TEST		4
#define BOOT_MODE_OFFMODE_CHARGING	5
#define BOOT_MODE_REBOOT		6
#define BOOT_MODE_PANIC			7
#define BOOT_MODE_WATCHDOG		8
#define BOOT_MODE_TSADC			9

int rockchip_boot_mode(void);
void __init rockchip_boot_mode_init(u32 flag, u32 mode);
void rockchip_restart_get_boot_mode(const char *cmd, u32 *flag, u32 *mode);
void __init rockchip_efuse_init(void);
void __init rockchip_suspend_init(void);
void __init rockchip_ion_reserve(void);
void __init rockchip_uboot_mem_reserve(void);

enum rockchip_pm_policy {
	ROCKCHIP_PM_POLICY_PERFORMANCE = 0,
	ROCKCHIP_PM_POLICY_NORMAL,
	ROCKCHIP_PM_POLICY_POWERSAVE,
	ROCKCHIP_PM_NR_POLICYS,
};

enum rockchip_pm_policy rockchip_pm_get_policy(void);
int rockchip_pm_set_policy(enum rockchip_pm_policy policy);
int rockchip_pm_policy_register_notifier(struct notifier_block *nb);
int rockchip_pm_policy_unregister_notifier(struct notifier_block *nb);

u32 pvtm_get_value(u32 ch, u32 time_us);

#define INVALID_TEMP INT_MAX
#if IS_ENABLED(CONFIG_ROCKCHIP_THERMAL)
int rockchip_tsadc_get_temp(int chn, int voltage);
#else
#if IS_ENABLED(CONFIG_SENSORS_ROCKCHIP_TSADC)
int rockchip_tsadc_get_temp(int chn);
#else
static inline int rockchip_tsadc_get_temp(int chn) { return INVALID_TEMP; }
#endif
#endif

#ifdef CONFIG_RK_LAST_LOG
void rk_last_log_text(char *text, size_t size);
#else
static inline void rk_last_log_text(char *text, size_t size) {}
#endif

#endif
