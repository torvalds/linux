#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <linux/bpf.h>
#include "libbpf.h"
#include "bpf_load.h"
#include "perf-sys.h"

#define SAMPLE_PERIOD  0x7fffffffffffffffULL

static void test_bpf_perf_event(void)
{
	int nr_cpus = sysconf(_SC_NPROCESSORS_CONF);
	int *pmu_fd = malloc(nr_cpus * sizeof(int));
	int status, i;

	struct perf_event_attr attr_insn_pmu = {
		.freq = 0,
		.sample_period = SAMPLE_PERIOD,
		.inherit = 0,
		.type = PERF_TYPE_HARDWARE,
		.read_format = 0,
		.sample_type = 0,
		.config = 0,/* PMU: cycles */
	};

	for (i = 0; i < nr_cpus; i++) {
		pmu_fd[i] = sys_perf_event_open(&attr_insn_pmu, -1/*pid*/, i/*cpu*/, -1/*group_fd*/, 0);
		if (pmu_fd[i] < 0) {
			printf("event syscall failed\n");
			goto exit;
		}

		bpf_map_update_elem(map_fd[0], &i, &pmu_fd[i], BPF_ANY);
		ioctl(pmu_fd[i], PERF_EVENT_IOC_ENABLE, 0);
	}

	status = system("ls > /dev/null");
	if (status)
		goto exit;
	status = system("sleep 2");
	if (status)
		goto exit;

exit:
	for (i = 0; i < nr_cpus; i++)
		close(pmu_fd[i]);
	close(map_fd[0]);
	free(pmu_fd);
}

int main(int argc, char **argv)
{
	char filename[256];

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	test_bpf_perf_event();
	read_trace_pipe();

	return 0;
}
