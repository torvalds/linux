#ifndef __ACPI_PROCESSOR_H
#define __ACPI_PROCESSOR_H

#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/thermal.h>
#include <asm/acpi.h>

#define ACPI_PROCESSOR_BUSY_METRIC	10

#define ACPI_PROCESSOR_MAX_POWER	8
#define ACPI_PROCESSOR_MAX_C2_LATENCY	100
#define ACPI_PROCESSOR_MAX_C3_LATENCY	1000

#define ACPI_PROCESSOR_MAX_THROTTLING	16
#define ACPI_PROCESSOR_MAX_THROTTLE	250	/* 25% */
#define ACPI_PROCESSOR_MAX_DUTY_WIDTH	4

#define ACPI_PDC_REVISION_ID		0x1

#define ACPI_PSD_REV0_REVISION		0	/* Support for _PSD as in ACPI 3.0 */
#define ACPI_PSD_REV0_ENTRIES		5

#define ACPI_TSD_REV0_REVISION		0	/* Support for _PSD as in ACPI 3.0 */
#define ACPI_TSD_REV0_ENTRIES		5
/*
 * Types of coordination defined in ACPI 3.0. Same macros can be used across
 * P, C and T states
 */
#define DOMAIN_COORD_TYPE_SW_ALL	0xfc
#define DOMAIN_COORD_TYPE_SW_ANY	0xfd
#define DOMAIN_COORD_TYPE_HW_ALL	0xfe

#define ACPI_CSTATE_SYSTEMIO	0
#define ACPI_CSTATE_FFH		1
#define ACPI_CSTATE_HALT	2

#define ACPI_CX_DESC_LEN	32

/* Power Management */

struct acpi_processor_cx;

struct acpi_power_register {
	u8 descriptor;
	u16 length;
	u8 space_id;
	u8 bit_width;
	u8 bit_offset;
	u8 access_size;
	u64 address;
} __attribute__ ((packed));

struct acpi_processor_cx {
	u8 valid;
	u8 type;
	u32 address;
	u8 entry_method;
	u8 index;
	u32 latency;
	u8 bm_sts_skip;
	char desc[ACPI_CX_DESC_LEN];
};

struct acpi_processor_power {
	struct cpuidle_device dev;
	struct acpi_processor_cx *state;
	unsigned long bm_check_timestamp;
	u32 default_state;
	int count;
	struct acpi_processor_cx states[ACPI_PROCESSOR_MAX_POWER];
	int timer_broadcast_on_state;
};

/* Performance Management */

struct acpi_psd_package {
	u64 num_entries;
	u64 revision;
	u64 domain;
	u64 coord_type;
	u64 num_processors;
} __attribute__ ((packed));

struct acpi_pct_register {
	u8 descriptor;
	u16 length;
	u8 space_id;
	u8 bit_width;
	u8 bit_offset;
	u8 reserved;
	u64 address;
} __attribute__ ((packed));

struct acpi_processor_px {
	u64 core_frequency;	/* megahertz */
	u64 power;	/* milliWatts */
	u64 transition_latency;	/* microseconds */
	u64 bus_master_latency;	/* microseconds */
	u64 control;	/* control value */
	u64 status;	/* success indicator */
};

struct acpi_processor_performance {
	unsigned int state;
	unsigned int platform_limit;
	struct acpi_pct_register control_register;
	struct acpi_pct_register status_register;
	unsigned int state_count;
	struct acpi_processor_px *states;
	struct acpi_psd_package domain_info;
	cpumask_var_t shared_cpu_map;
	unsigned int shared_type;
};

/* Throttling Control */

struct acpi_tsd_package {
	u64 num_entries;
	u64 revision;
	u64 domain;
	u64 coord_type;
	u64 num_processors;
} __attribute__ ((packed));

struct acpi_ptc_register {
	u8 descriptor;
	u16 length;
	u8 space_id;
	u8 bit_width;
	u8 bit_offset;
	u8 reserved;
	u64 address;
} __attribute__ ((packed));

struct acpi_processor_tx_tss {
	u64 freqpercentage;	/* */
	u64 power;	/* milliWatts */
	u64 transition_latency;	/* microseconds */
	u64 control;	/* control value */
	u64 status;	/* success indicator */
};
struct acpi_processor_tx {
	u16 power;
	u16 performance;
};

struct acpi_processor;
struct acpi_processor_throttling {
	unsigned int state;
	unsigned int platform_limit;
	struct acpi_pct_register control_register;
	struct acpi_pct_register status_register;
	unsigned int state_count;
	struct acpi_processor_tx_tss *states_tss;
	struct acpi_tsd_package domain_info;
	cpumask_var_t shared_cpu_map;
	int (*acpi_processor_get_throttling) (struct acpi_processor * pr);
	int (*acpi_processor_set_throttling) (struct acpi_processor * pr,
					      int state, bool force);

	u32 address;
	u8 duty_offset;
	u8 duty_width;
	u8 tsd_valid_flag;
	unsigned int shared_type;
	struct acpi_processor_tx states[ACPI_PROCESSOR_MAX_THROTTLING];
};

/* Limit Interface */

struct acpi_processor_lx {
	int px;			/* performance state */
	int tx;			/* throttle level */
};

struct acpi_processor_limit {
	struct acpi_processor_lx state;	/* current limit */
	struct acpi_processor_lx thermal;	/* thermal limit */
	struct acpi_processor_lx user;	/* user limit */
};

struct acpi_processor_flags {
	u8 power:1;
	u8 performance:1;
	u8 throttling:1;
	u8 limit:1;
	u8 bm_control:1;
	u8 bm_check:1;
	u8 has_cst:1;
	u8 power_setup_done:1;
	u8 bm_rld_set:1;
	u8 need_hotplug_init:1;
};

struct acpi_processor {
	acpi_handle handle;
	u32 acpi_id;
	u32 id;
	u32 pblk;
	int performance_platform_limit;
	int throttling_platform_limit;
	/* 0 - states 0..n-th state available */

	struct acpi_processor_flags flags;
	struct acpi_processor_power power;
	struct acpi_processor_performance *performance;
	struct acpi_processor_throttling throttling;
	struct acpi_processor_limit limit;
	struct thermal_cooling_device *cdev;
};

struct acpi_processor_errata {
	u8 smp;
	struct {
		u8 throttle:1;
		u8 fdma:1;
		u8 reserved:6;
		u32 bmisx;
	} piix4;
};

extern void acpi_processor_load_module(struct acpi_processor *pr);
extern int acpi_processor_preregister_performance(struct
						  acpi_processor_performance
						  __percpu *performance);

extern int acpi_processor_register_performance(struct acpi_processor_performance
					       *performance, unsigned int cpu);
extern void acpi_processor_unregister_performance(struct
						  acpi_processor_performance
						  *performance,
						  unsigned int cpu);

/* note: this locks both the calling module and the processor module
         if a _PPC object exists, rmmod is disallowed then */
int acpi_processor_notify_smm(struct module *calling_module);

/* for communication between multiple parts of the processor kernel module */
DECLARE_PER_CPU(struct acpi_processor *, processors);
extern struct acpi_processor_errata errata;

#ifdef ARCH_HAS_POWER_INIT
void acpi_processor_power_init_bm_check(struct acpi_processor_flags *flags,
					unsigned int cpu);
int acpi_processor_ffh_cstate_probe(unsigned int cpu,
				    struct acpi_processor_cx *cx,
				    struct acpi_power_register *reg);
void acpi_processor_ffh_cstate_enter(struct acpi_processor_cx *cstate);
#else
static inline void acpi_processor_power_init_bm_check(struct
						      acpi_processor_flags
						      *flags, unsigned int cpu)
{
	flags->bm_check = 1;
	return;
}
static inline int acpi_processor_ffh_cstate_probe(unsigned int cpu,
						  struct acpi_processor_cx *cx,
						  struct acpi_power_register
						  *reg)
{
	return -1;
}
static inline void acpi_processor_ffh_cstate_enter(struct acpi_processor_cx
						   *cstate)
{
	return;
}
#endif

/* in processor_perflib.c */

#ifdef CONFIG_CPU_FREQ
void acpi_processor_ppc_init(void);
void acpi_processor_ppc_exit(void);
int acpi_processor_ppc_has_changed(struct acpi_processor *pr, int event_flag);
extern int acpi_processor_get_bios_limit(int cpu, unsigned int *limit);
#else
static inline void acpi_processor_ppc_init(void)
{
	return;
}
static inline void acpi_processor_ppc_exit(void)
{
	return;
}
static inline int acpi_processor_ppc_has_changed(struct acpi_processor *pr,
								int event_flag)
{
	static unsigned int printout = 1;
	if (printout) {
		printk(KERN_WARNING
		       "Warning: Processor Platform Limit event detected, but not handled.\n");
		printk(KERN_WARNING
		       "Consider compiling CPUfreq support into your kernel.\n");
		printout = 0;
	}
	return 0;
}
static inline int acpi_processor_get_bios_limit(int cpu, unsigned int *limit)
{
	return -ENODEV;
}

#endif				/* CONFIG_CPU_FREQ */

/* in processor_core.c */
void acpi_processor_set_pdc(acpi_handle handle);
int acpi_get_cpuid(acpi_handle, int type, u32 acpi_id);

/* in processor_throttling.c */
int acpi_processor_tstate_has_changed(struct acpi_processor *pr);
int acpi_processor_get_throttling_info(struct acpi_processor *pr);
extern int acpi_processor_set_throttling(struct acpi_processor *pr,
					 int state, bool force);
/*
 * Reevaluate whether the T-state is invalid after one cpu is
 * onlined/offlined. In such case the flags.throttling will be updated.
 */
extern void acpi_processor_reevaluate_tstate(struct acpi_processor *pr,
			unsigned long action);
extern const struct file_operations acpi_processor_throttling_fops;
extern void acpi_processor_throttling_init(void);
/* in processor_idle.c */
int acpi_processor_power_init(struct acpi_processor *pr,
			      struct acpi_device *device);
int acpi_processor_cst_has_changed(struct acpi_processor *pr);
int acpi_processor_hotplug(struct acpi_processor *pr);
int acpi_processor_power_exit(struct acpi_processor *pr,
			      struct acpi_device *device);
int acpi_processor_suspend(struct device *dev);
int acpi_processor_resume(struct device *dev);
extern struct cpuidle_driver acpi_idle_driver;

/* in processor_thermal.c */
int acpi_processor_get_limit_info(struct acpi_processor *pr);
extern const struct thermal_cooling_device_ops processor_cooling_ops;
#ifdef CONFIG_CPU_FREQ
void acpi_thermal_cpufreq_init(void);
void acpi_thermal_cpufreq_exit(void);
#else
static inline void acpi_thermal_cpufreq_init(void)
{
	return;
}
static inline void acpi_thermal_cpufreq_exit(void)
{
	return;
}
#endif

#endif
