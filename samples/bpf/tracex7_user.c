#define _GNU_SOURCE

#include <stdio.h>
#include <linux/bpf.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include "bpf_load.h"

int main(int argc, char **argv)
{
	FILE *f;
	char filename[256];
	char command[256];
	int ret;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	snprintf(command, 256, "mount %s tmpmnt/", argv[1]);
	f = popen(command, "r");
	ret = pclose(f);

	return ret ? 0 : 1;
}
