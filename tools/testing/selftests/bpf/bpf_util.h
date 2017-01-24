#ifndef __BPF_UTIL__
#define __BPF_UTIL__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static inline unsigned int bpf_num_possible_cpus(void)
{
	static const char *fcpu = "/sys/devices/system/cpu/possible";
	unsigned int start, end, possible_cpus = 0;
	char buff[128];
	FILE *fp;

	fp = fopen(fcpu, "r");
	if (!fp) {
		printf("Failed to open %s: '%s'!\n", fcpu, strerror(errno));
		exit(1);
	}

	while (fgets(buff, sizeof(buff), fp)) {
		if (sscanf(buff, "%u-%u", &start, &end) == 2) {
			possible_cpus = start == 0 ? end + 1 : 0;
			break;
		}
	}

	fclose(fp);
	if (!possible_cpus) {
		printf("Failed to retrieve # possible CPUs!\n");
		exit(1);
	}

	return possible_cpus;
}

#endif /* __BPF_UTIL__ */
