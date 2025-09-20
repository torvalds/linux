// SPDX-License-Identifier: GPL-2.0

#include <stdint.h>
#include "resctrl.h"

char llc_occup_path[1024];

void perf_event_attr_initialize(struct perf_event_attr *pea, __u64 config)
{
	memset(pea, 0, sizeof(*pea));
	pea->type = PERF_TYPE_HARDWARE;
	pea->size = sizeof(*pea);
	pea->read_format = PERF_FORMAT_GROUP;
	pea->exclude_kernel = 1;
	pea->exclude_hv = 1;
	pea->exclude_idle = 1;
	pea->exclude_callchain_kernel = 1;
	pea->inherit = 1;
	pea->exclude_guest = 1;
	pea->disabled = 1;
	pea->config = config;
}

/* Start counters to log values */
int perf_event_reset_enable(int pe_fd)
{
	int ret;

	ret = ioctl(pe_fd, PERF_EVENT_IOC_RESET, 0);
	if (ret < 0)
		return ret;

	ret = ioctl(pe_fd, PERF_EVENT_IOC_ENABLE, 0);
	if (ret < 0)
		return ret;

	return 0;
}

void perf_event_initialize_read_format(struct perf_event_read *pe_read)
{
	memset(pe_read, 0, sizeof(*pe_read));
	pe_read->nr = 1;
}

int perf_open(struct perf_event_attr *pea, pid_t pid, int cpu_no)
{
	int pe_fd;

	pe_fd = perf_event_open(pea, pid, cpu_no, -1, PERF_FLAG_FD_CLOEXEC);
	if (pe_fd == -1) {
		ksft_perror("Error opening leader");
		return -1;
	}

	perf_event_reset_enable(pe_fd);

	return pe_fd;
}

/*
 * Get LLC Occupancy as reported by RESCTRL FS
 * For CMT,
 * 1. If con_mon grp and mon grp given, then read from mon grp in
 * con_mon grp
 * 2. If only con_mon grp given, then read from con_mon grp
 * 3. If both not given, then read from root con_mon grp
 * For CAT,
 * 1. If con_mon grp given, then read from it
 * 2. If con_mon grp not given, then read from root con_mon grp
 *
 * Return: =0 on success.  <0 on failure.
 */
static int get_llc_occu_resctrl(unsigned long *llc_occupancy)
{
	FILE *fp;

	fp = fopen(llc_occup_path, "r");
	if (!fp) {
		ksft_perror("Failed to open results file");

		return -1;
	}
	if (fscanf(fp, "%lu", llc_occupancy) <= 0) {
		ksft_perror("Could not get llc occupancy");
		fclose(fp);

		return -1;
	}
	fclose(fp);

	return 0;
}

/*
 * print_results_cache:	the cache results are stored in a file
 * @filename:		file that stores the results
 * @bm_pid:		child pid that runs benchmark
 * @llc_value:		perf miss value /
 *			llc occupancy value reported by resctrl FS
 *
 * Return:		0 on success, < 0 on error.
 */
static int print_results_cache(const char *filename, pid_t bm_pid, __u64 llc_value)
{
	FILE *fp;

	if (strcmp(filename, "stdio") == 0 || strcmp(filename, "stderr") == 0) {
		printf("Pid: %d \t LLC_value: %llu\n", (int)bm_pid, llc_value);
	} else {
		fp = fopen(filename, "a");
		if (!fp) {
			ksft_perror("Cannot open results file");

			return -1;
		}
		fprintf(fp, "Pid: %d \t llc_value: %llu\n", (int)bm_pid, llc_value);
		fclose(fp);
	}

	return 0;
}

/*
 * perf_event_measure - Measure perf events
 * @filename:	Filename for writing the results
 * @bm_pid:	PID that runs the benchmark
 *
 * Measures perf events (e.g., cache misses) and writes the results into
 * @filename. @bm_pid is written to the results file along with the measured
 * value.
 *
 * Return: =0 on success. <0 on failure.
 */
int perf_event_measure(int pe_fd, struct perf_event_read *pe_read,
		       const char *filename, pid_t bm_pid)
{
	int ret;

	/* Stop counters after one span to get miss rate */
	ret = ioctl(pe_fd, PERF_EVENT_IOC_DISABLE, 0);
	if (ret < 0)
		return ret;

	ret = read(pe_fd, pe_read, sizeof(*pe_read));
	if (ret == -1) {
		ksft_perror("Could not get perf value");
		return -1;
	}

	return print_results_cache(filename, bm_pid, pe_read->values[0].value);
}

/*
 * measure_llc_resctrl - Measure resctrl LLC value from resctrl
 * @filename:	Filename for writing the results
 * @bm_pid:	PID that runs the benchmark
 *
 * Measures LLC occupancy from resctrl and writes the results into @filename.
 * @bm_pid is written to the results file along with the measured value.
 *
 * Return: =0 on success. <0 on failure.
 */
int measure_llc_resctrl(const char *filename, pid_t bm_pid)
{
	unsigned long llc_occu_resc = 0;
	int ret;

	ret = get_llc_occu_resctrl(&llc_occu_resc);
	if (ret < 0)
		return ret;

	return print_results_cache(filename, bm_pid, llc_occu_resc);
}

/*
 * show_cache_info - Show generic cache test information
 * @no_of_bits:		Number of bits
 * @avg_llc_val:	Average of LLC cache result data
 * @cache_span:		Cache span
 * @lines:		@cache_span in lines or bytes
 */
void show_cache_info(int no_of_bits, __u64 avg_llc_val, size_t cache_span, bool lines)
{
	ksft_print_msg("Number of bits: %d\n", no_of_bits);
	ksft_print_msg("Average LLC val: %llu\n", avg_llc_val);
	ksft_print_msg("Cache span (%s): %zu\n", lines ? "lines" : "bytes",
		       cache_span);
}
