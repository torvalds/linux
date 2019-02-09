/*
 * x86_energy_perf_policy -- set the energy versus performance
 * policy preference bias on recent X86 processors.
 */
/*
 * Copyright (c) 2010 - 2017 Intel Corporation.
 * Len Brown <len.brown@intel.com>
 *
 * This program is released under GPL v2
 */

#define _GNU_SOURCE
#include MSRHEADER
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <getopt.h>
#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <cpuid.h>
#include <errno.h>

#define	OPTARG_NORMAL			(INT_MAX - 1)
#define	OPTARG_POWER			(INT_MAX - 2)
#define	OPTARG_BALANCE_POWER		(INT_MAX - 3)
#define	OPTARG_BALANCE_PERFORMANCE	(INT_MAX - 4)
#define	OPTARG_PERFORMANCE		(INT_MAX - 5)

struct msr_hwp_cap {
	unsigned char highest;
	unsigned char guaranteed;
	unsigned char efficient;
	unsigned char lowest;
};

struct msr_hwp_request {
	unsigned char hwp_min;
	unsigned char hwp_max;
	unsigned char hwp_desired;
	unsigned char hwp_epp;
	unsigned int hwp_window;
	unsigned char hwp_use_pkg;
} req_update;

unsigned int debug;
unsigned int verbose;
unsigned int force;
char *progname;
int base_cpu;
unsigned char update_epb;
unsigned long long new_epb;
unsigned char turbo_is_enabled;
unsigned char update_turbo;
unsigned char turbo_update_value;
unsigned char update_hwp_epp;
unsigned char update_hwp_min;
unsigned char update_hwp_max;
unsigned char update_hwp_desired;
unsigned char update_hwp_window;
unsigned char update_hwp_use_pkg;
unsigned char update_hwp_enable;
#define hwp_update_enabled() (update_hwp_enable | update_hwp_epp | update_hwp_max | update_hwp_min | update_hwp_desired | update_hwp_window | update_hwp_use_pkg)
int max_cpu_num;
int max_pkg_num;
#define MAX_PACKAGES 64
unsigned int first_cpu_in_pkg[MAX_PACKAGES];
unsigned long long pkg_present_set;
unsigned long long pkg_selected_set;
cpu_set_t *cpu_present_set;
cpu_set_t *cpu_selected_set;
int genuine_intel;

size_t cpu_setsize;

char *proc_stat = "/proc/stat";

unsigned int has_epb;	/* MSR_IA32_ENERGY_PERF_BIAS */
unsigned int has_hwp;	/* IA32_PM_ENABLE, IA32_HWP_CAPABILITIES */
			/* IA32_HWP_REQUEST, IA32_HWP_STATUS */
unsigned int has_hwp_notify;		/* IA32_HWP_INTERRUPT */
unsigned int has_hwp_activity_window;	/* IA32_HWP_REQUEST[bits 41:32] */
unsigned int has_hwp_epp;	/* IA32_HWP_REQUEST[bits 31:24] */
unsigned int has_hwp_request_pkg;	/* IA32_HWP_REQUEST_PKG */

unsigned int bdx_highest_ratio;

/*
 * maintain compatibility with original implementation, but don't document it:
 */
void usage(void)
{
	fprintf(stderr, "%s [options] [scope][field value]\n", progname);
	fprintf(stderr, "scope: --cpu cpu-list [--hwp-use-pkg #] | --pkg pkg-list\n");
	fprintf(stderr, "field: --all | --epb | --hwp-epp | --hwp-min | --hwp-max | --hwp-desired\n");
	fprintf(stderr, "other: --hwp-enable | --turbo-enable (0 | 1) | --help | --force\n");
	fprintf(stderr,
		"value: ( # | \"normal\" | \"performance\" | \"balance-performance\" | \"balance-power\"| \"power\")\n");
	fprintf(stderr, "--hwp-window usec\n");

	fprintf(stderr, "Specify only Energy Performance BIAS (legacy usage):\n");
	fprintf(stderr, "%s: [-c cpu] [-v] (-r | policy-value )\n", progname);

	exit(1);
}

/*
 * If bdx_highest_ratio is set,
 * then we must translate between MSR format and simple ratio
 * used on the cmdline.
 */
int ratio_2_msr_perf(int ratio)
{
	int msr_perf;

	if (!bdx_highest_ratio)
		return ratio;

	msr_perf = ratio * 255 / bdx_highest_ratio;

	if (debug)
		fprintf(stderr, "%d = ratio_to_msr_perf(%d)\n", msr_perf, ratio);

	return msr_perf;
}
int msr_perf_2_ratio(int msr_perf)
{
	int ratio;
	double d;

	if (!bdx_highest_ratio)
		return msr_perf;

	d = (double)msr_perf * (double) bdx_highest_ratio / 255.0;
	d = d + 0.5;	/* round */
	ratio = (int)d;

	if (debug)
		fprintf(stderr, "%d = msr_perf_ratio(%d) {%f}\n", ratio, msr_perf, d);

	return ratio;
}
int parse_cmdline_epb(int i)
{
	if (!has_epb)
		errx(1, "EPB not enabled on this platform");

	update_epb = 1;

	switch (i) {
	case OPTARG_POWER:
		return ENERGY_PERF_BIAS_POWERSAVE;
	case OPTARG_BALANCE_POWER:
		return ENERGY_PERF_BIAS_BALANCE_POWERSAVE;
	case OPTARG_NORMAL:
		return ENERGY_PERF_BIAS_NORMAL;
	case OPTARG_BALANCE_PERFORMANCE:
		return ENERGY_PERF_BIAS_BALANCE_PERFORMANCE;
	case OPTARG_PERFORMANCE:
		return ENERGY_PERF_BIAS_PERFORMANCE;
	}
	if (i < 0 || i > ENERGY_PERF_BIAS_POWERSAVE)
		errx(1, "--epb must be from 0 to 15");
	return i;
}

#define HWP_CAP_LOWEST 0
#define HWP_CAP_HIGHEST 255

/*
 * "performance" changes hwp_min to cap.highest
 * All others leave it at cap.lowest
 */
int parse_cmdline_hwp_min(int i)
{
	update_hwp_min = 1;

	switch (i) {
	case OPTARG_POWER:
	case OPTARG_BALANCE_POWER:
	case OPTARG_NORMAL:
	case OPTARG_BALANCE_PERFORMANCE:
		return HWP_CAP_LOWEST;
	case OPTARG_PERFORMANCE:
		return HWP_CAP_HIGHEST;
	}
	return i;
}
/*
 * "power" changes hwp_max to cap.lowest
 * All others leave it at cap.highest
 */
int parse_cmdline_hwp_max(int i)
{
	update_hwp_max = 1;

	switch (i) {
	case OPTARG_POWER:
		return HWP_CAP_LOWEST;
	case OPTARG_NORMAL:
	case OPTARG_BALANCE_POWER:
	case OPTARG_BALANCE_PERFORMANCE:
	case OPTARG_PERFORMANCE:
		return HWP_CAP_HIGHEST;
	}
	return i;
}
/*
 * for --hwp-des, all strings leave it in autonomous mode
 * If you want to change it, you need to explicitly pick a value
 */
int parse_cmdline_hwp_desired(int i)
{
	update_hwp_desired = 1;

	switch (i) {
	case OPTARG_POWER:
	case OPTARG_BALANCE_POWER:
	case OPTARG_BALANCE_PERFORMANCE:
	case OPTARG_NORMAL:
	case OPTARG_PERFORMANCE:
		return 0;	/* autonomous */
	}
	return i;
}

int parse_cmdline_hwp_window(int i)
{
	unsigned int exponent;

	update_hwp_window = 1;

	switch (i) {
	case OPTARG_POWER:
	case OPTARG_BALANCE_POWER:
	case OPTARG_NORMAL:
	case OPTARG_BALANCE_PERFORMANCE:
	case OPTARG_PERFORMANCE:
		return 0;
	}
	if (i < 0 || i > 1270000000) {
		fprintf(stderr, "--hwp-window: 0 for auto; 1 - 1270000000 usec for window duration\n");
		usage();
	}
	for (exponent = 0; ; ++exponent) {
		if (debug)
			printf("%d 10^%d\n", i, exponent);

		if (i <= 127)
			break;

		i = i / 10;
	}
	if (debug)
		fprintf(stderr, "%d*10^%d: 0x%x\n", i, exponent, (exponent << 7) | i);

	return (exponent << 7) | i;
}
int parse_cmdline_hwp_epp(int i)
{
	update_hwp_epp = 1;

	switch (i) {
	case OPTARG_POWER:
		return HWP_EPP_POWERSAVE;
	case OPTARG_BALANCE_POWER:
		return HWP_EPP_BALANCE_POWERSAVE;
	case OPTARG_NORMAL:
	case OPTARG_BALANCE_PERFORMANCE:
		return HWP_EPP_BALANCE_PERFORMANCE;
	case OPTARG_PERFORMANCE:
		return HWP_EPP_PERFORMANCE;
	}
	if (i < 0 || i > 0xff) {
		fprintf(stderr, "--hwp-epp must be from 0 to 0xff\n");
		usage();
	}
	return i;
}
int parse_cmdline_turbo(int i)
{
	update_turbo = 1;

	switch (i) {
	case OPTARG_POWER:
		return 0;
	case OPTARG_NORMAL:
	case OPTARG_BALANCE_POWER:
	case OPTARG_BALANCE_PERFORMANCE:
	case OPTARG_PERFORMANCE:
		return 1;
	}
	if (i < 0 || i > 1) {
		fprintf(stderr, "--turbo-enable: 1 to enable, 0 to disable\n");
		usage();
	}
	return i;
}

int parse_optarg_string(char *s)
{
	int i;
	char *endptr;

	if (!strncmp(s, "default", 7))
		return OPTARG_NORMAL;

	if (!strncmp(s, "normal", 6))
		return OPTARG_NORMAL;

	if (!strncmp(s, "power", 9))
		return OPTARG_POWER;

	if (!strncmp(s, "balance-power", 17))
		return OPTARG_BALANCE_POWER;

	if (!strncmp(s, "balance-performance", 19))
		return OPTARG_BALANCE_PERFORMANCE;

	if (!strncmp(s, "performance", 11))
		return OPTARG_PERFORMANCE;

	i = strtol(s, &endptr, 0);
	if (s == endptr) {
		fprintf(stderr, "no digits in \"%s\"\n", s);
		usage();
	}
	if (i == LONG_MIN || i == LONG_MAX)
		errx(-1, "%s", s);

	if (i > 0xFF)
		errx(-1, "%d (0x%x) must be < 256", i, i);

	if (i < 0)
		errx(-1, "%d (0x%x) must be >= 0", i, i);
	return i;
}

void parse_cmdline_all(char *s)
{
	force++;
	update_hwp_enable = 1;
	req_update.hwp_min = parse_cmdline_hwp_min(parse_optarg_string(s));
	req_update.hwp_max = parse_cmdline_hwp_max(parse_optarg_string(s));
	req_update.hwp_epp = parse_cmdline_hwp_epp(parse_optarg_string(s));
	if (has_epb)
		new_epb = parse_cmdline_epb(parse_optarg_string(s));
	turbo_update_value = parse_cmdline_turbo(parse_optarg_string(s));
	req_update.hwp_desired = parse_cmdline_hwp_desired(parse_optarg_string(s));
	req_update.hwp_window = parse_cmdline_hwp_window(parse_optarg_string(s));
}

void validate_cpu_selected_set(void)
{
	int cpu;

	if (CPU_COUNT_S(cpu_setsize, cpu_selected_set) == 0)
		errx(0, "no CPUs requested");

	for (cpu = 0; cpu <= max_cpu_num; ++cpu) {
		if (CPU_ISSET_S(cpu, cpu_setsize, cpu_selected_set))
			if (!CPU_ISSET_S(cpu, cpu_setsize, cpu_present_set))
				errx(1, "Requested cpu% is not present", cpu);
	}
}

void parse_cmdline_cpu(char *s)
{
	char *startp, *endp;
	int cpu = 0;

	if (pkg_selected_set) {
		usage();
		errx(1, "--cpu | --pkg");
	}
	cpu_selected_set = CPU_ALLOC((max_cpu_num + 1));
	if (cpu_selected_set == NULL)
		err(1, "cpu_selected_set");
	CPU_ZERO_S(cpu_setsize, cpu_selected_set);

	for (startp = s; startp && *startp;) {

		if (*startp == ',') {
			startp++;
			continue;
		}

		if (*startp == '-') {
			int end_cpu;

			startp++;
			end_cpu = strtol(startp, &endp, 10);
			if (startp == endp)
				continue;

			while (cpu <= end_cpu) {
				if (cpu > max_cpu_num)
					errx(1, "Requested cpu%d exceeds max cpu%d", cpu, max_cpu_num);
				CPU_SET_S(cpu, cpu_setsize, cpu_selected_set);
				cpu++;
			}
			startp = endp;
			continue;
		}

		if (strncmp(startp, "all", 3) == 0) {
			for (cpu = 0; cpu <= max_cpu_num; cpu += 1) {
				if (CPU_ISSET_S(cpu, cpu_setsize, cpu_present_set))
					CPU_SET_S(cpu, cpu_setsize, cpu_selected_set);
			}
			startp += 3;
			if (*startp == 0)
				break;
		}
		/* "--cpu even" is not documented */
		if (strncmp(startp, "even", 4) == 0) {
			for (cpu = 0; cpu <= max_cpu_num; cpu += 2) {
				if (CPU_ISSET_S(cpu, cpu_setsize, cpu_present_set))
					CPU_SET_S(cpu, cpu_setsize, cpu_selected_set);
			}
			startp += 4;
			if (*startp == 0)
				break;
		}

		/* "--cpu odd" is not documented */
		if (strncmp(startp, "odd", 3) == 0) {
			for (cpu = 1; cpu <= max_cpu_num; cpu += 2) {
				if (CPU_ISSET_S(cpu, cpu_setsize, cpu_present_set))
					CPU_SET_S(cpu, cpu_setsize, cpu_selected_set);
			}
			startp += 3;
			if (*startp == 0)
				break;
		}

		cpu = strtol(startp, &endp, 10);
		if (startp == endp)
			errx(1, "--cpu cpu-set: confused by '%s'", startp);
		if (cpu > max_cpu_num)
			errx(1, "Requested cpu%d exceeds max cpu%d", cpu, max_cpu_num);
		CPU_SET_S(cpu, cpu_setsize, cpu_selected_set);
		startp = endp;
	}

	validate_cpu_selected_set();

}

void parse_cmdline_pkg(char *s)
{
	char *startp, *endp;
	int pkg = 0;

	if (cpu_selected_set) {
		usage();
		errx(1, "--pkg | --cpu");
	}
	pkg_selected_set = 0;

	for (startp = s; startp && *startp;) {

		if (*startp == ',') {
			startp++;
			continue;
		}

		if (*startp == '-') {
			int end_pkg;

			startp++;
			end_pkg = strtol(startp, &endp, 10);
			if (startp == endp)
				continue;

			while (pkg <= end_pkg) {
				if (pkg > max_pkg_num)
					errx(1, "Requested pkg%d exceeds max pkg%d", pkg, max_pkg_num);
				pkg_selected_set |= 1 << pkg;
				pkg++;
			}
			startp = endp;
			continue;
		}

		if (strncmp(startp, "all", 3) == 0) {
			pkg_selected_set = pkg_present_set;
			return;
		}

		pkg = strtol(startp, &endp, 10);
		if (pkg > max_pkg_num)
			errx(1, "Requested pkg%d Exceeds max pkg%d", pkg, max_pkg_num);
		pkg_selected_set |= 1 << pkg;
		startp = endp;
	}
}

void for_packages(unsigned long long pkg_set, int (func)(int))
{
	int pkg_num;

	for (pkg_num = 0; pkg_num <= max_pkg_num; ++pkg_num) {
		if (pkg_set & (1UL << pkg_num))
			func(pkg_num);
	}
}

void print_version(void)
{
	printf("x86_energy_perf_policy 17.05.11 (C) Len Brown <len.brown@intel.com>\n");
}

void cmdline(int argc, char **argv)
{
	int opt;
	int option_index = 0;

	static struct option long_options[] = {
		{"all",		required_argument,	0, 'a'},
		{"cpu",		required_argument,	0, 'c'},
		{"pkg",		required_argument,	0, 'p'},
		{"debug",	no_argument,		0, 'd'},
		{"hwp-desired",	required_argument,	0, 'D'},
		{"epb",	required_argument,	0, 'B'},
		{"force",	no_argument,	0, 'f'},
		{"hwp-enable",	no_argument,	0, 'e'},
		{"help",	no_argument,	0, 'h'},
		{"hwp-epp",	required_argument,	0, 'P'},
		{"hwp-min",	required_argument,	0, 'm'},
		{"hwp-max",	required_argument,	0, 'M'},
		{"read",	no_argument,		0, 'r'},
		{"turbo-enable",	required_argument,	0, 't'},
		{"hwp-use-pkg",	required_argument,	0, 'u'},
		{"version",	no_argument,		0, 'v'},
		{"hwp-window",	required_argument,	0, 'w'},
		{0,		0,			0, 0 }
	};

	progname = argv[0];

	while ((opt = getopt_long_only(argc, argv, "+a:c:dD:E:e:f:m:M:rt:u:vw:",
				long_options, &option_index)) != -1) {
		switch (opt) {
		case 'a':
			parse_cmdline_all(optarg);
			break;
		case 'B':
			new_epb = parse_cmdline_epb(parse_optarg_string(optarg));
			break;
		case 'c':
			parse_cmdline_cpu(optarg);
			break;
		case 'e':
			update_hwp_enable = 1;
			break;
		case 'h':
			usage();
			break;
		case 'd':
			debug++;
			verbose++;
			break;
		case 'f':
			force++;
			break;
		case 'D':
			req_update.hwp_desired = parse_cmdline_hwp_desired(parse_optarg_string(optarg));
			break;
		case 'm':
			req_update.hwp_min = parse_cmdline_hwp_min(parse_optarg_string(optarg));
			break;
		case 'M':
			req_update.hwp_max = parse_cmdline_hwp_max(parse_optarg_string(optarg));
			break;
		case 'p':
			parse_cmdline_pkg(optarg);
			break;
		case 'P':
			req_update.hwp_epp = parse_cmdline_hwp_epp(parse_optarg_string(optarg));
			break;
		case 'r':
			/* v1 used -r to specify read-only mode, now the default */
			break;
		case 't':
			turbo_update_value = parse_cmdline_turbo(parse_optarg_string(optarg));
			break;
		case 'u':
			update_hwp_use_pkg++;
			if (atoi(optarg) == 0)
				req_update.hwp_use_pkg = 0;
			else
				req_update.hwp_use_pkg = 1;
			break;
		case 'v':
			print_version();
			exit(0);
			break;
		case 'w':
			req_update.hwp_window = parse_cmdline_hwp_window(parse_optarg_string(optarg));
			break;
		default:
			usage();
		}
	}
	/*
	 * v1 allowed "performance"|"normal"|"power" with no policy specifier
	 * to update BIAS.  Continue to support that, even though no longer documented.
	 */
	if (argc == optind + 1)
		new_epb = parse_cmdline_epb(parse_optarg_string(argv[optind]));

	if (argc > optind + 1) {
		fprintf(stderr, "stray parameter '%s'\n", argv[optind + 1]);
		usage();
	}
}


int get_msr(int cpu, int offset, unsigned long long *msr)
{
	int retval;
	char pathname[32];
	int fd;

	sprintf(pathname, "/dev/cpu/%d/msr", cpu);
	fd = open(pathname, O_RDONLY);
	if (fd < 0)
		err(-1, "%s open failed, try chown or chmod +r /dev/cpu/*/msr, or run as root", pathname);

	retval = pread(fd, msr, sizeof(*msr), offset);
	if (retval != sizeof(*msr))
		err(-1, "%s offset 0x%llx read failed", pathname, (unsigned long long)offset);

	if (debug > 1)
		fprintf(stderr, "get_msr(cpu%d, 0x%X, 0x%llX)\n", cpu, offset, *msr);

	close(fd);
	return 0;
}

int put_msr(int cpu, int offset, unsigned long long new_msr)
{
	char pathname[32];
	int retval;
	int fd;

	sprintf(pathname, "/dev/cpu/%d/msr", cpu);
	fd = open(pathname, O_RDWR);
	if (fd < 0)
		err(-1, "%s open failed, try chown or chmod +r /dev/cpu/*/msr, or run as root", pathname);

	retval = pwrite(fd, &new_msr, sizeof(new_msr), offset);
	if (retval != sizeof(new_msr))
		err(-2, "pwrite(cpu%d, offset 0x%x, 0x%llx) = %d", cpu, offset, new_msr, retval);

	close(fd);

	if (debug > 1)
		fprintf(stderr, "put_msr(cpu%d, 0x%X, 0x%llX)\n", cpu, offset, new_msr);

	return 0;
}

void print_hwp_cap(int cpu, struct msr_hwp_cap *cap, char *str)
{
	if (cpu != -1)
		printf("cpu%d: ", cpu);

	printf("HWP_CAP: low %d eff %d guar %d high %d\n",
		cap->lowest, cap->efficient, cap->guaranteed, cap->highest);
}
void read_hwp_cap(int cpu, struct msr_hwp_cap *cap, unsigned int msr_offset)
{
	unsigned long long msr;

	get_msr(cpu, msr_offset, &msr);

	cap->highest = msr_perf_2_ratio(HWP_HIGHEST_PERF(msr));
	cap->guaranteed = msr_perf_2_ratio(HWP_GUARANTEED_PERF(msr));
	cap->efficient = msr_perf_2_ratio(HWP_MOSTEFFICIENT_PERF(msr));
	cap->lowest = msr_perf_2_ratio(HWP_LOWEST_PERF(msr));
}

void print_hwp_request(int cpu, struct msr_hwp_request *h, char *str)
{
	if (cpu != -1)
		printf("cpu%d: ", cpu);

	if (str)
		printf("%s", str);

	printf("HWP_REQ: min %d max %d des %d epp %d window 0x%x (%d*10^%dus) use_pkg %d\n",
		h->hwp_min, h->hwp_max, h->hwp_desired, h->hwp_epp,
		h->hwp_window, h->hwp_window & 0x7F, (h->hwp_window >> 7) & 0x7, h->hwp_use_pkg);
}
void print_hwp_request_pkg(int pkg, struct msr_hwp_request *h, char *str)
{
	printf("pkg%d: ", pkg);

	if (str)
		printf("%s", str);

	printf("HWP_REQ_PKG: min %d max %d des %d epp %d window 0x%x (%d*10^%dus)\n",
		h->hwp_min, h->hwp_max, h->hwp_desired, h->hwp_epp,
		h->hwp_window, h->hwp_window & 0x7F, (h->hwp_window >> 7) & 0x7);
}
void read_hwp_request(int cpu, struct msr_hwp_request *hwp_req, unsigned int msr_offset)
{
	unsigned long long msr;

	get_msr(cpu, msr_offset, &msr);

	hwp_req->hwp_min = msr_perf_2_ratio((((msr) >> 0) & 0xff));
	hwp_req->hwp_max = msr_perf_2_ratio((((msr) >> 8) & 0xff));
	hwp_req->hwp_desired = msr_perf_2_ratio((((msr) >> 16) & 0xff));
	hwp_req->hwp_epp = (((msr) >> 24) & 0xff);
	hwp_req->hwp_window = (((msr) >> 32) & 0x3ff);
	hwp_req->hwp_use_pkg = (((msr) >> 42) & 0x1);
}

void write_hwp_request(int cpu, struct msr_hwp_request *hwp_req, unsigned int msr_offset)
{
	unsigned long long msr = 0;

	if (debug > 1)
		printf("cpu%d: requesting min %d max %d des %d epp %d window 0x%0x use_pkg %d\n",
			cpu, hwp_req->hwp_min, hwp_req->hwp_max,
			hwp_req->hwp_desired, hwp_req->hwp_epp,
			hwp_req->hwp_window, hwp_req->hwp_use_pkg);

	msr |= HWP_MIN_PERF(ratio_2_msr_perf(hwp_req->hwp_min));
	msr |= HWP_MAX_PERF(ratio_2_msr_perf(hwp_req->hwp_max));
	msr |= HWP_DESIRED_PERF(ratio_2_msr_perf(hwp_req->hwp_desired));
	msr |= HWP_ENERGY_PERF_PREFERENCE(hwp_req->hwp_epp);
	msr |= HWP_ACTIVITY_WINDOW(hwp_req->hwp_window);
	msr |= HWP_PACKAGE_CONTROL(hwp_req->hwp_use_pkg);

	put_msr(cpu, msr_offset, msr);
}

int print_cpu_msrs(int cpu)
{
	unsigned long long msr;
	struct msr_hwp_request req;
	struct msr_hwp_cap cap;

	if (has_epb) {
		get_msr(cpu, MSR_IA32_ENERGY_PERF_BIAS, &msr);

		printf("cpu%d: EPB %u\n", cpu, (unsigned int) msr);
	}

	if (!has_hwp)
		return 0;

	read_hwp_request(cpu, &req, MSR_HWP_REQUEST);
	print_hwp_request(cpu, &req, "");

	read_hwp_cap(cpu, &cap, MSR_HWP_CAPABILITIES);
	print_hwp_cap(cpu, &cap, "");

	return 0;
}

int print_pkg_msrs(int pkg)
{
	struct msr_hwp_request req;
	unsigned long long msr;

	if (!has_hwp)
		return 0;

	read_hwp_request(first_cpu_in_pkg[pkg], &req, MSR_HWP_REQUEST_PKG);
	print_hwp_request_pkg(pkg, &req, "");

	if (has_hwp_notify) {
		get_msr(first_cpu_in_pkg[pkg], MSR_HWP_INTERRUPT, &msr);
		fprintf(stderr,
		"pkg%d: MSR_HWP_INTERRUPT: 0x%08llx (Excursion_Min-%sabled, Guaranteed_Perf_Change-%sabled)\n",
		pkg, msr,
		((msr) & 0x2) ? "EN" : "Dis",
		((msr) & 0x1) ? "EN" : "Dis");
	}
	get_msr(first_cpu_in_pkg[pkg], MSR_HWP_STATUS, &msr);
	fprintf(stderr,
		"pkg%d: MSR_HWP_STATUS: 0x%08llx (%sExcursion_Min, %sGuaranteed_Perf_Change)\n",
		pkg, msr,
		((msr) & 0x4) ? "" : "No-",
		((msr) & 0x1) ? "" : "No-");

	return 0;
}

/*
 * Assumption: All HWP systems have 100 MHz bus clock
 */
int ratio_2_sysfs_khz(int ratio)
{
	int bclk_khz = 100 * 1000;	/* 100,000 KHz = 100 MHz */

	return ratio * bclk_khz;
}
/*
 * If HWP is enabled and cpufreq sysfs attribtes are present,
 * then update sysfs, so that it will not become
 * stale when we write to MSRs.
 * (intel_pstate's max_perf_pct and min_perf_pct will follow cpufreq,
 *  so we don't have to touch that.)
 */
void update_cpufreq_scaling_freq(int is_max, int cpu, unsigned int ratio)
{
	char pathname[64];
	FILE *fp;
	int retval;
	int khz;

	sprintf(pathname, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_%s_freq",
		cpu, is_max ? "max" : "min");

	fp = fopen(pathname, "w");
	if (!fp) {
		if (debug)
			perror(pathname);
		return;
	}

	khz = ratio_2_sysfs_khz(ratio);
	retval = fprintf(fp, "%d", khz);
	if (retval < 0)
		if (debug)
			perror("fprintf");
	if (debug)
		printf("echo %d > %s\n", khz, pathname);

	fclose(fp);
}

/*
 * We update all sysfs before updating any MSRs because of
 * bugs in cpufreq/intel_pstate where the sysfs writes
 * for a CPU may change the min/max values on other CPUS.
 */

int update_sysfs(int cpu)
{
	if (!has_hwp)
		return 0;

	if (!hwp_update_enabled())
		return 0;

	if (access("/sys/devices/system/cpu/cpu0/cpufreq", F_OK))
		return 0;

	if (update_hwp_min)
		update_cpufreq_scaling_freq(0, cpu, req_update.hwp_min);

	if (update_hwp_max)
		update_cpufreq_scaling_freq(1, cpu, req_update.hwp_max);

	return 0;
}

int verify_hwp_req_self_consistency(int cpu, struct msr_hwp_request *req)
{
	/* fail if min > max requested */
	if (req->hwp_min > req->hwp_max) {
		errx(1, "cpu%d: requested hwp-min %d > hwp_max %d",
			cpu, req->hwp_min, req->hwp_max);
	}

	/* fail if desired > max requestd */
	if (req->hwp_desired && (req->hwp_desired > req->hwp_max)) {
		errx(1, "cpu%d: requested hwp-desired %d > hwp_max %d",
			cpu, req->hwp_desired, req->hwp_max);
	}
	/* fail if desired < min requestd */
	if (req->hwp_desired && (req->hwp_desired < req->hwp_min)) {
		errx(1, "cpu%d: requested hwp-desired %d < requested hwp_min %d",
			cpu, req->hwp_desired, req->hwp_min);
	}

	return 0;
}

int check_hwp_request_v_hwp_capabilities(int cpu, struct msr_hwp_request *req, struct msr_hwp_cap *cap)
{
	if (update_hwp_max) {
		if (req->hwp_max > cap->highest)
			errx(1, "cpu%d: requested max %d > capabilities highest %d, use --force?",
				cpu, req->hwp_max, cap->highest);
		if (req->hwp_max < cap->lowest)
			errx(1, "cpu%d: requested max %d < capabilities lowest %d, use --force?",
				cpu, req->hwp_max, cap->lowest);
	}

	if (update_hwp_min) {
		if (req->hwp_min > cap->highest)
			errx(1, "cpu%d: requested min %d > capabilities highest %d, use --force?",
				cpu, req->hwp_min, cap->highest);
		if (req->hwp_min < cap->lowest)
			errx(1, "cpu%d: requested min %d < capabilities lowest %d, use --force?",
				cpu, req->hwp_min, cap->lowest);
	}

	if (update_hwp_min && update_hwp_max && (req->hwp_min > req->hwp_max))
		errx(1, "cpu%d: requested min %d > requested max %d",
			cpu, req->hwp_min, req->hwp_max);

	if (update_hwp_desired && req->hwp_desired) {
		if (req->hwp_desired > req->hwp_max)
			errx(1, "cpu%d: requested desired %d > requested max %d, use --force?",
				cpu, req->hwp_desired, req->hwp_max);
		if (req->hwp_desired < req->hwp_min)
			errx(1, "cpu%d: requested desired %d < requested min %d, use --force?",
				cpu, req->hwp_desired, req->hwp_min);
		if (req->hwp_desired < cap->lowest)
			errx(1, "cpu%d: requested desired %d < capabilities lowest %d, use --force?",
				cpu, req->hwp_desired, cap->lowest);
		if (req->hwp_desired > cap->highest)
			errx(1, "cpu%d: requested desired %d > capabilities highest %d, use --force?",
				cpu, req->hwp_desired, cap->highest);
	}

	return 0;
}

int update_hwp_request(int cpu)
{
	struct msr_hwp_request req;
	struct msr_hwp_cap cap;

	int msr_offset = MSR_HWP_REQUEST;

	read_hwp_request(cpu, &req, msr_offset);
	if (debug)
		print_hwp_request(cpu, &req, "old: ");

	if (update_hwp_min)
		req.hwp_min = req_update.hwp_min;

	if (update_hwp_max)
		req.hwp_max = req_update.hwp_max;

	if (update_hwp_desired)
		req.hwp_desired = req_update.hwp_desired;

	if (update_hwp_window)
		req.hwp_window = req_update.hwp_window;

	if (update_hwp_epp)
		req.hwp_epp = req_update.hwp_epp;

	req.hwp_use_pkg = req_update.hwp_use_pkg;

	read_hwp_cap(cpu, &cap, MSR_HWP_CAPABILITIES);
	if (debug)
		print_hwp_cap(cpu, &cap, "");

	if (!force)
		check_hwp_request_v_hwp_capabilities(cpu, &req, &cap);

	verify_hwp_req_self_consistency(cpu, &req);

	write_hwp_request(cpu, &req, msr_offset);

	if (debug) {
		read_hwp_request(cpu, &req, msr_offset);
		print_hwp_request(cpu, &req, "new: ");
	}
	return 0;
}
int update_hwp_request_pkg(int pkg)
{
	struct msr_hwp_request req;
	struct msr_hwp_cap cap;
	int cpu = first_cpu_in_pkg[pkg];

	int msr_offset = MSR_HWP_REQUEST_PKG;

	read_hwp_request(cpu, &req, msr_offset);
	if (debug)
		print_hwp_request_pkg(pkg, &req, "old: ");

	if (update_hwp_min)
		req.hwp_min = req_update.hwp_min;

	if (update_hwp_max)
		req.hwp_max = req_update.hwp_max;

	if (update_hwp_desired)
		req.hwp_desired = req_update.hwp_desired;

	if (update_hwp_window)
		req.hwp_window = req_update.hwp_window;

	if (update_hwp_epp)
		req.hwp_epp = req_update.hwp_epp;

	read_hwp_cap(cpu, &cap, MSR_HWP_CAPABILITIES);
	if (debug)
		print_hwp_cap(cpu, &cap, "");

	if (!force)
		check_hwp_request_v_hwp_capabilities(cpu, &req, &cap);

	verify_hwp_req_self_consistency(cpu, &req);

	write_hwp_request(cpu, &req, msr_offset);

	if (debug) {
		read_hwp_request(cpu, &req, msr_offset);
		print_hwp_request_pkg(pkg, &req, "new: ");
	}
	return 0;
}

int enable_hwp_on_cpu(int cpu)
{
	unsigned long long msr;

	get_msr(cpu, MSR_PM_ENABLE, &msr);
	put_msr(cpu, MSR_PM_ENABLE, 1);

	if (verbose)
		printf("cpu%d: MSR_PM_ENABLE old: %d new: %d\n", cpu, (unsigned int) msr, 1);

	return 0;
}

int update_cpu_msrs(int cpu)
{
	unsigned long long msr;


	if (update_epb) {
		get_msr(cpu, MSR_IA32_ENERGY_PERF_BIAS, &msr);
		put_msr(cpu, MSR_IA32_ENERGY_PERF_BIAS, new_epb);

		if (verbose)
			printf("cpu%d: ENERGY_PERF_BIAS old: %d new: %d\n",
				cpu, (unsigned int) msr, (unsigned int) new_epb);
	}

	if (update_turbo) {
		int turbo_is_present_and_disabled;

		get_msr(cpu, MSR_IA32_MISC_ENABLE, &msr);

		turbo_is_present_and_disabled = ((msr & MSR_IA32_MISC_ENABLE_TURBO_DISABLE) != 0);

		if (turbo_update_value == 1)	{
			if (turbo_is_present_and_disabled) {
				msr &= ~MSR_IA32_MISC_ENABLE_TURBO_DISABLE;
				put_msr(cpu, MSR_IA32_MISC_ENABLE, msr);
				if (verbose)
					printf("cpu%d: turbo ENABLE\n", cpu);
			}
		} else {
			/*
			 * if "turbo_is_enabled" were known to be describe this cpu
			 * then we could use it here to skip redundant disable requests.
			 * but cpu may be in a different package, so we always write.
			 */
			msr |= MSR_IA32_MISC_ENABLE_TURBO_DISABLE;
			put_msr(cpu, MSR_IA32_MISC_ENABLE, msr);
			if (verbose)
				printf("cpu%d: turbo DISABLE\n", cpu);
		}
	}

	if (!has_hwp)
		return 0;

	if (!hwp_update_enabled())
		return 0;

	update_hwp_request(cpu);
	return 0;
}

/*
 * Open a file, and exit on failure
 */
FILE *fopen_or_die(const char *path, const char *mode)
{
	FILE *filep = fopen(path, "r");

	if (!filep)
		err(1, "%s: open failed", path);
	return filep;
}

unsigned int get_pkg_num(int cpu)
{
	FILE *fp;
	char pathname[128];
	unsigned int pkg;
	int retval;

	sprintf(pathname, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);

	fp = fopen_or_die(pathname, "r");
	retval = fscanf(fp, "%d\n", &pkg);
	if (retval != 1)
		errx(1, "%s: failed to parse", pathname);
	return pkg;
}

int set_max_cpu_pkg_num(int cpu)
{
	unsigned int pkg;

	if (max_cpu_num < cpu)
		max_cpu_num = cpu;

	pkg = get_pkg_num(cpu);

	if (pkg >= MAX_PACKAGES)
		errx(1, "cpu%d: %d >= MAX_PACKAGES (%d)", cpu, pkg, MAX_PACKAGES);

	if (pkg > max_pkg_num)
		max_pkg_num = pkg;

	if ((pkg_present_set & (1ULL << pkg)) == 0) {
		pkg_present_set |= (1ULL << pkg);
		first_cpu_in_pkg[pkg] = cpu;
	}

	return 0;
}
int mark_cpu_present(int cpu)
{
	CPU_SET_S(cpu, cpu_setsize, cpu_present_set);
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
			return retval;
		}
	}
	fclose(fp);
	return 0;
}

void for_all_cpus_in_set(size_t set_size, cpu_set_t *cpu_set, int (func)(int))
{
	int cpu_num;

	for (cpu_num = 0; cpu_num <= max_cpu_num; ++cpu_num)
		if (CPU_ISSET_S(cpu_num, set_size, cpu_set))
			func(cpu_num);
}

void init_data_structures(void)
{
	for_all_proc_cpus(set_max_cpu_pkg_num);

	cpu_setsize = CPU_ALLOC_SIZE((max_cpu_num + 1));

	cpu_present_set = CPU_ALLOC((max_cpu_num + 1));
	if (cpu_present_set == NULL)
		err(3, "CPU_ALLOC");
	CPU_ZERO_S(cpu_setsize, cpu_present_set);
	for_all_proc_cpus(mark_cpu_present);
}

/* clear has_hwp if it is not enable (or being enabled) */

void verify_hwp_is_enabled(void)
{
	unsigned long long msr;

	if (!has_hwp)	/* set in early_cpuid() */
		return;

	/* MSR_PM_ENABLE[1] == 1 if HWP is enabled and MSRs visible */
	get_msr(base_cpu, MSR_PM_ENABLE, &msr);
	if ((msr & 1) == 0) {
		fprintf(stderr, "HWP can be enabled using '--hwp-enable'\n");
		has_hwp = 0;
		return;
	}
}

int req_update_bounds_check(void)
{
	if (!hwp_update_enabled())
		return 0;

	/* fail if min > max requested */
	if ((update_hwp_max && update_hwp_min) &&
	    (req_update.hwp_min > req_update.hwp_max)) {
		printf("hwp-min %d > hwp_max %d\n", req_update.hwp_min, req_update.hwp_max);
		return -EINVAL;
	}

	/* fail if desired > max requestd */
	if (req_update.hwp_desired && update_hwp_max &&
	    (req_update.hwp_desired > req_update.hwp_max)) {
		printf("hwp-desired cannot be greater than hwp_max\n");
		return -EINVAL;
	}
	/* fail if desired < min requestd */
	if (req_update.hwp_desired && update_hwp_min &&
	    (req_update.hwp_desired < req_update.hwp_min)) {
		printf("hwp-desired cannot be less than hwp_min\n");
		return -EINVAL;
	}

	return 0;
}

void set_base_cpu(void)
{
	base_cpu = sched_getcpu();
	if (base_cpu < 0)
		err(-ENODEV, "No valid cpus found");
}


void probe_dev_msr(void)
{
	struct stat sb;
	char pathname[32];

	sprintf(pathname, "/dev/cpu/%d/msr", base_cpu);
	if (stat(pathname, &sb))
		if (system("/sbin/modprobe msr > /dev/null 2>&1"))
			err(-5, "no /dev/cpu/0/msr, Try \"# modprobe msr\" ");
}

static void get_cpuid_or_exit(unsigned int leaf,
			     unsigned int *eax, unsigned int *ebx,
			     unsigned int *ecx, unsigned int *edx)
{
	if (!__get_cpuid(leaf, eax, ebx, ecx, edx))
		errx(1, "Processor not supported\n");
}

/*
 * early_cpuid()
 * initialize turbo_is_enabled, has_hwp, has_epb
 * before cmdline is parsed
 */
void early_cpuid(void)
{
	unsigned int eax, ebx, ecx, edx;
	unsigned int fms, family, model;

	get_cpuid_or_exit(1, &fms, &ebx, &ecx, &edx);
	family = (fms >> 8) & 0xf;
	model = (fms >> 4) & 0xf;
	if (family == 6 || family == 0xf)
		model += ((fms >> 16) & 0xf) << 4;

	if (model == 0x4F) {
		unsigned long long msr;

		get_msr(base_cpu, MSR_TURBO_RATIO_LIMIT, &msr);

		bdx_highest_ratio = msr & 0xFF;
	}

	get_cpuid_or_exit(0x6, &eax, &ebx, &ecx, &edx);
	turbo_is_enabled = (eax >> 1) & 1;
	has_hwp = (eax >> 7) & 1;
	has_epb = (ecx >> 3) & 1;
}

/*
 * parse_cpuid()
 * set
 * has_hwp, has_hwp_notify, has_hwp_activity_window, has_hwp_epp, has_hwp_request_pkg, has_epb
 */
void parse_cpuid(void)
{
	unsigned int eax, ebx, ecx, edx, max_level;
	unsigned int fms, family, model, stepping;

	eax = ebx = ecx = edx = 0;

	get_cpuid_or_exit(0, &max_level, &ebx, &ecx, &edx);

	if (ebx == 0x756e6547 && edx == 0x49656e69 && ecx == 0x6c65746e)
		genuine_intel = 1;

	if (debug)
		fprintf(stderr, "CPUID(0): %.4s%.4s%.4s ",
			(char *)&ebx, (char *)&edx, (char *)&ecx);

	get_cpuid_or_exit(1, &fms, &ebx, &ecx, &edx);
	family = (fms >> 8) & 0xf;
	model = (fms >> 4) & 0xf;
	stepping = fms & 0xf;
	if (family == 6 || family == 0xf)
		model += ((fms >> 16) & 0xf) << 4;

	if (debug) {
		fprintf(stderr, "%d CPUID levels; family:model:stepping 0x%x:%x:%x (%d:%d:%d)\n",
			max_level, family, model, stepping, family, model, stepping);
		fprintf(stderr, "CPUID(1): %s %s %s %s %s %s %s %s\n",
			ecx & (1 << 0) ? "SSE3" : "-",
			ecx & (1 << 3) ? "MONITOR" : "-",
			ecx & (1 << 7) ? "EIST" : "-",
			ecx & (1 << 8) ? "TM2" : "-",
			edx & (1 << 4) ? "TSC" : "-",
			edx & (1 << 5) ? "MSR" : "-",
			edx & (1 << 22) ? "ACPI-TM" : "-",
			edx & (1 << 29) ? "TM" : "-");
	}

	if (!(edx & (1 << 5)))
		errx(1, "CPUID: no MSR");


	get_cpuid_or_exit(0x6, &eax, &ebx, &ecx, &edx);
	/* turbo_is_enabled already set */
	/* has_hwp already set */
	has_hwp_notify = eax & (1 << 8);
	has_hwp_activity_window = eax & (1 << 9);
	has_hwp_epp = eax & (1 << 10);
	has_hwp_request_pkg = eax & (1 << 11);

	if (!has_hwp_request_pkg && update_hwp_use_pkg)
		errx(1, "--hwp-use-pkg is not available on this hardware");

	/* has_epb already set */

	if (debug)
		fprintf(stderr,
			"CPUID(6): %sTURBO, %sHWP, %sHWPnotify, %sHWPwindow, %sHWPepp, %sHWPpkg, %sEPB\n",
			turbo_is_enabled ? "" : "No-",
			has_hwp ? "" : "No-",
			has_hwp_notify ? "" : "No-",
			has_hwp_activity_window ? "" : "No-",
			has_hwp_epp ? "" : "No-",
			has_hwp_request_pkg ? "" : "No-",
			has_epb ? "" : "No-");

	return;	/* success */
}

int main(int argc, char **argv)
{
	set_base_cpu();
	probe_dev_msr();
	init_data_structures();

	early_cpuid();	/* initial cpuid parse before cmdline */

	cmdline(argc, argv);

	if (debug)
		print_version();

	parse_cpuid();

	 /* If CPU-set and PKG-set are not initialized, default to all CPUs */
	if ((cpu_selected_set == 0) && (pkg_selected_set == 0))
		cpu_selected_set = cpu_present_set;

	/*
	 * If HWP is being enabled, do it now, so that subsequent operations
	 * that access HWP registers can work.
	 */
	if (update_hwp_enable)
		for_all_cpus_in_set(cpu_setsize, cpu_selected_set, enable_hwp_on_cpu);

	/* If HWP present, but disabled, warn and ignore from here forward */
	verify_hwp_is_enabled();

	if (req_update_bounds_check())
		return -EINVAL;

	/* display information only, no updates to settings */
	if (!update_epb && !update_turbo && !hwp_update_enabled()) {
		if (cpu_selected_set)
			for_all_cpus_in_set(cpu_setsize, cpu_selected_set, print_cpu_msrs);

		if (has_hwp_request_pkg) {
			if (pkg_selected_set == 0)
				pkg_selected_set = pkg_present_set;

			for_packages(pkg_selected_set, print_pkg_msrs);
		}

		return 0;
	}

	/* update CPU set */
	if (cpu_selected_set) {
		for_all_cpus_in_set(cpu_setsize, cpu_selected_set, update_sysfs);
		for_all_cpus_in_set(cpu_setsize, cpu_selected_set, update_cpu_msrs);
	} else if (pkg_selected_set)
		for_packages(pkg_selected_set, update_hwp_request_pkg);

	return 0;
}
