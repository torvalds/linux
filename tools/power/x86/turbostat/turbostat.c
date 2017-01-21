/*
 * turbostat -- show CPU frequency and C-state residency
 * on modern Intel turbo-capable processors.
 *
 * Copyright (c) 2013 Intel Corporation.
 * Len Brown <len.brown@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <linux/capability.h>
#include <errno.h>

char *proc_stat = "/proc/stat";
FILE *outf;
int *fd_percpu;
struct timespec interval_ts = {5, 0};
unsigned int debug;
unsigned int rapl_joules;
unsigned int summary_only;
unsigned int dump_only;
unsigned int do_snb_cstates;
unsigned int do_knl_cstates;
unsigned int do_pc2;
unsigned int do_pc3;
unsigned int do_pc6;
unsigned int do_pc7;
unsigned int do_c8_c9_c10;
unsigned int do_skl_residency;
unsigned int do_slm_cstates;
unsigned int use_c1_residency_msr;
unsigned int has_aperf;
unsigned int has_epb;
unsigned int do_irtl_snb;
unsigned int do_irtl_hsw;
unsigned int units = 1000000;	/* MHz etc */
unsigned int genuine_intel;
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
unsigned long long  gfx_cur_rc6_ms;
unsigned int gfx_cur_mhz;
unsigned int tcc_activation_temp;
unsigned int tcc_activation_temp_override;
double rapl_power_units, rapl_time_units;
double rapl_dram_energy_units, rapl_energy_units;
double rapl_joule_counter_range;
unsigned int do_core_perf_limit_reasons;
unsigned int do_gfx_perf_limit_reasons;
unsigned int do_ring_perf_limit_reasons;
unsigned int crystal_hz;
unsigned long long tsc_hz;
int base_cpu;
double discover_bclk(unsigned int family, unsigned int model);
unsigned int has_hwp;	/* IA32_PM_ENABLE, IA32_HWP_CAPABILITIES */
			/* IA32_HWP_REQUEST, IA32_HWP_STATUS */
unsigned int has_hwp_notify;		/* IA32_HWP_INTERRUPT */
unsigned int has_hwp_activity_window;	/* IA32_HWP_REQUEST[bits 41:32] */
unsigned int has_hwp_epp;		/* IA32_HWP_REQUEST[bits 31:24] */
unsigned int has_hwp_pkg;		/* IA32_HWP_REQUEST_PKG */

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
#define RAPL_CORES (RAPL_CORES_ENERGY_STATUS | RAPL_CORES_POWER_LIMIT)
#define	TJMAX_DEFAULT	100

#define MAX(a, b) ((a) > (b) ? (a) : (b))

/*
 * buffer size used by sscanf() for added column names
 * Usually truncated to 7 characters, but also handles 18 columns for raw 64-bit counters
 */
#define	NAME_BYTES 20

int backwards_count;
char *progname;

cpu_set_t *cpu_present_set, *cpu_affinity_set;
size_t cpu_present_setsize, cpu_affinity_setsize;
#define MAX_ADDED_COUNTERS 16

struct thread_data {
	unsigned long long tsc;
	unsigned long long aperf;
	unsigned long long mperf;
	unsigned long long c1;
	unsigned int irq_count;
	unsigned int smi_count;
	unsigned int cpu_id;
	unsigned int flags;
#define CPU_IS_FIRST_THREAD_IN_CORE	0x2
#define CPU_IS_FIRST_CORE_IN_PACKAGE	0x4
	unsigned long long counter[MAX_ADDED_COUNTERS];
} *thread_even, *thread_odd;

struct core_data {
	unsigned long long c3;
	unsigned long long c6;
	unsigned long long c7;
	unsigned long long mc6_us;	/* duplicate as per-core for now, even though per module */
	unsigned int core_temp_c;
	unsigned int core_id;
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
	unsigned long long pkg_wtd_core_c0;
	unsigned long long pkg_any_core_c0;
	unsigned long long pkg_any_gfxe_c0;
	unsigned long long pkg_both_core_gfxe_c0;
	long long gfx_rc6_ms;
	unsigned int gfx_mhz;
	unsigned int package_id;
	unsigned int energy_pkg;	/* MSR_PKG_ENERGY_STATUS */
	unsigned int energy_dram;	/* MSR_DRAM_ENERGY_STATUS */
	unsigned int energy_cores;	/* MSR_PP0_ENERGY_STATUS */
	unsigned int energy_gfx;	/* MSR_PP1_ENERGY_STATUS */
	unsigned int rapl_pkg_perf_status;	/* MSR_PKG_PERF_STATUS */
	unsigned int rapl_dram_perf_status;	/* MSR_DRAM_PERF_STATUS */
	unsigned int pkg_temp_c;
	unsigned long long counter[MAX_ADDED_COUNTERS];
} *package_even, *package_odd;

#define ODD_COUNTERS thread_odd, core_odd, package_odd
#define EVEN_COUNTERS thread_even, core_even, package_even

#define GET_THREAD(thread_base, thread_no, core_no, pkg_no) \
	(thread_base + (pkg_no) * topo.num_cores_per_pkg * \
		topo.num_threads_per_core + \
		(core_no) * topo.num_threads_per_core + (thread_no))
#define GET_CORE(core_base, core_no, pkg_no) \
	(core_base + (pkg_no) * topo.num_cores_per_pkg + (core_no))
#define GET_PKG(pkg_base, pkg_no) (pkg_base + pkg_no)

enum counter_scope {SCOPE_CPU, SCOPE_CORE, SCOPE_PACKAGE};
enum counter_type {COUNTER_CYCLES, COUNTER_SECONDS};
enum counter_format {FORMAT_RAW, FORMAT_DELTA, FORMAT_PERCENT};

struct msr_counter {
	unsigned int msr_num;
	char name[NAME_BYTES];
	unsigned int width;
	enum counter_type type;
	enum counter_format format;
	struct msr_counter *next;
	unsigned int flags;
#define	FLAGS_HIDE	(1 << 0)
#define	FLAGS_SHOW	(1 << 1)
};

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


struct topo_params {
	int num_packages;
	int num_cpus;
	int num_cores;
	int max_cpu_num;
	int num_cores_per_pkg;
	int num_threads_per_core;
} topo;

struct timeval tv_even, tv_odd, tv_delta;

int *irq_column_2_cpu;	/* /proc/interrupts column numbers */
int *irqs_per_cpu;		/* indexed by cpu_num */

void setup_all_buffers(void);

int cpu_is_not_present(int cpu)
{
	return !CPU_ISSET_S(cpu, cpu_present_setsize, cpu_present_set);
}
/*
 * run func(thread, core, package) in topology order
 * skip non-present cpus
 */

int for_all_cpus(int (func)(struct thread_data *, struct core_data *, struct pkg_data *),
	struct thread_data *thread_base, struct core_data *core_base, struct pkg_data *pkg_base)
{
	int retval, pkg_no, core_no, thread_no;

	for (pkg_no = 0; pkg_no < topo.num_packages; ++pkg_no) {
		for (core_no = 0; core_no < topo.num_cores_per_pkg; ++core_no) {
			for (thread_no = 0; thread_no <
				topo.num_threads_per_core; ++thread_no) {
				struct thread_data *t;
				struct core_data *c;
				struct pkg_data *p;

				t = GET_THREAD(thread_base, thread_no, core_no, pkg_no);

				if (cpu_is_not_present(t->cpu_id))
					continue;

				c = GET_CORE(core_base, core_no, pkg_no);
				p = GET_PKG(pkg_base, pkg_no);

				retval = func(t, c, p);
				if (retval)
					return retval;
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

int get_msr(int cpu, off_t offset, unsigned long long *msr)
{
	ssize_t retval;

	retval = pread(get_msr_fd(cpu), msr, sizeof(*msr), offset);

	if (retval != sizeof *msr)
		err(-1, "cpu%d: msr offset 0x%llx read failed", cpu, (unsigned long long)offset);

	return 0;
}

/*
 * Each string in this array is compared in --show and --hide cmdline.
 * Thus, strings that are proper sub-sets must follow their more specific peers.
 */
struct msr_counter bic[] = {
	{ 0x0, "Package" },
	{ 0x0, "Avg_MHz" },
	{ 0x0, "Bzy_MHz" },
	{ 0x0, "TSC_MHz" },
	{ 0x0, "IRQ" },
	{ 0x0, "SMI", 32, 0, FORMAT_DELTA, NULL},
	{ 0x0, "Busy%" },
	{ 0x0, "CPU%c1" },
	{ 0x0, "CPU%c3" },
	{ 0x0, "CPU%c6" },
	{ 0x0, "CPU%c7" },
	{ 0x0, "ThreadC" },
	{ 0x0, "CoreTmp" },
	{ 0x0, "CoreCnt" },
	{ 0x0, "PkgTmp" },
	{ 0x0, "GFX%rc6" },
	{ 0x0, "GFXMHz" },
	{ 0x0, "Pkg%pc2" },
	{ 0x0, "Pkg%pc3" },
	{ 0x0, "Pkg%pc6" },
	{ 0x0, "Pkg%pc7" },
	{ 0x0, "PkgWatt" },
	{ 0x0, "CorWatt" },
	{ 0x0, "GFXWatt" },
	{ 0x0, "PkgCnt" },
	{ 0x0, "RAMWatt" },
	{ 0x0, "PKG_%" },
	{ 0x0, "RAM_%" },
	{ 0x0, "Pkg_J" },
	{ 0x0, "Cor_J" },
	{ 0x0, "GFX_J" },
	{ 0x0, "RAM_J" },
	{ 0x0, "Core" },
	{ 0x0, "CPU" },
	{ 0x0, "Mod%c6" },
};

#define MAX_BIC (sizeof(bic) / sizeof(struct msr_counter))
#define	BIC_Package	(1ULL << 0)
#define	BIC_Avg_MHz	(1ULL << 1)
#define	BIC_Bzy_MHz	(1ULL << 2)
#define	BIC_TSC_MHz	(1ULL << 3)
#define	BIC_IRQ		(1ULL << 4)
#define	BIC_SMI		(1ULL << 5)
#define	BIC_Busy	(1ULL << 6)
#define	BIC_CPU_c1	(1ULL << 7)
#define	BIC_CPU_c3	(1ULL << 8)
#define	BIC_CPU_c6	(1ULL << 9)
#define	BIC_CPU_c7	(1ULL << 10)
#define	BIC_ThreadC	(1ULL << 11)
#define	BIC_CoreTmp	(1ULL << 12)
#define	BIC_CoreCnt	(1ULL << 13)
#define	BIC_PkgTmp	(1ULL << 14)
#define	BIC_GFX_rc6	(1ULL << 15)
#define	BIC_GFXMHz	(1ULL << 16)
#define	BIC_Pkgpc2	(1ULL << 17)
#define	BIC_Pkgpc3	(1ULL << 18)
#define	BIC_Pkgpc6	(1ULL << 19)
#define	BIC_Pkgpc7	(1ULL << 20)
#define	BIC_PkgWatt	(1ULL << 21)
#define	BIC_CorWatt	(1ULL << 22)
#define	BIC_GFXWatt	(1ULL << 23)
#define	BIC_PkgCnt	(1ULL << 24)
#define	BIC_RAMWatt	(1ULL << 27)
#define	BIC_PKG__	(1ULL << 28)
#define	BIC_RAM__	(1ULL << 29)
#define	BIC_Pkg_J	(1ULL << 30)
#define	BIC_Cor_J	(1ULL << 31)
#define	BIC_GFX_J	(1ULL << 30)
#define	BIC_RAM_J	(1ULL << 31)
#define	BIC_Core	(1ULL << 32)
#define	BIC_CPU		(1ULL << 33)
#define	BIC_Mod_c6	(1ULL << 34)

unsigned long long bic_enabled = 0xFFFFFFFFFFFFFFFFULL;
unsigned long long bic_present;

#define DO_BIC(COUNTER_NAME) (bic_enabled & bic_present & COUNTER_NAME)
#define BIC_PRESENT(COUNTER_BIT) (bic_present |= COUNTER_BIT)

/*
 * bic_lookup
 * for all the strings in comma separate name_list,
 * set the approprate bit in return value.
 */
unsigned long long bic_lookup(char *name_list)
{
	int i;
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
		}
		if (i == MAX_BIC) {
			fprintf(stderr, "Invalid counter name: %s\n", name_list);
			exit(-1);
		}

		name_list = comma;
		if (name_list)
			name_list++;

	}
	return retval;
}

void print_header(void)
{
	struct msr_counter *mp;

	if (DO_BIC(BIC_Package))
		outp += sprintf(outp, "\tPackage");
	if (DO_BIC(BIC_Core))
		outp += sprintf(outp, "\tCore");
	if (DO_BIC(BIC_CPU))
		outp += sprintf(outp, "\tCPU");
	if (DO_BIC(BIC_Avg_MHz))
		outp += sprintf(outp, "\tAvg_MHz");
	if (DO_BIC(BIC_Busy))
		outp += sprintf(outp, "\tBusy%%");
	if (DO_BIC(BIC_Bzy_MHz))
		outp += sprintf(outp, "\tBzy_MHz");
	if (DO_BIC(BIC_TSC_MHz))
		outp += sprintf(outp, "\tTSC_MHz");

	if (!debug)
		goto done;

	if (DO_BIC(BIC_IRQ))
		outp += sprintf(outp, "\tIRQ");
	if (DO_BIC(BIC_SMI))
		outp += sprintf(outp, "\tSMI");

	if (DO_BIC(BIC_CPU_c1))
		outp += sprintf(outp, "\tCPU%%c1");

	for (mp = sys.tp; mp; mp = mp->next) {
		if (mp->format == FORMAT_RAW) {
			if (mp->width == 64)
				outp += sprintf(outp, "\t%18.18s", mp->name);
			else
				outp += sprintf(outp, "\t%10.10s", mp->name);
		} else {
			outp += sprintf(outp, "\t%-7.7s", mp->name);
		}
	}

	if (DO_BIC(BIC_CPU_c3) && !do_slm_cstates && !do_knl_cstates)
		outp += sprintf(outp, "\tCPU%%c3");
	if (DO_BIC(BIC_CPU_c6))
		outp += sprintf(outp, "\tCPU%%c6");
	if (DO_BIC(BIC_CPU_c7))
		outp += sprintf(outp, "\tCPU%%c7");

	if (DO_BIC(BIC_Mod_c6))
		outp += sprintf(outp, "\tMod%%c6");

	if (DO_BIC(BIC_CoreTmp))
		outp += sprintf(outp, "\tCoreTmp");

	for (mp = sys.cp; mp; mp = mp->next) {
		if (mp->format == FORMAT_RAW) {
			if (mp->width == 64)
				outp += sprintf(outp, "\t%18.18s", mp->name);
			else
				outp += sprintf(outp, "\t%10.10s", mp->name);
		} else {
			outp += sprintf(outp, "\t%-7.7s", mp->name);
		}
	}

	if (DO_BIC(BIC_PkgTmp))
		outp += sprintf(outp, "\tPkgTmp");

	if (DO_BIC(BIC_GFX_rc6))
		outp += sprintf(outp, "\tGFX%%rc6");

	if (DO_BIC(BIC_GFXMHz))
		outp += sprintf(outp, "\tGFXMHz");

	if (do_skl_residency) {
		outp += sprintf(outp, "\tTotl%%C0");
		outp += sprintf(outp, "\tAny%%C0");
		outp += sprintf(outp, "\tGFX%%C0");
		outp += sprintf(outp, "\tCPUGFX%%");
	}

	if (do_pc2)
		outp += sprintf(outp, "\tPkg%%pc2");
	if (do_pc3)
		outp += sprintf(outp, "\tPkg%%pc3");
	if (do_pc6)
		outp += sprintf(outp, "\tPkg%%pc6");
	if (do_pc7)
		outp += sprintf(outp, "\tPkg%%pc7");
	if (do_c8_c9_c10) {
		outp += sprintf(outp, "\tPkg%%pc8");
		outp += sprintf(outp, "\tPkg%%pc9");
		outp += sprintf(outp, "\tPk%%pc10");
	}

	if (do_rapl && !rapl_joules) {
		if (DO_BIC(BIC_PkgWatt))
			outp += sprintf(outp, "\tPkgWatt");
		if (DO_BIC(BIC_CorWatt))
			outp += sprintf(outp, "\tCorWatt");
		if (DO_BIC(BIC_GFXWatt))
			outp += sprintf(outp, "\tGFXWatt");
		if (DO_BIC(BIC_RAMWatt))
			outp += sprintf(outp, "\tRAMWatt");
		if (DO_BIC(BIC_PKG__))
			outp += sprintf(outp, "\tPKG_%%");
		if (DO_BIC(BIC_RAM__))
			outp += sprintf(outp, "\tRAM_%%");
	} else if (do_rapl && rapl_joules) {
		if (DO_BIC(BIC_Pkg_J))
			outp += sprintf(outp, "\tPkg_J");
		if (DO_BIC(BIC_Cor_J))
			outp += sprintf(outp, "\tCor_J");
		if (DO_BIC(BIC_GFX_J))
			outp += sprintf(outp, "\tGFX_J");
		if (DO_BIC(BIC_RAM_J))
			outp += sprintf(outp, "\tRAM_J");
		if (DO_BIC(BIC_PKG__))
			outp += sprintf(outp, "\tPKG_%%");
		if (DO_BIC(BIC_RAM__))
			outp += sprintf(outp, "\tRAM_%%");
	}
	for (mp = sys.pp; mp; mp = mp->next) {
		if (mp->format == FORMAT_RAW) {
			if (mp->width == 64)
				outp += sprintf(outp, "\t%18.18s", mp->name);
			else
				outp += sprintf(outp, "\t%10.10s", mp->name);
		} else {
			outp += sprintf(outp, "\t%-7.7s", mp->name);
		}
	}

done:
	outp += sprintf(outp, "\n");
}

int dump_counters(struct thread_data *t, struct core_data *c,
	struct pkg_data *p)
{
	int i;
	struct msr_counter *mp;

	outp += sprintf(outp, "t %p, c %p, p %p\n", t, c, p);

	if (t) {
		outp += sprintf(outp, "CPU: %d flags 0x%x\n",
			t->cpu_id, t->flags);
		outp += sprintf(outp, "TSC: %016llX\n", t->tsc);
		outp += sprintf(outp, "aperf: %016llX\n", t->aperf);
		outp += sprintf(outp, "mperf: %016llX\n", t->mperf);
		outp += sprintf(outp, "c1: %016llX\n", t->c1);

		if (DO_BIC(BIC_IRQ))
			outp += sprintf(outp, "IRQ: %08X\n", t->irq_count);
		if (DO_BIC(BIC_SMI))
			outp += sprintf(outp, "SMI: %08X\n", t->smi_count);

		for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
			outp += sprintf(outp, "tADDED [%d] msr0x%x: %08llX\n",
				i, mp->msr_num, t->counter[i]);
		}
	}

	if (c) {
		outp += sprintf(outp, "core: %d\n", c->core_id);
		outp += sprintf(outp, "c3: %016llX\n", c->c3);
		outp += sprintf(outp, "c6: %016llX\n", c->c6);
		outp += sprintf(outp, "c7: %016llX\n", c->c7);
		outp += sprintf(outp, "DTS: %dC\n", c->core_temp_c);

		for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
			outp += sprintf(outp, "cADDED [%d] msr0x%x: %08llX\n",
				i, mp->msr_num, c->counter[i]);
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
		if (do_pc3)
			outp += sprintf(outp, "pc3: %016llX\n", p->pc3);
		if (do_pc6)
			outp += sprintf(outp, "pc6: %016llX\n", p->pc6);
		if (do_pc7)
			outp += sprintf(outp, "pc7: %016llX\n", p->pc7);
		outp += sprintf(outp, "pc8: %016llX\n", p->pc8);
		outp += sprintf(outp, "pc9: %016llX\n", p->pc9);
		outp += sprintf(outp, "pc10: %016llX\n", p->pc10);
		outp += sprintf(outp, "Joules PKG: %0X\n", p->energy_pkg);
		outp += sprintf(outp, "Joules COR: %0X\n", p->energy_cores);
		outp += sprintf(outp, "Joules GFX: %0X\n", p->energy_gfx);
		outp += sprintf(outp, "Joules RAM: %0X\n", p->energy_dram);
		outp += sprintf(outp, "Throttle PKG: %0X\n",
			p->rapl_pkg_perf_status);
		outp += sprintf(outp, "Throttle RAM: %0X\n",
			p->rapl_dram_perf_status);
		outp += sprintf(outp, "PTM: %dC\n", p->pkg_temp_c);

		for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
			outp += sprintf(outp, "pADDED [%d] msr0x%x: %08llX\n",
				i, mp->msr_num, p->counter[i]);
		}
	}

	outp += sprintf(outp, "\n");

	return 0;
}

/*
 * column formatting convention & formats
 */
int format_counters(struct thread_data *t, struct core_data *c,
	struct pkg_data *p)
{
	double interval_float;
	char *fmt8;
	int i;
	struct msr_counter *mp;

	 /* if showing only 1st thread in core and this isn't one, bail out */
	if (show_core_only && !(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		return 0;

	 /* if showing only 1st thread in pkg and this isn't one, bail out */
	if (show_pkg_only && !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	interval_float = tv_delta.tv_sec + tv_delta.tv_usec/1000000.0;

	/* topo columns, print blanks on 1st (average) line */
	if (t == &average.threads) {
		if (DO_BIC(BIC_Package))
			outp += sprintf(outp, "\t-");
		if (DO_BIC(BIC_Core))
			outp += sprintf(outp, "\t-");
		if (DO_BIC(BIC_CPU))
			outp += sprintf(outp, "\t-");
	} else {
		if (DO_BIC(BIC_Package)) {
			if (p)
				outp += sprintf(outp, "\t%d", p->package_id);
			else
				outp += sprintf(outp, "\t-");
		}
		if (DO_BIC(BIC_Core)) {
			if (c)
				outp += sprintf(outp, "\t%d", c->core_id);
			else
				outp += sprintf(outp, "\t-");
		}
		if (DO_BIC(BIC_CPU))
			outp += sprintf(outp, "\t%d", t->cpu_id);
	}

	if (DO_BIC(BIC_Avg_MHz))
		outp += sprintf(outp, "\t%.0f",
			1.0 / units * t->aperf / interval_float);

	if (DO_BIC(BIC_Busy))
		outp += sprintf(outp, "\t%.2f", 100.0 * t->mperf/t->tsc/tsc_tweak);

	if (DO_BIC(BIC_Bzy_MHz)) {
		if (has_base_hz)
			outp += sprintf(outp, "\t%.0f", base_hz / units * t->aperf / t->mperf);
		else
			outp += sprintf(outp, "\t%.0f",
				1.0 * t->tsc / units * t->aperf / t->mperf / interval_float);
	}

	if (DO_BIC(BIC_TSC_MHz))
		outp += sprintf(outp, "\t%.0f", 1.0 * t->tsc/units/interval_float);

	if (!debug)
		goto done;

	/* IRQ */
	if (DO_BIC(BIC_IRQ))
		outp += sprintf(outp, "\t%d", t->irq_count);

	/* SMI */
	if (DO_BIC(BIC_SMI))
		outp += sprintf(outp, "\t%d", t->smi_count);

	/* C1 */
	if (DO_BIC(BIC_CPU_c1))
		outp += sprintf(outp, "\t%.2f", 100.0 * t->c1/t->tsc);

	/* Added counters */
	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW) {
			if (mp->width == 32)
				outp += sprintf(outp, "\t0x%08lx", (unsigned long) t->counter[i]);
			else
				outp += sprintf(outp, "\t0x%016llx", t->counter[i]);
		} else if (mp->format == FORMAT_DELTA) {
			outp += sprintf(outp, "\t%lld", t->counter[i]);
		} else if (mp->format == FORMAT_PERCENT) {
			outp += sprintf(outp, "\t%.2f", 100.0 * t->counter[i]/t->tsc);
		}
	}

	/* print per-core data only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		goto done;

	if (DO_BIC(BIC_CPU_c3) && !do_slm_cstates && !do_knl_cstates)
		outp += sprintf(outp, "\t%.2f", 100.0 * c->c3/t->tsc);
	if (DO_BIC(BIC_CPU_c6))
		outp += sprintf(outp, "\t%.2f", 100.0 * c->c6/t->tsc);
	if (DO_BIC(BIC_CPU_c7))
		outp += sprintf(outp, "\t%.2f", 100.0 * c->c7/t->tsc);

	/* Mod%c6 */
	if (DO_BIC(BIC_Mod_c6))
		outp += sprintf(outp, "\t%.2f", 100.0 * c->mc6_us / t->tsc);

	if (DO_BIC(BIC_CoreTmp))
		outp += sprintf(outp, "\t%d", c->core_temp_c);

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW) {
			if (mp->width == 32)
				outp += sprintf(outp, "\t0x%08lx", (unsigned long) c->counter[i]);
			else
				outp += sprintf(outp, "\t0x%016llx", c->counter[i]);
		} else if (mp->format == FORMAT_DELTA) {
			outp += sprintf(outp, "\t%lld", c->counter[i]);
		} else if (mp->format == FORMAT_PERCENT) {
			outp += sprintf(outp, "\t%.2f", 100.0 * c->counter[i]/t->tsc);
		}
	}

	/* print per-package data only for 1st core in package */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		goto done;

	/* PkgTmp */
	if (DO_BIC(BIC_PkgTmp))
		outp += sprintf(outp, "\t%d", p->pkg_temp_c);

	/* GFXrc6 */
	if (DO_BIC(BIC_GFX_rc6)) {
		if (p->gfx_rc6_ms == -1) {	/* detect GFX counter reset */
			outp += sprintf(outp, "\t**.**");
		} else {
			outp += sprintf(outp, "\t%.2f",
				p->gfx_rc6_ms / 10.0 / interval_float);
		}
	}

	/* GFXMHz */
	if (DO_BIC(BIC_GFXMHz))
		outp += sprintf(outp, "\t%d", p->gfx_mhz);

	/* Totl%C0, Any%C0 GFX%C0 CPUGFX% */
	if (do_skl_residency) {
		outp += sprintf(outp, "\t%.2f", 100.0 * p->pkg_wtd_core_c0/t->tsc);
		outp += sprintf(outp, "\t%.2f", 100.0 * p->pkg_any_core_c0/t->tsc);
		outp += sprintf(outp, "\t%.2f", 100.0 * p->pkg_any_gfxe_c0/t->tsc);
		outp += sprintf(outp, "\t%.2f", 100.0 * p->pkg_both_core_gfxe_c0/t->tsc);
	}

	if (do_pc2)
		outp += sprintf(outp, "\t%.2f", 100.0 * p->pc2/t->tsc);
	if (do_pc3)
		outp += sprintf(outp, "\t%.2f", 100.0 * p->pc3/t->tsc);
	if (do_pc6)
		outp += sprintf(outp, "\t%.2f", 100.0 * p->pc6/t->tsc);
	if (do_pc7)
		outp += sprintf(outp, "\t%.2f", 100.0 * p->pc7/t->tsc);
	if (do_c8_c9_c10) {
		outp += sprintf(outp, "\t%.2f", 100.0 * p->pc8/t->tsc);
		outp += sprintf(outp, "\t%.2f", 100.0 * p->pc9/t->tsc);
		outp += sprintf(outp, "\t%.2f", 100.0 * p->pc10/t->tsc);
	}

	/*
 	 * If measurement interval exceeds minimum RAPL Joule Counter range,
 	 * indicate that results are suspect by printing "**" in fraction place.
 	 */
	if (interval_float < rapl_joule_counter_range)
		fmt8 = "\t%.2f";
	else
		fmt8 = "%6.0f**";

	if (DO_BIC(BIC_PkgWatt))
		outp += sprintf(outp, fmt8, p->energy_pkg * rapl_energy_units / interval_float);
	if (DO_BIC(BIC_CorWatt))
		outp += sprintf(outp, fmt8, p->energy_cores * rapl_energy_units / interval_float);
	if (DO_BIC(BIC_GFXWatt))
		outp += sprintf(outp, fmt8, p->energy_gfx * rapl_energy_units / interval_float);
	if (DO_BIC(BIC_RAMWatt))
		outp += sprintf(outp, fmt8, p->energy_dram * rapl_dram_energy_units / interval_float);
	if (DO_BIC(BIC_Pkg_J))
		outp += sprintf(outp, fmt8, p->energy_pkg * rapl_energy_units);
	if (DO_BIC(BIC_Cor_J))
		outp += sprintf(outp, fmt8, p->energy_cores * rapl_energy_units);
	if (DO_BIC(BIC_GFX_J))
		outp += sprintf(outp, fmt8, p->energy_gfx * rapl_energy_units);
	if (DO_BIC(BIC_RAM_J))
		outp += sprintf(outp, fmt8, p->energy_dram * rapl_dram_energy_units);
	if (DO_BIC(BIC_PKG__))
		outp += sprintf(outp, fmt8, 100.0 * p->rapl_pkg_perf_status * rapl_time_units / interval_float);
	if (DO_BIC(BIC_RAM__))
		outp += sprintf(outp, fmt8, 100.0 * p->rapl_dram_perf_status * rapl_time_units / interval_float);

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW) {
			if (mp->width == 32)
				outp += sprintf(outp, "\t0x%08lx", (unsigned long) p->counter[i]);
			else
				outp += sprintf(outp, "\t0x%016llx", p->counter[i]);
		} else if (mp->format == FORMAT_DELTA) {
			outp += sprintf(outp, "\t%lld", p->counter[i]);
		} else if (mp->format == FORMAT_PERCENT) {
			outp += sprintf(outp, "\t%.2f", 100.0 * p->counter[i]/t->tsc);
		}
	}

done:
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
	static int printed;

	if (!printed || !summary_only)
		print_header();

	if (topo.num_cpus > 1)
		format_counters(&average.threads, &average.cores,
			&average.packages);

	printed = 1;

	if (summary_only)
		return;

	for_all_cpus(format_counters, t, c, p);
}

#define DELTA_WRAP32(new, old)			\
	if (new > old) {			\
		old = new - old;		\
	} else {				\
		old = 0x100000000 + new - old;	\
	}

int
delta_package(struct pkg_data *new, struct pkg_data *old)
{
	int i;
	struct msr_counter *mp;

	if (do_skl_residency) {
		old->pkg_wtd_core_c0 = new->pkg_wtd_core_c0 - old->pkg_wtd_core_c0;
		old->pkg_any_core_c0 = new->pkg_any_core_c0 - old->pkg_any_core_c0;
		old->pkg_any_gfxe_c0 = new->pkg_any_gfxe_c0 - old->pkg_any_gfxe_c0;
		old->pkg_both_core_gfxe_c0 = new->pkg_both_core_gfxe_c0 - old->pkg_both_core_gfxe_c0;
	}
	old->pc2 = new->pc2 - old->pc2;
	if (do_pc3)
		old->pc3 = new->pc3 - old->pc3;
	if (do_pc6)
		old->pc6 = new->pc6 - old->pc6;
	if (do_pc7)
		old->pc7 = new->pc7 - old->pc7;
	old->pc8 = new->pc8 - old->pc8;
	old->pc9 = new->pc9 - old->pc9;
	old->pc10 = new->pc10 - old->pc10;
	old->pkg_temp_c = new->pkg_temp_c;

	/* flag an error when rc6 counter resets/wraps */
	if (old->gfx_rc6_ms >  new->gfx_rc6_ms)
		old->gfx_rc6_ms = -1;
	else
		old->gfx_rc6_ms = new->gfx_rc6_ms - old->gfx_rc6_ms;

	old->gfx_mhz = new->gfx_mhz;

	DELTA_WRAP32(new->energy_pkg, old->energy_pkg);
	DELTA_WRAP32(new->energy_cores, old->energy_cores);
	DELTA_WRAP32(new->energy_gfx, old->energy_gfx);
	DELTA_WRAP32(new->energy_dram, old->energy_dram);
	DELTA_WRAP32(new->rapl_pkg_perf_status, old->rapl_pkg_perf_status);
	DELTA_WRAP32(new->rapl_dram_perf_status, old->rapl_dram_perf_status);

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			old->counter[i] = new->counter[i];
		else
			old->counter[i] = new->counter[i] - old->counter[i];
	}

	return 0;
}

void
delta_core(struct core_data *new, struct core_data *old)
{
	int i;
	struct msr_counter *mp;

	old->c3 = new->c3 - old->c3;
	old->c6 = new->c6 - old->c6;
	old->c7 = new->c7 - old->c7;
	old->core_temp_c = new->core_temp_c;
	old->mc6_us = new->mc6_us - old->mc6_us;

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			old->counter[i] = new->counter[i];
		else
			old->counter[i] = new->counter[i] - old->counter[i];
	}
}

/*
 * old = new - old
 */
int
delta_thread(struct thread_data *new, struct thread_data *old,
	struct core_data *core_delta)
{
	int i;
	struct msr_counter *mp;

	old->tsc = new->tsc - old->tsc;

	/* check for TSC < 1 Mcycles over interval */
	if (old->tsc < (1000 * 1000))
		errx(-3, "Insanely slow TSC rate, TSC stops in idle?\n"
		     "You can disable all c-states by booting with \"idle=poll\"\n"
		     "or just the deep ones with \"processor.max_cstate=1\"");

	old->c1 = new->c1 - old->c1;

	if (DO_BIC(BIC_Avg_MHz) || DO_BIC(BIC_Busy) || DO_BIC(BIC_Bzy_MHz)) {
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
		if ((old->mperf + core_delta->c3 + core_delta->c6 + core_delta->c7) > old->tsc)
			old->c1 = 0;
		else {
			/* normal case, derive c1 */
			old->c1 = old->tsc - old->mperf - core_delta->c3
				- core_delta->c6 - core_delta->c7;
		}
	}

	if (old->mperf == 0) {
		if (debug > 1)
			fprintf(outf, "cpu%d MPERF 0!\n", old->cpu_id);
		old->mperf = 1;	/* divide by 0 protection */
	}

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
	struct pkg_data *p, struct thread_data *t2,
	struct core_data *c2, struct pkg_data *p2)
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
	struct msr_counter  *mp;

	t->tsc = 0;
	t->aperf = 0;
	t->mperf = 0;
	t->c1 = 0;

	t->irq_count = 0;
	t->smi_count = 0;

	/* tells format_counters to dump all fields from this set */
	t->flags = CPU_IS_FIRST_THREAD_IN_CORE | CPU_IS_FIRST_CORE_IN_PACKAGE;

	c->c3 = 0;
	c->c6 = 0;
	c->c7 = 0;
	c->mc6_us = 0;
	c->core_temp_c = 0;

	p->pkg_wtd_core_c0 = 0;
	p->pkg_any_core_c0 = 0;
	p->pkg_any_gfxe_c0 = 0;
	p->pkg_both_core_gfxe_c0 = 0;

	p->pc2 = 0;
	if (do_pc3)
		p->pc3 = 0;
	if (do_pc6)
		p->pc6 = 0;
	if (do_pc7)
		p->pc7 = 0;
	p->pc8 = 0;
	p->pc9 = 0;
	p->pc10 = 0;

	p->energy_pkg = 0;
	p->energy_dram = 0;
	p->energy_cores = 0;
	p->energy_gfx = 0;
	p->rapl_pkg_perf_status = 0;
	p->rapl_dram_perf_status = 0;
	p->pkg_temp_c = 0;

	p->gfx_rc6_ms = 0;
	p->gfx_mhz = 0;
	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next)
		t->counter[i] = 0;

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next)
		c->counter[i] = 0;

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next)
		p->counter[i] = 0;
}
int sum_counters(struct thread_data *t, struct core_data *c,
	struct pkg_data *p)
{
	int i;
	struct msr_counter *mp;

	average.threads.tsc += t->tsc;
	average.threads.aperf += t->aperf;
	average.threads.mperf += t->mperf;
	average.threads.c1 += t->c1;

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

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		average.cores.counter[i] += c->counter[i];
	}

	/* sum per-pkg values only for 1st core in pkg */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (do_skl_residency) {
		average.packages.pkg_wtd_core_c0 += p->pkg_wtd_core_c0;
		average.packages.pkg_any_core_c0 += p->pkg_any_core_c0;
		average.packages.pkg_any_gfxe_c0 += p->pkg_any_gfxe_c0;
		average.packages.pkg_both_core_gfxe_c0 += p->pkg_both_core_gfxe_c0;
	}

	average.packages.pc2 += p->pc2;
	if (do_pc3)
		average.packages.pc3 += p->pc3;
	if (do_pc6)
		average.packages.pc6 += p->pc6;
	if (do_pc7)
		average.packages.pc7 += p->pc7;
	average.packages.pc8 += p->pc8;
	average.packages.pc9 += p->pc9;
	average.packages.pc10 += p->pc10;

	average.packages.energy_pkg += p->energy_pkg;
	average.packages.energy_dram += p->energy_dram;
	average.packages.energy_cores += p->energy_cores;
	average.packages.energy_gfx += p->energy_gfx;

	average.packages.gfx_rc6_ms = p->gfx_rc6_ms;
	average.packages.gfx_mhz = p->gfx_mhz;

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
void compute_average(struct thread_data *t, struct core_data *c,
	struct pkg_data *p)
{
	int i;
	struct msr_counter *mp;

	clear_counters(&average.threads, &average.cores, &average.packages);

	for_all_cpus(sum_counters, t, c, p);

	average.threads.tsc /= topo.num_cpus;
	average.threads.aperf /= topo.num_cpus;
	average.threads.mperf /= topo.num_cpus;
	average.threads.c1 /= topo.num_cpus;

	average.cores.c3 /= topo.num_cores;
	average.cores.c6 /= topo.num_cores;
	average.cores.c7 /= topo.num_cores;
	average.cores.mc6_us /= topo.num_cores;

	if (do_skl_residency) {
		average.packages.pkg_wtd_core_c0 /= topo.num_packages;
		average.packages.pkg_any_core_c0 /= topo.num_packages;
		average.packages.pkg_any_gfxe_c0 /= topo.num_packages;
		average.packages.pkg_both_core_gfxe_c0 /= topo.num_packages;
	}

	average.packages.pc2 /= topo.num_packages;
	if (do_pc3)
		average.packages.pc3 /= topo.num_packages;
	if (do_pc6)
		average.packages.pc6 /= topo.num_packages;
	if (do_pc7)
		average.packages.pc7 /= topo.num_packages;

	average.packages.pc8 /= topo.num_packages;
	average.packages.pc9 /= topo.num_packages;
	average.packages.pc10 /= topo.num_packages;

	for (i = 0, mp = sys.tp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		average.threads.counter[i] /= topo.num_cpus;
	}
	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		average.cores.counter[i] /= topo.num_cores;
	}
	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if (mp->format == FORMAT_RAW)
			continue;
		average.packages.counter[i] /= topo.num_packages;
	}
}

static unsigned long long rdtsc(void)
{
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((unsigned long long)high) << 32;
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
		fprintf(outf, "Could not migrate to CPU %d\n", cpu);
		return -1;
	}

retry:
	t->tsc = rdtsc();	/* we are running on local CPU of interest */

	if (DO_BIC(BIC_Avg_MHz) || DO_BIC(BIC_Busy) || DO_BIC(BIC_Bzy_MHz)) {
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
				warnx("cpu%d jitter %lld %lld",
					cpu, aperf_time, mperf_time);
		}
		aperf_mperf_retry_count = 0;

		t->aperf = t->aperf * aperf_mperf_multiplier;
		t->mperf = t->mperf * aperf_mperf_multiplier;
	}

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
		if (get_msr(cpu, mp->msr_num, &t->counter[i]))
			return -10;
	}


	/* collect core counters only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		return 0;

	if (DO_BIC(BIC_CPU_c3) && !do_slm_cstates && !do_knl_cstates) {
		if (get_msr(cpu, MSR_CORE_C3_RESIDENCY, &c->c3))
			return -6;
	}

	if (DO_BIC(BIC_CPU_c6) && !do_knl_cstates) {
		if (get_msr(cpu, MSR_CORE_C6_RESIDENCY, &c->c6))
			return -7;
	} else if (do_knl_cstates) {
		if (get_msr(cpu, MSR_KNL_CORE_C6_RESIDENCY, &c->c6))
			return -7;
	}

	if (DO_BIC(BIC_CPU_c7))
		if (get_msr(cpu, MSR_CORE_C7_RESIDENCY, &c->c7))
			return -8;

	if (DO_BIC(BIC_Mod_c6))
		if (get_msr(cpu, MSR_MODULE_C6_RES_MS, &c->mc6_us))
			return -8;

	if (DO_BIC(BIC_CoreTmp)) {
		if (get_msr(cpu, MSR_IA32_THERM_STATUS, &msr))
			return -9;
		c->core_temp_c = tcc_activation_temp - ((msr >> 16) & 0x7F);
	}

	for (i = 0, mp = sys.cp; mp; i++, mp = mp->next) {
		if (get_msr(cpu, mp->msr_num, &c->counter[i]))
			return -10;
	}

	/* collect package counters only for 1st core in package */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (do_skl_residency) {
		if (get_msr(cpu, MSR_PKG_WEIGHTED_CORE_C0_RES, &p->pkg_wtd_core_c0))
			return -10;
		if (get_msr(cpu, MSR_PKG_ANY_CORE_C0_RES, &p->pkg_any_core_c0))
			return -11;
		if (get_msr(cpu, MSR_PKG_ANY_GFXE_C0_RES, &p->pkg_any_gfxe_c0))
			return -12;
		if (get_msr(cpu, MSR_PKG_BOTH_CORE_GFXE_C0_RES, &p->pkg_both_core_gfxe_c0))
			return -13;
	}
	if (do_pc3)
		if (get_msr(cpu, MSR_PKG_C3_RESIDENCY, &p->pc3))
			return -9;
	if (do_pc6) {
		if (do_slm_cstates) {
			if (get_msr(cpu, MSR_ATOM_PKG_C6_RESIDENCY, &p->pc6))
				return -10;
		} else {
			if (get_msr(cpu, MSR_PKG_C6_RESIDENCY, &p->pc6))
				return -10;
		}
	}

	if (do_pc2)
		if (get_msr(cpu, MSR_PKG_C2_RESIDENCY, &p->pc2))
			return -11;
	if (do_pc7)
		if (get_msr(cpu, MSR_PKG_C7_RESIDENCY, &p->pc7))
			return -12;
	if (do_c8_c9_c10) {
		if (get_msr(cpu, MSR_PKG_C8_RESIDENCY, &p->pc8))
			return -13;
		if (get_msr(cpu, MSR_PKG_C9_RESIDENCY, &p->pc9))
			return -13;
		if (get_msr(cpu, MSR_PKG_C10_RESIDENCY, &p->pc10))
			return -13;
	}
	if (do_rapl & RAPL_PKG) {
		if (get_msr(cpu, MSR_PKG_ENERGY_STATUS, &msr))
			return -13;
		p->energy_pkg = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_CORES_ENERGY_STATUS) {
		if (get_msr(cpu, MSR_PP0_ENERGY_STATUS, &msr))
			return -14;
		p->energy_cores = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_DRAM) {
		if (get_msr(cpu, MSR_DRAM_ENERGY_STATUS, &msr))
			return -15;
		p->energy_dram = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_GFX) {
		if (get_msr(cpu, MSR_PP1_ENERGY_STATUS, &msr))
			return -16;
		p->energy_gfx = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_PKG_PERF_STATUS) {
		if (get_msr(cpu, MSR_PKG_PERF_STATUS, &msr))
			return -16;
		p->rapl_pkg_perf_status = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_DRAM_PERF_STATUS) {
		if (get_msr(cpu, MSR_DRAM_PERF_STATUS, &msr))
			return -16;
		p->rapl_dram_perf_status = msr & 0xFFFFFFFF;
	}
	if (DO_BIC(BIC_PkgTmp)) {
		if (get_msr(cpu, MSR_IA32_PACKAGE_THERM_STATUS, &msr))
			return -17;
		p->pkg_temp_c = tcc_activation_temp - ((msr >> 16) & 0x7F);
	}

	if (DO_BIC(BIC_GFX_rc6))
		p->gfx_rc6_ms = gfx_cur_rc6_ms;

	if (DO_BIC(BIC_GFXMHz))
		p->gfx_mhz = gfx_cur_mhz;

	for (i = 0, mp = sys.pp; mp; i++, mp = mp->next) {
		if (get_msr(cpu, mp->msr_num, &p->counter[i]))
			return -10;
	}

	return 0;
}

/*
 * MSR_PKG_CST_CONFIG_CONTROL decoding for pkg_cstate_limit:
 * If you change the values, note they are used both in comparisons
 * (>= PCL__7) and to index pkg_cstate_limit_strings[].
 */

#define PCLUKN 0 /* Unknown */
#define PCLRSV 1 /* Reserved */
#define PCL__0 2 /* PC0 */
#define PCL__1 3 /* PC1 */
#define PCL__2 4 /* PC2 */
#define PCL__3 5 /* PC3 */
#define PCL__4 6 /* PC4 */
#define PCL__6 7 /* PC6 */
#define PCL_6N 8 /* PC6 No Retention */
#define PCL_6R 9 /* PC6 Retention */
#define PCL__7 10 /* PC7 */
#define PCL_7S 11 /* PC7 Shrink */
#define PCL__8 12 /* PC8 */
#define PCL__9 13 /* PC9 */
#define PCLUNL 14 /* Unlimited */

int pkg_cstate_limit = PCLUKN;
char *pkg_cstate_limit_strings[] = { "reserved", "unknown", "pc0", "pc1", "pc2",
	"pc3", "pc4", "pc6", "pc6n", "pc6r", "pc7", "pc7s", "pc8", "pc9", "unlimited"};

int nhm_pkg_cstate_limits[16] = {PCL__0, PCL__1, PCL__3, PCL__6, PCL__7, PCLRSV, PCLRSV, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV};
int snb_pkg_cstate_limits[16] = {PCL__0, PCL__2, PCL_6N, PCL_6R, PCL__7, PCL_7S, PCLRSV, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV};
int hsw_pkg_cstate_limits[16] = {PCL__0, PCL__2, PCL__3, PCL__6, PCL__7, PCL_7S, PCL__8, PCL__9, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV};
int slv_pkg_cstate_limits[16] = {PCL__0, PCL__1, PCLRSV, PCLRSV, PCL__4, PCLRSV, PCL__6, PCL__7, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCL__6, PCL__7};
int amt_pkg_cstate_limits[16] = {PCLUNL, PCL__1, PCL__2, PCLRSV, PCLRSV, PCLRSV, PCL__6, PCL__7, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV};
int phi_pkg_cstate_limits[16] = {PCL__0, PCL__2, PCL_6N, PCL_6R, PCLRSV, PCLRSV, PCLRSV, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV};
int bxt_pkg_cstate_limits[16] = {PCL__0, PCL__2, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV};
int skx_pkg_cstate_limits[16] = {PCL__0, PCL__2, PCL_6N, PCL_6R, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLUNL, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV, PCLRSV};


static void
calculate_tsc_tweak()
{
	tsc_tweak = base_hz / tsc_hz;
}

static void
dump_nhm_platform_info(void)
{
	unsigned long long msr;
	unsigned int ratio;

	get_msr(base_cpu, MSR_PLATFORM_INFO, &msr);

	fprintf(outf, "cpu%d: MSR_PLATFORM_INFO: 0x%08llx\n", base_cpu, msr);

	ratio = (msr >> 40) & 0xFF;
	fprintf(outf, "%d * %.1f = %.1f MHz max efficiency frequency\n",
		ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0xFF;
	fprintf(outf, "%d * %.1f = %.1f MHz base frequency\n",
		ratio, bclk, ratio * bclk);

	get_msr(base_cpu, MSR_IA32_POWER_CTL, &msr);
	fprintf(outf, "cpu%d: MSR_IA32_POWER_CTL: 0x%08llx (C1E auto-promotion: %sabled)\n",
		base_cpu, msr, msr & 0x2 ? "EN" : "DIS");

	return;
}

static void
dump_hsw_turbo_ratio_limits(void)
{
	unsigned long long msr;
	unsigned int ratio;

	get_msr(base_cpu, MSR_TURBO_RATIO_LIMIT2, &msr);

	fprintf(outf, "cpu%d: MSR_TURBO_RATIO_LIMIT2: 0x%08llx\n", base_cpu, msr);

	ratio = (msr >> 8) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 18 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 0) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 17 active cores\n",
			ratio, bclk, ratio * bclk);
	return;
}

static void
dump_ivt_turbo_ratio_limits(void)
{
	unsigned long long msr;
	unsigned int ratio;

	get_msr(base_cpu, MSR_TURBO_RATIO_LIMIT1, &msr);

	fprintf(outf, "cpu%d: MSR_TURBO_RATIO_LIMIT1: 0x%08llx\n", base_cpu, msr);

	ratio = (msr >> 56) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 16 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 48) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 15 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 40) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 14 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 32) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 13 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 24) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 12 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 16) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 11 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 10 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 0) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 9 active cores\n",
			ratio, bclk, ratio * bclk);
	return;
}

static void
dump_nhm_turbo_ratio_limits(void)
{
	unsigned long long msr;
	unsigned int ratio;

	get_msr(base_cpu, MSR_TURBO_RATIO_LIMIT, &msr);

	fprintf(outf, "cpu%d: MSR_TURBO_RATIO_LIMIT: 0x%08llx\n", base_cpu, msr);

	ratio = (msr >> 56) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 8 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 48) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 7 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 40) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 6 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 32) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 5 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 24) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 4 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 16) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 3 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 2 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 0) & 0xFF;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 1 active cores\n",
			ratio, bclk, ratio * bclk);
	return;
}

static void
dump_atom_turbo_ratio_limits(void)
{
	unsigned long long msr;
	unsigned int ratio;

	get_msr(base_cpu, MSR_ATOM_CORE_RATIOS, &msr);
	fprintf(outf, "cpu%d: MSR_ATOM_CORE_RATIOS: 0x%08llx\n", base_cpu, msr & 0xFFFFFFFF);

	ratio = (msr >> 0) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz minimum operating frequency\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz low frequency mode (LFM)\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 16) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz base frequency\n",
			ratio, bclk, ratio * bclk);

	get_msr(base_cpu, MSR_ATOM_CORE_TURBO_RATIOS, &msr);
	fprintf(outf, "cpu%d: MSR_ATOM_CORE_TURBO_RATIOS: 0x%08llx\n", base_cpu, msr & 0xFFFFFFFF);

	ratio = (msr >> 24) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 4 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 16) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 3 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 2 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 0) & 0x3F;
	if (ratio)
		fprintf(outf, "%d * %.1f = %.1f MHz max turbo 1 active core\n",
			ratio, bclk, ratio * bclk);
}

static void
dump_knl_turbo_ratio_limits(void)
{
	const unsigned int buckets_no = 7;

	unsigned long long msr;
	int delta_cores, delta_ratio;
	int i, b_nr;
	unsigned int cores[buckets_no];
	unsigned int ratio[buckets_no];

	get_msr(base_cpu, MSR_TURBO_RATIO_LIMIT, &msr);

	fprintf(outf, "cpu%d: MSR_TURBO_RATIO_LIMIT: 0x%08llx\n",
		base_cpu, msr);

	/**
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

static void
dump_nhm_cst_cfg(void)
{
	unsigned long long msr;

	get_msr(base_cpu, MSR_PKG_CST_CONFIG_CONTROL, &msr);

#define SNB_C1_AUTO_UNDEMOTE              (1UL << 27)
#define SNB_C3_AUTO_UNDEMOTE              (1UL << 28)

	fprintf(outf, "cpu%d: MSR_PKG_CST_CONFIG_CONTROL: 0x%08llx", base_cpu, msr);

	fprintf(outf, " (%s%s%s%s%slocked: pkg-cstate-limit=%d: %s)\n",
		(msr & SNB_C3_AUTO_UNDEMOTE) ? "UNdemote-C3, " : "",
		(msr & SNB_C1_AUTO_UNDEMOTE) ? "UNdemote-C1, " : "",
		(msr & NHM_C3_AUTO_DEMOTE) ? "demote-C3, " : "",
		(msr & NHM_C1_AUTO_DEMOTE) ? "demote-C1, " : "",
		(msr & (1 << 15)) ? "" : "UN",
		(unsigned int)msr & 0xF,
		pkg_cstate_limit_strings[pkg_cstate_limit]);
	return;
}

static void
dump_config_tdp(void)
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

unsigned int irtl_time_units[] = {1, 32, 1024, 32768, 1048576, 33554432, 0, 0 };

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
 * Parse a file containing a single int.
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
	filep = fopen_or_die(path, "r");
	if (fscanf(filep, "%d", &value) != 1)
		err(1, "%s: failed to parse number from file", path);
	fclose(filep);
	return value;
}

/*
 * get_cpu_position_in_core(cpu)
 * return the position of the CPU among its HT siblings in the core
 * return -1 if the sibling is not in list
 */
int get_cpu_position_in_core(int cpu)
{
	char path[64];
	FILE *filep;
	int this_cpu;
	char character;
	int i;

	sprintf(path,
		"/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list",
		cpu);
	filep = fopen(path, "r");
	if (filep == NULL) {
		perror(path);
		exit(1);
	}

	for (i = 0; i < topo.num_threads_per_core; i++) {
		fscanf(filep, "%d", &this_cpu);
		if (this_cpu == cpu) {
			fclose(filep);
			return i;
		}

		/* Account for no separator after last thread*/
		if (i != (topo.num_threads_per_core - 1))
			fscanf(filep, "%c", &character);
	}

	fclose(filep);
	return -1;
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

int get_core_id(int cpu)
{
	return parse_int_file("/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
}

int get_num_ht_siblings(int cpu)
{
	char path[80];
	FILE *filep;
	int sib1;
	int matches = 0;
	char character;
	char str[100];
	char *ch;

	sprintf(path, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
	filep = fopen_or_die(path, "r");

	/*
	 * file format:
	 * A ',' separated or '-' separated set of numbers
	 * (eg 1-2 or 1,3,4,5)
	 */
	fscanf(filep, "%d%c\n", &sib1, &character);
	fseek(filep, 0, SEEK_SET);
	fgets(str, 100, filep);
	ch = strchr(str, character);
	while (ch != NULL) {
		matches++;
		ch = strchr(ch+1, character);
	}

	fclose(filep);
	return matches+1;
}

/*
 * run func(thread, core, package) in topology order
 * skip non-present cpus
 */

int for_all_cpus_2(int (func)(struct thread_data *, struct core_data *,
	struct pkg_data *, struct thread_data *, struct core_data *,
	struct pkg_data *), struct thread_data *thread_base,
	struct core_data *core_base, struct pkg_data *pkg_base,
	struct thread_data *thread_base2, struct core_data *core_base2,
	struct pkg_data *pkg_base2)
{
	int retval, pkg_no, core_no, thread_no;

	for (pkg_no = 0; pkg_no < topo.num_packages; ++pkg_no) {
		for (core_no = 0; core_no < topo.num_cores_per_pkg; ++core_no) {
			for (thread_no = 0; thread_no <
				topo.num_threads_per_core; ++thread_no) {
				struct thread_data *t, *t2;
				struct core_data *c, *c2;
				struct pkg_data *p, *p2;

				t = GET_THREAD(thread_base, thread_no, core_no, pkg_no);

				if (cpu_is_not_present(t->cpu_id))
					continue;

				t2 = GET_THREAD(thread_base2, thread_no, core_no, pkg_no);

				c = GET_CORE(core_base, core_no, pkg_no);
				c2 = GET_CORE(core_base2, core_no, pkg_no);

				p = GET_PKG(pkg_base, pkg_no);
				p2 = GET_PKG(pkg_base2, pkg_no);

				retval = func(t, c, p, t2, c2, p2);
				if (retval)
					return retval;
			}
		}
	}
	return 0;
}

/*
 * run func(cpu) on every cpu in /proc/stat
 * return max_cpu number
 */
int for_all_proc_cpus(int (func)(int))
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
			return(retval);
		}
	}
	fclose(fp);
	return 0;
}

void re_initialize(void)
{
	free_all_buffers();
	setup_all_buffers();
	printf("turbostat: re-initialized with num_cpus %d\n", topo.num_cpus);
}


/*
 * count_cpus()
 * remember the last one seen, it will be the max
 */
int count_cpus(int cpu)
{
	if (topo.max_cpu_num < cpu)
		topo.max_cpu_num = cpu;

	topo.num_cpus += 1;
	return 0;
}
int mark_cpu_present(int cpu)
{
	CPU_SET_S(cpu, cpu_present_setsize, cpu_present_set);
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

		while (getc(fp) != '\n')
			;	/* flush interrupt description */

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
	else
		rewind(fp);

	retval = fscanf(fp, "%d", &gfx_cur_mhz);
	if (retval != 1)
		err(1, "GFX MHz");

	return 0;
}

/*
 * snapshot /proc and /sys files
 *
 * return 1 if configuration restart needed, else return 0
 */
int snapshot_proc_sysfs_files(void)
{
	if (snapshot_proc_interrupts())
		return 1;

	if (DO_BIC(BIC_GFX_rc6))
		snapshot_gfx_rc6_ms();

	if (DO_BIC(BIC_GFXMHz))
		snapshot_gfx_mhz();

	return 0;
}

void turbostat_loop()
{
	int retval;
	int restarted = 0;

restart:
	restarted++;

	snapshot_proc_sysfs_files();
	retval = for_all_cpus(get_counters, EVEN_COUNTERS);
	if (retval < -1) {
		exit(retval);
	} else if (retval == -1) {
		if (restarted > 1) {
			exit(retval);
		}
		re_initialize();
		goto restart;
	}
	restarted = 0;
	gettimeofday(&tv_even, (struct timezone *)NULL);

	while (1) {
		if (for_all_proc_cpus(cpu_is_not_present)) {
			re_initialize();
			goto restart;
		}
		nanosleep(&interval_ts, NULL);
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
		nanosleep(&interval_ts, NULL);
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

void check_permissions()
{
	struct __user_cap_header_struct cap_header_data;
	cap_user_header_t cap_header = &cap_header_data;
	struct __user_cap_data_struct cap_data_data;
	cap_user_data_t cap_data = &cap_data_data;
	extern int capget(cap_user_header_t hdrp, cap_user_data_t datap);
	int do_exit = 0;
	char pathname[32];

	/* check for CAP_SYS_RAWIO */
	cap_header->pid = getpid();
	cap_header->version = _LINUX_CAPABILITY_VERSION;
	if (capget(cap_header, cap_data) < 0)
		err(-6, "capget(2) failed");

	if ((cap_data->effective & (1 << CAP_SYS_RAWIO)) == 0) {
		do_exit++;
		warnx("capget(CAP_SYS_RAWIO) failed,"
			" try \"# setcap cap_sys_rawio=ep %s\"", progname);
	}

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
	case INTEL_FAM6_NEHALEM_EP:	/* Core i7, Xeon 5500 series - Bloomfield, Gainstown NHM-EP */
	case INTEL_FAM6_NEHALEM:	/* Core i7 and i5 Processor - Clarksfield, Lynnfield, Jasper Forest */
	case 0x1F:	/* Core i7 and i5 Processor - Nehalem */
	case INTEL_FAM6_WESTMERE:	/* Westmere Client - Clarkdale, Arrandale */
	case INTEL_FAM6_WESTMERE_EP:	/* Westmere EP - Gulftown */
	case INTEL_FAM6_NEHALEM_EX:	/* Nehalem-EX Xeon - Beckton */
	case INTEL_FAM6_WESTMERE_EX:	/* Westmere-EX Xeon - Eagleton */
		pkg_cstate_limits = nhm_pkg_cstate_limits;
		break;
	case INTEL_FAM6_SANDYBRIDGE:	/* SNB */
	case INTEL_FAM6_SANDYBRIDGE_X:	/* SNB Xeon */
	case INTEL_FAM6_IVYBRIDGE:	/* IVB */
	case INTEL_FAM6_IVYBRIDGE_X:	/* IVB Xeon */
		pkg_cstate_limits = snb_pkg_cstate_limits;
		break;
	case INTEL_FAM6_HASWELL_CORE:	/* HSW */
	case INTEL_FAM6_HASWELL_X:	/* HSX */
	case INTEL_FAM6_HASWELL_ULT:	/* HSW */
	case INTEL_FAM6_HASWELL_GT3E:	/* HSW */
	case INTEL_FAM6_BROADWELL_CORE:	/* BDW */
	case INTEL_FAM6_BROADWELL_GT3E:	/* BDW */
	case INTEL_FAM6_BROADWELL_X:	/* BDX */
	case INTEL_FAM6_BROADWELL_XEON_D:	/* BDX-DE */
	case INTEL_FAM6_SKYLAKE_MOBILE:	/* SKL */
	case INTEL_FAM6_SKYLAKE_DESKTOP:	/* SKL */
	case INTEL_FAM6_KABYLAKE_MOBILE:	/* KBL */
	case INTEL_FAM6_KABYLAKE_DESKTOP:	/* KBL */
		pkg_cstate_limits = hsw_pkg_cstate_limits;
		break;
	case INTEL_FAM6_SKYLAKE_X:	/* SKX */
		pkg_cstate_limits = skx_pkg_cstate_limits;
		break;
	case INTEL_FAM6_ATOM_SILVERMONT1:	/* BYT */
		no_MSR_MISC_PWR_MGMT = 1;
	case INTEL_FAM6_ATOM_SILVERMONT2:	/* AVN */
		pkg_cstate_limits = slv_pkg_cstate_limits;
		break;
	case INTEL_FAM6_ATOM_AIRMONT:	/* AMT */
		pkg_cstate_limits = amt_pkg_cstate_limits;
		no_MSR_MISC_PWR_MGMT = 1;
		break;
	case INTEL_FAM6_XEON_PHI_KNL:	/* PHI */
	case INTEL_FAM6_XEON_PHI_KNM:
		pkg_cstate_limits = phi_pkg_cstate_limits;
		break;
	case INTEL_FAM6_ATOM_GOLDMONT:	/* BXT */
	case INTEL_FAM6_ATOM_DENVERTON:	/* DNV */
		pkg_cstate_limits = bxt_pkg_cstate_limits;
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
 * SLV client has supporet for unique MSRs:
 *
 * MSR_CC6_DEMOTION_POLICY_CONFIG
 * MSR_MC6_DEMOTION_POLICY_CONFIG
 */

int has_slv_msrs(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	switch (model) {
	case INTEL_FAM6_ATOM_SILVERMONT1:
	case INTEL_FAM6_ATOM_MERRIFIELD:
	case INTEL_FAM6_ATOM_MOOREFIELD:
		return 1;
	}
	return 0;
}

int has_nhm_turbo_ratio_limit(unsigned int family, unsigned int model)
{
	if (has_slv_msrs(family, model))
		return 0;

	switch (model) {
	/* Nehalem compatible, but do not include turbo-ratio limit support */
	case INTEL_FAM6_NEHALEM_EX:	/* Nehalem-EX Xeon - Beckton */
	case INTEL_FAM6_WESTMERE_EX:	/* Westmere-EX Xeon - Eagleton */
	case INTEL_FAM6_XEON_PHI_KNL:	/* PHI - Knights Landing (different MSR definition) */
	case INTEL_FAM6_XEON_PHI_KNM:
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
	case INTEL_FAM6_XEON_PHI_KNM:
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
	case INTEL_FAM6_HASWELL_CORE:	/* HSW */
	case INTEL_FAM6_HASWELL_X:	/* HSX */
	case INTEL_FAM6_HASWELL_ULT:	/* HSW */
	case INTEL_FAM6_HASWELL_GT3E:	/* HSW */
	case INTEL_FAM6_BROADWELL_CORE:	/* BDW */
	case INTEL_FAM6_BROADWELL_GT3E:	/* BDW */
	case INTEL_FAM6_BROADWELL_X:	/* BDX */
	case INTEL_FAM6_BROADWELL_XEON_D:	/* BDX-DE */
	case INTEL_FAM6_SKYLAKE_MOBILE:	/* SKL */
	case INTEL_FAM6_SKYLAKE_DESKTOP:	/* SKL */
	case INTEL_FAM6_KABYLAKE_MOBILE:	/* KBL */
	case INTEL_FAM6_KABYLAKE_DESKTOP:	/* KBL */
	case INTEL_FAM6_SKYLAKE_X:	/* SKX */

	case INTEL_FAM6_XEON_PHI_KNL:	/* Knights Landing */
	case INTEL_FAM6_XEON_PHI_KNM:
		return 1;
	default:
		return 0;
	}
}

static void
dump_cstate_pstate_config_info(unsigned int family, unsigned int model)
{
	if (!do_nhm_platform_info)
		return;

	dump_nhm_platform_info();

	if (has_hsw_turbo_ratio_limit(family, model))
		dump_hsw_turbo_ratio_limits();

	if (has_ivt_turbo_ratio_limit(family, model))
		dump_ivt_turbo_ratio_limits();

	if (has_nhm_turbo_ratio_limit(family, model))
		dump_nhm_turbo_ratio_limits();

	if (has_atom_turbo_ratio_limit(family, model))
		dump_atom_turbo_ratio_limits();

	if (has_knl_turbo_ratio_limit(family, model))
		dump_knl_turbo_ratio_limits();

	if (has_config_tdp(family, model))
		dump_config_tdp();

	dump_nhm_cst_cfg();
}


/*
 * print_epb()
 * Decode the ENERGY_PERF_BIAS MSR
 */
int print_epb(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned long long msr;
	char *epb_string;
	int cpu;

	if (!has_epb)
		return 0;

	cpu = t->cpu_id;

	/* EPB is per-package */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE) || !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (get_msr(cpu, MSR_IA32_ENERGY_PERF_BIAS, &msr))
		return 0;

	switch (msr & 0xF) {
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
	fprintf(outf, "cpu%d: MSR_IA32_ENERGY_PERF_BIAS: 0x%08llx (%s)\n", cpu, msr, epb_string);

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

	if (!has_hwp)
		return 0;

	cpu = t->cpu_id;

	/* MSR_HWP_CAPABILITIES is per-package */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE) || !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (get_msr(cpu, MSR_PM_ENABLE, &msr))
		return 0;

	fprintf(outf, "cpu%d: MSR_PM_ENABLE: 0x%08llx (%sHWP)\n",
		cpu, msr, (msr & (1 << 0)) ? "" : "No-");

	/* MSR_PM_ENABLE[1] == 1 if HWP is enabled and MSRs visible */
	if ((msr & (1 << 0)) == 0)
		return 0;

	if (get_msr(cpu, MSR_HWP_CAPABILITIES, &msr))
		return 0;

	fprintf(outf, "cpu%d: MSR_HWP_CAPABILITIES: 0x%08llx "
			"(high 0x%x guar 0x%x eff 0x%x low 0x%x)\n",
			cpu, msr,
			(unsigned int)HWP_HIGHEST_PERF(msr),
			(unsigned int)HWP_GUARANTEED_PERF(msr),
			(unsigned int)HWP_MOSTEFFICIENT_PERF(msr),
			(unsigned int)HWP_LOWEST_PERF(msr));

	if (get_msr(cpu, MSR_HWP_REQUEST, &msr))
		return 0;

	fprintf(outf, "cpu%d: MSR_HWP_REQUEST: 0x%08llx "
			"(min 0x%x max 0x%x des 0x%x epp 0x%x window 0x%x pkg 0x%x)\n",
			cpu, msr,
			(unsigned int)(((msr) >> 0) & 0xff),
			(unsigned int)(((msr) >> 8) & 0xff),
			(unsigned int)(((msr) >> 16) & 0xff),
			(unsigned int)(((msr) >> 24) & 0xff),
			(unsigned int)(((msr) >> 32) & 0xff3),
			(unsigned int)(((msr) >> 42) & 0x1));

	if (has_hwp_pkg) {
		if (get_msr(cpu, MSR_HWP_REQUEST_PKG, &msr))
			return 0;

		fprintf(outf, "cpu%d: MSR_HWP_REQUEST_PKG: 0x%08llx "
			"(min 0x%x max 0x%x des 0x%x epp 0x%x window 0x%x)\n",
			cpu, msr,
			(unsigned int)(((msr) >> 0) & 0xff),
			(unsigned int)(((msr) >> 8) & 0xff),
			(unsigned int)(((msr) >> 16) & 0xff),
			(unsigned int)(((msr) >> 24) & 0xff),
			(unsigned int)(((msr) >> 32) & 0xff3));
	}
	if (has_hwp_notify) {
		if (get_msr(cpu, MSR_HWP_INTERRUPT, &msr))
			return 0;

		fprintf(outf, "cpu%d: MSR_HWP_INTERRUPT: 0x%08llx "
			"(%s_Guaranteed_Perf_Change, %s_Excursion_Min)\n",
			cpu, msr,
			((msr) & 0x1) ? "EN" : "Dis",
			((msr) & 0x2) ? "EN" : "Dis");
	}
	if (get_msr(cpu, MSR_HWP_STATUS, &msr))
		return 0;

	fprintf(outf, "cpu%d: MSR_HWP_STATUS: 0x%08llx "
			"(%sGuaranteed_Perf_Change, %sExcursion_Min)\n",
			cpu, msr,
			((msr) & 0x1) ? "" : "No-",
			((msr) & 0x2) ? "" : "No-");

	return 0;
}

/*
 * print_perf_limit()
 */
int print_perf_limit(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned long long msr;
	int cpu;

	cpu = t->cpu_id;

	/* per-package */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE) || !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "Could not migrate to CPU %d\n", cpu);
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
			(msr & 1 << 1) ? "ThermStatus, " : "",
			(msr & 1 << 0) ? "PROCHOT, " : "");
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
			(msr & 1 << 17) ? "ThermStatus, " : "",
			(msr & 1 << 16) ? "PROCHOT, " : "");

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
			(msr & 1 << 10) ? "PkgPwrL1, " : "",
			(msr & 1 << 11) ? "PkgPwrL2, " : "");
		fprintf(outf, " (Logged: %s%s%s%s%s%s%s%s)\n",
			(msr & 1 << 16) ? "PROCHOT, " : "",
			(msr & 1 << 17) ? "ThermStatus, " : "",
			(msr & 1 << 20) ? "Graphics, " : "",
			(msr & 1 << 22) ? "VR-Therm, " : "",
			(msr & 1 << 24) ? "Amps, " : "",
			(msr & 1 << 25) ? "GFXPwr, " : "",
			(msr & 1 << 26) ? "PkgPwrL1, " : "",
			(msr & 1 << 27) ? "PkgPwrL2, " : "");
	}
	if (do_ring_perf_limit_reasons) {
		get_msr(cpu, MSR_RING_PERF_LIMIT_REASONS, &msr);
		fprintf(outf, "cpu%d: MSR_RING_PERF_LIMIT_REASONS, 0x%08llx", cpu, msr);
		fprintf(outf, " (Active: %s%s%s%s%s%s)",
			(msr & 1 << 0) ? "PROCHOT, " : "",
			(msr & 1 << 1) ? "ThermStatus, " : "",
			(msr & 1 << 6) ? "VR-Therm, " : "",
			(msr & 1 << 8) ? "Amps, " : "",
			(msr & 1 << 10) ? "PkgPwrL1, " : "",
			(msr & 1 << 11) ? "PkgPwrL2, " : "");
		fprintf(outf, " (Logged: %s%s%s%s%s%s)\n",
			(msr & 1 << 16) ? "PROCHOT, " : "",
			(msr & 1 << 17) ? "ThermStatus, " : "",
			(msr & 1 << 22) ? "VR-Therm, " : "",
			(msr & 1 << 24) ? "Amps, " : "",
			(msr & 1 << 26) ? "PkgPwrL1, " : "",
			(msr & 1 << 27) ? "PkgPwrL2, " : "");
	}
	return 0;
}

#define	RAPL_POWER_GRANULARITY	0x7FFF	/* 15 bit power granularity */
#define	RAPL_TIME_GRANULARITY	0x3F /* 6 bit time granularity */

double get_tdp(unsigned int model)
{
	unsigned long long msr;

	if (do_rapl & RAPL_PKG_POWER_INFO)
		if (!get_msr(base_cpu, MSR_PKG_POWER_INFO, &msr))
			return ((msr >> 0) & RAPL_POWER_GRANULARITY) * rapl_power_units;

	switch (model) {
	case INTEL_FAM6_ATOM_SILVERMONT1:
	case INTEL_FAM6_ATOM_SILVERMONT2:
		return 30.0;
	default:
		return 135.0;
	}
}

/*
 * rapl_dram_energy_units_probe()
 * Energy units are either hard-coded, or come from RAPL Energy Unit MSR.
 */
static double
rapl_dram_energy_units_probe(int  model, double rapl_energy_units)
{
	/* only called for genuine_intel, family 6 */

	switch (model) {
	case INTEL_FAM6_HASWELL_X:	/* HSX */
	case INTEL_FAM6_BROADWELL_X:	/* BDX */
	case INTEL_FAM6_BROADWELL_XEON_D:	/* BDX-DE */
	case INTEL_FAM6_XEON_PHI_KNL:	/* KNL */
	case INTEL_FAM6_XEON_PHI_KNM:
		return (rapl_dram_energy_units = 15.3 / 1000000);
	default:
		return (rapl_energy_units);
	}
}


/*
 * rapl_probe()
 *
 * sets do_rapl, rapl_power_units, rapl_energy_units, rapl_time_units
 */
void rapl_probe(unsigned int family, unsigned int model)
{
	unsigned long long msr;
	unsigned int time_unit;
	double tdp;

	if (!genuine_intel)
		return;

	if (family != 6)
		return;

	switch (model) {
	case INTEL_FAM6_SANDYBRIDGE:
	case INTEL_FAM6_IVYBRIDGE:
	case INTEL_FAM6_HASWELL_CORE:	/* HSW */
	case INTEL_FAM6_HASWELL_ULT:	/* HSW */
	case INTEL_FAM6_HASWELL_GT3E:	/* HSW */
	case INTEL_FAM6_BROADWELL_CORE:	/* BDW */
	case INTEL_FAM6_BROADWELL_GT3E:	/* BDW */
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
		do_rapl = RAPL_PKG | RAPL_PKG_POWER_INFO;
		if (rapl_joules)
			BIC_PRESENT(BIC_Pkg_J);
		else
			BIC_PRESENT(BIC_PkgWatt);
		break;
	case INTEL_FAM6_SKYLAKE_MOBILE:	/* SKL */
	case INTEL_FAM6_SKYLAKE_DESKTOP:	/* SKL */
	case INTEL_FAM6_KABYLAKE_MOBILE:	/* KBL */
	case INTEL_FAM6_KABYLAKE_DESKTOP:	/* KBL */
		do_rapl = RAPL_PKG | RAPL_DRAM | RAPL_DRAM_PERF_STATUS | RAPL_PKG_PERF_STATUS | RAPL_PKG_POWER_INFO;
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
	case INTEL_FAM6_HASWELL_X:	/* HSX */
	case INTEL_FAM6_BROADWELL_X:	/* BDX */
	case INTEL_FAM6_BROADWELL_XEON_D:	/* BDX-DE */
	case INTEL_FAM6_SKYLAKE_X:	/* SKX */
	case INTEL_FAM6_XEON_PHI_KNL:	/* KNL */
	case INTEL_FAM6_XEON_PHI_KNM:
		do_rapl = RAPL_PKG | RAPL_DRAM | RAPL_DRAM_POWER_INFO | RAPL_DRAM_PERF_STATUS | RAPL_PKG_PERF_STATUS | RAPL_PKG_POWER_INFO;
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
		do_rapl = RAPL_PKG | RAPL_CORES | RAPL_CORE_POLICY | RAPL_DRAM | RAPL_DRAM_POWER_INFO | RAPL_PKG_PERF_STATUS | RAPL_DRAM_PERF_STATUS | RAPL_PKG_POWER_INFO;
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
	case INTEL_FAM6_ATOM_SILVERMONT1:	/* BYT */
	case INTEL_FAM6_ATOM_SILVERMONT2:	/* AVN */
		do_rapl = RAPL_PKG | RAPL_CORES;
		if (rapl_joules) {
			BIC_PRESENT(BIC_Pkg_J);
			BIC_PRESENT(BIC_Cor_J);
		} else {
			BIC_PRESENT(BIC_PkgWatt);
			BIC_PRESENT(BIC_CorWatt);
		}
		break;
	case INTEL_FAM6_ATOM_DENVERTON:	/* DNV */
		do_rapl = RAPL_PKG | RAPL_DRAM | RAPL_DRAM_POWER_INFO | RAPL_DRAM_PERF_STATUS | RAPL_PKG_PERF_STATUS | RAPL_PKG_POWER_INFO | RAPL_CORES_ENERGY_STATUS;
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
	if (model == INTEL_FAM6_ATOM_SILVERMONT1)
		rapl_energy_units = 1.0 * (1 << (msr >> 8 & 0x1F)) / 1000000;
	else
		rapl_energy_units = 1.0 / (1 << (msr >> 8 & 0x1F));

	rapl_dram_energy_units = rapl_dram_energy_units_probe(model, rapl_energy_units);

	time_unit = msr >> 16 & 0xF;
	if (time_unit == 0)
		time_unit = 0xA;

	rapl_time_units = 1.0 / (1 << (time_unit));

	tdp = get_tdp(model);

	rapl_joule_counter_range = 0xFFFFFFFF * rapl_energy_units / tdp;
	if (debug)
		fprintf(outf, "RAPL: %.0f sec. Joule Counter Range, at %.0f Watts\n", rapl_joule_counter_range, tdp);

	return;
}

void perf_limit_reasons_probe(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return;

	if (family != 6)
		return;

	switch (model) {
	case INTEL_FAM6_HASWELL_CORE:	/* HSW */
	case INTEL_FAM6_HASWELL_ULT:	/* HSW */
	case INTEL_FAM6_HASWELL_GT3E:	/* HSW */
		do_gfx_perf_limit_reasons = 1;
	case INTEL_FAM6_HASWELL_X:	/* HSX */
		do_core_perf_limit_reasons = 1;
		do_ring_perf_limit_reasons = 1;
	default:
		return;
	}
}

int print_thermal(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned long long msr;
	unsigned int dts;
	int cpu;

	if (!(do_dts || do_ptm))
		return 0;

	cpu = t->cpu_id;

	/* DTS is per-core, no need to print for each thread */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		return 0;

	if (cpu_migrate(cpu)) {
		fprintf(outf, "Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (do_ptm && (t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE)) {
		if (get_msr(cpu, MSR_IA32_PACKAGE_THERM_STATUS, &msr))
			return 0;

		dts = (msr >> 16) & 0x7F;
		fprintf(outf, "cpu%d: MSR_IA32_PACKAGE_THERM_STATUS: 0x%08llx (%d C)\n",
			cpu, msr, tcc_activation_temp - dts);

#ifdef	THERM_DEBUG
		if (get_msr(cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT, &msr))
			return 0;

		dts = (msr >> 16) & 0x7F;
		dts2 = (msr >> 8) & 0x7F;
		fprintf(outf, "cpu%d: MSR_IA32_PACKAGE_THERM_INTERRUPT: 0x%08llx (%d C, %d C)\n",
			cpu, msr, tcc_activation_temp - dts, tcc_activation_temp - dts2);
#endif
	}


	if (do_dts) {
		unsigned int resolution;

		if (get_msr(cpu, MSR_IA32_THERM_STATUS, &msr))
			return 0;

		dts = (msr >> 16) & 0x7F;
		resolution = (msr >> 27) & 0xF;
		fprintf(outf, "cpu%d: MSR_IA32_THERM_STATUS: 0x%08llx (%d C +/- %d)\n",
			cpu, msr, tcc_activation_temp - dts, resolution);

#ifdef THERM_DEBUG
		if (get_msr(cpu, MSR_IA32_THERM_INTERRUPT, &msr))
			return 0;

		dts = (msr >> 16) & 0x7F;
		dts2 = (msr >> 8) & 0x7F;
		fprintf(outf, "cpu%d: MSR_IA32_THERM_INTERRUPT: 0x%08llx (%d C, %d C)\n",
			cpu, msr, tcc_activation_temp - dts, tcc_activation_temp - dts2);
#endif
	}

	return 0;
}

void print_power_limit_msr(int cpu, unsigned long long msr, char *label)
{
	fprintf(outf, "cpu%d: %s: %sabled (%f Watts, %f sec, clamp %sabled)\n",
		cpu, label,
		((msr >> 15) & 1) ? "EN" : "DIS",
		((msr >> 0) & 0x7FFF) * rapl_power_units,
		(1.0 + (((msr >> 22) & 0x3)/4.0)) * (1 << ((msr >> 17) & 0x1F)) * rapl_time_units,
		(((msr >> 16) & 1) ? "EN" : "DIS"));

	return;
}

int print_rapl(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned long long msr;
	int cpu;

	if (!do_rapl)
		return 0;

	/* RAPL counters are per package, so print only for 1st thread/package */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE) || !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	cpu = t->cpu_id;
	if (cpu_migrate(cpu)) {
		fprintf(outf, "Could not migrate to CPU %d\n", cpu);
		return -1;
	}

	if (get_msr(cpu, MSR_RAPL_POWER_UNIT, &msr))
		return -1;

	if (debug) {
		fprintf(outf, "cpu%d: MSR_RAPL_POWER_UNIT: 0x%08llx "
			"(%f Watts, %f Joules, %f sec.)\n", cpu, msr,
			rapl_power_units, rapl_energy_units, rapl_time_units);
	}
	if (do_rapl & RAPL_PKG_POWER_INFO) {

		if (get_msr(cpu, MSR_PKG_POWER_INFO, &msr))
                	return -5;


		fprintf(outf, "cpu%d: MSR_PKG_POWER_INFO: 0x%08llx (%.0f W TDP, RAPL %.0f - %.0f W, %f sec.)\n",
			cpu, msr,
			((msr >>  0) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 16) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 32) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 48) & RAPL_TIME_GRANULARITY) * rapl_time_units);

	}
	if (do_rapl & RAPL_PKG) {

		if (get_msr(cpu, MSR_PKG_POWER_LIMIT, &msr))
			return -9;

		fprintf(outf, "cpu%d: MSR_PKG_POWER_LIMIT: 0x%08llx (%slocked)\n",
			cpu, msr, (msr >> 63) & 1 ? "": "UN");

		print_power_limit_msr(cpu, msr, "PKG Limit #1");
		fprintf(outf, "cpu%d: PKG Limit #2: %sabled (%f Watts, %f* sec, clamp %sabled)\n",
			cpu,
			((msr >> 47) & 1) ? "EN" : "DIS",
			((msr >> 32) & 0x7FFF) * rapl_power_units,
			(1.0 + (((msr >> 54) & 0x3)/4.0)) * (1 << ((msr >> 49) & 0x1F)) * rapl_time_units,
			((msr >> 48) & 1) ? "EN" : "DIS");
	}

	if (do_rapl & RAPL_DRAM_POWER_INFO) {
		if (get_msr(cpu, MSR_DRAM_POWER_INFO, &msr))
                	return -6;

		fprintf(outf, "cpu%d: MSR_DRAM_POWER_INFO,: 0x%08llx (%.0f W TDP, RAPL %.0f - %.0f W, %f sec.)\n",
			cpu, msr,
			((msr >>  0) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 16) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 32) & RAPL_POWER_GRANULARITY) * rapl_power_units,
			((msr >> 48) & RAPL_TIME_GRANULARITY) * rapl_time_units);
	}
	if (do_rapl & RAPL_DRAM) {
		if (get_msr(cpu, MSR_DRAM_POWER_LIMIT, &msr))
			return -9;
		fprintf(outf, "cpu%d: MSR_DRAM_POWER_LIMIT: 0x%08llx (%slocked)\n",
				cpu, msr, (msr >> 31) & 1 ? "": "UN");

		print_power_limit_msr(cpu, msr, "DRAM Limit");
	}
	if (do_rapl & RAPL_CORE_POLICY) {
		if (debug) {
			if (get_msr(cpu, MSR_PP0_POLICY, &msr))
				return -7;

			fprintf(outf, "cpu%d: MSR_PP0_POLICY: %lld\n", cpu, msr & 0xF);
		}
	}
	if (do_rapl & RAPL_CORES_POWER_LIMIT) {
		if (debug) {
			if (get_msr(cpu, MSR_PP0_POWER_LIMIT, &msr))
				return -9;
			fprintf(outf, "cpu%d: MSR_PP0_POWER_LIMIT: 0x%08llx (%slocked)\n",
					cpu, msr, (msr >> 31) & 1 ? "": "UN");
			print_power_limit_msr(cpu, msr, "Cores Limit");
		}
	}
	if (do_rapl & RAPL_GFX) {
		if (debug) {
			if (get_msr(cpu, MSR_PP1_POLICY, &msr))
				return -8;

			fprintf(outf, "cpu%d: MSR_PP1_POLICY: %lld\n", cpu, msr & 0xF);

			if (get_msr(cpu, MSR_PP1_POWER_LIMIT, &msr))
				return -9;
			fprintf(outf, "cpu%d: MSR_PP1_POWER_LIMIT: 0x%08llx (%slocked)\n",
					cpu, msr, (msr >> 31) & 1 ? "": "UN");
			print_power_limit_msr(cpu, msr, "GFX Limit");
		}
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

	switch (model) {
	case INTEL_FAM6_SANDYBRIDGE:
	case INTEL_FAM6_SANDYBRIDGE_X:
	case INTEL_FAM6_IVYBRIDGE:	/* IVB */
	case INTEL_FAM6_IVYBRIDGE_X:	/* IVB Xeon */
	case INTEL_FAM6_HASWELL_CORE:	/* HSW */
	case INTEL_FAM6_HASWELL_X:	/* HSW */
	case INTEL_FAM6_HASWELL_ULT:	/* HSW */
	case INTEL_FAM6_HASWELL_GT3E:	/* HSW */
	case INTEL_FAM6_BROADWELL_CORE:	/* BDW */
	case INTEL_FAM6_BROADWELL_GT3E:	/* BDW */
	case INTEL_FAM6_BROADWELL_X:	/* BDX */
	case INTEL_FAM6_BROADWELL_XEON_D:	/* BDX-DE */
	case INTEL_FAM6_SKYLAKE_MOBILE:	/* SKL */
	case INTEL_FAM6_SKYLAKE_DESKTOP:	/* SKL */
	case INTEL_FAM6_KABYLAKE_MOBILE:	/* KBL */
	case INTEL_FAM6_KABYLAKE_DESKTOP:	/* KBL */
	case INTEL_FAM6_SKYLAKE_X:	/* SKX */
	case INTEL_FAM6_ATOM_GOLDMONT:	/* BXT */
	case INTEL_FAM6_ATOM_DENVERTON:	/* DNV */
		return 1;
	}
	return 0;
}

/*
 * HSW adds support for additional MSRs:
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
int has_hsw_msrs(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	switch (model) {
	case INTEL_FAM6_HASWELL_ULT:	/* HSW */
	case INTEL_FAM6_BROADWELL_CORE:	/* BDW */
	case INTEL_FAM6_SKYLAKE_MOBILE:	/* SKL */
	case INTEL_FAM6_SKYLAKE_DESKTOP:	/* SKL */
	case INTEL_FAM6_KABYLAKE_MOBILE:	/* KBL */
	case INTEL_FAM6_KABYLAKE_DESKTOP:	/* KBL */
	case INTEL_FAM6_ATOM_GOLDMONT:	/* BXT */
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

	switch (model) {
	case INTEL_FAM6_SKYLAKE_MOBILE:	/* SKL */
	case INTEL_FAM6_SKYLAKE_DESKTOP:	/* SKL */
	case INTEL_FAM6_KABYLAKE_MOBILE:	/* KBL */
	case INTEL_FAM6_KABYLAKE_DESKTOP:	/* KBL */
		return 1;
	}
	return 0;
}

int is_slm(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;
	switch (model) {
	case INTEL_FAM6_ATOM_SILVERMONT1:	/* BYT */
	case INTEL_FAM6_ATOM_SILVERMONT2:	/* AVN */
		return 1;
	}
	return 0;
}

int is_knl(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;
	switch (model) {
	case INTEL_FAM6_XEON_PHI_KNL:	/* KNL */
	case INTEL_FAM6_XEON_PHI_KNM:
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
double slm_freq_table[SLM_BCLK_FREQS] = { 83.3, 100.0, 133.3, 116.7, 80.0};

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

	if (debug)
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
	unsigned int target_c_local;
	int cpu;

	/* tcc_activation_temp is used only for dts or ptm */
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

	if (tcc_activation_temp_override != 0) {
		tcc_activation_temp = tcc_activation_temp_override;
		fprintf(outf, "cpu%d: Using cmdline TCC Target (%d C)\n",
			cpu, tcc_activation_temp);
		return 0;
	}

	/* Temperature Target MSR is Nehalem and newer only */
	if (!do_nhm_platform_info)
		goto guess;

	if (get_msr(base_cpu, MSR_IA32_TEMPERATURE_TARGET, &msr))
		goto guess;

	target_c_local = (msr >> 16) & 0xFF;

	if (debug)
		fprintf(outf, "cpu%d: MSR_IA32_TEMPERATURE_TARGET: 0x%08llx (%d C)\n",
			cpu, msr, target_c_local);

	if (!target_c_local)
		goto guess;

	tcc_activation_temp = target_c_local;

	return 0;

guess:
	tcc_activation_temp = TJMAX_DEFAULT;
	fprintf(outf, "cpu%d: Guessing tjMax %d C, Please use -T to specify\n",
		cpu, tcc_activation_temp);

	return 0;
}

void decode_feature_control_msr(void)
{
	unsigned long long msr;

	if (!get_msr(base_cpu, MSR_IA32_FEATURE_CONTROL, &msr))
		fprintf(outf, "cpu%d: MSR_IA32_FEATURE_CONTROL: 0x%08llx (%sLocked %s)\n",
			base_cpu, msr,
			msr & FEATURE_CONTROL_LOCKED ? "" : "UN-",
			msr & (1 << 18) ? "SGX" : "");
}

void decode_misc_enable_msr(void)
{
	unsigned long long msr;

	if (!get_msr(base_cpu, MSR_IA32_MISC_ENABLE, &msr))
		fprintf(outf, "cpu%d: MSR_IA32_MISC_ENABLE: 0x%08llx (%sTCC %sEIST %sMWAIT %sPREFETCH %sTURBO)\n",
			base_cpu, msr,
			msr & MSR_IA32_MISC_ENABLE_TM1 ? "" : "No-",
			msr & MSR_IA32_MISC_ENABLE_ENHANCED_SPEEDSTEP ? "" : "No-",
			msr & MSR_IA32_MISC_ENABLE_MWAIT ? "No-" : "",
			msr & MSR_IA32_MISC_ENABLE_PREFETCH_DISABLE ? "No-" : "",
			msr & MSR_IA32_MISC_ENABLE_TURBO_DISABLE ? "No-" : "");
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
			msr & (1 << 0) ? "DIS" : "EN",
			msr & (1 << 1) ? "EN" : "DIS",
			msr & (1 << 8) ? "EN" : "DIS");
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

void process_cpuid()
{
	unsigned int eax, ebx, ecx, edx, max_level, max_extended_level;
	unsigned int fms, family, model, stepping;
	unsigned int has_turbo;

	eax = ebx = ecx = edx = 0;

	__cpuid(0, max_level, ebx, ecx, edx);

	if (ebx == 0x756e6547 && edx == 0x49656e69 && ecx == 0x6c65746e)
		genuine_intel = 1;

	if (debug)
		fprintf(outf, "CPUID(0): %.4s%.4s%.4s ",
			(char *)&ebx, (char *)&edx, (char *)&ecx);

	__cpuid(1, fms, ebx, ecx, edx);
	family = (fms >> 8) & 0xf;
	model = (fms >> 4) & 0xf;
	stepping = fms & 0xf;
	if (family == 6 || family == 0xf)
		model += ((fms >> 16) & 0xf) << 4;

	if (debug) {
		fprintf(outf, "%d CPUID levels; family:model:stepping 0x%x:%x:%x (%d:%d:%d)\n",
			max_level, family, model, stepping, family, model, stepping);
		fprintf(outf, "CPUID(1): %s %s %s %s %s %s %s %s %s\n",
			ecx & (1 << 0) ? "SSE3" : "-",
			ecx & (1 << 3) ? "MONITOR" : "-",
			ecx & (1 << 6) ? "SMX" : "-",
			ecx & (1 << 7) ? "EIST" : "-",
			ecx & (1 << 8) ? "TM2" : "-",
			edx & (1 << 4) ? "TSC" : "-",
			edx & (1 << 5) ? "MSR" : "-",
			edx & (1 << 22) ? "ACPI-TM" : "-",
			edx & (1 << 29) ? "TM" : "-");
	}

	if (!(edx & (1 << 5)))
		errx(1, "CPUID: no MSR");

	/*
	 * check max extended function levels of CPUID.
	 * This is needed to check for invariant TSC.
	 * This check is valid for both Intel and AMD.
	 */
	ebx = ecx = edx = 0;
	__cpuid(0x80000000, max_extended_level, ebx, ecx, edx);

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

	if (debug)
		fprintf(outf, "CPUID(6): %sAPERF, %sTURBO, %sDTS, %sPTM, %sHWP, "
			"%sHWPnotify, %sHWPwindow, %sHWPepp, %sHWPpkg, %sEPB\n",
			has_aperf ? "" : "No-",
			has_turbo ? "" : "No-",
			do_dts ? "" : "No-",
			do_ptm ? "" : "No-",
			has_hwp ? "" : "No-",
			has_hwp_notify ? "" : "No-",
			has_hwp_activity_window ? "" : "No-",
			has_hwp_epp ? "" : "No-",
			has_hwp_pkg ? "" : "No-",
			has_epb ? "" : "No-");

	if (debug)
		decode_misc_enable_msr();

	if (max_level >= 0x7 && debug) {
		int has_sgx;

		ecx = 0;

		__cpuid_count(0x7, 0, eax, ebx, ecx, edx);

		has_sgx = ebx & (1 << 2);
		fprintf(outf, "CPUID(7): %sSGX\n", has_sgx ? "" : "No-");

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

			if (debug && (ebx != 0))
				fprintf(outf, "CPUID(0x15): eax_crystal: %d ebx_tsc: %d ecx_crystal_hz: %d\n",
					eax_crystal, ebx_tsc, crystal_hz);

			if (crystal_hz == 0)
				switch(model) {
				case INTEL_FAM6_SKYLAKE_MOBILE:	/* SKL */
				case INTEL_FAM6_SKYLAKE_DESKTOP:	/* SKL */
				case INTEL_FAM6_KABYLAKE_MOBILE:	/* KBL */
				case INTEL_FAM6_KABYLAKE_DESKTOP:	/* KBL */
					crystal_hz = 24000000;	/* 24.0 MHz */
					break;
				case INTEL_FAM6_SKYLAKE_X:	/* SKX */
				case INTEL_FAM6_ATOM_DENVERTON:	/* DNV */
					crystal_hz = 25000000;	/* 25.0 MHz */
					break;
				case INTEL_FAM6_ATOM_GOLDMONT:	/* BXT */
					crystal_hz = 19200000;	/* 19.2 MHz */
					break;
				default:
					crystal_hz = 0;
			}

			if (crystal_hz) {
				tsc_hz =  (unsigned long long) crystal_hz * ebx_tsc / eax_crystal;
				if (debug)
					fprintf(outf, "TSC: %lld MHz (%d Hz * %d / %d / 1000000)\n",
						tsc_hz / 1000000, crystal_hz, ebx_tsc,  eax_crystal);
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
		if (debug)
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
	do_pc2 = do_snb_cstates && (pkg_cstate_limit >= PCL__2);
	do_pc3 = (pkg_cstate_limit >= PCL__3);
	do_pc6 = (pkg_cstate_limit >= PCL__6);
	do_pc7 = do_snb_cstates && (pkg_cstate_limit >= PCL__7);
	if (has_slv_msrs(family, model)) {
		do_pc2 = do_pc3 = do_pc7 = 0;
		do_pc6 = 1;
		BIC_PRESENT(BIC_Mod_c6);
		use_c1_residency_msr = 1;
	}
	do_c8_c9_c10 = has_hsw_msrs(family, model);
	do_irtl_hsw = has_hsw_msrs(family, model);
	do_skl_residency = has_skl_msrs(family, model);
	do_slm_cstates = is_slm(family, model);
	do_knl_cstates  = is_knl(family, model);

	if (debug)
		decode_misc_pwr_mgmt_msr();

	if (debug && has_slv_msrs(family, model))
		decode_c6_demotion_policy_msr();

	rapl_probe(family, model);
	perf_limit_reasons_probe(family, model);

	if (debug)
		dump_cstate_pstate_config_info(family, model);

	if (has_skl_msrs(family, model))
		calculate_tsc_tweak();

	if (!access("/sys/class/drm/card0/power/rc6_residency_ms", R_OK))
		BIC_PRESENT(BIC_GFX_rc6);

	if (!access("/sys/class/graphics/fb0/device/drm/card0/gt_cur_freq_mhz", R_OK))
		BIC_PRESENT(BIC_GFXMHz);

	return;
}

void help()
{
	fprintf(outf,
	"Usage: turbostat [OPTIONS][(--interval seconds) | COMMAND ...]\n"
	"\n"
	"Turbostat forks the specified COMMAND and prints statistics\n"
	"when COMMAND completes.\n"
	"If no COMMAND is specified, turbostat wakes every 5-seconds\n"
	"to print statistics, until interrupted.\n"
	"--add		add a counter\n"
	"		eg. --add msr0x10,u64,cpu,delta,MY_TSC\n"
	"--debug	run in \"debug\" mode\n"
	"--interval sec	Override default 5-second measurement interval\n"
	"--help		print this help message\n"
	"--out file	create or truncate \"file\" for all output\n"
	"--version	print version information\n"
	"\n"
	"For more help, run \"man turbostat\"\n");
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

int open_dev_cpu_msr(int dummy1)
{
	return 0;
}

void topology_probe()
{
	int i;
	int max_core_id = 0;
	int max_package_id = 0;
	int max_siblings = 0;
	struct cpu_topology {
		int core_id;
		int physical_package_id;
	} *cpus;

	/* Initialize num_cpus, max_cpu_num */
	topo.num_cpus = 0;
	topo.max_cpu_num = 0;
	for_all_proc_cpus(count_cpus);
	if (!summary_only && topo.num_cpus > 1)
		BIC_PRESENT(BIC_CPU);

	if (debug > 1)
		fprintf(outf, "num_cpus %d max_cpu_num %d\n", topo.num_cpus, topo.max_cpu_num);

	cpus = calloc(1, (topo.max_cpu_num  + 1) * sizeof(struct cpu_topology));
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
	 * Allocate and initialize cpu_affinity_set
	 */
	cpu_affinity_set = CPU_ALLOC((topo.max_cpu_num + 1));
	if (cpu_affinity_set == NULL)
		err(3, "CPU_ALLOC");
	cpu_affinity_setsize = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(cpu_affinity_setsize, cpu_affinity_set);


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
		cpus[i].core_id = get_core_id(i);
		if (cpus[i].core_id > max_core_id)
			max_core_id = cpus[i].core_id;

		cpus[i].physical_package_id = get_physical_package_id(i);
		if (cpus[i].physical_package_id > max_package_id)
			max_package_id = cpus[i].physical_package_id;

		siblings = get_num_ht_siblings(i);
		if (siblings > max_siblings)
			max_siblings = siblings;
		if (debug > 1)
			fprintf(outf, "cpu %d pkg %d core %d\n",
				i, cpus[i].physical_package_id, cpus[i].core_id);
	}
	topo.num_cores_per_pkg = max_core_id + 1;
	if (debug > 1)
		fprintf(outf, "max_core_id %d, sizing for %d cores per package\n",
			max_core_id, topo.num_cores_per_pkg);
	if (debug && !summary_only && topo.num_cores_per_pkg > 1)
		BIC_PRESENT(BIC_Core);

	topo.num_packages = max_package_id + 1;
	if (debug > 1)
		fprintf(outf, "max_package_id %d, sizing for %d packages\n",
			max_package_id, topo.num_packages);
	if (debug && !summary_only && topo.num_packages > 1)
		BIC_PRESENT(BIC_Package);

	topo.num_threads_per_core = max_siblings;
	if (debug > 1)
		fprintf(outf, "max_siblings %d\n", max_siblings);

	free(cpus);
}

void
allocate_counters(struct thread_data **t, struct core_data **c, struct pkg_data **p)
{
	int i;

	*t = calloc(topo.num_threads_per_core * topo.num_cores_per_pkg *
		topo.num_packages, sizeof(struct thread_data));
	if (*t == NULL)
		goto error;

	for (i = 0; i < topo.num_threads_per_core *
		topo.num_cores_per_pkg * topo.num_packages; i++)
		(*t)[i].cpu_id = -1;

	*c = calloc(topo.num_cores_per_pkg * topo.num_packages,
		sizeof(struct core_data));
	if (*c == NULL)
		goto error;

	for (i = 0; i < topo.num_cores_per_pkg * topo.num_packages; i++)
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
 * set cpu_id, core_num, pkg_num
 * set FIRST_THREAD_IN_CORE and FIRST_CORE_IN_PACKAGE
 *
 * increment topo.num_cores when 1st core in pkg seen
 */
void init_counter(struct thread_data *thread_base, struct core_data *core_base,
	struct pkg_data *pkg_base, int thread_num, int core_num,
	int pkg_num, int cpu_id)
{
	struct thread_data *t;
	struct core_data *c;
	struct pkg_data *p;

	t = GET_THREAD(thread_base, thread_num, core_num, pkg_num);
	c = GET_CORE(core_base, core_num, pkg_num);
	p = GET_PKG(pkg_base, pkg_num);

	t->cpu_id = cpu_id;
	if (thread_num == 0) {
		t->flags |= CPU_IS_FIRST_THREAD_IN_CORE;
		if (cpu_is_first_core_in_package(cpu_id))
			t->flags |= CPU_IS_FIRST_CORE_IN_PACKAGE;
	}

	c->core_id = core_num;
	p->package_id = pkg_num;
}


int initialize_counters(int cpu_id)
{
	int my_thread_id, my_core_id, my_package_id;

	my_package_id = get_physical_package_id(cpu_id);
	my_core_id = get_core_id(cpu_id);
	my_thread_id = get_cpu_position_in_core(cpu_id);
	if (!my_thread_id)
		topo.num_cores++;

	init_counter(EVEN_COUNTERS, my_thread_id, my_core_id, my_package_id, cpu_id);
	init_counter(ODD_COUNTERS, my_thread_id, my_core_id, my_package_id, cpu_id);
	return 0;
}

void allocate_output_buffer()
{
	output_buffer = calloc(1, (1 + topo.num_cpus) * 1024);
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


	if (debug)
		for_all_cpus(print_hwp, ODD_COUNTERS);

	if (debug)
		for_all_cpus(print_epb, ODD_COUNTERS);

	if (debug)
		for_all_cpus(print_perf_limit, ODD_COUNTERS);

	if (debug)
		for_all_cpus(print_rapl, ODD_COUNTERS);

	for_all_cpus(set_temperature_target, ODD_COUNTERS);

	if (debug)
		for_all_cpus(print_thermal, ODD_COUNTERS);

	if (debug && do_irtl_snb)
		print_irtl();
}

int fork_it(char **argv)
{
	pid_t child_pid;
	int status;

	status = for_all_cpus(get_counters, EVEN_COUNTERS);
	if (status)
		exit(status);
	/* clear affinity side-effect of get_counters() */
	sched_setaffinity(0, cpu_present_setsize, cpu_present_set);
	gettimeofday(&tv_even, (struct timezone *)NULL);

	child_pid = fork();
	if (!child_pid) {
		/* child */
		execvp(argv[0], argv);
	} else {

		/* parent */
		if (child_pid == -1)
			err(1, "fork");

		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
		if (waitpid(child_pid, &status, 0) == -1)
			err(status, "waitpid");
	}
	/*
	 * n.b. fork_it() does not check for errors from for_all_cpus()
	 * because re-starting is problematic when forking
	 */
	for_all_cpus(get_counters, ODD_COUNTERS);
	gettimeofday(&tv_odd, (struct timezone *)NULL);
	timersub(&tv_odd, &tv_even, &tv_delta);
	if (for_all_cpus_2(delta_cpu, ODD_COUNTERS, EVEN_COUNTERS))
		fprintf(outf, "%s: Counter reset detected\n", progname);
	else {
		compute_average(EVEN_COUNTERS);
		format_all_counters(EVEN_COUNTERS);
	}

	fprintf(outf, "%.6f sec\n", tv_delta.tv_sec + tv_delta.tv_usec/1000000.0);

	flush_output_stderr();

	return status;
}

int get_and_dump_counters(void)
{
	int status;

	status = for_all_cpus(get_counters, ODD_COUNTERS);
	if (status)
		return status;

	status = for_all_cpus(dump_counters, ODD_COUNTERS);
	if (status)
		return status;

	flush_output_stdout();

	return status;
}

void print_version() {
	fprintf(outf, "turbostat version 4.17 10 Jan 2017"
		" - Len Brown <lenb@kernel.org>\n");
}

int add_counter(unsigned int msr_num, char *name, unsigned int width,
	enum counter_scope scope, enum counter_type type,
	enum counter_format format)
{
	struct msr_counter *msrp;

	msrp = calloc(1, sizeof(struct msr_counter));
	if (msrp == NULL) {
		perror("calloc");
		exit(1);
	}

	msrp->msr_num = msr_num;
	strncpy(msrp->name, name, NAME_BYTES);
	msrp->width = width;
	msrp->type = type;
	msrp->format = format;

	switch (scope) {

	case SCOPE_CPU:
		msrp->next = sys.tp;
		sys.tp = msrp;
		sys.added_thread_counters++;
		if (sys.added_thread_counters > MAX_ADDED_COUNTERS) {
			fprintf(stderr, "exceeded max %d added thread counters\n",
				MAX_ADDED_COUNTERS);
			exit(-1);
		}
		break;

	case SCOPE_CORE:
		msrp->next = sys.cp;
		sys.cp = msrp;
		sys.added_core_counters++;
		if (sys.added_core_counters > MAX_ADDED_COUNTERS) {
			fprintf(stderr, "exceeded max %d added core counters\n",
				MAX_ADDED_COUNTERS);
			exit(-1);
		}
		break;

	case SCOPE_PACKAGE:
		msrp->next = sys.pp;
		sys.pp = msrp;
		sys.added_package_counters++;
		if (sys.added_package_counters > MAX_ADDED_COUNTERS) {
			fprintf(stderr, "exceeded max %d added package counters\n",
				MAX_ADDED_COUNTERS);
			exit(-1);
		}
		break;
	}

	return 0;
}

void parse_add_command(char *add_command)
{
	int msr_num = 0;
	char name_buffer[NAME_BYTES];
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
		if (add_command)
			add_command++;

	}
	if (msr_num == 0) {
		fprintf(stderr, "--add: (msrDDD | msr0xXXX) required\n");
		fail++;
	}

	/* generate default column header */
	if (*name_buffer == '\0') {
		if (format == FORMAT_RAW) {
			if (width == 32)
				sprintf(name_buffer, "msr%d", msr_num);
			else
				sprintf(name_buffer, "MSR%d", msr_num);
		} else if (format == FORMAT_DELTA) {
			if (width == 32)
				sprintf(name_buffer, "cnt%d", msr_num);
			else
				sprintf(name_buffer, "CNT%d", msr_num);
		} else if (format == FORMAT_PERCENT) {
			if (width == 32)
				sprintf(name_buffer, "msr%d%%", msr_num);
			else
				sprintf(name_buffer, "MSR%d%%", msr_num);
		}
	}

	if (add_counter(msr_num, name_buffer, width, scope, type, format))
		fail++;

	if (fail) {
		help();
		exit(1);
	}
}
/*
 * HIDE_LIST - hide this list of counters, show the rest [default]
 * SHOW_LIST - show this list of counters, hide the rest
 */
enum show_hide_mode { SHOW_LIST, HIDE_LIST } global_show_hide_mode = HIDE_LIST;

int shown;
/*
 * parse_show_hide() - process cmdline to set default counter action
 */
void parse_show_hide(char *optarg, enum show_hide_mode new_mode)
{
	/*
	 * --show: show only those specified
	 *  The 1st invocation will clear and replace the enabled mask
	 *  subsequent invocations can add to it.
	 */
	if (new_mode == SHOW_LIST) {
		if (shown == 0)
			bic_enabled = bic_lookup(optarg);
		else
			bic_enabled |= bic_lookup(optarg);
		shown = 1;

		return;
	}

	/*
	 * --hide: do not show those specified
	 *  multiple invocations simply clear more bits in enabled mask
	 */
	bic_enabled &= ~bic_lookup(optarg);
}

void cmdline(int argc, char **argv)
{
	int opt;
	int option_index = 0;
	static struct option long_options[] = {
		{"add",		required_argument,	0, 'a'},
		{"Dump",	no_argument,		0, 'D'},
		{"debug",	no_argument,		0, 'd'},
		{"interval",	required_argument,	0, 'i'},
		{"help",	no_argument,		0, 'h'},
		{"hide",	required_argument,	0, 'H'},	// meh, -h taken by --help
		{"Joules",	no_argument,		0, 'J'},
		{"out",		required_argument,	0, 'o'},
		{"Package",	no_argument,		0, 'p'},
		{"processor",	no_argument,		0, 'p'},
		{"show",	required_argument,	0, 's'},
		{"Summary",	no_argument,		0, 'S'},
		{"TCC",		required_argument,	0, 'T'},
		{"version",	no_argument,		0, 'v' },
		{0,		0,			0,  0 }
	};

	progname = argv[0];

	while ((opt = getopt_long_only(argc, argv, "+C:c:Ddhi:JM:m:o:PpST:v",
				long_options, &option_index)) != -1) {
		switch (opt) {
		case 'a':
			parse_add_command(optarg);
			break;
		case 'D':
			dump_only++;
			break;
		case 'd':
			debug++;
			break;
		case 'H':
			parse_show_hide(optarg, HIDE_LIST);
			break;
		case 'h':
		default:
			help();
			exit(1);
		case 'i':
			{
				double interval = strtod(optarg, NULL);

				if (interval < 0.001) {
					fprintf(outf, "interval %f seconds is too small\n",
						interval);
					exit(2);
				}

				interval_ts.tv_sec = interval;
				interval_ts.tv_nsec = (interval - interval_ts.tv_sec) * 1000000000;
			}
			break;
		case 'J':
			rapl_joules++;
			break;
		case 'o':
			outf = fopen_or_die(optarg, "w");
			break;
		case 'P':
			show_pkg_only++;
			break;
		case 'p':
			show_core_only++;
			break;
		case 's':
			parse_show_hide(optarg, SHOW_LIST);
			break;
		case 'S':
			summary_only++;
			break;
		case 'T':
			tcc_activation_temp_override = atoi(optarg);
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

	if (debug)
		print_version();

	turbostat_init();

	/* dump counters and exit */
	if (dump_only)
		return get_and_dump_counters();

	/*
	 * if any params left, it must be a command to fork
	 */
	if (argc - optind)
		return fork_it(argv + optind);
	else
		turbostat_loop();

	return 0;
}
