// SPDX-License-Identifier: GPL-2.0-only
/*
 * turbostat -- show CPU frequency and C-state residency
 * on modern Intel and AMD processors.
 *
 * Copyright (c) 2022 Intel Corporation.
 * Len Brown <len.brown@intel.com>
 */

#define _GNU_SOURCE
#include MSRHEADER
#include INTEL_FAMILY_HEADER
#include <stdarg.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <stdlib.h>
#include <getopt.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <sched.h>
#include <time.h>
#include <cpuid.h>
#include <sys/capability.h>
#include <errno.h>
#include <math.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <stdbool.h>

#define UNUSED(x) (void)(x)

/*
 * This list matches the column headers, except
 * 1. built-in only, the sysfs counters are not here -- we learn of those at run-time
 * 2. Core and CPU are moved to the end, we can't have strings that contain them
 *    matching on them for --show and --hide.
 */

/*
 * buffer size used by sscanf() for added column names
 * Usually truncated to 7 characters, but also handles 18 columns for raw 64-bit counters
 */
#define	NAME_BYTES 20
#define PATH_BYTES 128

enum counter_scope { SCOPE_CPU, SCOPE_CORE, SCOPE_PACKAGE };
enum counter_type { COUNTER_ITEMS, COUNTER_CYCLES, COUNTER_SECONDS, COUNTER_USEC };
enum counter_format { FORMAT_RAW, FORMAT_DELTA, FORMAT_PERCENT };

struct msr_counter {
	unsigned int msr_num;
	char name[NAME_BYTES];
	char path[PATH_BYTES];
	unsigned int width;
	enum counter_type type;
	enum counter_format format;
	struct msr_counter *next;
	unsigned int flags;
#define	FLAGS_HIDE	(1 << 0)
#define	FLAGS_SHOW	(1 << 1)
#define	SYSFS_PERCPU	(1 << 1)
};

struct msr_counter bic[] = {
	{ 0x0, "usec", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Time_Of_Day_Seconds", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Package", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Node", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Avg_MHz", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Busy%", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Bzy_MHz", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "TSC_MHz", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "IRQ", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "SMI", "", 32, 0, FORMAT_DELTA, NULL, 0 },
	{ 0x0, "sysfs", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU%c1", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU%c3", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU%c6", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU%c7", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "ThreadC", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "CoreTmp", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "CoreCnt", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "PkgTmp", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "GFX%rc6", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "GFXMHz", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc2", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc3", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc6", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc7", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc8", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc9", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Pk%pc10", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU%LPI", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "SYS%LPI", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "PkgWatt", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "CorWatt", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "GFXWatt", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "PkgCnt", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "RAMWatt", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "PKG_%", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "RAM_%", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg_J", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Cor_J", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "GFX_J", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "RAM_J", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Mod%c6", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Totl%C0", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Any%C0", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "GFX%C0", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "CPUGFX%", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Core", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "APIC", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "X2APIC", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "Die", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "GFXAMHz", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "IPC", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "CoreThr", "", 0, 0, 0, NULL, 0 },
	{ 0x0, "UncMHz", "", 0, 0, 0, NULL, 0 },
};

#define MAX_BIC (sizeof(bic) / sizeof(struct msr_counter))
#define	BIC_USEC	(1ULL << 0)
#define	BIC_TOD		(1ULL << 1)
#define	BIC_Package	(1ULL << 2)
#define	BIC_Node	(1ULL << 3)
#define	BIC_Avg_MHz	(1ULL << 4)
#define	BIC_Busy	(1ULL << 5)
#define	BIC_Bzy_MHz	(1ULL << 6)
#define	BIC_TSC_MHz	(1ULL << 7)
#define	BIC_IRQ		(1ULL << 8)
#define	BIC_SMI		(1ULL << 9)
#define	BIC_sysfs	(1ULL << 10)
#define	BIC_CPU_c1	(1ULL << 11)
#define	BIC_CPU_c3	(1ULL << 12)
#define	BIC_CPU_c6	(1ULL << 13)
#define	BIC_CPU_c7	(1ULL << 14)
#define	BIC_ThreadC	(1ULL << 15)
#define	BIC_CoreTmp	(1ULL << 16)
#define	BIC_CoreCnt	(1ULL << 17)
#define	BIC_PkgTmp	(1ULL << 18)
#define	BIC_GFX_rc6	(1ULL << 19)
#define	BIC_GFXMHz	(1ULL << 20)
#define	BIC_Pkgpc2	(1ULL << 21)
#define	BIC_Pkgpc3	(1ULL << 22)
#define	BIC_Pkgpc6	(1ULL << 23)
#define	BIC_Pkgpc7	(1ULL << 24)
#define	BIC_Pkgpc8	(1ULL << 25)
#define	BIC_Pkgpc9	(1ULL << 26)
#define	BIC_Pkgpc10	(1ULL << 27)
#define BIC_CPU_LPI	(1ULL << 28)
#define BIC_SYS_LPI	(1ULL << 29)
#define	BIC_PkgWatt	(1ULL << 30)
#define	BIC_CorWatt	(1ULL << 31)
#define	BIC_GFXWatt	(1ULL << 32)
#define	BIC_PkgCnt	(1ULL << 33)
#define	BIC_RAMWatt	(1ULL << 34)
#define	BIC_PKG__	(1ULL << 35)
#define	BIC_RAM__	(1ULL << 36)
#define	BIC_Pkg_J	(1ULL << 37)
#define	BIC_Cor_J	(1ULL << 38)
#define	BIC_GFX_J	(1ULL << 39)
#define	BIC_RAM_J	(1ULL << 40)
#define	BIC_Mod_c6	(1ULL << 41)
#define	BIC_Totl_c0	(1ULL << 42)
#define	BIC_Any_c0	(1ULL << 43)
#define	BIC_GFX_c0	(1ULL << 44)
#define	BIC_CPUGFX	(1ULL << 45)
#define	BIC_Core	(1ULL << 46)
#define	BIC_CPU		(1ULL << 47)
#define	BIC_APIC	(1ULL << 48)
#define	BIC_X2APIC	(1ULL << 49)
#define	BIC_Die		(1ULL << 50)
#define	BIC_GFXACTMHz	(1ULL << 51)
#define	BIC_IPC		(1ULL << 52)
#define	BIC_CORE_THROT_CNT	(1ULL << 53)
#define	BIC_UNCORE_MHZ		(1ULL << 54)

#define BIC_TOPOLOGY (BIC_Package | BIC_Node | BIC_CoreCnt | BIC_PkgCnt | BIC_Core | BIC_CPU | BIC_Die )
#define BIC_THERMAL_PWR ( BIC_CoreTmp | BIC_PkgTmp | BIC_PkgWatt | BIC_CorWatt | BIC_GFXWatt | BIC_RAMWatt | BIC_PKG__ | BIC_RAM__)
#define BIC_FREQUENCY ( BIC_Avg_MHz | BIC_Busy | BIC_Bzy_MHz | BIC_TSC_MHz | BIC_GFXMHz | BIC_GFXACTMHz | BIC_UNCORE_MHZ)
#define BIC_IDLE ( BIC_sysfs | BIC_CPU_c1 | BIC_CPU_c3 | BIC_CPU_c6 | BIC_CPU_c7 | BIC_GFX_rc6 | BIC_Pkgpc2 | BIC_Pkgpc3 | BIC_Pkgpc6 | BIC_Pkgpc7 | BIC_Pkgpc8 | BIC_Pkgpc9 | BIC_Pkgpc10 | BIC_CPU_LPI | BIC_SYS_LPI | BIC_Mod_c6 | BIC_Totl_c0 | BIC_Any_c0 | BIC_GFX_c0 | BIC_CPUGFX)
#define BIC_OTHER ( BIC_IRQ | BIC_SMI | BIC_ThreadC | BIC_CoreTmp | BIC_IPC)

#define BIC_DISABLED_BY_DEFAULT	(BIC_USEC | BIC_TOD | BIC_APIC | BIC_X2APIC)

unsigned long long bic_enabled = (0xFFFFFFFFFFFFFFFFULL & ~BIC_DISABLED_BY_DEFAULT);
unsigned long long bic_present = BIC_USEC | BIC_TOD | BIC_sysfs | BIC_APIC | BIC_X2APIC;

#define DO_BIC(COUNTER_NAME) (bic_enabled & bic_present & COUNTER_NAME)
#define DO_BIC_READ(COUNTER_NAME) (bic_present & COUNTER_NAME)
#define ENABLE_BIC(COUNTER_NAME) (bic_enabled |= COUNTER_NAME)
#define BIC_PRESENT(COUNTER_BIT) (bic_present |= COUNTER_BIT)
#define BIC_NOT_PRESENT(COUNTER_BIT) (bic_present &= ~COUNTER_BIT)
#define BIC_IS_ENABLED(COUNTER_BIT) (bic_enabled & COUNTER_BIT)

char *proc_stat = "/proc/stat";
FILE *outf;
int *fd_percpu;
int *fd_instr_count_percpu;
struct timeval interval_tv = { 5, 0 };
struct timespec interval_ts = { 5, 0 };

/* Save original CPU model */
unsigned int model_orig;

unsigned int num_iterations;
unsigned int header_iterations;
unsigned int debug;
unsigned int quiet;
unsigned int shown;
unsigned int sums_need_wide_columns;
unsigned int rapl_joules;
unsigned int summary_only;
unsigned int list_header_only;
unsigned int dump_only;
unsigned int do_snb_cstates;
unsigned int do_knl_cstates;
unsigned int do_slm_cstates;
unsigned int use_c1_residency_msr;
unsigned int has_aperf;
unsigned int has_epb;
unsigned int has_turbo;
unsigned int is_hybrid;
unsigned int do_irtl_snb;
unsigned int do_irtl_hsw;
unsigned int units = 1000000;	/* MHz etc */
unsigned int genuine_intel;
unsigned int authentic_amd;
unsigned int hygon_genuine;
unsigned int max_level, max_extended_level;
unsigned int has_invariant_tsc;
unsigned int do_nhm_platform_info;
unsigned int no_MSR_MISC_PWR_MGMT;
unsigned int aperf_mperf_multiplier = 1;
double bclk;
double base_hz;
unsigned int has_base_hz;
double tsc_tweak = 1.0;
unsigned int show_pkg_only;
unsigned int show_core_only;
char *output_buffer, *outp;
unsigned int do_rapl;
unsigned int do_dts;
unsigned int do_ptm;
unsigned int do_ipc;
unsigned long long gfx_cur_rc6_ms;
unsigned long long cpuidle_cur_cpu_lpi_us;
unsigned long long cpuidle_cur_sys_lpi_us;
unsigned int gfx_cur_mhz;
unsigned int gfx_act_mhz;
unsigned int tj_max;
unsigned int tj_max_override;
int tcc_offset_bits;
double rapl_power_units, rapl_time_units;
double rapl_dram_energy_units, rapl_energy_units;
double rapl_joule_counter_range;
unsigned int do_core_perf_limit_reasons;
unsigned int has_automatic_cstate_conversion;
unsigned int dis_cstate_prewake;
unsigned int do_gfx_perf_limit_reasons;
unsigned int do_ring_perf_limit_reasons;
unsigned int crystal_hz;
unsigned long long tsc_hz;
int base_cpu;
double discover_bclk(unsigned int family, unsigned int model);
unsigned int has_hwp;		/* IA32_PM_ENABLE, IA32_HWP_CAPABILITIES */
			/* IA32_HWP_REQUEST, IA32_HWP_STATUS */
unsigned int has_hwp_notify;	/* IA32_HWP_INTERRUPT */
unsigned int has_hwp_activity_window;	/* IA32_HWP_REQUEST[bits 41:32] */
unsigned int has_hwp_epp;	/* IA32_HWP_REQUEST[bits 31:24] */
unsigned int has_hwp_pkg;	/* IA32_HWP_REQUEST_PKG */
unsigned int has_misc_feature_control;
unsigned int first_counter_read = 1;
int ignore_stdin;

#define RAPL_PKG		(1 << 0)
					/* 0x610 MSR_PKG_POWER_LIMIT */
					/* 0x611 MSR_PKG_ENERGY_STATUS */
#define RAPL_PKG_PERF_STATUS	(1 << 1)
					/* 0x613 MSR_PKG_PERF_STATUS */
#define RAPL_PKG_POWER_INFO	(1 << 2)
					/* 0x614 MSR_PKG_POWER_INFO */

#define RAPL_DRAM		(1 << 3)
					/* 0x618 MSR_DRAM_POWER_LIMIT */
					/* 0x619 MSR_DRAM_ENERGY_STATUS */
#define RAPL_DRAM_PERF_STATUS	(1 << 4)
					/* 0x61b MSR_DRAM_PERF_STATUS */
#define RAPL_DRAM_POWER_INFO	(1 << 5)
					/* 0x61c MSR_DRAM_POWER_INFO */

#define RAPL_CORES_POWER_LIMIT	(1 << 6)
					/* 0x638 MSR_PP0_POWER_LIMIT */
#define RAPL_CORE_POLICY	(1 << 7)
					/* 0x63a MSR_PP0_POLICY */

#define RAPL_GFX		(1 << 8)
					/* 0x640 MSR_PP1_POWER_LIMIT */
					/* 0x641 MSR_PP1_ENERGY_STATUS */
					/* 0x642 MSR_PP1_POLICY */

#define RAPL_CORES_ENERGY_STATUS	(1 << 9)
					/* 0x639 MSR_PP0_ENERGY_STATUS */
#define RAPL_PER_CORE_ENERGY	(1 << 10)
					/* Indicates cores energy collection is per-core,
					 * not per-package. */
#define RAPL_AMD_F17H		(1 << 11)
					/* 0xc0010299 MSR_RAPL_PWR_UNIT */
					/* 0xc001029a MSR_CORE_ENERGY_STAT */
					/* 0xc001029b MSR_PKG_ENERGY_STAT */
#define RAPL_CORES (RAPL_CORES_ENERGY_STATUS | RAPL_CORES_POWER_LIMIT)
#define	TJMAX_DEFAULT	100

/* MSRs that are not yet in the kernel-provided header. */
#define MSR_RAPL_PWR_UNIT	0xc0010299
#define MSR_CORE_ENERGY_STAT	0xc001029a
#define MSR_PKG_ENERGY_STAT	0xc001029b

#define MAX(a, b) ((a) > (b) ? (a) : (b))

int backwards_count;
char *progname;

#define CPU_SUBSET_MAXCPUS	1024	/* need to use before probe... */
cpu_set_t *cpu_present_set, *cpu_affinity_set, *cpu_subset;
size_t cpu_present_setsize, cpu_affinity_setsize, cpu_subset_size;
#define MAX_ADDED_COUNTERS 8
#define MAX_ADDED_THREAD_COUNTERS 24
#define BITMASK_SIZE 32

struct thread_data {
	struct timeval tv_begin;
	struct timeval tv_end;
	struct timeval tv_delta;
	unsigned long long tsc;
	unsigned long long aperf;
	unsigned long long mperf;
	unsigned long long c1;
	unsigned long long instr_count;
	unsigned long long irq_count;
	unsigned int smi_count;
	unsigned int cpu_id;
	unsigned int apic_id;
	unsigned int x2apic_id;
	unsigned int flags;
	bool is_atom;
#define CPU_IS_FIRST_THREAD_IN_CORE	0x2
#define CPU_IS_FIRST_CORE_IN_PACKAGE	0x4
	unsigned long long counter[MAX_ADDED_THREAD_COUNTERS];
} *thread_even, *thread_odd;

struct core_data {
	unsigned long long c3;
	unsigned long long c6;
	unsigned long long c7;
	unsigned long long mc6_us;	/* duplicate as per-core for now, even though per module */
	unsigned int core_temp_c;
	unsigned int core_energy;	/* MSR_CORE_ENERGY_STAT */
	unsigned int core_id;
	unsigned long long core_throt_cnt;
	unsigned long long counter[MAX_ADDED_COUNTERS];
} *core_even, *core_odd;

struct pkg_data {
	unsigned long long pc2;
	unsigned long long pc3;
	unsigned long long pc6;
	unsigned long long pc7;
	unsigned long long pc8;
	unsigned long long pc9;
	unsigned long long pc10;
	unsigned long long cpu_lpi;
	unsigned long long sys_lpi;
	unsigned long long pkg_wtd_core_c0;
	unsigned long long pkg_any_core_c0;
	unsigned long long pkg_any_gfxe_c0;
	unsigned long long pkg_both_core_gfxe_c0;
	long long gfx_rc6_ms;
	unsigned int gfx_mhz;
	unsigned int gfx_act_mhz;
	unsigned int package_id;
	unsigned long long energy_pkg;	/* MSR_PKG_ENERGY_STATUS */
	unsigned long long energy_dram;	/* MSR_DRAM_ENERGY_STATUS */
	unsigned long long energy_cores;	/* MSR_PP0_ENERGY_STATUS */
	unsigned long long energy_gfx;	/* MSR_PP1_ENERGY_STATUS */
	unsigned long long rapl_pkg_perf_status;	/* MSR_PKG_PERF_STATUS */
	unsigned long long rapl_dram_perf_status;	/* MSR_DRAM_PERF_STATUS */
	unsigned int pkg_temp_c;
	unsigned int uncore_mhz;
	unsigned long long counter[MAX_ADDED_COUNTERS];
} *package_even, *package_odd;

#define ODD_COUNTERS thread_odd, core_odd, package_odd
#define EVEN_COUNTERS thread_even, core_even, package_even

#define GET_THREAD(thread_base, thread_no, core_no, node_no, pkg_no)	      \
	((thread_base) +						      \
	 ((pkg_no) *							      \
	  topo.nodes_per_pkg * topo.cores_per_node * topo.threads_per_core) + \
	 ((node_no) * topo.cores_per_node * topo.threads_per_core) +	      \
	 ((core_no) * topo.threads_per_core) +				      \
	 (thread_no))

#define GET_CORE(core_base, core_no, node_no, pkg_no)			\
	((core_base) +							\
	 ((pkg_no) *  topo.nodes_per_pkg * topo.cores_per_node) +	\
	 ((node_no) * topo.cores_per_node) +				\
	 (core_no))

#define GET_PKG(pkg_base, pkg_no) (pkg_base + pkg_no)

/*
 * The accumulated sum of MSR is defined as a monotonic
 * increasing MSR, it will be accumulated periodically,
 * despite its register's bit width.
 */
enum {
	IDX_PKG_ENERGY,
	IDX_DRAM_ENERGY,
	IDX_PP0_ENERGY,
	IDX_PP1_ENERGY,
	IDX_PKG_PERF,
	IDX_DRAM_PERF,
	IDX_COUNT,
};

int get_msr_sum(int cpu, off_t offset, unsigned long long *msr);

struct msr_sum_array {
	/* get_msr_sum() = sum + (get_msr() - last) */
	struct {
		/*The accumulated MSR value is updated by the timer */
		unsigned long long sum;
		/*The MSR footprint recorded in last timer */
		unsigned long long last;
	} entries[IDX_COUNT];
};

/* The percpu MSR sum array.*/
struct msr_sum_array *per_cpu_msr_sum;

off_t idx_to_offset(int idx)
{
	off_t offset;

	switch (idx) {
	case IDX_PKG_ENERGY:
		if (do_rapl & RAPL_AMD_F17H)
			offset = MSR_PKG_ENERGY_STAT;
		else
			offset = MSR_PKG_ENERGY_STATUS;
		break;
	case IDX_DRAM_ENERGY:
		offset = MSR_DRAM_ENERGY_STATUS;
		break;
	case IDX_PP0_ENERGY:
		offset = MSR_PP0_ENERGY_STATUS;
		break;
	case IDX_PP1_ENERGY:
		offset = MSR_PP1_ENERGY_STATUS;
		break;
	case IDX_PKG_PERF:
		offset = MSR_PKG_PERF_STATUS;
		break;
	case IDX_DRAM_PERF:
		offset = MSR_DRAM_PERF_STATUS;
		break;
	default:
		offset = -1;
	}
	return offset;
}

int offset_to_idx(off_t offset)
{
	int idx;

	switch (offset) {
	case MSR_PKG_ENERGY_STATUS:
	case MSR_PKG_ENERGY_STAT:
		idx = IDX_PKG_ENERGY;
		break;
	case MSR_DRAM_ENERGY_STATUS:
		idx = IDX_DRAM_ENERGY;
		break;
	case MSR_PP0_ENERGY_STATUS:
		idx = IDX_PP0_ENERGY;
		break;
	case MSR_PP1_ENERGY_STATUS:
		idx = IDX_PP1_ENERGY;
		break;
	case MSR_PKG_PERF_STATUS:
		idx = IDX_PKG_PERF;
		break;
	case MSR_DRAM_PERF_STATUS:
		idx = IDX_DRAM_PERF;
		break;
	default:
		idx = -1;
	}
	return idx;
}

int idx_valid(int idx)
{
	switch (idx) {
	case IDX_PKG_ENERGY:
		return do_rapl & (RAPL_PKG | RAPL_AMD_F17H);
	case IDX_DRAM_ENERGY:
		return do_rapl & RAPL_DRAM;
	case IDX_PP0_ENERGY:
		return do_rapl & RAPL_CORES_ENERGY_STATUS;
	case IDX_PP1_ENERGY:
		return do_rapl & RAPL_GFX;
	case IDX_PKG_PERF:
		return do_rapl & RAPL_PKG_PERF_STATUS;
	case IDX_DRAM_PERF:
		return do_rapl & RAPL_DRAM_PERF_STATUS;
	default:
		return 0;
	}
}

struct sys_counters {
	unsigned int added_thread_counters;
	unsigned int added_core_counters;
	unsigned int added_package_counters;
	struct msr_counter *tp;
	struct msr_counter *cp;
	struct msr_counter *pp;
} sys;

struct system_summary {
	struct thread_data threads;
	struct core_data cores;
	struct pkg_data packages;
} average;

struct cpu_topology {
	int physical_package_id;
	int die_id;
	int logical_cpu_id;
	int physical_node_id;
	int logical_node_id;	/* 0-based count within the package */
	int physical_core_id;
	int thread_id;
	cpu_set_t *put_ids;	/* Processing Unit/Thread IDs */
} *cpus;

struct topo_params {
	int num_packages;
	int num_die;
	int num_cpus;
	int num_cores;
	int max_cpu_num;
	int max_node_num;
	int nodes_per_pkg;
	int cores_per_node;
	int threads_per_core;
} topo;

struct timeval tv_even, tv_odd, tv_delta;

int *irq_column_2_cpu;		/* /proc/interrupts column numbers */
int *irqs_per_cpu;		/* indexed by cpu_num */

void setup_all_buffers(void);

char *sys_lpi_file;
char *sys_lpi_file_sysfs = "/sys/devices/system/cpu/cpuidle/low_power_idle_system_residency_us";
char *sys_lpi_file_debugfs = "/sys/kernel/debug/pmc_core/slp_s0_residency_usec";

int cpu_is_not_present(int cpu)
{
	return !CPU_ISSET_S(cpu, cpu_present_setsize, cpu_present_set);
}

/*
 * run func(thread, core, package) in topology order
 * skip non-present cpus
 */

int for_all_cpus(int (func) (struct thread_data *, struct core_data *, struct pkg_data *),
		 struct thread_data *thread_base, struct core_data *core_base, struct pkg_data *pkg_base)
{
	int retval, pkg_no, core_no, thread_no, node_no;

	for (pkg_no = 0; pkg_no < topo.num_packages; ++pkg_no) {
		for (node_no = 0; node_no < topo.nodes_per_pkg; node_no++) {
			for (core_no = 0; core_no < topo.cores_per_node; ++core_no) {
				for (thread_no = 0; thread_no < topo.threads_per_core; ++thread_no) {
					struct thread_data *t;
					struct core_data *c;
					struct pkg_data *p;

					t = GET_THREAD(thread_base, thread_no, core_no, node_no, pkg_no);

					if (cpu_is_not_present(t->cpu_id))
						continue;

					c = GET_CORE(core_base, core_no, node_no, pkg_no);
					p = GET_PKG(pkg_base, pkg_no);

					retval = func(t, c, p);
					if (retval)
						return retval;
				}
			}
		}
	}
	return 0;
}

int cpu_migrate(int cpu)
{
	CPU_ZERO_S(cpu_affinity_setsize, cpu_affinity_set);
	CPU_SET_S(cpu, cpu_affinity_setsize, cpu_affinity_set);
	if (sched_setaffinity(0, cpu_affinity_setsize, cpu_affinity_set) == -1)
		return -1;
	else
		return 0;
}

int get_msr_fd(int cpu)
{
	char pathname[32];
	int fd;

	fd = fd_percpu[cpu];

	if (fd)
		return fd;

	sprintf(pathname, "/dev/cpu/%d/msr", cpu);
	fd = open(pathname, O_RDONLY);
	if (fd < 0)
		err(-1, "%s open failed, try chown or chmod +r /dev/cpu/*/msr, or run as root", pathname);

	fd_percpu[cpu] = fd;

	return fd;
}

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static int perf_instr_count_open(int cpu_num)
{
	struct perf_event_attr pea;
	int fd;

	memset(&pea, 0, sizeof(struct perf_event_attr));
	pea.type = PERF_TYPE_HARDWARE;
	pea.size = sizeof(struct perf_event_attr);
	pea.config = PERF_COUNT_HW_INSTRUCTIONS;

	/* counter for cpu_num, including user + kernel and all processes */
	fd = perf_event_open(&pea, -1, cpu_num, -1, 0);
	if (fd == -1) {
		warn("cpu%d: perf instruction counter", cpu_num);
		BIC_NOT_PRESENT(BIC_IPC);
	}

	return fd;
}

int get_instr_count_fd(int cpu)
{
	if (fd_instr_count_percpu[cpu])
		return fd_instr_count_percpu[cpu];

	fd_instr_count_percpu[cpu] = perf_instr_count_open(cpu);

	return fd_instr_count_percpu[cpu];
}

int get_msr(int cpu, off_t offset, unsigned long long *msr)
{
	ssize_t retval;

	retval = pread(get_msr_fd(cpu), msr, sizeof(*msr), offset);

	if (retval != sizeof *msr)
		err(-1, "cpu%d: msr offset 0x%llx read failed", cpu, (unsigned long long)offset);

	return 0;
}

#define MAX_DEFERRED 16
char *deferred_add_names[MAX_DEFERRED];
char *deferred_skip_names[MAX_DEFERRED];
int deferred_add_index;
int deferred_skip_index;

/*
 * HIDE_LIST - hide this list of counters, show the rest [default]
 * SHOW_LIST - show this list of counters, hide the rest
 */
enum show_hide_mode { SHOW_LIST, HIDE_LIST } global_show_hide_mode = HIDE_LIST;

void help(void)
{
	fprintf(outf,
		"Usage: turbostat [OPTIONS][(--interval seconds) | COMMAND ...]\n"
		"\n"
		"Turbostat forks the specified COMMAND and prints statistics\n"
		"when COMMAND completes.\n"
		"If no COMMAND is specified, turbostat wakes every 5-seconds\n"
		"to print statistics, until interrupted.\n"
		"  -a, --add	add a counter\n"
		"		  eg. --add msr0x10,u64,cpu,delta,MY_TSC\n"
		"  -c, --cpu	cpu-set	limit output to summary plus cpu-set:\n"
		"		  {core | package | j,k,l..m,n-p }\n"
		"  -d, --debug	displays usec, Time_Of_Day_Seconds and more debugging\n"
		"  -D, --Dump	displays the raw counter values\n"
		"  -e, --enable	[all | column]\n"
		"		shows all or the specified disabled column\n"
		"  -H, --hide [column|column,column,...]\n"
		"		hide the specified column(s)\n"
		"  -i, --interval sec.subsec\n"
		"		Override default 5-second measurement interval\n"
		"  -J, --Joules	displays energy in Joules instead of Watts\n"
		"  -l, --list	list column headers only\n"
		"  -n, --num_iterations num\n"
		"		number of the measurement iterations\n"
		"  -N, --header_iterations num\n"
		"		print header every num iterations\n"
		"  -o, --out file\n"
		"		create or truncate \"file\" for all output\n"
		"  -q, --quiet	skip decoding system configuration header\n"
		"  -s, --show [column|column,column,...]\n"
		"		show only the specified column(s)\n"
		"  -S, --Summary\n"
		"		limits output to 1-line system summary per interval\n"
		"  -T, --TCC temperature\n"
		"		sets the Thermal Control Circuit temperature in\n"
		"		  degrees Celsius\n"
		"  -h, --help	print this help message\n"
		"  -v, --version	print version information\n" "\n" "For more help, run \"man turbostat\"\n");
}

/*
 * bic_lookup
 * for all the strings in comma separate name_list,
 * set the approprate bit in return value.
 */
unsigned long long bic_lookup(char *name_list, enum show_hide_mode mode)
{
	unsigned int i;
	unsigned long long retval = 0;

	while (name_list) {
		char *comma;

		comma = strchr(name_list, ',');

		if (comma)
			*comma = '\0';

		for (i = 0; i < MAX_BIC; ++i) {
			if (!strcmp(name_list, bic[i].name)) {
				retval |= (1ULL << i);
				break;
			}
			if (!strcmp(name_list, "all")) {
				retval |= ~0;
				break;
			} else if (!strcmp(name_list, "topology")) {
				retval |= BIC_TOPOLOGY;
				break;
			} else if (!strcmp(name_list, "power")) {
				retval |= BIC_THERMAL_PWR;
				break;
			} else if (!strcmp(name_list, "idle")) {
				retval |= BIC_IDLE;
				break;
			} else if (!strcmp(name_list, "frequency")) {
				retval |= BIC_FREQUENCY;
				break;
			} else if (!strcmp(name_list, "other")) {
				retval |= BIC_OTHER;
				break;
			}

		}
		if (i == MAX_BIC) {
			if (mode == SHOW_LIST) {
				deferred_add_names[deferred_add_index++] = name_list;
				if (deferred_add_index >= MAX_DEFERRED) {
					fprintf(stderr, "More than max %d un-recognized --add options '%s'\n",
						MAX_DEFERRED, name_list);
					help();
					exit(1);
				}
			} else {
				deferred_skip_names[deferred_skip_index++] = name_list;
				if (debug)
					fprintf(stderr, "deferred \"%s\"\n", name_list);
				if (deferred_skip_index >= MAX_DEFERRED) {
					fprintf(stderr, "More than max %d un-recognized --skip options '%s'\n",
						MAX_DEFERRED, name_list);
					help();
					exit(1);
				}
			}
		}

		name_list = comma;
		if (name_list)
			name_list++;

	}
	return retval;
}

void print_header(char *delim)
{
	struct msr_counter *mp;
	int printed = 0;

	if (DO_BIC(BIC_USEC))
		outp += sprintf(outp, "%susec", (printed++ ? delim : ""));
	if (DO_BIC(BIC_TOD))
		outp += sprintf(outp, "%sTime_Of_Day_Seconds", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Package))
		outp += sprintf(outp, "%sPackage", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Die))
		outp += sprintf(outp, "%sDie", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Node))
		outp += sprintf(outp, "%sNode", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Core))
		outp += sprintf(outp, "%sCore", (printed++ ? delim : ""));
	if (DO_BIC(BIC_CPU))
		outp += sprintf(outp, "%sCPU", (printed++ ? delim : ""));
	if (DO_BIC(BIC_APIC))
		outp += sprintf(outp, "%sAPIC", (printed++ ? delim : ""));
	if (DO_BIC(BIC_X2APIC))
		outp += sprintf(outp, "%sX2APIC", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Avg_MHz))
		outp += sprintf(outp, "%sAvg_MHz", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Busy))
		outp += sprintf(outp, "%sBusy%%", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Bzy_MHz))
		outp += sprintf(outp, "%sBzy_MHz", (printed++ ? delim : ""));
	if (DO_BIC(BIC_TSC_MHz))
		outp += sprintf(outp, "%sTSC_MHz", (printed++ ? delim : ""));

	if (DO_BIC(BIC_IPC))
		outp += sprintf(outp, "%sIPC", (printed++ ? delim : ""));

	if (DO_BIC(BIC_IRQ)) {
		if (sums_need_wide_columns)
			outp += sprintf(outp, "%s     IRQ", (printed++ ? delim : ""));
		else
			outp += sprintf(outp, "%sIRQ", (printed++ ? delim : ""));
	}

	if (DO_BIC(BIC_SMI))
		outp += sprintf(outp, "%sSMI", (printed++ ? delim : ""));

	for (mp = sys.tp; mp; mp = mp->next) {

		if (mp->format == FORMAT_RAW) {
			if (mp->width == 64)
				outp += sprintf(outp, "%s%18.18s", (printed++ ? delim : ""), mp->name);
			else
				outp += sprintf(outp, "%s%10.10s", (printed++ ? delim : ""), mp->name);
		} else {
			if ((mp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8s", (printed++ ? delim : ""), mp->name);
			else
				outp += sprintf(outp, "%s%s", (printed++ ? delim : ""), mp->name);
		}
	}

	if (DO_BIC(BIC_CPU_c1))
		outp += sprintf(outp, "%sCPU%%c1", (printed++ ? delim : ""));
	if (DO_BIC(BIC_CPU_c3))
		outp += sprintf(outp, "%sCPU%%c3", (printed++ ? delim : ""));
	if (DO_BIC(BIC_CPU_c6))
		outp += sprintf(outp, "%sCPU%%c6", (printed++ ? delim : ""));
	if (DO_BIC(BIC_CPU_c7))
		outp += sprintf(outp, "%sCPU%%c7", (printed++ ? delim : ""));

	if (DO_BIC(BIC_Mod_c6))
		outp += sprintf(outp, "%sMod%%c6", (printed++ ? delim : ""));

	if (DO_BIC(BIC_CoreTmp))
		outp += sprintf(outp, "%sCoreTmp", (printed++ ? delim : ""));

	if (DO_BIC(BIC_CORE_THROT_CNT))
		outp += sprintf(outp, "%sCoreThr", (printed++ ? delim : ""));

	if (do_rapl && !rapl_joules) {
		if (DO_BIC(BIC_CorWatt) && (do_rapl & RAPL_PER_CORE_ENERGY))
			outp += sprintf(outp, "%sCorWatt", (printed++ ? delim : ""));
	} else if (do_rapl && rapl_joules) {
		if (DO_BIC(BIC_Cor_J) && (do_rapl & RAPL_PER_CORE_ENERGY))
			outp += sprintf(outp, "%sCor_J", (printed++ ? delim : ""));
	}

	for (mp = sys.cp; mp; mp = mp->next) {
		if (mp->format == FORMAT_RAW) {
			if (mp->width == 64)
				outp += sprintf(outp, "%s%18.18s", delim, mp->name);
			else
				outp += sprintf(outp, "%s%10.10s", delim, mp->name);
		} else {
			if ((mp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8s", delim, mp->name);
			else
				outp += sprintf(outp, "%s%s", delim, mp->name);
		}
	}

	if (DO_BIC(BIC_PkgTmp))
		outp += sprintf(outp, "%sPkgTmp", (printed++ ? delim : ""));

	if (DO_BIC(BIC_GFX_rc6))
		outp += sprintf(outp, "%sGFX%%rc6", (printed++ ? delim : ""));

	if (DO_BIC(BIC_GFXMHz))
		outp += sprintf(outp, "%sGFXMHz", (printed++ ? delim : ""));

	if (DO_BIC(BIC_GFXACTMHz))
		outp += sprintf(outp, "%sGFXAMHz", (printed++ ? delim : ""));

	if (DO_BIC(BIC_Totl_c0))
		outp += sprintf(outp, "%sTotl%%C0", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Any_c0))
		outp += sprintf(outp, "%sAny%%C0", (printed++ ? delim : ""));
	if (DO_BIC(BIC_GFX_c0))
		outp += sprintf(outp, "%sGFX%%C0", (printed++ ? delim : ""));
	if (DO_BIC(BIC_CPUGFX))
		outp += sprintf(outp, "%sCPUGFX%%", (printed++ ? delim : ""));

	if (DO_BIC(BIC_Pkgpc2))
		outp += sprintf(outp, "%sPkg%%pc2", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Pkgpc3))
		outp += sprintf(outp, "%sPkg%%pc3", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Pkgpc6))
		outp += sprintf(outp, "%sPkg%%pc6", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Pkgpc7))
		outp += sprintf(outp, "%sPkg%%pc7", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Pkgpc8))
		outp += sprintf(outp, "%sPkg%%pc8", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Pkgpc9))
		outp += sprintf(outp, "%sPkg%%pc9", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Pkgpc10))
		outp += sprintf(outp, "%sPk%%pc10", (printed++ ? delim : ""));
	if (DO_BIC(BIC_CPU_LPI))
		outp += sprintf(outp, "%sCPU%%LPI", (printed++ ? delim : ""));
	if (DO_BIC(BIC_SYS_LPI))
		outp += sprintf(outp, "%sSYS%%LPI", (printed++ ? delim : ""));

	if (do_rapl && !rapl_joules) {
		if (DO_BIC(BIC_PkgWatt))
			outp += sprintf(outp, "%sPkgWatt", (printed++ ? delim : ""));
		if (DO_BIC(BIC_CorWatt) && !(do_rapl & RAPL_PER_CORE_ENERGY))
			outp += sprintf(outp, "%sCorWatt", (printed++ ? delim : ""));
		if (DO_BIC(BIC_GFXWatt))
			outp += sprintf(outp, "%sGFXWatt", (printed++ ? delim : ""));
		if (DO_BIC(BIC_RAMWatt))
			outp += sprintf(outp, "%sRAMWatt", (printed++ ? delim : ""));
		if (DO_BIC(BIC_PKG__))
			outp += sprintf(outp, "%sPKG_%%", (printed++ ? delim : ""));
		if (DO_BIC(BIC_RAM__))
			outp += sprintf(outp, "%sRAM_%%", (printed++ ? delim : ""));
	} else if (do_rapl && rapl_joules) {
		if (DO_BIC(BIC_Pkg_J))
			outp += sprintf(outp, "%sPkg_J", (printed++ ? delim : ""));
		if (DO_BIC(BIC_Cor_J) && !(do_rapl & RAPL_PER_CORE_ENERGY))
			outp += sprintf(outp, "%sCor_J", (printed++ ? delim : ""));
		if (DO_BIC(BIC_GFX_J))
			outp += sprintf(outp, "%sGFX_J", (printed++ ? delim : ""));
		if (DO_BIC(BIC_RAM_J))
			outp += sprintf(outp, "%sRAM_J", (printed++ ? delim : ""));
		if (DO_BIC(BIC_PKG__))
			outp += sprintf(outp, "%sPKG_%%", (printed++ ? delim : ""));
		if (DO_BIC(BIC_RAM__))
			outp += sprintf(outp, "%sRAM_%%", (printed++ ? delim : ""));
	}
	if (DO_BIC(BIC_UNCORE_MHZ))
		outp += sprintf(outp, "%sUncMHz", (printed++ ? delim : ""));

	for (mp = sys.pp; mp; mp = mp->next) {
		if (mp->format == FORMAT_RAW) {
			if (mp->width == 64)
				outp += sprintf(outp, "%s%18.18s", delim, mp->name);
			else
				outp += sprintf(outp, "%s%10.10s", delim, mp->name);
		} else {
			if ((mp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8s", delim, mp->name);
			else
				outp += sprintf(outp, "%s%s", delim, mp->name);
		}
	}

	outp += sprintf(outp, "\n");
}

int dump_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	int i;
	struct msr_counter *mp;

	outp += sprintf(outp, "t %p, c %p, p %p\n", t, c, p);

	if (t) {
		outp += sprintf(outp, "CPU: %d flags 0x%x\n", t->cpu_id, t->flags);
		outp += sprintf(outp, "TSC: %016llX\n", t->tsc);
		outp += sprintf(outp, "aperf: %016llX\n", t->aperf);
		outp += sprintf(outp, "mperf: %016llX\n", t->mperf);
		outp += sprintf(outp, "c1: %016llX\n", t->c1);

		if (DO_BIC(BIC_IPC))
			outp += sprintf(outp, "IPC: %lld\n", t->instr_count);

		if (DO_BIC(BIC_IRQ))
			outp += sprintf(outp, "IRQ: %lld\n", t->irq_count);
		if (DO_BIC(BIC_SMI))
			outp += sprintf(outp, "SMI: %d\n", t->smi_count);

		for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
			outp += sprintf(outp, "tADDED [%d] msr0x%x: %08llX\n", i, mp->msr_num, t->counter[i]);
		}
	}

	if (c) {
		outp += sprintf(outp, "core: %d\n", c->core_id);
		outp += sprintf(outp, "c3: %016llX\n", c->c3);
		outp += sprintf(outp, "c6: %016llX\n", c->c6);
		outp += sprintf(outp, "c7: %016llX\n", c->c7);
		outp += sprintf(outp, "DTS: %dC\n", c->core_temp_c);
		outp += sprintf(outp, "cpu_throt_count: %016llX\n", c->core_throt_cnt);
		outp += sprintf(outp, "Joules: %0X\n", c->core_energy);

		for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
			outp += sprintf(outp, "cADDED [%d] msr0x%x: %08llX\n", i, mp->msr_num, c->counter[i]);
		}
		outp += sprintf(outp, "mc6_us: %016llX\n", c->mc6_us);
	}

	if (p) {
		outp += sprintf(outp, "package: %d\n", p->package_id);

		outp += sprintf(outp, "Weighted cores: %016llX\n", p->pkg_wtd_core_c0);
		outp += sprintf(outp, "Any cores: %016llX\n", p->pkg_any_core_c0);
		outp += sprintf(outp, "Any GFX: %016llX\n", p->pkg_any_gfxe_c0);
		outp += sprintf(outp, "CPU + GFX: %016llX\n", p->pkg_both_core_gfxe_c0);

		outp += sprintf(outp, "pc2: %016llX\n", p->pc2);
		if (DO_BIC(BIC_Pkgpc3))
			outp += sprintf(outp, "pc3: %016llX\n", p->pc3);
		if (DO_BIC(BIC_Pkgpc6))
			outp += sprintf(outp, "pc6: %016llX\n", p->pc6);
		if (DO_BIC(BIC_Pkgpc7))
			outp += sprintf(outp, "pc7: %016llX\n", p->pc7);
		outp += sprintf(outp, "pc8: %016llX\n", p->pc8);
		outp += sprintf(outp, "pc9: %016llX\n", p->pc9);
		outp += sprintf(outp, "pc10: %016llX\n", p->pc10);
		outp += sprintf(outp, "cpu_lpi: %016llX\n", p->cpu_lpi);
		outp += sprintf(outp, "sys_lpi: %016llX\n", p->sys_lpi);
		outp += sprintf(outp, "Joules PKG: %0llX\n", p->energy_pkg);
		outp += sprintf(outp, "Joules COR: %0llX\n", p->energy_cores);
		outp += sprintf(outp, "Joules GFX: %0llX\n", p->energy_gfx);
		outp += sprintf(outp, "Joules RAM: %0llX\n", p->energy_dram);
		outp += sprintf(outp, "Throttle PKG: %0llX\n", p->rapl_pkg_perf_status);
		outp += sprintf(outp, "Throttle RAM: %0llX\n", p->rapl_dram_perf_status);
		outp += sprintf(outp, "PTM: %dC\n", p->pkg_temp_c);

		for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
			outp += sprintf(outp, "pADDED [%d] msr0x%x: %08llX\n", i, mp->msr_num, p->counter[i]);
		}
	}

	outp += sprintf(outp, "\n");

	return 0;
}

/*
 * column formatting convention & formats
 */
int format_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	double interval_float, tsc;
	char *fmt8;
	int i;
	struct msr_counter *mp;
	char *delim = "\t";
	int printed = 0;

	/* if showing only 1st thread in core and this isn't one, bail out */
	if (show_core_only && !(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		return 0;

	/* if showing only 1st thread in pkg and this isn't one, bail out */
	if (show_pkg_only && !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	/*if not summary line and --cpu is used */
	if ((t != &average.threads) && (cpu_subset && !CPU_ISSET_S(t->cpu_id, cpu_subset_size, cpu_subset)))
		return 0;

	if (DO_BIC(BIC_USEC)) {
		/* on each row, print how many usec each timestamp took to gather */
		struct timeval tv;

		timersub(&t->tv_end, &t->tv_begin, &tv);
		outp += sprintf(outp, "%5ld\t", tv.tv_sec * 1000000 + tv.tv_usec);
	}

	/* Time_Of_Day_Seconds: on each row, print sec.usec last timestamp taken */
	if (DO_BIC(BIC_TOD))
		outp += sprintf(outp, "%10ld.%06ld\t", t->tv_end.tv_sec, t->tv_end.tv_usec);

	interval_float = t->tv_delta.tv_sec + t->tv_delta.tv_usec / 1000000.0;

	tsc = t->tsc * tsc_tweak;

	/* topo columns, print blanks on 1st (average) line */
	if (t == &average.threads) {
		if (DO_BIC(BIC_Package))
			outp += sprintf(outp, "%s-", (printed++ ? delim : ""));
		if (DO_BIC(BIC_Die))
			outp += sprintf(outp, "%s-", (printed++ ? delim : ""));
		if (DO_BIC(BIC_Node))
			outp += sprintf(outp, "%s-", (printed++ ? delim : ""));
		if (DO_BIC(BIC_Core))
			outp += sprintf(outp, "%s-", (printed++ ? delim : ""));
		if (DO_BIC(BIC_CPU))
			outp += sprintf(outp, "%s-", (printed++ ? delim : ""));
		if (DO_BIC(BIC_APIC))
			outp += sprintf(outp, "%s-", (printed++ ? delim : ""));
		if (DO_BIC(BIC_X2APIC))
			outp += sprintf(outp, "%s-", (printed++ ? delim : ""));
	} else {
		if (DO_BIC(BIC_Package)) {
			if (p)
				outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), p->package_id);
			else
				outp += sprintf(outp, "%s-", (printed++ ? delim : ""));
		}
		if (DO_BIC(BIC_Die)) {
			if (c)
				outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), cpus[t->cpu_id].die_id);
			else
				outp += sprintf(outp, "%s-", (printed++ ? delim : ""));
		}
		if (DO_BIC(BIC_Node)) {
			if (t)
				outp += sprintf(outp, "%s%d",
						(printed++ ? delim : ""), cpus[t->cpu_id].physical_node_id);
			else
				outp += sprintf(outp, "%s-", (printed++ ? delim : ""));
		}
		if (DO_BIC(BIC_Core)) {
			if (c)
				outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), c->core_id);
			else
				outp += sprintf(outp, "%s-", (printed++ ? delim : ""));
		}
		if (DO_BIC(BIC_CPU))
			outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), t->cpu_id);
		if (DO_BIC(BIC_APIC))
			outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), t->apic_id);
		if (DO_BIC(BIC_X2APIC))
			outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), t->x2apic_id);
	}

	if (DO_BIC(BIC_Avg_MHz))
		outp += sprintf(outp, "%s%.0f", (printed++ ? delim : ""), 1.0 / units * t->aperf / interval_float);

	if (DO_BIC(BIC_Busy))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * t->mperf / tsc);

	if (DO_BIC(BIC_Bzy_MHz)) {
		if (has_base_hz)
			outp +=
			    sprintf(outp, "%s%.0f", (printed++ ? delim : ""), base_hz / units * t->aperf / t->mperf);
		else
			outp += sprintf(outp, "%s%.0f", (printed++ ? delim : ""),
					tsc / units * t->aperf / t->mperf / interval_float);
	}

	if (DO_BIC(BIC_TSC_MHz))
		outp += sprintf(outp, "%s%.0f", (printed++ ? delim : ""), 1.0 * t->tsc / units / interval_float);

	if (DO_BIC(BIC_IPC))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 1.0 * t->instr_count / t->aperf);

	/* IRQ */
	if (DO_BIC(BIC_IRQ)) {
		if (sums_need_wide_columns)
			outp += sprintf(outp, "%s%8lld", (printed++ ? delim : ""), t->irq_count);
		else
			outp += sprintf(outp, "%s%lld", (printed++ ? delim : ""), t->irq_count);
	}

	/* SMI */
	if (DO_BIC(BIC_SMI))
		outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), t->smi_count);

	/* Added counters */
	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW) {
			if (mp->width == 32)
				outp +=
				    sprintf(outp, "%s0x%08x", (printed++ ? delim : ""), (unsigned int)t->counter[i]);
			else
				outp += sprintf(outp, "%s0x%016llx", (printed++ ? delim : ""), t->counter[i]);
		} else if (mp->format == FORMAT_DELTA) {
			if ((mp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8lld", (printed++ ? delim : ""), t->counter[i]);
			else
				outp += sprintf(outp, "%s%lld", (printed++ ? delim : ""), t->counter[i]);
		} else if (mp->format == FORMAT_PERCENT) {
			if (mp->type == COUNTER_USEC)
				outp +=
				    sprintf(outp, "%s%.2f", (printed++ ? delim : ""),
					    t->counter[i] / interval_float / 10000);
			else
				outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * t->counter[i] / tsc);
		}
	}

	/* C1 */
	if (DO_BIC(BIC_CPU_c1))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * t->c1 / tsc);

	/* print per-core data only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		goto done;

	if (DO_BIC(BIC_CPU_c3))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * c->c3 / tsc);
	if (DO_BIC(BIC_CPU_c6))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * c->c6 / tsc);
	if (DO_BIC(BIC_CPU_c7))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * c->c7 / tsc);

	/* Mod%c6 */
	if (DO_BIC(BIC_Mod_c6))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * c->mc6_us / tsc);

	if (DO_BIC(BIC_CoreTmp))
		outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), c->core_temp_c);

	/* Core throttle count */
	if (DO_BIC(BIC_CORE_THROT_CNT))
		outp += sprintf(outp, "%s%lld", (printed++ ? delim : ""), c->core_throt_cnt);

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW) {
			if (mp->width == 32)
				outp +=
				    sprintf(outp, "%s0x%08x", (printed++ ? delim : ""), (unsigned int)c->counter[i]);
			else
				outp += sprintf(outp, "%s0x%016llx", (printed++ ? delim : ""), c->counter[i]);
		} else if (mp->format == FORMAT_DELTA) {
			if ((mp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8lld", (printed++ ? delim : ""), c->counter[i]);
			else
				outp += sprintf(outp, "%s%lld", (printed++ ? delim : ""), c->counter[i]);
		} else if (mp->format == FORMAT_PERCENT) {
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * c->counter[i] / tsc);
		}
	}

	fmt8 = "%s%.2f";

	if (DO_BIC(BIC_CorWatt) && (do_rapl & RAPL_PER_CORE_ENERGY))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""), c->core_energy * rapl_energy_units / interval_float);
	if (DO_BIC(BIC_Cor_J) && (do_rapl & RAPL_PER_CORE_ENERGY))
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""), c->core_energy * rapl_energy_units);

	/* print per-package data only for 1st core in package */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		goto done;

	/* PkgTmp */
	if (DO_BIC(BIC_PkgTmp))
		outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), p->pkg_temp_c);

	/* GFXrc6 */
	if (DO_BIC(BIC_GFX_rc6)) {
		if (p->gfx_rc6_ms == -1) {	/* detect GFX counter reset */
			outp += sprintf(outp, "%s**.**", (printed++ ? delim : ""));
		} else {
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""),
					p->gfx_rc6_ms / 10.0 / interval_float);
		}
	}

	/* GFXMHz */
	if (DO_BIC(BIC_GFXMHz))
		outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), p->gfx_mhz);

	/* GFXACTMHz */
	if (DO_BIC(BIC_GFXACTMHz))
		outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), p->gfx_act_mhz);

	/* Totl%C0, Any%C0 GFX%C0 CPUGFX% */
	if (DO_BIC(BIC_Totl_c0))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->pkg_wtd_core_c0 / tsc);
	if (DO_BIC(BIC_Any_c0))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->pkg_any_core_c0 / tsc);
	if (DO_BIC(BIC_GFX_c0))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->pkg_any_gfxe_c0 / tsc);
	if (DO_BIC(BIC_CPUGFX))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->pkg_both_core_gfxe_c0 / tsc);

	if (DO_BIC(BIC_Pkgpc2))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->pc2 / tsc);
	if (DO_BIC(BIC_Pkgpc3))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->pc3 / tsc);
	if (DO_BIC(BIC_Pkgpc6))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->pc6 / tsc);
	if (DO_BIC(BIC_Pkgpc7))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->pc7 / tsc);
	if (DO_BIC(BIC_Pkgpc8))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->pc8 / tsc);
	if (DO_BIC(BIC_Pkgpc9))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->pc9 / tsc);
	if (DO_BIC(BIC_Pkgpc10))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->pc10 / tsc);

	if (DO_BIC(BIC_CPU_LPI))
		outp +=
		    sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->cpu_lpi / 1000000.0 / interval_float);
	if (DO_BIC(BIC_SYS_LPI))
		outp +=
		    sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->sys_lpi / 1000000.0 / interval_float);

	if (DO_BIC(BIC_PkgWatt))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""), p->energy_pkg * rapl_energy_units / interval_float);

	if (DO_BIC(BIC_CorWatt) && !(do_rapl & RAPL_PER_CORE_ENERGY))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""), p->energy_cores * rapl_energy_units / interval_float);
	if (DO_BIC(BIC_GFXWatt))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""), p->energy_gfx * rapl_energy_units / interval_float);
	if (DO_BIC(BIC_RAMWatt))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""),
			    p->energy_dram * rapl_dram_energy_units / interval_float);
	if (DO_BIC(BIC_Pkg_J))
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""), p->energy_pkg * rapl_energy_units);
	if (DO_BIC(BIC_Cor_J) && !(do_rapl & RAPL_PER_CORE_ENERGY))
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""), p->energy_cores * rapl_energy_units);
	if (DO_BIC(BIC_GFX_J))
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""), p->energy_gfx * rapl_energy_units);
	if (DO_BIC(BIC_RAM_J))
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""), p->energy_dram * rapl_dram_energy_units);
	if (DO_BIC(BIC_PKG__))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""),
			    100.0 * p->rapl_pkg_perf_status * rapl_time_units / interval_float);
	if (DO_BIC(BIC_RAM__))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""),
			    100.0 * p->rapl_dram_perf_status * rapl_time_units / interval_float);
	/* UncMHz */
	if (DO_BIC(BIC_UNCORE_MHZ))
		outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), p->uncore_mhz);

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW) {
			if (mp->width == 32)
				outp +=
				    sprintf(outp, "%s0x%08x", (printed++ ? delim : ""), (unsigned int)p->counter[i]);
			else
				outp += sprintf(outp, "%s0x%016llx", (printed++ ? delim : ""), p->counter[i]);
		} else if (mp->format == FORMAT_DELTA) {
			if ((mp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8lld", (printed++ ? delim : ""), p->counter[i]);
			else
				outp += sprintf(outp, "%s%lld", (printed++ ? delim : ""), p->counter[i]);
		} else if (mp->format == FORMAT_PERCENT) {
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->counter[i] / tsc);
		}
	}

done:
	if (*(outp - 1) != '\n')
		outp += sprintf(outp, "\n");

	return 0;
}

void flush_output_stdout(void)
{
	FILE *filep;

	if (outf == stderr)
		filep = stdout;
	else
		filep = outf;

	fputs(output_buffer, filep);
	fflush(filep);

	outp = output_buffer;
}

void flush_output_stderr(void)
{
	fputs(output_buffer, outf);
	fflush(outf);
	outp = output_buffer;
}

void format_all_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	static int count;

	if ((!count || (header_iterations && !(count % header_iterations))) || !summary_only)
		print_header("\t");

	format_counters(&average.threads, &average.cores, &average.packages);

	count++;

	if (summary_only)
		return;

	for_all_cpus(format_counters, t, c, p);
}

#define DELTA_WRAP32(new, old)			\
	old = ((((unsigned long long)new << 32) - ((unsigned long long)old << 32)) >> 32);

int delta_package(struct pkg_data *new, struct pkg_data *old)
{
	int i;
	struct msr_counter *mp;

	if (DO_BIC(BIC_Totl_c0))
		old->pkg_wtd_core_c0 = new->pkg_wtd_core_c0 - old->pkg_wtd_core_c0;
	if (DO_BIC(BIC_Any_c0))
		old->pkg_any_core_c0 = new->pkg_any_core_c0 - old->pkg_any_core_c0;
	if (DO_BIC(BIC_GFX_c0))
		old->pkg_any_gfxe_c0 = new->pkg_any_gfxe_c0 - old->pkg_any_gfxe_c0;
	if (DO_BIC(BIC_CPUGFX))
		old->pkg_both_core_gfxe_c0 = new->pkg_both_core_gfxe_c0 - old->pkg_both_core_gfxe_c0;

	old->pc2 = new->pc2 - old->pc2;
	if (DO_BIC(BIC_Pkgpc3))
		old->pc3 = new->pc3 - old->pc3;
	if (DO_BIC(BIC_Pkgpc6))
		old->pc6 = new->pc6 - old->pc6;
	if (DO_BIC(BIC_Pkgpc7))
		old->pc7 = new->pc7 - old->pc7;
	old->pc8 = new->pc8 - old->pc8;
	old->pc9 = new->pc9 - old->pc9;
	old->pc10 = new->pc10 - old->pc10;
	old->cpu_lpi = new->cpu_lpi - old->cpu_lpi;
	old->sys_lpi = new->sys_lpi - old->sys_lpi;
	old->pkg_temp_c = new->pkg_temp_c;

	/* flag an error when rc6 counter resets/wraps */
	if (old->gfx_rc6_ms > new->gfx_rc6_ms)
		old->gfx_rc6_ms = -1;
	else
		old->gfx_rc6_ms = new->gfx_rc6_ms - old->gfx_rc6_ms;

	old->uncore_mhz = new->uncore_mhz;
	old->gfx_mhz = new->gfx_mhz;
	old->gfx_act_mhz = new->gfx_act_mhz;

	old->energy_pkg = new->energy_pkg - old->energy_pkg;
	old->energy_cores = new->energy_cores - old->energy_cores;
	old->energy_gfx = new->energy_gfx - old->energy_gfx;
	old->energy_dram = new->energy_dram - old->energy_dram;
	old->rapl_pkg_perf_status = new->rapl_pkg_perf_status - old->rapl_pkg_perf_status;
	old->rapl_dram_perf_status = new->rapl_dram_perf_status - old->rapl_dram_perf_status;

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			old->counter[i] = new->counter[i];
		else
			old->counter[i] = new->counter[i] - old->counter[i];
	}

	return 0;
}

void delta_core(struct core_data *new, struct core_data *old)
{
	int i;
	struct msr_counter *mp;

	old->c3 = new->c3 - old->c3;
	old->c6 = new->c6 - old->c6;
	old->c7 = new->c7 - old->c7;
	old->core_temp_c = new->core_temp_c;
	old->core_throt_cnt = new->core_throt_cnt;
	old->mc6_us = new->mc6_us - old->mc6_us;

	DELTA_WRAP32(new->core_energy, old->core_energy);

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			old->counter[i] = new->counter[i];
		else
			old->counter[i] = new->counter[i] - old->counter[i];
	}
}

int soft_c1_residency_display(int bic)
{
	if (!DO_BIC(BIC_CPU_c1) || use_c1_residency_msr)
		return 0;

	return DO_BIC_READ(bic);
}

/*
 * old = new - old
 */
int delta_thread(struct thread_data *new, struct thread_data *old, struct core_data *core_delta)
{
	int i;
	struct msr_counter *mp;

	/* we run cpuid just the 1st time, copy the results */
	if (DO_BIC(BIC_APIC))
		new->apic_id = old->apic_id;
	if (DO_BIC(BIC_X2APIC))
		new->x2apic_id = old->x2apic_id;

	/*
	 * the timestamps from start of measurement interval are in "old"
	 * the timestamp from end of measurement interval are in "new"
	 * over-write old w/ new so we can print end of interval values
	 */

	timersub(&new->tv_begin, &old->tv_begin, &old->tv_delta);
	old->tv_begin = new->tv_begin;
	old->tv_end = new->tv_end;

	old->tsc = new->tsc - old->tsc;

	/* check for TSC < 1 Mcycles over interval */
	if (old->tsc < (1000 * 1000))
		errx(-3, "Insanely slow TSC rate, TSC stops in idle?\n"
		     "You can disable all c-states by booting with \"idle=poll\"\n"
		     "or just the deep ones with \"processor.max_cstate=1\"");

	old->c1 = new->c1 - old->c1;

	if (DO_BIC(BIC_Avg_MHz) || DO_BIC(BIC_Busy) || DO_BIC(BIC_Bzy_MHz) || soft_c1_residency_display(BIC_Avg_MHz)) {
		if ((new->aperf > old->aperf) && (new->mperf > old->mperf)) {
			old->aperf = new->aperf - old->aperf;
			old->mperf = new->mperf - old->mperf;
		} else {
			return -1;
		}
	}

	if (use_c1_residency_msr) {
		/*
		 * Some models have a dedicated C1 residency MSR,
		 * which should be more accurate than the derivation below.
		 */
	} else {
		/*
		 * As counter collection is not atomic,
		 * it is possible for mperf's non-halted cycles + idle states
		 * to exceed TSC's all cycles: show c1 = 0% in that case.
		 */
		if ((old->mperf + core_delta->c3 + core_delta->c6 + core_delta->c7) > (old->tsc * tsc_tweak))
			old->c1 = 0;
		else {
			/* normal case, derive c1 */
			old->c1 = (old->tsc * tsc_tweak) - old->mperf - core_delta->c3
			    - core_delta->c6 - core_delta->c7;
		}
	}

	if (old->mperf == 0) {
		if (debug > 1)
			fprintf(outf, "cpu%d MPERF 0!\n", old->cpu_id);
		old->mperf = 1;	/* divide by 0 protection */
	}

	if (DO_BIC(BIC_IPC))
		old->instr_count = new->instr_count - old->instr_count;

	if (DO_BIC(BIC_IRQ))
		old->irq_count = new->irq_count - old->irq_count;

	if (DO_BIC(BIC_SMI))
		old->smi_count = new->smi_count - old->smi_count;

	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			old->counter[i] = new->counter[i];
		else
			old->counter[i] = new->counter[i] - old->counter[i];
	}
	return 0;
}

int delta_cpu(struct thread_data *t, struct core_data *c,
	      struct pkg_data *p, struct thread_data *t2, struct core_data *c2, struct pkg_data *p2)
{
	int retval = 0;

	/* calculate core delta only for 1st thread in core */
	if (t->flags & CPU_IS_FIRST_THREAD_IN_CORE)
		delta_core(c, c2);

	/* always calculate thread delta */
	retval = delta_thread(t, t2, c2);	/* c2 is core delta */
	if (retval)
		return retval;

	/* calculate package delta only for 1st core in package */
	if (t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE)
		retval = delta_package(p, p2);

	return retval;
}

void clear_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	int i;
	struct msr_counter *mp;

	t->tv_begin.tv_sec = 0;
	t->tv_begin.tv_usec = 0;
	t->tv_end.tv_sec = 0;
	t->tv_end.tv_usec = 0;
	t->tv_delta.tv_sec = 0;
	t->tv_delta.tv_usec = 0;

	t->tsc = 0;
	t->aperf = 0;
	t->mperf = 0;
	t->c1 = 0;

	t->instr_count = 0;

	t->irq_count = 0;
	t->smi_count = 0;

	/* tells format_counters to dump all fields from this set */
	t->flags = CPU_IS_FIRST_THREAD_IN_CORE | CPU_IS_FIRST_CORE_IN_PACKAGE;

	c->c3 = 0;
	c->c6 = 0;
	c->c7 = 0;
	c->mc6_us = 0;
	c->core_temp_c = 0;
	c->core_energy = 0;
	c->core_throt_cnt = 0;

	p->pkg_wtd_core_c0 = 0;
	p->pkg_any_core_c0 = 0;
	p->pkg_any_gfxe_c0 = 0;
	p->pkg_both_core_gfxe_c0 = 0;

	p->pc2 = 0;
	if (DO_BIC(BIC_Pkgpc3))
		p->pc3 = 0;
	if (DO_BIC(BIC_Pkgpc6))
		p->pc6 = 0;
	if (DO_BIC(BIC_Pkgpc7))
		p->pc7 = 0;
	p->pc8 = 0;
	p->pc9 = 0;
	p->pc10 = 0;
	p->cpu_lpi = 0;
	p->sys_lpi = 0;

	p->energy_pkg = 0;
	p->energy_dram = 0;
	p->energy_cores = 0;
	p->energy_gfx = 0;
	p->rapl_pkg_perf_status = 0;
	p->rapl_dram_perf_status = 0;
	p->pkg_temp_c = 0;

	p->gfx_rc6_ms = 0;
	p->uncore_mhz = 0;
	p->gfx_mhz = 0;
	p->gfx_act_mhz = 0;
	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next)
		t->counter[i] = 0;

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next)
		c->counter[i] = 0;

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next)
		p->counter[i] = 0;
}

int sum_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	int i;
	struct msr_counter *mp;

	/* copy un-changing apic_id's */
	if (DO_BIC(BIC_APIC))
		average.threads.apic_id = t->apic_id;
	if (DO_BIC(BIC_X2APIC))
		average.threads.x2apic_id = t->x2apic_id;

	/* remember first tv_begin */
	if (average.threads.tv_begin.tv_sec == 0)
		average.threads.tv_begin = t->tv_begin;

	/* remember last tv_end */
	average.threads.tv_end = t->tv_end;

	average.threads.tsc += t->tsc;
	average.threads.aperf += t->aperf;
	average.threads.mperf += t->mperf;
	average.threads.c1 += t->c1;

	average.threads.instr_count += t->instr_count;

	average.threads.irq_count += t->irq_count;
	average.threads.smi_count += t->smi_count;

	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		average.threads.counter[i] += t->counter[i];
	}

	/* sum per-core values only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		return 0;

	average.cores.c3 += c->c3;
	average.cores.c6 += c->c6;
	average.cores.c7 += c->c7;
	average.cores.mc6_us += c->mc6_us;

	average.cores.core_temp_c = MAX(average.cores.core_temp_c, c->core_temp_c);
	average.cores.core_throt_cnt = MAX(average.cores.core_throt_cnt, c->core_throt_cnt);

	average.cores.core_energy += c->core_energy;

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		average.cores.counter[i] += c->counter[i];
	}

	/* sum per-pkg values only for 1st core in pkg */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (DO_BIC(BIC_Totl_c0))
		average.packages.pkg_wtd_core_c0 += p->pkg_wtd_core_c0;
	if (DO_BIC(BIC_Any_c0))
		average.packages.pkg_any_core_c0 += p->pkg_any_core_c0;
	if (DO_BIC(BIC_GFX_c0))
		average.packages.pkg_any_gfxe_c0 += p->pkg_any_gfxe_c0;
	if (DO_BIC(BIC_CPUGFX))
		average.packages.pkg_both_core_gfxe_c0 += p->pkg_both_core_gfxe_c0;

	average.packages.pc2 += p->pc2;
	if (DO_BIC(BIC_Pkgpc3))
		average.packages.pc3 += p->pc3;
	if (DO_BIC(BIC_Pkgpc6))
		average.packages.pc6 += p->pc6;
	if (DO_BIC(BIC_Pkgpc7))
		average.packages.pc7 += p->pc7;
	average.packages.pc8 += p->pc8;
	average.packages.pc9 += p->pc9;
	average.packages.pc10 += p->pc10;

	average.packages.cpu_lpi = p->cpu_lpi;
	average.packages.sys_lpi = p->sys_lpi;

	average.packages.energy_pkg += p->energy_pkg;
	average.packages.energy_dram += p->energy_dram;
	average.packages.energy_cores += p->energy_cores;
	average.packages.energy_gfx += p->energy_gfx;

	average.packages.gfx_rc6_ms = p->gfx_rc6_ms;
	average.packages.uncore_mhz = p->uncore_mhz;
	average.packages.gfx_mhz = p->gfx_mhz;
	average.packages.gfx_act_mhz = p->gfx_act_mhz;

	average.packages.pkg_temp_c = MAX(average.packages.pkg_temp_c, p->pkg_temp_c);

	average.packages.rapl_pkg_perf_status += p->rapl_pkg_perf_status;
	average.packages.rapl_dram_perf_status += p->rapl_dram_perf_status;

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		average.packages.counter[i] += p->counter[i];
	}
	return 0;
}

/*
 * sum the counters for all cpus in the system
 * compute the weighted average
 */
void compute_average(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	int i;
	struct msr_counter *mp;

	clear_counters(&average.threads, &average.cores, &average.packages);

	for_all_cpus(sum_counters, t, c, p);

	/* Use the global time delta for the average. */
	average.threads.tv_delta = tv_delta;

	average.threads.tsc /= topo.num_cpus;
	average.threads.aperf /= topo.num_cpus;
	average.threads.mperf /= topo.num_cpus;
	average.threads.instr_count /= topo.num_cpus;
	average.threads.c1 /= topo.num_cpus;

	if (average.threads.irq_count > 9999999)
		sums_need_wide_columns = 1;

	average.cores.c3 /= topo.num_cores;
	average.cores.c6 /= topo.num_cores;
	average.cores.c7 /= topo.num_cores;
	average.cores.mc6_us /= topo.num_cores;

	if (DO_BIC(BIC_Totl_c0))
		average.packages.pkg_wtd_core_c0 /= topo.num_packages;
	if (DO_BIC(BIC_Any_c0))
		average.packages.pkg_any_core_c0 /= topo.num_packages;
	if (DO_BIC(BIC_GFX_c0))
		average.packages.pkg_any_gfxe_c0 /= topo.num_packages;
	if (DO_BIC(BIC_CPUGFX))
		average.packages.pkg_both_core_gfxe_c0 /= topo.num_packages;

	average.packages.pc2 /= topo.num_packages;
	if (DO_BIC(BIC_Pkgpc3))
		average.packages.pc3 /= topo.num_packages;
	if (DO_BIC(BIC_Pkgpc6))
		average.packages.pc6 /= topo.num_packages;
	if (DO_BIC(BIC_Pkgpc7))
		average.packages.pc7 /= topo.num_packages;

	average.packages.pc8 /= topo.num_packages;
	average.packages.pc9 /= topo.num_packages;
	average.packages.pc10 /= topo.num_packages;

	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		if (mp->type == COUNTER_ITEMS) {
			if (average.threads.counter[i] > 9999999)
				sums_need_wide_columns = 1;
			continue;
		}
		average.threads.counter[i] /= topo.num_cpus;
	}
	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		if (mp->type == COUNTER_ITEMS) {
			if (average.cores.counter[i] > 9999999)
				sums_need_wide_columns = 1;
		}
		average.cores.counter[i] /= topo.num_cores;
	}
	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		if (mp->type == COUNTER_ITEMS) {
			if (average.packages.counter[i] > 9999999)
				sums_need_wide_columns = 1;
		}
		average.packages.counter[i] /= topo.num_packages;
	}
}

static unsigned long long rdtsc(void)
{
	unsigned int low, high;

	asm volatile ("rdtsc":"=a" (low), "=d"(high));

	return low | ((unsigned long long)high) << 32;
}

/*
 * Open a file, and exit on failure
 */
FILE *fopen_or_die(const char *path, const char *mode)
{
	FILE *filep = fopen(path, mode);

	if (!filep)
		err(1, "%s: open failed", path);
	return filep;
}

/*
 * snapshot_sysfs_counter()
 *
 * return snapshot of given counter
 */
unsigned long long snapshot_sysfs_counter(char *path)
{
	FILE *fp;
	int retval;
	unsigned long long counter;

	fp = fopen_or_die(path, "r");

	retval = fscanf(fp, "%lld", &counter);
	if (retval != 1)
		err(1, "snapshot_sysfs_counter(%s)", path);

	fclose(fp);

	return counter;
}

int get_mp(int cpu, struct msr_counter *mp, unsigned long long *counterp)
{
	if (mp->msr_num != 0) {
		if (get_msr(cpu, mp->msr_num, counterp))
			return -1;
	} else {
		char path[128 + PATH_BYTES];

		if (mp->flags & SYSFS_PERCPU) {
			sprintf(path, "/sys/devices/system/cpu/cpu%d/%s", cpu, mp->path);

			*counterp = snapshot_sysfs_counter(path);
		} else {
			*counterp = snapshot_sysfs_counter(mp->path);
		}
	}

	return 0;
}

unsigned long long get_uncore_mhz(int package, int die)
{
	char path[128];

	sprintf(path, "/sys/devices/system/cpu/intel_uncore_frequency/package_0%d_die_0%d/current_freq_khz", package,
		die);

	return (snapshot_sysfs_counter(path) / 1000);
}

int get_epb(int cpu)
{
	char path[128 + PATH_BYTES];
	unsigned long long msr;
	int ret, epb = -1;
	FILE *fp;

	sprintf(path, "/sys/devices/system/cpu/cpu%d/power/energy_perf_bias", cpu);

	fp = fopen(path, "r");
	if (!fp)
		goto msr_fallback;

	ret = fscanf(fp, "%d", &epb);
	if (ret != 1)
		err(1, "%s(%s)", __func__, path);

	fclose(fp);

	return epb;

msr_fallback:
	get_msr(cpu, MSR_IA32_ENERGY_PERF_BIAS, &msr);

	return msr & 0xf;
}

void get_apic_id(struct thread_data *t)
{
	unsigned int eax, ebx, ecx, edx;

	if (DO_BIC(BIC_APIC)) {
		eax = ebx = ecx = edx = 0;
		__cpuid(1, eax, ebx, ecx, edx);

		t->apic_id = (ebx >> 24) & 0xff;
	}

	if (!DO_BIC(BIC_X2APIC))
		return;

	if (authentic_amd || hygon_genuine) {
		unsigned int topology_extensions;

		if (max_extended_level < 0x8000001e)
			return;

		eax = ebx = ecx = edx = 0;
		__cpuid(0x80000001, eax, ebx, ecx, edx);
		topology_extensions = ecx & (1 << 22);

		if (topology_extensions == 0)
			return;

		eax = ebx = ecx = edx = 0;
		__cpuid(0x8000001e, eax, ebx, ecx, edx);

		t->x2apic_id = eax;
		return;
	}

	if (!genuine_intel)
		return;

	if (max_level < 0xb)
		return;

	ecx = 0;
	__cpuid(0xb, eax, ebx, ecx, edx);
	t->x2apic_id = edx;

	if (debug && (t->apic_id != (t->x2apic_id & 0xff)))
		fprintf(outf, "cpu%d: BIOS BUG: apic 0x%x x2apic 0x%x\n", t->cpu_id, t->apic_id, t->x2apic_id);
}

int get_core_throt_cnt(int cpu, unsigned long long *cnt)
{
	char path[128 + PATH_BYTES];
	unsigned long long tmp;
	FILE *fp;
	int ret;

	sprintf(path, "/sys/devices/system/cpu/cpu%d/thermal_throttle/core_throttle_count", cpu);
	fp = fopen(path, "r");
	if (!fp)
		return -1;
	ret = fscanf(fp, "%lld", &tmp);
	fclose(fp);
	if (ret != 1)
		return -1;
	*cnt = tmp;

	return 0;
}

/*
 * get_counters(...)
 * migrate to cpu
 * acquire and record local counters for that cpu
 */
int get_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	int cpu = t->cpu_id;
	unsigned long long msr;
	int aperf_mperf_retry_count = 0;
	struct msr_counter *mp;
	int i;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "get_counters: Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	gettimeofday(&t->tv_begin, (struct timezone *)NULL);

	if (first_counter_read)
		get_apic_id(t);
retry:
	t->tsc = rdtsc();	/* we are running on local CPU of interest */

	if (DO_BIC(BIC_Avg_MHz) || DO_BIC(BIC_Busy) || DO_BIC(BIC_Bzy_MHz) || soft_c1_residency_display(BIC_Avg_MHz)) {
		unsigned long long tsc_before, tsc_between, tsc_after, aperf_time, mperf_time;

		/*
		 * The TSC, APERF and MPERF must be read together for
		 * APERF/MPERF and MPERF/TSC to give accurate results.
		 *
		 * Unfortunately, APERF and MPERF are read by
		 * individual system call, so delays may occur
		 * between them.  If the time to read them
		 * varies by a large amount, we re-read them.
		 */

		/*
		 * This initial dummy APERF read has been seen to
		 * reduce jitter in the subsequent reads.
		 */

		if (get_msr(cpu, MSR_IA32_APERF, &t->aperf))
			return -3;

		t->tsc = rdtsc();	/* re-read close to APERF */

		tsc_before = t->tsc;

		if (get_msr(cpu, MSR_IA32_APERF, &t->aperf))
			return -3;

		tsc_between = rdtsc();

		if (get_msr(cpu, MSR_IA32_MPERF, &t->mperf))
			return -4;

		tsc_after = rdtsc();

		aperf_time = tsc_between - tsc_before;
		mperf_time = tsc_after - tsc_between;

		/*
		 * If the system call latency to read APERF and MPERF
		 * differ by more than 2x, then try again.
		 */
		if ((aperf_time > (2 * mperf_time)) || (mperf_time > (2 * aperf_time))) {
			aperf_mperf_retry_count++;
			if (aperf_mperf_retry_count < 5)
				goto retry;
			else
				warnx("cpu%d jitter %lld %lld", cpu, aperf_time, mperf_time);
		}
		aperf_mperf_retry_count = 0;

		t->aperf = t->aperf * aperf_mperf_multiplier;
		t->mperf = t->mperf * aperf_mperf_multiplier;
	}

	if (DO_BIC(BIC_IPC))
		if (read(get_instr_count_fd(cpu), &t->instr_count, sizeof(long long)) != sizeof(long long))
			return -4;

	if (DO_BIC(BIC_IRQ))
		t->irq_count = irqs_per_cpu[cpu];
	if (DO_BIC(BIC_SMI)) {
		if (get_msr(cpu, MSR_SMI_COUNT, &msr))
			return -5;
		t->smi_count = msr & 0xFFFFFFFF;
	}
	if (DO_BIC(BIC_CPU_c1) && use_c1_residency_msr) {
		if (get_msr(cpu, MSR_CORE_C1_RES, &t->c1))
			return -6;
	}

	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
		if (get_mp(cpu, mp, &t->counter[i]))
			return -10;
	}

	/* collect core counters only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		goto done;

	if (DO_BIC(BIC_CPU_c3) || soft_c1_residency_display(BIC_CPU_c3)) {
		if (get_msr(cpu, MSR_CORE_C3_RESIDENCY, &c->c3))
			return -6;
	}

	if ((DO_BIC(BIC_CPU_c6) || soft_c1_residency_display(BIC_CPU_c6)) && !do_knl_cstates) {
		if (get_msr(cpu, MSR_CORE_C6_RESIDENCY, &c->c6))
			return -7;
	} else if (do_knl_cstates || soft_c1_residency_display(BIC_CPU_c6)) {
		if (get_msr(cpu, MSR_KNL_CORE_C6_RESIDENCY, &c->c6))
			return -7;
	}

	if (DO_BIC(BIC_CPU_c7) || soft_c1_residency_display(BIC_CPU_c7)) {
		if (get_msr(cpu, MSR_CORE_C7_RESIDENCY, &c->c7))
			return -8;
		else if (t->is_atom) {
			/*
			 * For Atom CPUs that has core cstate deeper than c6,
			 * MSR_CORE_C6_RESIDENCY returns residency of cc6 and deeper.
			 * Minus CC7 (and deeper cstates) residency to get
			 * accturate cc6 residency.
			 */
			c->c6 -= c->c7;
		}
	}

	if (DO_BIC(BIC_Mod_c6))
		if (get_msr(cpu, MSR_MODULE_C6_RES_MS, &c->mc6_us))
			return -8;

	if (DO_BIC(BIC_CoreTmp)) {
		if (get_msr(cpu, MSR_IA32_THERM_STATUS, &msr))
			return -9;
		c->core_temp_c = tj_max - ((msr >> 16) & 0x7F);
	}

	if (DO_BIC(BIC_CORE_THROT_CNT))
		get_core_throt_cnt(cpu, &c->core_throt_cnt);

	if (do_rapl & RAPL_AMD_F17H) {
		if (get_msr(cpu, MSR_CORE_ENERGY_STAT, &msr))
			return -14;
		c->core_energy = msr & 0xFFFFFFFF;
	}

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (get_mp(cpu, mp, &c->counter[i]))
			return -10;
	}

	/* collect package counters only for 1st core in package */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		goto done;

	if (DO_BIC(BIC_Totl_c0)) {
		if (get_msr(cpu, MSR_PKG_WEIGHTED_CORE_C0_RES, &p->pkg_wtd_core_c0))
			return -10;
	}
	if (DO_BIC(BIC_Any_c0)) {
		if (get_msr(cpu, MSR_PKG_ANY_CORE_C0_RES, &p->pkg_any_core_c0))
			return -11;
	}
	if (DO_BIC(BIC_GFX_c0)) {
		if (get_msr(cpu, MSR_PKG_ANY_GFXE_C0_RES, &p->pkg_any_gfxe_c0))
			return -12;
	}
	if (DO_BIC(BIC_CPUGFX)) {
		if (get_msr(cpu, MSR_PKG_BOTH_CORE_GFXE_C0_RES, &p->pkg_both_core_gfxe_c0))
			return -13;
	}
	if (DO_BIC(BIC_Pkgpc3))
		if (get_msr(cpu, MSR_PKG_C3_RESIDENCY, &p->pc3))
			return -9;
	if (DO_BIC(BIC_Pkgpc6)) {
		if (do_slm_cstates) {
			if (get_msr(cpu, MSR_ATOM_PKG_C6_RESIDENCY, &p->pc6))
				return -10;
		} else {
			if (get_msr(cpu, MSR_PKG_C6_RESIDENCY, &p->pc6))
				return -10;
		}
	}

	if (DO_BIC(BIC_Pkgpc2))
		if (get_msr(cpu, MSR_PKG_C2_RESIDENCY, &p->pc2))
			return -11;
	if (DO_BIC(BIC_Pkgpc7))
		if (get_msr(cpu, MSR_PKG_C7_RESIDENCY, &p->pc7))
			return -12;
	if (DO_BIC(BIC_Pkgpc8))
		if (get_msr(cpu, MSR_PKG_C8_RESIDENCY, &p->pc8))
			return -13;
	if (DO_BIC(BIC_Pkgpc9))
		if (get_msr(cpu, MSR_PKG_C9_RESIDENCY, &p->pc9))
			return -13;
	if (DO_BIC(BIC_Pkgpc10))
		if (get_msr(cpu, MSR_PKG_C10_RESIDENCY, &p->pc10))
			return -13;

	if (DO_BIC(BIC_CPU_LPI))
		p->cpu_lpi = cpuidle_cur_cpu_lpi_us;
	if (DO_BIC(BIC_SYS_LPI))
		p->sys_lpi = cpuidle_cur_sys_lpi_us;

	if (do_rapl & RAPL_PKG) {
		if (get_msr_sum(cpu, MSR_PKG_ENERGY_STATUS, &msr))
			return -13;
		p->energy_pkg = msr;
	}
	if (do_rapl & RAPL_CORES_ENERGY_STATUS) {
		if (get_msr_sum(cpu, MSR_PP0_ENERGY_STATUS, &msr))
			return -14;
		p->energy_cores = msr;
	}
	if (do_rapl & RAPL_DRAM) {
		if (get_msr_sum(cpu, MSR_DRAM_ENERGY_STATUS, &msr))
			return -15;
		p->energy_dram = msr;
	}
	if (do_rapl & RAPL_GFX) {
		if (get_msr_sum(cpu, MSR_PP1_ENERGY_STATUS, &msr))
			return -16;
		p->energy_gfx = msr;
	}
	if (do_rapl & RAPL_PKG_PERF_STATUS) {
		if (get_msr_sum(cpu, MSR_PKG_PERF_STATUS, &msr))
			return -16;
		p->rapl_pkg_perf_status = msr;
	}
	if (do_rapl & RAPL_DRAM_PERF_STATUS) {
		if (get_msr_sum(cpu, MSR_DRAM_PERF_STATUS, &msr))
			return -16;
		p->rapl_dram_perf_status = msr;
	}
	if (do_rapl & RAPL_AMD_F17H) {
		if (get_msr_sum(cpu, MSR_PKG_ENERGY_STAT, &msr))
			return -13;
		p->energy_pkg = msr;
	}
	if (DO_BIC(BIC_PkgTmp)) {
		if (get_msr(cpu, MSR_IA32_PACKAGE_THERM_STATUS, &msr))
			return -17;
		p->pkg_temp_c = tj_max - ((msr >> 16) & 0x7F);
	}

	if (DO_BIC(BIC_GFX_rc6))
		p->gfx_rc6_ms = gfx_cur_rc6_ms;

	/* n.b. assume die0 uncore frequency applies to whole package */
	if (DO_BIC(BIC_UNCORE_MHZ))
		p->uncore_mhz = get_uncore_mhz(p->package_id, 0);

	if (DO_BIC(BIC_GFXMHz))
		p->gfx_mhz = gfx_cur_mhz;

	if (DO_BIC(BIC_GFXACTMHz))
		p->gfx_act_mhz = gfx_act_mhz;

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if (get_mp(cpu, mp, &p->counter[i]))
			return -10;
	}
done:
	gettimeofday(&t->tv_end, (struct timezone *)NULL);

	return 0;
}

/*
 * MSR_PKG_CST_CONFIG_CONTROL decoding for pkg_cstate_limit:
 * If you change the values, note they are used both in comparisons
 * (>= PCL__7) and to index pkg_cstate_limit_strings[].
 */

#define PCLUKN 0		/* Unknown */
#define PCLRSV 1		/* Reserved */
#define PCL__0 2		/* PC0 */
#define PCL__1 3		/* PC1 */
#define PCL__2 4		/* PC2 */
#define PCL__3 5		/* PC3 */
#define PCL__4 6		/* PC4 */
#define PCL__6 7		/* PC6 */
#define PCL_6N 8		/* PC6 No Retention */
#define PCL_6R 9		/* PC6 Retention */
#define PCL__7 10		/* PC7 */
#define PCL_7S 11		/* PC7 Shrink */
#define PCL__8 12		/* PC8 */
#define PCL__9 13		/* PC9 */
#define PCL_10 14		/* PC10 */
#define PCLUNL 15		/* Unlimited */

int pkg_cstate_limit = PCLUKN;
char *pkg_cstate_limit_strings[] = { "reserved", "unknown", "pc0", "pc1", "pc2",
	"pc3", "pc4", "pc6", "pc6n", "pc6r", "pc7", "pc7s", "pc8", "pc9", "pc10", "unlimited"
};

int nhm_pkg_cstate_limits[16] =
    { PCL__0, PCL__1, PCL__3, PCL__6, PCL__7, PCLRSV, PCLRSV, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV,
	PCLRSV, PCLRSV
};

int snb_pkg_cstate_limits[16] =
    { PCL__0, PCL__2, PCL_6N, PCL_6R, PCL__7, PCL_7S, PCLRSV, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV,
	PCLRSV, PCLRSV
};

int hsw_pkg_cstate_limits[16] =
    { PCL__0, PCL__2, PCL__3, PCL__6, PCL__7, PCL_7S, PCL__8, PCL__9, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV,
	PCLRSV, PCLRSV
};

int slv_pkg_cstate_limits[16] =
    { PCL__0, PCL__1, PCLRSV, PCLRSV, PCL__4, PCLRSV, PCL__6, PCL__7, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV,
	PCL__6, PCL__7
};

int amt_pkg_cstate_limits[16] =
    { PCLUNL, PCL__1, PCL__2, PCLRSV, PCLRSV, PCLRSV, PCL__6, PCL__7, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV,
	PCLRSV, PCLRSV
};

int phi_pkg_cstate_limits[16] =
    { PCL__0, PCL__2, PCL_6N, PCL_6R, PCLRSV, PCLRSV, PCLRSV, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV,
	PCLRSV, PCLRSV
};

int glm_pkg_cstate_limits[16] =
    { PCLUNL, PCL__1, PCL__3, PCL__6, PCL__7, PCL_7S, PCL__8, PCL__9, PCL_10, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV,
	PCLRSV, PCLRSV
};

int skx_pkg_cstate_limits[16] =
    { PCL__0, PCL__2, PCL_6N, PCL_6R, PCLRSV, PCLRSV, PCLRSV, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV,
	PCLRSV, PCLRSV
};

int icx_pkg_cstate_limits[16] =
    { PCL__0, PCL__2, PCL__6, PCL__6, PCLRSV, PCLRSV, PCLRSV, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV,
	PCLRSV, PCLRSV
};

static void calculate_tsc_tweak()
{
	tsc_tweak = base_hz / tsc_hz;
}

void prewake_cstate_probe(unsigned int family, unsigned int model);

static void dump_nhm_platform_info(void)
{
	unsigned long long msr;
	unsigned int ratio;

	get_msr(base_cpu, MSR_PLATFORM_INFO, &msr);

	fprintf(outf, "cpu%d: MSR_PLATFORM_INFO: 0x%08llx\n", base_cpu, msr);

	ratio = (msr >> 40) & 0xFF;
	fprintf(outf, "%d * %.1f = %.1f MHz max efficiency frequency\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0xFF;
	fprintf(outf, "%d * %.1f = %.1f MHz base frequency\n", ratio, bclk, ratio * bclk);

	get_msr(base_cpu, MSR_IA32_POWER_CTL, &msr);
	fprintf(outf, "cpu%d: MSR_IA32_POWER_CTL: 0x%08llx (C1E auto-promotion: %sabled)\n",
		base_cpu, msr, msr & 0x2 ? "EN" : "DIS");

	/* C-state Pre-wake Disable (CSTATE_PREWAKE_DISABLE) */
	if (dis_cstate_prewake)
		fprintf(outf, "C-state Pre-wake: %sabled\n", msr & 0x40000000 ? "DIS" : "EN");

	return;
}

static void dump_hsw_turbo_ratio_limits(void)
{
	unsigned long long msr;
	unsigned int ratio;

	get_msr(base_cpu, MSR_TURBO_RATIO_LIMIT2, &msr);

	fprintf(outf, "cpu%d: MSR_TURBO_RATIO_LIMIT2: 0x%08llx\n", base_cpu, msr);

	ratio = (msr >> 8) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 18 active cores\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 0) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 17 active cores\n", ratio, bclk, ratio * bclk);
	return;
}

static void dump_ivt_turbo_ratio_limits(void)
{
	unsigned long long msr;
	unsigned int ratio;

	get_msr(base_cpu, MSR_TURBO_RATIO_LIMIT1, &msr);

	fprintf(outf, "cpu%d: MSR_TURBO_RATIO_LIMIT1: 0x%08llx\n", base_cpu, msr);

	ratio = (msr >> 56) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 16 active cores\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 48) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 15 active cores\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 40) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 14 active cores\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 32) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 13 active cores\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 24) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 12 active cores\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 16) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 11 active cores\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 10 active cores\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 0) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 9 active cores\n", ratio, bclk, ratio * bclk);
	return;
}

int has_turbo_ratio_group_limits(int family, int model)
{

	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_ATOM_GOLDMONT:
	case INTEL_FAM6_SKYLAKE_X:
	case INTEL_FAM6_ICELAKE_X:
	case INTEL_FAM6_SAPPHIRERAPIDS_X:
	case INTEL_FAM6_ATOM_GOLDMONT_D:
	case INTEL_FAM6_ATOM_TREMONT_D:
		return 1;
	default:
		return 0;
	}
}

static void dump_turbo_ratio_limits(int trl_msr_offset, int family, int model)
{
	unsigned long long msr, core_counts;
	int shift;

	get_msr(base_cpu, trl_msr_offset, &msr);
	fprintf(outf, "cpu%d: MSR_%sTURBO_RATIO_LIMIT: 0x%08llx\n",
		base_cpu, trl_msr_offset == MSR_SECONDARY_TURBO_RATIO_LIMIT ? "SECONDARY" : "", msr);

	if (has_turbo_ratio_group_limits(family, model)) {
		get_msr(base_cpu, MSR_TURBO_RATIO_LIMIT1, &core_counts);
		fprintf(outf, "cpu%d: MSR_TURBO_RATIO_LIMIT1: 0x%08llx\n", base_cpu, core_counts);
	} else {
		core_counts = 0x0807060504030201;
	}

	for (shift = 56; shift >= 0; shift -= 8) {
		unsigned int ratio, group_size;

		ratio = (msr >> shift) & 0xFF;
		group_size = (core_counts >> shift) & 0xFF;
		if (ratio)
			fprintf(outf, "%d * %.1f = %.1f MHz max turbo %d active cores\n",
				ratio, bclk, ratio * bclk, group_size);
	}

	return;
}

static void dump_atom_turbo_ratio_limits(void)
{
	unsigned long long msr;
	unsigned int ratio;

	get_msr(base_cpu, MSR_ATOM_CORE_RATIOS, &msr);
	fprintf(outf, "cpu%d: MSR_ATOM_CORE_RATIOS: 0x%08llx\n", base_cpu, msr & 0xFFFFFFFF);

	ratio = (msr >> 0) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz minimum operating frequency\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz low frequency mode (LFM)\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 16) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz base frequency\n", ratio, bclk, ratio * bclk);

	get_msr(base_cpu, MSR_ATOM_CORE_TURBO_RATIOS, &msr);
	fprintf(outf, "cpu%d: MSR_ATOM_CORE_TURBO_RATIOS: 0x%08llx\n", base_cpu, msr & 0xFFFFFFFF);

	ratio = (msr >> 24) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 4 active cores\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 16) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 3 active cores\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 2 active cores\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 0) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 1 active core\n", ratio, bclk, ratio * bclk);
}

static void dump_knl_turbo_ratio_limits(void)
{
	const unsigned int buckets_no = 7;

	unsigned long long msr;
	int delta_cores, delta_ratio;
	int i, b_nr;
	unsigned int cores[buckets_no];
	unsigned int ratio[buckets_no];

	get_msr(base_cpu, MSR_TURBO_RATIO_LIMIT, &msr);

	fprintf(outf, "cpu%d: MSR_TURBO_RATIO_LIMIT: 0x%08llx\n", base_cpu, msr);

	/*
	 * Turbo encoding in KNL is as follows:
	 * [0] -- Reserved
	 * [7:1] -- Base value of number of active cores of bucket 1.
	 * [15:8] -- Base value of freq ratio of bucket 1.
	 * [20:16] -- +ve delta of number of active cores of bucket 2.
	 * i.e. active cores of bucket 2 =
	 * active cores of bucket 1 + delta
	 * [23:21] -- Negative delta of freq ratio of bucket 2.
	 * i.e. freq ratio of bucket 2 =
	 * freq ratio of bucket 1 - delta
	 * [28:24]-- +ve delta of number of active cores of bucket 3.
	 * [31:29]-- -ve delta of freq ratio of bucket 3.
	 * [36:32]-- +ve delta of number of active cores of bucket 4.
	 * [39:37]-- -ve delta of freq ratio of bucket 4.
	 * [44:40]-- +ve delta of number of active cores of bucket 5.
	 * [47:45]-- -ve delta of freq ratio of bucket 5.
	 * [52:48]-- +ve delta of number of active cores of bucket 6.
	 * [55:53]-- -ve delta of freq ratio of bucket 6.
	 * [60:56]-- +ve delta of number of active cores of bucket 7.
	 * [63:61]-- -ve delta of freq ratio of bucket 7.
	 */

	b_nr = 0;
	cores[b_nr] = (msr & 0xFF) >> 1;
	ratio[b_nr] = (msr >> 8) & 0xFF;

	for (i = 16; i < 64; i += 8) {
		delta_cores = (msr >> i) & 0x1F;
		delta_ratio = (msr >> (i + 5)) & 0x7;

		cores[b_nr + 1] = cores[b_nr] + delta_cores;
		ratio[b_nr + 1] = ratio[b_nr] - delta_ratio;
		b_nr++;
	}

	for (i = buckets_no - 1; i >= 0; i--)
		if (i > 0 ? ratio[i] != ratio[i - 1] : 1)
			fprintf(outf,
				"%d * %.1f = %.1f MHz max turbo %d active cores\n",
				ratio[i], bclk, ratio[i] * bclk, cores[i]);
}

static void dump_nhm_cst_cfg(void)
{
	unsigned long long msr;

	get_msr(base_cpu, MSR_PKG_CST_CONFIG_CONTROL, &msr);

	fprintf(outf, "cpu%d: MSR_PKG_CST_CONFIG_CONTROL: 0x%08llx", base_cpu, msr);

	fprintf(outf, " (%s%s%s%s%slocked, pkg-cstate-limit=%d (%s)",
		(msr & SNB_C3_AUTO_UNDEMOTE) ? "UNdemote-C3, " : "",
		(msr & SNB_C1_AUTO_UNDEMOTE) ? "UNdemote-C1, " : "",
		(msr & NHM_C3_AUTO_DEMOTE) ? "demote-C3, " : "",
		(msr & NHM_C1_AUTO_DEMOTE) ? "demote-C1, " : "",
		(msr & (1 << 15)) ? "" : "UN", (unsigned int)msr & 0xF, pkg_cstate_limit_strings[pkg_cstate_limit]);

#define AUTOMATIC_CSTATE_CONVERSION		(1UL << 16)
	if (has_automatic_cstate_conversion) {
		fprintf(outf, ", automatic c-state conversion=%s", (msr & AUTOMATIC_CSTATE_CONVERSION) ? "on" : "off");
	}

	fprintf(outf, ")\n");

	return;
}

static void dump_config_tdp(void)
{
	unsigned long long msr;

	get_msr(base_cpu, MSR_CONFIG_TDP_NOMINAL, &msr);
	fprintf(outf, "cpu%d: MSR_CONFIG_TDP_NOMINAL: 0x%08llx", base_cpu, msr);
	fprintf(outf, " (base_ratio=%d)\n", (unsigned int)msr & 0xFF);

	get_msr(base_cpu, MSR_CONFIG_TDP_LEVEL_1, &msr);
	fprintf(outf, "cpu%d: MSR_CONFIG_TDP_LEVEL_1: 0x%08llx (", base_cpu, msr);
	if (msr) {
		fprintf(outf, "PKG_MIN_PWR_LVL1=%d ", (unsigned int)(msr >> 48) & 0x7FFF);
		fprintf(outf, "PKG_MAX_PWR_LVL1=%d ", (unsigned int)(msr >> 32) & 0x7FFF);
		fprintf(outf, "LVL1_RATIO=%d ", (unsigned int)(msr >> 16) & 0xFF);
		fprintf(outf, "PKG_TDP_LVL1=%d", (unsigned int)(msr) & 0x7FFF);
	}
	fprintf(outf, ")\n");

	get_msr(base_cpu, MSR_CONFIG_TDP_LEVEL_2, &msr);
	fprintf(outf, "cpu%d: MSR_CONFIG_TDP_LEVEL_2: 0x%08llx (", base_cpu, msr);
	if (msr) {
		fprintf(outf, "PKG_MIN_PWR_LVL2=%d ", (unsigned int)(msr >> 48) & 0x7FFF);
		fprintf(outf, "PKG_MAX_PWR_LVL2=%d ", (unsigned int)(msr >> 32) & 0x7FFF);
		fprintf(outf, "LVL2_RATIO=%d ", (unsigned int)(msr >> 16) & 0xFF);
		fprintf(outf, "PKG_TDP_LVL2=%d", (unsigned int)(msr) & 0x7FFF);
	}
	fprintf(outf, ")\n");

	get_msr(base_cpu, MSR_CONFIG_TDP_CONTROL, &msr);
	fprintf(outf, "cpu%d: MSR_CONFIG_TDP_CONTROL: 0x%08llx (", base_cpu, msr);
	if ((msr) & 0x3)
		fprintf(outf, "TDP_LEVEL=%d ", (unsigned int)(msr) & 0x3);
	fprintf(outf, " lock=%d", (unsigned int)(msr >> 31) & 1);
	fprintf(outf, ")\n");

	get_msr(base_cpu, MSR_TURBO_ACTIVATION_RATIO, &msr);
	fprintf(outf, "cpu%d: MSR_TURBO_ACTIVATION_RATIO: 0x%08llx (", base_cpu, msr);
	fprintf(outf, "MAX_NON_TURBO_RATIO=%d", (unsigned int)(msr) & 0xFF);
	fprintf(outf, " lock=%d", (unsigned int)(msr >> 31) & 1);
	fprintf(outf, ")\n");
}

unsigned int irtl_time_units[] = { 1, 32, 1024, 32768, 1048576, 33554432, 0, 0 };

void print_irtl(void)
{
	unsigned long long msr;

	get_msr(base_cpu, MSR_PKGC3_IRTL, &msr);
	fprintf(outf, "cpu%d: MSR_PKGC3_IRTL: 0x%08llx (", base_cpu, msr);
	fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
		(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);

	get_msr(base_cpu, MSR_PKGC6_IRTL, &msr);
	fprintf(outf, "cpu%d: MSR_PKGC6_IRTL: 0x%08llx (", base_cpu, msr);
	fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
		(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);

	get_msr(base_cpu, MSR_PKGC7_IRTL, &msr);
	fprintf(outf, "cpu%d: MSR_PKGC7_IRTL: 0x%08llx (", base_cpu, msr);
	fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
		(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);

	if (!do_irtl_hsw)
		return;

	get_msr(base_cpu, MSR_PKGC8_IRTL, &msr);
	fprintf(outf, "cpu%d: MSR_PKGC8_IRTL: 0x%08llx (", base_cpu, msr);
	fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
		(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);

	get_msr(base_cpu, MSR_PKGC9_IRTL, &msr);
	fprintf(outf, "cpu%d: MSR_PKGC9_IRTL: 0x%08llx (", base_cpu, msr);
	fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
		(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);

	get_msr(base_cpu, MSR_PKGC10_IRTL, &msr);
	fprintf(outf, "cpu%d: MSR_PKGC10_IRTL: 0x%08llx (", base_cpu, msr);
	fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
		(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);

}

void free_fd_percpu(void)
{
	int i;

	for (i = 0; i < topo.max_cpu_num + 1; ++i) {
		if (fd_percpu[i] != 0)
			close(fd_percpu[i]);
	}

	free(fd_percpu);
}

void free_all_buffers(void)
{
	int i;

	CPU_FREE(cpu_present_set);
	cpu_present_set = NULL;
	cpu_present_setsize = 0;

	CPU_FREE(cpu_affinity_set);
	cpu_affinity_set = NULL;
	cpu_affinity_setsize = 0;

	free(thread_even);
	free(core_even);
	free(package_even);

	thread_even = NULL;
	core_even = NULL;
	package_even = NULL;

	free(thread_odd);
	free(core_odd);
	free(package_odd);

	thread_odd = NULL;
	core_odd = NULL;
	package_odd = NULL;

	free(output_buffer);
	output_buffer = NULL;
	outp = NULL;

	free_fd_percpu();

	free(irq_column_2_cpu);
	free(irqs_per_cpu);

	for (i = 0; i <= topo.max_cpu_num; ++i) {
		if (cpus[i].put_ids)
			CPU_FREE(cpus[i].put_ids);
	}
	free(cpus);
}

/*
 * Parse a file containing a single int.
 * Return 0 if file can not be opened
 * Exit if file can be opened, but can not be parsed
 */
int parse_int_file(const char *fmt, ...)
{
	va_list args;
	char path[PATH_MAX];
	FILE *filep;
	int value;

	va_start(args, fmt);
	vsnprintf(path, sizeof(path), fmt, args);
	va_end(args);
	filep = fopen(path, "r");
	if (!filep)
		return 0;
	if (fscanf(filep, "%d", &value) != 1)
		err(1, "%s: failed to parse number from file", path);
	fclose(filep);
	return value;
}

/*
 * cpu_is_first_core_in_package(cpu)
 * return 1 if given CPU is 1st core in package
 */
int cpu_is_first_core_in_package(int cpu)
{
	return cpu == parse_int_file("/sys/devices/system/cpu/cpu%d/topology/core_siblings_list", cpu);
}

int get_physical_package_id(int cpu)
{
	return parse_int_file("/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
}

int get_die_id(int cpu)
{
	return parse_int_file("/sys/devices/system/cpu/cpu%d/topology/die_id", cpu);
}

int get_core_id(int cpu)
{
	return parse_int_file("/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
}

void set_node_data(void)
{
	int pkg, node, lnode, cpu, cpux;
	int cpu_count;

	/* initialize logical_node_id */
	for (cpu = 0; cpu <= topo.max_cpu_num; ++cpu)
		cpus[cpu].logical_node_id = -1;

	cpu_count = 0;
	for (pkg = 0; pkg < topo.num_packages; pkg++) {
		lnode = 0;
		for (cpu = 0; cpu <= topo.max_cpu_num; ++cpu) {
			if (cpus[cpu].physical_package_id != pkg)
				continue;
			/* find a cpu with an unset logical_node_id */
			if (cpus[cpu].logical_node_id != -1)
				continue;
			cpus[cpu].logical_node_id = lnode;
			node = cpus[cpu].physical_node_id;
			cpu_count++;
			/*
			 * find all matching cpus on this pkg and set
			 * the logical_node_id
			 */
			for (cpux = cpu; cpux <= topo.max_cpu_num; cpux++) {
				if ((cpus[cpux].physical_package_id == pkg) && (cpus[cpux].physical_node_id == node)) {
					cpus[cpux].logical_node_id = lnode;
					cpu_count++;
				}
			}
			lnode++;
			if (lnode > topo.nodes_per_pkg)
				topo.nodes_per_pkg = lnode;
		}
		if (cpu_count >= topo.max_cpu_num)
			break;
	}
}

int get_physical_node_id(struct cpu_topology *thiscpu)
{
	char path[80];
	FILE *filep;
	int i;
	int cpu = thiscpu->logical_cpu_id;

	for (i = 0; i <= topo.max_cpu_num; i++) {
		sprintf(path, "/sys/devices/system/cpu/cpu%d/node%i/cpulist", cpu, i);
		filep = fopen(path, "r");
		if (!filep)
			continue;
		fclose(filep);
		return i;
	}
	return -1;
}

int get_thread_siblings(struct cpu_topology *thiscpu)
{
	char path[80], character;
	FILE *filep;
	unsigned long map;
	int so, shift, sib_core;
	int cpu = thiscpu->logical_cpu_id;
	int offset = topo.max_cpu_num + 1;
	size_t size;
	int thread_id = 0;

	thiscpu->put_ids = CPU_ALLOC((topo.max_cpu_num + 1));
	if (thiscpu->thread_id < 0)
		thiscpu->thread_id = thread_id++;
	if (!thiscpu->put_ids)
		return -1;

	size = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(size, thiscpu->put_ids);

	sprintf(path, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings", cpu);
	filep = fopen(path, "r");

	if (!filep) {
		warnx("%s: open failed", path);
		return -1;
	}
	do {
		offset -= BITMASK_SIZE;
		if (fscanf(filep, "%lx%c", &map, &character) != 2)
			err(1, "%s: failed to parse file", path);
		for (shift = 0; shift < BITMASK_SIZE; shift++) {
			if ((map >> shift) & 0x1) {
				so = shift + offset;
				sib_core = get_core_id(so);
				if (sib_core == thiscpu->physical_core_id) {
					CPU_SET_S(so, size, thiscpu->put_ids);
					if ((so != cpu) && (cpus[so].thread_id < 0))
						cpus[so].thread_id = thread_id++;
				}
			}
		}
	} while (character == ',');
	fclose(filep);

	return CPU_COUNT_S(size, thiscpu->put_ids);
}

/*
 * run func(thread, core, package) in topology order
 * skip non-present cpus
 */

int for_all_cpus_2(int (func) (struct thread_data *, struct core_data *,
			       struct pkg_data *, struct thread_data *, struct core_data *,
			       struct pkg_data *), struct thread_data *thread_base,
		   struct core_data *core_base, struct pkg_data *pkg_base,
		   struct thread_data *thread_base2, struct core_data *core_base2, struct pkg_data *pkg_base2)
{
	int retval, pkg_no, node_no, core_no, thread_no;

	for (pkg_no = 0; pkg_no < topo.num_packages; ++pkg_no) {
		for (node_no = 0; node_no < topo.nodes_per_pkg; ++node_no) {
			for (core_no = 0; core_no < topo.cores_per_node; ++core_no) {
				for (thread_no = 0; thread_no < topo.threads_per_core; ++thread_no) {
					struct thread_data *t, *t2;
					struct core_data *c, *c2;
					struct pkg_data *p, *p2;

					t = GET_THREAD(thread_base, thread_no, core_no, node_no, pkg_no);

					if (cpu_is_not_present(t->cpu_id))
						continue;

					t2 = GET_THREAD(thread_base2, thread_no, core_no, node_no, pkg_no);

					c = GET_CORE(core_base, core_no, node_no, pkg_no);
					c2 = GET_CORE(core_base2, core_no, node_no, pkg_no);

					p = GET_PKG(pkg_base, pkg_no);
					p2 = GET_PKG(pkg_base2, pkg_no);

					retval = func(t, c, p, t2, c2, p2);
					if (retval)
						return retval;
				}
			}
		}
	}
	return 0;
}

/*
 * run func(cpu) on every cpu in /proc/stat
 * return max_cpu number
 */
int for_all_proc_cpus(int (func) (int))
{
	FILE *fp;
	int cpu_num;
	int retval;

	fp = fopen_or_die(proc_stat, "r");

	retval = fscanf(fp, "cpu %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d\n");
	if (retval != 0)
		err(1, "%s: failed to parse format", proc_stat);

	while (1) {
		retval = fscanf(fp, "cpu%u %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d\n", &cpu_num);
		if (retval != 1)
			break;

		retval = func(cpu_num);
		if (retval) {
			fclose(fp);
			return (retval);
		}
	}
	fclose(fp);
	return 0;
}

void re_initialize(void)
{
	free_all_buffers();
	setup_all_buffers();
	fprintf(outf, "turbostat: re-initialized with num_cpus %d\n", topo.num_cpus);
}

void set_max_cpu_num(void)
{
	FILE *filep;
	int base_cpu;
	unsigned long dummy;
	char pathname[64];

	base_cpu = sched_getcpu();
	if (base_cpu < 0)
		err(1, "cannot find calling cpu ID");
	sprintf(pathname, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings", base_cpu);

	filep = fopen_or_die(pathname, "r");
	topo.max_cpu_num = 0;
	while (fscanf(filep, "%lx,", &dummy) == 1)
		topo.max_cpu_num += BITMASK_SIZE;
	fclose(filep);
	topo.max_cpu_num--;	/* 0 based */
}

/*
 * count_cpus()
 * remember the last one seen, it will be the max
 */
int count_cpus(int cpu)
{
	UNUSED(cpu);

	topo.num_cpus++;
	return 0;
}

int mark_cpu_present(int cpu)
{
	CPU_SET_S(cpu, cpu_present_setsize, cpu_present_set);
	return 0;
}

int init_thread_id(int cpu)
{
	cpus[cpu].thread_id = -1;
	return 0;
}

/*
 * snapshot_proc_interrupts()
 *
 * read and record summary of /proc/interrupts
 *
 * return 1 if config change requires a restart, else return 0
 */
int snapshot_proc_interrupts(void)
{
	static FILE *fp;
	int column, retval;

	if (fp == NULL)
		fp = fopen_or_die("/proc/interrupts", "r");
	else
		rewind(fp);

	/* read 1st line of /proc/interrupts to get cpu* name for each column */
	for (column = 0; column < topo.num_cpus; ++column) {
		int cpu_number;

		retval = fscanf(fp, " CPU%d", &cpu_number);
		if (retval != 1)
			break;

		if (cpu_number > topo.max_cpu_num) {
			warn("/proc/interrupts: cpu%d: > %d", cpu_number, topo.max_cpu_num);
			return 1;
		}

		irq_column_2_cpu[column] = cpu_number;
		irqs_per_cpu[cpu_number] = 0;
	}

	/* read /proc/interrupt count lines and sum up irqs per cpu */
	while (1) {
		int column;
		char buf[64];

		retval = fscanf(fp, " %s:", buf);	/* flush irq# "N:" */
		if (retval != 1)
			break;

		/* read the count per cpu */
		for (column = 0; column < topo.num_cpus; ++column) {

			int cpu_number, irq_count;

			retval = fscanf(fp, " %d", &irq_count);
			if (retval != 1)
				break;

			cpu_number = irq_column_2_cpu[column];
			irqs_per_cpu[cpu_number] += irq_count;

		}

		while (getc(fp) != '\n') ;	/* flush interrupt description */

	}
	return 0;
}

/*
 * snapshot_gfx_rc6_ms()
 *
 * record snapshot of
 * /sys/class/drm/card0/power/rc6_residency_ms
 *
 * return 1 if config change requires a restart, else return 0
 */
int snapshot_gfx_rc6_ms(void)
{
	FILE *fp;
	int retval;

	fp = fopen_or_die("/sys/class/drm/card0/power/rc6_residency_ms", "r");

	retval = fscanf(fp, "%lld", &gfx_cur_rc6_ms);
	if (retval != 1)
		err(1, "GFX rc6");

	fclose(fp);

	return 0;
}

/*
 * snapshot_gfx_mhz()
 *
 * record snapshot of
 * /sys/class/graphics/fb0/device/drm/card0/gt_cur_freq_mhz
 *
 * return 1 if config change requires a restart, else return 0
 */
int snapshot_gfx_mhz(void)
{
	static FILE *fp;
	int retval;

	if (fp == NULL)
		fp = fopen_or_die("/sys/class/graphics/fb0/device/drm/card0/gt_cur_freq_mhz", "r");
	else {
		rewind(fp);
		fflush(fp);
	}

	retval = fscanf(fp, "%d", &gfx_cur_mhz);
	if (retval != 1)
		err(1, "GFX MHz");

	return 0;
}

/*
 * snapshot_gfx_cur_mhz()
 *
 * record snapshot of
 * /sys/class/graphics/fb0/device/drm/card0/gt_act_freq_mhz
 *
 * return 1 if config change requires a restart, else return 0
 */
int snapshot_gfx_act_mhz(void)
{
	static FILE *fp;
	int retval;

	if (fp == NULL)
		fp = fopen_or_die("/sys/class/graphics/fb0/device/drm/card0/gt_act_freq_mhz", "r");
	else {
		rewind(fp);
		fflush(fp);
	}

	retval = fscanf(fp, "%d", &gfx_act_mhz);
	if (retval != 1)
		err(1, "GFX ACT MHz");

	return 0;
}

/*
 * snapshot_cpu_lpi()
 *
 * record snapshot of
 * /sys/devices/system/cpu/cpuidle/low_power_idle_cpu_residency_us
 */
int snapshot_cpu_lpi_us(void)
{
	FILE *fp;
	int retval;

	fp = fopen_or_die("/sys/devices/system/cpu/cpuidle/low_power_idle_cpu_residency_us", "r");

	retval = fscanf(fp, "%lld", &cpuidle_cur_cpu_lpi_us);
	if (retval != 1) {
		fprintf(stderr, "Disabling Low Power Idle CPU output\n");
		BIC_NOT_PRESENT(BIC_CPU_LPI);
		fclose(fp);
		return -1;
	}

	fclose(fp);

	return 0;
}

/*
 * snapshot_sys_lpi()
 *
 * record snapshot of sys_lpi_file
 */
int snapshot_sys_lpi_us(void)
{
	FILE *fp;
	int retval;

	fp = fopen_or_die(sys_lpi_file, "r");

	retval = fscanf(fp, "%lld", &cpuidle_cur_sys_lpi_us);
	if (retval != 1) {
		fprintf(stderr, "Disabling Low Power Idle System output\n");
		BIC_NOT_PRESENT(BIC_SYS_LPI);
		fclose(fp);
		return -1;
	}
	fclose(fp);

	return 0;
}

/*
 * snapshot /proc and /sys files
 *
 * return 1 if configuration restart needed, else return 0
 */
int snapshot_proc_sysfs_files(void)
{
	if (DO_BIC(BIC_IRQ))
		if (snapshot_proc_interrupts())
			return 1;

	if (DO_BIC(BIC_GFX_rc6))
		snapshot_gfx_rc6_ms();

	if (DO_BIC(BIC_GFXMHz))
		snapshot_gfx_mhz();

	if (DO_BIC(BIC_GFXACTMHz))
		snapshot_gfx_act_mhz();

	if (DO_BIC(BIC_CPU_LPI))
		snapshot_cpu_lpi_us();

	if (DO_BIC(BIC_SYS_LPI))
		snapshot_sys_lpi_us();

	return 0;
}

int exit_requested;

static void signal_handler(int signal)
{
	switch (signal) {
	case SIGINT:
		exit_requested = 1;
		if (debug)
			fprintf(stderr, " SIGINT\n");
		break;
	case SIGUSR1:
		if (debug > 1)
			fprintf(stderr, "SIGUSR1\n");
		break;
	}
}

void setup_signal_handler(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));

	sa.sa_handler = &signal_handler;

	if (sigaction(SIGINT, &sa, NULL) < 0)
		err(1, "sigaction SIGINT");
	if (sigaction(SIGUSR1, &sa, NULL) < 0)
		err(1, "sigaction SIGUSR1");
}

void do_sleep(void)
{
	struct timeval tout;
	struct timespec rest;
	fd_set readfds;
	int retval;

	FD_ZERO(&readfds);
	FD_SET(0, &readfds);

	if (ignore_stdin) {
		nanosleep(&interval_ts, NULL);
		return;
	}

	tout = interval_tv;
	retval = select(1, &readfds, NULL, NULL, &tout);

	if (retval == 1) {
		switch (getc(stdin)) {
		case 'q':
			exit_requested = 1;
			break;
		case EOF:
			/*
			 * 'stdin' is a pipe closed on the other end. There
			 * won't be any further input.
			 */
			ignore_stdin = 1;
			/* Sleep the rest of the time */
			rest.tv_sec = (tout.tv_sec + tout.tv_usec / 1000000);
			rest.tv_nsec = (tout.tv_usec % 1000000) * 1000;
			nanosleep(&rest, NULL);
		}
	}
}

int get_msr_sum(int cpu, off_t offset, unsigned long long *msr)
{
	int ret, idx;
	unsigned long long msr_cur, msr_last;

	if (!per_cpu_msr_sum)
		return 1;

	idx = offset_to_idx(offset);
	if (idx < 0)
		return idx;
	/* get_msr_sum() = sum + (get_msr() - last) */
	ret = get_msr(cpu, offset, &msr_cur);
	if (ret)
		return ret;
	msr_last = per_cpu_msr_sum[cpu].entries[idx].last;
	DELTA_WRAP32(msr_cur, msr_last);
	*msr = msr_last + per_cpu_msr_sum[cpu].entries[idx].sum;

	return 0;
}

timer_t timerid;

/* Timer callback, update the sum of MSRs periodically. */
static int update_msr_sum(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	int i, ret;
	int cpu = t->cpu_id;

	UNUSED(c);
	UNUSED(p);

	for (i = IDX_PKG_ENERGY; i < IDX_COUNT; i++) {
		unsigned long long msr_cur, msr_last;
		off_t offset;

		if (!idx_valid(i))
			continue;
		offset = idx_to_offset(i);
		if (offset < 0)
			continue;
		ret = get_msr(cpu, offset, &msr_cur);
		if (ret) {
			fprintf(outf, "Can not update msr(0x%llx)\n", (unsigned long long)offset);
			continue;
		}

		msr_last = per_cpu_msr_sum[cpu].entries[i].last;
		per_cpu_msr_sum[cpu].entries[i].last = msr_cur & 0xffffffff;

		DELTA_WRAP32(msr_cur, msr_last);
		per_cpu_msr_sum[cpu].entries[i].sum += msr_last;
	}
	return 0;
}

static void msr_record_handler(union sigval v)
{
	UNUSED(v);

	for_all_cpus(update_msr_sum, EVEN_COUNTERS);
}

void msr_sum_record(void)
{
	struct itimerspec its;
	struct sigevent sev;

	per_cpu_msr_sum = calloc(topo.max_cpu_num + 1, sizeof(struct msr_sum_array));
	if (!per_cpu_msr_sum) {
		fprintf(outf, "Can not allocate memory for long time MSR.\n");
		return;
	}
	/*
	 * Signal handler might be restricted, so use thread notifier instead.
	 */
	memset(&sev, 0, sizeof(struct sigevent));
	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = msr_record_handler;

	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
		fprintf(outf, "Can not create timer.\n");
		goto release_msr;
	}

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 1;
	/*
	 * A wraparound time has been calculated early.
	 * Some sources state that the peak power for a
	 * microprocessor is usually 1.5 times the TDP rating,
	 * use 2 * TDP for safety.
	 */
	its.it_interval.tv_sec = rapl_joule_counter_range / 2;
	its.it_interval.tv_nsec = 0;

	if (timer_settime(timerid, 0, &its, NULL) == -1) {
		fprintf(outf, "Can not set timer.\n");
		goto release_timer;
	}
	return;

release_timer:
	timer_delete(timerid);
release_msr:
	free(per_cpu_msr_sum);
}

/*
 * set_my_sched_priority(pri)
 * return previous
 *
 * if non-root, do this:
 * # /sbin/setcap cap_sys_rawio,cap_sys_nice=+ep /usr/bin/turbostat
 */
int set_my_sched_priority(int priority)
{
	int retval;
	int original_priority;

	errno = 0;
	original_priority = getpriority(PRIO_PROCESS, 0);
	if (errno && (original_priority == -1))
		err(errno, "getpriority");

	retval = setpriority(PRIO_PROCESS, 0, priority);
	if (retval)
		err(retval, "setpriority(%d)", priority);

	errno = 0;
	retval = getpriority(PRIO_PROCESS, 0);
	if (retval != priority)
		err(retval, "getpriority(%d) != setpriority(%d)", retval, priority);

	return original_priority;
}

void turbostat_loop()
{
	int retval;
	int restarted = 0;
	unsigned int done_iters = 0;

	setup_signal_handler();

	/*
	 * elevate own priority for interval mode
	 */
	set_my_sched_priority(-20);

restart:
	restarted++;

	snapshot_proc_sysfs_files();
	retval = for_all_cpus(get_counters, EVEN_COUNTERS);
	first_counter_read = 0;
	if (retval < -1) {
		exit(retval);
	} else if (retval == -1) {
		if (restarted > 10) {
			exit(retval);
		}
		re_initialize();
		goto restart;
	}
	restarted = 0;
	done_iters = 0;
	gettimeofday(&tv_even, (struct timezone *)NULL);

	while (1) {
		if (for_all_proc_cpus(cpu_is_not_present)) {
			re_initialize();
			goto restart;
		}
		do_sleep();
		if (snapshot_proc_sysfs_files())
			goto restart;
		retval = for_all_cpus(get_counters, ODD_COUNTERS);
		if (retval < -1) {
			exit(retval);
		} else if (retval == -1) {
			re_initialize();
			goto restart;
		}
		gettimeofday(&tv_odd, (struct timezone *)NULL);
		timersub(&tv_odd, &tv_even, &tv_delta);
		if (for_all_cpus_2(delta_cpu, ODD_COUNTERS, EVEN_COUNTERS)) {
			re_initialize();
			goto restart;
		}
		compute_average(EVEN_COUNTERS);
		format_all_counters(EVEN_COUNTERS);
		flush_output_stdout();
		if (exit_requested)
			break;
		if (num_iterations && ++done_iters >= num_iterations)
			break;
		do_sleep();
		if (snapshot_proc_sysfs_files())
			goto restart;
		retval = for_all_cpus(get_counters, EVEN_COUNTERS);
		if (retval < -1) {
			exit(retval);
		} else if (retval == -1) {
			re_initialize();
			goto restart;
		}
		gettimeofday(&tv_even, (struct timezone *)NULL);
		timersub(&tv_even, &tv_odd, &tv_delta);
		if (for_all_cpus_2(delta_cpu, EVEN_COUNTERS, ODD_COUNTERS)) {
			re_initialize();
			goto restart;
		}
		compute_average(ODD_COUNTERS);
		format_all_counters(ODD_COUNTERS);
		flush_output_stdout();
		if (exit_requested)
			break;
		if (num_iterations && ++done_iters >= num_iterations)
			break;
	}
}

void check_dev_msr()
{
	struct stat sb;
	char pathname[32];

	sprintf(pathname, "/dev/cpu/%d/msr", base_cpu);
	if (stat(pathname, &sb))
		if (system("/sbin/modprobe msr > /dev/null 2>&1"))
			err(-5, "no /dev/cpu/0/msr, Try \"# modprobe msr\" ");
}

/*
 * check for CAP_SYS_RAWIO
 * return 0 on success
 * return 1 on fail
 */
int check_for_cap_sys_rawio(void)
{
	cap_t caps;
	cap_flag_value_t cap_flag_value;

	caps = cap_get_proc();
	if (caps == NULL)
		err(-6, "cap_get_proc\n");

	if (cap_get_flag(caps, CAP_SYS_RAWIO, CAP_EFFECTIVE, &cap_flag_value))
		err(-6, "cap_get\n");

	if (cap_flag_value != CAP_SET) {
		warnx("capget(CAP_SYS_RAWIO) failed," " try \"# setcap cap_sys_rawio=ep %s\"", progname);
		return 1;
	}

	if (cap_free(caps) == -1)
		err(-6, "cap_free\n");

	return 0;
}

void check_permissions(void)
{
	int do_exit = 0;
	char pathname[32];

	/* check for CAP_SYS_RAWIO */
	do_exit += check_for_cap_sys_rawio();

	/* test file permissions */
	sprintf(pathname, "/dev/cpu/%d/msr", base_cpu);
	if (euidaccess(pathname, R_OK)) {
		do_exit++;
		warn("/dev/cpu/0/msr open failed, try chown or chmod +r /dev/cpu/*/msr");
	}

	/* if all else fails, thell them to be root */
	if (do_exit)
		if (getuid() != 0)
			warnx("... or simply run as root");

	if (do_exit)
		exit(-6);
}

/*
 * NHM adds support for additional MSRs:
 *
 * MSR_SMI_COUNT                   0x00000034
 *
 * MSR_PLATFORM_INFO               0x000000ce
 * MSR_PKG_CST_CONFIG_CONTROL     0x000000e2
 *
 * MSR_MISC_PWR_MGMT               0x000001aa
 *
 * MSR_PKG_C3_RESIDENCY            0x000003f8
 * MSR_PKG_C6_RESIDENCY            0x000003f9
 * MSR_CORE_C3_RESIDENCY           0x000003fc
 * MSR_CORE_C6_RESIDENCY           0x000003fd
 *
 * Side effect:
 * sets global pkg_cstate_limit to decode MSR_PKG_CST_CONFIG_CONTROL
 * sets has_misc_feature_control
 */
int probe_nhm_msrs(unsigned int family, unsigned int model)
{
	unsigned long long msr;
	unsigned int base_ratio;
	int *pkg_cstate_limits;

	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	bclk = discover_bclk(family, model);

	switch (model) {
	case INTEL_FAM6_NEHALEM:	/* Core i7 and i5 Processor - Clarksfield, Lynnfield, Jasper Forest */
	case INTEL_FAM6_NEHALEM_EX:	/* Nehalem-EX Xeon - Beckton */
		pkg_cstate_limits = nhm_pkg_cstate_limits;
		break;
	case INTEL_FAM6_SANDYBRIDGE:	/* SNB */
	case INTEL_FAM6_SANDYBRIDGE_X:	/* SNB Xeon */
	case INTEL_FAM6_IVYBRIDGE:	/* IVB */
	case INTEL_FAM6_IVYBRIDGE_X:	/* IVB Xeon */
		pkg_cstate_limits = snb_pkg_cstate_limits;
		has_misc_feature_control = 1;
		break;
	case INTEL_FAM6_HASWELL:	/* HSW */
	case INTEL_FAM6_HASWELL_G:	/* HSW */
	case INTEL_FAM6_HASWELL_X:	/* HSX */
	case INTEL_FAM6_HASWELL_L:	/* HSW */
	case INTEL_FAM6_BROADWELL:	/* BDW */
	case INTEL_FAM6_BROADWELL_G:	/* BDW */
	case INTEL_FAM6_BROADWELL_X:	/* BDX */
	case INTEL_FAM6_SKYLAKE_L:	/* SKL */
	case INTEL_FAM6_CANNONLAKE_L:	/* CNL */
		pkg_cstate_limits = hsw_pkg_cstate_limits;
		has_misc_feature_control = 1;
		break;
	case INTEL_FAM6_SKYLAKE_X:	/* SKX */
	case INTEL_FAM6_SAPPHIRERAPIDS_X:	/* SPR */
		pkg_cstate_limits = skx_pkg_cstate_limits;
		has_misc_feature_control = 1;
		break;
	case INTEL_FAM6_ICELAKE_X:	/* ICX */
		pkg_cstate_limits = icx_pkg_cstate_limits;
		has_misc_feature_control = 1;
		break;
	case INTEL_FAM6_ATOM_SILVERMONT:	/* BYT */
		no_MSR_MISC_PWR_MGMT = 1;
		/* FALLTHRU */
	case INTEL_FAM6_ATOM_SILVERMONT_D:	/* AVN */
		pkg_cstate_limits = slv_pkg_cstate_limits;
		break;
	case INTEL_FAM6_ATOM_AIRMONT:	/* AMT */
		pkg_cstate_limits = amt_pkg_cstate_limits;
		no_MSR_MISC_PWR_MGMT = 1;
		break;
	case INTEL_FAM6_XEON_PHI_KNL:	/* PHI */
		pkg_cstate_limits = phi_pkg_cstate_limits;
		break;
	case INTEL_FAM6_ATOM_GOLDMONT:	/* BXT */
	case INTEL_FAM6_ATOM_GOLDMONT_PLUS:
	case INTEL_FAM6_ATOM_GOLDMONT_D:	/* DNV */
	case INTEL_FAM6_ATOM_TREMONT:	/* EHL */
	case INTEL_FAM6_ATOM_TREMONT_D:	/* JVL */
		pkg_cstate_limits = glm_pkg_cstate_limits;
		break;
	default:
		return 0;
	}
	get_msr(base_cpu, MSR_PKG_CST_CONFIG_CONTROL, &msr);
	pkg_cstate_limit = pkg_cstate_limits[msr & 0xF];

	get_msr(base_cpu, MSR_PLATFORM_INFO, &msr);
	base_ratio = (msr >> 8) & 0xFF;

	base_hz = base_ratio * bclk * 1000000;
	has_base_hz = 1;
	return 1;
}

/*
 * SLV client has support for unique MSRs:
 *
 * MSR_CC6_DEMOTION_POLICY_CONFIG
 * MSR_MC6_DEMOTION_POLICY_CONFIG
 */

int has_slv_msrs(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_ATOM_SILVERMONT:
	case INTEL_FAM6_ATOM_SILVERMONT_MID:
	case INTEL_FAM6_ATOM_AIRMONT_MID:
		return 1;
	}
	return 0;
}

int is_dnv(unsigned int family, unsigned int model)
{

	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_ATOM_GOLDMONT_D:
		return 1;
	}
	return 0;
}

int is_bdx(unsigned int family, unsigned int model)
{

	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_BROADWELL_X:
		return 1;
	}
	return 0;
}

int is_skx(unsigned int family, unsigned int model)
{

	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_SKYLAKE_X:
		return 1;
	}
	return 0;
}

int is_icx(unsigned int family, unsigned int model)
{

	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_ICELAKE_X:
		return 1;
	}
	return 0;
}

int is_spr(unsigned int family, unsigned int model)
{

	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_SAPPHIRERAPIDS_X:
		return 1;
	}
	return 0;
}

int is_ehl(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_ATOM_TREMONT:
		return 1;
	}
	return 0;
}

int is_jvl(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_ATOM_TREMONT_D:
		return 1;
	}
	return 0;
}

int has_turbo_ratio_limit(unsigned int family, unsigned int model)
{
	if (has_slv_msrs(family, model))
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
		/* Nehalem compatible, but do not include turbo-ratio limit support */
	case INTEL_FAM6_NEHALEM_EX:	/* Nehalem-EX Xeon - Beckton */
	case INTEL_FAM6_XEON_PHI_KNL:	/* PHI - Knights Landing (different MSR definition) */
		return 0;
	default:
		return 1;
	}
}

int has_atom_turbo_ratio_limit(unsigned int family, unsigned int model)
{
	if (has_slv_msrs(family, model))
		return 1;

	return 0;
}

int has_ivt_turbo_ratio_limit(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_IVYBRIDGE_X:	/* IVB Xeon */
	case INTEL_FAM6_HASWELL_X:	/* HSW Xeon */
		return 1;
	default:
		return 0;
	}
}

int has_hsw_turbo_ratio_limit(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_HASWELL_X:	/* HSW Xeon */
		return 1;
	default:
		return 0;
	}
}

int has_knl_turbo_ratio_limit(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_XEON_PHI_KNL:	/* Knights Landing */
		return 1;
	default:
		return 0;
	}
}

int has_glm_turbo_ratio_limit(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_ATOM_GOLDMONT:
	case INTEL_FAM6_SKYLAKE_X:
	case INTEL_FAM6_ICELAKE_X:
	case INTEL_FAM6_SAPPHIRERAPIDS_X:
		return 1;
	default:
		return 0;
	}
}

int has_config_tdp(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_IVYBRIDGE:	/* IVB */
	case INTEL_FAM6_HASWELL:	/* HSW */
	case INTEL_FAM6_HASWELL_X:	/* HSX */
	case INTEL_FAM6_HASWELL_L:	/* HSW */
	case INTEL_FAM6_HASWELL_G:	/* HSW */
	case INTEL_FAM6_BROADWELL:	/* BDW */
	case INTEL_FAM6_BROADWELL_G:	/* BDW */
	case INTEL_FAM6_BROADWELL_X:	/* BDX */
	case INTEL_FAM6_SKYLAKE_L:	/* SKL */
	case INTEL_FAM6_CANNONLAKE_L:	/* CNL */
	case INTEL_FAM6_SKYLAKE_X:	/* SKX */
	case INTEL_FAM6_ICELAKE_X:	/* ICX */
	case INTEL_FAM6_SAPPHIRERAPIDS_X:	/* SPR */
	case INTEL_FAM6_XEON_PHI_KNL:	/* Knights Landing */
		return 1;
	default:
		return 0;
	}
}

/*
 * tcc_offset_bits:
 * 0: Tcc Offset not supported (Default)
 * 6: Bit 29:24 of MSR_PLATFORM_INFO
 * 4: Bit 27:24 of MSR_PLATFORM_INFO
 */
void check_tcc_offset(int model)
{
	unsigned long long msr;

	if (!genuine_intel)
		return;

	switch (model) {
	case INTEL_FAM6_SKYLAKE_L:
	case INTEL_FAM6_SKYLAKE:
	case INTEL_FAM6_KABYLAKE_L:
	case INTEL_FAM6_KABYLAKE:
	case INTEL_FAM6_ICELAKE_L:
	case INTEL_FAM6_ICELAKE:
	case INTEL_FAM6_TIGERLAKE_L:
	case INTEL_FAM6_TIGERLAKE:
	case INTEL_FAM6_COMETLAKE:
		if (!get_msr(base_cpu, MSR_PLATFORM_INFO, &msr)) {
			msr = (msr >> 30) & 1;
			if (msr)
				tcc_offset_bits = 6;
		}
		return;
	default:
		return;
	}
}

static void remove_underbar(char *s)
{
	char *to = s;

	while (*s) {
		if (*s != '_')
			*to++ = *s;
		s++;
	}

	*to = 0;
}

static void dump_turbo_ratio_info(unsigned int family, unsigned int model)
{
	if (!has_turbo)
		return;

	if (has_hsw_turbo_ratio_limit(family, model))
		dump_hsw_turbo_ratio_limits();

	if (has_ivt_turbo_ratio_limit(family, model))
		dump_ivt_turbo_ratio_limits();

	if (has_turbo_ratio_limit(family, model)) {
		dump_turbo_ratio_limits(MSR_TURBO_RATIO_LIMIT, family, model);

		if (is_hybrid)
			dump_turbo_ratio_limits(MSR_SECONDARY_TURBO_RATIO_LIMIT, family, model);
	}

	if (has_atom_turbo_ratio_limit(family, model))
		dump_atom_turbo_ratio_limits();

	if (has_knl_turbo_ratio_limit(family, model))
		dump_knl_turbo_ratio_limits();

	if (has_config_tdp(family, model))
		dump_config_tdp();
}

static void dump_cstate_pstate_config_info(unsigned int family, unsigned int model)
{
	if (!do_nhm_platform_info)
		return;

	dump_nhm_platform_info();
	dump_turbo_ratio_info(family, model);
	dump_nhm_cst_cfg();
}

static int read_sysfs_int(char *path)
{
	FILE *input;
	int retval = -1;

	input = fopen(path, "r");
	if (input == NULL) {
		if (debug)
			fprintf(outf, "NSFOD %s\n", path);
		return (-1);
	}
	if (fscanf(input, "%d", &retval) != 1)
		err(1, "%s: failed to read int from file", path);
	fclose(input);

	return (retval);
}

static void dump_sysfs_file(char *path)
{
	FILE *input;
	char cpuidle_buf[64];

	input = fopen(path, "r");
	if (input == NULL) {
		if (debug)
			fprintf(outf, "NSFOD %s\n", path);
		return;
	}
	if (!fgets(cpuidle_buf, sizeof(cpuidle_buf), input))
		err(1, "%s: failed to read file", path);
	fclose(input);

	fprintf(outf, "%s: %s", strrchr(path, '/') + 1, cpuidle_buf);
}

static void intel_uncore_frequency_probe(void)
{
	int i, j;
	char path[128];

	if (!genuine_intel)
		return;

	if (access("/sys/devices/system/cpu/intel_uncore_frequency/package_00_die_00", R_OK))
		return;

	if (!access("/sys/devices/system/cpu/intel_uncore_frequency/package_00_die_00/current_freq_khz", R_OK))
		BIC_PRESENT(BIC_UNCORE_MHZ);

	if (quiet)
		return;

	for (i = 0; i < topo.num_packages; ++i) {
		for (j = 0; j < topo.num_die; ++j) {
			int k, l;

			sprintf(path, "/sys/devices/system/cpu/intel_uncore_frequency/package_0%d_die_0%d/min_freq_khz",
				i, j);
			k = read_sysfs_int(path);
			sprintf(path, "/sys/devices/system/cpu/intel_uncore_frequency/package_0%d_die_0%d/max_freq_khz",
				i, j);
			l = read_sysfs_int(path);
			fprintf(outf, "Uncore Frequency pkg%d die%d: %d - %d MHz ", i, j, k / 1000, l / 1000);

			sprintf(path,
				"/sys/devices/system/cpu/intel_uncore_frequency/package_0%d_die_0%d/initial_min_freq_khz",
				i, j);
			k = read_sysfs_int(path);
			sprintf(path,
				"/sys/devices/system/cpu/intel_uncore_frequency/package_0%d_die_0%d/initial_max_freq_khz",
				i, j);
			l = read_sysfs_int(path);
			fprintf(outf, "(%d - %d MHz)\n", k / 1000, l / 1000);
		}
	}
}

static void dump_sysfs_cstate_config(void)
{
	char path[64];
	char name_buf[16];
	char desc[64];
	FILE *input;
	int state;
	char *sp;

	if (access("/sys/devices/system/cpu/cpuidle", R_OK)) {
		fprintf(outf, "cpuidle not loaded\n");
		return;
	}

	dump_sysfs_file("/sys/devices/system/cpu/cpuidle/current_driver");
	dump_sysfs_file("/sys/devices/system/cpu/cpuidle/current_governor");
	dump_sysfs_file("/sys/devices/system/cpu/cpuidle/current_governor_ro");

	for (state = 0; state < 10; ++state) {

		sprintf(path, "/sys/devices/system/cpu/cpu%d/cpuidle/state%d/name", base_cpu, state);
		input = fopen(path, "r");
		if (input == NULL)
			continue;
		if (!fgets(name_buf, sizeof(name_buf), input))
			err(1, "%s: failed to read file", path);

		/* truncate "C1-HSW\n" to "C1", or truncate "C1\n" to "C1" */
		sp = strchr(name_buf, '-');
		if (!sp)
			sp = strchrnul(name_buf, '\n');
		*sp = '\0';
		fclose(input);

		remove_underbar(name_buf);

		sprintf(path, "/sys/devices/system/cpu/cpu%d/cpuidle/state%d/desc", base_cpu, state);
		input = fopen(path, "r");
		if (input == NULL)
			continue;
		if (!fgets(desc, sizeof(desc), input))
			err(1, "%s: failed to read file", path);

		fprintf(outf, "cpu%d: %s: %s", base_cpu, name_buf, desc);
		fclose(input);
	}
}

static void dump_sysfs_pstate_config(void)
{
	char path[64];
	char driver_buf[64];
	char governor_buf[64];
	FILE *input;
	int turbo;

	sprintf(path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_driver", base_cpu);
	input = fopen(path, "r");
	if (input == NULL) {
		fprintf(outf, "NSFOD %s\n", path);
		return;
	}
	if (!fgets(driver_buf, sizeof(driver_buf), input))
		err(1, "%s: failed to read file", path);
	fclose(input);

	sprintf(path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", base_cpu);
	input = fopen(path, "r");
	if (input == NULL) {
		fprintf(outf, "NSFOD %s\n", path);
		return;
	}
	if (!fgets(governor_buf, sizeof(governor_buf), input))
		err(1, "%s: failed to read file", path);
	fclose(input);

	fprintf(outf, "cpu%d: cpufreq driver: %s", base_cpu, driver_buf);
	fprintf(outf, "cpu%d: cpufreq governor: %s", base_cpu, governor_buf);

	sprintf(path, "/sys/devices/system/cpu/cpufreq/boost");
	input = fopen(path, "r");
	if (input != NULL) {
		if (fscanf(input, "%d", &turbo) != 1)
			err(1, "%s: failed to parse number from file", path);
		fprintf(outf, "cpufreq boost: %d\n", turbo);
		fclose(input);
	}

	sprintf(path, "/sys/devices/system/cpu/intel_pstate/no_turbo");
	input = fopen(path, "r");
	if (input != NULL) {
		if (fscanf(input, "%d", &turbo) != 1)
			err(1, "%s: failed to parse number from file", path);
		fprintf(outf, "cpufreq intel_pstate no_turbo: %d\n", turbo);
		fclose(input);
	}
}

/*
 * print_epb()
 * Decode the ENERGY_PERF_BIAS MSR
 */
int print_epb(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	char *epb_string;
	int cpu, epb;

	UNUSED(c);
	UNUSED(p);

	if (!has_epb)
		return 0;

	cpu = t->cpu_id;

	/* EPB is per-package */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE) || !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "print_epb: Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	epb = get_epb(cpu);
	if (epb < 0)
		return 0;

	switch (epb) {
	case ENERGY_PERF_BIAS_PERFORMANCE:
		epb_string = "performance";
		break;
	case ENERGY_PERF_BIAS_NORMAL:
		epb_string = "balanced";
		break;
	case ENERGY_PERF_BIAS_POWERSAVE:
		epb_string = "powersave";
		break;
	default:
		epb_string = "custom";
		break;
	}
	fprintf(outf, "cpu%d: EPB: %d (%s)\n", cpu, epb, epb_string);

	return 0;
}

/*
 * print_hwp()
 * Decode the MSR_HWP_CAPABILITIES
 */
int print_hwp(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned long long msr;
	int cpu;

	UNUSED(c);
	UNUSED(p);

	if (!has_hwp)
		return 0;

	cpu = t->cpu_id;

	/* MSR_HWP_CAPABILITIES is per-package */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE) || !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "print_hwp: Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (get_msr(cpu, MSR_PM_ENABLE, &msr))
		return 0;

	fprintf(outf, "cpu%d: MSR_PM_ENABLE: 0x%08llx (%sHWP)\n", cpu, msr, (msr & (1 << 0)) ? "" : "No-");

	/* MSR_PM_ENABLE[1] == 1 if HWP is enabled and MSRs visible */
	if ((msr & (1 << 0)) == 0)
		return 0;

	if (get_msr(cpu, MSR_HWP_CAPABILITIES, &msr))
		return 0;

	fprintf(outf, "cpu%d: MSR_HWP_CAPABILITIES: 0x%08llx "
		"(high %d guar %d eff %d low %d)\n",
		cpu, msr,
		(unsigned int)HWP_HIGHEST_PERF(msr),
		(unsigned int)HWP_GUARANTEED_PERF(msr),
		(unsigned int)HWP_MOSTEFFICIENT_PERF(msr), (unsigned int)HWP_LOWEST_PERF(msr));

	if (get_msr(cpu, MSR_HWP_REQUEST, &msr))
		return 0;

	fprintf(outf, "cpu%d: MSR_HWP_REQUEST: 0x%08llx "
		"(min %d max %d des %d epp 0x%x window 0x%x pkg 0x%x)\n",
		cpu, msr,
		(unsigned int)(((msr) >> 0) & 0xff),
		(unsigned int)(((msr) >> 8) & 0xff),
		(unsigned int)(((msr) >> 16) & 0xff),
		(unsigned int)(((msr) >> 24) & 0xff),
		(unsigned int)(((msr) >> 32) & 0xff3), (unsigned int)(((msr) >> 42) & 0x1));

	if (has_hwp_pkg) {
		if (get_msr(cpu, MSR_HWP_REQUEST_PKG, &msr))
			return 0;

		fprintf(outf, "cpu%d: MSR_HWP_REQUEST_PKG: 0x%08llx "
			"(min %d max %d des %d epp 0x%x window 0x%x)\n",
			cpu, msr,
			(unsigned int)(((msr) >> 0) & 0xff),
			(unsigned int)(((msr) >> 8) & 0xff),
			(unsigned int)(((msr) >> 16) & 0xff),
			(unsigned int)(((msr) >> 24) & 0xff), (unsigned int)(((msr) >> 32) & 0xff3));
	}
	if (has_hwp_notify) {
		if (get_msr(cpu, MSR_HWP_INTERRUPT, &msr))
			return 0;

		fprintf(outf, "cpu%d: MSR_HWP_INTERRUPT: 0x%08llx "
			"(%s_Guaranteed_Perf_Change, %s_Excursion_Min)\n",
			cpu, msr, ((msr) & 0x1) ? "EN" : "Dis", ((msr) & 0x2) ? "EN" : "Dis");
	}
	if (get_msr(cpu, MSR_HWP_STATUS, &msr))
		return 0;

	fprintf(outf, "cpu%d: MSR_HWP_STATUS: 0x%08llx "
		"(%sGuaranteed_Perf_Change, %sExcursion_Min)\n",
		cpu, msr, ((msr) & 0x1) ? "" : "No-", ((msr) & 0x4) ? "" : "No-");

	return 0;
}

/*
 * print_perf_limit()
 */
int print_perf_limit(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned long long msr;
	int cpu;

	UNUSED(c);
	UNUSED(p);

	cpu = t->cpu_id;

	/* per-package */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE) || !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "print_perf_limit: Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (do_core_perf_limit_reasons) {
		get_msr(cpu, MSR_CORE_PERF_LIMIT_REASONS, &msr);
		fprintf(outf, "cpu%d: MSR_CORE_PERF_LIMIT_REASONS, 0x%08llx", cpu, msr);
		fprintf(outf, " (Active: %s%s%s%s%s%s%s%s%s%s%s%s%s%s)",
			(msr & 1 << 15) ? "bit15, " : "",
			(msr & 1 << 14) ? "bit14, " : "",
			(msr & 1 << 13) ? "Transitions, " : "",
			(msr & 1 << 12) ? "MultiCoreTurbo, " : "",
			(msr & 1 << 11) ? "PkgPwrL2, " : "",
			(msr & 1 << 10) ? "PkgPwrL1, " : "",
			(msr & 1 << 9) ? "CorePwr, " : "",
			(msr & 1 << 8) ? "Amps, " : "",
			(msr & 1 << 6) ? "VR-Therm, " : "",
			(msr & 1 << 5) ? "Auto-HWP, " : "",
			(msr & 1 << 4) ? "Graphics, " : "",
			(msr & 1 << 2) ? "bit2, " : "",
			(msr & 1 << 1) ? "ThermStatus, " : "", (msr & 1 << 0) ? "PROCHOT, " : "");
		fprintf(outf, " (Logged: %s%s%s%s%s%s%s%s%s%s%s%s%s%s)\n",
			(msr & 1 << 31) ? "bit31, " : "",
			(msr & 1 << 30) ? "bit30, " : "",
			(msr & 1 << 29) ? "Transitions, " : "",
			(msr & 1 << 28) ? "MultiCoreTurbo, " : "",
			(msr & 1 << 27) ? "PkgPwrL2, " : "",
			(msr & 1 << 26) ? "PkgPwrL1, " : "",
			(msr & 1 << 25) ? "CorePwr, " : "",
			(msr & 1 << 24) ? "Amps, " : "",
			(msr & 1 << 22) ? "VR-Therm, " : "",
			(msr & 1 << 21) ? "Auto-HWP, " : "",
			(msr & 1 << 20) ? "Graphics, " : "",
			(msr & 1 << 18) ? "bit18, " : "",
			(msr & 1 << 17) ? "ThermStatus, " : "", (msr & 1 << 16) ? "PROCHOT, " : "");

	}
	if (do_gfx_perf_limit_reasons) {
		get_msr(cpu, MSR_GFX_PERF_LIMIT_REASONS, &msr);
		fprintf(outf, "cpu%d: MSR_GFX_PERF_LIMIT_REASONS, 0x%08llx", cpu, msr);
		fprintf(outf, " (Active: %s%s%s%s%s%s%s%s)",
			(msr & 1 << 0) ? "PROCHOT, " : "",
			(msr & 1 << 1) ? "ThermStatus, " : "",
			(msr & 1 << 4) ? "Graphics, " : "",
			(msr & 1 << 6) ? "VR-Therm, " : "",
			(msr & 1 << 8) ? "Amps, " : "",
			(msr & 1 << 9) ? "GFXPwr, " : "",
			(msr & 1 << 10) ? "PkgPwrL1, " : "", (msr & 1 << 11) ? "PkgPwrL2, " : "");
		fprintf(outf, " (Logged: %s%s%s%s%s%s%s%s)\n",
			(msr & 1 << 16) ? "PROCHOT, " : "",
			(msr & 1 << 17) ? "ThermStatus, " : "",
			(msr & 1 << 20) ? "Graphics, " : "",
			(msr & 1 << 22) ? "VR-Therm, " : "",
			(msr & 1 << 24) ? "Amps, " : "",
			(msr & 1 << 25) ? "GFXPwr, " : "",
			(msr & 1 << 26) ? "PkgPwrL1, " : "", (msr & 1 << 27) ? "PkgPwrL2, " : "");
	}
	if (do_ring_perf_limit_reasons) {
		get_msr(cpu, MSR_RING_PERF_LIMIT_REASONS, &msr);
		fprintf(outf, "cpu%d: MSR_RING_PERF_LIMIT_REASONS, 0x%08llx", cpu, msr);
		fprintf(outf, " (Active: %s%s%s%s%s%s)",
			(msr & 1 << 0) ? "PROCHOT, " : "",
			(msr & 1 << 1) ? "ThermStatus, " : "",
			(msr & 1 << 6) ? "VR-Therm, " : "",
			(msr & 1 << 8) ? "Amps, " : "",
			(msr & 1 << 10) ? "PkgPwrL1, " : "", (msr & 1 << 11) ? "PkgPwrL2, " : "");
		fprintf(outf, " (Logged: %s%s%s%s%s%s)\n",
			(msr & 1 << 16) ? "PROCHOT, " : "",
			(msr & 1 << 17) ? "ThermStatus, " : "",
			(msr & 1 << 22) ? "VR-Therm, " : "",
			(msr & 1 << 24) ? "Amps, " : "",
			(msr & 1 << 26) ? "PkgPwrL1, " : "", (msr & 1 << 27) ? "PkgPwrL2, " : "");
	}
	return 0;
}

#define	RAPL_POWER_GRANULARITY	0x7FFF	/* 15 bit power granularity */
#define	RAPL_TIME_GRANULARITY	0x3F	/* 6 bit time granularity */

double get_tdp_intel(unsigned int model)
{
	unsigned long long msr;

	if (do_rapl & RAPL_PKG_POWER_INFO)
		if (!get_msr(base_cpu, MSR_PKG_POWER_INFO, &msr))
			return ((msr >> 0) & RAPL_POWER_GRANULARITY) * rapl_power_units;

	switch (model) {
	case INTEL_FAM6_ATOM_SILVERMONT:
	case INTEL_FAM6_ATOM_SILVERMONT_D:
		return 30.0;
	default:
		return 135.0;
	}
}

double get_tdp_amd(unsigned int family)
{
	UNUSED(family);

	/* This is the max stock TDP of HEDT/Server Fam17h+ chips */
	return 280.0;
}

/*
 * rapl_dram_energy_units_probe()
 * Energy units are either hard-coded, or come from RAPL Energy Unit MSR.
 */
static double rapl_dram_energy_units_probe(int model, double rapl_energy_units)
{
	/* only called for genuine_intel, family 6 */

	switch (model) {
	case INTEL_FAM6_HASWELL_X:	/* HSX */
	case INTEL_FAM6_BROADWELL_X:	/* BDX */
	case INTEL_FAM6_SKYLAKE_X:	/* SKX */
	case INTEL_FAM6_XEON_PHI_KNL:	/* KNL */
	case INTEL_FAM6_ICELAKE_X:	/* ICX */
		return (rapl_dram_energy_units = 15.3 / 1000000);
	default:
		return (rapl_energy_units);
	}
}

void rapl_probe_intel(unsigned int family, unsigned int model)
{
	unsigned long long msr;
	unsigned int time_unit;
	double tdp;

	if (family != 6)
		return;

	switch (model) {
	case INTEL_FAM6_SANDYBRIDGE:
	case INTEL_FAM6_IVYBRIDGE:
	case INTEL_FAM6_HASWELL:	/* HSW */
	case INTEL_FAM6_HASWELL_L:	/* HSW */
	case INTEL_FAM6_HASWELL_G:	/* HSW */
	case INTEL_FAM6_BROADWELL:	/* BDW */
	case INTEL_FAM6_BROADWELL_G:	/* BDW */
		do_rapl = RAPL_PKG | RAPL_CORES | RAPL_CORE_POLICY | RAPL_GFX | RAPL_PKG_POWER_INFO;
		if (rapl_joules) {
			BIC_PRESENT(BIC_Pkg_J);
			BIC_PRESENT(BIC_Cor_J);
			BIC_PRESENT(BIC_GFX_J);
		} else {
			BIC_PRESENT(BIC_PkgWatt);
			BIC_PRESENT(BIC_CorWatt);
			BIC_PRESENT(BIC_GFXWatt);
		}
		break;
	case INTEL_FAM6_ATOM_GOLDMONT:	/* BXT */
	case INTEL_FAM6_ATOM_GOLDMONT_PLUS:
		do_rapl = RAPL_PKG | RAPL_PKG_POWER_INFO;
		if (rapl_joules)
			BIC_PRESENT(BIC_Pkg_J);
		else
			BIC_PRESENT(BIC_PkgWatt);
		break;
	case INTEL_FAM6_ATOM_TREMONT:	/* EHL */
		do_rapl =
		    RAPL_PKG | RAPL_CORES | RAPL_CORE_POLICY | RAPL_DRAM | RAPL_DRAM_PERF_STATUS | RAPL_PKG_PERF_STATUS
		    | RAPL_GFX | RAPL_PKG_POWER_INFO;
		if (rapl_joules) {
			BIC_PRESENT(BIC_Pkg_J);
			BIC_PRESENT(BIC_Cor_J);
			BIC_PRESENT(BIC_RAM_J);
			BIC_PRESENT(BIC_GFX_J);
		} else {
			BIC_PRESENT(BIC_PkgWatt);
			BIC_PRESENT(BIC_CorWatt);
			BIC_PRESENT(BIC_RAMWatt);
			BIC_PRESENT(BIC_GFXWatt);
		}
		break;
	case INTEL_FAM6_ATOM_TREMONT_D:	/* JVL */
		do_rapl = RAPL_PKG | RAPL_PKG_PERF_STATUS | RAPL_PKG_POWER_INFO;
		BIC_PRESENT(BIC_PKG__);
		if (rapl_joules)
			BIC_PRESENT(BIC_Pkg_J);
		else
			BIC_PRESENT(BIC_PkgWatt);
		break;
	case INTEL_FAM6_SKYLAKE_L:	/* SKL */
	case INTEL_FAM6_CANNONLAKE_L:	/* CNL */
		do_rapl =
		    RAPL_PKG | RAPL_CORES | RAPL_CORE_POLICY | RAPL_DRAM | RAPL_DRAM_PERF_STATUS | RAPL_PKG_PERF_STATUS
		    | RAPL_GFX | RAPL_PKG_POWER_INFO;
		BIC_PRESENT(BIC_PKG__);
		BIC_PRESENT(BIC_RAM__);
		if (rapl_joules) {
			BIC_PRESENT(BIC_Pkg_J);
			BIC_PRESENT(BIC_Cor_J);
			BIC_PRESENT(BIC_RAM_J);
			BIC_PRESENT(BIC_GFX_J);
		} else {
			BIC_PRESENT(BIC_PkgWatt);
			BIC_PRESENT(BIC_CorWatt);
			BIC_PRESENT(BIC_RAMWatt);
			BIC_PRESENT(BIC_GFXWatt);
		}
		break;
	case INTEL_FAM6_HASWELL_X:	/* HSX */
	case INTEL_FAM6_BROADWELL_X:	/* BDX */
	case INTEL_FAM6_SKYLAKE_X:	/* SKX */
	case INTEL_FAM6_ICELAKE_X:	/* ICX */
	case INTEL_FAM6_SAPPHIRERAPIDS_X:	/* SPR */
	case INTEL_FAM6_XEON_PHI_KNL:	/* KNL */
		do_rapl =
		    RAPL_PKG | RAPL_DRAM | RAPL_DRAM_POWER_INFO | RAPL_DRAM_PERF_STATUS | RAPL_PKG_PERF_STATUS |
		    RAPL_PKG_POWER_INFO;
		BIC_PRESENT(BIC_PKG__);
		BIC_PRESENT(BIC_RAM__);
		if (rapl_joules) {
			BIC_PRESENT(BIC_Pkg_J);
			BIC_PRESENT(BIC_RAM_J);
		} else {
			BIC_PRESENT(BIC_PkgWatt);
			BIC_PRESENT(BIC_RAMWatt);
		}
		break;
	case INTEL_FAM6_SANDYBRIDGE_X:
	case INTEL_FAM6_IVYBRIDGE_X:
		do_rapl =
		    RAPL_PKG | RAPL_CORES | RAPL_CORE_POLICY | RAPL_DRAM | RAPL_DRAM_POWER_INFO | RAPL_PKG_PERF_STATUS |
		    RAPL_DRAM_PERF_STATUS | RAPL_PKG_POWER_INFO;
		BIC_PRESENT(BIC_PKG__);
		BIC_PRESENT(BIC_RAM__);
		if (rapl_joules) {
			BIC_PRESENT(BIC_Pkg_J);
			BIC_PRESENT(BIC_Cor_J);
			BIC_PRESENT(BIC_RAM_J);
		} else {
			BIC_PRESENT(BIC_PkgWatt);
			BIC_PRESENT(BIC_CorWatt);
			BIC_PRESENT(BIC_RAMWatt);
		}
		break;
	case INTEL_FAM6_ATOM_SILVERMONT:	/* BYT */
	case INTEL_FAM6_ATOM_SILVERMONT_D:	/* AVN */
		do_rapl = RAPL_PKG | RAPL_CORES;
		if (rapl_joules) {
			BIC_PRESENT(BIC_Pkg_J);
			BIC_PRESENT(BIC_Cor_J);
		} else {
			BIC_PRESENT(BIC_PkgWatt);
			BIC_PRESENT(BIC_CorWatt);
		}
		break;
	case INTEL_FAM6_ATOM_GOLDMONT_D:	/* DNV */
		do_rapl =
		    RAPL_PKG | RAPL_DRAM | RAPL_DRAM_POWER_INFO | RAPL_DRAM_PERF_STATUS | RAPL_PKG_PERF_STATUS |
		    RAPL_PKG_POWER_INFO | RAPL_CORES_ENERGY_STATUS;
		BIC_PRESENT(BIC_PKG__);
		BIC_PRESENT(BIC_RAM__);
		if (rapl_joules) {
			BIC_PRESENT(BIC_Pkg_J);
			BIC_PRESENT(BIC_Cor_J);
			BIC_PRESENT(BIC_RAM_J);
		} else {
			BIC_PRESENT(BIC_PkgWatt);
			BIC_PRESENT(BIC_CorWatt);
			BIC_PRESENT(BIC_RAMWatt);
		}
		break;
	default:
		return;
	}

	/* units on package 0, verify later other packages match */
	if (get_msr(base_cpu, MSR_RAPL_POWER_UNIT, &msr))
		return;

	rapl_power_units = 1.0 / (1 << (msr & 0xF));
	if (model == INTEL_FAM6_ATOM_SILVERMONT)
		rapl_energy_units = 1.0 * (1 << (msr >> 8 & 0x1F)) / 1000000;
	else
		rapl_energy_units = 1.0 / (1 << (msr >> 8 & 0x1F));

	rapl_dram_energy_units = rapl_dram_energy_units_probe(model, rapl_energy_units);

	time_unit = msr >> 16 & 0xF;
	if (time_unit == 0)
		time_unit = 0xA;

	rapl_time_units = 1.0 / (1 << (time_unit));

	tdp = get_tdp_intel(model);

	rapl_joule_counter_range = 0xFFFFFFFF * rapl_energy_units / tdp;
	if (!quiet)
		fprintf(outf, "RAPL: %.0f sec. Joule Counter Range, at %.0f Watts\n", rapl_joule_counter_range, tdp);
}

void rapl_probe_amd(unsigned int family, unsigned int model)
{
	unsigned long long msr;
	unsigned int eax, ebx, ecx, edx;
	unsigned int has_rapl = 0;
	double tdp;

	UNUSED(model);

	if (max_extended_level >= 0x80000007) {
		__cpuid(0x80000007, eax, ebx, ecx, edx);
		/* RAPL (Fam 17h+) */
		has_rapl = edx & (1 << 14);
	}

	if (!has_rapl || family < 0x17)
		return;

	do_rapl = RAPL_AMD_F17H | RAPL_PER_CORE_ENERGY;
	if (rapl_joules) {
		BIC_PRESENT(BIC_Pkg_J);
		BIC_PRESENT(BIC_Cor_J);
	} else {
		BIC_PRESENT(BIC_PkgWatt);
		BIC_PRESENT(BIC_CorWatt);
	}

	if (get_msr(base_cpu, MSR_RAPL_PWR_UNIT, &msr))
		return;

	rapl_time_units = ldexp(1.0, -(msr >> 16 & 0xf));
	rapl_energy_units = ldexp(1.0, -(msr >> 8 & 0x1f));
	rapl_power_units = ldexp(1.0, -(msr & 0xf));

	tdp = get_tdp_amd(family);

	rapl_joule_counter_range = 0xFFFFFFFF * rapl_energy_units / tdp;
	if (!quiet)
		fprintf(outf, "RAPL: %.0f sec. Joule Counter Range, at %.0f Watts\n", rapl_joule_counter_range, tdp);
}

/*
 * rapl_probe()
 *
 * sets do_rapl, rapl_power_units, rapl_energy_units, rapl_time_units
 */
void rapl_probe(unsigned int family, unsigned int model)
{
	if (genuine_intel)
		rapl_probe_intel(family, model);
	if (authentic_amd || hygon_genuine)
		rapl_probe_amd(family, model);
}

void perf_limit_reasons_probe(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return;

	if (family != 6)
		return;

	switch (model) {
	case INTEL_FAM6_HASWELL:	/* HSW */
	case INTEL_FAM6_HASWELL_L:	/* HSW */
	case INTEL_FAM6_HASWELL_G:	/* HSW */
		do_gfx_perf_limit_reasons = 1;
		/* FALLTHRU */
	case INTEL_FAM6_HASWELL_X:	/* HSX */
		do_core_perf_limit_reasons = 1;
		do_ring_perf_limit_reasons = 1;
	default:
		return;
	}
}

void automatic_cstate_conversion_probe(unsigned int family, unsigned int model)
{
	if (family != 6)
		return;

	switch (model) {
	case INTEL_FAM6_BROADWELL_X:
	case INTEL_FAM6_SKYLAKE_X:
		has_automatic_cstate_conversion = 1;
	}
}

void prewake_cstate_probe(unsigned int family, unsigned int model)
{
	if (is_icx(family, model) || is_spr(family, model))
		dis_cstate_prewake = 1;
}

int print_thermal(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned long long msr;
	unsigned int dts, dts2;
	int cpu;

	UNUSED(c);
	UNUSED(p);

	if (!(do_dts || do_ptm))
		return 0;

	cpu = t->cpu_id;

	/* DTS is per-core, no need to print for each thread */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		return 0;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "print_thermal: Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (do_ptm && (t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE)) {
		if (get_msr(cpu, MSR_IA32_PACKAGE_THERM_STATUS, &msr))
			return 0;

		dts = (msr >> 16) & 0x7F;
		fprintf(outf, "cpu%d: MSR_IA32_PACKAGE_THERM_STATUS: 0x%08llx (%d C)\n", cpu, msr, tj_max - dts);

		if (get_msr(cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT, &msr))
			return 0;

		dts = (msr >> 16) & 0x7F;
		dts2 = (msr >> 8) & 0x7F;
		fprintf(outf, "cpu%d: MSR_IA32_PACKAGE_THERM_INTERRUPT: 0x%08llx (%d C, %d C)\n",
			cpu, msr, tj_max - dts, tj_max - dts2);
	}

	if (do_dts && debug) {
		unsigned int resolution;

		if (get_msr(cpu, MSR_IA32_THERM_STATUS, &msr))
			return 0;

		dts = (msr >> 16) & 0x7F;
		resolution = (msr >> 27) & 0xF;
		fprintf(outf, "cpu%d: MSR_IA32_THERM_STATUS: 0x%08llx (%d C +/- %d)\n",
			cpu, msr, tj_max - dts, resolution);

		if (get_msr(cpu, MSR_IA32_THERM_INTERRUPT, &msr))
			return 0;

		dts = (msr >> 16) & 0x7F;
		dts2 = (msr >> 8) & 0x7F;
		fprintf(outf, "cpu%d: MSR_IA32_THERM_INTERRUPT: 0x%08llx (%d C, %d C)\n",
			cpu, msr, tj_max - dts, tj_max - dts2);
	}

	return 0;
}

void print_power_limit_msr(int cpu, unsigned long long msr, char *label)
{
	fprintf(outf, "cpu%d: %s: %sabled (%0.3f Watts, %f sec, clamp %sabled)\n",
		cpu, label,
		((msr >> 15) & 1) ? "EN" : "DIS",
		((msr >> 0) & 0x7FFF) * rapl_power_units,
		(1.0 + (((msr >> 22) & 0x3) / 4.0)) * (1 << ((msr >> 17) & 0x1F)) * rapl_time_units,
		(((msr >> 16) & 1) ? "EN" : "DIS"));

	return;
}

int print_rapl(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned long long msr;
	const char *msr_name;
	int cpu;

	UNUSED(c);
	UNUSED(p);

	if (!do_rapl)
		return 0;

	/* RAPL counters are per package, so print only for 1st thread/package */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE) || !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	cpu = t->cpu_id;
	if (cpu_migrate(cpu)) {
		fprintf(outf, "print_rapl: Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (do_rapl & RAPL_AMD_F17H) {
		msr_name = "MSR_RAPL_PWR_UNIT";
		if (get_msr(cpu, MSR_RAPL_PWR_UNIT, &msr))
			return -1;
	} else {
		msr_name = "MSR_RAPL_POWER_UNIT";
		if (get_msr(cpu, MSR_RAPL_POWER_UNIT, &msr))
			return -1;
	}

	fprintf(outf, "cpu%d: %s: 0x%08llx (%f Watts, %f Joules, %f sec.)\n", cpu, msr_name, msr,
		rapl_power_units, rapl_energy_units, rapl_time_units);

	if (do_rapl & RAPL_PKG_POWER_INFO) {

		if (get_msr(cpu, MSR_PKG_POWER_INFO, &msr))
			return -5;

		fprintf(outf, "cpu%d: MSR_PKG_POWER_INFO: 0x%08llx (%.0f W TDP, RAPL %.0f - %.0f W, %f sec.)\n",
			cpu, msr,
			((msr >> 0) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 16) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 32) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 48) & RAPL_TIME_GRANULARITY) * rapl_time_units);

	}
	if (do_rapl & RAPL_PKG) {

		if (get_msr(cpu, MSR_PKG_POWER_LIMIT, &msr))
			return -9;

		fprintf(outf, "cpu%d: MSR_PKG_POWER_LIMIT: 0x%08llx (%slocked)\n",
			cpu, msr, (msr >> 63) & 1 ? "" : "UN");

		print_power_limit_msr(cpu, msr, "PKG Limit #1");
		fprintf(outf, "cpu%d: PKG Limit #2: %sabled (%0.3f Watts, %f* sec, clamp %sabled)\n",
			cpu,
			((msr >> 47) & 1) ? "EN" : "DIS",
			((msr >> 32) & 0x7FFF) * rapl_power_units,
			(1.0 + (((msr >> 54) & 0x3) / 4.0)) * (1 << ((msr >> 49) & 0x1F)) * rapl_time_units,
			((msr >> 48) & 1) ? "EN" : "DIS");

		if (get_msr(cpu, MSR_VR_CURRENT_CONFIG, &msr))
			return -9;

		fprintf(outf, "cpu%d: MSR_VR_CURRENT_CONFIG: 0x%08llx\n", cpu, msr);
		fprintf(outf, "cpu%d: PKG Limit #4: %f Watts (%slocked)\n",
			cpu, ((msr >> 0) & 0x1FFF) * rapl_power_units, (msr >> 31) & 1 ? "" : "UN");
	}

	if (do_rapl & RAPL_DRAM_POWER_INFO) {
		if (get_msr(cpu, MSR_DRAM_POWER_INFO, &msr))
			return -6;

		fprintf(outf, "cpu%d: MSR_DRAM_POWER_INFO,: 0x%08llx (%.0f W TDP, RAPL %.0f - %.0f W, %f sec.)\n",
			cpu, msr,
			((msr >> 0) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 16) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 32) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 48) & RAPL_TIME_GRANULARITY) * rapl_time_units);
	}
	if (do_rapl & RAPL_DRAM) {
		if (get_msr(cpu, MSR_DRAM_POWER_LIMIT, &msr))
			return -9;
		fprintf(outf, "cpu%d: MSR_DRAM_POWER_LIMIT: 0x%08llx (%slocked)\n",
			cpu, msr, (msr >> 31) & 1 ? "" : "UN");

		print_power_limit_msr(cpu, msr, "DRAM Limit");
	}
	if (do_rapl & RAPL_CORE_POLICY) {
		if (get_msr(cpu, MSR_PP0_POLICY, &msr))
			return -7;

		fprintf(outf, "cpu%d: MSR_PP0_POLICY: %lld\n", cpu, msr & 0xF);
	}
	if (do_rapl & RAPL_CORES_POWER_LIMIT) {
		if (get_msr(cpu, MSR_PP0_POWER_LIMIT, &msr))
			return -9;
		fprintf(outf, "cpu%d: MSR_PP0_POWER_LIMIT: 0x%08llx (%slocked)\n",
			cpu, msr, (msr >> 31) & 1 ? "" : "UN");
		print_power_limit_msr(cpu, msr, "Cores Limit");
	}
	if (do_rapl & RAPL_GFX) {
		if (get_msr(cpu, MSR_PP1_POLICY, &msr))
			return -8;

		fprintf(outf, "cpu%d: MSR_PP1_POLICY: %lld\n", cpu, msr & 0xF);

		if (get_msr(cpu, MSR_PP1_POWER_LIMIT, &msr))
			return -9;
		fprintf(outf, "cpu%d: MSR_PP1_POWER_LIMIT: 0x%08llx (%slocked)\n",
			cpu, msr, (msr >> 31) & 1 ? "" : "UN");
		print_power_limit_msr(cpu, msr, "GFX Limit");
	}
	return 0;
}

/*
 * SNB adds support for additional MSRs:
 *
 * MSR_PKG_C7_RESIDENCY            0x000003fa
 * MSR_CORE_C7_RESIDENCY           0x000003fe
 * MSR_PKG_C2_RESIDENCY            0x0000060d
 */

int has_snb_msrs(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_SANDYBRIDGE:
	case INTEL_FAM6_SANDYBRIDGE_X:
	case INTEL_FAM6_IVYBRIDGE:	/* IVB */
	case INTEL_FAM6_IVYBRIDGE_X:	/* IVB Xeon */
	case INTEL_FAM6_HASWELL:	/* HSW */
	case INTEL_FAM6_HASWELL_X:	/* HSW */
	case INTEL_FAM6_HASWELL_L:	/* HSW */
	case INTEL_FAM6_HASWELL_G:	/* HSW */
	case INTEL_FAM6_BROADWELL:	/* BDW */
	case INTEL_FAM6_BROADWELL_G:	/* BDW */
	case INTEL_FAM6_BROADWELL_X:	/* BDX */
	case INTEL_FAM6_SKYLAKE_L:	/* SKL */
	case INTEL_FAM6_CANNONLAKE_L:	/* CNL */
	case INTEL_FAM6_SKYLAKE_X:	/* SKX */
	case INTEL_FAM6_ICELAKE_X:	/* ICX */
	case INTEL_FAM6_SAPPHIRERAPIDS_X:	/* SPR */
	case INTEL_FAM6_ATOM_GOLDMONT:	/* BXT */
	case INTEL_FAM6_ATOM_GOLDMONT_PLUS:
	case INTEL_FAM6_ATOM_GOLDMONT_D:	/* DNV */
	case INTEL_FAM6_ATOM_TREMONT:	/* EHL */
	case INTEL_FAM6_ATOM_TREMONT_D:	/* JVL */
		return 1;
	}
	return 0;
}

/*
 * HSW ULT added support for C8/C9/C10 MSRs:
 *
 * MSR_PKG_C8_RESIDENCY		0x00000630
 * MSR_PKG_C9_RESIDENCY		0x00000631
 * MSR_PKG_C10_RESIDENCY	0x00000632
 *
 * MSR_PKGC8_IRTL		0x00000633
 * MSR_PKGC9_IRTL		0x00000634
 * MSR_PKGC10_IRTL		0x00000635
 *
 */
int has_c8910_msrs(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_HASWELL_L:	/* HSW */
	case INTEL_FAM6_BROADWELL:	/* BDW */
	case INTEL_FAM6_SKYLAKE_L:	/* SKL */
	case INTEL_FAM6_CANNONLAKE_L:	/* CNL */
	case INTEL_FAM6_ATOM_GOLDMONT:	/* BXT */
	case INTEL_FAM6_ATOM_GOLDMONT_PLUS:
	case INTEL_FAM6_ATOM_TREMONT:	/* EHL */
		return 1;
	}
	return 0;
}

/*
 * SKL adds support for additional MSRS:
 *
 * MSR_PKG_WEIGHTED_CORE_C0_RES    0x00000658
 * MSR_PKG_ANY_CORE_C0_RES         0x00000659
 * MSR_PKG_ANY_GFXE_C0_RES         0x0000065A
 * MSR_PKG_BOTH_CORE_GFXE_C0_RES   0x0000065B
 */
int has_skl_msrs(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_SKYLAKE_L:	/* SKL */
	case INTEL_FAM6_CANNONLAKE_L:	/* CNL */
		return 1;
	}
	return 0;
}

int is_slm(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_ATOM_SILVERMONT:	/* BYT */
	case INTEL_FAM6_ATOM_SILVERMONT_D:	/* AVN */
		return 1;
	}
	return 0;
}

int is_knl(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_XEON_PHI_KNL:	/* KNL */
		return 1;
	}
	return 0;
}

int is_cnl(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case INTEL_FAM6_CANNONLAKE_L:	/* CNL */
		return 1;
	}

	return 0;
}

unsigned int get_aperf_mperf_multiplier(unsigned int family, unsigned int model)
{
	if (is_knl(family, model))
		return 1024;
	return 1;
}

#define SLM_BCLK_FREQS 5
double slm_freq_table[SLM_BCLK_FREQS] = { 83.3, 100.0, 133.3, 116.7, 80.0 };

double slm_bclk(void)
{
	unsigned long long msr = 3;
	unsigned int i;
	double freq;

	if (get_msr(base_cpu, MSR_FSB_FREQ, &msr))
		fprintf(outf, "SLM BCLK: unknown\n");

	i = msr & 0xf;
	if (i >= SLM_BCLK_FREQS) {
		fprintf(outf, "SLM BCLK[%d] invalid\n", i);
		i = 3;
	}
	freq = slm_freq_table[i];

	if (!quiet)
		fprintf(outf, "SLM BCLK: %.1f Mhz\n", freq);

	return freq;
}

double discover_bclk(unsigned int family, unsigned int model)
{
	if (has_snb_msrs(family, model) || is_knl(family, model))
		return 100.00;
	else if (is_slm(family, model))
		return slm_bclk();
	else
		return 133.33;
}

int get_cpu_type(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned int eax, ebx, ecx, edx;

	UNUSED(c);
	UNUSED(p);

	if (!genuine_intel)
		return 0;

	if (cpu_migrate(t->cpu_id)) {
		fprintf(outf, "Could not migrate to CPU %d\n", t->cpu_id);
		return -1;
	}

	if (max_level < 0x1a)
		return 0;

	__cpuid(0x1a, eax, ebx, ecx, edx);
	eax = (eax >> 24) & 0xFF;
	if (eax == 0x20)
		t->is_atom = true;
	return 0;
}

/*
 * MSR_IA32_TEMPERATURE_TARGET indicates the temperature where
 * the Thermal Control Circuit (TCC) activates.
 * This is usually equal to tjMax.
 *
 * Older processors do not have this MSR, so there we guess,
 * but also allow cmdline over-ride with -T.
 *
 * Several MSR temperature values are in units of degrees-C
 * below this value, including the Digital Thermal Sensor (DTS),
 * Package Thermal Management Sensor (PTM), and thermal event thresholds.
 */
int set_temperature_target(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned long long msr;
	unsigned int tcc_default, tcc_offset;
	int cpu;

	UNUSED(c);
	UNUSED(p);

	/* tj_max is used only for dts or ptm */
	if (!(do_dts || do_ptm))
		return 0;

	/* this is a per-package concept */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE) || !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	cpu = t->cpu_id;
	if (cpu_migrate(cpu)) {
		fprintf(outf, "Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (tj_max_override != 0) {
		tj_max = tj_max_override;
		fprintf(outf, "cpu%d: Using cmdline TCC Target (%d C)\n", cpu, tj_max);
		return 0;
	}

	/* Temperature Target MSR is Nehalem and newer only */
	if (!do_nhm_platform_info)
		goto guess;

	if (get_msr(base_cpu, MSR_IA32_TEMPERATURE_TARGET, &msr))
		goto guess;

	tcc_default = (msr >> 16) & 0xFF;

	if (!quiet) {
		switch (tcc_offset_bits) {
		case 4:
			tcc_offset = (msr >> 24) & 0xF;
			fprintf(outf, "cpu%d: MSR_IA32_TEMPERATURE_TARGET: 0x%08llx (%d C) (%d default - %d offset)\n",
				cpu, msr, tcc_default - tcc_offset, tcc_default, tcc_offset);
			break;
		case 6:
			tcc_offset = (msr >> 24) & 0x3F;
			fprintf(outf, "cpu%d: MSR_IA32_TEMPERATURE_TARGET: 0x%08llx (%d C) (%d default - %d offset)\n",
				cpu, msr, tcc_default - tcc_offset, tcc_default, tcc_offset);
			break;
		default:
			fprintf(outf, "cpu%d: MSR_IA32_TEMPERATURE_TARGET: 0x%08llx (%d C)\n", cpu, msr, tcc_default);
			break;
		}
	}

	if (!tcc_default)
		goto guess;

	tj_max = tcc_default;

	return 0;

guess:
	tj_max = TJMAX_DEFAULT;
	fprintf(outf, "cpu%d: Guessing tjMax %d C, Please use -T to specify\n", cpu, tj_max);

	return 0;
}

void decode_feature_control_msr(void)
{
	unsigned long long msr;

	if (!get_msr(base_cpu, MSR_IA32_FEAT_CTL, &msr))
		fprintf(outf, "cpu%d: MSR_IA32_FEATURE_CONTROL: 0x%08llx (%sLocked %s)\n",
			base_cpu, msr, msr & FEAT_CTL_LOCKED ? "" : "UN-", msr & (1 << 18) ? "SGX" : "");
}

void decode_misc_enable_msr(void)
{
	unsigned long long msr;

	if (!genuine_intel)
		return;

	if (!get_msr(base_cpu, MSR_IA32_MISC_ENABLE, &msr))
		fprintf(outf, "cpu%d: MSR_IA32_MISC_ENABLE: 0x%08llx (%sTCC %sEIST %sMWAIT %sPREFETCH %sTURBO)\n",
			base_cpu, msr,
			msr & MSR_IA32_MISC_ENABLE_TM1 ? "" : "No-",
			msr & MSR_IA32_MISC_ENABLE_ENHANCED_SPEEDSTEP ? "" : "No-",
			msr & MSR_IA32_MISC_ENABLE_MWAIT ? "" : "No-",
			msr & MSR_IA32_MISC_ENABLE_PREFETCH_DISABLE ? "No-" : "",
			msr & MSR_IA32_MISC_ENABLE_TURBO_DISABLE ? "No-" : "");
}

void decode_misc_feature_control(void)
{
	unsigned long long msr;

	if (!has_misc_feature_control)
		return;

	if (!get_msr(base_cpu, MSR_MISC_FEATURE_CONTROL, &msr))
		fprintf(outf,
			"cpu%d: MSR_MISC_FEATURE_CONTROL: 0x%08llx (%sL2-Prefetch %sL2-Prefetch-pair %sL1-Prefetch %sL1-IP-Prefetch)\n",
			base_cpu, msr, msr & (0 << 0) ? "No-" : "", msr & (1 << 0) ? "No-" : "",
			msr & (2 << 0) ? "No-" : "", msr & (3 << 0) ? "No-" : "");
}

/*
 * Decode MSR_MISC_PWR_MGMT
 *
 * Decode the bits according to the Nehalem documentation
 * bit[0] seems to continue to have same meaning going forward
 * bit[1] less so...
 */
void decode_misc_pwr_mgmt_msr(void)
{
	unsigned long long msr;

	if (!do_nhm_platform_info)
		return;

	if (no_MSR_MISC_PWR_MGMT)
		return;

	if (!get_msr(base_cpu, MSR_MISC_PWR_MGMT, &msr))
		fprintf(outf, "cpu%d: MSR_MISC_PWR_MGMT: 0x%08llx (%sable-EIST_Coordination %sable-EPB %sable-OOB)\n",
			base_cpu, msr,
			msr & (1 << 0) ? "DIS" : "EN", msr & (1 << 1) ? "EN" : "DIS", msr & (1 << 8) ? "EN" : "DIS");
}

/*
 * Decode MSR_CC6_DEMOTION_POLICY_CONFIG, MSR_MC6_DEMOTION_POLICY_CONFIG
 *
 * This MSRs are present on Silvermont processors,
 * Intel Atom processor E3000 series (Baytrail), and friends.
 */
void decode_c6_demotion_policy_msr(void)
{
	unsigned long long msr;

	if (!get_msr(base_cpu, MSR_CC6_DEMOTION_POLICY_CONFIG, &msr))
		fprintf(outf, "cpu%d: MSR_CC6_DEMOTION_POLICY_CONFIG: 0x%08llx (%sable-CC6-Demotion)\n",
			base_cpu, msr, msr & (1 << 0) ? "EN" : "DIS");

	if (!get_msr(base_cpu, MSR_MC6_DEMOTION_POLICY_CONFIG, &msr))
		fprintf(outf, "cpu%d: MSR_MC6_DEMOTION_POLICY_CONFIG: 0x%08llx (%sable-MC6-Demotion)\n",
			base_cpu, msr, msr & (1 << 0) ? "EN" : "DIS");
}

/*
 * When models are the same, for the purpose of turbostat, reuse
 */
unsigned int intel_model_duplicates(unsigned int model)
{

	switch (model) {
	case INTEL_FAM6_NEHALEM_EP:	/* Core i7, Xeon 5500 series - Bloomfield, Gainstown NHM-EP */
	case INTEL_FAM6_NEHALEM:	/* Core i7 and i5 Processor - Clarksfield, Lynnfield, Jasper Forest */
	case 0x1F:		/* Core i7 and i5 Processor - Nehalem */
	case INTEL_FAM6_WESTMERE:	/* Westmere Client - Clarkdale, Arrandale */
	case INTEL_FAM6_WESTMERE_EP:	/* Westmere EP - Gulftown */
		return INTEL_FAM6_NEHALEM;

	case INTEL_FAM6_NEHALEM_EX:	/* Nehalem-EX Xeon - Beckton */
	case INTEL_FAM6_WESTMERE_EX:	/* Westmere-EX Xeon - Eagleton */
		return INTEL_FAM6_NEHALEM_EX;

	case INTEL_FAM6_XEON_PHI_KNM:
		return INTEL_FAM6_XEON_PHI_KNL;

	case INTEL_FAM6_BROADWELL_X:
	case INTEL_FAM6_BROADWELL_D:	/* BDX-DE */
		return INTEL_FAM6_BROADWELL_X;

	case INTEL_FAM6_SKYLAKE_L:
	case INTEL_FAM6_SKYLAKE:
	case INTEL_FAM6_KABYLAKE_L:
	case INTEL_FAM6_KABYLAKE:
	case INTEL_FAM6_COMETLAKE_L:
	case INTEL_FAM6_COMETLAKE:
		return INTEL_FAM6_SKYLAKE_L;

	case INTEL_FAM6_ICELAKE_L:
	case INTEL_FAM6_ICELAKE_NNPI:
	case INTEL_FAM6_TIGERLAKE_L:
	case INTEL_FAM6_TIGERLAKE:
	case INTEL_FAM6_ROCKETLAKE:
	case INTEL_FAM6_LAKEFIELD:
	case INTEL_FAM6_ALDERLAKE:
	case INTEL_FAM6_ALDERLAKE_L:
	case INTEL_FAM6_ALDERLAKE_N:
	case INTEL_FAM6_RAPTORLAKE:
	case INTEL_FAM6_RAPTORLAKE_P:
	case INTEL_FAM6_RAPTORLAKE_S:
	case INTEL_FAM6_METEORLAKE:
	case INTEL_FAM6_METEORLAKE_L:
		return INTEL_FAM6_CANNONLAKE_L;

	case INTEL_FAM6_ATOM_TREMONT_L:
		return INTEL_FAM6_ATOM_TREMONT;

	case INTEL_FAM6_ICELAKE_D:
		return INTEL_FAM6_ICELAKE_X;
	}
	return model;
}

void print_dev_latency(void)
{
	char *path = "/dev/cpu_dma_latency";
	int fd;
	int value;
	int retval;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		warn("fopen %s\n", path);
		return;
	}

	retval = read(fd, (void *)&value, sizeof(int));
	if (retval != sizeof(int)) {
		warn("read failed %s\n", path);
		close(fd);
		return;
	}
	fprintf(outf, "/dev/cpu_dma_latency: %d usec (%s)\n", value, value == 2000000000 ? "default" : "constrained");

	close(fd);
}

/*
 * Linux-perf manages the HW instructions-retired counter
 * by enabling when requested, and hiding rollover
 */
void linux_perf_init(void)
{
	if (!BIC_IS_ENABLED(BIC_IPC))
		return;

	if (access("/proc/sys/kernel/perf_event_paranoid", F_OK))
		return;

	fd_instr_count_percpu = calloc(topo.max_cpu_num + 1, sizeof(int));
	if (fd_instr_count_percpu == NULL)
		err(-1, "calloc fd_instr_count_percpu");

	BIC_PRESENT(BIC_IPC);
}

void process_cpuid()
{
	unsigned int eax, ebx, ecx, edx;
	unsigned int fms, family, model, stepping, ecx_flags, edx_flags;
	unsigned long long ucode_patch = 0;

	eax = ebx = ecx = edx = 0;

	__cpuid(0, max_level, ebx, ecx, edx);

	if (ebx == 0x756e6547 && ecx == 0x6c65746e && edx == 0x49656e69)
		genuine_intel = 1;
	else if (ebx == 0x68747541 && ecx == 0x444d4163 && edx == 0x69746e65)
		authentic_amd = 1;
	else if (ebx == 0x6f677948 && ecx == 0x656e6975 && edx == 0x6e65476e)
		hygon_genuine = 1;

	if (!quiet)
		fprintf(outf, "CPUID(0): %.4s%.4s%.4s 0x%x CPUID levels\n",
			(char *)&ebx, (char *)&edx, (char *)&ecx, max_level);

	__cpuid(1, fms, ebx, ecx, edx);
	family = (fms >> 8) & 0xf;
	model = (fms >> 4) & 0xf;
	stepping = fms & 0xf;
	if (family == 0xf)
		family += (fms >> 20) & 0xff;
	if (family >= 6)
		model += ((fms >> 16) & 0xf) << 4;
	ecx_flags = ecx;
	edx_flags = edx;

	if (get_msr(sched_getcpu(), MSR_IA32_UCODE_REV, &ucode_patch))
		warnx("get_msr(UCODE)\n");

	/*
	 * check max extended function levels of CPUID.
	 * This is needed to check for invariant TSC.
	 * This check is valid for both Intel and AMD.
	 */
	ebx = ecx = edx = 0;
	__cpuid(0x80000000, max_extended_level, ebx, ecx, edx);

	if (!quiet) {
		fprintf(outf, "CPUID(1): family:model:stepping 0x%x:%x:%x (%d:%d:%d) microcode 0x%x\n",
			family, model, stepping, family, model, stepping,
			(unsigned int)((ucode_patch >> 32) & 0xFFFFFFFF));
		fprintf(outf, "CPUID(0x80000000): max_extended_levels: 0x%x\n", max_extended_level);
		fprintf(outf, "CPUID(1): %s %s %s %s %s %s %s %s %s %s\n",
			ecx_flags & (1 << 0) ? "SSE3" : "-",
			ecx_flags & (1 << 3) ? "MONITOR" : "-",
			ecx_flags & (1 << 6) ? "SMX" : "-",
			ecx_flags & (1 << 7) ? "EIST" : "-",
			ecx_flags & (1 << 8) ? "TM2" : "-",
			edx_flags & (1 << 4) ? "TSC" : "-",
			edx_flags & (1 << 5) ? "MSR" : "-",
			edx_flags & (1 << 22) ? "ACPI-TM" : "-",
			edx_flags & (1 << 28) ? "HT" : "-", edx_flags & (1 << 29) ? "TM" : "-");
	}
	if (genuine_intel) {
		model_orig = model;
		model = intel_model_duplicates(model);
	}

	if (!(edx_flags & (1 << 5)))
		errx(1, "CPUID: no MSR");

	if (max_extended_level >= 0x80000007) {

		/*
		 * Non-Stop TSC is advertised by CPUID.EAX=0x80000007: EDX.bit8
		 * this check is valid for both Intel and AMD
		 */
		__cpuid(0x80000007, eax, ebx, ecx, edx);
		has_invariant_tsc = edx & (1 << 8);
	}

	/*
	 * APERF/MPERF is advertised by CPUID.EAX=0x6: ECX.bit0
	 * this check is valid for both Intel and AMD
	 */

	__cpuid(0x6, eax, ebx, ecx, edx);
	has_aperf = ecx & (1 << 0);
	if (has_aperf) {
		BIC_PRESENT(BIC_Avg_MHz);
		BIC_PRESENT(BIC_Busy);
		BIC_PRESENT(BIC_Bzy_MHz);
	}
	do_dts = eax & (1 << 0);
	if (do_dts)
		BIC_PRESENT(BIC_CoreTmp);
	has_turbo = eax & (1 << 1);
	do_ptm = eax & (1 << 6);
	if (do_ptm)
		BIC_PRESENT(BIC_PkgTmp);
	has_hwp = eax & (1 << 7);
	has_hwp_notify = eax & (1 << 8);
	has_hwp_activity_window = eax & (1 << 9);
	has_hwp_epp = eax & (1 << 10);
	has_hwp_pkg = eax & (1 << 11);
	has_epb = ecx & (1 << 3);

	if (!quiet)
		fprintf(outf, "CPUID(6): %sAPERF, %sTURBO, %sDTS, %sPTM, %sHWP, "
			"%sHWPnotify, %sHWPwindow, %sHWPepp, %sHWPpkg, %sEPB\n",
			has_aperf ? "" : "No-",
			has_turbo ? "" : "No-",
			do_dts ? "" : "No-",
			do_ptm ? "" : "No-",
			has_hwp ? "" : "No-",
			has_hwp_notify ? "" : "No-",
			has_hwp_activity_window ? "" : "No-",
			has_hwp_epp ? "" : "No-", has_hwp_pkg ? "" : "No-", has_epb ? "" : "No-");

	if (!quiet)
		decode_misc_enable_msr();

	if (max_level >= 0x7 && !quiet) {
		int has_sgx;

		ecx = 0;

		__cpuid_count(0x7, 0, eax, ebx, ecx, edx);

		has_sgx = ebx & (1 << 2);

		is_hybrid = edx & (1 << 15);

		fprintf(outf, "CPUID(7): %sSGX %sHybrid\n", has_sgx ? "" : "No-", is_hybrid ? "" : "No-");

		if (has_sgx)
			decode_feature_control_msr();
	}

	if (max_level >= 0x15) {
		unsigned int eax_crystal;
		unsigned int ebx_tsc;

		/*
		 * CPUID 15H TSC/Crystal ratio, possibly Crystal Hz
		 */
		eax_crystal = ebx_tsc = crystal_hz = edx = 0;
		__cpuid(0x15, eax_crystal, ebx_tsc, crystal_hz, edx);

		if (ebx_tsc != 0) {

			if (!quiet && (ebx != 0))
				fprintf(outf, "CPUID(0x15): eax_crystal: %d ebx_tsc: %d ecx_crystal_hz: %d\n",
					eax_crystal, ebx_tsc, crystal_hz);

			if (crystal_hz == 0)
				switch (model) {
				case INTEL_FAM6_SKYLAKE_L:	/* SKL */
					crystal_hz = 24000000;	/* 24.0 MHz */
					break;
				case INTEL_FAM6_ATOM_GOLDMONT_D:	/* DNV */
					crystal_hz = 25000000;	/* 25.0 MHz */
					break;
				case INTEL_FAM6_ATOM_GOLDMONT:	/* BXT */
				case INTEL_FAM6_ATOM_GOLDMONT_PLUS:
					crystal_hz = 19200000;	/* 19.2 MHz */
					break;
				default:
					crystal_hz = 0;
				}

			if (crystal_hz) {
				tsc_hz = (unsigned long long)crystal_hz *ebx_tsc / eax_crystal;
				if (!quiet)
					fprintf(outf, "TSC: %lld MHz (%d Hz * %d / %d / 1000000)\n",
						tsc_hz / 1000000, crystal_hz, ebx_tsc, eax_crystal);
			}
		}
	}
	if (max_level >= 0x16) {
		unsigned int base_mhz, max_mhz, bus_mhz, edx;

		/*
		 * CPUID 16H Base MHz, Max MHz, Bus MHz
		 */
		base_mhz = max_mhz = bus_mhz = edx = 0;

		__cpuid(0x16, base_mhz, max_mhz, bus_mhz, edx);
		if (!quiet)
			fprintf(outf, "CPUID(0x16): base_mhz: %d max_mhz: %d bus_mhz: %d\n",
				base_mhz, max_mhz, bus_mhz);
	}

	if (has_aperf)
		aperf_mperf_multiplier = get_aperf_mperf_multiplier(family, model);

	BIC_PRESENT(BIC_IRQ);
	BIC_PRESENT(BIC_TSC_MHz);

	if (probe_nhm_msrs(family, model)) {
		do_nhm_platform_info = 1;
		BIC_PRESENT(BIC_CPU_c1);
		BIC_PRESENT(BIC_CPU_c3);
		BIC_PRESENT(BIC_CPU_c6);
		BIC_PRESENT(BIC_SMI);
	}
	do_snb_cstates = has_snb_msrs(family, model);

	if (do_snb_cstates)
		BIC_PRESENT(BIC_CPU_c7);

	do_irtl_snb = has_snb_msrs(family, model);
	if (do_snb_cstates && (pkg_cstate_limit >= PCL__2))
		BIC_PRESENT(BIC_Pkgpc2);
	if (pkg_cstate_limit >= PCL__3)
		BIC_PRESENT(BIC_Pkgpc3);
	if (pkg_cstate_limit >= PCL__6)
		BIC_PRESENT(BIC_Pkgpc6);
	if (do_snb_cstates && (pkg_cstate_limit >= PCL__7))
		BIC_PRESENT(BIC_Pkgpc7);
	if (has_slv_msrs(family, model)) {
		BIC_NOT_PRESENT(BIC_Pkgpc2);
		BIC_NOT_PRESENT(BIC_Pkgpc3);
		BIC_PRESENT(BIC_Pkgpc6);
		BIC_NOT_PRESENT(BIC_Pkgpc7);
		BIC_PRESENT(BIC_Mod_c6);
		use_c1_residency_msr = 1;
	}
	if (is_jvl(family, model)) {
		BIC_NOT_PRESENT(BIC_CPU_c3);
		BIC_NOT_PRESENT(BIC_CPU_c7);
		BIC_NOT_PRESENT(BIC_Pkgpc2);
		BIC_NOT_PRESENT(BIC_Pkgpc3);
		BIC_NOT_PRESENT(BIC_Pkgpc6);
		BIC_NOT_PRESENT(BIC_Pkgpc7);
	}
	if (is_dnv(family, model)) {
		BIC_PRESENT(BIC_CPU_c1);
		BIC_NOT_PRESENT(BIC_CPU_c3);
		BIC_NOT_PRESENT(BIC_Pkgpc3);
		BIC_NOT_PRESENT(BIC_CPU_c7);
		BIC_NOT_PRESENT(BIC_Pkgpc7);
		use_c1_residency_msr = 1;
	}
	if (is_skx(family, model) || is_icx(family, model) || is_spr(family, model)) {
		BIC_NOT_PRESENT(BIC_CPU_c3);
		BIC_NOT_PRESENT(BIC_Pkgpc3);
		BIC_NOT_PRESENT(BIC_CPU_c7);
		BIC_NOT_PRESENT(BIC_Pkgpc7);
	}
	if (is_bdx(family, model)) {
		BIC_NOT_PRESENT(BIC_CPU_c7);
		BIC_NOT_PRESENT(BIC_Pkgpc7);
	}
	if (has_c8910_msrs(family, model)) {
		if (pkg_cstate_limit >= PCL__8)
			BIC_PRESENT(BIC_Pkgpc8);
		if (pkg_cstate_limit >= PCL__9)
			BIC_PRESENT(BIC_Pkgpc9);
		if (pkg_cstate_limit >= PCL_10)
			BIC_PRESENT(BIC_Pkgpc10);
	}
	do_irtl_hsw = has_c8910_msrs(family, model);
	if (has_skl_msrs(family, model)) {
		BIC_PRESENT(BIC_Totl_c0);
		BIC_PRESENT(BIC_Any_c0);
		BIC_PRESENT(BIC_GFX_c0);
		BIC_PRESENT(BIC_CPUGFX);
	}
	do_slm_cstates = is_slm(family, model);
	do_knl_cstates = is_knl(family, model);

	if (do_slm_cstates || do_knl_cstates || is_cnl(family, model) || is_ehl(family, model))
		BIC_NOT_PRESENT(BIC_CPU_c3);

	if (!quiet)
		decode_misc_pwr_mgmt_msr();

	if (!quiet && has_slv_msrs(family, model))
		decode_c6_demotion_policy_msr();

	rapl_probe(family, model);
	perf_limit_reasons_probe(family, model);
	automatic_cstate_conversion_probe(family, model);

	check_tcc_offset(model_orig);

	if (!quiet)
		dump_cstate_pstate_config_info(family, model);
	intel_uncore_frequency_probe();

	if (!quiet)
		print_dev_latency();
	if (!quiet)
		dump_sysfs_cstate_config();
	if (!quiet)
		dump_sysfs_pstate_config();

	if (has_skl_msrs(family, model) || is_ehl(family, model))
		calculate_tsc_tweak();

	if (!access("/sys/class/drm/card0/power/rc6_residency_ms", R_OK))
		BIC_PRESENT(BIC_GFX_rc6);

	if (!access("/sys/class/graphics/fb0/device/drm/card0/gt_cur_freq_mhz", R_OK))
		BIC_PRESENT(BIC_GFXMHz);

	if (!access("/sys/class/graphics/fb0/device/drm/card0/gt_act_freq_mhz", R_OK))
		BIC_PRESENT(BIC_GFXACTMHz);

	if (!access("/sys/devices/system/cpu/cpuidle/low_power_idle_cpu_residency_us", R_OK))
		BIC_PRESENT(BIC_CPU_LPI);
	else
		BIC_NOT_PRESENT(BIC_CPU_LPI);

	if (!access("/sys/devices/system/cpu/cpu0/thermal_throttle/core_throttle_count", R_OK))
		BIC_PRESENT(BIC_CORE_THROT_CNT);
	else
		BIC_NOT_PRESENT(BIC_CORE_THROT_CNT);

	if (!access(sys_lpi_file_sysfs, R_OK)) {
		sys_lpi_file = sys_lpi_file_sysfs;
		BIC_PRESENT(BIC_SYS_LPI);
	} else if (!access(sys_lpi_file_debugfs, R_OK)) {
		sys_lpi_file = sys_lpi_file_debugfs;
		BIC_PRESENT(BIC_SYS_LPI);
	} else {
		sys_lpi_file_sysfs = NULL;
		BIC_NOT_PRESENT(BIC_SYS_LPI);
	}

	if (!quiet)
		decode_misc_feature_control();

	return;
}

/*
 * in /dev/cpu/ return success for names that are numbers
 * ie. filter out ".", "..", "microcode".
 */
int dir_filter(const struct dirent *dirp)
{
	if (isdigit(dirp->d_name[0]))
		return 1;
	else
		return 0;
}

void topology_probe()
{
	int i;
	int max_core_id = 0;
	int max_package_id = 0;
	int max_die_id = 0;
	int max_siblings = 0;

	/* Initialize num_cpus, max_cpu_num */
	set_max_cpu_num();
	topo.num_cpus = 0;
	for_all_proc_cpus(count_cpus);
	if (!summary_only && topo.num_cpus > 1)
		BIC_PRESENT(BIC_CPU);

	if (debug > 1)
		fprintf(outf, "num_cpus %d max_cpu_num %d\n", topo.num_cpus, topo.max_cpu_num);

	cpus = calloc(1, (topo.max_cpu_num + 1) * sizeof(struct cpu_topology));
	if (cpus == NULL)
		err(1, "calloc cpus");

	/*
	 * Allocate and initialize cpu_present_set
	 */
	cpu_present_set = CPU_ALLOC((topo.max_cpu_num + 1));
	if (cpu_present_set == NULL)
		err(3, "CPU_ALLOC");
	cpu_present_setsize = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(cpu_present_setsize, cpu_present_set);
	for_all_proc_cpus(mark_cpu_present);

	/*
	 * Validate that all cpus in cpu_subset are also in cpu_present_set
	 */
	for (i = 0; i < CPU_SUBSET_MAXCPUS; ++i) {
		if (CPU_ISSET_S(i, cpu_subset_size, cpu_subset))
			if (!CPU_ISSET_S(i, cpu_present_setsize, cpu_present_set))
				err(1, "cpu%d not present", i);
	}

	/*
	 * Allocate and initialize cpu_affinity_set
	 */
	cpu_affinity_set = CPU_ALLOC((topo.max_cpu_num + 1));
	if (cpu_affinity_set == NULL)
		err(3, "CPU_ALLOC");
	cpu_affinity_setsize = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(cpu_affinity_setsize, cpu_affinity_set);

	for_all_proc_cpus(init_thread_id);

	/*
	 * For online cpus
	 * find max_core_id, max_package_id
	 */
	for (i = 0; i <= topo.max_cpu_num; ++i) {
		int siblings;

		if (cpu_is_not_present(i)) {
			if (debug > 1)
				fprintf(outf, "cpu%d NOT PRESENT\n", i);
			continue;
		}

		cpus[i].logical_cpu_id = i;

		/* get package information */
		cpus[i].physical_package_id = get_physical_package_id(i);
		if (cpus[i].physical_package_id > max_package_id)
			max_package_id = cpus[i].physical_package_id;

		/* get die information */
		cpus[i].die_id = get_die_id(i);
		if (cpus[i].die_id > max_die_id)
			max_die_id = cpus[i].die_id;

		/* get numa node information */
		cpus[i].physical_node_id = get_physical_node_id(&cpus[i]);
		if (cpus[i].physical_node_id > topo.max_node_num)
			topo.max_node_num = cpus[i].physical_node_id;

		/* get core information */
		cpus[i].physical_core_id = get_core_id(i);
		if (cpus[i].physical_core_id > max_core_id)
			max_core_id = cpus[i].physical_core_id;

		/* get thread information */
		siblings = get_thread_siblings(&cpus[i]);
		if (siblings > max_siblings)
			max_siblings = siblings;
		if (cpus[i].thread_id == 0)
			topo.num_cores++;
	}

	topo.cores_per_node = max_core_id + 1;
	if (debug > 1)
		fprintf(outf, "max_core_id %d, sizing for %d cores per package\n", max_core_id, topo.cores_per_node);
	if (!summary_only && topo.cores_per_node > 1)
		BIC_PRESENT(BIC_Core);

	topo.num_die = max_die_id + 1;
	if (debug > 1)
		fprintf(outf, "max_die_id %d, sizing for %d die\n", max_die_id, topo.num_die);
	if (!summary_only && topo.num_die > 1)
		BIC_PRESENT(BIC_Die);

	topo.num_packages = max_package_id + 1;
	if (debug > 1)
		fprintf(outf, "max_package_id %d, sizing for %d packages\n", max_package_id, topo.num_packages);
	if (!summary_only && topo.num_packages > 1)
		BIC_PRESENT(BIC_Package);

	set_node_data();
	if (debug > 1)
		fprintf(outf, "nodes_per_pkg %d\n", topo.nodes_per_pkg);
	if (!summary_only && topo.nodes_per_pkg > 1)
		BIC_PRESENT(BIC_Node);

	topo.threads_per_core = max_siblings;
	if (debug > 1)
		fprintf(outf, "max_siblings %d\n", max_siblings);

	if (debug < 1)
		return;

	for (i = 0; i <= topo.max_cpu_num; ++i) {
		if (cpu_is_not_present(i))
			continue;
		fprintf(outf,
			"cpu %d pkg %d die %d node %d lnode %d core %d thread %d\n",
			i, cpus[i].physical_package_id, cpus[i].die_id,
			cpus[i].physical_node_id, cpus[i].logical_node_id, cpus[i].physical_core_id, cpus[i].thread_id);
	}

}

void allocate_counters(struct thread_data **t, struct core_data **c, struct pkg_data **p)
{
	int i;
	int num_cores = topo.cores_per_node * topo.nodes_per_pkg * topo.num_packages;
	int num_threads = topo.threads_per_core * num_cores;

	*t = calloc(num_threads, sizeof(struct thread_data));
	if (*t == NULL)
		goto error;

	for (i = 0; i < num_threads; i++)
		(*t)[i].cpu_id = -1;

	*c = calloc(num_cores, sizeof(struct core_data));
	if (*c == NULL)
		goto error;

	for (i = 0; i < num_cores; i++)
		(*c)[i].core_id = -1;

	*p = calloc(topo.num_packages, sizeof(struct pkg_data));
	if (*p == NULL)
		goto error;

	for (i = 0; i < topo.num_packages; i++)
		(*p)[i].package_id = i;

	return;
error:
	err(1, "calloc counters");
}

/*
 * init_counter()
 *
 * set FIRST_THREAD_IN_CORE and FIRST_CORE_IN_PACKAGE
 */
void init_counter(struct thread_data *thread_base, struct core_data *core_base, struct pkg_data *pkg_base, int cpu_id)
{
	int pkg_id = cpus[cpu_id].physical_package_id;
	int node_id = cpus[cpu_id].logical_node_id;
	int core_id = cpus[cpu_id].physical_core_id;
	int thread_id = cpus[cpu_id].thread_id;
	struct thread_data *t;
	struct core_data *c;
	struct pkg_data *p;

	/* Workaround for systems where physical_node_id==-1
	 * and logical_node_id==(-1 - topo.num_cpus)
	 */
	if (node_id < 0)
		node_id = 0;

	t = GET_THREAD(thread_base, thread_id, core_id, node_id, pkg_id);
	c = GET_CORE(core_base, core_id, node_id, pkg_id);
	p = GET_PKG(pkg_base, pkg_id);

	t->cpu_id = cpu_id;
	if (thread_id == 0) {
		t->flags |= CPU_IS_FIRST_THREAD_IN_CORE;
		if (cpu_is_first_core_in_package(cpu_id))
			t->flags |= CPU_IS_FIRST_CORE_IN_PACKAGE;
	}

	c->core_id = core_id;
	p->package_id = pkg_id;
}

int initialize_counters(int cpu_id)
{
	init_counter(EVEN_COUNTERS, cpu_id);
	init_counter(ODD_COUNTERS, cpu_id);
	return 0;
}

void allocate_output_buffer()
{
	output_buffer = calloc(1, (1 + topo.num_cpus) * 2048);
	outp = output_buffer;
	if (outp == NULL)
		err(-1, "calloc output buffer");
}

void allocate_fd_percpu(void)
{
	fd_percpu = calloc(topo.max_cpu_num + 1, sizeof(int));
	if (fd_percpu == NULL)
		err(-1, "calloc fd_percpu");
}

void allocate_irq_buffers(void)
{
	irq_column_2_cpu = calloc(topo.num_cpus, sizeof(int));
	if (irq_column_2_cpu == NULL)
		err(-1, "calloc %d", topo.num_cpus);

	irqs_per_cpu = calloc(topo.max_cpu_num + 1, sizeof(int));
	if (irqs_per_cpu == NULL)
		err(-1, "calloc %d", topo.max_cpu_num + 1);
}

void setup_all_buffers(void)
{
	topology_probe();
	allocate_irq_buffers();
	allocate_fd_percpu();
	allocate_counters(&thread_even, &core_even, &package_even);
	allocate_counters(&thread_odd, &core_odd, &package_odd);
	allocate_output_buffer();
	for_all_proc_cpus(initialize_counters);
}

void set_base_cpu(void)
{
	base_cpu = sched_getcpu();
	if (base_cpu < 0)
		err(-ENODEV, "No valid cpus found");

	if (debug > 1)
		fprintf(outf, "base_cpu = %d\n", base_cpu);
}

void turbostat_init()
{
	setup_all_buffers();
	set_base_cpu();
	check_dev_msr();
	check_permissions();
	process_cpuid();
	linux_perf_init();

	if (!quiet)
		for_all_cpus(print_hwp, ODD_COUNTERS);

	if (!quiet)
		for_all_cpus(print_epb, ODD_COUNTERS);

	if (!quiet)
		for_all_cpus(print_perf_limit, ODD_COUNTERS);

	if (!quiet)
		for_all_cpus(print_rapl, ODD_COUNTERS);

	for_all_cpus(set_temperature_target, ODD_COUNTERS);

	for_all_cpus(get_cpu_type, ODD_COUNTERS);
	for_all_cpus(get_cpu_type, EVEN_COUNTERS);

	if (!quiet)
		for_all_cpus(print_thermal, ODD_COUNTERS);

	if (!quiet && do_irtl_snb)
		print_irtl();

	if (DO_BIC(BIC_IPC))
		(void)get_instr_count_fd(base_cpu);
}

int fork_it(char **argv)
{
	pid_t child_pid;
	int status;

	snapshot_proc_sysfs_files();
	status = for_all_cpus(get_counters, EVEN_COUNTERS);
	first_counter_read = 0;
	if (status)
		exit(status);
	/* clear affinity side-effect of get_counters() */
	sched_setaffinity(0, cpu_present_setsize, cpu_present_set);
	gettimeofday(&tv_even, (struct timezone *)NULL);

	child_pid = fork();
	if (!child_pid) {
		/* child */
		execvp(argv[0], argv);
		err(errno, "exec %s", argv[0]);
	} else {

		/* parent */
		if (child_pid == -1)
			err(1, "fork");

		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
		if (waitpid(child_pid, &status, 0) == -1)
			err(status, "waitpid");

		if (WIFEXITED(status))
			status = WEXITSTATUS(status);
	}
	/*
	 * n.b. fork_it() does not check for errors from for_all_cpus()
	 * because re-starting is problematic when forking
	 */
	snapshot_proc_sysfs_files();
	for_all_cpus(get_counters, ODD_COUNTERS);
	gettimeofday(&tv_odd, (struct timezone *)NULL);
	timersub(&tv_odd, &tv_even, &tv_delta);
	if (for_all_cpus_2(delta_cpu, ODD_COUNTERS, EVEN_COUNTERS))
		fprintf(outf, "%s: Counter reset detected\n", progname);
	else {
		compute_average(EVEN_COUNTERS);
		format_all_counters(EVEN_COUNTERS);
	}

	fprintf(outf, "%.6f sec\n", tv_delta.tv_sec + tv_delta.tv_usec / 1000000.0);

	flush_output_stderr();

	return status;
}

int get_and_dump_counters(void)
{
	int status;

	snapshot_proc_sysfs_files();
	status = for_all_cpus(get_counters, ODD_COUNTERS);
	if (status)
		return status;

	status = for_all_cpus(dump_counters, ODD_COUNTERS);
	if (status)
		return status;

	flush_output_stdout();

	return status;
}

void print_version()
{
	fprintf(outf, "turbostat version 2022.10.04 - Len Brown <lenb@kernel.org>\n");
}

#define COMMAND_LINE_SIZE 2048

void print_bootcmd(void)
{
	char bootcmd[COMMAND_LINE_SIZE];
	FILE *fp;
	int ret;

	memset(bootcmd, 0, COMMAND_LINE_SIZE);
	fp = fopen("/proc/cmdline", "r");
	if (!fp)
		return;

	ret = fread(bootcmd, sizeof(char), COMMAND_LINE_SIZE - 1, fp);
	if (ret) {
		bootcmd[ret] = '\0';
		/* the last character is already '\n' */
		fprintf(outf, "Kernel command line: %s", bootcmd);
	}

	fclose(fp);
}

int add_counter(unsigned int msr_num, char *path, char *name,
		unsigned int width, enum counter_scope scope,
		enum counter_type type, enum counter_format format, int flags)
{
	struct msr_counter *msrp;

	msrp = calloc(1, sizeof(struct msr_counter));
	if (msrp == NULL) {
		perror("calloc");
		exit(1);
	}

	msrp->msr_num = msr_num;
	strncpy(msrp->name, name, NAME_BYTES - 1);
	if (path)
		strncpy(msrp->path, path, PATH_BYTES - 1);
	msrp->width = width;
	msrp->type = type;
	msrp->format = format;
	msrp->flags = flags;

	switch (scope) {

	case SCOPE_CPU:
		msrp->next = sys.tp;
		sys.tp = msrp;
		sys.added_thread_counters++;
		if (sys.added_thread_counters > MAX_ADDED_THREAD_COUNTERS) {
			fprintf(stderr, "exceeded max %d added thread counters\n", MAX_ADDED_COUNTERS);
			exit(-1);
		}
		break;

	case SCOPE_CORE:
		msrp->next = sys.cp;
		sys.cp = msrp;
		sys.added_core_counters++;
		if (sys.added_core_counters > MAX_ADDED_COUNTERS) {
			fprintf(stderr, "exceeded max %d added core counters\n", MAX_ADDED_COUNTERS);
			exit(-1);
		}
		break;

	case SCOPE_PACKAGE:
		msrp->next = sys.pp;
		sys.pp = msrp;
		sys.added_package_counters++;
		if (sys.added_package_counters > MAX_ADDED_COUNTERS) {
			fprintf(stderr, "exceeded max %d added package counters\n", MAX_ADDED_COUNTERS);
			exit(-1);
		}
		break;
	}

	return 0;
}

void parse_add_command(char *add_command)
{
	int msr_num = 0;
	char *path = NULL;
	char name_buffer[NAME_BYTES] = "";
	int width = 64;
	int fail = 0;
	enum counter_scope scope = SCOPE_CPU;
	enum counter_type type = COUNTER_CYCLES;
	enum counter_format format = FORMAT_DELTA;

	while (add_command) {

		if (sscanf(add_command, "msr0x%x", &msr_num) == 1)
			goto next;

		if (sscanf(add_command, "msr%d", &msr_num) == 1)
			goto next;

		if (*add_command == '/') {
			path = add_command;
			goto next;
		}

		if (sscanf(add_command, "u%d", &width) == 1) {
			if ((width == 32) || (width == 64))
				goto next;
			width = 64;
		}
		if (!strncmp(add_command, "cpu", strlen("cpu"))) {
			scope = SCOPE_CPU;
			goto next;
		}
		if (!strncmp(add_command, "core", strlen("core"))) {
			scope = SCOPE_CORE;
			goto next;
		}
		if (!strncmp(add_command, "package", strlen("package"))) {
			scope = SCOPE_PACKAGE;
			goto next;
		}
		if (!strncmp(add_command, "cycles", strlen("cycles"))) {
			type = COUNTER_CYCLES;
			goto next;
		}
		if (!strncmp(add_command, "seconds", strlen("seconds"))) {
			type = COUNTER_SECONDS;
			goto next;
		}
		if (!strncmp(add_command, "usec", strlen("usec"))) {
			type = COUNTER_USEC;
			goto next;
		}
		if (!strncmp(add_command, "raw", strlen("raw"))) {
			format = FORMAT_RAW;
			goto next;
		}
		if (!strncmp(add_command, "delta", strlen("delta"))) {
			format = FORMAT_DELTA;
			goto next;
		}
		if (!strncmp(add_command, "percent", strlen("percent"))) {
			format = FORMAT_PERCENT;
			goto next;
		}

		if (sscanf(add_command, "%18s,%*s", name_buffer) == 1) {	/* 18 < NAME_BYTES */
			char *eos;

			eos = strchr(name_buffer, ',');
			if (eos)
				*eos = '\0';
			goto next;
		}

next:
		add_command = strchr(add_command, ',');
		if (add_command) {
			*add_command = '\0';
			add_command++;
		}

	}
	if ((msr_num == 0) && (path == NULL)) {
		fprintf(stderr, "--add: (msrDDD | msr0xXXX | /path_to_counter ) required\n");
		fail++;
	}

	/* generate default column header */
	if (*name_buffer == '\0') {
		if (width == 32)
			sprintf(name_buffer, "M0x%x%s", msr_num, format == FORMAT_PERCENT ? "%" : "");
		else
			sprintf(name_buffer, "M0X%x%s", msr_num, format == FORMAT_PERCENT ? "%" : "");
	}

	if (add_counter(msr_num, path, name_buffer, width, scope, type, format, 0))
		fail++;

	if (fail) {
		help();
		exit(1);
	}
}

int is_deferred_add(char *name)
{
	int i;

	for (i = 0; i < deferred_add_index; ++i)
		if (!strcmp(name, deferred_add_names[i]))
			return 1;
	return 0;
}

int is_deferred_skip(char *name)
{
	int i;

	for (i = 0; i < deferred_skip_index; ++i)
		if (!strcmp(name, deferred_skip_names[i]))
			return 1;
	return 0;
}

void probe_sysfs(void)
{
	char path[64];
	char name_buf[16];
	FILE *input;
	int state;
	char *sp;

	for (state = 10; state >= 0; --state) {

		sprintf(path, "/sys/devices/system/cpu/cpu%d/cpuidle/state%d/name", base_cpu, state);
		input = fopen(path, "r");
		if (input == NULL)
			continue;
		if (!fgets(name_buf, sizeof(name_buf), input))
			err(1, "%s: failed to read file", path);

		/* truncate "C1-HSW\n" to "C1", or truncate "C1\n" to "C1" */
		sp = strchr(name_buf, '-');
		if (!sp)
			sp = strchrnul(name_buf, '\n');
		*sp = '%';
		*(sp + 1) = '\0';

		remove_underbar(name_buf);

		fclose(input);

		sprintf(path, "cpuidle/state%d/time", state);

		if (!DO_BIC(BIC_sysfs) && !is_deferred_add(name_buf))
			continue;

		if (is_deferred_skip(name_buf))
			continue;

		add_counter(0, path, name_buf, 64, SCOPE_CPU, COUNTER_USEC, FORMAT_PERCENT, SYSFS_PERCPU);
	}

	for (state = 10; state >= 0; --state) {

		sprintf(path, "/sys/devices/system/cpu/cpu%d/cpuidle/state%d/name", base_cpu, state);
		input = fopen(path, "r");
		if (input == NULL)
			continue;
		if (!fgets(name_buf, sizeof(name_buf), input))
			err(1, "%s: failed to read file", path);
		/* truncate "C1-HSW\n" to "C1", or truncate "C1\n" to "C1" */
		sp = strchr(name_buf, '-');
		if (!sp)
			sp = strchrnul(name_buf, '\n');
		*sp = '\0';
		fclose(input);

		remove_underbar(name_buf);

		sprintf(path, "cpuidle/state%d/usage", state);

		if (!DO_BIC(BIC_sysfs) && !is_deferred_add(name_buf))
			continue;

		if (is_deferred_skip(name_buf))
			continue;

		add_counter(0, path, name_buf, 64, SCOPE_CPU, COUNTER_ITEMS, FORMAT_DELTA, SYSFS_PERCPU);
	}

}

/*
 * parse cpuset with following syntax
 * 1,2,4..6,8-10 and set bits in cpu_subset
 */
void parse_cpu_command(char *optarg)
{
	unsigned int start, end;
	char *next;

	if (!strcmp(optarg, "core")) {
		if (cpu_subset)
			goto error;
		show_core_only++;
		return;
	}
	if (!strcmp(optarg, "package")) {
		if (cpu_subset)
			goto error;
		show_pkg_only++;
		return;
	}
	if (show_core_only || show_pkg_only)
		goto error;

	cpu_subset = CPU_ALLOC(CPU_SUBSET_MAXCPUS);
	if (cpu_subset == NULL)
		err(3, "CPU_ALLOC");
	cpu_subset_size = CPU_ALLOC_SIZE(CPU_SUBSET_MAXCPUS);

	CPU_ZERO_S(cpu_subset_size, cpu_subset);

	next = optarg;

	while (next && *next) {

		if (*next == '-')	/* no negative cpu numbers */
			goto error;

		start = strtoul(next, &next, 10);

		if (start >= CPU_SUBSET_MAXCPUS)
			goto error;
		CPU_SET_S(start, cpu_subset_size, cpu_subset);

		if (*next == '\0')
			break;

		if (*next == ',') {
			next += 1;
			continue;
		}

		if (*next == '-') {
			next += 1;	/* start range */
		} else if (*next == '.') {
			next += 1;
			if (*next == '.')
				next += 1;	/* start range */
			else
				goto error;
		}

		end = strtoul(next, &next, 10);
		if (end <= start)
			goto error;

		while (++start <= end) {
			if (start >= CPU_SUBSET_MAXCPUS)
				goto error;
			CPU_SET_S(start, cpu_subset_size, cpu_subset);
		}

		if (*next == ',')
			next += 1;
		else if (*next != '\0')
			goto error;
	}

	return;

error:
	fprintf(stderr, "\"--cpu %s\" malformed\n", optarg);
	help();
	exit(-1);
}

void cmdline(int argc, char **argv)
{
	int opt;
	int option_index = 0;
	static struct option long_options[] = {
		{ "add", required_argument, 0, 'a' },
		{ "cpu", required_argument, 0, 'c' },
		{ "Dump", no_argument, 0, 'D' },
		{ "debug", no_argument, 0, 'd' },	/* internal, not documented */
		{ "enable", required_argument, 0, 'e' },
		{ "interval", required_argument, 0, 'i' },
		{ "IPC", no_argument, 0, 'I' },
		{ "num_iterations", required_argument, 0, 'n' },
		{ "header_iterations", required_argument, 0, 'N' },
		{ "help", no_argument, 0, 'h' },
		{ "hide", required_argument, 0, 'H' },	// meh, -h taken by --help
		{ "Joules", no_argument, 0, 'J' },
		{ "list", no_argument, 0, 'l' },
		{ "out", required_argument, 0, 'o' },
		{ "quiet", no_argument, 0, 'q' },
		{ "show", required_argument, 0, 's' },
		{ "Summary", no_argument, 0, 'S' },
		{ "TCC", required_argument, 0, 'T' },
		{ "version", no_argument, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	progname = argv[0];

	while ((opt = getopt_long_only(argc, argv, "+C:c:Dde:hi:Jn:o:qST:v", long_options, &option_index)) != -1) {
		switch (opt) {
		case 'a':
			parse_add_command(optarg);
			break;
		case 'c':
			parse_cpu_command(optarg);
			break;
		case 'D':
			dump_only++;
			break;
		case 'e':
			/* --enable specified counter */
			bic_enabled = bic_enabled | bic_lookup(optarg, SHOW_LIST);
			break;
		case 'd':
			debug++;
			ENABLE_BIC(BIC_DISABLED_BY_DEFAULT);
			break;
		case 'H':
			/*
			 * --hide: do not show those specified
			 *  multiple invocations simply clear more bits in enabled mask
			 */
			bic_enabled &= ~bic_lookup(optarg, HIDE_LIST);
			break;
		case 'h':
		default:
			help();
			exit(1);
		case 'i':
			{
				double interval = strtod(optarg, NULL);

				if (interval < 0.001) {
					fprintf(outf, "interval %f seconds is too small\n", interval);
					exit(2);
				}

				interval_tv.tv_sec = interval_ts.tv_sec = interval;
				interval_tv.tv_usec = (interval - interval_tv.tv_sec) * 1000000;
				interval_ts.tv_nsec = (interval - interval_ts.tv_sec) * 1000000000;
			}
			break;
		case 'J':
			rapl_joules++;
			break;
		case 'l':
			ENABLE_BIC(BIC_DISABLED_BY_DEFAULT);
			list_header_only++;
			quiet++;
			break;
		case 'o':
			outf = fopen_or_die(optarg, "w");
			break;
		case 'q':
			quiet = 1;
			break;
		case 'n':
			num_iterations = strtod(optarg, NULL);

			if (num_iterations <= 0) {
				fprintf(outf, "iterations %d should be positive number\n", num_iterations);
				exit(2);
			}
			break;
		case 'N':
			header_iterations = strtod(optarg, NULL);

			if (header_iterations <= 0) {
				fprintf(outf, "iterations %d should be positive number\n", header_iterations);
				exit(2);
			}
			break;
		case 's':
			/*
			 * --show: show only those specified
			 *  The 1st invocation will clear and replace the enabled mask
			 *  subsequent invocations can add to it.
			 */
			if (shown == 0)
				bic_enabled = bic_lookup(optarg, SHOW_LIST);
			else
				bic_enabled |= bic_lookup(optarg, SHOW_LIST);
			shown = 1;
			break;
		case 'S':
			summary_only++;
			break;
		case 'T':
			tj_max_override = atoi(optarg);
			break;
		case 'v':
			print_version();
			exit(0);
			break;
		}
	}
}

int main(int argc, char **argv)
{
	outf = stderr;
	cmdline(argc, argv);

	if (!quiet) {
		print_version();
		print_bootcmd();
	}

	probe_sysfs();

	turbostat_init();

	msr_sum_record();

	/* dump counters and exit */
	if (dump_only)
		return get_and_dump_counters();

	/* list header and exit */
	if (list_header_only) {
		print_header(",");
		flush_output_stdout();
		return 0;
	}

	/*
	 * if any params left, it must be a command to fork
	 */
	if (argc - optind)
		return fork_it(argv + optind);
	else
		turbostat_loop();

	return 0;
}
