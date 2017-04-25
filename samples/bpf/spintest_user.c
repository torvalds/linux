#include <stdio.h>
#include <unistd.h>
#include <linux/bpf.h>
#include <string.h>
#include <assert.h>
#include <sys/resource.h>
#include "libbpf.h"
#include "bpf_load.h"

int main(int ac, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	long key, next_key, value;
	char filename[256];
	struct ksym *sym;
	int i;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	setrlimit(RLIMIT_MEMLOCK, &r);

	if (load_kallsyms()) {
		printf("failed to process /proc/kallsyms\n");
		return 2;
	}

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	for (i = 0; i < 5; i++) {
		key = 0;
		printf("kprobing funcs:");
		while (bpf_map_get_next_key(map_fd[0], &key, &next_key) == 0) {
			bpf_map_lookup_elem(map_fd[0], &next_key, &value);
			assert(next_key == value);
			sym = ksym_search(value);
			printf(" %s", sym->name);
			key = next_key;
		}
		if (key)
			printf("\n");
		key = 0;
		while (bpf_map_get_next_key(map_fd[0], &key, &next_key) == 0)
			bpf_map_delete_elem(map_fd[0], &next_key);
		sleep(1);
	}

	return 0;
}
