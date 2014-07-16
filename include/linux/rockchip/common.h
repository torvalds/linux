#ifndef __MACH_ROCKCHIP_COMMON_H
#define __MACH_ROCKCHIP_COMMON_H

#include <linux/notifier.h>

#define RK_DEVICE(VIRT,PHYS,SIZE) \
	{ \
		.virtual	= (unsigned long)(VIRT), \
		.pfn		= __phys_to_pfn(PHYS), \
		.length		= SIZE, \
		.type		= MT_DEVICE, \
	}

extern bool rockchip_jtag_enabled;
extern unsigned long rockchip_boot_fn;
extern struct smp_operations rockchip_smp_ops;

extern int (*ddr_change_freq)(uint32_t nMHz);
extern long (*ddr_round_rate)(uint32_t nMHz);
extern void (*ddr_set_auto_self_refresh)(bool en);

extern int rockchip_cpu_kill(unsigned int cpu);
extern void rockchip_cpu_die(unsigned int cpu);
extern int rockchip_cpu_disable(unsigned int cpu);

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

extern int rockchip_boot_mode(void);
extern void __init rockchip_boot_mode_init(u32 flag, u32 mode);
extern void rockchip_restart_get_boot_mode(const char *cmd, u32 *flag, u32 *mode);
extern void __init rockchip_suspend_init(void);
extern void __init rockchip_ion_reserve(void);

enum rockchip_pm_policy {
	ROCKCHIP_PM_POLICY_PERFORMANCE = 0,
	ROCKCHIP_PM_POLICY_NORMAL,
	ROCKCHIP_PM_POLICY_POWERSAVE,
	ROCKCHIP_PM_NR_POLICYS,
};

extern enum rockchip_pm_policy rockchip_pm_get_policy(void);
extern int rockchip_pm_set_policy(enum rockchip_pm_policy policy);
extern int rockchip_pm_policy_register_notifier(struct notifier_block *nb);
extern int rockchip_pm_policy_unregister_notifier(struct notifier_block *nb);

extern int rockchip_register_system_status_notifier(struct notifier_block *nb);
extern int rockchip_unregister_system_status_notifier(struct notifier_block *nb);
extern int rockchip_set_system_status(unsigned long status);
extern int rockchip_clear_system_status(unsigned long status);
extern unsigned long rockchip_get_system_status(void);

#if IS_ENABLED(CONFIG_SENSORS_ROCKCHIP_TSADC)
extern int rockchip_tsadc_get_temp(int chn);
#else
static inline int rockchip_tsadc_get_temp(int chn) { return 0; }
#endif

#ifdef CONFIG_RK_LAST_LOG
extern void rk_last_log_text(char *text, size_t size);
#else
static inline void rk_last_log_text(char *text, size_t size) {}
#endif

#endif
