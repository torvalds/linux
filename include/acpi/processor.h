#ifndef __ACPI_PROCESSOR_H
#define __ACPI_PROCESSOR_H

#include <linux/kernel.h>
#include <linux/cpu.h>

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

#define ACPI_CSTATE_SYSTEMIO	(0)
#define ACPI_CSTATE_FFH		(1)

/* Power Management */

struct acpi_processor_cx;

struct acpi_power_register {
	u8 descriptor;
	u16 length;
	u8 space_id;
	u8 bit_width;
	u8 bit_offset;
	u8 reserved;
	u64 address;
} __attribute__ ((packed));

struct acpi_processor_cx_policy {
	u32 count;
	struct acpi_processor_cx *state;
	struct {
		u32 time;
		u32 ticks;
		u32 count;
		u32 bm;
	} threshold;
};

struct acpi_processor_cx {
	u8 valid;
	u8 type;
	u32 address;
	u8 space_id;
	u8 index;
	u32 latency;
	u32 latency_ticks;
	u32 power;
	u32 usage;
	u64 time;
	struct acpi_processor_cx_policy promotion;
	struct acpi_processor_cx_policy demotion;
};

struct acpi_processor_power {
	struct acpi_processor_cx *state;
	unsigned long bm_check_timestamp;
	u32 default_state;
	u32 bm_activity;
	int count;
	struct acpi_processor_cx states[ACPI_PROCESSOR_MAX_POWER];
	int timer_broadcast_on_state;
};

/* Performance Management */

struct acpi_psd_package {
	acpi_integer num_entries;
	acpi_integer revision;
	acpi_integer domain;
	acpi_integer coord_type;
	acpi_integer num_processors;
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
	acpi_integer core_frequency;	/* megahertz */
	acpi_integer power;	/* milliWatts */
	acpi_integer transition_latency;	/* microseconds */
	acpi_integer bus_master_latency;	/* microseconds */
	acpi_integer control;	/* control value */
	acpi_integer status;	/* success indicator */
};

struct acpi_processor_performance {
	unsigned int state;
	unsigned int platform_limit;
	struct acpi_pct_register control_register;
	struct acpi_pct_register status_register;
	unsigned int state_count;
	struct acpi_processor_px *states;
	struct acpi_psd_package domain_info;
	cpumask_t shared_cpu_map;
	unsigned int shared_type;
};

/* Throttling Control */

struct acpi_tsd_package {
	acpi_integer num_entries;
	acpi_integer revision;
	acpi_integer domain;
	acpi_integer coord_type;
	acpi_integer num_processors;
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
	acpi_integer freqpercentage;	/* */
	acpi_integer power;	/* milliWatts */
	acpi_integer transition_latency;	/* microseconds */
	acpi_integer control;	/* control value */
	acpi_integer status;	/* success indicator */
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
	cpumask_t shared_cpu_map;
	int (*acpi_processor_get_throttling) (struct acpi_processor * pr);
	int (*acpi_processor_set_throttling) (struct acpi_processor * pr,
					      int state);

	u32 address;
	u8 duty_offset;
	u8 duty_width;
	struct acpi_processor_tx states[ACPI_PROCESSOR_MAX_THROTTLING];
};

/* Limit Interface */

struct acpi_processor_lx {
	int px;			/* performace state */
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

	/* the _PDC objects for this processor, if any */
	struct acpi_object_list *pdc;
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

extern int acpi_processor_preregister_performance(struct
						  acpi_processor_performance
						  *performance);

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
extern struct acpi_processor *processors[NR_CPUS];
extern struct acpi_processor_errata errata;

void arch_acpi_processor_init_pdc(struct acpi_processor *pr);

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
int acpi_processor_ppc_has_changed(struct acpi_processor *pr);
#else
static inline void acpi_processor_ppc_init(void)
{
	return;
}
static inline void acpi_processor_ppc_exit(void)
{
	return;
}
static inline int acpi_processor_ppc_has_changed(struct acpi_processor *pr)
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
#endif				/* CONFIG_CPU_FREQ */

/* in processor_throttling.c */
int acpi_processor_get_throttling_info(struct acpi_processor *pr);
extern int acpi_processor_set_throttling(struct acpi_processor *pr, int state);
extern struct file_operations acpi_processor_throttling_fops;

/* in processor_idle.c */
int acpi_processor_power_init(struct acpi_processor *pr,
			      struct acpi_device *device);
int acpi_processor_cst_has_changed(struct acpi_processor *pr);
int acpi_processor_power_exit(struct acpi_processor *pr,
			      struct acpi_device *device);

/* in processor_thermal.c */
int acpi_processor_get_limit_info(struct acpi_processor *pr);
extern struct file_operations acpi_processor_limit_fops;

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
