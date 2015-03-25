#include <stdio.h>
#include <linux/bpf.h>
#include <unistd.h>
#include "libbpf.h"
#include "bpf_load.h"

int main(int ac, char **argv)
{
	FILE *f;
	char filename[256];

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	f = popen("taskset 1 ping -c5 localhost", "r");
	(void) f;

	read_trace_pipe();

	return 0;
}
