// SPDX-License-Identifier: GPL-2.0-only
/*
 * turbostat -- show CPU frequency and C-state residency
 * on modern Intel and AMD processors.
 *
 * Copyright (c) 2025 Intel Corporation.
 * Len Brown <len.brown@intel.com>
 */

#define _GNU_SOURCE
#include MSRHEADER

// copied from arch/x86/include/asm/cpu_device_id.h
#define VFM_MODEL_BIT	0
#define VFM_FAMILY_BIT	8
#define VFM_VENDOR_BIT	16
#define VFM_RSVD_BIT	24

#define	VFM_MODEL_MASK	GENMASK(VFM_FAMILY_BIT - 1, VFM_MODEL_BIT)
#define	VFM_FAMILY_MASK	GENMASK(VFM_VENDOR_BIT - 1, VFM_FAMILY_BIT)
#define	VFM_VENDOR_MASK	GENMASK(VFM_RSVD_BIT - 1, VFM_VENDOR_BIT)

#define VFM_MODEL(vfm)	(((vfm) & VFM_MODEL_MASK) >> VFM_MODEL_BIT)
#define VFM_FAMILY(vfm)	(((vfm) & VFM_FAMILY_MASK) >> VFM_FAMILY_BIT)
#define VFM_VENDOR(vfm)	(((vfm) & VFM_VENDOR_MASK) >> VFM_VENDOR_BIT)

#define	VFM_MAKE(_vendor, _family, _model) (	\
	((_model) << VFM_MODEL_BIT) |		\
	((_family) << VFM_FAMILY_BIT) |		\
	((_vendor) << VFM_VENDOR_BIT)		\
)
// end copied section

#define CPUID_LEAF_MODEL_ID			0x1A
#define CPUID_LEAF_MODEL_ID_CORE_TYPE_SHIFT	24

#define X86_VENDOR_INTEL	0

#include INTEL_FAMILY_HEADER
#include BUILD_BUG_HEADER
#include <stdarg.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <sys/mman.h>
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
#include <assert.h>
#include <linux/kernel.h>

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
#define PERF_NAME_BYTES 128

#define MAX_NOFILE 0x8000

#define COUNTER_KIND_PERF_PREFIX "perf/"
#define COUNTER_KIND_PERF_PREFIX_LEN strlen(COUNTER_KIND_PERF_PREFIX)
#define PERF_DEV_NAME_BYTES 32
#define PERF_EVT_NAME_BYTES 32

#define INTEL_ECORE_TYPE	0x20
#define INTEL_PCORE_TYPE	0x40

#define ROUND_UP_TO_PAGE_SIZE(n) (((n) + 0x1000UL-1UL) & ~(0x1000UL-1UL))

enum counter_scope { SCOPE_CPU, SCOPE_CORE, SCOPE_PACKAGE };
enum counter_type { COUNTER_ITEMS, COUNTER_CYCLES, COUNTER_SECONDS, COUNTER_USEC, COUNTER_K2M };
enum counter_format { FORMAT_RAW, FORMAT_DELTA, FORMAT_PERCENT, FORMAT_AVERAGE };
enum counter_source { COUNTER_SOURCE_NONE, COUNTER_SOURCE_PERF, COUNTER_SOURCE_MSR };

struct perf_counter_info {
	struct perf_counter_info *next;

	/* How to open the counter / What counter it is. */
	char device[PERF_DEV_NAME_BYTES];
	char event[PERF_EVT_NAME_BYTES];

	/* How to show/format the counter. */
	char name[PERF_NAME_BYTES];
	unsigned int width;
	enum counter_scope scope;
	enum counter_type type;
	enum counter_format format;
	double scale;

	/* For reading the counter. */
	int *fd_perf_per_domain;
	size_t num_domains;
};

struct sysfs_path {
	char path[PATH_BYTES];
	int id;
	struct sysfs_path *next;
};

struct msr_counter {
	unsigned int msr_num;
	char name[NAME_BYTES];
	struct sysfs_path *sp;
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
	{ 0x0, "usec", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Time_Of_Day_Seconds", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Package", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Node", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Avg_MHz", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Busy%", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Bzy_MHz", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "TSC_MHz", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "IRQ", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "SMI", NULL, 32, 0, FORMAT_DELTA, NULL, 0 },
	{ 0x0, "sysfs", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU%c1", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU%c3", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU%c6", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU%c7", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "ThreadC", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CoreTmp", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CoreCnt", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "PkgTmp", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "GFX%rc6", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "GFXMHz", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc2", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc3", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc6", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc7", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc8", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg%pc9", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Pk%pc10", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU%LPI", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "SYS%LPI", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "PkgWatt", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CorWatt", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "GFXWatt", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "PkgCnt", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "RAMWatt", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "PKG_%", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "RAM_%", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Pkg_J", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Cor_J", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "GFX_J", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "RAM_J", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Mod%c6", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Totl%C0", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Any%C0", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "GFX%C0", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CPUGFX%", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Core", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "APIC", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "X2APIC", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Die", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "GFXAMHz", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "IPC", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CoreThr", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "UncMHz", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "SAM%mc6", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "SAMMHz", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "SAMAMHz", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Die%c6", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "SysWatt", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "Sys_J", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "NMI", NULL, 0, 0, 0, NULL, 0 },
	{ 0x0, "CPU%c1e", NULL, 0, 0, 0, NULL, 0 },
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
#define	BIC_SAM_mc6		(1ULL << 55)
#define	BIC_SAMMHz		(1ULL << 56)
#define	BIC_SAMACTMHz		(1ULL << 57)
#define	BIC_Diec6		(1ULL << 58)
#define	BIC_SysWatt		(1ULL << 59)
#define	BIC_Sys_J		(1ULL << 60)
#define	BIC_NMI			(1ULL << 61)
#define	BIC_CPU_c1e		(1ULL << 62)

#define BIC_TOPOLOGY (BIC_Package | BIC_Node | BIC_CoreCnt | BIC_PkgCnt | BIC_Core | BIC_CPU | BIC_Die)
#define BIC_THERMAL_PWR (BIC_CoreTmp | BIC_PkgTmp | BIC_PkgWatt | BIC_CorWatt | BIC_GFXWatt | BIC_RAMWatt | BIC_PKG__ | BIC_RAM__ | BIC_SysWatt)
#define BIC_FREQUENCY (BIC_Avg_MHz | BIC_Busy | BIC_Bzy_MHz | BIC_TSC_MHz | BIC_GFXMHz | BIC_GFXACTMHz | BIC_SAMMHz | BIC_SAMACTMHz | BIC_UNCORE_MHZ)
#define BIC_IDLE (BIC_Busy | BIC_sysfs | BIC_CPU_c1 | BIC_CPU_c3 | BIC_CPU_c6 | BIC_CPU_c7 | BIC_GFX_rc6 | BIC_Pkgpc2 | BIC_Pkgpc3 | BIC_Pkgpc6 | BIC_Pkgpc7 | BIC_Pkgpc8 | BIC_Pkgpc9 | BIC_Pkgpc10 | BIC_CPU_LPI | BIC_SYS_LPI | BIC_Mod_c6 | BIC_Totl_c0 | BIC_Any_c0 | BIC_GFX_c0 | BIC_CPUGFX | BIC_SAM_mc6 | BIC_Diec6)
#define BIC_OTHER (BIC_IRQ | BIC_NMI | BIC_SMI | BIC_ThreadC | BIC_CoreTmp | BIC_IPC)

#define BIC_DISABLED_BY_DEFAULT	(BIC_USEC | BIC_TOD | BIC_APIC | BIC_X2APIC)

unsigned long long bic_enabled = (0xFFFFFFFFFFFFFFFFULL & ~BIC_DISABLED_BY_DEFAULT);
unsigned long long bic_present = BIC_USEC | BIC_TOD | BIC_sysfs | BIC_APIC | BIC_X2APIC;

#define DO_BIC(COUNTER_NAME) (bic_enabled & bic_present & COUNTER_NAME)
#define DO_BIC_READ(COUNTER_NAME) (bic_present & COUNTER_NAME)
#define ENABLE_BIC(COUNTER_NAME) (bic_enabled |= COUNTER_NAME)
#define BIC_PRESENT(COUNTER_BIT) (bic_present |= COUNTER_BIT)
#define BIC_NOT_PRESENT(COUNTER_BIT) (bic_present &= ~COUNTER_BIT)
#define BIC_IS_ENABLED(COUNTER_BIT) (bic_enabled & COUNTER_BIT)

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

struct amperf_group_fd;

char *proc_stat = "/proc/stat";
FILE *outf;
int *fd_percpu;
int *fd_instr_count_percpu;
struct timeval interval_tv = { 5, 0 };
struct timespec interval_ts = { 5, 0 };

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
unsigned int force_load;
unsigned int has_aperf;
unsigned int has_aperf_access;
unsigned int has_epb;
unsigned int has_turbo;
unsigned int is_hybrid;
unsigned int units = 1000000;	/* MHz etc */
unsigned int genuine_intel;
unsigned int authentic_amd;
unsigned int hygon_genuine;
unsigned int max_level, max_extended_level;
unsigned int has_invariant_tsc;
unsigned int aperf_mperf_multiplier = 1;
double bclk;
double base_hz;
unsigned int has_base_hz;
double tsc_tweak = 1.0;
unsigned int show_pkg_only;
unsigned int show_core_only;
char *output_buffer, *outp;
unsigned int do_dts;
unsigned int do_ptm;
unsigned int do_ipc;
unsigned long long cpuidle_cur_cpu_lpi_us;
unsigned long long cpuidle_cur_sys_lpi_us;
unsigned int tj_max;
unsigned int tj_max_override;
double rapl_power_units, rapl_time_units;
double rapl_dram_energy_units, rapl_energy_units, rapl_psys_energy_units;
double rapl_joule_counter_range;
unsigned int crystal_hz;
unsigned long long tsc_hz;
int base_cpu;
unsigned int has_hwp;		/* IA32_PM_ENABLE, IA32_HWP_CAPABILITIES */
			/* IA32_HWP_REQUEST, IA32_HWP_STATUS */
unsigned int has_hwp_notify;	/* IA32_HWP_INTERRUPT */
unsigned int has_hwp_activity_window;	/* IA32_HWP_REQUEST[bits 41:32] */
unsigned int has_hwp_epp;	/* IA32_HWP_REQUEST[bits 31:24] */
unsigned int has_hwp_pkg;	/* IA32_HWP_REQUEST_PKG */
unsigned int first_counter_read = 1;

static struct timeval procsysfs_tv_begin;

int ignore_stdin;
bool no_msr;
bool no_perf;

enum gfx_sysfs_idx {
	GFX_rc6,
	GFX_MHz,
	GFX_ACTMHz,
	SAM_mc6,
	SAM_MHz,
	SAM_ACTMHz,
	GFX_MAX
};

struct gfx_sysfs_info {
	FILE *fp;
	unsigned int val;
	unsigned long long val_ull;
};

static struct gfx_sysfs_info gfx_info[GFX_MAX];

int get_msr(int cpu, off_t offset, unsigned long long *msr);
int add_counter(unsigned int msr_num, char *path, char *name,
		unsigned int width, enum counter_scope scope,
		enum counter_type type, enum counter_format format, int flags, int package_num);

/* Model specific support Start */

/* List of features that may diverge among different platforms */
struct platform_features {
	bool has_msr_misc_feature_control;	/* MSR_MISC_FEATURE_CONTROL */
	bool has_msr_misc_pwr_mgmt;	/* MSR_MISC_PWR_MGMT */
	bool has_nhm_msrs;	/* MSR_PLATFORM_INFO, MSR_IA32_TEMPERATURE_TARGET, MSR_SMI_COUNT, MSR_PKG_CST_CONFIG_CONTROL, MSR_IA32_POWER_CTL, TRL MSRs */
	bool has_config_tdp;	/* MSR_CONFIG_TDP_NOMINAL/LEVEL_1/LEVEL_2/CONTROL, MSR_TURBO_ACTIVATION_RATIO */
	int bclk_freq;		/* CPU base clock */
	int crystal_freq;	/* Crystal clock to use when not available from CPUID.15 */
	int supported_cstates;	/* Core cstates and Package cstates supported */
	int cst_limit;		/* MSR_PKG_CST_CONFIG_CONTROL */
	bool has_cst_auto_convension;	/* AUTOMATIC_CSTATE_CONVERSION bit in MSR_PKG_CST_CONFIG_CONTROL */
	bool has_irtl_msrs;	/* MSR_PKGC3/PKGC6/PKGC7/PKGC8/PKGC9/PKGC10_IRTL */
	bool has_msr_core_c1_res;	/* MSR_CORE_C1_RES */
	bool has_msr_module_c6_res_ms;	/* MSR_MODULE_C6_RES_MS */
	bool has_msr_c6_demotion_policy_config;	/* MSR_CC6_DEMOTION_POLICY_CONFIG/MSR_MC6_DEMOTION_POLICY_CONFIG */
	bool has_msr_atom_pkg_c6_residency;	/* MSR_ATOM_PKG_C6_RESIDENCY */
	bool has_msr_knl_core_c6_residency;	/* MSR_KNL_CORE_C6_RESIDENCY */
	bool has_ext_cst_msrs;	/* MSR_PKG_WEIGHTED_CORE_C0_RES/MSR_PKG_ANY_CORE_C0_RES/MSR_PKG_ANY_GFXE_C0_RES/MSR_PKG_BOTH_CORE_GFXE_C0_RES */
	bool has_cst_prewake_bit;	/* Cstate prewake bit in MSR_IA32_POWER_CTL */
	int trl_msrs;		/* MSR_TURBO_RATIO_LIMIT/LIMIT1/LIMIT2/SECONDARY, Atom TRL MSRs */
	int plr_msrs;		/* MSR_CORE/GFX/RING_PERF_LIMIT_REASONS */
	int rapl_msrs;		/* RAPL PKG/DRAM/CORE/GFX MSRs, AMD RAPL MSRs */
	bool has_per_core_rapl;	/* Indicates cores energy collection is per-core, not per-package. AMD specific for now */
	bool has_rapl_divisor;	/* Divisor for Energy unit raw value from MSR_RAPL_POWER_UNIT */
	bool has_fixed_rapl_unit;	/* Fixed Energy Unit used for DRAM RAPL Domain */
	bool has_fixed_rapl_psys_unit;	/* Fixed Energy Unit used for PSYS RAPL Domain */
	int rapl_quirk_tdp;	/* Hardcoded TDP value when cannot be retrieved from hardware */
	int tcc_offset_bits;	/* TCC Offset bits in MSR_IA32_TEMPERATURE_TARGET */
	bool enable_tsc_tweak;	/* Use CPU Base freq instead of TSC freq for aperf/mperf counter */
	bool need_perf_multiplier;	/* mperf/aperf multiplier */
};

struct platform_data {
	unsigned int vfm;
	const struct platform_features *features;
};

/* For BCLK */
enum bclk_freq {
	BCLK_100MHZ = 1,
	BCLK_133MHZ,
	BCLK_SLV,
};

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

/* For Package cstate limit */
enum package_cstate_limit {
	CST_LIMIT_NHM = 1,
	CST_LIMIT_SNB,
	CST_LIMIT_HSW,
	CST_LIMIT_SKX,
	CST_LIMIT_ICX,
	CST_LIMIT_SLV,
	CST_LIMIT_AMT,
	CST_LIMIT_KNL,
	CST_LIMIT_GMT,
};

/* For Turbo Ratio Limit MSRs */
enum turbo_ratio_limit_msrs {
	TRL_BASE = BIT(0),
	TRL_LIMIT1 = BIT(1),
	TRL_LIMIT2 = BIT(2),
	TRL_ATOM = BIT(3),
	TRL_KNL = BIT(4),
	TRL_CORECOUNT = BIT(5),
};

/* For Perf Limit Reason MSRs */
enum perf_limit_reason_msrs {
	PLR_CORE = BIT(0),
	PLR_GFX = BIT(1),
	PLR_RING = BIT(2),
};

/* For RAPL MSRs */
enum rapl_msrs {
	RAPL_PKG_POWER_LIMIT = BIT(0),	/* 0x610 MSR_PKG_POWER_LIMIT */
	RAPL_PKG_ENERGY_STATUS = BIT(1),	/* 0x611 MSR_PKG_ENERGY_STATUS */
	RAPL_PKG_PERF_STATUS = BIT(2),	/* 0x613 MSR_PKG_PERF_STATUS */
	RAPL_PKG_POWER_INFO = BIT(3),	/* 0x614 MSR_PKG_POWER_INFO */
	RAPL_DRAM_POWER_LIMIT = BIT(4),	/* 0x618 MSR_DRAM_POWER_LIMIT */
	RAPL_DRAM_ENERGY_STATUS = BIT(5),	/* 0x619 MSR_DRAM_ENERGY_STATUS */
	RAPL_DRAM_PERF_STATUS = BIT(6),	/* 0x61b MSR_DRAM_PERF_STATUS */
	RAPL_DRAM_POWER_INFO = BIT(7),	/* 0x61c MSR_DRAM_POWER_INFO */
	RAPL_CORE_POWER_LIMIT = BIT(8),	/* 0x638 MSR_PP0_POWER_LIMIT */
	RAPL_CORE_ENERGY_STATUS = BIT(9),	/* 0x639 MSR_PP0_ENERGY_STATUS */
	RAPL_CORE_POLICY = BIT(10),	/* 0x63a MSR_PP0_POLICY */
	RAPL_GFX_POWER_LIMIT = BIT(11),	/* 0x640 MSR_PP1_POWER_LIMIT */
	RAPL_GFX_ENERGY_STATUS = BIT(12),	/* 0x641 MSR_PP1_ENERGY_STATUS */
	RAPL_GFX_POLICY = BIT(13),	/* 0x642 MSR_PP1_POLICY */
	RAPL_AMD_PWR_UNIT = BIT(14),	/* 0xc0010299 MSR_AMD_RAPL_POWER_UNIT */
	RAPL_AMD_CORE_ENERGY_STAT = BIT(15),	/* 0xc001029a MSR_AMD_CORE_ENERGY_STATUS */
	RAPL_AMD_PKG_ENERGY_STAT = BIT(16),	/* 0xc001029b MSR_AMD_PKG_ENERGY_STATUS */
	RAPL_PLATFORM_ENERGY_LIMIT = BIT(17),	/* 0x64c MSR_PLATFORM_ENERGY_LIMIT */
	RAPL_PLATFORM_ENERGY_STATUS = BIT(18),	/* 0x64d MSR_PLATFORM_ENERGY_STATUS */
};

#define RAPL_PKG	(RAPL_PKG_ENERGY_STATUS | RAPL_PKG_POWER_LIMIT)
#define RAPL_DRAM	(RAPL_DRAM_ENERGY_STATUS | RAPL_DRAM_POWER_LIMIT)
#define RAPL_CORE	(RAPL_CORE_ENERGY_STATUS | RAPL_CORE_POWER_LIMIT)
#define RAPL_GFX	(RAPL_GFX_POWER_LIMIT | RAPL_GFX_ENERGY_STATUS)
#define RAPL_PSYS	(RAPL_PLATFORM_ENERGY_STATUS | RAPL_PLATFORM_ENERGY_LIMIT)

#define RAPL_PKG_ALL	(RAPL_PKG | RAPL_PKG_PERF_STATUS | RAPL_PKG_POWER_INFO)
#define RAPL_DRAM_ALL	(RAPL_DRAM | RAPL_DRAM_PERF_STATUS | RAPL_DRAM_POWER_INFO)
#define RAPL_CORE_ALL	(RAPL_CORE | RAPL_CORE_POLICY)
#define RAPL_GFX_ALL	(RAPL_GFX | RAPL_GFX_POLIGY)

#define RAPL_AMD_F17H	(RAPL_AMD_PWR_UNIT | RAPL_AMD_CORE_ENERGY_STAT | RAPL_AMD_PKG_ENERGY_STAT)

/* For Cstates */
enum cstates {
	CC1 = BIT(0),
	CC3 = BIT(1),
	CC6 = BIT(2),
	CC7 = BIT(3),
	PC2 = BIT(4),
	PC3 = BIT(5),
	PC6 = BIT(6),
	PC7 = BIT(7),
	PC8 = BIT(8),
	PC9 = BIT(9),
	PC10 = BIT(10),
};

static const struct platform_features nhm_features = {
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_133MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | PC3 | PC6,
	.cst_limit = CST_LIMIT_NHM,
	.trl_msrs = TRL_BASE,
};

static const struct platform_features nhx_features = {
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_133MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | PC3 | PC6,
	.cst_limit = CST_LIMIT_NHM,
};

static const struct platform_features snb_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7,
	.cst_limit = CST_LIMIT_SNB,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE,
	.rapl_msrs = RAPL_PKG | RAPL_CORE_ALL | RAPL_GFX | RAPL_PKG_POWER_INFO,
};

static const struct platform_features snx_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7,
	.cst_limit = CST_LIMIT_SNB,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_CORE_ALL | RAPL_DRAM_ALL,
};

static const struct platform_features ivb_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7,
	.cst_limit = CST_LIMIT_SNB,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE,
	.rapl_msrs = RAPL_PKG | RAPL_CORE_ALL | RAPL_GFX | RAPL_PKG_POWER_INFO,
};

static const struct platform_features ivx_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7,
	.cst_limit = CST_LIMIT_SNB,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE | TRL_LIMIT1,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_CORE_ALL | RAPL_DRAM_ALL,
};

static const struct platform_features hsw_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7,
	.cst_limit = CST_LIMIT_HSW,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE,
	.plr_msrs = PLR_CORE | PLR_GFX | PLR_RING,
	.rapl_msrs = RAPL_PKG | RAPL_CORE_ALL | RAPL_GFX | RAPL_PKG_POWER_INFO,
};

static const struct platform_features hsx_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7,
	.cst_limit = CST_LIMIT_HSW,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE | TRL_LIMIT1 | TRL_LIMIT2,
	.plr_msrs = PLR_CORE | PLR_RING,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_DRAM_ALL,
	.has_fixed_rapl_unit = 1,
};

static const struct platform_features hswl_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7 | PC8 | PC9 | PC10,
	.cst_limit = CST_LIMIT_HSW,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE,
	.plr_msrs = PLR_CORE | PLR_GFX | PLR_RING,
	.rapl_msrs = RAPL_PKG | RAPL_CORE_ALL | RAPL_GFX | RAPL_PKG_POWER_INFO,
};

static const struct platform_features hswg_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7,
	.cst_limit = CST_LIMIT_HSW,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE,
	.plr_msrs = PLR_CORE | PLR_GFX | PLR_RING,
	.rapl_msrs = RAPL_PKG | RAPL_CORE_ALL | RAPL_GFX | RAPL_PKG_POWER_INFO,
};

static const struct platform_features bdw_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7 | PC8 | PC9 | PC10,
	.cst_limit = CST_LIMIT_HSW,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE,
	.rapl_msrs = RAPL_PKG | RAPL_CORE_ALL | RAPL_GFX | RAPL_PKG_POWER_INFO,
};

static const struct platform_features bdwg_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7,
	.cst_limit = CST_LIMIT_HSW,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE,
	.rapl_msrs = RAPL_PKG | RAPL_CORE_ALL | RAPL_GFX | RAPL_PKG_POWER_INFO,
};

static const struct platform_features bdx_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | PC2 | PC3 | PC6,
	.cst_limit = CST_LIMIT_HSW,
	.has_irtl_msrs = 1,
	.has_cst_auto_convension = 1,
	.trl_msrs = TRL_BASE,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_DRAM_ALL,
	.has_fixed_rapl_unit = 1,
};

static const struct platform_features skl_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.crystal_freq = 24000000,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7 | PC8 | PC9 | PC10,
	.cst_limit = CST_LIMIT_HSW,
	.has_irtl_msrs = 1,
	.has_ext_cst_msrs = 1,
	.trl_msrs = TRL_BASE,
	.tcc_offset_bits = 6,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_CORE_ALL | RAPL_DRAM | RAPL_DRAM_PERF_STATUS | RAPL_GFX | RAPL_PSYS,
	.enable_tsc_tweak = 1,
};

static const struct platform_features cnl_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7 | PC8 | PC9 | PC10,
	.cst_limit = CST_LIMIT_HSW,
	.has_irtl_msrs = 1,
	.has_msr_core_c1_res = 1,
	.has_ext_cst_msrs = 1,
	.trl_msrs = TRL_BASE,
	.tcc_offset_bits = 6,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_CORE_ALL | RAPL_DRAM | RAPL_DRAM_PERF_STATUS | RAPL_GFX | RAPL_PSYS,
	.enable_tsc_tweak = 1,
};

/* Copied from cnl_features, with PC7/PC9 removed */
static const struct platform_features adl_features = {
	.has_msr_misc_feature_control	= cnl_features.has_msr_misc_feature_control,
	.has_msr_misc_pwr_mgmt		= cnl_features.has_msr_misc_pwr_mgmt,
	.has_nhm_msrs			= cnl_features.has_nhm_msrs,
	.has_config_tdp			= cnl_features.has_config_tdp,
	.bclk_freq			= cnl_features.bclk_freq,
	.supported_cstates		= CC1 | CC6 | CC7 | PC2 | PC3 | PC6 | PC8 | PC10,
	.cst_limit			= cnl_features.cst_limit,
	.has_irtl_msrs			= cnl_features.has_irtl_msrs,
	.has_msr_core_c1_res		= cnl_features.has_msr_core_c1_res,
	.has_ext_cst_msrs		= cnl_features.has_ext_cst_msrs,
	.trl_msrs			= cnl_features.trl_msrs,
	.tcc_offset_bits		= cnl_features.tcc_offset_bits,
	.rapl_msrs			= cnl_features.rapl_msrs,
	.enable_tsc_tweak		= cnl_features.enable_tsc_tweak,
};

/* Copied from adl_features, with PC3/PC8 removed */
static const struct platform_features lnl_features = {
	.has_msr_misc_feature_control	= adl_features.has_msr_misc_feature_control,
	.has_msr_misc_pwr_mgmt		= adl_features.has_msr_misc_pwr_mgmt,
	.has_nhm_msrs			= adl_features.has_nhm_msrs,
	.has_config_tdp			= adl_features.has_config_tdp,
	.bclk_freq			= adl_features.bclk_freq,
	.supported_cstates		= CC1 | CC6 | CC7 | PC2 | PC6 | PC10,
	.cst_limit			= adl_features.cst_limit,
	.has_irtl_msrs			= adl_features.has_irtl_msrs,
	.has_msr_core_c1_res		= adl_features.has_msr_core_c1_res,
	.has_ext_cst_msrs		= adl_features.has_ext_cst_msrs,
	.trl_msrs			= adl_features.trl_msrs,
	.tcc_offset_bits		= adl_features.tcc_offset_bits,
	.rapl_msrs			= adl_features.rapl_msrs,
	.enable_tsc_tweak		= adl_features.enable_tsc_tweak,
};

static const struct platform_features skx_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC6 | PC2 | PC6,
	.cst_limit = CST_LIMIT_SKX,
	.has_irtl_msrs = 1,
	.has_cst_auto_convension = 1,
	.trl_msrs = TRL_BASE | TRL_CORECOUNT,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_DRAM_ALL,
	.has_fixed_rapl_unit = 1,
};

static const struct platform_features icx_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC6 | PC2 | PC6,
	.cst_limit = CST_LIMIT_ICX,
	.has_msr_core_c1_res = 1,
	.has_irtl_msrs = 1,
	.has_cst_prewake_bit = 1,
	.trl_msrs = TRL_BASE | TRL_CORECOUNT,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_DRAM_ALL | RAPL_PSYS,
	.has_fixed_rapl_unit = 1,
};

static const struct platform_features spr_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC6 | PC2 | PC6,
	.cst_limit = CST_LIMIT_SKX,
	.has_msr_core_c1_res = 1,
	.has_irtl_msrs = 1,
	.has_cst_prewake_bit = 1,
	.has_fixed_rapl_psys_unit = 1,
	.trl_msrs = TRL_BASE | TRL_CORECOUNT,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_DRAM_ALL | RAPL_PSYS,
};

static const struct platform_features srf_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC6 | PC2 | PC6,
	.cst_limit = CST_LIMIT_SKX,
	.has_msr_core_c1_res = 1,
	.has_msr_module_c6_res_ms = 1,
	.has_irtl_msrs = 1,
	.has_cst_prewake_bit = 1,
	.trl_msrs = TRL_BASE | TRL_CORECOUNT,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_DRAM_ALL | RAPL_PSYS,
};

static const struct platform_features grr_features = {
	.has_msr_misc_feature_control = 1,
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC6,
	.cst_limit = CST_LIMIT_SKX,
	.has_msr_core_c1_res = 1,
	.has_msr_module_c6_res_ms = 1,
	.has_irtl_msrs = 1,
	.has_cst_prewake_bit = 1,
	.trl_msrs = TRL_BASE | TRL_CORECOUNT,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_DRAM_ALL | RAPL_PSYS,
};

static const struct platform_features slv_features = {
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_SLV,
	.supported_cstates = CC1 | CC6 | PC6,
	.cst_limit = CST_LIMIT_SLV,
	.has_msr_core_c1_res = 1,
	.has_msr_module_c6_res_ms = 1,
	.has_msr_c6_demotion_policy_config = 1,
	.has_msr_atom_pkg_c6_residency = 1,
	.trl_msrs = TRL_ATOM,
	.rapl_msrs = RAPL_PKG | RAPL_CORE,
	.has_rapl_divisor = 1,
	.rapl_quirk_tdp = 30,
};

static const struct platform_features slvd_features = {
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_SLV,
	.supported_cstates = CC1 | CC6 | PC3 | PC6,
	.cst_limit = CST_LIMIT_SLV,
	.has_msr_atom_pkg_c6_residency = 1,
	.trl_msrs = TRL_BASE,
	.rapl_msrs = RAPL_PKG | RAPL_CORE,
	.rapl_quirk_tdp = 30,
};

static const struct platform_features amt_features = {
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_133MHZ,
	.supported_cstates = CC1 | CC3 | CC6 | PC3 | PC6,
	.cst_limit = CST_LIMIT_AMT,
	.trl_msrs = TRL_BASE,
};

static const struct platform_features gmt_features = {
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_100MHZ,
	.crystal_freq = 19200000,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7 | PC8 | PC9 | PC10,
	.cst_limit = CST_LIMIT_GMT,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE | TRL_CORECOUNT,
	.rapl_msrs = RAPL_PKG | RAPL_PKG_POWER_INFO,
};

static const struct platform_features gmtd_features = {
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_100MHZ,
	.crystal_freq = 25000000,
	.supported_cstates = CC1 | CC6 | PC2 | PC6,
	.cst_limit = CST_LIMIT_GMT,
	.has_irtl_msrs = 1,
	.has_msr_core_c1_res = 1,
	.trl_msrs = TRL_BASE | TRL_CORECOUNT,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_DRAM_ALL | RAPL_CORE_ENERGY_STATUS,
};

static const struct platform_features gmtp_features = {
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_100MHZ,
	.crystal_freq = 19200000,
	.supported_cstates = CC1 | CC3 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7 | PC8 | PC9 | PC10,
	.cst_limit = CST_LIMIT_GMT,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE,
	.rapl_msrs = RAPL_PKG | RAPL_PKG_POWER_INFO,
};

static const struct platform_features tmt_features = {
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC6 | CC7 | PC2 | PC3 | PC6 | PC7 | PC8 | PC9 | PC10,
	.cst_limit = CST_LIMIT_GMT,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_CORE_ALL | RAPL_DRAM | RAPL_DRAM_PERF_STATUS | RAPL_GFX,
	.enable_tsc_tweak = 1,
};

static const struct platform_features tmtd_features = {
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC6,
	.cst_limit = CST_LIMIT_GMT,
	.has_irtl_msrs = 1,
	.trl_msrs = TRL_BASE | TRL_CORECOUNT,
	.rapl_msrs = RAPL_PKG_ALL,
};

static const struct platform_features knl_features = {
	.has_msr_misc_pwr_mgmt = 1,
	.has_nhm_msrs = 1,
	.has_config_tdp = 1,
	.bclk_freq = BCLK_100MHZ,
	.supported_cstates = CC1 | CC6 | PC3 | PC6,
	.cst_limit = CST_LIMIT_KNL,
	.has_msr_knl_core_c6_residency = 1,
	.trl_msrs = TRL_KNL,
	.rapl_msrs = RAPL_PKG_ALL | RAPL_DRAM_ALL,
	.has_fixed_rapl_unit = 1,
	.need_perf_multiplier = 1,
};

static const struct platform_features default_features = {
};

static const struct platform_features amd_features_with_rapl = {
	.rapl_msrs = RAPL_AMD_F17H,
	.has_per_core_rapl = 1,
	.rapl_quirk_tdp = 280,	/* This is the max stock TDP of HEDT/Server Fam17h+ chips */
};

static const struct platform_data turbostat_pdata[] = {
	{ INTEL_NEHALEM, &nhm_features },
	{ INTEL_NEHALEM_G, &nhm_features },
	{ INTEL_NEHALEM_EP, &nhm_features },
	{ INTEL_NEHALEM_EX, &nhx_features },
	{ INTEL_WESTMERE, &nhm_features },
	{ INTEL_WESTMERE_EP, &nhm_features },
	{ INTEL_WESTMERE_EX, &nhx_features },
	{ INTEL_SANDYBRIDGE, &snb_features },
	{ INTEL_SANDYBRIDGE_X, &snx_features },
	{ INTEL_IVYBRIDGE, &ivb_features },
	{ INTEL_IVYBRIDGE_X, &ivx_features },
	{ INTEL_HASWELL, &hsw_features },
	{ INTEL_HASWELL_X, &hsx_features },
	{ INTEL_HASWELL_L, &hswl_features },
	{ INTEL_HASWELL_G, &hswg_features },
	{ INTEL_BROADWELL, &bdw_features },
	{ INTEL_BROADWELL_G, &bdwg_features },
	{ INTEL_BROADWELL_X, &bdx_features },
	{ INTEL_BROADWELL_D, &bdx_features },
	{ INTEL_SKYLAKE_L, &skl_features },
	{ INTEL_SKYLAKE, &skl_features },
	{ INTEL_SKYLAKE_X, &skx_features },
	{ INTEL_KABYLAKE_L, &skl_features },
	{ INTEL_KABYLAKE, &skl_features },
	{ INTEL_COMETLAKE, &skl_features },
	{ INTEL_COMETLAKE_L, &skl_features },
	{ INTEL_CANNONLAKE_L, &cnl_features },
	{ INTEL_ICELAKE_X, &icx_features },
	{ INTEL_ICELAKE_D, &icx_features },
	{ INTEL_ICELAKE_L, &cnl_features },
	{ INTEL_ICELAKE_NNPI, &cnl_features },
	{ INTEL_ROCKETLAKE, &cnl_features },
	{ INTEL_TIGERLAKE_L, &cnl_features },
	{ INTEL_TIGERLAKE, &cnl_features },
	{ INTEL_SAPPHIRERAPIDS_X, &spr_features },
	{ INTEL_EMERALDRAPIDS_X, &spr_features },
	{ INTEL_GRANITERAPIDS_X, &spr_features },
	{ INTEL_GRANITERAPIDS_D, &spr_features },
	{ INTEL_LAKEFIELD, &cnl_features },
	{ INTEL_ALDERLAKE, &adl_features },
	{ INTEL_ALDERLAKE_L, &adl_features },
	{ INTEL_RAPTORLAKE, &adl_features },
	{ INTEL_RAPTORLAKE_P, &adl_features },
	{ INTEL_RAPTORLAKE_S, &adl_features },
	{ INTEL_METEORLAKE, &adl_features },
	{ INTEL_METEORLAKE_L, &adl_features },
	{ INTEL_ARROWLAKE_H, &adl_features },
	{ INTEL_ARROWLAKE_U, &adl_features },
	{ INTEL_ARROWLAKE, &adl_features },
	{ INTEL_LUNARLAKE_M, &lnl_features },
	{ INTEL_PANTHERLAKE_L, &lnl_features },
	{ INTEL_ATOM_SILVERMONT, &slv_features },
	{ INTEL_ATOM_SILVERMONT_D, &slvd_features },
	{ INTEL_ATOM_AIRMONT, &amt_features },
	{ INTEL_ATOM_GOLDMONT, &gmt_features },
	{ INTEL_ATOM_GOLDMONT_D, &gmtd_features },
	{ INTEL_ATOM_GOLDMONT_PLUS, &gmtp_features },
	{ INTEL_ATOM_TREMONT_D, &tmtd_features },
	{ INTEL_ATOM_TREMONT, &tmt_features },
	{ INTEL_ATOM_TREMONT_L, &tmt_features },
	{ INTEL_ATOM_GRACEMONT, &adl_features },
	{ INTEL_ATOM_CRESTMONT_X, &srf_features },
	{ INTEL_ATOM_CRESTMONT, &grr_features },
	{ INTEL_ATOM_DARKMONT_X, &srf_features },
	{ INTEL_XEON_PHI_KNL, &knl_features },
	{ INTEL_XEON_PHI_KNM, &knl_features },
	/*
	 * Missing support for
	 * INTEL_ICELAKE
	 * INTEL_ATOM_SILVERMONT_MID
	 * INTEL_ATOM_AIRMONT_MID
	 * INTEL_ATOM_AIRMONT_NP
	 */
	{ 0, NULL },
};

static const struct platform_features *platform;

void probe_platform_features(unsigned int family, unsigned int model)
{
	int i;


	if (authentic_amd || hygon_genuine) {
		/* fallback to default features on unsupported models */
		force_load++;
		if (max_extended_level >= 0x80000007) {
			unsigned int eax, ebx, ecx, edx;

			__cpuid(0x80000007, eax, ebx, ecx, edx);
			/* RAPL (Fam 17h+) */
			if ((edx & (1 << 14)) && family >= 0x17)
				platform = &amd_features_with_rapl;
		}
		goto end;
	}

	if (!genuine_intel)
		goto end;

	for (i = 0; turbostat_pdata[i].features; i++) {
		if (VFM_FAMILY(turbostat_pdata[i].vfm) == family && VFM_MODEL(turbostat_pdata[i].vfm) == model) {
			platform = turbostat_pdata[i].features;
			return;
		}
	}

end:
	if (force_load && !platform) {
		fprintf(outf, "Forced to run on unsupported platform!\n");
		platform = &default_features;
	}

	if (platform)
		return;

	fprintf(stderr, "Unsupported platform detected.\n"
		"\tSee RUN THE LATEST VERSION on turbostat(8)\n");
	exit(1);
}

/* Model specific support End */

#define	TJMAX_DEFAULT	100

/* MSRs that are not yet in the kernel-provided header. */
#define MSR_RAPL_PWR_UNIT	0xc0010299
#define MSR_CORE_ENERGY_STAT	0xc001029a
#define MSR_PKG_ENERGY_STAT	0xc001029b

#define MAX(a, b) ((a) > (b) ? (a) : (b))

int backwards_count;
char *progname;

#define CPU_SUBSET_MAXCPUS	1024	/* need to use before probe... */
cpu_set_t *cpu_present_set, *cpu_possible_set, *cpu_effective_set, *cpu_allowed_set, *cpu_affinity_set, *cpu_subset;
size_t cpu_present_setsize, cpu_possible_setsize, cpu_effective_setsize, cpu_allowed_setsize, cpu_affinity_setsize, cpu_subset_size;
#define MAX_ADDED_THREAD_COUNTERS 24
#define MAX_ADDED_CORE_COUNTERS 8
#define MAX_ADDED_PACKAGE_COUNTERS 16
#define PMT_MAX_ADDED_THREAD_COUNTERS 24
#define PMT_MAX_ADDED_CORE_COUNTERS 8
#define PMT_MAX_ADDED_PACKAGE_COUNTERS 16
#define BITMASK_SIZE 32

#define ZERO_ARRAY(arr) (memset(arr, 0, sizeof(arr)) + __must_be_array(arr))

/* Indexes used to map data read from perf and MSRs into global variables */
enum rapl_rci_index {
	RAPL_RCI_INDEX_ENERGY_PKG = 0,
	RAPL_RCI_INDEX_ENERGY_CORES = 1,
	RAPL_RCI_INDEX_DRAM = 2,
	RAPL_RCI_INDEX_GFX = 3,
	RAPL_RCI_INDEX_PKG_PERF_STATUS = 4,
	RAPL_RCI_INDEX_DRAM_PERF_STATUS = 5,
	RAPL_RCI_INDEX_CORE_ENERGY = 6,
	RAPL_RCI_INDEX_ENERGY_PLATFORM = 7,
	NUM_RAPL_COUNTERS,
};

enum rapl_unit {
	RAPL_UNIT_INVALID,
	RAPL_UNIT_JOULES,
	RAPL_UNIT_WATTS,
};

struct rapl_counter_info_t {
	unsigned long long data[NUM_RAPL_COUNTERS];
	enum counter_source source[NUM_RAPL_COUNTERS];
	unsigned long long flags[NUM_RAPL_COUNTERS];
	double scale[NUM_RAPL_COUNTERS];
	enum rapl_unit unit[NUM_RAPL_COUNTERS];
	unsigned long long msr[NUM_RAPL_COUNTERS];
	unsigned long long msr_mask[NUM_RAPL_COUNTERS];
	int msr_shift[NUM_RAPL_COUNTERS];

	int fd_perf;
};

/* struct rapl_counter_info_t for each RAPL domain */
struct rapl_counter_info_t *rapl_counter_info_perdomain;
unsigned int rapl_counter_info_perdomain_size;

#define RAPL_COUNTER_FLAG_PLATFORM_COUNTER (1u << 0)
#define RAPL_COUNTER_FLAG_USE_MSR_SUM (1u << 1)

struct rapl_counter_arch_info {
	int feature_mask;	/* Mask for testing if the counter is supported on host */
	const char *perf_subsys;
	const char *perf_name;
	unsigned long long msr;
	unsigned long long msr_mask;
	int msr_shift;		/* Positive mean shift right, negative mean shift left */
	double *platform_rapl_msr_scale;	/* Scale applied to values read by MSR (platform dependent, filled at runtime) */
	unsigned int rci_index;	/* Maps data from perf counters to global variables */
	unsigned long long bic;
	double compat_scale;	/* Some counters require constant scaling to be in the same range as other, similar ones */
	unsigned long long flags;
};

static const struct rapl_counter_arch_info rapl_counter_arch_infos[] = {
	{
	 .feature_mask = RAPL_PKG,
	 .perf_subsys = "power",
	 .perf_name = "energy-pkg",
	 .msr = MSR_PKG_ENERGY_STATUS,
	 .msr_mask = 0xFFFFFFFFFFFFFFFF,
	 .msr_shift = 0,
	 .platform_rapl_msr_scale = &rapl_energy_units,
	 .rci_index = RAPL_RCI_INDEX_ENERGY_PKG,
	 .bic = BIC_PkgWatt | BIC_Pkg_J,
	 .compat_scale = 1.0,
	 .flags = RAPL_COUNTER_FLAG_USE_MSR_SUM,
	  },
	{
	 .feature_mask = RAPL_AMD_F17H,
	 .perf_subsys = "power",
	 .perf_name = "energy-pkg",
	 .msr = MSR_PKG_ENERGY_STAT,
	 .msr_mask = 0xFFFFFFFFFFFFFFFF,
	 .msr_shift = 0,
	 .platform_rapl_msr_scale = &rapl_energy_units,
	 .rci_index = RAPL_RCI_INDEX_ENERGY_PKG,
	 .bic = BIC_PkgWatt | BIC_Pkg_J,
	 .compat_scale = 1.0,
	 .flags = RAPL_COUNTER_FLAG_USE_MSR_SUM,
	  },
	{
	 .feature_mask = RAPL_CORE_ENERGY_STATUS,
	 .perf_subsys = "power",
	 .perf_name = "energy-cores",
	 .msr = MSR_PP0_ENERGY_STATUS,
	 .msr_mask = 0xFFFFFFFFFFFFFFFF,
	 .msr_shift = 0,
	 .platform_rapl_msr_scale = &rapl_energy_units,
	 .rci_index = RAPL_RCI_INDEX_ENERGY_CORES,
	 .bic = BIC_CorWatt | BIC_Cor_J,
	 .compat_scale = 1.0,
	 .flags = RAPL_COUNTER_FLAG_USE_MSR_SUM,
	  },
	{
	 .feature_mask = RAPL_DRAM,
	 .perf_subsys = "power",
	 .perf_name = "energy-ram",
	 .msr = MSR_DRAM_ENERGY_STATUS,
	 .msr_mask = 0xFFFFFFFFFFFFFFFF,
	 .msr_shift = 0,
	 .platform_rapl_msr_scale = &rapl_dram_energy_units,
	 .rci_index = RAPL_RCI_INDEX_DRAM,
	 .bic = BIC_RAMWatt | BIC_RAM_J,
	 .compat_scale = 1.0,
	 .flags = RAPL_COUNTER_FLAG_USE_MSR_SUM,
	  },
	{
	 .feature_mask = RAPL_GFX,
	 .perf_subsys = "power",
	 .perf_name = "energy-gpu",
	 .msr = MSR_PP1_ENERGY_STATUS,
	 .msr_mask = 0xFFFFFFFFFFFFFFFF,
	 .msr_shift = 0,
	 .platform_rapl_msr_scale = &rapl_energy_units,
	 .rci_index = RAPL_RCI_INDEX_GFX,
	 .bic = BIC_GFXWatt | BIC_GFX_J,
	 .compat_scale = 1.0,
	 .flags = RAPL_COUNTER_FLAG_USE_MSR_SUM,
	  },
	{
	 .feature_mask = RAPL_PKG_PERF_STATUS,
	 .perf_subsys = NULL,
	 .perf_name = NULL,
	 .msr = MSR_PKG_PERF_STATUS,
	 .msr_mask = 0xFFFFFFFFFFFFFFFF,
	 .msr_shift = 0,
	 .platform_rapl_msr_scale = &rapl_time_units,
	 .rci_index = RAPL_RCI_INDEX_PKG_PERF_STATUS,
	 .bic = BIC_PKG__,
	 .compat_scale = 100.0,
	 .flags = RAPL_COUNTER_FLAG_USE_MSR_SUM,
	  },
	{
	 .feature_mask = RAPL_DRAM_PERF_STATUS,
	 .perf_subsys = NULL,
	 .perf_name = NULL,
	 .msr = MSR_DRAM_PERF_STATUS,
	 .msr_mask = 0xFFFFFFFFFFFFFFFF,
	 .msr_shift = 0,
	 .platform_rapl_msr_scale = &rapl_time_units,
	 .rci_index = RAPL_RCI_INDEX_DRAM_PERF_STATUS,
	 .bic = BIC_RAM__,
	 .compat_scale = 100.0,
	 .flags = RAPL_COUNTER_FLAG_USE_MSR_SUM,
	  },
	{
	 .feature_mask = RAPL_AMD_F17H,
	 .perf_subsys = NULL,
	 .perf_name = NULL,
	 .msr = MSR_CORE_ENERGY_STAT,
	 .msr_mask = 0xFFFFFFFF,
	 .msr_shift = 0,
	 .platform_rapl_msr_scale = &rapl_energy_units,
	 .rci_index = RAPL_RCI_INDEX_CORE_ENERGY,
	 .bic = BIC_CorWatt | BIC_Cor_J,
	 .compat_scale = 1.0,
	 .flags = 0,
	  },
	{
	 .feature_mask = RAPL_PSYS,
	 .perf_subsys = "power",
	 .perf_name = "energy-psys",
	 .msr = MSR_PLATFORM_ENERGY_STATUS,
	 .msr_mask = 0x00000000FFFFFFFF,
	 .msr_shift = 0,
	 .platform_rapl_msr_scale = &rapl_psys_energy_units,
	 .rci_index = RAPL_RCI_INDEX_ENERGY_PLATFORM,
	 .bic = BIC_SysWatt | BIC_Sys_J,
	 .compat_scale = 1.0,
	 .flags = RAPL_COUNTER_FLAG_PLATFORM_COUNTER | RAPL_COUNTER_FLAG_USE_MSR_SUM,
	  },
};

struct rapl_counter {
	unsigned long long raw_value;
	enum rapl_unit unit;
	double scale;
};

/* Indexes used to map data read from perf and MSRs into global variables */
enum ccstate_rci_index {
	CCSTATE_RCI_INDEX_C1_RESIDENCY = 0,
	CCSTATE_RCI_INDEX_C3_RESIDENCY = 1,
	CCSTATE_RCI_INDEX_C6_RESIDENCY = 2,
	CCSTATE_RCI_INDEX_C7_RESIDENCY = 3,
	PCSTATE_RCI_INDEX_C2_RESIDENCY = 4,
	PCSTATE_RCI_INDEX_C3_RESIDENCY = 5,
	PCSTATE_RCI_INDEX_C6_RESIDENCY = 6,
	PCSTATE_RCI_INDEX_C7_RESIDENCY = 7,
	PCSTATE_RCI_INDEX_C8_RESIDENCY = 8,
	PCSTATE_RCI_INDEX_C9_RESIDENCY = 9,
	PCSTATE_RCI_INDEX_C10_RESIDENCY = 10,
	NUM_CSTATE_COUNTERS,
};

struct cstate_counter_info_t {
	unsigned long long data[NUM_CSTATE_COUNTERS];
	enum counter_source source[NUM_CSTATE_COUNTERS];
	unsigned long long msr[NUM_CSTATE_COUNTERS];
	int fd_perf_core;
	int fd_perf_pkg;
};

struct cstate_counter_info_t *ccstate_counter_info;
unsigned int ccstate_counter_info_size;

#define CSTATE_COUNTER_FLAG_COLLECT_PER_CORE   (1u << 0)
#define CSTATE_COUNTER_FLAG_COLLECT_PER_THREAD ((1u << 1) | CSTATE_COUNTER_FLAG_COLLECT_PER_CORE)
#define CSTATE_COUNTER_FLAG_SOFT_C1_DEPENDENCY (1u << 2)

struct cstate_counter_arch_info {
	int feature_mask;	/* Mask for testing if the counter is supported on host */
	const char *perf_subsys;
	const char *perf_name;
	unsigned long long msr;
	unsigned int rci_index;	/* Maps data from perf counters to global variables */
	unsigned long long bic;
	unsigned long long flags;
	int pkg_cstate_limit;
};

static struct cstate_counter_arch_info ccstate_counter_arch_infos[] = {
	{
	 .feature_mask = CC1,
	 .perf_subsys = "cstate_core",
	 .perf_name = "c1-residency",
	 .msr = MSR_CORE_C1_RES,
	 .rci_index = CCSTATE_RCI_INDEX_C1_RESIDENCY,
	 .bic = BIC_CPU_c1,
	 .flags = CSTATE_COUNTER_FLAG_COLLECT_PER_THREAD,
	 .pkg_cstate_limit = 0,
	  },
	{
	 .feature_mask = CC3,
	 .perf_subsys = "cstate_core",
	 .perf_name = "c3-residency",
	 .msr = MSR_CORE_C3_RESIDENCY,
	 .rci_index = CCSTATE_RCI_INDEX_C3_RESIDENCY,
	 .bic = BIC_CPU_c3,
	 .flags = CSTATE_COUNTER_FLAG_COLLECT_PER_CORE | CSTATE_COUNTER_FLAG_SOFT_C1_DEPENDENCY,
	 .pkg_cstate_limit = 0,
	  },
	{
	 .feature_mask = CC6,
	 .perf_subsys = "cstate_core",
	 .perf_name = "c6-residency",
	 .msr = MSR_CORE_C6_RESIDENCY,
	 .rci_index = CCSTATE_RCI_INDEX_C6_RESIDENCY,
	 .bic = BIC_CPU_c6,
	 .flags = CSTATE_COUNTER_FLAG_COLLECT_PER_CORE | CSTATE_COUNTER_FLAG_SOFT_C1_DEPENDENCY,
	 .pkg_cstate_limit = 0,
	  },
	{
	 .feature_mask = CC7,
	 .perf_subsys = "cstate_core",
	 .perf_name = "c7-residency",
	 .msr = MSR_CORE_C7_RESIDENCY,
	 .rci_index = CCSTATE_RCI_INDEX_C7_RESIDENCY,
	 .bic = BIC_CPU_c7,
	 .flags = CSTATE_COUNTER_FLAG_COLLECT_PER_CORE | CSTATE_COUNTER_FLAG_SOFT_C1_DEPENDENCY,
	 .pkg_cstate_limit = 0,
	  },
	{
	 .feature_mask = PC2,
	 .perf_subsys = "cstate_pkg",
	 .perf_name = "c2-residency",
	 .msr = MSR_PKG_C2_RESIDENCY,
	 .rci_index = PCSTATE_RCI_INDEX_C2_RESIDENCY,
	 .bic = BIC_Pkgpc2,
	 .flags = 0,
	 .pkg_cstate_limit = PCL__2,
	  },
	{
	 .feature_mask = PC3,
	 .perf_subsys = "cstate_pkg",
	 .perf_name = "c3-residency",
	 .msr = MSR_PKG_C3_RESIDENCY,
	 .rci_index = PCSTATE_RCI_INDEX_C3_RESIDENCY,
	 .bic = BIC_Pkgpc3,
	 .flags = 0,
	 .pkg_cstate_limit = PCL__3,
	  },
	{
	 .feature_mask = PC6,
	 .perf_subsys = "cstate_pkg",
	 .perf_name = "c6-residency",
	 .msr = MSR_PKG_C6_RESIDENCY,
	 .rci_index = PCSTATE_RCI_INDEX_C6_RESIDENCY,
	 .bic = BIC_Pkgpc6,
	 .flags = 0,
	 .pkg_cstate_limit = PCL__6,
	  },
	{
	 .feature_mask = PC7,
	 .perf_subsys = "cstate_pkg",
	 .perf_name = "c7-residency",
	 .msr = MSR_PKG_C7_RESIDENCY,
	 .rci_index = PCSTATE_RCI_INDEX_C7_RESIDENCY,
	 .bic = BIC_Pkgpc7,
	 .flags = 0,
	 .pkg_cstate_limit = PCL__7,
	  },
	{
	 .feature_mask = PC8,
	 .perf_subsys = "cstate_pkg",
	 .perf_name = "c8-residency",
	 .msr = MSR_PKG_C8_RESIDENCY,
	 .rci_index = PCSTATE_RCI_INDEX_C8_RESIDENCY,
	 .bic = BIC_Pkgpc8,
	 .flags = 0,
	 .pkg_cstate_limit = PCL__8,
	  },
	{
	 .feature_mask = PC9,
	 .perf_subsys = "cstate_pkg",
	 .perf_name = "c9-residency",
	 .msr = MSR_PKG_C9_RESIDENCY,
	 .rci_index = PCSTATE_RCI_INDEX_C9_RESIDENCY,
	 .bic = BIC_Pkgpc9,
	 .flags = 0,
	 .pkg_cstate_limit = PCL__9,
	  },
	{
	 .feature_mask = PC10,
	 .perf_subsys = "cstate_pkg",
	 .perf_name = "c10-residency",
	 .msr = MSR_PKG_C10_RESIDENCY,
	 .rci_index = PCSTATE_RCI_INDEX_C10_RESIDENCY,
	 .bic = BIC_Pkgpc10,
	 .flags = 0,
	 .pkg_cstate_limit = PCL_10,
	  },
};

/* Indexes used to map data read from perf and MSRs into global variables */
enum msr_rci_index {
	MSR_RCI_INDEX_APERF = 0,
	MSR_RCI_INDEX_MPERF = 1,
	MSR_RCI_INDEX_SMI = 2,
	NUM_MSR_COUNTERS,
};

struct msr_counter_info_t {
	unsigned long long data[NUM_MSR_COUNTERS];
	enum counter_source source[NUM_MSR_COUNTERS];
	unsigned long long msr[NUM_MSR_COUNTERS];
	unsigned long long msr_mask[NUM_MSR_COUNTERS];
	int fd_perf;
};

struct msr_counter_info_t *msr_counter_info;
unsigned int msr_counter_info_size;

struct msr_counter_arch_info {
	const char *perf_subsys;
	const char *perf_name;
	unsigned long long msr;
	unsigned long long msr_mask;
	unsigned int rci_index;	/* Maps data from perf counters to global variables */
	bool needed;
	bool present;
};

enum msr_arch_info_index {
	MSR_ARCH_INFO_APERF_INDEX = 0,
	MSR_ARCH_INFO_MPERF_INDEX = 1,
	MSR_ARCH_INFO_SMI_INDEX = 2,
};

static struct msr_counter_arch_info msr_counter_arch_infos[] = {
	[MSR_ARCH_INFO_APERF_INDEX] = {
				       .perf_subsys = "msr",
				       .perf_name = "aperf",
				       .msr = MSR_IA32_APERF,
				       .msr_mask = 0xFFFFFFFFFFFFFFFF,
				       .rci_index = MSR_RCI_INDEX_APERF,
				        },

	[MSR_ARCH_INFO_MPERF_INDEX] = {
				       .perf_subsys = "msr",
				       .perf_name = "mperf",
				       .msr = MSR_IA32_MPERF,
				       .msr_mask = 0xFFFFFFFFFFFFFFFF,
				       .rci_index = MSR_RCI_INDEX_MPERF,
				        },

	[MSR_ARCH_INFO_SMI_INDEX] = {
				     .perf_subsys = "msr",
				     .perf_name = "smi",
				     .msr = MSR_SMI_COUNT,
				     .msr_mask = 0xFFFFFFFF,
				     .rci_index = MSR_RCI_INDEX_SMI,
				      },
};

/* Can be redefined when compiling, useful for testing. */
#ifndef SYSFS_TELEM_PATH
#define SYSFS_TELEM_PATH "/sys/class/intel_pmt"
#endif

#define PMT_COUNTER_MTL_DC6_OFFSET 120
#define PMT_COUNTER_MTL_DC6_LSB    0
#define PMT_COUNTER_MTL_DC6_MSB    63
#define PMT_MTL_DC6_GUID           0x1a067102
#define PMT_MTL_DC6_SEQ            0

#define PMT_COUNTER_CWF_MC1E_OFFSET_BASE          20936
#define PMT_COUNTER_CWF_MC1E_OFFSET_INCREMENT     24
#define PMT_COUNTER_CWF_MC1E_NUM_MODULES_PER_FILE 12
#define PMT_COUNTER_CWF_CPUS_PER_MODULE           4
#define PMT_COUNTER_CWF_MC1E_LSB                  0
#define PMT_COUNTER_CWF_MC1E_MSB                  63
#define PMT_CWF_MC1E_GUID                         0x14421519

unsigned long long tcore_clock_freq_hz = 800000000;

#define PMT_COUNTER_NAME_SIZE_BYTES      16
#define PMT_COUNTER_TYPE_NAME_SIZE_BYTES 32

struct pmt_mmio {
	struct pmt_mmio *next;

	unsigned int guid;
	unsigned int size;

	/* Base pointer to the mmaped memory. */
	void *mmio_base;

	/*
	 * Offset to be applied to the mmio_base
	 * to get the beginning of the PMT counters for given GUID.
	 */
	unsigned long pmt_offset;
} *pmt_mmios;

enum pmt_datatype {
	PMT_TYPE_RAW,
	PMT_TYPE_XTAL_TIME,
	PMT_TYPE_TCORE_CLOCK,
};

struct pmt_domain_info {
	/*
	 * Pointer to the MMIO obtained by applying a counter offset
	 * to the mmio_base of the mmaped region for the given GUID.
	 *
	 * This is where to read the raw value of the counter from.
	 */
	unsigned long *pcounter;
};

struct pmt_counter {
	struct pmt_counter *next;

	/* PMT metadata */
	char name[PMT_COUNTER_NAME_SIZE_BYTES];
	enum pmt_datatype type;
	enum counter_scope scope;
	unsigned int lsb;
	unsigned int msb;

	/* BIC-like metadata */
	enum counter_format format;

	unsigned int num_domains;
	struct pmt_domain_info *domains;
};

/*
 * PMT telemetry directory iterator.
 * Used to iterate telemetry files in sysfs in correct order.
 */
struct pmt_diriter_t {
	DIR *dir;
	struct dirent **namelist;
	unsigned int num_names;
	unsigned int current_name_idx;
};

int pmt_telemdir_filter(const struct dirent *e)
{
	unsigned int dummy;

	return sscanf(e->d_name, "telem%u", &dummy);
}

int pmt_telemdir_sort(const struct dirent **a, const struct dirent **b)
{
	unsigned int aidx = 0, bidx = 0;

	sscanf((*a)->d_name, "telem%u", &aidx);
	sscanf((*b)->d_name, "telem%u", &bidx);

	return aidx >= bidx;
}

const struct dirent *pmt_diriter_next(struct pmt_diriter_t *iter)
{
	const struct dirent *ret = NULL;

	if (!iter->dir)
		return NULL;

	if (iter->current_name_idx >= iter->num_names)
		return NULL;

	ret = iter->namelist[iter->current_name_idx];
	++iter->current_name_idx;

	return ret;
}

const struct dirent *pmt_diriter_begin(struct pmt_diriter_t *iter, const char *pmt_root_path)
{
	int num_names = iter->num_names;

	if (!iter->dir) {
		iter->dir = opendir(pmt_root_path);
		if (iter->dir == NULL)
			return NULL;

		num_names = scandir(pmt_root_path, &iter->namelist, pmt_telemdir_filter, pmt_telemdir_sort);
		if (num_names == -1)
			return NULL;
	}

	iter->current_name_idx = 0;
	iter->num_names = num_names;

	return pmt_diriter_next(iter);
}

void pmt_diriter_init(struct pmt_diriter_t *iter)
{
	memset(iter, 0, sizeof(*iter));
}

void pmt_diriter_remove(struct pmt_diriter_t *iter)
{
	if (iter->namelist) {
		for (unsigned int i = 0; i < iter->num_names; i++) {
			free(iter->namelist[i]);
			iter->namelist[i] = NULL;
		}
	}

	free(iter->namelist);
	iter->namelist = NULL;
	iter->num_names = 0;
	iter->current_name_idx = 0;

	closedir(iter->dir);
	iter->dir = NULL;
}

unsigned int pmt_counter_get_width(const struct pmt_counter *p)
{
	return (p->msb - p->lsb) + 1;
}

void pmt_counter_resize_(struct pmt_counter *pcounter, unsigned int new_size)
{
	struct pmt_domain_info *new_mem;

	new_mem = (struct pmt_domain_info *)reallocarray(pcounter->domains, new_size, sizeof(*pcounter->domains));
	if (!new_mem) {
		fprintf(stderr, "%s: failed to allocate memory for PMT counters\n", __func__);
		exit(1);
	}

	/* Zero initialize just allocated memory. */
	const size_t num_new_domains = new_size - pcounter->num_domains;

	memset(&new_mem[pcounter->num_domains], 0, num_new_domains * sizeof(*pcounter->domains));

	pcounter->num_domains = new_size;
	pcounter->domains = new_mem;
}

void pmt_counter_resize(struct pmt_counter *pcounter, unsigned int new_size)
{
	/*
	 * Allocate more memory ahead of time.
	 *
	 * Always allocate space for at least 8 elements
	 * and double the size when growing.
	 */
	if (new_size < 8)
		new_size = 8;
	new_size = MAX(new_size, pcounter->num_domains * 2);

	pmt_counter_resize_(pcounter, new_size);
}

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
	unsigned long long nmi_count;
	unsigned int smi_count;
	unsigned int cpu_id;
	unsigned int apic_id;
	unsigned int x2apic_id;
	unsigned int flags;
	bool is_atom;
	unsigned long long counter[MAX_ADDED_THREAD_COUNTERS];
	unsigned long long perf_counter[MAX_ADDED_THREAD_COUNTERS];
	unsigned long long pmt_counter[PMT_MAX_ADDED_THREAD_COUNTERS];
} *thread_even, *thread_odd;

struct core_data {
	int base_cpu;
	unsigned long long c3;
	unsigned long long c6;
	unsigned long long c7;
	unsigned long long mc6_us;	/* duplicate as per-core for now, even though per module */
	unsigned int core_temp_c;
	struct rapl_counter core_energy;	/* MSR_CORE_ENERGY_STAT */
	unsigned int core_id;
	unsigned long long core_throt_cnt;
	unsigned long long counter[MAX_ADDED_CORE_COUNTERS];
	unsigned long long perf_counter[MAX_ADDED_CORE_COUNTERS];
	unsigned long long pmt_counter[PMT_MAX_ADDED_CORE_COUNTERS];
} *core_even, *core_odd;

struct pkg_data {
	int base_cpu;
	unsigned long long pc2;
	unsigned long long pc3;
	unsigned long long pc6;
	unsigned long long pc7;
	unsigned long long pc8;
	unsigned long long pc9;
	unsigned long long pc10;
	long long cpu_lpi;
	long long sys_lpi;
	unsigned long long pkg_wtd_core_c0;
	unsigned long long pkg_any_core_c0;
	unsigned long long pkg_any_gfxe_c0;
	unsigned long long pkg_both_core_gfxe_c0;
	long long gfx_rc6_ms;
	unsigned int gfx_mhz;
	unsigned int gfx_act_mhz;
	long long sam_mc6_ms;
	unsigned int sam_mhz;
	unsigned int sam_act_mhz;
	unsigned int package_id;
	struct rapl_counter energy_pkg;	/* MSR_PKG_ENERGY_STATUS */
	struct rapl_counter energy_dram;	/* MSR_DRAM_ENERGY_STATUS */
	struct rapl_counter energy_cores;	/* MSR_PP0_ENERGY_STATUS */
	struct rapl_counter energy_gfx;	/* MSR_PP1_ENERGY_STATUS */
	struct rapl_counter rapl_pkg_perf_status;	/* MSR_PKG_PERF_STATUS */
	struct rapl_counter rapl_dram_perf_status;	/* MSR_DRAM_PERF_STATUS */
	unsigned int pkg_temp_c;
	unsigned int uncore_mhz;
	unsigned long long die_c6;
	unsigned long long counter[MAX_ADDED_PACKAGE_COUNTERS];
	unsigned long long perf_counter[MAX_ADDED_PACKAGE_COUNTERS];
	unsigned long long pmt_counter[PMT_MAX_ADDED_PACKAGE_COUNTERS];
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
	IDX_PSYS_ENERGY,
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
		if (platform->rapl_msrs & RAPL_AMD_F17H)
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
	case IDX_PSYS_ENERGY:
		offset = MSR_PLATFORM_ENERGY_STATUS;
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
	case MSR_PLATFORM_ENERGY_STATUS:
		idx = IDX_PSYS_ENERGY;
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
		return platform->rapl_msrs & (RAPL_PKG | RAPL_AMD_F17H);
	case IDX_DRAM_ENERGY:
		return platform->rapl_msrs & RAPL_DRAM;
	case IDX_PP0_ENERGY:
		return platform->rapl_msrs & RAPL_CORE_ENERGY_STATUS;
	case IDX_PP1_ENERGY:
		return platform->rapl_msrs & RAPL_GFX;
	case IDX_PKG_PERF:
		return platform->rapl_msrs & RAPL_PKG_PERF_STATUS;
	case IDX_DRAM_PERF:
		return platform->rapl_msrs & RAPL_DRAM_PERF_STATUS;
	case IDX_PSYS_ENERGY:
		return platform->rapl_msrs & RAPL_PSYS;
	default:
		return 0;
	}
}

struct sys_counters {
	/* MSR added counters */
	unsigned int added_thread_counters;
	unsigned int added_core_counters;
	unsigned int added_package_counters;
	struct msr_counter *tp;
	struct msr_counter *cp;
	struct msr_counter *pp;

	/* perf added counters */
	unsigned int added_thread_perf_counters;
	unsigned int added_core_perf_counters;
	unsigned int added_package_perf_counters;
	struct perf_counter_info *perf_tp;
	struct perf_counter_info *perf_cp;
	struct perf_counter_info *perf_pp;

	struct pmt_counter *pmt_tp;
	struct pmt_counter *pmt_cp;
	struct pmt_counter *pmt_pp;
} sys;

static size_t free_msr_counters_(struct msr_counter **pp)
{
	struct msr_counter *p = NULL;
	size_t num_freed = 0;

	while (*pp) {
		p = *pp;

		if (p->msr_num != 0) {
			*pp = p->next;

			free(p);
			++num_freed;

			continue;
		}

		pp = &p->next;
	}

	return num_freed;
}

/*
 * Free all added counters accessed via msr.
 */
static void free_sys_msr_counters(void)
{
	/* Thread counters */
	sys.added_thread_counters -= free_msr_counters_(&sys.tp);

	/* Core counters */
	sys.added_core_counters -= free_msr_counters_(&sys.cp);

	/* Package counters */
	sys.added_package_counters -= free_msr_counters_(&sys.pp);
}

struct system_summary {
	struct thread_data threads;
	struct core_data cores;
	struct pkg_data packages;
} average;

struct platform_counters {
	struct rapl_counter energy_psys;	/* MSR_PLATFORM_ENERGY_STATUS */
} platform_counters_odd, platform_counters_even;

struct cpu_topology {
	int physical_package_id;
	int die_id;
	int logical_cpu_id;
	int physical_node_id;
	int logical_node_id;	/* 0-based count within the package */
	int physical_core_id;
	int thread_id;
	int type;
	cpu_set_t *put_ids;	/* Processing Unit/Thread IDs */
} *cpus;

struct topo_params {
	int num_packages;
	int num_die;
	int num_cpus;
	int num_cores;
	int allowed_packages;
	int allowed_cpus;
	int allowed_cores;
	int max_cpu_num;
	int max_core_id;
	int max_package_id;
	int max_die_id;
	int max_node_num;
	int nodes_per_pkg;
	int cores_per_node;
	int threads_per_core;
} topo;

struct timeval tv_even, tv_odd, tv_delta;

int *irq_column_2_cpu;		/* /proc/interrupts column numbers */
int *irqs_per_cpu;		/* indexed by cpu_num */
int *nmi_per_cpu;		/* indexed by cpu_num */

void setup_all_buffers(bool startup);

char *sys_lpi_file;
char *sys_lpi_file_sysfs = "/sys/devices/system/cpu/cpuidle/low_power_idle_system_residency_us";
char *sys_lpi_file_debugfs = "/sys/kernel/debug/pmc_core/slp_s0_residency_usec";

int cpu_is_not_present(int cpu)
{
	return !CPU_ISSET_S(cpu, cpu_present_setsize, cpu_present_set);
}

int cpu_is_not_allowed(int cpu)
{
	return !CPU_ISSET_S(cpu, cpu_allowed_setsize, cpu_allowed_set);
}

/*
 * run func(thread, core, package) in topology order
 * skip non-present cpus
 */

int for_all_cpus(int (func) (struct thread_data *, struct core_data *, struct pkg_data *),
		 struct thread_data *thread_base, struct core_data *core_base, struct pkg_data *pkg_base)
{
	int retval, pkg_no, core_no, thread_no, node_no;

	retval = 0;

	for (pkg_no = 0; pkg_no < topo.num_packages; ++pkg_no) {
		for (node_no = 0; node_no < topo.nodes_per_pkg; node_no++) {
			for (core_no = 0; core_no < topo.cores_per_node; ++core_no) {
				for (thread_no = 0; thread_no < topo.threads_per_core; ++thread_no) {
					struct thread_data *t;
					struct core_data *c;
					struct pkg_data *p;
					t = GET_THREAD(thread_base, thread_no, core_no, node_no, pkg_no);

					if (cpu_is_not_allowed(t->cpu_id))
						continue;

					c = GET_CORE(core_base, core_no, node_no, pkg_no);
					p = GET_PKG(pkg_base, pkg_no);

					retval |= func(t, c, p);
				}
			}
		}
	}
	return retval;
}

int is_cpu_first_thread_in_core(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	UNUSED(p);

	return ((int)t->cpu_id == c->base_cpu || c->base_cpu < 0);
}

int is_cpu_first_core_in_package(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	UNUSED(c);

	return ((int)t->cpu_id == p->base_cpu || p->base_cpu < 0);
}

int is_cpu_first_thread_in_package(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	return is_cpu_first_thread_in_core(t, c, p) && is_cpu_first_core_in_package(t, c, p);
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
		err(-1, "%s open failed, try chown or chmod +r /dev/cpu/*/msr, "
		    "or run with --no-msr, or run as root", pathname);

	fd_percpu[cpu] = fd;

	return fd;
}

static void bic_disable_msr_access(void)
{
	const unsigned long bic_msrs = BIC_Mod_c6 | BIC_CoreTmp |
	    BIC_Totl_c0 | BIC_Any_c0 | BIC_GFX_c0 | BIC_CPUGFX | BIC_PkgTmp;

	bic_enabled &= ~bic_msrs;

	free_sys_msr_counters();
}

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
	assert(!no_perf);

	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static long open_perf_counter(int cpu, unsigned int type, unsigned int config, int group_fd, __u64 read_format)
{
	struct perf_event_attr attr;
	const pid_t pid = -1;
	const unsigned long flags = 0;

	assert(!no_perf);

	memset(&attr, 0, sizeof(struct perf_event_attr));

	attr.type = type;
	attr.size = sizeof(struct perf_event_attr);
	attr.config = config;
	attr.disabled = 0;
	attr.sample_type = PERF_SAMPLE_IDENTIFIER;
	attr.read_format = read_format;

	const int fd = perf_event_open(&attr, pid, cpu, group_fd, flags);

	return fd;
}

int get_instr_count_fd(int cpu)
{
	if (fd_instr_count_percpu[cpu])
		return fd_instr_count_percpu[cpu];

	fd_instr_count_percpu[cpu] = open_perf_counter(cpu, PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, -1, 0);

	return fd_instr_count_percpu[cpu];
}

int get_msr(int cpu, off_t offset, unsigned long long *msr)
{
	ssize_t retval;

	assert(!no_msr);

	retval = pread(get_msr_fd(cpu), msr, sizeof(*msr), offset);

	if (retval != sizeof *msr)
		err(-1, "cpu%d: msr offset 0x%llx read failed", cpu, (unsigned long long)offset);

	return 0;
}

int probe_msr(int cpu, off_t offset)
{
	ssize_t retval;
	unsigned long long value;

	assert(!no_msr);

	retval = pread(get_msr_fd(cpu), &value, sizeof(value), offset);

	/*
	 * Expect MSRs to accumulate some non-zero value since the system was powered on.
	 * Treat zero as a read failure.
	 */
	if (retval != sizeof(value) || value == 0)
		return 1;

	return 0;
}

/* Convert CPU ID to domain ID for given added perf counter. */
unsigned int cpu_to_domain(const struct perf_counter_info *pc, int cpu)
{
	switch (pc->scope) {
	case SCOPE_CPU:
		return cpu;

	case SCOPE_CORE:
		return cpus[cpu].physical_core_id;

	case SCOPE_PACKAGE:
		return cpus[cpu].physical_package_id;
	}

	__builtin_unreachable();
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
		"  -a, --add counter\n"
		"		add a counter\n"
		"		  eg. --add msr0x10,u64,cpu,delta,MY_TSC\n"
		"		  eg. --add perf/cstate_pkg/c2-residency,package,delta,percent,perfPC2\n"
		"		  eg. --add pmt,name=XTAL,type=raw,domain=package0,offset=0,lsb=0,msb=63,guid=0x1a067102\n"
		"  -c, --cpu cpu-set\n"
		"		limit output to summary plus cpu-set:\n"
		"		  {core | package | j,k,l..m,n-p }\n"
		"  -d, --debug\n"
		"		displays usec, Time_Of_Day_Seconds and more debugging\n"
		"		debug messages are printed to stderr\n"
		"  -D, --Dump\n"
		"		displays the raw counter values\n"
		"  -e, --enable [all | column]\n"
		"		shows all or the specified disabled column\n"
		"  -f, --force\n"
		"		force load turbostat with minimum default features on unsupported platforms.\n"
		"  -H, --hide [column | column,column,...]\n"
		"		hide the specified column(s)\n"
		"  -i, --interval sec.subsec\n"
		"		override default 5-second measurement interval\n"
		"  -J, --Joules\n"
		"		displays energy in Joules instead of Watts\n"
		"  -l, --list\n"
		"		list column headers only\n"
		"  -M, --no-msr\n"
		"		disable all uses of the MSR driver\n"
		"  -P, --no-perf\n"
		"		disable all uses of the perf API\n"
		"  -n, --num_iterations num\n"
		"		number of the measurement iterations\n"
		"  -N, --header_iterations num\n"
		"		print header every num iterations\n"
		"  -o, --out file\n"
		"		create or truncate \"file\" for all output\n"
		"  -q, --quiet\n"
		"		skip decoding system configuration header\n"
		"  -s, --show [column | column,column,...]\n"
		"		show only the specified column(s)\n"
		"  -S, --Summary\n"
		"		limits output to 1-line system summary per interval\n"
		"  -T, --TCC temperature\n"
		"		sets the Thermal Control Circuit temperature in\n"
		"		  degrees Celsius\n"
		"  -h, --help\n"
		"		print this help message\n"
		"  -v, --version\n"
		"		print version information\n\nFor more help, run \"man turbostat\"\n");
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
	struct perf_counter_info *pp;
	struct pmt_counter *ppmt;
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
	if (DO_BIC(BIC_NMI)) {
		if (sums_need_wide_columns)
			outp += sprintf(outp, "%s     NMI", (printed++ ? delim : ""));
		else
			outp += sprintf(outp, "%sNMI", (printed++ ? delim : ""));
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

	for (pp = sys.perf_tp; pp; pp = pp->next) {

		if (pp->format == FORMAT_RAW) {
			if (pp->width == 64)
				outp += sprintf(outp, "%s%18.18s", (printed++ ? delim : ""), pp->name);
			else
				outp += sprintf(outp, "%s%10.10s", (printed++ ? delim : ""), pp->name);
		} else {
			if ((pp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8s", (printed++ ? delim : ""), pp->name);
			else
				outp += sprintf(outp, "%s%s", (printed++ ? delim : ""), pp->name);
		}
	}

	ppmt = sys.pmt_tp;
	while (ppmt) {
		switch (ppmt->type) {
		case PMT_TYPE_RAW:
			if (pmt_counter_get_width(ppmt) <= 32)
				outp += sprintf(outp, "%s%10.10s", (printed++ ? delim : ""), ppmt->name);
			else
				outp += sprintf(outp, "%s%18.18s", (printed++ ? delim : ""), ppmt->name);

			break;

		case PMT_TYPE_XTAL_TIME:
		case PMT_TYPE_TCORE_CLOCK:
			outp += sprintf(outp, "%s%s", (printed++ ? delim : ""), ppmt->name);
			break;
		}

		ppmt = ppmt->next;
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

	if (platform->rapl_msrs && !rapl_joules) {
		if (DO_BIC(BIC_CorWatt) && platform->has_per_core_rapl)
			outp += sprintf(outp, "%sCorWatt", (printed++ ? delim : ""));
	} else if (platform->rapl_msrs && rapl_joules) {
		if (DO_BIC(BIC_Cor_J) && platform->has_per_core_rapl)
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

	for (pp = sys.perf_cp; pp; pp = pp->next) {

		if (pp->format == FORMAT_RAW) {
			if (pp->width == 64)
				outp += sprintf(outp, "%s%18.18s", (printed++ ? delim : ""), pp->name);
			else
				outp += sprintf(outp, "%s%10.10s", (printed++ ? delim : ""), pp->name);
		} else {
			if ((pp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8s", (printed++ ? delim : ""), pp->name);
			else
				outp += sprintf(outp, "%s%s", (printed++ ? delim : ""), pp->name);
		}
	}

	ppmt = sys.pmt_cp;
	while (ppmt) {
		switch (ppmt->type) {
		case PMT_TYPE_RAW:
			if (pmt_counter_get_width(ppmt) <= 32)
				outp += sprintf(outp, "%s%10.10s", (printed++ ? delim : ""), ppmt->name);
			else
				outp += sprintf(outp, "%s%18.18s", (printed++ ? delim : ""), ppmt->name);

			break;

		case PMT_TYPE_XTAL_TIME:
		case PMT_TYPE_TCORE_CLOCK:
			outp += sprintf(outp, "%s%s", (printed++ ? delim : ""), ppmt->name);
			break;
		}

		ppmt = ppmt->next;
	}

	if (DO_BIC(BIC_PkgTmp))
		outp += sprintf(outp, "%sPkgTmp", (printed++ ? delim : ""));

	if (DO_BIC(BIC_GFX_rc6))
		outp += sprintf(outp, "%sGFX%%rc6", (printed++ ? delim : ""));

	if (DO_BIC(BIC_GFXMHz))
		outp += sprintf(outp, "%sGFXMHz", (printed++ ? delim : ""));

	if (DO_BIC(BIC_GFXACTMHz))
		outp += sprintf(outp, "%sGFXAMHz", (printed++ ? delim : ""));

	if (DO_BIC(BIC_SAM_mc6))
		outp += sprintf(outp, "%sSAM%%mc6", (printed++ ? delim : ""));

	if (DO_BIC(BIC_SAMMHz))
		outp += sprintf(outp, "%sSAMMHz", (printed++ ? delim : ""));

	if (DO_BIC(BIC_SAMACTMHz))
		outp += sprintf(outp, "%sSAMAMHz", (printed++ ? delim : ""));

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
	if (DO_BIC(BIC_Diec6))
		outp += sprintf(outp, "%sDie%%c6", (printed++ ? delim : ""));
	if (DO_BIC(BIC_CPU_LPI))
		outp += sprintf(outp, "%sCPU%%LPI", (printed++ ? delim : ""));
	if (DO_BIC(BIC_SYS_LPI))
		outp += sprintf(outp, "%sSYS%%LPI", (printed++ ? delim : ""));

	if (platform->rapl_msrs && !rapl_joules) {
		if (DO_BIC(BIC_PkgWatt))
			outp += sprintf(outp, "%sPkgWatt", (printed++ ? delim : ""));
		if (DO_BIC(BIC_CorWatt) && !platform->has_per_core_rapl)
			outp += sprintf(outp, "%sCorWatt", (printed++ ? delim : ""));
		if (DO_BIC(BIC_GFXWatt))
			outp += sprintf(outp, "%sGFXWatt", (printed++ ? delim : ""));
		if (DO_BIC(BIC_RAMWatt))
			outp += sprintf(outp, "%sRAMWatt", (printed++ ? delim : ""));
		if (DO_BIC(BIC_PKG__))
			outp += sprintf(outp, "%sPKG_%%", (printed++ ? delim : ""));
		if (DO_BIC(BIC_RAM__))
			outp += sprintf(outp, "%sRAM_%%", (printed++ ? delim : ""));
	} else if (platform->rapl_msrs && rapl_joules) {
		if (DO_BIC(BIC_Pkg_J))
			outp += sprintf(outp, "%sPkg_J", (printed++ ? delim : ""));
		if (DO_BIC(BIC_Cor_J) && !platform->has_per_core_rapl)
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
			else if (mp->width == 32)
				outp += sprintf(outp, "%s%10.10s", delim, mp->name);
			else
				outp += sprintf(outp, "%s%7.7s", delim, mp->name);
		} else {
			if ((mp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8s", delim, mp->name);
			else
				outp += sprintf(outp, "%s%7.7s", delim, mp->name);
		}
	}

	for (pp = sys.perf_pp; pp; pp = pp->next) {

		if (pp->format == FORMAT_RAW) {
			if (pp->width == 64)
				outp += sprintf(outp, "%s%18.18s", (printed++ ? delim : ""), pp->name);
			else
				outp += sprintf(outp, "%s%10.10s", (printed++ ? delim : ""), pp->name);
		} else {
			if ((pp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8s", (printed++ ? delim : ""), pp->name);
			else
				outp += sprintf(outp, "%s%s", (printed++ ? delim : ""), pp->name);
		}
	}

	ppmt = sys.pmt_pp;
	while (ppmt) {
		switch (ppmt->type) {
		case PMT_TYPE_RAW:
			if (pmt_counter_get_width(ppmt) <= 32)
				outp += sprintf(outp, "%s%10.10s", (printed++ ? delim : ""), ppmt->name);
			else
				outp += sprintf(outp, "%s%18.18s", (printed++ ? delim : ""), ppmt->name);

			break;

		case PMT_TYPE_XTAL_TIME:
		case PMT_TYPE_TCORE_CLOCK:
			outp += sprintf(outp, "%s%s", (printed++ ? delim : ""), ppmt->name);
			break;
		}

		ppmt = ppmt->next;
	}

	if (DO_BIC(BIC_SysWatt))
		outp += sprintf(outp, "%sSysWatt", (printed++ ? delim : ""));
	if (DO_BIC(BIC_Sys_J))
		outp += sprintf(outp, "%sSys_J", (printed++ ? delim : ""));

	outp += sprintf(outp, "\n");
}

int dump_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	int i;
	struct msr_counter *mp;
	struct platform_counters *pplat_cnt = p == package_odd ? &platform_counters_odd : &platform_counters_even;

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
		if (DO_BIC(BIC_NMI))
			outp += sprintf(outp, "IRQ: %lld\n", t->nmi_count);
		if (DO_BIC(BIC_SMI))
			outp += sprintf(outp, "SMI: %d\n", t->smi_count);

		for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
			outp +=
			    sprintf(outp, "tADDED [%d] %8s msr0x%x: %08llX %s\n", i, mp->name, mp->msr_num,
				    t->counter[i], mp->sp->path);
		}
	}

	if (c && is_cpu_first_thread_in_core(t, c, p)) {
		outp += sprintf(outp, "core: %d\n", c->core_id);
		outp += sprintf(outp, "c3: %016llX\n", c->c3);
		outp += sprintf(outp, "c6: %016llX\n", c->c6);
		outp += sprintf(outp, "c7: %016llX\n", c->c7);
		outp += sprintf(outp, "DTS: %dC\n", c->core_temp_c);
		outp += sprintf(outp, "cpu_throt_count: %016llX\n", c->core_throt_cnt);

		const unsigned long long energy_value = c->core_energy.raw_value * c->core_energy.scale;
		const double energy_scale = c->core_energy.scale;

		if (c->core_energy.unit == RAPL_UNIT_JOULES)
			outp += sprintf(outp, "Joules: %0llX (scale: %lf)\n", energy_value, energy_scale);

		for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
			outp +=
			    sprintf(outp, "cADDED [%d] %8s msr0x%x: %08llX %s\n", i, mp->name, mp->msr_num,
				    c->counter[i], mp->sp->path);
		}
		outp += sprintf(outp, "mc6_us: %016llX\n", c->mc6_us);
	}

	if (p && is_cpu_first_core_in_package(t, c, p)) {
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
		outp += sprintf(outp, "Joules PKG: %0llX\n", p->energy_pkg.raw_value);
		outp += sprintf(outp, "Joules COR: %0llX\n", p->energy_cores.raw_value);
		outp += sprintf(outp, "Joules GFX: %0llX\n", p->energy_gfx.raw_value);
		outp += sprintf(outp, "Joules RAM: %0llX\n", p->energy_dram.raw_value);
		outp += sprintf(outp, "Joules PSYS: %0llX\n", pplat_cnt->energy_psys.raw_value);
		outp += sprintf(outp, "Throttle PKG: %0llX\n", p->rapl_pkg_perf_status.raw_value);
		outp += sprintf(outp, "Throttle RAM: %0llX\n", p->rapl_dram_perf_status.raw_value);
		outp += sprintf(outp, "PTM: %dC\n", p->pkg_temp_c);

		for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
			outp +=
			    sprintf(outp, "pADDED [%d] %8s msr0x%x: %08llX %s\n", i, mp->name, mp->msr_num,
				    p->counter[i], mp->sp->path);
		}
	}

	outp += sprintf(outp, "\n");

	return 0;
}

double rapl_counter_get_value(const struct rapl_counter *c, enum rapl_unit desired_unit, double interval)
{
	assert(desired_unit != RAPL_UNIT_INVALID);

	/*
	 * For now we don't expect anything other than joules,
	 * so just simplify the logic.
	 */
	assert(c->unit == RAPL_UNIT_JOULES);

	const double scaled = c->raw_value * c->scale;

	if (desired_unit == RAPL_UNIT_WATTS)
		return scaled / interval;
	return scaled;
}

/*
 * column formatting convention & formats
 */
int format_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	static int count;

	struct platform_counters *pplat_cnt = NULL;
	double interval_float, tsc;
	char *fmt8;
	int i;
	struct msr_counter *mp;
	struct perf_counter_info *pp;
	struct pmt_counter *ppmt;
	char *delim = "\t";
	int printed = 0;

	if (t == &average.threads) {
		pplat_cnt = count & 1 ? &platform_counters_odd : &platform_counters_even;
		++count;
	}

	/* if showing only 1st thread in core and this isn't one, bail out */
	if (show_core_only && !is_cpu_first_thread_in_core(t, c, p))
		return 0;

	/* if showing only 1st thread in pkg and this isn't one, bail out */
	if (show_pkg_only && !is_cpu_first_core_in_package(t, c, p))
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

	/* NMI */
	if (DO_BIC(BIC_NMI)) {
		if (sums_need_wide_columns)
			outp += sprintf(outp, "%s%8lld", (printed++ ? delim : ""), t->nmi_count);
		else
			outp += sprintf(outp, "%s%lld", (printed++ ? delim : ""), t->nmi_count);
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

	/* Added perf counters */
	for (i = 0, pp = sys.perf_tp; pp; ++i, pp = pp->next) {
		if (pp->format == FORMAT_RAW) {
			if (pp->width == 32)
				outp +=
				    sprintf(outp, "%s0x%08x", (printed++ ? delim : ""),
					    (unsigned int)t->perf_counter[i]);
			else
				outp += sprintf(outp, "%s0x%016llx", (printed++ ? delim : ""), t->perf_counter[i]);
		} else if (pp->format == FORMAT_DELTA) {
			if ((pp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8lld", (printed++ ? delim : ""), t->perf_counter[i]);
			else
				outp += sprintf(outp, "%s%lld", (printed++ ? delim : ""), t->perf_counter[i]);
		} else if (pp->format == FORMAT_PERCENT) {
			if (pp->type == COUNTER_USEC)
				outp +=
				    sprintf(outp, "%s%.2f", (printed++ ? delim : ""),
					    t->perf_counter[i] / interval_float / 10000);
			else
				outp +=
				    sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * t->perf_counter[i] / tsc);
		}
	}

	for (i = 0, ppmt = sys.pmt_tp; ppmt; i++, ppmt = ppmt->next) {
		const unsigned long value_raw = t->pmt_counter[i];
		double value_converted;
		switch (ppmt->type) {
		case PMT_TYPE_RAW:
			if (pmt_counter_get_width(ppmt) <= 32)
				outp += sprintf(outp, "%s0x%08x", (printed++ ? delim : ""),
						(unsigned int)t->pmt_counter[i]);
			else
				outp += sprintf(outp, "%s0x%016llx", (printed++ ? delim : ""), t->pmt_counter[i]);

			break;

		case PMT_TYPE_XTAL_TIME:
			value_converted = 100.0 * value_raw / crystal_hz / interval_float;
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), value_converted);
			break;

		case PMT_TYPE_TCORE_CLOCK:
			value_converted = 100.0 * value_raw / tcore_clock_freq_hz / interval_float;
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), value_converted);
		}
	}

	/* C1 */
	if (DO_BIC(BIC_CPU_c1))
		outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * t->c1 / tsc);

	/* print per-core data only for 1st thread in core */
	if (!is_cpu_first_thread_in_core(t, c, p))
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

	for (i = 0, pp = sys.perf_cp; pp; i++, pp = pp->next) {
		if (pp->format == FORMAT_RAW) {
			if (pp->width == 32)
				outp +=
				    sprintf(outp, "%s0x%08x", (printed++ ? delim : ""),
					    (unsigned int)c->perf_counter[i]);
			else
				outp += sprintf(outp, "%s0x%016llx", (printed++ ? delim : ""), c->perf_counter[i]);
		} else if (pp->format == FORMAT_DELTA) {
			if ((pp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8lld", (printed++ ? delim : ""), c->perf_counter[i]);
			else
				outp += sprintf(outp, "%s%lld", (printed++ ? delim : ""), c->perf_counter[i]);
		} else if (pp->format == FORMAT_PERCENT) {
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * c->perf_counter[i] / tsc);
		}
	}

	for (i = 0, ppmt = sys.pmt_cp; ppmt; i++, ppmt = ppmt->next) {
		const unsigned long value_raw = c->pmt_counter[i];
		double value_converted;
		switch (ppmt->type) {
		case PMT_TYPE_RAW:
			if (pmt_counter_get_width(ppmt) <= 32)
				outp += sprintf(outp, "%s0x%08x", (printed++ ? delim : ""),
						(unsigned int)c->pmt_counter[i]);
			else
				outp += sprintf(outp, "%s0x%016llx", (printed++ ? delim : ""), c->pmt_counter[i]);

			break;

		case PMT_TYPE_XTAL_TIME:
			value_converted = 100.0 * value_raw / crystal_hz / interval_float;
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), value_converted);
			break;

		case PMT_TYPE_TCORE_CLOCK:
			value_converted = 100.0 * value_raw / tcore_clock_freq_hz / interval_float;
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), value_converted);
		}
	}

	fmt8 = "%s%.2f";

	if (DO_BIC(BIC_CorWatt) && platform->has_per_core_rapl)
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""),
			    rapl_counter_get_value(&c->core_energy, RAPL_UNIT_WATTS, interval_float));
	if (DO_BIC(BIC_Cor_J) && platform->has_per_core_rapl)
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""),
				rapl_counter_get_value(&c->core_energy, RAPL_UNIT_JOULES, interval_float));

	/* print per-package data only for 1st core in package */
	if (!is_cpu_first_core_in_package(t, c, p))
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

	/* SAMmc6 */
	if (DO_BIC(BIC_SAM_mc6)) {
		if (p->sam_mc6_ms == -1) {	/* detect GFX counter reset */
			outp += sprintf(outp, "%s**.**", (printed++ ? delim : ""));
		} else {
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""),
					p->sam_mc6_ms / 10.0 / interval_float);
		}
	}

	/* SAMMHz */
	if (DO_BIC(BIC_SAMMHz))
		outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), p->sam_mhz);

	/* SAMACTMHz */
	if (DO_BIC(BIC_SAMACTMHz))
		outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), p->sam_act_mhz);

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

	if (DO_BIC(BIC_Diec6))
		outp +=
		    sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->die_c6 / crystal_hz / interval_float);

	if (DO_BIC(BIC_CPU_LPI)) {
		if (p->cpu_lpi >= 0)
			outp +=
			    sprintf(outp, "%s%.2f", (printed++ ? delim : ""),
				    100.0 * p->cpu_lpi / 1000000.0 / interval_float);
		else
			outp += sprintf(outp, "%s(neg)", (printed++ ? delim : ""));
	}
	if (DO_BIC(BIC_SYS_LPI)) {
		if (p->sys_lpi >= 0)
			outp +=
			    sprintf(outp, "%s%.2f", (printed++ ? delim : ""),
				    100.0 * p->sys_lpi / 1000000.0 / interval_float);
		else
			outp += sprintf(outp, "%s(neg)", (printed++ ? delim : ""));
	}

	if (DO_BIC(BIC_PkgWatt))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""),
			    rapl_counter_get_value(&p->energy_pkg, RAPL_UNIT_WATTS, interval_float));
	if (DO_BIC(BIC_CorWatt) && !platform->has_per_core_rapl)
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""),
			    rapl_counter_get_value(&p->energy_cores, RAPL_UNIT_WATTS, interval_float));
	if (DO_BIC(BIC_GFXWatt))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""),
			    rapl_counter_get_value(&p->energy_gfx, RAPL_UNIT_WATTS, interval_float));
	if (DO_BIC(BIC_RAMWatt))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""),
			    rapl_counter_get_value(&p->energy_dram, RAPL_UNIT_WATTS, interval_float));
	if (DO_BIC(BIC_Pkg_J))
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""),
				rapl_counter_get_value(&p->energy_pkg, RAPL_UNIT_JOULES, interval_float));
	if (DO_BIC(BIC_Cor_J) && !platform->has_per_core_rapl)
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""),
				rapl_counter_get_value(&p->energy_cores, RAPL_UNIT_JOULES, interval_float));
	if (DO_BIC(BIC_GFX_J))
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""),
				rapl_counter_get_value(&p->energy_gfx, RAPL_UNIT_JOULES, interval_float));
	if (DO_BIC(BIC_RAM_J))
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""),
				rapl_counter_get_value(&p->energy_dram, RAPL_UNIT_JOULES, interval_float));
	if (DO_BIC(BIC_PKG__))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""),
			    rapl_counter_get_value(&p->rapl_pkg_perf_status, RAPL_UNIT_WATTS, interval_float));
	if (DO_BIC(BIC_RAM__))
		outp +=
		    sprintf(outp, fmt8, (printed++ ? delim : ""),
			    rapl_counter_get_value(&p->rapl_dram_perf_status, RAPL_UNIT_WATTS, interval_float));
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
		} else if (mp->type == COUNTER_K2M)
			outp += sprintf(outp, "%s%d", (printed++ ? delim : ""), (unsigned int)p->counter[i] / 1000);
	}

	for (i = 0, pp = sys.perf_pp; pp; i++, pp = pp->next) {
		if (pp->format == FORMAT_RAW) {
			if (pp->width == 32)
				outp +=
				    sprintf(outp, "%s0x%08x", (printed++ ? delim : ""),
					    (unsigned int)p->perf_counter[i]);
			else
				outp += sprintf(outp, "%s0x%016llx", (printed++ ? delim : ""), p->perf_counter[i]);
		} else if (pp->format == FORMAT_DELTA) {
			if ((pp->type == COUNTER_ITEMS) && sums_need_wide_columns)
				outp += sprintf(outp, "%s%8lld", (printed++ ? delim : ""), p->perf_counter[i]);
			else
				outp += sprintf(outp, "%s%lld", (printed++ ? delim : ""), p->perf_counter[i]);
		} else if (pp->format == FORMAT_PERCENT) {
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), 100.0 * p->perf_counter[i] / tsc);
		} else if (pp->type == COUNTER_K2M) {
			outp +=
			    sprintf(outp, "%s%d", (printed++ ? delim : ""), (unsigned int)p->perf_counter[i] / 1000);
		}
	}

	for (i = 0, ppmt = sys.pmt_pp; ppmt; i++, ppmt = ppmt->next) {
		const unsigned long value_raw = p->pmt_counter[i];
		double value_converted;
		switch (ppmt->type) {
		case PMT_TYPE_RAW:
			if (pmt_counter_get_width(ppmt) <= 32)
				outp += sprintf(outp, "%s0x%08x", (printed++ ? delim : ""),
						(unsigned int)p->pmt_counter[i]);
			else
				outp += sprintf(outp, "%s0x%016llx", (printed++ ? delim : ""), p->pmt_counter[i]);

			break;

		case PMT_TYPE_XTAL_TIME:
			value_converted = 100.0 * value_raw / crystal_hz / interval_float;
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), value_converted);
			break;

		case PMT_TYPE_TCORE_CLOCK:
			value_converted = 100.0 * value_raw / tcore_clock_freq_hz / interval_float;
			outp += sprintf(outp, "%s%.2f", (printed++ ? delim : ""), value_converted);
		}
	}

	if (DO_BIC(BIC_SysWatt) && (t == &average.threads))
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""),
				rapl_counter_get_value(&pplat_cnt->energy_psys, RAPL_UNIT_WATTS, interval_float));
	if (DO_BIC(BIC_Sys_J) && (t == &average.threads))
		outp += sprintf(outp, fmt8, (printed++ ? delim : ""),
				rapl_counter_get_value(&pplat_cnt->energy_psys, RAPL_UNIT_JOULES, interval_float));

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
	struct perf_counter_info *pp;
	struct pmt_counter *ppmt;

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
	old->die_c6 = new->die_c6 - old->die_c6;
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

	/* flag an error when mc6 counter resets/wraps */
	if (old->sam_mc6_ms > new->sam_mc6_ms)
		old->sam_mc6_ms = -1;
	else
		old->sam_mc6_ms = new->sam_mc6_ms - old->sam_mc6_ms;

	old->sam_mhz = new->sam_mhz;
	old->sam_act_mhz = new->sam_act_mhz;

	old->energy_pkg.raw_value = new->energy_pkg.raw_value - old->energy_pkg.raw_value;
	old->energy_cores.raw_value = new->energy_cores.raw_value - old->energy_cores.raw_value;
	old->energy_gfx.raw_value = new->energy_gfx.raw_value - old->energy_gfx.raw_value;
	old->energy_dram.raw_value = new->energy_dram.raw_value - old->energy_dram.raw_value;
	old->rapl_pkg_perf_status.raw_value = new->rapl_pkg_perf_status.raw_value - old->rapl_pkg_perf_status.raw_value;
	old->rapl_dram_perf_status.raw_value =
	    new->rapl_dram_perf_status.raw_value - old->rapl_dram_perf_status.raw_value;

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			old->counter[i] = new->counter[i];
		else if (mp->format == FORMAT_AVERAGE)
			old->counter[i] = new->counter[i];
		else
			old->counter[i] = new->counter[i] - old->counter[i];
	}

	for (i = 0, pp = sys.perf_pp; pp; i++, pp = pp->next) {
		if (pp->format == FORMAT_RAW)
			old->perf_counter[i] = new->perf_counter[i];
		else if (pp->format == FORMAT_AVERAGE)
			old->perf_counter[i] = new->perf_counter[i];
		else
			old->perf_counter[i] = new->perf_counter[i] - old->perf_counter[i];
	}

	for (i = 0, ppmt = sys.pmt_pp; ppmt; i++, ppmt = ppmt->next) {
		if (ppmt->format == FORMAT_RAW)
			old->pmt_counter[i] = new->pmt_counter[i];
		else
			old->pmt_counter[i] = new->pmt_counter[i] - old->pmt_counter[i];
	}

	return 0;
}

void delta_core(struct core_data *new, struct core_data *old)
{
	int i;
	struct msr_counter *mp;
	struct perf_counter_info *pp;
	struct pmt_counter *ppmt;

	old->c3 = new->c3 - old->c3;
	old->c6 = new->c6 - old->c6;
	old->c7 = new->c7 - old->c7;
	old->core_temp_c = new->core_temp_c;
	old->core_throt_cnt = new->core_throt_cnt;
	old->mc6_us = new->mc6_us - old->mc6_us;

	DELTA_WRAP32(new->core_energy.raw_value, old->core_energy.raw_value);

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			old->counter[i] = new->counter[i];
		else
			old->counter[i] = new->counter[i] - old->counter[i];
	}

	for (i = 0, pp = sys.perf_cp; pp; i++, pp = pp->next) {
		if (pp->format == FORMAT_RAW)
			old->perf_counter[i] = new->perf_counter[i];
		else
			old->perf_counter[i] = new->perf_counter[i] - old->perf_counter[i];
	}

	for (i = 0, ppmt = sys.pmt_cp; ppmt; i++, ppmt = ppmt->next) {
		if (ppmt->format == FORMAT_RAW)
			old->pmt_counter[i] = new->pmt_counter[i];
		else
			old->pmt_counter[i] = new->pmt_counter[i] - old->pmt_counter[i];
	}
}

int soft_c1_residency_display(int bic)
{
	if (!DO_BIC(BIC_CPU_c1) || platform->has_msr_core_c1_res)
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
	struct perf_counter_info *pp;
	struct pmt_counter *ppmt;

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

	if (DO_BIC(BIC_Avg_MHz) || DO_BIC(BIC_Busy) || DO_BIC(BIC_Bzy_MHz) || DO_BIC(BIC_IPC)
	    || soft_c1_residency_display(BIC_Avg_MHz)) {
		if ((new->aperf > old->aperf) && (new->mperf > old->mperf)) {
			old->aperf = new->aperf - old->aperf;
			old->mperf = new->mperf - old->mperf;
		} else {
			return -1;
		}
	}

	if (platform->has_msr_core_c1_res) {
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

	if (DO_BIC(BIC_NMI))
		old->nmi_count = new->nmi_count - old->nmi_count;

	if (DO_BIC(BIC_SMI))
		old->smi_count = new->smi_count - old->smi_count;

	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			old->counter[i] = new->counter[i];
		else
			old->counter[i] = new->counter[i] - old->counter[i];
	}

	for (i = 0, pp = sys.perf_tp; pp; i++, pp = pp->next) {
		if (pp->format == FORMAT_RAW)
			old->perf_counter[i] = new->perf_counter[i];
		else
			old->perf_counter[i] = new->perf_counter[i] - old->perf_counter[i];
	}

	for (i = 0, ppmt = sys.pmt_tp; ppmt; i++, ppmt = ppmt->next) {
		if (ppmt->format == FORMAT_RAW)
			old->pmt_counter[i] = new->pmt_counter[i];
		else
			old->pmt_counter[i] = new->pmt_counter[i] - old->pmt_counter[i];
	}

	return 0;
}

int delta_cpu(struct thread_data *t, struct core_data *c,
	      struct pkg_data *p, struct thread_data *t2, struct core_data *c2, struct pkg_data *p2)
{
	int retval = 0;

	/* calculate core delta only for 1st thread in core */
	if (is_cpu_first_thread_in_core(t, c, p))
		delta_core(c, c2);

	/* always calculate thread delta */
	retval = delta_thread(t, t2, c2);	/* c2 is core delta */

	/* calculate package delta only for 1st core in package */
	if (is_cpu_first_core_in_package(t, c, p))
		retval |= delta_package(p, p2);

	return retval;
}

void delta_platform(struct platform_counters *new, struct platform_counters *old)
{
	old->energy_psys.raw_value = new->energy_psys.raw_value - old->energy_psys.raw_value;
}

void rapl_counter_clear(struct rapl_counter *c)
{
	c->raw_value = 0;
	c->scale = 0.0;
	c->unit = RAPL_UNIT_INVALID;
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
	t->nmi_count = 0;
	t->smi_count = 0;

	c->c3 = 0;
	c->c6 = 0;
	c->c7 = 0;
	c->mc6_us = 0;
	c->core_temp_c = 0;
	rapl_counter_clear(&c->core_energy);
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
	p->die_c6 = 0;
	p->cpu_lpi = 0;
	p->sys_lpi = 0;

	rapl_counter_clear(&p->energy_pkg);
	rapl_counter_clear(&p->energy_dram);
	rapl_counter_clear(&p->energy_cores);
	rapl_counter_clear(&p->energy_gfx);
	rapl_counter_clear(&p->rapl_pkg_perf_status);
	rapl_counter_clear(&p->rapl_dram_perf_status);
	p->pkg_temp_c = 0;

	p->gfx_rc6_ms = 0;
	p->uncore_mhz = 0;
	p->gfx_mhz = 0;
	p->gfx_act_mhz = 0;
	p->sam_mc6_ms = 0;
	p->sam_mhz = 0;
	p->sam_act_mhz = 0;
	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next)
		t->counter[i] = 0;

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next)
		c->counter[i] = 0;

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next)
		p->counter[i] = 0;

	memset(&t->perf_counter[0], 0, sizeof(t->perf_counter));
	memset(&c->perf_counter[0], 0, sizeof(c->perf_counter));
	memset(&p->perf_counter[0], 0, sizeof(p->perf_counter));

	memset(&t->pmt_counter[0], 0, ARRAY_SIZE(t->pmt_counter));
	memset(&c->pmt_counter[0], 0, ARRAY_SIZE(c->pmt_counter));
	memset(&p->pmt_counter[0], 0, ARRAY_SIZE(p->pmt_counter));
}

void rapl_counter_accumulate(struct rapl_counter *dst, const struct rapl_counter *src)
{
	/* Copy unit and scale from src if dst is not initialized */
	if (dst->unit == RAPL_UNIT_INVALID) {
		dst->unit = src->unit;
		dst->scale = src->scale;
	}

	assert(dst->unit == src->unit);
	assert(dst->scale == src->scale);

	dst->raw_value += src->raw_value;
}

int sum_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	int i;
	struct msr_counter *mp;
	struct perf_counter_info *pp;
	struct pmt_counter *ppmt;

	/* copy un-changing apic_id's */
	if (DO_BIC(BIC_APIC))
		average.threads.apic_id = t->apic_id;
	if (DO_BIC(BIC_X2APIC))
		average.threads.x2apic_id = t->x2apic_id;

	/* remember first tv_begin */
	if (average.threads.tv_begin.tv_sec == 0)
		average.threads.tv_begin = procsysfs_tv_begin;

	/* remember last tv_end */
	average.threads.tv_end = t->tv_end;

	average.threads.tsc += t->tsc;
	average.threads.aperf += t->aperf;
	average.threads.mperf += t->mperf;
	average.threads.c1 += t->c1;

	average.threads.instr_count += t->instr_count;

	average.threads.irq_count += t->irq_count;
	average.threads.nmi_count += t->nmi_count;
	average.threads.smi_count += t->smi_count;

	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		average.threads.counter[i] += t->counter[i];
	}

	for (i = 0, pp = sys.perf_tp; pp; i++, pp = pp->next) {
		if (pp->format == FORMAT_RAW)
			continue;
		average.threads.perf_counter[i] += t->perf_counter[i];
	}

	for (i = 0, ppmt = sys.pmt_tp; ppmt; i++, ppmt = ppmt->next) {
		average.threads.pmt_counter[i] += t->pmt_counter[i];
	}

	/* sum per-core values only for 1st thread in core */
	if (!is_cpu_first_thread_in_core(t, c, p))
		return 0;

	average.cores.c3 += c->c3;
	average.cores.c6 += c->c6;
	average.cores.c7 += c->c7;
	average.cores.mc6_us += c->mc6_us;

	average.cores.core_temp_c = MAX(average.cores.core_temp_c, c->core_temp_c);
	average.cores.core_throt_cnt = MAX(average.cores.core_throt_cnt, c->core_throt_cnt);

	rapl_counter_accumulate(&average.cores.core_energy, &c->core_energy);

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		average.cores.counter[i] += c->counter[i];
	}

	for (i = 0, pp = sys.perf_cp; pp; i++, pp = pp->next) {
		if (pp->format == FORMAT_RAW)
			continue;
		average.cores.perf_counter[i] += c->perf_counter[i];
	}

	for (i = 0, ppmt = sys.pmt_cp; ppmt; i++, ppmt = ppmt->next) {
		average.cores.pmt_counter[i] += c->pmt_counter[i];
	}

	/* sum per-pkg values only for 1st core in pkg */
	if (!is_cpu_first_core_in_package(t, c, p))
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
	average.packages.die_c6 += p->die_c6;

	average.packages.cpu_lpi = p->cpu_lpi;
	average.packages.sys_lpi = p->sys_lpi;

	rapl_counter_accumulate(&average.packages.energy_pkg, &p->energy_pkg);
	rapl_counter_accumulate(&average.packages.energy_dram, &p->energy_dram);
	rapl_counter_accumulate(&average.packages.energy_cores, &p->energy_cores);
	rapl_counter_accumulate(&average.packages.energy_gfx, &p->energy_gfx);

	average.packages.gfx_rc6_ms = p->gfx_rc6_ms;
	average.packages.uncore_mhz = p->uncore_mhz;
	average.packages.gfx_mhz = p->gfx_mhz;
	average.packages.gfx_act_mhz = p->gfx_act_mhz;
	average.packages.sam_mc6_ms = p->sam_mc6_ms;
	average.packages.sam_mhz = p->sam_mhz;
	average.packages.sam_act_mhz = p->sam_act_mhz;

	average.packages.pkg_temp_c = MAX(average.packages.pkg_temp_c, p->pkg_temp_c);

	rapl_counter_accumulate(&average.packages.rapl_pkg_perf_status, &p->rapl_pkg_perf_status);
	rapl_counter_accumulate(&average.packages.rapl_dram_perf_status, &p->rapl_dram_perf_status);

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if ((mp->format == FORMAT_RAW) && (topo.num_packages == 0))
			average.packages.counter[i] = p->counter[i];
		else
			average.packages.counter[i] += p->counter[i];
	}

	for (i = 0, pp = sys.perf_pp; pp; i++, pp = pp->next) {
		if ((pp->format == FORMAT_RAW) && (topo.num_packages == 0))
			average.packages.perf_counter[i] = p->perf_counter[i];
		else
			average.packages.perf_counter[i] += p->perf_counter[i];
	}

	for (i = 0, ppmt = sys.pmt_pp; ppmt; i++, ppmt = ppmt->next) {
		average.packages.pmt_counter[i] += p->pmt_counter[i];
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
	struct perf_counter_info *pp;
	struct pmt_counter *ppmt;

	clear_counters(&average.threads, &average.cores, &average.packages);

	for_all_cpus(sum_counters, t, c, p);

	/* Use the global time delta for the average. */
	average.threads.tv_delta = tv_delta;

	average.threads.tsc /= topo.allowed_cpus;
	average.threads.aperf /= topo.allowed_cpus;
	average.threads.mperf /= topo.allowed_cpus;
	average.threads.instr_count /= topo.allowed_cpus;
	average.threads.c1 /= topo.allowed_cpus;

	if (average.threads.irq_count > 9999999)
		sums_need_wide_columns = 1;
	if (average.threads.nmi_count > 9999999)
		sums_need_wide_columns = 1;


	average.cores.c3 /= topo.allowed_cores;
	average.cores.c6 /= topo.allowed_cores;
	average.cores.c7 /= topo.allowed_cores;
	average.cores.mc6_us /= topo.allowed_cores;

	if (DO_BIC(BIC_Totl_c0))
		average.packages.pkg_wtd_core_c0 /= topo.allowed_packages;
	if (DO_BIC(BIC_Any_c0))
		average.packages.pkg_any_core_c0 /= topo.allowed_packages;
	if (DO_BIC(BIC_GFX_c0))
		average.packages.pkg_any_gfxe_c0 /= topo.allowed_packages;
	if (DO_BIC(BIC_CPUGFX))
		average.packages.pkg_both_core_gfxe_c0 /= topo.allowed_packages;

	average.packages.pc2 /= topo.allowed_packages;
	if (DO_BIC(BIC_Pkgpc3))
		average.packages.pc3 /= topo.allowed_packages;
	if (DO_BIC(BIC_Pkgpc6))
		average.packages.pc6 /= topo.allowed_packages;
	if (DO_BIC(BIC_Pkgpc7))
		average.packages.pc7 /= topo.allowed_packages;

	average.packages.pc8 /= topo.allowed_packages;
	average.packages.pc9 /= topo.allowed_packages;
	average.packages.pc10 /= topo.allowed_packages;
	average.packages.die_c6 /= topo.allowed_packages;

	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		if (mp->type == COUNTER_ITEMS) {
			if (average.threads.counter[i] > 9999999)
				sums_need_wide_columns = 1;
			continue;
		}
		average.threads.counter[i] /= topo.allowed_cpus;
	}
	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		if (mp->type == COUNTER_ITEMS) {
			if (average.cores.counter[i] > 9999999)
				sums_need_wide_columns = 1;
		}
		average.cores.counter[i] /= topo.allowed_cores;
	}
	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		if (mp->type == COUNTER_ITEMS) {
			if (average.packages.counter[i] > 9999999)
				sums_need_wide_columns = 1;
		}
		average.packages.counter[i] /= topo.allowed_packages;
	}

	for (i = 0, pp = sys.perf_tp; pp; i++, pp = pp->next) {
		if (pp->format == FORMAT_RAW)
			continue;
		if (pp->type == COUNTER_ITEMS) {
			if (average.threads.perf_counter[i] > 9999999)
				sums_need_wide_columns = 1;
			continue;
		}
		average.threads.perf_counter[i] /= topo.allowed_cpus;
	}
	for (i = 0, pp = sys.perf_cp; pp; i++, pp = pp->next) {
		if (pp->format == FORMAT_RAW)
			continue;
		if (pp->type == COUNTER_ITEMS) {
			if (average.cores.perf_counter[i] > 9999999)
				sums_need_wide_columns = 1;
		}
		average.cores.perf_counter[i] /= topo.allowed_cores;
	}
	for (i = 0, pp = sys.perf_pp; pp; i++, pp = pp->next) {
		if (pp->format == FORMAT_RAW)
			continue;
		if (pp->type == COUNTER_ITEMS) {
			if (average.packages.perf_counter[i] > 9999999)
				sums_need_wide_columns = 1;
		}
		average.packages.perf_counter[i] /= topo.allowed_packages;
	}

	for (i = 0, ppmt = sys.pmt_tp; ppmt; i++, ppmt = ppmt->next) {
		average.threads.pmt_counter[i] /= topo.allowed_cpus;
	}
	for (i = 0, ppmt = sys.pmt_cp; ppmt; i++, ppmt = ppmt->next) {
		average.cores.pmt_counter[i] /= topo.allowed_cores;
	}
	for (i = 0, ppmt = sys.pmt_pp; ppmt; i++, ppmt = ppmt->next) {
		average.packages.pmt_counter[i] /= topo.allowed_packages;
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

int get_mp(int cpu, struct msr_counter *mp, unsigned long long *counterp, char *counter_path)
{
	if (mp->msr_num != 0) {
		assert(!no_msr);
		if (get_msr(cpu, mp->msr_num, counterp))
			return -1;
	} else {
		char path[128 + PATH_BYTES];

		if (mp->flags & SYSFS_PERCPU) {
			sprintf(path, "/sys/devices/system/cpu/cpu%d/%s", cpu, mp->sp->path);

			*counterp = snapshot_sysfs_counter(path);
		} else {
			*counterp = snapshot_sysfs_counter(counter_path);
		}
	}

	return 0;
}

unsigned long long get_legacy_uncore_mhz(int package)
{
	char path[128];
	int die;
	static int warn_once;

	/*
	 * for this package, use the first die_id that exists
	 */
	for (die = 0; die <= topo.max_die_id; ++die) {

		sprintf(path, "/sys/devices/system/cpu/intel_uncore_frequency/package_%02d_die_%02d/current_freq_khz",
			package, die);

		if (access(path, R_OK) == 0)
			return (snapshot_sysfs_counter(path) / 1000);
	}
	if (!warn_once) {
		warnx("BUG: %s: No %s", __func__, path);
		warn_once = 1;
	}

	return 0;
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
	if (no_msr)
		return -1;

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

struct amperf_group_fd {
	int aperf;		/* Also the group descriptor */
	int mperf;
};

static int read_perf_counter_info(const char *const path, const char *const parse_format, void *value_ptr)
{
	int fdmt;
	int bytes_read;
	char buf[64];
	int ret = -1;

	fdmt = open(path, O_RDONLY, 0);
	if (fdmt == -1) {
		if (debug)
			fprintf(stderr, "Failed to parse perf counter info %s\n", path);
		ret = -1;
		goto cleanup_and_exit;
	}

	bytes_read = read(fdmt, buf, sizeof(buf) - 1);
	if (bytes_read <= 0 || bytes_read >= (int)sizeof(buf)) {
		if (debug)
			fprintf(stderr, "Failed to parse perf counter info %s\n", path);
		ret = -1;
		goto cleanup_and_exit;
	}

	buf[bytes_read] = '\0';

	if (sscanf(buf, parse_format, value_ptr) != 1) {
		if (debug)
			fprintf(stderr, "Failed to parse perf counter info %s\n", path);
		ret = -1;
		goto cleanup_and_exit;
	}

	ret = 0;

cleanup_and_exit:
	close(fdmt);
	return ret;
}

static unsigned int read_perf_counter_info_n(const char *const path, const char *const parse_format)
{
	unsigned int v;
	int status;

	status = read_perf_counter_info(path, parse_format, &v);
	if (status)
		v = -1;

	return v;
}

static unsigned int read_perf_type(const char *subsys)
{
	const char *const path_format = "/sys/bus/event_source/devices/%s/type";
	const char *const format = "%u";
	char path[128];

	snprintf(path, sizeof(path), path_format, subsys);

	return read_perf_counter_info_n(path, format);
}

static unsigned int read_perf_config(const char *subsys, const char *event_name)
{
	const char *const path_format = "/sys/bus/event_source/devices/%s/events/%s";
	FILE *fconfig = NULL;
	char path[128];
	char config_str[64];
	unsigned int config;
	unsigned int umask;
	bool has_config = false;
	bool has_umask = false;
	unsigned int ret = -1;

	snprintf(path, sizeof(path), path_format, subsys, event_name);

	fconfig = fopen(path, "r");
	if (!fconfig)
		return -1;

	if (fgets(config_str, ARRAY_SIZE(config_str), fconfig) != config_str)
		goto cleanup_and_exit;

	for (char *pconfig_str = &config_str[0]; pconfig_str;) {
		if (sscanf(pconfig_str, "event=%x", &config) == 1) {
			has_config = true;
			goto next;
		}

		if (sscanf(pconfig_str, "umask=%x", &umask) == 1) {
			has_umask = true;
			goto next;
		}

next:
		pconfig_str = strchr(pconfig_str, ',');
		if (pconfig_str) {
			*pconfig_str = '\0';
			++pconfig_str;
		}
	}

	if (!has_umask)
		umask = 0;

	if (has_config)
		ret = (umask << 8) | config;

cleanup_and_exit:
	fclose(fconfig);
	return ret;
}

static unsigned int read_perf_rapl_unit(const char *subsys, const char *event_name)
{
	const char *const path_format = "/sys/bus/event_source/devices/%s/events/%s.unit";
	const char *const format = "%s";
	char path[128];
	char unit_buffer[16];

	snprintf(path, sizeof(path), path_format, subsys, event_name);

	read_perf_counter_info(path, format, &unit_buffer);
	if (strcmp("Joules", unit_buffer) == 0)
		return RAPL_UNIT_JOULES;

	return RAPL_UNIT_INVALID;
}

static double read_perf_scale(const char *subsys, const char *event_name)
{
	const char *const path_format = "/sys/bus/event_source/devices/%s/events/%s.scale";
	const char *const format = "%lf";
	char path[128];
	double scale;

	snprintf(path, sizeof(path), path_format, subsys, event_name);

	if (read_perf_counter_info(path, format, &scale))
		return 0.0;

	return scale;
}

size_t rapl_counter_info_count_perf(const struct rapl_counter_info_t *rci)
{
	size_t ret = 0;

	for (int i = 0; i < NUM_RAPL_COUNTERS; ++i)
		if (rci->source[i] == COUNTER_SOURCE_PERF)
			++ret;

	return ret;
}

static size_t cstate_counter_info_count_perf(const struct cstate_counter_info_t *cci)
{
	size_t ret = 0;

	for (int i = 0; i < NUM_CSTATE_COUNTERS; ++i)
		if (cci->source[i] == COUNTER_SOURCE_PERF)
			++ret;

	return ret;
}

void write_rapl_counter(struct rapl_counter *rc, struct rapl_counter_info_t *rci, unsigned int idx)
{
	if (rci->source[idx] == COUNTER_SOURCE_NONE)
		return;

	rc->raw_value = rci->data[idx];
	rc->unit = rci->unit[idx];
	rc->scale = rci->scale[idx];
}

int get_rapl_counters(int cpu, unsigned int domain, struct core_data *c, struct pkg_data *p)
{
	struct platform_counters *pplat_cnt = p == package_odd ? &platform_counters_odd : &platform_counters_even;
	unsigned long long perf_data[NUM_RAPL_COUNTERS + 1];
	struct rapl_counter_info_t *rci;

	if (debug >= 2)
		fprintf(stderr, "%s: cpu%d domain%d\n", __func__, cpu, domain);

	assert(rapl_counter_info_perdomain);
	assert(domain < rapl_counter_info_perdomain_size);

	rci = &rapl_counter_info_perdomain[domain];

	/*
	 * If we have any perf counters to read, read them all now, in bulk
	 */
	if (rci->fd_perf != -1) {
		size_t num_perf_counters = rapl_counter_info_count_perf(rci);
		const ssize_t expected_read_size = (num_perf_counters + 1) * sizeof(unsigned long long);
		const ssize_t actual_read_size = read(rci->fd_perf, &perf_data[0], sizeof(perf_data));

		if (actual_read_size != expected_read_size)
			err(-1, "%s: failed to read perf_data (%zu %zu)", __func__, expected_read_size,
			    actual_read_size);
	}

	for (unsigned int i = 0, pi = 1; i < NUM_RAPL_COUNTERS; ++i) {
		switch (rci->source[i]) {
		case COUNTER_SOURCE_NONE:
			rci->data[i] = 0;
			break;

		case COUNTER_SOURCE_PERF:
			assert(pi < ARRAY_SIZE(perf_data));
			assert(rci->fd_perf != -1);

			if (debug >= 2)
				fprintf(stderr, "Reading rapl counter via perf at %u (%llu %e %lf)\n",
					i, perf_data[pi], rci->scale[i], perf_data[pi] * rci->scale[i]);

			rci->data[i] = perf_data[pi];

			++pi;
			break;

		case COUNTER_SOURCE_MSR:
			if (debug >= 2)
				fprintf(stderr, "Reading rapl counter via msr at %u\n", i);

			assert(!no_msr);
			if (rci->flags[i] & RAPL_COUNTER_FLAG_USE_MSR_SUM) {
				if (get_msr_sum(cpu, rci->msr[i], &rci->data[i]))
					return -13 - i;
			} else {
				if (get_msr(cpu, rci->msr[i], &rci->data[i]))
					return -13 - i;
			}

			rci->data[i] &= rci->msr_mask[i];
			if (rci->msr_shift[i] >= 0)
				rci->data[i] >>= abs(rci->msr_shift[i]);
			else
				rci->data[i] <<= abs(rci->msr_shift[i]);

			break;
		}
	}

	BUILD_BUG_ON(NUM_RAPL_COUNTERS != 8);
	write_rapl_counter(&p->energy_pkg, rci, RAPL_RCI_INDEX_ENERGY_PKG);
	write_rapl_counter(&p->energy_cores, rci, RAPL_RCI_INDEX_ENERGY_CORES);
	write_rapl_counter(&p->energy_dram, rci, RAPL_RCI_INDEX_DRAM);
	write_rapl_counter(&p->energy_gfx, rci, RAPL_RCI_INDEX_GFX);
	write_rapl_counter(&p->rapl_pkg_perf_status, rci, RAPL_RCI_INDEX_PKG_PERF_STATUS);
	write_rapl_counter(&p->rapl_dram_perf_status, rci, RAPL_RCI_INDEX_DRAM_PERF_STATUS);
	write_rapl_counter(&c->core_energy, rci, RAPL_RCI_INDEX_CORE_ENERGY);
	write_rapl_counter(&pplat_cnt->energy_psys, rci, RAPL_RCI_INDEX_ENERGY_PLATFORM);

	return 0;
}

char *find_sysfs_path_by_id(struct sysfs_path *sp, int id)
{
	while (sp) {
		if (sp->id == id)
			return (sp->path);
		sp = sp->next;
	}
	if (debug)
		warnx("%s: id%d not found", __func__, id);
	return NULL;
}

int get_cstate_counters(unsigned int cpu, struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	/*
	 * Overcommit memory a little bit here,
	 * but skip calculating exact sizes for the buffers.
	 */
	unsigned long long perf_data[NUM_CSTATE_COUNTERS];
	unsigned long long perf_data_core[NUM_CSTATE_COUNTERS + 1];
	unsigned long long perf_data_pkg[NUM_CSTATE_COUNTERS + 1];

	struct cstate_counter_info_t *cci;

	if (debug >= 2)
		fprintf(stderr, "%s: cpu%d\n", __func__, cpu);

	assert(ccstate_counter_info);
	assert(cpu <= ccstate_counter_info_size);

	ZERO_ARRAY(perf_data);
	ZERO_ARRAY(perf_data_core);
	ZERO_ARRAY(perf_data_pkg);

	cci = &ccstate_counter_info[cpu];

	/*
	 * If we have any perf counters to read, read them all now, in bulk
	 */
	const size_t num_perf_counters = cstate_counter_info_count_perf(cci);
	ssize_t expected_read_size = num_perf_counters * sizeof(unsigned long long);
	ssize_t actual_read_size_core = 0, actual_read_size_pkg = 0;

	if (cci->fd_perf_core != -1) {
		/* Each descriptor read begins with number of counters read. */
		expected_read_size += sizeof(unsigned long long);

		actual_read_size_core = read(cci->fd_perf_core, &perf_data_core[0], sizeof(perf_data_core));

		if (actual_read_size_core <= 0)
			err(-1, "%s: read perf %s: %ld", __func__, "core", actual_read_size_core);
	}

	if (cci->fd_perf_pkg != -1) {
		/* Each descriptor read begins with number of counters read. */
		expected_read_size += sizeof(unsigned long long);

		actual_read_size_pkg = read(cci->fd_perf_pkg, &perf_data_pkg[0], sizeof(perf_data_pkg));

		if (actual_read_size_pkg <= 0)
			err(-1, "%s: read perf %s: %ld", __func__, "pkg", actual_read_size_pkg);
	}

	const ssize_t actual_read_size_total = actual_read_size_core + actual_read_size_pkg;

	if (actual_read_size_total != expected_read_size)
		err(-1, "%s: failed to read perf_data (%zu %zu)", __func__, expected_read_size, actual_read_size_total);

	/*
	 * Copy ccstate and pcstate data into unified buffer.
	 *
	 * Skip first element from core and pkg buffers.
	 * Kernel puts there how many counters were read.
	 */
	const size_t num_core_counters = perf_data_core[0];
	const size_t num_pkg_counters = perf_data_pkg[0];

	assert(num_perf_counters == num_core_counters + num_pkg_counters);

	/* Copy ccstate perf data */
	memcpy(&perf_data[0], &perf_data_core[1], num_core_counters * sizeof(unsigned long long));

	/* Copy pcstate perf data */
	memcpy(&perf_data[num_core_counters], &perf_data_pkg[1], num_pkg_counters * sizeof(unsigned long long));

	for (unsigned int i = 0, pi = 0; i < NUM_CSTATE_COUNTERS; ++i) {
		switch (cci->source[i]) {
		case COUNTER_SOURCE_NONE:
			break;

		case COUNTER_SOURCE_PERF:
			assert(pi < ARRAY_SIZE(perf_data));
			assert(cci->fd_perf_core != -1 || cci->fd_perf_pkg != -1);

			if (debug >= 2)
				fprintf(stderr, "cstate via %s %u: %llu\n", "perf", i, perf_data[pi]);

			cci->data[i] = perf_data[pi];

			++pi;
			break;

		case COUNTER_SOURCE_MSR:
			assert(!no_msr);
			if (get_msr(cpu, cci->msr[i], &cci->data[i]))
				return -13 - i;

			if (debug >= 2)
				fprintf(stderr, "cstate via %s0x%llx %u: %llu\n", "msr", cci->msr[i], i, cci->data[i]);

			break;
		}
	}

	/*
	 * Helper to write the data only if the source of
	 * the counter for the current cpu is not none.
	 *
	 * Otherwise we would overwrite core data with 0 (default value),
	 * when invoked for the thread sibling.
	 */
#define PERF_COUNTER_WRITE_DATA(out_counter, index) do {	\
	if (cci->source[index] != COUNTER_SOURCE_NONE)		\
		out_counter = cci->data[index];			\
} while (0)

	BUILD_BUG_ON(NUM_CSTATE_COUNTERS != 11);

	PERF_COUNTER_WRITE_DATA(t->c1, CCSTATE_RCI_INDEX_C1_RESIDENCY);
	PERF_COUNTER_WRITE_DATA(c->c3, CCSTATE_RCI_INDEX_C3_RESIDENCY);
	PERF_COUNTER_WRITE_DATA(c->c6, CCSTATE_RCI_INDEX_C6_RESIDENCY);
	PERF_COUNTER_WRITE_DATA(c->c7, CCSTATE_RCI_INDEX_C7_RESIDENCY);

	PERF_COUNTER_WRITE_DATA(p->pc2, PCSTATE_RCI_INDEX_C2_RESIDENCY);
	PERF_COUNTER_WRITE_DATA(p->pc3, PCSTATE_RCI_INDEX_C3_RESIDENCY);
	PERF_COUNTER_WRITE_DATA(p->pc6, PCSTATE_RCI_INDEX_C6_RESIDENCY);
	PERF_COUNTER_WRITE_DATA(p->pc7, PCSTATE_RCI_INDEX_C7_RESIDENCY);
	PERF_COUNTER_WRITE_DATA(p->pc8, PCSTATE_RCI_INDEX_C8_RESIDENCY);
	PERF_COUNTER_WRITE_DATA(p->pc9, PCSTATE_RCI_INDEX_C9_RESIDENCY);
	PERF_COUNTER_WRITE_DATA(p->pc10, PCSTATE_RCI_INDEX_C10_RESIDENCY);

#undef PERF_COUNTER_WRITE_DATA

	return 0;
}

size_t msr_counter_info_count_perf(const struct msr_counter_info_t *mci)
{
	size_t ret = 0;

	for (int i = 0; i < NUM_MSR_COUNTERS; ++i)
		if (mci->source[i] == COUNTER_SOURCE_PERF)
			++ret;

	return ret;
}

int get_smi_aperf_mperf(unsigned int cpu, struct thread_data *t)
{
	unsigned long long perf_data[NUM_MSR_COUNTERS + 1];

	struct msr_counter_info_t *mci;

	if (debug >= 2)
		fprintf(stderr, "%s: cpu%d\n", __func__, cpu);

	assert(msr_counter_info);
	assert(cpu <= msr_counter_info_size);

	mci = &msr_counter_info[cpu];

	ZERO_ARRAY(perf_data);
	ZERO_ARRAY(mci->data);

	if (mci->fd_perf != -1) {
		const size_t num_perf_counters = msr_counter_info_count_perf(mci);
		const ssize_t expected_read_size = (num_perf_counters + 1) * sizeof(unsigned long long);
		const ssize_t actual_read_size = read(mci->fd_perf, &perf_data[0], sizeof(perf_data));

		if (actual_read_size != expected_read_size)
			err(-1, "%s: failed to read perf_data (%zu %zu)", __func__, expected_read_size,
			    actual_read_size);
	}

	for (unsigned int i = 0, pi = 1; i < NUM_MSR_COUNTERS; ++i) {
		switch (mci->source[i]) {
		case COUNTER_SOURCE_NONE:
			break;

		case COUNTER_SOURCE_PERF:
			assert(pi < ARRAY_SIZE(perf_data));
			assert(mci->fd_perf != -1);

			if (debug >= 2)
				fprintf(stderr, "Reading msr counter via perf at %u: %llu\n", i, perf_data[pi]);

			mci->data[i] = perf_data[pi];

			++pi;
			break;

		case COUNTER_SOURCE_MSR:
			assert(!no_msr);

			if (get_msr(cpu, mci->msr[i], &mci->data[i]))
				return -2 - i;

			mci->data[i] &= mci->msr_mask[i];

			if (debug >= 2)
				fprintf(stderr, "Reading msr counter via msr at %u: %llu\n", i, mci->data[i]);

			break;
		}
	}

	BUILD_BUG_ON(NUM_MSR_COUNTERS != 3);
	t->aperf = mci->data[MSR_RCI_INDEX_APERF];
	t->mperf = mci->data[MSR_RCI_INDEX_MPERF];
	t->smi_count = mci->data[MSR_RCI_INDEX_SMI];

	return 0;
}

int perf_counter_info_read_values(struct perf_counter_info *pp, int cpu, unsigned long long *out, size_t out_size)
{
	unsigned int domain;
	unsigned long long value;
	int fd_counter;

	for (size_t i = 0; pp; ++i, pp = pp->next) {
		domain = cpu_to_domain(pp, cpu);
		assert(domain < pp->num_domains);

		fd_counter = pp->fd_perf_per_domain[domain];

		if (fd_counter == -1)
			continue;

		if (read(fd_counter, &value, sizeof(value)) != sizeof(value))
			return 1;

		assert(i < out_size);
		out[i] = value * pp->scale;
	}

	return 0;
}

unsigned long pmt_gen_value_mask(unsigned int lsb, unsigned int msb)
{
	unsigned long mask;

	if (msb == 63)
		mask = 0xffffffffffffffff;
	else
		mask = ((1 << (msb + 1)) - 1);

	mask -= (1 << lsb) - 1;

	return mask;
}

unsigned long pmt_read_counter(struct pmt_counter *ppmt, unsigned int domain_id)
{
	if (domain_id >= ppmt->num_domains)
		return 0;

	const unsigned long *pmmio = ppmt->domains[domain_id].pcounter;
	const unsigned long value = pmmio ? *pmmio : 0;
	const unsigned long value_mask = pmt_gen_value_mask(ppmt->lsb, ppmt->msb);
	const unsigned long value_shift = ppmt->lsb;

	return (value & value_mask) >> value_shift;
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
	struct msr_counter *mp;
	struct pmt_counter *pp;
	int i;
	int status;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "%s: Could not migrate to CPU %d\n", __func__, cpu);
		return -1;
	}

	gettimeofday(&t->tv_begin, (struct timezone *)NULL);

	if (first_counter_read)
		get_apic_id(t);

	t->tsc = rdtsc();	/* we are running on local CPU of interest */

	get_smi_aperf_mperf(cpu, t);

	if (DO_BIC(BIC_IPC))
		if (read(get_instr_count_fd(cpu), &t->instr_count, sizeof(long long)) != sizeof(long long))
			return -4;

	if (DO_BIC(BIC_IRQ))
		t->irq_count = irqs_per_cpu[cpu];
	if (DO_BIC(BIC_NMI))
		t->nmi_count = nmi_per_cpu[cpu];

	get_cstate_counters(cpu, t, c, p);

	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
		if (get_mp(cpu, mp, &t->counter[i], mp->sp->path))
			return -10;
	}

	if (perf_counter_info_read_values(sys.perf_tp, cpu, t->perf_counter, MAX_ADDED_THREAD_COUNTERS))
		return -10;

	for (i = 0, pp = sys.pmt_tp; pp; i++, pp = pp->next)
		t->pmt_counter[i] = pmt_read_counter(pp, t->cpu_id);

	/* collect core counters only for 1st thread in core */
	if (!is_cpu_first_thread_in_core(t, c, p))
		goto done;

	if (platform->has_per_core_rapl) {
		status = get_rapl_counters(cpu, c->core_id, c, p);
		if (status != 0)
			return status;
	}

	if (DO_BIC(BIC_CPU_c7) && t->is_atom) {
		/*
		 * For Atom CPUs that has core cstate deeper than c6,
		 * MSR_CORE_C6_RESIDENCY returns residency of cc6 and deeper.
		 * Minus CC7 (and deeper cstates) residency to get
		 * accturate cc6 residency.
		 */
		c->c6 -= c->c7;
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

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (get_mp(cpu, mp, &c->counter[i], mp->sp->path))
			return -10;
	}

	if (perf_counter_info_read_values(sys.perf_cp, cpu, c->perf_counter, MAX_ADDED_CORE_COUNTERS))
		return -10;

	for (i = 0, pp = sys.pmt_cp; pp; i++, pp = pp->next)
		c->pmt_counter[i] = pmt_read_counter(pp, c->core_id);

	/* collect package counters only for 1st core in package */
	if (!is_cpu_first_core_in_package(t, c, p))
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

	if (DO_BIC(BIC_CPU_LPI))
		p->cpu_lpi = cpuidle_cur_cpu_lpi_us;
	if (DO_BIC(BIC_SYS_LPI))
		p->sys_lpi = cpuidle_cur_sys_lpi_us;

	if (!platform->has_per_core_rapl) {
		status = get_rapl_counters(cpu, p->package_id, c, p);
		if (status != 0)
			return status;
	}

	if (DO_BIC(BIC_PkgTmp)) {
		if (get_msr(cpu, MSR_IA32_PACKAGE_THERM_STATUS, &msr))
			return -17;
		p->pkg_temp_c = tj_max - ((msr >> 16) & 0x7F);
	}

	if (DO_BIC(BIC_UNCORE_MHZ))
		p->uncore_mhz = get_legacy_uncore_mhz(p->package_id);

	if (DO_BIC(BIC_GFX_rc6))
		p->gfx_rc6_ms = gfx_info[GFX_rc6].val_ull;

	if (DO_BIC(BIC_GFXMHz))
		p->gfx_mhz = gfx_info[GFX_MHz].val;

	if (DO_BIC(BIC_GFXACTMHz))
		p->gfx_act_mhz = gfx_info[GFX_ACTMHz].val;

	if (DO_BIC(BIC_SAM_mc6))
		p->sam_mc6_ms = gfx_info[SAM_mc6].val_ull;

	if (DO_BIC(BIC_SAMMHz))
		p->sam_mhz = gfx_info[SAM_MHz].val;

	if (DO_BIC(BIC_SAMACTMHz))
		p->sam_act_mhz = gfx_info[SAM_ACTMHz].val;

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		char *path = NULL;

		if (mp->msr_num == 0) {
			path = find_sysfs_path_by_id(mp->sp, p->package_id);
			if (path == NULL) {
				warnx("%s: package_id %d not found", __func__, p->package_id);
				return -10;
			}
		}
		if (get_mp(cpu, mp, &p->counter[i], path))
			return -10;
	}

	if (perf_counter_info_read_values(sys.perf_pp, cpu, p->perf_counter, MAX_ADDED_PACKAGE_COUNTERS))
		return -10;

	for (i = 0, pp = sys.pmt_pp; pp; i++, pp = pp->next)
		p->pmt_counter[i] = pmt_read_counter(pp, p->package_id);

done:
	gettimeofday(&t->tv_end, (struct timezone *)NULL);

	return 0;
}

int pkg_cstate_limit = PCLUKN;
char *pkg_cstate_limit_strings[] = { "unknown", "reserved", "pc0", "pc1", "pc2",
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

void probe_cst_limit(void)
{
	unsigned long long msr;
	int *pkg_cstate_limits;

	if (!platform->has_nhm_msrs || no_msr)
		return;

	switch (platform->cst_limit) {
	case CST_LIMIT_NHM:
		pkg_cstate_limits = nhm_pkg_cstate_limits;
		break;
	case CST_LIMIT_SNB:
		pkg_cstate_limits = snb_pkg_cstate_limits;
		break;
	case CST_LIMIT_HSW:
		pkg_cstate_limits = hsw_pkg_cstate_limits;
		break;
	case CST_LIMIT_SKX:
		pkg_cstate_limits = skx_pkg_cstate_limits;
		break;
	case CST_LIMIT_ICX:
		pkg_cstate_limits = icx_pkg_cstate_limits;
		break;
	case CST_LIMIT_SLV:
		pkg_cstate_limits = slv_pkg_cstate_limits;
		break;
	case CST_LIMIT_AMT:
		pkg_cstate_limits = amt_pkg_cstate_limits;
		break;
	case CST_LIMIT_KNL:
		pkg_cstate_limits = phi_pkg_cstate_limits;
		break;
	case CST_LIMIT_GMT:
		pkg_cstate_limits = glm_pkg_cstate_limits;
		break;
	default:
		return;
	}

	get_msr(base_cpu, MSR_PKG_CST_CONFIG_CONTROL, &msr);
	pkg_cstate_limit = pkg_cstate_limits[msr & 0xF];
}

static void dump_platform_info(void)
{
	unsigned long long msr;
	unsigned int ratio;

	if (!platform->has_nhm_msrs || no_msr)
		return;

	get_msr(base_cpu, MSR_PLATFORM_INFO, &msr);

	fprintf(outf, "cpu%d: MSR_PLATFORM_INFO: 0x%08llx\n", base_cpu, msr);

	ratio = (msr >> 40) & 0xFF;
	fprintf(outf, "%d * %.1f = %.1f MHz max efficiency frequency\n", ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0xFF;
	fprintf(outf, "%d * %.1f = %.1f MHz base frequency\n", ratio, bclk, ratio * bclk);
}

static void dump_power_ctl(void)
{
	unsigned long long msr;

	if (!platform->has_nhm_msrs || no_msr)
		return;

	get_msr(base_cpu, MSR_IA32_POWER_CTL, &msr);
	fprintf(outf, "cpu%d: MSR_IA32_POWER_CTL: 0x%08llx (C1E auto-promotion: %sabled)\n",
		base_cpu, msr, msr & 0x2 ? "EN" : "DIS");

	/* C-state Pre-wake Disable (CSTATE_PREWAKE_DISABLE) */
	if (platform->has_cst_prewake_bit)
		fprintf(outf, "C-state Pre-wake: %sabled\n", msr & 0x40000000 ? "DIS" : "EN");

	return;
}

static void dump_turbo_ratio_limit2(void)
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

static void dump_turbo_ratio_limit1(void)
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

static void dump_turbo_ratio_limits(int trl_msr_offset)
{
	unsigned long long msr, core_counts;
	int shift;

	get_msr(base_cpu, trl_msr_offset, &msr);
	fprintf(outf, "cpu%d: MSR_%sTURBO_RATIO_LIMIT: 0x%08llx\n",
		base_cpu, trl_msr_offset == MSR_SECONDARY_TURBO_RATIO_LIMIT ? "SECONDARY_" : "", msr);

	if (platform->trl_msrs & TRL_CORECOUNT) {
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

static void dump_cst_cfg(void)
{
	unsigned long long msr;

	if (!platform->has_nhm_msrs || no_msr)
		return;

	get_msr(base_cpu, MSR_PKG_CST_CONFIG_CONTROL, &msr);

	fprintf(outf, "cpu%d: MSR_PKG_CST_CONFIG_CONTROL: 0x%08llx", base_cpu, msr);

	fprintf(outf, " (%s%s%s%s%slocked, pkg-cstate-limit=%d (%s)",
		(msr & SNB_C3_AUTO_UNDEMOTE) ? "UNdemote-C3, " : "",
		(msr & SNB_C1_AUTO_UNDEMOTE) ? "UNdemote-C1, " : "",
		(msr & NHM_C3_AUTO_DEMOTE) ? "demote-C3, " : "",
		(msr & NHM_C1_AUTO_DEMOTE) ? "demote-C1, " : "",
		(msr & (1 << 15)) ? "" : "UN", (unsigned int)msr & 0xF, pkg_cstate_limit_strings[pkg_cstate_limit]);

#define AUTOMATIC_CSTATE_CONVERSION		(1UL << 16)
	if (platform->has_cst_auto_convension) {
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

	if (!platform->has_irtl_msrs || no_msr)
		return;

	if (platform->supported_cstates & PC3) {
		get_msr(base_cpu, MSR_PKGC3_IRTL, &msr);
		fprintf(outf, "cpu%d: MSR_PKGC3_IRTL: 0x%08llx (", base_cpu, msr);
		fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
			(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);
	}

	if (platform->supported_cstates & PC6) {
		get_msr(base_cpu, MSR_PKGC6_IRTL, &msr);
		fprintf(outf, "cpu%d: MSR_PKGC6_IRTL: 0x%08llx (", base_cpu, msr);
		fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
			(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);
	}

	if (platform->supported_cstates & PC7) {
		get_msr(base_cpu, MSR_PKGC7_IRTL, &msr);
		fprintf(outf, "cpu%d: MSR_PKGC7_IRTL: 0x%08llx (", base_cpu, msr);
		fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
			(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);
	}

	if (platform->supported_cstates & PC8) {
		get_msr(base_cpu, MSR_PKGC8_IRTL, &msr);
		fprintf(outf, "cpu%d: MSR_PKGC8_IRTL: 0x%08llx (", base_cpu, msr);
		fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
			(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);
	}

	if (platform->supported_cstates & PC9) {
		get_msr(base_cpu, MSR_PKGC9_IRTL, &msr);
		fprintf(outf, "cpu%d: MSR_PKGC9_IRTL: 0x%08llx (", base_cpu, msr);
		fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
			(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);
	}

	if (platform->supported_cstates & PC10) {
		get_msr(base_cpu, MSR_PKGC10_IRTL, &msr);
		fprintf(outf, "cpu%d: MSR_PKGC10_IRTL: 0x%08llx (", base_cpu, msr);
		fprintf(outf, "%svalid, %lld ns)\n", msr & (1 << 15) ? "" : "NOT",
			(msr & 0x3FF) * irtl_time_units[(msr >> 10) & 0x3]);
	}
}

void free_fd_percpu(void)
{
	int i;

	if (!fd_percpu)
		return;

	for (i = 0; i < topo.max_cpu_num + 1; ++i) {
		if (fd_percpu[i] != 0)
			close(fd_percpu[i]);
	}

	free(fd_percpu);
	fd_percpu = NULL;
}

void free_fd_instr_count_percpu(void)
{
	if (!fd_instr_count_percpu)
		return;

	for (int i = 0; i < topo.max_cpu_num + 1; ++i) {
		if (fd_instr_count_percpu[i] != 0)
			close(fd_instr_count_percpu[i]);
	}

	free(fd_instr_count_percpu);
	fd_instr_count_percpu = NULL;
}

void free_fd_cstate(void)
{
	if (!ccstate_counter_info)
		return;

	const int counter_info_num = ccstate_counter_info_size;

	for (int counter_id = 0; counter_id < counter_info_num; ++counter_id) {
		if (ccstate_counter_info[counter_id].fd_perf_core != -1)
			close(ccstate_counter_info[counter_id].fd_perf_core);

		if (ccstate_counter_info[counter_id].fd_perf_pkg != -1)
			close(ccstate_counter_info[counter_id].fd_perf_pkg);
	}

	free(ccstate_counter_info);
	ccstate_counter_info = NULL;
	ccstate_counter_info_size = 0;
}

void free_fd_msr(void)
{
	if (!msr_counter_info)
		return;

	for (int cpu = 0; cpu < topo.max_cpu_num; ++cpu) {
		if (msr_counter_info[cpu].fd_perf != -1)
			close(msr_counter_info[cpu].fd_perf);
	}

	free(msr_counter_info);
	msr_counter_info = NULL;
	msr_counter_info_size = 0;
}

void free_fd_rapl_percpu(void)
{
	if (!rapl_counter_info_perdomain)
		return;

	const int num_domains = rapl_counter_info_perdomain_size;

	for (int domain_id = 0; domain_id < num_domains; ++domain_id) {
		if (rapl_counter_info_perdomain[domain_id].fd_perf != -1)
			close(rapl_counter_info_perdomain[domain_id].fd_perf);
	}

	free(rapl_counter_info_perdomain);
	rapl_counter_info_perdomain = NULL;
	rapl_counter_info_perdomain_size = 0;
}

void free_fd_added_perf_counters_(struct perf_counter_info *pp)
{
	if (!pp)
		return;

	if (!pp->fd_perf_per_domain)
		return;

	while (pp) {
		for (size_t domain = 0; domain < pp->num_domains; ++domain) {
			if (pp->fd_perf_per_domain[domain] != -1) {
				close(pp->fd_perf_per_domain[domain]);
				pp->fd_perf_per_domain[domain] = -1;
			}
		}

		free(pp->fd_perf_per_domain);
		pp->fd_perf_per_domain = NULL;

		pp = pp->next;
	}
}

void free_fd_added_perf_counters(void)
{
	free_fd_added_perf_counters_(sys.perf_tp);
	free_fd_added_perf_counters_(sys.perf_cp);
	free_fd_added_perf_counters_(sys.perf_pp);
}

void free_all_buffers(void)
{
	int i;

	CPU_FREE(cpu_present_set);
	cpu_present_set = NULL;
	cpu_present_setsize = 0;

	CPU_FREE(cpu_effective_set);
	cpu_effective_set = NULL;
	cpu_effective_setsize = 0;

	CPU_FREE(cpu_allowed_set);
	cpu_allowed_set = NULL;
	cpu_allowed_setsize = 0;

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
	free_fd_instr_count_percpu();
	free_fd_msr();
	free_fd_rapl_percpu();
	free_fd_cstate();
	free_fd_added_perf_counters();

	free(irq_column_2_cpu);
	free(irqs_per_cpu);
	free(nmi_per_cpu);

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

static int parse_cpu_str(char *cpu_str, cpu_set_t *cpu_set, int cpu_set_size)
{
	unsigned int start, end;
	char *next = cpu_str;

	while (next && *next) {

		if (*next == '-')	/* no negative cpu numbers */
			return 1;

		if (*next == '\0' || *next == '\n')
			break;

		start = strtoul(next, &next, 10);

		if (start >= CPU_SUBSET_MAXCPUS)
			return 1;
		CPU_SET_S(start, cpu_set_size, cpu_set);

		if (*next == '\0' || *next == '\n')
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
				return 1;
		}

		end = strtoul(next, &next, 10);
		if (end <= start)
			return 1;

		while (++start <= end) {
			if (start >= CPU_SUBSET_MAXCPUS)
				return 1;
			CPU_SET_S(start, cpu_set_size, cpu_set);
		}

		if (*next == ',')
			next += 1;
		else if (*next != '\0' && *next != '\n')
			return 1;
	}

	return 0;
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

	retval = 0;

	for (pkg_no = 0; pkg_no < topo.num_packages; ++pkg_no) {
		for (node_no = 0; node_no < topo.nodes_per_pkg; ++node_no) {
			for (core_no = 0; core_no < topo.cores_per_node; ++core_no) {
				for (thread_no = 0; thread_no < topo.threads_per_core; ++thread_no) {
					struct thread_data *t, *t2;
					struct core_data *c, *c2;
					struct pkg_data *p, *p2;

					t = GET_THREAD(thread_base, thread_no, core_no, node_no, pkg_no);

					if (cpu_is_not_allowed(t->cpu_id))
						continue;

					t2 = GET_THREAD(thread_base2, thread_no, core_no, node_no, pkg_no);

					c = GET_CORE(core_base, core_no, node_no, pkg_no);
					c2 = GET_CORE(core_base2, core_no, node_no, pkg_no);

					p = GET_PKG(pkg_base, pkg_no);
					p2 = GET_PKG(pkg_base2, pkg_no);

					retval |= func(t, c, p, t2, c2, p2);
				}
			}
		}
	}
	return retval;
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

#define PATH_EFFECTIVE_CPUS	"/sys/fs/cgroup/cpuset.cpus.effective"

static char cpu_effective_str[1024];

static int update_effective_str(bool startup)
{
	FILE *fp;
	char *pos;
	char buf[1024];
	int ret;

	if (cpu_effective_str[0] == '\0' && !startup)
		return 0;

	fp = fopen(PATH_EFFECTIVE_CPUS, "r");
	if (!fp)
		return 0;

	pos = fgets(buf, 1024, fp);
	if (!pos)
		err(1, "%s: file read failed\n", PATH_EFFECTIVE_CPUS);

	fclose(fp);

	ret = strncmp(cpu_effective_str, buf, 1024);
	if (!ret)
		return 0;

	strncpy(cpu_effective_str, buf, 1024);
	return 1;
}

static void update_effective_set(bool startup)
{
	update_effective_str(startup);

	if (parse_cpu_str(cpu_effective_str, cpu_effective_set, cpu_effective_setsize))
		err(1, "%s: cpu str malformat %s\n", PATH_EFFECTIVE_CPUS, cpu_effective_str);
}

void linux_perf_init(void);
void msr_perf_init(void);
void rapl_perf_init(void);
void cstate_perf_init(void);
void added_perf_counters_init(void);
void pmt_init(void);

void re_initialize(void)
{
	free_all_buffers();
	setup_all_buffers(false);
	linux_perf_init();
	msr_perf_init();
	rapl_perf_init();
	cstate_perf_init();
	added_perf_counters_init();
	pmt_init();
	fprintf(outf, "turbostat: re-initialized with num_cpus %d, allowed_cpus %d\n", topo.num_cpus,
		topo.allowed_cpus);
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

int set_my_cpu_type(void)
{
	unsigned int eax, ebx, ecx, edx;
	unsigned int max_level;

	__cpuid(0, max_level, ebx, ecx, edx);

	if (max_level < CPUID_LEAF_MODEL_ID)
		return 0;

	__cpuid(CPUID_LEAF_MODEL_ID, eax, ebx, ecx, edx);

	return (eax >> CPUID_LEAF_MODEL_ID_CORE_TYPE_SHIFT);
}

int set_cpu_hybrid_type(int cpu)
{
	if (cpu_migrate(cpu))
		return -1;

	int type = set_my_cpu_type();

	cpus[cpu].type = type;
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
		nmi_per_cpu[cpu_number] = 0;
	}

	/* read /proc/interrupt count lines and sum up irqs per cpu */
	while (1) {
		int column;
		char buf[64];
		int this_row_is_nmi = 0;

		retval = fscanf(fp, " %s:", buf);	/* irq# "N:" */
		if (retval != 1)
			break;

		if (strncmp(buf, "NMI", strlen("NMI")) == 0)
			this_row_is_nmi = 1;

		/* read the count per cpu */
		for (column = 0; column < topo.num_cpus; ++column) {

			int cpu_number, irq_count;

			retval = fscanf(fp, " %d", &irq_count);

			if (retval != 1)
				break;

			cpu_number = irq_column_2_cpu[column];
			irqs_per_cpu[cpu_number] += irq_count;
			if (this_row_is_nmi)
				nmi_per_cpu[cpu_number] += irq_count;
		}
		while (getc(fp) != '\n') ;	/* flush interrupt description */

	}
	return 0;
}

/*
 * snapshot_graphics()
 *
 * record snapshot of specified graphics sysfs knob
 *
 * return 1 if config change requires a restart, else return 0
 */
int snapshot_graphics(int idx)
{
	int retval;

	rewind(gfx_info[idx].fp);

	switch (idx) {
	case GFX_rc6:
	case SAM_mc6:
		retval = fscanf(gfx_info[idx].fp, "%lld", &gfx_info[idx].val_ull);
		if (retval != 1)
			err(1, "rc6");
		return 0;
	case GFX_MHz:
	case GFX_ACTMHz:
	case SAM_MHz:
	case SAM_ACTMHz:
		retval = fscanf(gfx_info[idx].fp, "%d", &gfx_info[idx].val);
		if (retval != 1)
			err(1, "MHz");
		return 0;
	default:
		return -EINVAL;
	}
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
	gettimeofday(&procsysfs_tv_begin, (struct timezone *)NULL);

	if (DO_BIC(BIC_IRQ) || DO_BIC(BIC_NMI))
		if (snapshot_proc_interrupts())
			return 1;

	if (DO_BIC(BIC_GFX_rc6))
		snapshot_graphics(GFX_rc6);

	if (DO_BIC(BIC_GFXMHz))
		snapshot_graphics(GFX_MHz);

	if (DO_BIC(BIC_GFXACTMHz))
		snapshot_graphics(GFX_ACTMHz);

	if (DO_BIC(BIC_SAM_mc6))
		snapshot_graphics(SAM_mc6);

	if (DO_BIC(BIC_SAMMHz))
		snapshot_graphics(SAM_MHz);

	if (DO_BIC(BIC_SAMACTMHz))
		snapshot_graphics(SAM_ACTMHz);

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

	assert(!no_msr);

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

	assert(!no_msr);

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
 * return previous priority on success
 * return value < -20 on failure
 */
int set_my_sched_priority(int priority)
{
	int retval;
	int original_priority;

	errno = 0;
	original_priority = getpriority(PRIO_PROCESS, 0);
	if (errno && (original_priority == -1))
		return -21;

	retval = setpriority(PRIO_PROCESS, 0, priority);
	if (retval)
		return -21;

	errno = 0;
	retval = getpriority(PRIO_PROCESS, 0);
	if (retval != priority)
		return -21;

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
	 *
	 * ignore on error - we probably don't have permission to set it, but
	 * it's not a big deal
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
		if (update_effective_str(false)) {
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
		delta_platform(&platform_counters_odd, &platform_counters_even);
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
		delta_platform(&platform_counters_even, &platform_counters_odd);
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

	if (no_msr)
		return;

	sprintf(pathname, "/dev/cpu/%d/msr", base_cpu);
	if (stat(pathname, &sb))
		if (system("/sbin/modprobe msr > /dev/null 2>&1"))
			no_msr = 1;
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
	int ret = 0;

	caps = cap_get_proc();
	if (caps == NULL)
		return 1;

	if (cap_get_flag(caps, CAP_SYS_RAWIO, CAP_EFFECTIVE, &cap_flag_value)) {
		ret = 1;
		goto free_and_exit;
	}

	if (cap_flag_value != CAP_SET) {
		ret = 1;
		goto free_and_exit;
	}

free_and_exit:
	if (cap_free(caps) == -1)
		err(-6, "cap_free\n");

	return ret;
}

void check_msr_permission(void)
{
	int failed = 0;
	char pathname[32];

	if (no_msr)
		return;

	/* check for CAP_SYS_RAWIO */
	failed += check_for_cap_sys_rawio();

	/* test file permissions */
	sprintf(pathname, "/dev/cpu/%d/msr", base_cpu);
	if (euidaccess(pathname, R_OK)) {
		failed++;
	}

	if (failed) {
		warnx("Failed to access %s. Some of the counters may not be available\n"
		      "\tRun as root to enable them or use %s to disable the access explicitly", pathname, "--no-msr");
		no_msr = 1;
	}
}

void probe_bclk(void)
{
	unsigned long long msr;
	unsigned int base_ratio;

	if (!platform->has_nhm_msrs || no_msr)
		return;

	if (platform->bclk_freq == BCLK_100MHZ)
		bclk = 100.00;
	else if (platform->bclk_freq == BCLK_133MHZ)
		bclk = 133.33;
	else if (platform->bclk_freq == BCLK_SLV)
		bclk = slm_bclk();
	else
		return;

	get_msr(base_cpu, MSR_PLATFORM_INFO, &msr);
	base_ratio = (msr >> 8) & 0xFF;

	base_hz = base_ratio * bclk * 1000000;
	has_base_hz = 1;

	if (platform->enable_tsc_tweak)
		tsc_tweak = base_hz / tsc_hz;
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

static void dump_turbo_ratio_info(void)
{
	if (!has_turbo)
		return;

	if (!platform->has_nhm_msrs || no_msr)
		return;

	if (platform->trl_msrs & TRL_LIMIT2)
		dump_turbo_ratio_limit2();

	if (platform->trl_msrs & TRL_LIMIT1)
		dump_turbo_ratio_limit1();

	if (platform->trl_msrs & TRL_BASE) {
		dump_turbo_ratio_limits(MSR_TURBO_RATIO_LIMIT);

		if (is_hybrid)
			dump_turbo_ratio_limits(MSR_SECONDARY_TURBO_RATIO_LIMIT);
	}

	if (platform->trl_msrs & TRL_ATOM)
		dump_atom_turbo_ratio_limits();

	if (platform->trl_msrs & TRL_KNL)
		dump_knl_turbo_ratio_limits();

	if (platform->has_config_tdp)
		dump_config_tdp();
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

static void probe_intel_uncore_frequency_legacy(void)
{
	int i, j;
	char path[256];

	for (i = 0; i < topo.num_packages; ++i) {
		for (j = 0; j <= topo.max_die_id; ++j) {
			int k, l;
			char path_base[128];

			sprintf(path_base, "/sys/devices/system/cpu/intel_uncore_frequency/package_%02d_die_%02d", i,
				j);

			if (access(path_base, R_OK))
				continue;

			BIC_PRESENT(BIC_UNCORE_MHZ);

			if (quiet)
				return;

			sprintf(path, "%s/min_freq_khz", path_base);
			k = read_sysfs_int(path);
			sprintf(path, "%s/max_freq_khz", path_base);
			l = read_sysfs_int(path);
			fprintf(outf, "Uncore Frequency package%d die%d: %d - %d MHz ", i, j, k / 1000, l / 1000);

			sprintf(path, "%s/initial_min_freq_khz", path_base);
			k = read_sysfs_int(path);
			sprintf(path, "%s/initial_max_freq_khz", path_base);
			l = read_sysfs_int(path);
			fprintf(outf, "(%d - %d MHz)", k / 1000, l / 1000);

			sprintf(path, "%s/current_freq_khz", path_base);
			k = read_sysfs_int(path);
			fprintf(outf, " %d MHz\n", k / 1000);
		}
	}
}

static void probe_intel_uncore_frequency_cluster(void)
{
	int i, uncore_max_id;
	char path[256];
	char path_base[128];

	if (access("/sys/devices/system/cpu/intel_uncore_frequency/uncore00/current_freq_khz", R_OK))
		return;

	for (uncore_max_id = 0;; ++uncore_max_id) {

		sprintf(path_base, "/sys/devices/system/cpu/intel_uncore_frequency/uncore%02d", uncore_max_id);

		/* uncore## start at 00 and skips no numbers, so stop upon first missing */
		if (access(path_base, R_OK)) {
			uncore_max_id -= 1;
			break;
		}
	}
	for (i = uncore_max_id; i >= 0; --i) {
		int k, l;
		int package_id, domain_id, cluster_id;
		char name_buf[16];

		sprintf(path_base, "/sys/devices/system/cpu/intel_uncore_frequency/uncore%02d", i);

		if (access(path_base, R_OK))
			err(1, "%s: %s\n", __func__, path_base);

		sprintf(path, "%s/package_id", path_base);
		package_id = read_sysfs_int(path);

		sprintf(path, "%s/domain_id", path_base);
		domain_id = read_sysfs_int(path);

		sprintf(path, "%s/fabric_cluster_id", path_base);
		cluster_id = read_sysfs_int(path);

		sprintf(path, "%s/current_freq_khz", path_base);
		sprintf(name_buf, "UMHz%d.%d", domain_id, cluster_id);

		add_counter(0, path, name_buf, 0, SCOPE_PACKAGE, COUNTER_K2M, FORMAT_AVERAGE, 0, package_id);

		if (quiet)
			continue;

		sprintf(path, "%s/min_freq_khz", path_base);
		k = read_sysfs_int(path);
		sprintf(path, "%s/max_freq_khz", path_base);
		l = read_sysfs_int(path);
		fprintf(outf, "Uncore Frequency package%d domain%d cluster%d: %d - %d MHz ", package_id, domain_id,
			cluster_id, k / 1000, l / 1000);

		sprintf(path, "%s/initial_min_freq_khz", path_base);
		k = read_sysfs_int(path);
		sprintf(path, "%s/initial_max_freq_khz", path_base);
		l = read_sysfs_int(path);
		fprintf(outf, "(%d - %d MHz)", k / 1000, l / 1000);

		sprintf(path, "%s/current_freq_khz", path_base);
		k = read_sysfs_int(path);
		fprintf(outf, " %d MHz\n", k / 1000);
	}
}

static void probe_intel_uncore_frequency(void)
{
	if (!genuine_intel)
		return;

	if (access("/sys/devices/system/cpu/intel_uncore_frequency/uncore00", R_OK) == 0)
		probe_intel_uncore_frequency_cluster();
	else
		probe_intel_uncore_frequency_legacy();
}

static void set_graphics_fp(char *path, int idx)
{
	if (!access(path, R_OK))
		gfx_info[idx].fp = fopen_or_die(path, "r");
}

/* Enlarge this if there are /sys/class/drm/card2 ... */
#define GFX_MAX_CARDS	2

static void probe_graphics(void)
{
	char path[PATH_MAX];
	int i;

	/* Xe graphics sysfs knobs */
	if (!access("/sys/class/drm/card0/device/tile0/gt0/gtidle/idle_residency_ms", R_OK)) {
		FILE *fp;
		char buf[8];
		bool gt0_is_gt;

		fp = fopen("/sys/class/drm/card0/device/tile0/gt0/gtidle/name", "r");
		if (!fp)
			goto next;

		if (!fread(buf, sizeof(char), 7, fp)) {
			fclose(fp);
			goto next;
		}
		fclose(fp);

		if (!strncmp(buf, "gt0-rc", strlen("gt0-rc")))
			gt0_is_gt = true;
		else if (!strncmp(buf, "gt0-mc", strlen("gt0-mc")))
			gt0_is_gt = false;
		else
			goto next;

		set_graphics_fp("/sys/class/drm/card0/device/tile0/gt0/gtidle/idle_residency_ms", gt0_is_gt ? GFX_rc6 : SAM_mc6);

		set_graphics_fp("/sys/class/drm/card0/device/tile0/gt0/freq0/cur_freq", gt0_is_gt ? GFX_MHz : SAM_MHz);

		set_graphics_fp("/sys/class/drm/card0/device/tile0/gt0/freq0/act_freq", gt0_is_gt ? GFX_ACTMHz : SAM_ACTMHz);

		set_graphics_fp("/sys/class/drm/card0/device/tile0/gt1/gtidle/idle_residency_ms", gt0_is_gt ? SAM_mc6 : GFX_rc6);

		set_graphics_fp("/sys/class/drm/card0/device/tile0/gt1/freq0/cur_freq", gt0_is_gt ? SAM_MHz : GFX_MHz);

		set_graphics_fp("/sys/class/drm/card0/device/tile0/gt1/freq0/act_freq", gt0_is_gt ? SAM_ACTMHz : GFX_ACTMHz);

		goto end;
	}

next:
	/* New i915 graphics sysfs knobs */
	for (i = 0; i < GFX_MAX_CARDS; i++) {
		snprintf(path, PATH_MAX, "/sys/class/drm/card%d/gt/gt0/rc6_residency_ms", i);
		if (!access(path, R_OK))
			break;
	}

	if (i == GFX_MAX_CARDS)
		goto legacy_i915;

	snprintf(path, PATH_MAX, "/sys/class/drm/card%d/gt/gt0/rc6_residency_ms", i);
	set_graphics_fp(path, GFX_rc6);

	snprintf(path, PATH_MAX, "/sys/class/drm/card%d/gt/gt0/rps_cur_freq_mhz", i);
	set_graphics_fp(path, GFX_MHz);

	snprintf(path, PATH_MAX, "/sys/class/drm/card%d/gt/gt0/rps_act_freq_mhz", i);
	set_graphics_fp(path, GFX_ACTMHz);

	snprintf(path, PATH_MAX, "/sys/class/drm/card%d/gt/gt1/rc6_residency_ms", i);
	set_graphics_fp(path, SAM_mc6);

	snprintf(path, PATH_MAX, "/sys/class/drm/card%d/gt/gt1/rps_cur_freq_mhz", i);
	set_graphics_fp(path, SAM_MHz);

	snprintf(path, PATH_MAX, "/sys/class/drm/card%d/gt/gt1/rps_act_freq_mhz", i);
	set_graphics_fp(path, SAM_ACTMHz);

	goto end;

legacy_i915:
	/* Fall back to traditional i915 graphics sysfs knobs */
	set_graphics_fp("/sys/class/drm/card0/power/rc6_residency_ms", GFX_rc6);

	set_graphics_fp("/sys/class/drm/card0/gt_cur_freq_mhz", GFX_MHz);
	if (!gfx_info[GFX_MHz].fp)
		set_graphics_fp("/sys/class/graphics/fb0/device/drm/card0/gt_cur_freq_mhz", GFX_MHz);

	set_graphics_fp("/sys/class/drm/card0/gt_act_freq_mhz", GFX_ACTMHz);
	if (!gfx_info[GFX_ACTMHz].fp)
		set_graphics_fp("/sys/class/graphics/fb0/device/drm/card0/gt_act_freq_mhz", GFX_ACTMHz);

end:
	if (gfx_info[GFX_rc6].fp)
		BIC_PRESENT(BIC_GFX_rc6);
	if (gfx_info[GFX_MHz].fp)
		BIC_PRESENT(BIC_GFXMHz);
	if (gfx_info[GFX_ACTMHz].fp)
		BIC_PRESENT(BIC_GFXACTMHz);
	if (gfx_info[SAM_mc6].fp)
		BIC_PRESENT(BIC_SAM_mc6);
	if (gfx_info[SAM_MHz].fp)
		BIC_PRESENT(BIC_SAMMHz);
	if (gfx_info[SAM_ACTMHz].fp)
		BIC_PRESENT(BIC_SAMACTMHz);
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
	if (!is_cpu_first_thread_in_package(t, c, p))
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

	if (no_msr)
		return 0;

	if (!has_hwp)
		return 0;

	cpu = t->cpu_id;

	/* MSR_HWP_CAPABILITIES is per-package */
	if (!is_cpu_first_thread_in_package(t, c, p))
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

	if (no_msr)
		return 0;

	cpu = t->cpu_id;

	/* per-package */
	if (!is_cpu_first_thread_in_package(t, c, p))
		return 0;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "print_perf_limit: Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (platform->plr_msrs & PLR_CORE) {
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
	if (platform->plr_msrs & PLR_GFX) {
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
	if (platform->plr_msrs & PLR_RING) {
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

double get_quirk_tdp(void)
{
	if (platform->rapl_quirk_tdp)
		return platform->rapl_quirk_tdp;

	return 135.0;
}

double get_tdp_intel(void)
{
	unsigned long long msr;

	if (platform->rapl_msrs & RAPL_PKG_POWER_INFO)
		if (!get_msr(base_cpu, MSR_PKG_POWER_INFO, &msr))
			return ((msr >> 0) & RAPL_POWER_GRANULARITY) * rapl_power_units;
	return get_quirk_tdp();
}

double get_tdp_amd(void)
{
	return get_quirk_tdp();
}

void rapl_probe_intel(void)
{
	unsigned long long msr;
	unsigned int time_unit;
	double tdp;
	const unsigned long long bic_watt_bits = BIC_SysWatt | BIC_PkgWatt | BIC_CorWatt | BIC_RAMWatt | BIC_GFXWatt;
	const unsigned long long bic_joules_bits = BIC_Sys_J | BIC_Pkg_J | BIC_Cor_J | BIC_RAM_J | BIC_GFX_J;

	if (rapl_joules)
		bic_enabled &= ~bic_watt_bits;
	else
		bic_enabled &= ~bic_joules_bits;

	if (!(platform->rapl_msrs & RAPL_PKG_PERF_STATUS))
		bic_enabled &= ~BIC_PKG__;
	if (!(platform->rapl_msrs & RAPL_DRAM_PERF_STATUS))
		bic_enabled &= ~BIC_RAM__;

	/* units on package 0, verify later other packages match */
	if (get_msr(base_cpu, MSR_RAPL_POWER_UNIT, &msr))
		return;

	rapl_power_units = 1.0 / (1 << (msr & 0xF));
	if (platform->has_rapl_divisor)
		rapl_energy_units = 1.0 * (1 << (msr >> 8 & 0x1F)) / 1000000;
	else
		rapl_energy_units = 1.0 / (1 << (msr >> 8 & 0x1F));

	if (platform->has_fixed_rapl_unit)
		rapl_dram_energy_units = (15.3 / 1000000);
	else
		rapl_dram_energy_units = rapl_energy_units;

	if (platform->has_fixed_rapl_psys_unit)
		rapl_psys_energy_units = 1.0;
	else
		rapl_psys_energy_units = rapl_energy_units;

	time_unit = msr >> 16 & 0xF;
	if (time_unit == 0)
		time_unit = 0xA;

	rapl_time_units = 1.0 / (1 << (time_unit));

	tdp = get_tdp_intel();

	rapl_joule_counter_range = 0xFFFFFFFF * rapl_energy_units / tdp;
	if (!quiet)
		fprintf(outf, "RAPL: %.0f sec. Joule Counter Range, at %.0f Watts\n", rapl_joule_counter_range, tdp);
}

void rapl_probe_amd(void)
{
	unsigned long long msr;
	double tdp;
	const unsigned long long bic_watt_bits = BIC_PkgWatt | BIC_CorWatt;
	const unsigned long long bic_joules_bits = BIC_Pkg_J | BIC_Cor_J;

	if (rapl_joules)
		bic_enabled &= ~bic_watt_bits;
	else
		bic_enabled &= ~bic_joules_bits;

	if (get_msr(base_cpu, MSR_RAPL_PWR_UNIT, &msr))
		return;

	rapl_time_units = ldexp(1.0, -(msr >> 16 & 0xf));
	rapl_energy_units = ldexp(1.0, -(msr >> 8 & 0x1f));
	rapl_power_units = ldexp(1.0, -(msr & 0xf));

	tdp = get_tdp_amd();

	rapl_joule_counter_range = 0xFFFFFFFF * rapl_energy_units / tdp;
	if (!quiet)
		fprintf(outf, "RAPL: %.0f sec. Joule Counter Range, at %.0f Watts\n", rapl_joule_counter_range, tdp);
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

	if (!platform->rapl_msrs)
		return 0;

	/* RAPL counters are per package, so print only for 1st thread/package */
	if (!is_cpu_first_thread_in_package(t, c, p))
		return 0;

	cpu = t->cpu_id;
	if (cpu_migrate(cpu)) {
		fprintf(outf, "print_rapl: Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (platform->rapl_msrs & RAPL_AMD_F17H) {
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

	if (platform->rapl_msrs & RAPL_PKG_POWER_INFO) {

		if (get_msr(cpu, MSR_PKG_POWER_INFO, &msr))
			return -5;

		fprintf(outf, "cpu%d: MSR_PKG_POWER_INFO: 0x%08llx (%.0f W TDP, RAPL %.0f - %.0f W, %f sec.)\n",
			cpu, msr,
			((msr >> 0) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 16) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 32) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 48) & RAPL_TIME_GRANULARITY) * rapl_time_units);

	}
	if (platform->rapl_msrs & RAPL_PKG) {

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

	if (platform->rapl_msrs & RAPL_DRAM_POWER_INFO) {
		if (get_msr(cpu, MSR_DRAM_POWER_INFO, &msr))
			return -6;

		fprintf(outf, "cpu%d: MSR_DRAM_POWER_INFO,: 0x%08llx (%.0f W TDP, RAPL %.0f - %.0f W, %f sec.)\n",
			cpu, msr,
			((msr >> 0) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 16) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 32) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 48) & RAPL_TIME_GRANULARITY) * rapl_time_units);
	}
	if (platform->rapl_msrs & RAPL_DRAM) {
		if (get_msr(cpu, MSR_DRAM_POWER_LIMIT, &msr))
			return -9;
		fprintf(outf, "cpu%d: MSR_DRAM_POWER_LIMIT: 0x%08llx (%slocked)\n",
			cpu, msr, (msr >> 31) & 1 ? "" : "UN");

		print_power_limit_msr(cpu, msr, "DRAM Limit");
	}
	if (platform->rapl_msrs & RAPL_CORE_POLICY) {
		if (get_msr(cpu, MSR_PP0_POLICY, &msr))
			return -7;

		fprintf(outf, "cpu%d: MSR_PP0_POLICY: %lld\n", cpu, msr & 0xF);
	}
	if (platform->rapl_msrs & RAPL_CORE_POWER_LIMIT) {
		if (get_msr(cpu, MSR_PP0_POWER_LIMIT, &msr))
			return -9;
		fprintf(outf, "cpu%d: MSR_PP0_POWER_LIMIT: 0x%08llx (%slocked)\n",
			cpu, msr, (msr >> 31) & 1 ? "" : "UN");
		print_power_limit_msr(cpu, msr, "Cores Limit");
	}
	if (platform->rapl_msrs & RAPL_GFX) {
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
 * probe_rapl()
 *
 * sets rapl_power_units, rapl_energy_units, rapl_time_units
 */
void probe_rapl(void)
{
	if (!platform->rapl_msrs || no_msr)
		return;

	if (genuine_intel)
		rapl_probe_intel();
	if (authentic_amd || hygon_genuine)
		rapl_probe_amd();

	if (quiet)
		return;

	for_all_cpus(print_rapl, ODD_COUNTERS);
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
	if (!is_cpu_first_thread_in_package(t, c, p))
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
	if (!platform->has_nhm_msrs || no_msr)
		goto guess;

	if (get_msr(base_cpu, MSR_IA32_TEMPERATURE_TARGET, &msr))
		goto guess;

	tcc_default = (msr >> 16) & 0xFF;

	if (!quiet) {
		int bits = platform->tcc_offset_bits;
		unsigned long long enabled = 0;

		if (bits && !get_msr(base_cpu, MSR_PLATFORM_INFO, &enabled))
			enabled = (enabled >> 30) & 1;

		if (bits && enabled) {
			tcc_offset = (msr >> 24) & GENMASK(bits - 1, 0);
			fprintf(outf, "cpu%d: MSR_IA32_TEMPERATURE_TARGET: 0x%08llx (%d C) (%d default - %d offset)\n",
				cpu, msr, tcc_default - tcc_offset, tcc_default, tcc_offset);
		} else {
			fprintf(outf, "cpu%d: MSR_IA32_TEMPERATURE_TARGET: 0x%08llx (%d C)\n", cpu, msr, tcc_default);
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

int print_thermal(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned long long msr;
	unsigned int dts, dts2;
	int cpu;

	UNUSED(c);
	UNUSED(p);

	if (no_msr)
		return 0;

	if (!(do_dts || do_ptm))
		return 0;

	cpu = t->cpu_id;

	/* DTS is per-core, no need to print for each thread */
	if (!is_cpu_first_thread_in_core(t, c, p))
		return 0;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "print_thermal: Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (do_ptm && is_cpu_first_core_in_package(t, c, p)) {
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

void probe_thermal(void)
{
	if (!access("/sys/devices/system/cpu/cpu0/thermal_throttle/core_throttle_count", R_OK))
		BIC_PRESENT(BIC_CORE_THROT_CNT);
	else
		BIC_NOT_PRESENT(BIC_CORE_THROT_CNT);

	for_all_cpus(set_temperature_target, ODD_COUNTERS);

	if (quiet)
		return;

	for_all_cpus(print_thermal, ODD_COUNTERS);
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

void decode_feature_control_msr(void)
{
	unsigned long long msr;

	if (no_msr)
		return;

	if (!get_msr(base_cpu, MSR_IA32_FEAT_CTL, &msr))
		fprintf(outf, "cpu%d: MSR_IA32_FEATURE_CONTROL: 0x%08llx (%sLocked %s)\n",
			base_cpu, msr, msr & FEAT_CTL_LOCKED ? "" : "UN-", msr & (1 << 18) ? "SGX" : "");
}

void decode_misc_enable_msr(void)
{
	unsigned long long msr;

	if (no_msr)
		return;

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

	if (no_msr)
		return;

	if (!platform->has_msr_misc_feature_control)
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

	if (no_msr)
		return;

	if (!platform->has_msr_misc_pwr_mgmt)
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

	if (no_msr)
		return;

	if (!platform->has_msr_c6_demotion_policy_config)
		return;

	if (!get_msr(base_cpu, MSR_CC6_DEMOTION_POLICY_CONFIG, &msr))
		fprintf(outf, "cpu%d: MSR_CC6_DEMOTION_POLICY_CONFIG: 0x%08llx (%sable-CC6-Demotion)\n",
			base_cpu, msr, msr & (1 << 0) ? "EN" : "DIS");

	if (!get_msr(base_cpu, MSR_MC6_DEMOTION_POLICY_CONFIG, &msr))
		fprintf(outf, "cpu%d: MSR_MC6_DEMOTION_POLICY_CONFIG: 0x%08llx (%sable-MC6-Demotion)\n",
			base_cpu, msr, msr & (1 << 0) ? "EN" : "DIS");
}

void print_dev_latency(void)
{
	char *path = "/dev/cpu_dma_latency";
	int fd;
	int value;
	int retval;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (debug)
			warnx("Read %s failed", path);
		return;
	}

	retval = read(fd, (void *)&value, sizeof(int));
	if (retval != sizeof(int)) {
		warn("read failed %s", path);
		close(fd);
		return;
	}
	fprintf(outf, "/dev/cpu_dma_latency: %d usec (%s)\n", value, value == 2000000000 ? "default" : "constrained");

	close(fd);
}

static int has_instr_count_access(void)
{
	int fd;
	int has_access;

	if (no_perf)
		return 0;

	fd = open_perf_counter(base_cpu, PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, -1, 0);
	has_access = fd != -1;

	if (fd != -1)
		close(fd);

	if (!has_access)
		warnx("Failed to access %s. Some of the counters may not be available\n"
		      "\tRun as root to enable them or use %s to disable the access explicitly",
		      "instructions retired perf counter", "--no-perf");

	return has_access;
}

int add_rapl_perf_counter_(int cpu, struct rapl_counter_info_t *rci, const struct rapl_counter_arch_info *cai,
			   double *scale_, enum rapl_unit *unit_)
{
	if (no_perf)
		return -1;

	const double scale = read_perf_scale(cai->perf_subsys, cai->perf_name);

	if (scale == 0.0)
		return -1;

	const enum rapl_unit unit = read_perf_rapl_unit(cai->perf_subsys, cai->perf_name);

	if (unit == RAPL_UNIT_INVALID)
		return -1;

	const unsigned int rapl_type = read_perf_type(cai->perf_subsys);
	const unsigned int rapl_energy_pkg_config = read_perf_config(cai->perf_subsys, cai->perf_name);

	const int fd_counter =
	    open_perf_counter(cpu, rapl_type, rapl_energy_pkg_config, rci->fd_perf, PERF_FORMAT_GROUP);
	if (fd_counter == -1)
		return -1;

	/* If it's the first counter opened, make it a group descriptor */
	if (rci->fd_perf == -1)
		rci->fd_perf = fd_counter;

	*scale_ = scale;
	*unit_ = unit;
	return fd_counter;
}

int add_rapl_perf_counter(int cpu, struct rapl_counter_info_t *rci, const struct rapl_counter_arch_info *cai,
			  double *scale, enum rapl_unit *unit)
{
	int ret = add_rapl_perf_counter_(cpu, rci, cai, scale, unit);

	if (debug >= 2)
		fprintf(stderr, "%s: %d (cpu: %d)\n", __func__, ret, cpu);

	return ret;
}

/*
 * Linux-perf manages the HW instructions-retired counter
 * by enabling when requested, and hiding rollover
 */
void linux_perf_init(void)
{
	if (access("/proc/sys/kernel/perf_event_paranoid", F_OK))
		return;

	if (BIC_IS_ENABLED(BIC_IPC) && has_aperf) {
		fd_instr_count_percpu = calloc(topo.max_cpu_num + 1, sizeof(int));
		if (fd_instr_count_percpu == NULL)
			err(-1, "calloc fd_instr_count_percpu");
	}
}

void rapl_perf_init(void)
{
	const unsigned int num_domains = (platform->has_per_core_rapl ? topo.max_core_id : topo.max_package_id) + 1;
	bool *domain_visited = calloc(num_domains, sizeof(bool));

	rapl_counter_info_perdomain = calloc(num_domains, sizeof(*rapl_counter_info_perdomain));
	if (rapl_counter_info_perdomain == NULL)
		err(-1, "calloc rapl_counter_info_percpu");
	rapl_counter_info_perdomain_size = num_domains;

	/*
	 * Initialize rapl_counter_info_percpu
	 */
	for (unsigned int domain_id = 0; domain_id < num_domains; ++domain_id) {
		struct rapl_counter_info_t *rci = &rapl_counter_info_perdomain[domain_id];

		rci->fd_perf = -1;
		for (size_t i = 0; i < NUM_RAPL_COUNTERS; ++i) {
			rci->data[i] = 0;
			rci->source[i] = COUNTER_SOURCE_NONE;
		}
	}

	/*
	 * Open/probe the counters
	 * If can't get it via perf, fallback to MSR
	 */
	for (size_t i = 0; i < ARRAY_SIZE(rapl_counter_arch_infos); ++i) {

		const struct rapl_counter_arch_info *const cai = &rapl_counter_arch_infos[i];
		bool has_counter = 0;
		double scale;
		enum rapl_unit unit;
		unsigned int next_domain;

		memset(domain_visited, 0, num_domains * sizeof(*domain_visited));

		for (int cpu = 0; cpu < topo.max_cpu_num + 1; ++cpu) {

			if (cpu_is_not_allowed(cpu))
				continue;

			/* Skip already seen and handled RAPL domains */
			next_domain =
			    platform->has_per_core_rapl ? cpus[cpu].physical_core_id : cpus[cpu].physical_package_id;

			assert(next_domain < num_domains);

			if (domain_visited[next_domain])
				continue;

			domain_visited[next_domain] = 1;

			if ((cai->flags & RAPL_COUNTER_FLAG_PLATFORM_COUNTER) && (cpu != base_cpu))
				continue;

			struct rapl_counter_info_t *rci = &rapl_counter_info_perdomain[next_domain];

			/* Check if the counter is enabled and accessible */
			if (BIC_IS_ENABLED(cai->bic) && (platform->rapl_msrs & cai->feature_mask)) {

				/* Use perf API for this counter */
				if (!no_perf && cai->perf_name
				    && add_rapl_perf_counter(cpu, rci, cai, &scale, &unit) != -1) {
					rci->source[cai->rci_index] = COUNTER_SOURCE_PERF;
					rci->scale[cai->rci_index] = scale * cai->compat_scale;
					rci->unit[cai->rci_index] = unit;
					rci->flags[cai->rci_index] = cai->flags;

					/* Use MSR for this counter */
				} else if (!no_msr && cai->msr && probe_msr(cpu, cai->msr) == 0) {
					rci->source[cai->rci_index] = COUNTER_SOURCE_MSR;
					rci->msr[cai->rci_index] = cai->msr;
					rci->msr_mask[cai->rci_index] = cai->msr_mask;
					rci->msr_shift[cai->rci_index] = cai->msr_shift;
					rci->unit[cai->rci_index] = RAPL_UNIT_JOULES;
					rci->scale[cai->rci_index] = *cai->platform_rapl_msr_scale * cai->compat_scale;
					rci->flags[cai->rci_index] = cai->flags;
				}
			}

			if (rci->source[cai->rci_index] != COUNTER_SOURCE_NONE)
				has_counter = 1;
		}

		/* If any CPU has access to the counter, make it present */
		if (has_counter)
			BIC_PRESENT(cai->bic);
	}

	free(domain_visited);
}

/* Assumes msr_counter_info is populated */
static int has_amperf_access(void)
{
	return msr_counter_arch_infos[MSR_ARCH_INFO_APERF_INDEX].present &&
	    msr_counter_arch_infos[MSR_ARCH_INFO_MPERF_INDEX].present;
}

int *get_cstate_perf_group_fd(struct cstate_counter_info_t *cci, const char *group_name)
{
	if (strcmp(group_name, "cstate_core") == 0)
		return &cci->fd_perf_core;

	if (strcmp(group_name, "cstate_pkg") == 0)
		return &cci->fd_perf_pkg;

	return NULL;
}

int add_cstate_perf_counter_(int cpu, struct cstate_counter_info_t *cci, const struct cstate_counter_arch_info *cai)
{
	if (no_perf)
		return -1;

	int *pfd_group = get_cstate_perf_group_fd(cci, cai->perf_subsys);

	if (pfd_group == NULL)
		return -1;

	const unsigned int type = read_perf_type(cai->perf_subsys);
	const unsigned int config = read_perf_config(cai->perf_subsys, cai->perf_name);

	const int fd_counter = open_perf_counter(cpu, type, config, *pfd_group, PERF_FORMAT_GROUP);

	if (fd_counter == -1)
		return -1;

	/* If it's the first counter opened, make it a group descriptor */
	if (*pfd_group == -1)
		*pfd_group = fd_counter;

	return fd_counter;
}

int add_cstate_perf_counter(int cpu, struct cstate_counter_info_t *cci, const struct cstate_counter_arch_info *cai)
{
	int ret = add_cstate_perf_counter_(cpu, cci, cai);

	if (debug >= 2)
		fprintf(stderr, "%s: %d (cpu: %d)\n", __func__, ret, cpu);

	return ret;
}

int add_msr_perf_counter_(int cpu, struct msr_counter_info_t *cci, const struct msr_counter_arch_info *cai)
{
	if (no_perf)
		return -1;

	const unsigned int type = read_perf_type(cai->perf_subsys);
	const unsigned int config = read_perf_config(cai->perf_subsys, cai->perf_name);

	const int fd_counter = open_perf_counter(cpu, type, config, cci->fd_perf, PERF_FORMAT_GROUP);

	if (fd_counter == -1)
		return -1;

	/* If it's the first counter opened, make it a group descriptor */
	if (cci->fd_perf == -1)
		cci->fd_perf = fd_counter;

	return fd_counter;
}

int add_msr_perf_counter(int cpu, struct msr_counter_info_t *cci, const struct msr_counter_arch_info *cai)
{
	int ret = add_msr_perf_counter_(cpu, cci, cai);

	if (debug)
		fprintf(stderr, "%s: %s/%s: %d (cpu: %d)\n", __func__, cai->perf_subsys, cai->perf_name, ret, cpu);

	return ret;
}

void msr_perf_init_(void)
{
	const int mci_num = topo.max_cpu_num + 1;

	msr_counter_info = calloc(mci_num, sizeof(*msr_counter_info));
	if (!msr_counter_info)
		err(1, "calloc msr_counter_info");
	msr_counter_info_size = mci_num;

	for (int cpu = 0; cpu < mci_num; ++cpu)
		msr_counter_info[cpu].fd_perf = -1;

	for (int cidx = 0; cidx < NUM_MSR_COUNTERS; ++cidx) {

		struct msr_counter_arch_info *cai = &msr_counter_arch_infos[cidx];

		cai->present = false;

		for (int cpu = 0; cpu < mci_num; ++cpu) {

			struct msr_counter_info_t *const cci = &msr_counter_info[cpu];

			if (cpu_is_not_allowed(cpu))
				continue;

			if (cai->needed) {
				/* Use perf API for this counter */
				if (!no_perf && cai->perf_name && add_msr_perf_counter(cpu, cci, cai) != -1) {
					cci->source[cai->rci_index] = COUNTER_SOURCE_PERF;
					cai->present = true;

					/* User MSR for this counter */
				} else if (!no_msr && cai->msr && probe_msr(cpu, cai->msr) == 0) {
					cci->source[cai->rci_index] = COUNTER_SOURCE_MSR;
					cci->msr[cai->rci_index] = cai->msr;
					cci->msr_mask[cai->rci_index] = cai->msr_mask;
					cai->present = true;
				}
			}
		}
	}
}

/* Initialize data for reading perf counters from the MSR group. */
void msr_perf_init(void)
{
	bool need_amperf = false, need_smi = false;
	const bool need_soft_c1 = (!platform->has_msr_core_c1_res) && (platform->supported_cstates & CC1);

	need_amperf = BIC_IS_ENABLED(BIC_Avg_MHz) || BIC_IS_ENABLED(BIC_Busy) || BIC_IS_ENABLED(BIC_Bzy_MHz)
	    || BIC_IS_ENABLED(BIC_IPC) || need_soft_c1;

	if (BIC_IS_ENABLED(BIC_SMI))
		need_smi = true;

	/* Enable needed counters */
	msr_counter_arch_infos[MSR_ARCH_INFO_APERF_INDEX].needed = need_amperf;
	msr_counter_arch_infos[MSR_ARCH_INFO_MPERF_INDEX].needed = need_amperf;
	msr_counter_arch_infos[MSR_ARCH_INFO_SMI_INDEX].needed = need_smi;

	msr_perf_init_();

	const bool has_amperf = has_amperf_access();
	const bool has_smi = msr_counter_arch_infos[MSR_ARCH_INFO_SMI_INDEX].present;

	has_aperf_access = has_amperf;

	if (has_amperf) {
		BIC_PRESENT(BIC_Avg_MHz);
		BIC_PRESENT(BIC_Busy);
		BIC_PRESENT(BIC_Bzy_MHz);
		BIC_PRESENT(BIC_SMI);
	}

	if (has_smi)
		BIC_PRESENT(BIC_SMI);
}

void cstate_perf_init_(bool soft_c1)
{
	bool has_counter;
	bool *cores_visited = NULL, *pkg_visited = NULL;
	const int cores_visited_elems = topo.max_core_id + 1;
	const int pkg_visited_elems = topo.max_package_id + 1;
	const int cci_num = topo.max_cpu_num + 1;

	ccstate_counter_info = calloc(cci_num, sizeof(*ccstate_counter_info));
	if (!ccstate_counter_info)
		err(1, "calloc ccstate_counter_arch_info");
	ccstate_counter_info_size = cci_num;

	cores_visited = calloc(cores_visited_elems, sizeof(*cores_visited));
	if (!cores_visited)
		err(1, "calloc cores_visited");

	pkg_visited = calloc(pkg_visited_elems, sizeof(*pkg_visited));
	if (!pkg_visited)
		err(1, "calloc pkg_visited");

	/* Initialize cstate_counter_info_percpu */
	for (int cpu = 0; cpu < cci_num; ++cpu) {
		ccstate_counter_info[cpu].fd_perf_core = -1;
		ccstate_counter_info[cpu].fd_perf_pkg = -1;
	}

	for (int cidx = 0; cidx < NUM_CSTATE_COUNTERS; ++cidx) {
		has_counter = false;
		memset(cores_visited, 0, cores_visited_elems * sizeof(*cores_visited));
		memset(pkg_visited, 0, pkg_visited_elems * sizeof(*pkg_visited));

		const struct cstate_counter_arch_info *cai = &ccstate_counter_arch_infos[cidx];

		for (int cpu = 0; cpu < cci_num; ++cpu) {

			struct cstate_counter_info_t *const cci = &ccstate_counter_info[cpu];

			if (cpu_is_not_allowed(cpu))
				continue;

			const int core_id = cpus[cpu].physical_core_id;
			const int pkg_id = cpus[cpu].physical_package_id;

			assert(core_id < cores_visited_elems);
			assert(pkg_id < pkg_visited_elems);

			const bool per_thread = cai->flags & CSTATE_COUNTER_FLAG_COLLECT_PER_THREAD;
			const bool per_core = cai->flags & CSTATE_COUNTER_FLAG_COLLECT_PER_CORE;

			if (!per_thread && cores_visited[core_id])
				continue;

			if (!per_core && pkg_visited[pkg_id])
				continue;

			const bool counter_needed = BIC_IS_ENABLED(cai->bic) ||
			    (soft_c1 && (cai->flags & CSTATE_COUNTER_FLAG_SOFT_C1_DEPENDENCY));
			const bool counter_supported = (platform->supported_cstates & cai->feature_mask);

			if (counter_needed && counter_supported) {
				/* Use perf API for this counter */
				if (!no_perf && cai->perf_name && add_cstate_perf_counter(cpu, cci, cai) != -1) {

					cci->source[cai->rci_index] = COUNTER_SOURCE_PERF;

					/* User MSR for this counter */
				} else if (!no_msr && cai->msr && pkg_cstate_limit >= cai->pkg_cstate_limit
					   && probe_msr(cpu, cai->msr) == 0) {
					cci->source[cai->rci_index] = COUNTER_SOURCE_MSR;
					cci->msr[cai->rci_index] = cai->msr;
				}
			}

			if (cci->source[cai->rci_index] != COUNTER_SOURCE_NONE) {
				has_counter = true;
				cores_visited[core_id] = true;
				pkg_visited[pkg_id] = true;
			}
		}

		/* If any CPU has access to the counter, make it present */
		if (has_counter)
			BIC_PRESENT(cai->bic);
	}

	free(cores_visited);
	free(pkg_visited);
}

void cstate_perf_init(void)
{
	/*
	 * If we don't have a C1 residency MSR, we calculate it "in software",
	 * but we need APERF, MPERF too.
	 */
	const bool soft_c1 = !platform->has_msr_core_c1_res && has_amperf_access()
	    && platform->supported_cstates & CC1;

	if (soft_c1)
		BIC_PRESENT(BIC_CPU_c1);

	cstate_perf_init_(soft_c1);
}

void probe_cstates(void)
{
	probe_cst_limit();

	if (platform->has_msr_module_c6_res_ms)
		BIC_PRESENT(BIC_Mod_c6);

	if (platform->has_ext_cst_msrs && !no_msr) {
		BIC_PRESENT(BIC_Totl_c0);
		BIC_PRESENT(BIC_Any_c0);
		BIC_PRESENT(BIC_GFX_c0);
		BIC_PRESENT(BIC_CPUGFX);
	}

	if (quiet)
		return;

	dump_power_ctl();
	dump_cst_cfg();
	decode_c6_demotion_policy_msr();
	print_dev_latency();
	dump_sysfs_cstate_config();
	print_irtl();
}

void probe_lpi(void)
{
	if (!access("/sys/devices/system/cpu/cpuidle/low_power_idle_cpu_residency_us", R_OK))
		BIC_PRESENT(BIC_CPU_LPI);
	else
		BIC_NOT_PRESENT(BIC_CPU_LPI);

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

}

void probe_pstates(void)
{
	probe_bclk();

	if (quiet)
		return;

	dump_platform_info();
	dump_turbo_ratio_info();
	dump_sysfs_pstate_config();
	decode_misc_pwr_mgmt_msr();

	for_all_cpus(print_hwp, ODD_COUNTERS);
	for_all_cpus(print_epb, ODD_COUNTERS);
	for_all_cpus(print_perf_limit, ODD_COUNTERS);
}

void process_cpuid()
{
	unsigned int eax, ebx, ecx, edx;
	unsigned int fms, family, model, stepping, ecx_flags, edx_flags;
	unsigned long long ucode_patch = 0;
	bool ucode_patch_valid = false;

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

	if (!no_msr) {
		if (get_msr(sched_getcpu(), MSR_IA32_UCODE_REV, &ucode_patch))
			warnx("get_msr(UCODE)");
		else
			ucode_patch_valid = true;
	}

	/*
	 * check max extended function levels of CPUID.
	 * This is needed to check for invariant TSC.
	 * This check is valid for both Intel and AMD.
	 */
	ebx = ecx = edx = 0;
	__cpuid(0x80000000, max_extended_level, ebx, ecx, edx);

	if (!quiet) {
		fprintf(outf, "CPUID(1): family:model:stepping 0x%x:%x:%x (%d:%d:%d)",
			family, model, stepping, family, model, stepping);
		if (ucode_patch_valid)
			fprintf(outf, " microcode 0x%x", (unsigned int)((ucode_patch >> 32) & 0xFFFFFFFF));
		fputc('\n', outf);

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

	probe_platform_features(family, model);

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
				crystal_hz = platform->crystal_freq;

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

		bclk = bus_mhz;

		base_hz = base_mhz * 1000000;
		has_base_hz = 1;

		if (platform->enable_tsc_tweak)
			tsc_tweak = base_hz / tsc_hz;

		if (!quiet)
			fprintf(outf, "CPUID(0x16): base_mhz: %d max_mhz: %d bus_mhz: %d\n",
				base_mhz, max_mhz, bus_mhz);
	}

	if (has_aperf)
		aperf_mperf_multiplier = platform->need_perf_multiplier ? 1024 : 1;

	BIC_PRESENT(BIC_IRQ);
	BIC_PRESENT(BIC_NMI);
	BIC_PRESENT(BIC_TSC_MHz);
}

static void counter_info_init(void)
{
	for (int i = 0; i < NUM_CSTATE_COUNTERS; ++i) {
		struct cstate_counter_arch_info *const cai = &ccstate_counter_arch_infos[i];

		if (platform->has_msr_knl_core_c6_residency && cai->msr == MSR_CORE_C6_RESIDENCY)
			cai->msr = MSR_KNL_CORE_C6_RESIDENCY;

		if (!platform->has_msr_core_c1_res && cai->msr == MSR_CORE_C1_RES)
			cai->msr = 0;

		if (platform->has_msr_atom_pkg_c6_residency && cai->msr == MSR_PKG_C6_RESIDENCY)
			cai->msr = MSR_ATOM_PKG_C6_RESIDENCY;
	}

	for (int i = 0; i < NUM_MSR_COUNTERS; ++i) {
		msr_counter_arch_infos[i].present = false;
		msr_counter_arch_infos[i].needed = false;
	}
}

void probe_pm_features(void)
{
	probe_pstates();

	probe_cstates();

	probe_lpi();

	probe_intel_uncore_frequency();

	probe_graphics();

	probe_rapl();

	probe_thermal();

	if (platform->has_nhm_msrs && !no_msr)
		BIC_PRESENT(BIC_SMI);

	if (!quiet)
		decode_misc_feature_control();
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

char *possible_file = "/sys/devices/system/cpu/possible";
char possible_buf[1024];

int initialize_cpu_possible_set(void)
{
	FILE *fp;

	fp = fopen(possible_file, "r");
	if (!fp) {
		warn("open %s", possible_file);
		return -1;
	}
	if (fread(possible_buf, sizeof(char), 1024, fp) == 0) {
		warn("read %s", possible_file);
		goto err;
	}
	if (parse_cpu_str(possible_buf, cpu_possible_set, cpu_possible_setsize)) {
		warnx("%s: cpu str malformat %s\n", possible_file, cpu_effective_str);
		goto err;
	}
	return 0;

err:
	fclose(fp);
	return -1;
}

void topology_probe(bool startup)
{
	int i;
	int max_core_id = 0;
	int max_package_id = 0;
	int max_siblings = 0;

	/* Initialize num_cpus, max_cpu_num */
	set_max_cpu_num();
	topo.num_cpus = 0;
	for_all_proc_cpus(count_cpus);
	if (!summary_only)
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
	 * Allocate and initialize cpu_possible_set
	 */
	cpu_possible_set = CPU_ALLOC((topo.max_cpu_num + 1));
	if (cpu_possible_set == NULL)
		err(3, "CPU_ALLOC");
	cpu_possible_setsize = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(cpu_possible_setsize, cpu_possible_set);
	initialize_cpu_possible_set();

	/*
	 * Allocate and initialize cpu_effective_set
	 */
	cpu_effective_set = CPU_ALLOC((topo.max_cpu_num + 1));
	if (cpu_effective_set == NULL)
		err(3, "CPU_ALLOC");
	cpu_effective_setsize = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(cpu_effective_setsize, cpu_effective_set);
	update_effective_set(startup);

	/*
	 * Allocate and initialize cpu_allowed_set
	 */
	cpu_allowed_set = CPU_ALLOC((topo.max_cpu_num + 1));
	if (cpu_allowed_set == NULL)
		err(3, "CPU_ALLOC");
	cpu_allowed_setsize = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(cpu_allowed_setsize, cpu_allowed_set);

	/*
	 * Validate and update cpu_allowed_set.
	 *
	 * Make sure all cpus in cpu_subset are also in cpu_present_set during startup.
	 * Give a warning when cpus in cpu_subset become unavailable at runtime.
	 * Give a warning when cpus are not effective because of cgroup setting.
	 *
	 * cpu_allowed_set is the intersection of cpu_present_set/cpu_effective_set/cpu_subset.
	 */
	for (i = 0; i < CPU_SUBSET_MAXCPUS; ++i) {
		if (cpu_subset && !CPU_ISSET_S(i, cpu_subset_size, cpu_subset))
			continue;

		if (!CPU_ISSET_S(i, cpu_present_setsize, cpu_present_set)) {
			if (cpu_subset) {
				/* cpus in cpu_subset must be in cpu_present_set during startup */
				if (startup)
					err(1, "cpu%d not present", i);
				else
					fprintf(stderr, "cpu%d not present\n", i);
			}
			continue;
		}

		if (CPU_COUNT_S(cpu_effective_setsize, cpu_effective_set)) {
			if (!CPU_ISSET_S(i, cpu_effective_setsize, cpu_effective_set)) {
				fprintf(stderr, "cpu%d not effective\n", i);
				continue;
			}
		}

		CPU_SET_S(i, cpu_allowed_setsize, cpu_allowed_set);
	}

	if (!CPU_COUNT_S(cpu_allowed_setsize, cpu_allowed_set))
		err(-ENODEV, "No valid cpus found");
	sched_setaffinity(0, cpu_allowed_setsize, cpu_allowed_set);

	/*
	 * Allocate and initialize cpu_affinity_set
	 */
	cpu_affinity_set = CPU_ALLOC((topo.max_cpu_num + 1));
	if (cpu_affinity_set == NULL)
		err(3, "CPU_ALLOC");
	cpu_affinity_setsize = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(cpu_affinity_setsize, cpu_affinity_set);

	for_all_proc_cpus(init_thread_id);

	for_all_proc_cpus(set_cpu_hybrid_type);

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
		if (cpus[i].die_id > topo.max_die_id)
			topo.max_die_id = cpus[i].die_id;

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
	topo.max_core_id = max_core_id;
	topo.max_package_id = max_package_id;

	topo.cores_per_node = max_core_id + 1;
	if (debug > 1)
		fprintf(outf, "max_core_id %d, sizing for %d cores per package\n", max_core_id, topo.cores_per_node);
	if (!summary_only)
		BIC_PRESENT(BIC_Core);

	topo.num_die = topo.max_die_id + 1;
	if (debug > 1)
		fprintf(outf, "max_die_id %d, sizing for %d die\n", topo.max_die_id, topo.num_die);
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

	for (i = 0; i < num_cores; i++) {
		(*c)[i].core_id = -1;
		(*c)[i].base_cpu = -1;
	}

	*p = calloc(topo.num_packages, sizeof(struct pkg_data));
	if (*p == NULL)
		goto error;

	for (i = 0; i < topo.num_packages; i++) {
		(*p)[i].package_id = i;
		(*p)[i].base_cpu = -1;
	}

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
	if (!cpu_is_not_allowed(cpu_id)) {
		if (c->base_cpu < 0)
			c->base_cpu = t->cpu_id;
		if (p->base_cpu < 0)
			p->base_cpu = t->cpu_id;
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
		err(-1, "calloc %d IRQ", topo.max_cpu_num + 1);

	nmi_per_cpu = calloc(topo.max_cpu_num + 1, sizeof(int));
	if (nmi_per_cpu == NULL)
		err(-1, "calloc %d NMI", topo.max_cpu_num + 1);
}

int update_topo(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	topo.allowed_cpus++;
	if ((int)t->cpu_id == c->base_cpu)
		topo.allowed_cores++;
	if ((int)t->cpu_id == p->base_cpu)
		topo.allowed_packages++;

	return 0;
}

void topology_update(void)
{
	topo.allowed_cpus = 0;
	topo.allowed_cores = 0;
	topo.allowed_packages = 0;
	for_all_cpus(update_topo, ODD_COUNTERS);
}

void setup_all_buffers(bool startup)
{
	topology_probe(startup);
	allocate_irq_buffers();
	allocate_fd_percpu();
	allocate_counters(&thread_even, &core_even, &package_even);
	allocate_counters(&thread_odd, &core_odd, &package_odd);
	allocate_output_buffer();
	for_all_proc_cpus(initialize_counters);
	topology_update();
}

void set_base_cpu(void)
{
	int i;

	for (i = 0; i < topo.max_cpu_num + 1; ++i) {
		if (cpu_is_not_allowed(i))
			continue;
		base_cpu = i;
		if (debug > 1)
			fprintf(outf, "base_cpu = %d\n", base_cpu);
		return;
	}
	err(-ENODEV, "No valid cpus found");
}

bool has_added_counters(void)
{
	/*
	 * It only makes sense to call this after the command line is parsed,
	 * otherwise sys structure is not populated.
	 */

	return sys.added_core_counters | sys.added_thread_counters | sys.added_package_counters;
}

void check_msr_access(void)
{
	check_dev_msr();
	check_msr_permission();

	if (no_msr)
		bic_disable_msr_access();
}

void check_perf_access(void)
{
	if (no_perf || !BIC_IS_ENABLED(BIC_IPC) || !has_instr_count_access())
		bic_enabled &= ~BIC_IPC;
}

bool perf_has_hybrid_devices(void)
{
	/*
	 *  0: unknown
	 *  1: has separate perf device for p and e core
	 * -1: doesn't have separate perf device for p and e core
	 */
	static int cached;

	if (cached > 0)
		return true;

	if (cached < 0)
		return false;

	if (access("/sys/bus/event_source/devices/cpu_core", F_OK)) {
		cached = -1;
		return false;
	}

	if (access("/sys/bus/event_source/devices/cpu_atom", F_OK)) {
		cached = -1;
		return false;
	}

	cached = 1;
	return true;
}

int added_perf_counters_init_(struct perf_counter_info *pinfo)
{
	size_t num_domains = 0;
	unsigned int next_domain;
	bool *domain_visited;
	unsigned int perf_type, perf_config;
	double perf_scale;
	int fd_perf;

	if (!pinfo)
		return 0;

	const size_t max_num_domains = MAX(topo.max_cpu_num + 1, MAX(topo.max_core_id + 1, topo.max_package_id + 1));

	domain_visited = calloc(max_num_domains, sizeof(*domain_visited));

	while (pinfo) {
		switch (pinfo->scope) {
		case SCOPE_CPU:
			num_domains = topo.max_cpu_num + 1;
			break;

		case SCOPE_CORE:
			num_domains = topo.max_core_id + 1;
			break;

		case SCOPE_PACKAGE:
			num_domains = topo.max_package_id + 1;
			break;
		}

		/* Allocate buffer for file descriptor for each domain. */
		pinfo->fd_perf_per_domain = calloc(num_domains, sizeof(*pinfo->fd_perf_per_domain));
		if (!pinfo->fd_perf_per_domain)
			errx(1, "%s: alloc %s", __func__, "fd_perf_per_domain");

		for (size_t i = 0; i < num_domains; ++i)
			pinfo->fd_perf_per_domain[i] = -1;

		pinfo->num_domains = num_domains;
		pinfo->scale = 1.0;

		memset(domain_visited, 0, max_num_domains * sizeof(*domain_visited));

		for (int cpu = 0; cpu < topo.max_cpu_num + 1; ++cpu) {

			next_domain = cpu_to_domain(pinfo, cpu);

			assert(next_domain < num_domains);

			if (cpu_is_not_allowed(cpu))
				continue;

			if (domain_visited[next_domain])
				continue;

			/*
			 * Intel hybrid platforms expose different perf devices for P and E cores.
			 * Instead of one, "/sys/bus/event_source/devices/cpu" device, there are
			 * "/sys/bus/event_source/devices/{cpu_core,cpu_atom}".
			 *
			 * This makes it more complicated to the user, because most of the counters
			 * are available on both and have to be handled manually, otherwise.
			 *
			 * Code below, allow user to use the old "cpu" name, which is translated accordingly.
			 */
			const char *perf_device = pinfo->device;

			if (strcmp(perf_device, "cpu") == 0 && perf_has_hybrid_devices()) {
				switch (cpus[cpu].type) {
				case INTEL_PCORE_TYPE:
					perf_device = "cpu_core";
					break;

				case INTEL_ECORE_TYPE:
					perf_device = "cpu_atom";
					break;

				default: /* Don't change, we will probably fail and report a problem soon. */
					break;
				}
			}

			perf_type = read_perf_type(perf_device);
			if (perf_type == (unsigned int)-1) {
				warnx("%s: perf/%s/%s: failed to read %s",
				      __func__, perf_device, pinfo->event, "type");
				continue;
			}

			perf_config = read_perf_config(perf_device, pinfo->event);
			if (perf_config == (unsigned int)-1) {
				warnx("%s: perf/%s/%s: failed to read %s",
				      __func__, perf_device, pinfo->event, "config");
				continue;
			}

			/* Scale is not required, some counters just don't have it. */
			perf_scale = read_perf_scale(perf_device, pinfo->event);
			if (perf_scale == 0.0)
				perf_scale = 1.0;

			fd_perf = open_perf_counter(cpu, perf_type, perf_config, -1, 0);
			if (fd_perf == -1) {
				warnx("%s: perf/%s/%s: failed to open counter on cpu%d",
				      __func__, perf_device, pinfo->event, cpu);
				continue;
			}

			domain_visited[next_domain] = 1;
			pinfo->fd_perf_per_domain[next_domain] = fd_perf;
			pinfo->scale = perf_scale;

			if (debug)
				fprintf(stderr, "Add perf/%s/%s cpu%d: %d\n",
					perf_device, pinfo->event, cpu, pinfo->fd_perf_per_domain[next_domain]);
		}

		pinfo = pinfo->next;
	}

	free(domain_visited);

	return 0;
}

void added_perf_counters_init(void)
{
	if (added_perf_counters_init_(sys.perf_tp))
		errx(1, "%s: %s", __func__, "thread");

	if (added_perf_counters_init_(sys.perf_cp))
		errx(1, "%s: %s", __func__, "core");

	if (added_perf_counters_init_(sys.perf_pp))
		errx(1, "%s: %s", __func__, "package");
}

int parse_telem_info_file(int fd_dir, const char *info_filename, const char *format, unsigned long *output)
{
	int fd_telem_info;
	FILE *file_telem_info;
	unsigned long value;

	fd_telem_info = openat(fd_dir, info_filename, O_RDONLY);
	if (fd_telem_info == -1)
		return -1;

	file_telem_info = fdopen(fd_telem_info, "r");
	if (file_telem_info == NULL) {
		close(fd_telem_info);
		return -1;
	}

	if (fscanf(file_telem_info, format, &value) != 1) {
		fclose(file_telem_info);
		return -1;
	}

	fclose(file_telem_info);

	*output = value;

	return 0;
}

struct pmt_mmio *pmt_mmio_open(unsigned int target_guid)
{
	struct pmt_diriter_t pmt_iter;
	const struct dirent *entry;
	struct stat st;
	int fd_telem_dir, fd_pmt;
	unsigned long guid, size, offset;
	size_t mmap_size;
	void *mmio;
	struct pmt_mmio *head = NULL, *last = NULL;
	struct pmt_mmio *new_pmt = NULL;

	if (stat(SYSFS_TELEM_PATH, &st) == -1)
		return NULL;

	pmt_diriter_init(&pmt_iter);
	entry = pmt_diriter_begin(&pmt_iter, SYSFS_TELEM_PATH);
	if (!entry) {
		pmt_diriter_remove(&pmt_iter);
		return NULL;
	}

	for ( ; entry != NULL; entry = pmt_diriter_next(&pmt_iter)) {
		if (fstatat(dirfd(pmt_iter.dir), entry->d_name, &st, 0) == -1)
			break;

		if (!S_ISDIR(st.st_mode))
			continue;

		fd_telem_dir = openat(dirfd(pmt_iter.dir), entry->d_name, O_RDONLY);
		if (fd_telem_dir == -1)
			break;

		if (parse_telem_info_file(fd_telem_dir, "guid", "%lx", &guid)) {
			close(fd_telem_dir);
			break;
		}

		if (parse_telem_info_file(fd_telem_dir, "size", "%lu", &size)) {
			close(fd_telem_dir);
			break;
		}

		if (guid != target_guid) {
			close(fd_telem_dir);
			continue;
		}

		if (parse_telem_info_file(fd_telem_dir, "offset", "%lu", &offset)) {
			close(fd_telem_dir);
			break;
		}

		assert(offset == 0);

		fd_pmt = openat(fd_telem_dir, "telem", O_RDONLY);
		if (fd_pmt == -1)
			goto loop_cleanup_and_break;

		mmap_size = ROUND_UP_TO_PAGE_SIZE(size);
		mmio = mmap(0, mmap_size, PROT_READ, MAP_SHARED, fd_pmt, 0);
		if (mmio != MAP_FAILED) {
			if (debug)
				fprintf(stderr, "%s: 0x%lx mmaped at: %p\n", __func__, guid, mmio);

			new_pmt = calloc(1, sizeof(*new_pmt));

			if (!new_pmt) {
				fprintf(stderr, "%s: Failed to allocate pmt_mmio\n", __func__);
				exit(1);
			}

			/*
			 * Create linked list of mmaped regions,
			 * but preserve the ordering from sysfs.
			 * Ordering is important for the user to
			 * use the seq=%u parameter when adding a counter.
			 */
			new_pmt->guid = guid;
			new_pmt->mmio_base = mmio;
			new_pmt->pmt_offset = offset;
			new_pmt->size = size;
			new_pmt->next = pmt_mmios;

			if (last)
				last->next = new_pmt;
			else
				head = new_pmt;

			last = new_pmt;
		}

loop_cleanup_and_break:
		close(fd_pmt);
		close(fd_telem_dir);
	}

	pmt_diriter_remove(&pmt_iter);

	/*
	 * If we found something, stick just
	 * created linked list to the front.
	 */
	if (head)
		pmt_mmios = head;

	return head;
}

struct pmt_mmio *pmt_mmio_find(unsigned int guid)
{
	struct pmt_mmio *pmmio = pmt_mmios;

	while (pmmio) {
		if (pmmio->guid == guid)
			return pmmio;

		pmmio = pmmio->next;
	}

	return NULL;
}

void *pmt_get_counter_pointer(struct pmt_mmio *pmmio, unsigned long counter_offset)
{
	char *ret;

	/* Get base of mmaped PMT file. */
	ret = (char *)pmmio->mmio_base;

	/*
	 * Apply PMT MMIO offset to obtain beginning of the mmaped telemetry data.
	 * It's not guaranteed that the mmaped memory begins with the telemetry data
	 *      - we might have to apply the offset first.
	 */
	ret += pmmio->pmt_offset;

	/* Apply the counter offset to get the address to the mmaped counter. */
	ret += counter_offset;

	return ret;
}

struct pmt_mmio *pmt_add_guid(unsigned int guid, unsigned int seq)
{
	struct pmt_mmio *ret;

	ret = pmt_mmio_find(guid);
	if (!ret)
		ret = pmt_mmio_open(guid);

	while (ret && seq) {
		ret = ret->next;
		--seq;
	}

	return ret;
}

enum pmt_open_mode {
	PMT_OPEN_TRY,		/* Open failure is not an error. */
	PMT_OPEN_REQUIRED,	/* Open failure is a fatal error. */
};

struct pmt_counter *pmt_find_counter(struct pmt_counter *pcounter, const char *name)
{
	while (pcounter) {
		if (strcmp(pcounter->name, name) == 0)
			break;

		pcounter = pcounter->next;
	}

	return pcounter;
}

struct pmt_counter **pmt_get_scope_root(enum counter_scope scope)
{
	switch (scope) {
	case SCOPE_CPU:
		return &sys.pmt_tp;
	case SCOPE_CORE:
		return &sys.pmt_cp;
	case SCOPE_PACKAGE:
		return &sys.pmt_pp;
	}

	__builtin_unreachable();
}

void pmt_counter_add_domain(struct pmt_counter *pcounter, unsigned long *pmmio, unsigned int domain_id)
{
	/* Make sure the new domain fits. */
	if (domain_id >= pcounter->num_domains)
		pmt_counter_resize(pcounter, domain_id + 1);

	assert(pcounter->domains);
	assert(domain_id < pcounter->num_domains);

	pcounter->domains[domain_id].pcounter = pmmio;
}

int pmt_add_counter(unsigned int guid, unsigned int seq, const char *name, enum pmt_datatype type,
		    unsigned int lsb, unsigned int msb, unsigned int offset, enum counter_scope scope,
		    enum counter_format format, unsigned int domain_id, enum pmt_open_mode mode)
{
	struct pmt_mmio *mmio;
	struct pmt_counter *pcounter;
	struct pmt_counter **const pmt_root = pmt_get_scope_root(scope);
	bool new_counter = false;
	int conflict = 0;

	if (lsb > msb) {
		fprintf(stderr, "%s: %s: `%s` must be satisfied\n", __func__, "lsb <= msb", name);
		exit(1);
	}

	if (msb >= 64) {
		fprintf(stderr, "%s: %s: `%s` must be satisfied\n", __func__, "msb < 64", name);
		exit(1);
	}

	mmio = pmt_add_guid(guid, seq);
	if (!mmio) {
		if (mode != PMT_OPEN_TRY) {
			fprintf(stderr, "%s: failed to map PMT MMIO for guid %x, seq %u\n", __func__, guid, seq);
			exit(1);
		}

		return 1;
	}

	if (offset >= mmio->size) {
		if (mode != PMT_OPEN_TRY) {
			fprintf(stderr, "%s: offset %u outside of PMT MMIO size %u\n", __func__, offset, mmio->size);
			exit(1);
		}

		return 1;
	}

	pcounter = pmt_find_counter(*pmt_root, name);
	if (!pcounter) {
		pcounter = calloc(1, sizeof(*pcounter));
		new_counter = true;
	}

	if (new_counter) {
		strncpy(pcounter->name, name, ARRAY_SIZE(pcounter->name) - 1);
		pcounter->type = type;
		pcounter->scope = scope;
		pcounter->lsb = lsb;
		pcounter->msb = msb;
		pcounter->format = format;
	} else {
		conflict += pcounter->type != type;
		conflict += pcounter->scope != scope;
		conflict += pcounter->lsb != lsb;
		conflict += pcounter->msb != msb;
		conflict += pcounter->format != format;
	}

	if (conflict) {
		fprintf(stderr, "%s: conflicting parameters for the PMT counter with the same name %s\n",
			__func__, name);
		exit(1);
	}

	pmt_counter_add_domain(pcounter, pmt_get_counter_pointer(mmio, offset), domain_id);

	if (new_counter) {
		pcounter->next = *pmt_root;
		*pmt_root = pcounter;
	}

	return 0;
}

void pmt_init(void)
{
	int cpu_num;
	unsigned long seq, offset, mod_num;

	if (BIC_IS_ENABLED(BIC_Diec6)) {
		pmt_add_counter(PMT_MTL_DC6_GUID, PMT_MTL_DC6_SEQ, "Die%c6", PMT_TYPE_XTAL_TIME,
				PMT_COUNTER_MTL_DC6_LSB, PMT_COUNTER_MTL_DC6_MSB, PMT_COUNTER_MTL_DC6_OFFSET,
				SCOPE_PACKAGE, FORMAT_DELTA, 0, PMT_OPEN_TRY);
	}

	if (BIC_IS_ENABLED(BIC_CPU_c1e)) {
		seq = 0;
		offset = PMT_COUNTER_CWF_MC1E_OFFSET_BASE;
		mod_num = 0;	/* Relative module number for current PMT file. */

		/* Open the counter for each CPU. */
		for (cpu_num = 0; cpu_num < topo.max_cpu_num;) {

			if (cpu_is_not_allowed(cpu_num))
				goto next_loop_iter;

			/*
			 * Set the scope to CPU, even though CWF report the counter per module.
			 * CPUs inside the same module will read from the same location, instead of reporting zeros.
			 *
			 * CWF with newer firmware might require a PMT_TYPE_XTAL_TIME intead of PMT_TYPE_TCORE_CLOCK.
			 */
			pmt_add_counter(PMT_CWF_MC1E_GUID, seq, "CPU%c1e", PMT_TYPE_TCORE_CLOCK,
					PMT_COUNTER_CWF_MC1E_LSB, PMT_COUNTER_CWF_MC1E_MSB, offset, SCOPE_CPU,
					FORMAT_DELTA, cpu_num, PMT_OPEN_TRY);

			/*
			 * Rather complex logic for each time we go to the next loop iteration,
			 * so keep it as a label.
			 */
next_loop_iter:
			/*
			 * Advance the cpu number and check if we should also advance offset to
			 * the next counter inside the PMT file.
			 *
			 * On Clearwater Forest platform, the counter is reported per module,
			 * so open the same counter for all of the CPUs inside the module.
			 * That way, reported table show the correct value for all of the CPUs inside the module,
			 * instead of zeros.
			 */
			++cpu_num;
			if (cpu_num % PMT_COUNTER_CWF_CPUS_PER_MODULE == 0) {
				offset += PMT_COUNTER_CWF_MC1E_OFFSET_INCREMENT;
				++mod_num;
			}

			/*
			 * There are PMT_COUNTER_CWF_MC1E_NUM_MODULES_PER_FILE in each PMT file.
			 *
			 * If that number is reached, seq must be incremented to advance to the next file in a sequence.
			 * Offset inside that file and a module counter has to be reset.
			 */
			if (mod_num == PMT_COUNTER_CWF_MC1E_NUM_MODULES_PER_FILE) {
				++seq;
				offset = PMT_COUNTER_CWF_MC1E_OFFSET_BASE;
				mod_num = 0;
			}
		}
	}
}

void turbostat_init()
{
	setup_all_buffers(true);
	set_base_cpu();
	check_msr_access();
	check_perf_access();
	process_cpuid();
	counter_info_init();
	probe_pm_features();
	msr_perf_init();
	linux_perf_init();
	rapl_perf_init();
	cstate_perf_init();
	added_perf_counters_init();
	pmt_init();

	for_all_cpus(get_cpu_type, ODD_COUNTERS);
	for_all_cpus(get_cpu_type, EVEN_COUNTERS);

	if (BIC_IS_ENABLED(BIC_IPC) && has_aperf_access && get_instr_count_fd(base_cpu) != -1)
		BIC_PRESENT(BIC_IPC);

	/*
	 * If TSC tweak is needed, but couldn't get it,
	 * disable more BICs, since it can't be reported accurately.
	 */
	if (platform->enable_tsc_tweak && !has_base_hz) {
		bic_enabled &= ~BIC_Busy;
		bic_enabled &= ~BIC_Bzy_MHz;
	}
}

void affinitize_child(void)
{
	/* Prefer cpu_possible_set, if available */
	if (sched_setaffinity(0, cpu_possible_setsize, cpu_possible_set)) {
		warn("sched_setaffinity cpu_possible_set");

		/* Otherwise, allow child to run on same cpu set as turbostat */
		if (sched_setaffinity(0, cpu_allowed_setsize, cpu_allowed_set))
			warn("sched_setaffinity cpu_allowed_set");
	}
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
	gettimeofday(&tv_even, (struct timezone *)NULL);

	child_pid = fork();
	if (!child_pid) {
		/* child */
		affinitize_child();
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

	compute_average(EVEN_COUNTERS);
	format_all_counters(EVEN_COUNTERS);

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
	fprintf(outf, "turbostat version 2025.02.02 - Len Brown <lenb@kernel.org>\n");
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

struct msr_counter *find_msrp_by_name(struct msr_counter *head, char *name)
{
	struct msr_counter *mp;

	for (mp = head; mp; mp = mp->next) {
		if (debug)
			fprintf(stderr, "%s: %s %s\n", __func__, name, mp->name);
		if (!strncmp(name, mp->name, strlen(mp->name)))
			return mp;
	}
	return NULL;
}

int add_counter(unsigned int msr_num, char *path, char *name,
		unsigned int width, enum counter_scope scope,
		enum counter_type type, enum counter_format format, int flags, int id)
{
	struct msr_counter *msrp;

	if (no_msr && msr_num)
		errx(1, "Requested MSR counter 0x%x, but in --no-msr mode", msr_num);

	if (debug)
		fprintf(stderr, "%s(msr%d, %s, %s, width%d, scope%d, type%d, format%d, flags%x, id%d)\n",
			__func__, msr_num, path, name, width, scope, type, format, flags, id);

	switch (scope) {

	case SCOPE_CPU:
		msrp = find_msrp_by_name(sys.tp, name);
		if (msrp) {
			if (debug)
				fprintf(stderr, "%s: %s FOUND\n", __func__, name);
			break;
		}
		if (sys.added_thread_counters++ >= MAX_ADDED_THREAD_COUNTERS) {
			warnx("ignoring thread counter %s", name);
			return -1;
		}
		break;
	case SCOPE_CORE:
		msrp = find_msrp_by_name(sys.cp, name);
		if (msrp) {
			if (debug)
				fprintf(stderr, "%s: %s FOUND\n", __func__, name);
			break;
		}
		if (sys.added_core_counters++ >= MAX_ADDED_CORE_COUNTERS) {
			warnx("ignoring core counter %s", name);
			return -1;
		}
		break;
	case SCOPE_PACKAGE:
		msrp = find_msrp_by_name(sys.pp, name);
		if (msrp) {
			if (debug)
				fprintf(stderr, "%s: %s FOUND\n", __func__, name);
			break;
		}
		if (sys.added_package_counters++ >= MAX_ADDED_PACKAGE_COUNTERS) {
			warnx("ignoring package counter %s", name);
			return -1;
		}
		break;
	default:
		warnx("ignoring counter %s with unknown scope", name);
		return -1;
	}

	if (msrp == NULL) {
		msrp = calloc(1, sizeof(struct msr_counter));
		if (msrp == NULL)
			err(-1, "calloc msr_counter");

		msrp->msr_num = msr_num;
		strncpy(msrp->name, name, NAME_BYTES - 1);
		msrp->width = width;
		msrp->type = type;
		msrp->format = format;
		msrp->flags = flags;

		switch (scope) {
		case SCOPE_CPU:
			msrp->next = sys.tp;
			sys.tp = msrp;
			break;
		case SCOPE_CORE:
			msrp->next = sys.cp;
			sys.cp = msrp;
			break;
		case SCOPE_PACKAGE:
			msrp->next = sys.pp;
			sys.pp = msrp;
			break;
		}
	}

	if (path) {
		struct sysfs_path *sp;

		sp = calloc(1, sizeof(struct sysfs_path));
		if (sp == NULL) {
			perror("calloc");
			exit(1);
		}
		strncpy(sp->path, path, PATH_BYTES - 1);
		sp->id = id;
		sp->next = msrp->sp;
		msrp->sp = sp;
	}

	return 0;
}

/*
 * Initialize the fields used for identifying and opening the counter.
 *
 * Defer the initialization of any runtime buffers for actually reading
 * the counters for when we initialize all perf counters, so we can later
 * easily call re_initialize().
 */
struct perf_counter_info *make_perf_counter_info(const char *perf_device,
						 const char *perf_event,
						 const char *name,
						 unsigned int width,
						 enum counter_scope scope,
						 enum counter_type type, enum counter_format format)
{
	struct perf_counter_info *pinfo;

	pinfo = calloc(1, sizeof(*pinfo));
	if (!pinfo)
		errx(1, "%s: Failed to allocate %s/%s\n", __func__, perf_device, perf_event);

	strncpy(pinfo->device, perf_device, ARRAY_SIZE(pinfo->device) - 1);
	strncpy(pinfo->event, perf_event, ARRAY_SIZE(pinfo->event) - 1);

	strncpy(pinfo->name, name, ARRAY_SIZE(pinfo->name) - 1);
	pinfo->width = width;
	pinfo->scope = scope;
	pinfo->type = type;
	pinfo->format = format;

	return pinfo;
}

int add_perf_counter(const char *perf_device, const char *perf_event, const char *name_buffer, unsigned int width,
		     enum counter_scope scope, enum counter_type type, enum counter_format format)
{
	struct perf_counter_info *pinfo;

	switch (scope) {
	case SCOPE_CPU:
		if (sys.added_thread_perf_counters >= MAX_ADDED_THREAD_COUNTERS) {
			warnx("ignoring thread counter perf/%s/%s", perf_device, perf_event);
			return -1;
		}
		break;

	case SCOPE_CORE:
		if (sys.added_core_perf_counters >= MAX_ADDED_CORE_COUNTERS) {
			warnx("ignoring core counter perf/%s/%s", perf_device, perf_event);
			return -1;
		}
		break;

	case SCOPE_PACKAGE:
		if (sys.added_package_perf_counters >= MAX_ADDED_PACKAGE_COUNTERS) {
			warnx("ignoring package counter perf/%s/%s", perf_device, perf_event);
			return -1;
		}
		break;
	}

	pinfo = make_perf_counter_info(perf_device, perf_event, name_buffer, width, scope, type, format);

	if (!pinfo)
		return -1;

	switch (scope) {
	case SCOPE_CPU:
		pinfo->next = sys.perf_tp;
		sys.perf_tp = pinfo;
		++sys.added_thread_perf_counters;
		break;

	case SCOPE_CORE:
		pinfo->next = sys.perf_cp;
		sys.perf_cp = pinfo;
		++sys.added_core_perf_counters;
		break;

	case SCOPE_PACKAGE:
		pinfo->next = sys.perf_pp;
		sys.perf_pp = pinfo;
		++sys.added_package_perf_counters;
		break;
	}

	// FIXME: we might not have debug here yet
	if (debug)
		fprintf(stderr, "%s: %s/%s, name: %s, scope%d\n",
			__func__, pinfo->device, pinfo->event, pinfo->name, pinfo->scope);

	return 0;
}

void parse_add_command_msr(char *add_command)
{
	int msr_num = 0;
	char *path = NULL;
	char perf_device[PERF_DEV_NAME_BYTES] = "";
	char perf_event[PERF_EVT_NAME_BYTES] = "";
	char name_buffer[PERF_NAME_BYTES] = "";
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

		BUILD_BUG_ON(ARRAY_SIZE(perf_device) <= 31);
		BUILD_BUG_ON(ARRAY_SIZE(perf_event) <= 31);
		if (sscanf(add_command, "perf/%31[^/]/%31[^,]", &perf_device[0], &perf_event[0]) == 2)
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

		BUILD_BUG_ON(ARRAY_SIZE(name_buffer) <= 18);
		if (sscanf(add_command, "%18s,%*s", name_buffer) == 1) {
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
	if ((msr_num == 0) && (path == NULL) && (perf_device[0] == '\0' || perf_event[0] == '\0')) {
		fprintf(stderr, "--add: (msrDDD | msr0xXXX | /path_to_counter | perf/device/event) required\n");
		fail++;
	}

	/* Test for non-empty perf_device and perf_event */
	const bool is_perf_counter = perf_device[0] && perf_event[0];

	/* generate default column header */
	if (*name_buffer == '\0') {
		if (is_perf_counter) {
			snprintf(name_buffer, ARRAY_SIZE(name_buffer), "perf/%s", perf_event);
		} else {
			if (width == 32)
				sprintf(name_buffer, "M0x%x%s", msr_num, format == FORMAT_PERCENT ? "%" : "");
			else
				sprintf(name_buffer, "M0X%x%s", msr_num, format == FORMAT_PERCENT ? "%" : "");
		}
	}

	if (is_perf_counter) {
		if (add_perf_counter(perf_device, perf_event, name_buffer, width, scope, type, format))
			fail++;
	} else {
		if (add_counter(msr_num, path, name_buffer, width, scope, type, format, 0, 0))
			fail++;
	}

	if (fail) {
		help();
		exit(1);
	}
}

bool starts_with(const char *str, const char *prefix)
{
	return strncmp(prefix, str, strlen(prefix)) == 0;
}

int pmt_parse_from_path(const char *target_path, unsigned int *out_guid, unsigned int *out_seq)
{
	struct pmt_diriter_t pmt_iter;
	const struct dirent *dirname;
	struct stat stat, target_stat;
	int fd_telem_dir = -1;
	int fd_target_dir;
	unsigned int seq = 0;
	unsigned long guid, target_guid;
	int ret = -1;

	fd_target_dir = open(target_path, O_RDONLY | O_DIRECTORY);
	if (fd_target_dir == -1) {
		return -1;
	}

	if (fstat(fd_target_dir, &target_stat) == -1) {
		fprintf(stderr, "%s: Failed to stat the target: %s", __func__, strerror(errno));
		exit(1);
	}

	if (parse_telem_info_file(fd_target_dir, "guid", "%lx", &target_guid)) {
		fprintf(stderr, "%s: Failed to parse the target guid file: %s", __func__, strerror(errno));
		exit(1);
	}

	close(fd_target_dir);

	pmt_diriter_init(&pmt_iter);

	for (dirname = pmt_diriter_begin(&pmt_iter, SYSFS_TELEM_PATH); dirname != NULL;
	     dirname = pmt_diriter_next(&pmt_iter)) {

		fd_telem_dir = openat(dirfd(pmt_iter.dir), dirname->d_name, O_RDONLY | O_DIRECTORY);
		if (fd_telem_dir == -1)
			continue;

		if (parse_telem_info_file(fd_telem_dir, "guid", "%lx", &guid)) {
			fprintf(stderr, "%s: Failed to parse the guid file: %s", __func__, strerror(errno));
			continue;
		}

		if (fstat(fd_telem_dir, &stat) == -1) {
			fprintf(stderr, "%s: Failed to stat %s directory: %s", __func__,
				dirname->d_name, strerror(errno));
			continue;
		}

		/*
		 * If reached the same directory as target, exit the loop.
		 * Seq has the correct value now.
		 */
		if (stat.st_dev == target_stat.st_dev && stat.st_ino == target_stat.st_ino) {
			ret = 0;
			break;
		}

		/*
		 * If reached directory with the same guid,
		 * but it's not the target directory yet,
		 * increment seq and continue the search.
		 */
		if (guid == target_guid)
			++seq;

		close(fd_telem_dir);
		fd_telem_dir = -1;
	}

	pmt_diriter_remove(&pmt_iter);

	if (fd_telem_dir != -1)
		close(fd_telem_dir);

	if (!ret) {
		*out_guid = target_guid;
		*out_seq = seq;
	}

	return ret;
}

void parse_add_command_pmt(char *add_command)
{
	char *name = NULL;
	char *type_name = NULL;
	char *format_name = NULL;
	char *direct_path = NULL;
	static const char direct_path_prefix[] = "path=";
	unsigned int offset;
	unsigned int lsb;
	unsigned int msb;
	unsigned int guid;
	unsigned int seq = 0; /* By default, pick first file in a sequence with a given GUID. */
	unsigned int domain_id;
	enum counter_scope scope = 0;
	enum pmt_datatype type = PMT_TYPE_RAW;
	enum counter_format format = FORMAT_RAW;
	bool has_offset = false;
	bool has_lsb = false;
	bool has_msb = false;
	bool has_format = true;	/* Format has a default value. */
	bool has_guid = false;
	bool has_scope = false;
	bool has_type = true;	/* Type has a default value. */

	/* Consume the "pmt," prefix. */
	add_command = strchr(add_command, ',');
	if (!add_command) {
		help();
		exit(1);
	}
	++add_command;

	while (add_command) {
		if (starts_with(add_command, "name=")) {
			name = add_command + strlen("name=");
			goto next;
		}

		if (starts_with(add_command, "type=")) {
			type_name = add_command + strlen("type=");
			goto next;
		}

		if (starts_with(add_command, "domain=")) {
			const size_t prefix_len = strlen("domain=");

			if (sscanf(add_command + prefix_len, "cpu%u", &domain_id) == 1) {
				scope = SCOPE_CPU;
				has_scope = true;
			} else if (sscanf(add_command + prefix_len, "core%u", &domain_id) == 1) {
				scope = SCOPE_CORE;
				has_scope = true;
			} else if (sscanf(add_command + prefix_len, "package%u", &domain_id) == 1) {
				scope = SCOPE_PACKAGE;
				has_scope = true;
			}

			if (!has_scope) {
				printf("%s: invalid value for scope. Expected cpu%%u, core%%u or package%%u.\n",
				       __func__);
				exit(1);
			}

			goto next;
		}

		if (starts_with(add_command, "format=")) {
			format_name = add_command + strlen("format=");
			goto next;
		}

		if (sscanf(add_command, "offset=%u", &offset) == 1) {
			has_offset = true;
			goto next;
		}

		if (sscanf(add_command, "lsb=%u", &lsb) == 1) {
			has_lsb = true;
			goto next;
		}

		if (sscanf(add_command, "msb=%u", &msb) == 1) {
			has_msb = true;
			goto next;
		}

		if (sscanf(add_command, "guid=%x", &guid) == 1) {
			has_guid = true;
			goto next;
		}

		if (sscanf(add_command, "seq=%x", &seq) == 1)
			goto next;

		if (strncmp(add_command, direct_path_prefix, strlen(direct_path_prefix)) == 0) {
			direct_path = add_command + strlen(direct_path_prefix);
			goto next;
		}
next:
		add_command = strchr(add_command, ',');
		if (add_command) {
			*add_command = '\0';
			add_command++;
		}
	}

	if (!name) {
		printf("%s: missing %s\n", __func__, "name");
		exit(1);
	}

	if (strlen(name) >= PMT_COUNTER_NAME_SIZE_BYTES) {
		printf("%s: name has to be at most %d characters long\n", __func__, PMT_COUNTER_NAME_SIZE_BYTES);
		exit(1);
	}

	if (format_name) {
		has_format = false;

		if (strcmp("raw", format_name) == 0) {
			format = FORMAT_RAW;
			has_format = true;
		}

		if (strcmp("delta", format_name) == 0) {
			format = FORMAT_DELTA;
			has_format = true;
		}

		if (!has_format) {
			fprintf(stderr, "%s: Invalid format %s. Expected raw or delta\n", __func__, format_name);
			exit(1);
		}
	}

	if (type_name) {
		has_type = false;

		if (strcmp("raw", type_name) == 0) {
			type = PMT_TYPE_RAW;
			has_type = true;
		}

		if (strcmp("txtal_time", type_name) == 0) {
			type = PMT_TYPE_XTAL_TIME;
			has_type = true;
		}

		if (strcmp("tcore_clock", type_name) == 0) {
			type = PMT_TYPE_TCORE_CLOCK;
			has_type = true;
		}

		if (!has_type) {
			printf("%s: invalid %s: %s\n", __func__, "type", type_name);
			exit(1);
		}
	}

	if (!has_offset) {
		printf("%s : missing %s\n", __func__, "offset");
		exit(1);
	}

	if (!has_lsb) {
		printf("%s: missing %s\n", __func__, "lsb");
		exit(1);
	}

	if (!has_msb) {
		printf("%s: missing %s\n", __func__, "msb");
		exit(1);
	}

	if (direct_path && has_guid) {
		printf("%s: path and guid+seq parameters are mutually exclusive\n"
		       "notice: passed guid=0x%x and path=%s\n", __func__, guid, direct_path);
		exit(1);
	}

	if (direct_path) {
		if (pmt_parse_from_path(direct_path, &guid, &seq)) {
			printf("%s: failed to parse PMT file from %s\n", __func__, direct_path);
			exit(1);
		}

		/* GUID was just infered from the direct path. */
		has_guid = true;
	}

	if (!has_guid) {
		printf("%s: missing %s\n", __func__, "guid or path");
		exit(1);
	}

	if (!has_scope) {
		printf("%s: missing %s\n", __func__, "scope");
		exit(1);
	}

	if (lsb > msb) {
		printf("%s: lsb > msb doesn't make sense\n", __func__);
		exit(1);
	}

	pmt_add_counter(guid, seq, name, type, lsb, msb, offset, scope, format, domain_id, PMT_OPEN_REQUIRED);
}

void parse_add_command(char *add_command)
{
	if (strncmp(add_command, "pmt", strlen("pmt")) == 0)
		return parse_add_command_pmt(add_command);
	return parse_add_command_msr(add_command);
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

		add_counter(0, path, name_buf, 64, SCOPE_CPU, COUNTER_USEC, FORMAT_PERCENT, SYSFS_PERCPU, 0);
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

		add_counter(0, path, name_buf, 64, SCOPE_CPU, COUNTER_ITEMS, FORMAT_DELTA, SYSFS_PERCPU, 0);
	}

}

/*
 * parse cpuset with following syntax
 * 1,2,4..6,8-10 and set bits in cpu_subset
 */
void parse_cpu_command(char *optarg)
{
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

	if (parse_cpu_str(optarg, cpu_subset, cpu_subset_size))
		goto error;

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
		{ "force", no_argument, 0, 'f' },
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
		{ "no-msr", no_argument, 0, 'M' },
		{ "no-perf", no_argument, 0, 'P' },
		{ "show", required_argument, 0, 's' },
		{ "Summary", no_argument, 0, 'S' },
		{ "TCC", required_argument, 0, 'T' },
		{ "version", no_argument, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	progname = argv[0];

	/*
	 * Parse some options early, because they may make other options invalid,
	 * like adding the MSR counter with --add and at the same time using --no-msr.
	 */
	while ((opt = getopt_long_only(argc, argv, "+MPn:", long_options, &option_index)) != -1) {
		switch (opt) {
		case 'M':
			no_msr = 1;
			break;
		case 'P':
			no_perf = 1;
			break;
		default:
			break;
		}
	}
	optind = 0;

	while ((opt = getopt_long_only(argc, argv, "+C:c:Dde:hi:Jn:o:qMST:v", long_options, &option_index)) != -1) {
		switch (opt) {
		case 'a':
			parse_add_command(optarg);
			break;
		case 'c':
			parse_cpu_command(optarg);
			break;
		case 'D':
			dump_only++;
			/*
			 * Force the no_perf early to prevent using it as a source.
			 * User asks for raw values, but perf returns them relative
			 * to the opening of the file descriptor.
			 */
			no_perf = 1;
			break;
		case 'e':
			/* --enable specified counter */
			bic_enabled = bic_enabled | bic_lookup(optarg, SHOW_LIST);
			break;
		case 'f':
			force_load++;
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
		case 'M':
		case 'P':
			/* Parsed earlier */
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

void set_rlimit(void)
{
	struct rlimit limit;

	if (getrlimit(RLIMIT_NOFILE, &limit) < 0)
		err(1, "Failed to get rlimit");

	if (limit.rlim_max < MAX_NOFILE)
		limit.rlim_max = MAX_NOFILE;
	if (limit.rlim_cur < MAX_NOFILE)
		limit.rlim_cur = MAX_NOFILE;

	if (setrlimit(RLIMIT_NOFILE, &limit) < 0)
		err(1, "Failed to set rlimit");
}

int main(int argc, char **argv)
{
	int fd, ret;

	fd = open("/sys/fs/cgroup/cgroup.procs", O_WRONLY);
	if (fd < 0)
		goto skip_cgroup_setting;

	ret = write(fd, "0\n", 2);
	if (ret == -1)
		perror("Can't update cgroup\n");

	close(fd);

skip_cgroup_setting:
	outf = stderr;
	cmdline(argc, argv);

	if (!quiet) {
		print_version();
		print_bootcmd();
	}

	probe_sysfs();

	if (!getuid())
		set_rlimit();

	turbostat_init();

	if (!no_msr)
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
