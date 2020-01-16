// SPDX-License-Identifier: GPL-2.0

#include <stdint.h>
#include "resctrl.h"

struct read_format {
	__u64 nr;			/* The number of events */
	struct {
		__u64 value;		/* The value of the event */
	} values[2];
};

char llc_occup_path[1024];

/*
 * Get LLC Occupancy as reported by RESCTRL FS
 * For CQM,
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
		perror("Failed to open results file");

		return errno;
	}
	if (fscanf(fp, "%lu", llc_occupancy) <= 0) {
		perror("Could not get llc occupancy");
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
 * Return:		0 on success. non-zero on failure.
 */
static int print_results_cache(char *filename, int bm_pid,
			       unsigned long llc_value)
{
	FILE *fp;

	if (strcmp(filename, "stdio") == 0 || strcmp(filename, "stderr") == 0) {
		printf("Pid: %d \t LLC_value: %lu\n", bm_pid,
		       llc_value);
	} else {
		fp = fopen(filename, "a");
		if (!fp) {
			perror("Cannot open results file");

			return errno;
		}
		fprintf(fp, "Pid: %d \t llc_value: %lu\n", bm_pid, llc_value);
		fclose(fp);
	}

	return 0;
}

int measure_cache_vals(struct resctrl_val_param *param, int bm_pid)
{
	unsigned long llc_occu_resc = 0, llc_value = 0;
	int ret;

	/*
	 * Measure llc occupancy from resctrl.
	 */
	if (!strcmp(param->resctrl_val, "cqm")) {
		ret = get_llc_occu_resctrl(&llc_occu_resc);
		if (ret < 0)
			return ret;
		llc_value = llc_occu_resc;
	}
	ret = print_results_cache(param->filename, bm_pid, llc_value);
	if (ret)
		return ret;

	return 0;
}
