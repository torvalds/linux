/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
#ifndef FLOW_DISSECTOR_LOAD
#define FLOW_DISSECTOR_LOAD

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

static inline int bpf_flow_load(struct bpf_object **obj,
				const char *path,
				const char *section_name,
				const char *map_name,
				int *prog_fd)
{
	struct bpf_program *prog, *main_prog;
	struct bpf_map *prog_array;
	int prog_array_fd;
	int ret, fd, i;

	ret = bpf_prog_load(path, BPF_PROG_TYPE_FLOW_DISSECTOR, obj,
			    prog_fd);
	if (ret)
		return ret;

	main_prog = bpf_object__find_program_by_title(*obj, section_name);
	if (!main_prog)
		return ret;

	*prog_fd = bpf_program__fd(main_prog);
	if (*prog_fd < 0)
		return ret;

	prog_array = bpf_object__find_map_by_name(*obj, map_name);
	if (!prog_array)
		return ret;

	prog_array_fd = bpf_map__fd(prog_array);
	if (prog_array_fd < 0)
		return ret;

	i = 0;
	bpf_object__for_each_program(prog, *obj) {
		fd = bpf_program__fd(prog);
		if (fd < 0)
			return fd;

		if (fd != *prog_fd) {
			bpf_map_update_elem(prog_array_fd, &i, &fd, BPF_ANY);
			++i;
		}
	}

	return 0;
}

#endif /* FLOW_DISSECTOR_LOAD */
