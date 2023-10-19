// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "trace_helpers.h"

int main(int ac, char **argv)
{
	struct bpf_link *link = NULL;
	struct bpf_program *prog;
	struct bpf_object *obj;
	char filename[256];
	FILE *f;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	obj = bpf_object__open_file(filename, NULL);
	if (libbpf_get_error(obj)) {
		fprintf(stderr, "ERROR: opening BPF object file failed\n");
		return 0;
	}

	prog = bpf_object__find_program_by_name(obj, "bpf_prog1");
	if (!prog) {
		fprintf(stderr, "ERROR: finding a prog in obj file failed\n");
		goto cleanup;
	}

	/* load BPF program */
	if (bpf_object__load(obj)) {
		fprintf(stderr, "ERROR: loading BPF object file failed\n");
		goto cleanup;
	}

	link = bpf_program__attach(prog);
	if (libbpf_get_error(link)) {
		fprintf(stderr, "ERROR: bpf_program__attach failed\n");
		link = NULL;
		goto cleanup;
	}

	f = popen("taskset 1 ping -c5 localhost", "r");
	(void) f;

	read_trace_pipe();

cleanup:
	bpf_link__destroy(link);
	bpf_object__close(obj);
	return 0;
}
