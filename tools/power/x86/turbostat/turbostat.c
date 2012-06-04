/*
 * turbostat -- show CPU frequency and C-state residency
 * on modern Intel turbo-capable processors.
 *
 * Copyright (c) 2012 Intel Corporation.
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
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <sched.h>

#define MSR_TSC	0x10
#define MSR_NEHALEM_PLATFORM_INFO	0xCE
#define MSR_NEHALEM_TURBO_RATIO_LIMIT	0x1AD
#define MSR_APERF	0xE8
#define MSR_MPERF	0xE7
#define MSR_PKG_C2_RESIDENCY	0x60D	/* SNB only */
#define MSR_PKG_C3_RESIDENCY	0x3F8
#define MSR_PKG_C6_RESIDENCY	0x3F9
#define MSR_PKG_C7_RESIDENCY	0x3FA	/* SNB only */
#define MSR_CORE_C3_RESIDENCY	0x3FC
#define MSR_CORE_C6_RESIDENCY	0x3FD
#define MSR_CORE_C7_RESIDENCY	0x3FE	/* SNB only */

char *proc_stat = "/proc/stat";
unsigned int interval_sec = 5;	/* set with -i interval_sec */
unsigned int verbose;		/* set with -v */
unsigned int summary_only;	/* set with -s */
unsigned int skip_c0;
unsigned int skip_c1;
unsigned int do_nhm_cstates;
unsigned int do_snb_cstates;
unsigned int has_aperf;
unsigned int units = 1000000000;	/* Ghz etc */
unsigned int genuine_intel;
unsigned int has_invariant_tsc;
unsigned int do_nehalem_platform_info;
unsigned int do_nehalem_turbo_ratio_limit;
unsigned int extra_msr_offset;
double bclk;
unsigned int show_pkg;
unsigned int show_core;
unsigned int show_cpu;
unsigned int show_pkg_only;
unsigned int show_core_only;
char *output_buffer, *outp;

int aperf_mperf_unstable;
int backwards_count;
char *progname;

cpu_set_t *cpu_present_set, *cpu_affinity_set;
size_t cpu_present_setsize, cpu_affinity_setsize;

struct thread_data {
	unsigned long long tsc;
	unsigned long long aperf;
	unsigned long long mperf;
	unsigned long long c1;	/* derived */
	unsigned long long extra_msr;
	unsigned int cpu_id;
	unsigned int flags;
#define CPU_IS_FIRST_THREAD_IN_CORE	0x2
#define CPU_IS_FIRST_CORE_IN_PACKAGE	0x4
} *thread_even, *thread_odd;

struct core_data {
	unsigned long long c3;
	unsigned long long c6;
	unsigned long long c7;
	unsigned int core_id;
} *core_even, *core_odd;

struct pkg_data {
	unsigned long long pc2;
	unsigned long long pc3;
	unsigned long long pc6;
	unsigned long long pc7;
	unsigned int package_id;
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

struct system_summary {
	struct thread_data threads;
	struct core_data cores;
	struct pkg_data packages;
} sum, average;


struct topo_params {
	int num_packages;
	int num_cpus;
	int num_cores;
	int max_cpu_num;
	int num_cores_per_pkg;
	int num_threads_per_core;
} topo;

struct timeval tv_even, tv_odd, tv_delta;

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

int get_msr(int cpu, off_t offset, unsigned long long *msr)
{
	ssize_t retval;
	char pathname[32];
	int fd;

	sprintf(pathname, "/dev/cpu/%d/msr", cpu);
	fd = open(pathname, O_RDONLY);
	if (fd < 0)
		return -1;

	retval = pread(fd, msr, sizeof *msr, offset);
	close(fd);

	if (retval != sizeof *msr)
		return -1;

	return 0;
}

void print_header(void)
{
	if (show_pkg)
		outp += sprintf(outp, "pk");
	if (show_pkg)
		outp += sprintf(outp, " ");
	if (show_core)
		outp += sprintf(outp, "cor");
	if (show_cpu)
		outp += sprintf(outp, " CPU");
	if (show_pkg || show_core || show_cpu)
		outp += sprintf(outp, " ");
	if (do_nhm_cstates)
		outp += sprintf(outp, "   %%c0");
	if (has_aperf)
		outp += sprintf(outp, "  GHz");
	outp += sprintf(outp, "  TSC");
	if (do_nhm_cstates)
		outp += sprintf(outp, "    %%c1");
	if (do_nhm_cstates)
		outp += sprintf(outp, "    %%c3");
	if (do_nhm_cstates)
		outp += sprintf(outp, "    %%c6");
	if (do_snb_cstates)
		outp += sprintf(outp, "    %%c7");
	if (do_snb_cstates)
		outp += sprintf(outp, "   %%pc2");
	if (do_nhm_cstates)
		outp += sprintf(outp, "   %%pc3");
	if (do_nhm_cstates)
		outp += sprintf(outp, "   %%pc6");
	if (do_snb_cstates)
		outp += sprintf(outp, "   %%pc7");
	if (extra_msr_offset)
		outp += sprintf(outp, "        MSR 0x%x ", extra_msr_offset);

	outp += sprintf(outp, "\n");
}

int dump_counters(struct thread_data *t, struct core_data *c,
	struct pkg_data *p)
{
	fprintf(stderr, "t %p, c %p, p %p\n", t, c, p);

	if (t) {
		fprintf(stderr, "CPU: %d flags 0x%x\n", t->cpu_id, t->flags);
		fprintf(stderr, "TSC: %016llX\n", t->tsc);
		fprintf(stderr, "aperf: %016llX\n", t->aperf);
		fprintf(stderr, "mperf: %016llX\n", t->mperf);
		fprintf(stderr, "c1: %016llX\n", t->c1);
		fprintf(stderr, "msr0x%x: %016llX\n",
			extra_msr_offset, t->extra_msr);
	}

	if (c) {
		fprintf(stderr, "core: %d\n", c->core_id);
		fprintf(stderr, "c3: %016llX\n", c->c3);
		fprintf(stderr, "c6: %016llX\n", c->c6);
		fprintf(stderr, "c7: %016llX\n", c->c7);
	}

	if (p) {
		fprintf(stderr, "package: %d\n", p->package_id);
		fprintf(stderr, "pc2: %016llX\n", p->pc2);
		fprintf(stderr, "pc3: %016llX\n", p->pc3);
		fprintf(stderr, "pc6: %016llX\n", p->pc6);
		fprintf(stderr, "pc7: %016llX\n", p->pc7);
	}
	return 0;
}

/*
 * column formatting convention & formats
 * package: "pk" 2 columns %2d
 * core: "cor" 3 columns %3d
 * CPU: "CPU" 3 columns %3d
 * GHz: "GHz" 3 columns %3.2
 * TSC: "TSC" 3 columns %3.2
 * percentage " %pc3" %6.2
 */
int format_counters(struct thread_data *t, struct core_data *c,
	struct pkg_data *p)
{
	double interval_float;

	 /* if showing only 1st thread in core and this isn't one, bail out */
	if (show_core_only && !(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		return 0;

	 /* if showing only 1st thread in pkg and this isn't one, bail out */
	if (show_pkg_only && !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	interval_float = tv_delta.tv_sec + tv_delta.tv_usec/1000000.0;

	/* topo columns, print blanks on 1st (average) line */
	if (t == &average.threads) {
		if (show_pkg)
			outp += sprintf(outp, "  ");
		if (show_pkg && show_core)
			outp += sprintf(outp, " ");
		if (show_core)
			outp += sprintf(outp, "   ");
		if (show_cpu)
			outp += sprintf(outp, " " "   ");
	} else {
		if (show_pkg) {
			if (p)
				outp += sprintf(outp, "%2d", p->package_id);
			else
				outp += sprintf(outp, "  ");
		}
		if (show_pkg && show_core)
			outp += sprintf(outp, " ");
		if (show_core) {
			if (c)
				outp += sprintf(outp, "%3d", c->core_id);
			else
				outp += sprintf(outp, "   ");
		}
		if (show_cpu)
			outp += sprintf(outp, " %3d", t->cpu_id);
	}

	/* %c0 */
	if (do_nhm_cstates) {
		if (show_pkg || show_core || show_cpu)
			outp += sprintf(outp, " ");
		if (!skip_c0)
			outp += sprintf(outp, "%6.2f", 100.0 * t->mperf/t->tsc);
		else
			outp += sprintf(outp, "  ****");
	}

	/* GHz */
	if (has_aperf) {
		if (!aperf_mperf_unstable) {
			outp += sprintf(outp, " %3.2f",
				1.0 * t->tsc / units * t->aperf /
				t->mperf / interval_float);
		} else {
			if (t->aperf > t->tsc || t->mperf > t->tsc) {
				outp += sprintf(outp, " ***");
			} else {
				outp += sprintf(outp, "%3.1f*",
					1.0 * t->tsc /
					units * t->aperf /
					t->mperf / interval_float);
			}
		}
	}

	/* TSC */
	outp += sprintf(outp, "%5.2f", 1.0 * t->tsc/units/interval_float);

	if (do_nhm_cstates) {
		if (!skip_c1)
			outp += sprintf(outp, " %6.2f", 100.0 * t->c1/t->tsc);
		else
			outp += sprintf(outp, "  ****");
	}

	/* print per-core data only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		goto done;

	if (do_nhm_cstates)
		outp += sprintf(outp, " %6.2f", 100.0 * c->c3/t->tsc);
	if (do_nhm_cstates)
		outp += sprintf(outp, " %6.2f", 100.0 * c->c6/t->tsc);
	if (do_snb_cstates)
		outp += sprintf(outp, " %6.2f", 100.0 * c->c7/t->tsc);

	/* print per-package data only for 1st core in package */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		goto done;

	if (do_snb_cstates)
		outp += sprintf(outp, " %6.2f", 100.0 * p->pc2/t->tsc);
	if (do_nhm_cstates)
		outp += sprintf(outp, " %6.2f", 100.0 * p->pc3/t->tsc);
	if (do_nhm_cstates)
		outp += sprintf(outp, " %6.2f", 100.0 * p->pc6/t->tsc);
	if (do_snb_cstates)
		outp += sprintf(outp, " %6.2f", 100.0 * p->pc7/t->tsc);
done:
	if (extra_msr_offset)
		outp += sprintf(outp, "  0x%016llx", t->extra_msr);
	outp += sprintf(outp, "\n");

	return 0;
}

void flush_stdout()
{
	fputs(output_buffer, stdout);
	outp = output_buffer;
}
void flush_stderr()
{
	fputs(output_buffer, stderr);
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

void
delta_package(struct pkg_data *new, struct pkg_data *old)
{
	old->pc2 = new->pc2 - old->pc2;
	old->pc3 = new->pc3 - old->pc3;
	old->pc6 = new->pc6 - old->pc6;
	old->pc7 = new->pc7 - old->pc7;
}

void
delta_core(struct core_data *new, struct core_data *old)
{
	old->c3 = new->c3 - old->c3;
	old->c6 = new->c6 - old->c6;
	old->c7 = new->c7 - old->c7;
}

void
delta_thread(struct thread_data *new, struct thread_data *old,
	struct core_data *core_delta)
{
	old->tsc = new->tsc - old->tsc;

	/* check for TSC < 1 Mcycles over interval */
	if (old->tsc < (1000 * 1000)) {
		fprintf(stderr, "Insanely slow TSC rate, TSC stops in idle?\n");
		fprintf(stderr, "You can disable all c-states by booting with \"idle=poll\"\n");
		fprintf(stderr, "or just the deep ones with \"processor.max_cstate=1\"\n");
		exit(-3);
	}

	old->c1 = new->c1 - old->c1;

	if ((new->aperf > old->aperf) && (new->mperf > old->mperf)) {
		old->aperf = new->aperf - old->aperf;
		old->mperf = new->mperf - old->mperf;
	} else {

		if (!aperf_mperf_unstable) {
			fprintf(stderr, "%s: APERF or MPERF went backwards *\n", progname);
			fprintf(stderr, "* Frequency results do not cover entire interval *\n");
			fprintf(stderr, "* fix this by running Linux-2.6.30 or later *\n");

			aperf_mperf_unstable = 1;
		}
		/*
		 * mperf delta is likely a huge "positive" number
		 * can not use it for calculating c0 time
		 */
		skip_c0 = 1;
		skip_c1 = 1;
	}


	/*
	 * As mperf and tsc collection are not atomic,
	 * it is possible for mperf's non-halted cycles
	 * to exceed TSC's all cycles: show c1 = 0% in that case.
	 */
	if (old->mperf > old->tsc)
		old->c1 = 0;
	else {
		/* normal case, derive c1 */
		old->c1 = old->tsc - old->mperf - core_delta->c3
				- core_delta->c6 - core_delta->c7;
	}
	if (old->mperf == 0) {
		if (verbose) fprintf(stderr, "cpu%d MPERF 0!\n", old->cpu_id);
		old->mperf = 1;	/* divide by 0 protection */
	}

	/*
	 * for "extra msr", just copy the latest w/o subtracting
	 */
	old->extra_msr = new->extra_msr;
}

int delta_cpu(struct thread_data *t, struct core_data *c,
	struct pkg_data *p, struct thread_data *t2,
	struct core_data *c2, struct pkg_data *p2)
{
	/* calculate core delta only for 1st thread in core */
	if (t->flags & CPU_IS_FIRST_THREAD_IN_CORE)
		delta_core(c, c2);

	/* always calculate thread delta */
	delta_thread(t, t2, c2);	/* c2 is core delta */

	/* calculate package delta only for 1st core in package */
	if (t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE)
		delta_package(p, p2);

	return 0;
}

void clear_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	t->tsc = 0;
	t->aperf = 0;
	t->mperf = 0;
	t->c1 = 0;

	/* tells format_counters to dump all fields from this set */
	t->flags = CPU_IS_FIRST_THREAD_IN_CORE | CPU_IS_FIRST_CORE_IN_PACKAGE;

	c->c3 = 0;
	c->c6 = 0;
	c->c7 = 0;

	p->pc2 = 0;
	p->pc3 = 0;
	p->pc6 = 0;
	p->pc7 = 0;
}
int sum_counters(struct thread_data *t, struct core_data *c,
	struct pkg_data *p)
{
	average.threads.tsc += t->tsc;
	average.threads.aperf += t->aperf;
	average.threads.mperf += t->mperf;
	average.threads.c1 += t->c1;

	/* sum per-core values only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		return 0;

	average.cores.c3 += c->c3;
	average.cores.c6 += c->c6;
	average.cores.c7 += c->c7;

	/* sum per-pkg values only for 1st core in pkg */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	average.packages.pc2 += p->pc2;
	average.packages.pc3 += p->pc3;
	average.packages.pc6 += p->pc6;
	average.packages.pc7 += p->pc7;

	return 0;
}
/*
 * sum the counters for all cpus in the system
 * compute the weighted average
 */
void compute_average(struct thread_data *t, struct core_data *c,
	struct pkg_data *p)
{
	clear_counters(&average.threads, &average.cores, &average.packages);

	for_all_cpus(sum_counters, t, c, p);

	average.threads.tsc /= topo.num_cpus;
	average.threads.aperf /= topo.num_cpus;
	average.threads.mperf /= topo.num_cpus;
	average.threads.c1 /= topo.num_cpus;

	average.cores.c3 /= topo.num_cores;
	average.cores.c6 /= topo.num_cores;
	average.cores.c7 /= topo.num_cores;

	average.packages.pc2 /= topo.num_packages;
	average.packages.pc3 /= topo.num_packages;
	average.packages.pc6 /= topo.num_packages;
	average.packages.pc7 /= topo.num_packages;
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

	if (cpu_migrate(cpu))
		return -1;

	t->tsc = rdtsc();	/* we are running on local CPU of interest */

	if (has_aperf) {
		if (get_msr(cpu, MSR_APERF, &t->aperf))
			return -3;
		if (get_msr(cpu, MSR_MPERF, &t->mperf))
			return -4;
	}

	if (extra_msr_offset)
		if (get_msr(cpu, extra_msr_offset, &t->extra_msr))
			return -5;

	/* collect core counters only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		return 0;

	if (do_nhm_cstates) {
		if (get_msr(cpu, MSR_CORE_C3_RESIDENCY, &c->c3))
			return -6;
		if (get_msr(cpu, MSR_CORE_C6_RESIDENCY, &c->c6))
			return -7;
	}

	if (do_snb_cstates)
		if (get_msr(cpu, MSR_CORE_C7_RESIDENCY, &c->c7))
			return -8;

	/* collect package counters only for 1st core in package */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (do_nhm_cstates) {
		if (get_msr(cpu, MSR_PKG_C3_RESIDENCY, &p->pc3))
			return -9;
		if (get_msr(cpu, MSR_PKG_C6_RESIDENCY, &p->pc6))
			return -10;
	}
	if (do_snb_cstates) {
		if (get_msr(cpu, MSR_PKG_C2_RESIDENCY, &p->pc2))
			return -11;
		if (get_msr(cpu, MSR_PKG_C7_RESIDENCY, &p->pc7))
			return -12;
	}
	return 0;
}

void print_verbose_header(void)
{
	unsigned long long msr;
	unsigned int ratio;

	if (!do_nehalem_platform_info)
		return;

	get_msr(0, MSR_NEHALEM_PLATFORM_INFO, &msr);

	ratio = (msr >> 40) & 0xFF;
	fprintf(stderr, "%d * %.0f = %.0f MHz max efficiency\n",
		ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0xFF;
	fprintf(stderr, "%d * %.0f = %.0f MHz TSC frequency\n",
		ratio, bclk, ratio * bclk);

	if (verbose > 1)
		fprintf(stderr, "MSR_NEHALEM_PLATFORM_INFO: 0x%llx\n", msr);

	if (!do_nehalem_turbo_ratio_limit)
		return;

	get_msr(0, MSR_NEHALEM_TURBO_RATIO_LIMIT, &msr);

	ratio = (msr >> 24) & 0xFF;
	if (ratio)
		fprintf(stderr, "%d * %.0f = %.0f MHz max turbo 4 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 16) & 0xFF;
	if (ratio)
		fprintf(stderr, "%d * %.0f = %.0f MHz max turbo 3 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 8) & 0xFF;
	if (ratio)
		fprintf(stderr, "%d * %.0f = %.0f MHz max turbo 2 active cores\n",
			ratio, bclk, ratio * bclk);

	ratio = (msr >> 0) & 0xFF;
	if (ratio)
		fprintf(stderr, "%d * %.0f = %.0f MHz max turbo 1 active cores\n",
			ratio, bclk, ratio * bclk);

}

void free_all_buffers(void)
{
	CPU_FREE(cpu_present_set);
	cpu_present_set = NULL;
	cpu_present_set = 0;

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
}

/*
 * cpu_is_first_sibling_in_core(cpu)
 * return 1 if given CPU is 1st HT sibling in the core
 */
int cpu_is_first_sibling_in_core(int cpu)
{
	char path[64];
	FILE *filep;
	int first_cpu;

	sprintf(path, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
	filep = fopen(path, "r");
	if (filep == NULL) {
		perror(path);
		exit(1);
	}
	fscanf(filep, "%d", &first_cpu);
	fclose(filep);
	return (cpu == first_cpu);
}

/*
 * cpu_is_first_core_in_package(cpu)
 * return 1 if given CPU is 1st core in package
 */
int cpu_is_first_core_in_package(int cpu)
{
	char path[64];
	FILE *filep;
	int first_cpu;

	sprintf(path, "/sys/devices/system/cpu/cpu%d/topology/core_siblings_list", cpu);
	filep = fopen(path, "r");
	if (filep == NULL) {
		perror(path);
		exit(1);
	}
	fscanf(filep, "%d", &first_cpu);
	fclose(filep);
	return (cpu == first_cpu);
}

int get_physical_package_id(int cpu)
{
	char path[80];
	FILE *filep;
	int pkg;

	sprintf(path, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
	filep = fopen(path, "r");
	if (filep == NULL) {
		perror(path);
		exit(1);
	}
	fscanf(filep, "%d", &pkg);
	fclose(filep);
	return pkg;
}

int get_core_id(int cpu)
{
	char path[80];
	FILE *filep;
	int core;

	sprintf(path, "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
	filep = fopen(path, "r");
	if (filep == NULL) {
		perror(path);
		exit(1);
	}
	fscanf(filep, "%d", &core);
	fclose(filep);
	return core;
}

int get_num_ht_siblings(int cpu)
{
	char path[80];
	FILE *filep;
	int sib1, sib2;
	int matches;
	char character;

	sprintf(path, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
	filep = fopen(path, "r");
	if (filep == NULL) {
		perror(path);
		exit(1);
	}
	/*
	 * file format:
	 * if a pair of number with a character between: 2 siblings (eg. 1-2, or 1,4)
	 * otherwinse 1 sibling (self).
	 */
	matches = fscanf(filep, "%d%c%d\n", &sib1, &character, &sib2);

	fclose(filep);

	if (matches == 3)
		return 2;
	else
		return 1;
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

	fp = fopen(proc_stat, "r");
	if (fp == NULL) {
		perror(proc_stat);
		exit(1);
	}

	retval = fscanf(fp, "cpu %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d\n");
	if (retval != 0) {
		perror("/proc/stat format");
		exit(1);
	}

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

void turbostat_loop()
{
	int retval;

restart:
	retval = for_all_cpus(get_counters, EVEN_COUNTERS);
	if (retval) {
		re_initialize();
		goto restart;
	}
	gettimeofday(&tv_even, (struct timezone *)NULL);

	while (1) {
		if (for_all_proc_cpus(cpu_is_not_present)) {
			re_initialize();
			goto restart;
		}
		sleep(interval_sec);
		retval = for_all_cpus(get_counters, ODD_COUNTERS);
		if (retval) {
			re_initialize();
			goto restart;
		}
		gettimeofday(&tv_odd, (struct timezone *)NULL);
		timersub(&tv_odd, &tv_even, &tv_delta);
		for_all_cpus_2(delta_cpu, ODD_COUNTERS, EVEN_COUNTERS);
		compute_average(EVEN_COUNTERS);
		format_all_counters(EVEN_COUNTERS);
		flush_stdout();
		sleep(interval_sec);
		retval = for_all_cpus(get_counters, EVEN_COUNTERS);
		if (retval) {
			re_initialize();
			goto restart;
		}
		gettimeofday(&tv_even, (struct timezone *)NULL);
		timersub(&tv_even, &tv_odd, &tv_delta);
		for_all_cpus_2(delta_cpu, EVEN_COUNTERS, ODD_COUNTERS);
		compute_average(ODD_COUNTERS);
		format_all_counters(ODD_COUNTERS);
		flush_stdout();
	}
}

void check_dev_msr()
{
	struct stat sb;

	if (stat("/dev/cpu/0/msr", &sb)) {
		fprintf(stderr, "no /dev/cpu/0/msr\n");
		fprintf(stderr, "Try \"# modprobe msr\"\n");
		exit(-5);
	}
}

void check_super_user()
{
	if (getuid() != 0) {
		fprintf(stderr, "must be root\n");
		exit(-6);
	}
}

int has_nehalem_turbo_ratio_limit(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	if (family != 6)
		return 0;

	switch (model) {
	case 0x1A:	/* Core i7, Xeon 5500 series - Bloomfield, Gainstown NHM-EP */
	case 0x1E:	/* Core i7 and i5 Processor - Clarksfield, Lynnfield, Jasper Forest */
	case 0x1F:	/* Core i7 and i5 Processor - Nehalem */
	case 0x25:	/* Westmere Client - Clarkdale, Arrandale */
	case 0x2C:	/* Westmere EP - Gulftown */
	case 0x2A:	/* SNB */
	case 0x2D:	/* SNB Xeon */
	case 0x3A:	/* IVB */
	case 0x3D:	/* IVB Xeon */
		return 1;
	case 0x2E:	/* Nehalem-EX Xeon - Beckton */
	case 0x2F:	/* Westmere-EX Xeon - Eagleton */
	default:
		return 0;
	}
}

int is_snb(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	switch (model) {
	case 0x2A:
	case 0x2D:
	case 0x3A:	/* IVB */
	case 0x3D:	/* IVB Xeon */
		return 1;
	}
	return 0;
}

double discover_bclk(unsigned int family, unsigned int model)
{
	if (is_snb(family, model))
		return 100.00;
	else
		return 133.33;
}

void check_cpuid()
{
	unsigned int eax, ebx, ecx, edx, max_level;
	unsigned int fms, family, model, stepping;

	eax = ebx = ecx = edx = 0;

	asm("cpuid" : "=a" (max_level), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (0));

	if (ebx == 0x756e6547 && edx == 0x49656e69 && ecx == 0x6c65746e)
		genuine_intel = 1;

	if (verbose)
		fprintf(stderr, "%.4s%.4s%.4s ",
			(char *)&ebx, (char *)&edx, (char *)&ecx);

	asm("cpuid" : "=a" (fms), "=c" (ecx), "=d" (edx) : "a" (1) : "ebx");
	family = (fms >> 8) & 0xf;
	model = (fms >> 4) & 0xf;
	stepping = fms & 0xf;
	if (family == 6 || family == 0xf)
		model += ((fms >> 16) & 0xf) << 4;

	if (verbose)
		fprintf(stderr, "%d CPUID levels; family:model:stepping 0x%x:%x:%x (%d:%d:%d)\n",
			max_level, family, model, stepping, family, model, stepping);

	if (!(edx & (1 << 5))) {
		fprintf(stderr, "CPUID: no MSR\n");
		exit(1);
	}

	/*
	 * check max extended function levels of CPUID.
	 * This is needed to check for invariant TSC.
	 * This check is valid for both Intel and AMD.
	 */
	ebx = ecx = edx = 0;
	asm("cpuid" : "=a" (max_level), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (0x80000000));

	if (max_level < 0x80000007) {
		fprintf(stderr, "CPUID: no invariant TSC (max_level 0x%x)\n", max_level);
		exit(1);
	}

	/*
	 * Non-Stop TSC is advertised by CPUID.EAX=0x80000007: EDX.bit8
	 * this check is valid for both Intel and AMD
	 */
	asm("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (0x80000007));
	has_invariant_tsc = edx & (1 << 8);

	if (!has_invariant_tsc) {
		fprintf(stderr, "No invariant TSC\n");
		exit(1);
	}

	/*
	 * APERF/MPERF is advertised by CPUID.EAX=0x6: ECX.bit0
	 * this check is valid for both Intel and AMD
	 */

	asm("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (0x6));
	has_aperf = ecx & (1 << 0);
	if (!has_aperf) {
		fprintf(stderr, "No APERF MSR\n");
		exit(1);
	}

	do_nehalem_platform_info = genuine_intel && has_invariant_tsc;
	do_nhm_cstates = genuine_intel;	/* all Intel w/ non-stop TSC have NHM counters */
	do_snb_cstates = is_snb(family, model);
	bclk = discover_bclk(family, model);

	do_nehalem_turbo_ratio_limit = has_nehalem_turbo_ratio_limit(family, model);
}


void usage()
{
	fprintf(stderr, "%s: [-v] [-M MSR#] [-i interval_sec | command ...]\n",
		progname);
	exit(1);
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
		show_cpu = 1;

	if (verbose > 1)
		fprintf(stderr, "num_cpus %d max_cpu_num %d\n", topo.num_cpus, topo.max_cpu_num);

	cpus = calloc(1, (topo.max_cpu_num  + 1) * sizeof(struct cpu_topology));
	if (cpus == NULL) {
		perror("calloc cpus");
		exit(1);
	}

	/*
	 * Allocate and initialize cpu_present_set
	 */
	cpu_present_set = CPU_ALLOC((topo.max_cpu_num + 1));
	if (cpu_present_set == NULL) {
		perror("CPU_ALLOC");
		exit(3);
	}
	cpu_present_setsize = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(cpu_present_setsize, cpu_present_set);
	for_all_proc_cpus(mark_cpu_present);

	/*
	 * Allocate and initialize cpu_affinity_set
	 */
	cpu_affinity_set = CPU_ALLOC((topo.max_cpu_num + 1));
	if (cpu_affinity_set == NULL) {
		perror("CPU_ALLOC");
		exit(3);
	}
	cpu_affinity_setsize = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(cpu_affinity_setsize, cpu_affinity_set);


	/*
	 * For online cpus
	 * find max_core_id, max_package_id
	 */
	for (i = 0; i <= topo.max_cpu_num; ++i) {
		int siblings;

		if (cpu_is_not_present(i)) {
			if (verbose > 1)
				fprintf(stderr, "cpu%d NOT PRESENT\n", i);
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
		if (verbose > 1)
			fprintf(stderr, "cpu %d pkg %d core %d\n",
				i, cpus[i].physical_package_id, cpus[i].core_id);
	}
	topo.num_cores_per_pkg = max_core_id + 1;
	if (verbose > 1)
		fprintf(stderr, "max_core_id %d, sizing for %d cores per package\n",
			max_core_id, topo.num_cores_per_pkg);
	if (!summary_only && topo.num_cores_per_pkg > 1)
		show_core = 1;

	topo.num_packages = max_package_id + 1;
	if (verbose > 1)
		fprintf(stderr, "max_package_id %d, sizing for %d packages\n",
			max_package_id, topo.num_packages);
	if (!summary_only && topo.num_packages > 1)
		show_pkg = 1;

	topo.num_threads_per_core = max_siblings;
	if (verbose > 1)
		fprintf(stderr, "max_siblings %d\n", max_siblings);

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
	perror("calloc counters");
	exit(1);
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

	if (cpu_is_first_sibling_in_core(cpu_id)) {
		my_thread_id = 0;
		topo.num_cores++;
	} else {
		my_thread_id = 1;
	}

	init_counter(EVEN_COUNTERS, my_thread_id, my_core_id, my_package_id, cpu_id);
	init_counter(ODD_COUNTERS, my_thread_id, my_core_id, my_package_id, cpu_id);
	return 0;
}

void allocate_output_buffer()
{
	output_buffer = calloc(1, (1 + topo.num_cpus) * 128);
	outp = output_buffer;
	if (outp == NULL) {
		perror("calloc");
		exit(-1);
	}
}

void setup_all_buffers(void)
{
	topology_probe();
	allocate_counters(&thread_even, &core_even, &package_even);
	allocate_counters(&thread_odd, &core_odd, &package_odd);
	allocate_output_buffer();
	for_all_proc_cpus(initialize_counters);
}
void turbostat_init()
{
	check_cpuid();

	check_dev_msr();
	check_super_user();

	setup_all_buffers();

	if (verbose)
		print_verbose_header();
}

int fork_it(char **argv)
{
	pid_t child_pid;

	for_all_cpus(get_counters, EVEN_COUNTERS);
	/* clear affinity side-effect of get_counters() */
	sched_setaffinity(0, cpu_present_setsize, cpu_present_set);
	gettimeofday(&tv_even, (struct timezone *)NULL);

	child_pid = fork();
	if (!child_pid) {
		/* child */
		execvp(argv[0], argv);
	} else {
		int status;

		/* parent */
		if (child_pid == -1) {
			perror("fork");
			exit(1);
		}

		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
		if (waitpid(child_pid, &status, 0) == -1) {
			perror("wait");
			exit(1);
		}
	}
	/*
	 * n.b. fork_it() does not check for errors from for_all_cpus()
	 * because re-starting is problematic when forking
	 */
	for_all_cpus(get_counters, ODD_COUNTERS);
	gettimeofday(&tv_odd, (struct timezone *)NULL);
	timersub(&tv_odd, &tv_even, &tv_delta);
	for_all_cpus_2(delta_cpu, ODD_COUNTERS, EVEN_COUNTERS);
	compute_average(EVEN_COUNTERS);
	format_all_counters(EVEN_COUNTERS);
	flush_stderr();

	fprintf(stderr, "%.6f sec\n", tv_delta.tv_sec + tv_delta.tv_usec/1000000.0);

	return 0;
}

void cmdline(int argc, char **argv)
{
	int opt;

	progname = argv[0];

	while ((opt = getopt(argc, argv, "+cpsvi:M:")) != -1) {
		switch (opt) {
		case 'c':
			show_core_only++;
			break;
		case 'p':
			show_pkg_only++;
			break;
		case 's':
			summary_only++;
			break;
		case 'v':
			verbose++;
			break;
		case 'i':
			interval_sec = atoi(optarg);
			break;
		case 'M':
			sscanf(optarg, "%x", &extra_msr_offset);
			if (verbose > 1)
				fprintf(stderr, "MSR 0x%X\n", extra_msr_offset);
			break;
		default:
			usage();
		}
	}
}

int main(int argc, char **argv)
{
	cmdline(argc, argv);

	if (verbose > 1)
		fprintf(stderr, "turbostat v2.0 May 16, 2012"
			" - Len Brown <lenb@kernel.org>\n");

	turbostat_init();

	/*
	 * if any params left, it must be a command to fork
	 */
	if (argc - optind)
		return fork_it(argv + optind);
	else
		turbostat_loop();

	return 0;
}
