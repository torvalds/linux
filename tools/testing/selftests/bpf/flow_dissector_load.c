// SPDX-License-Identifier: GPL-2.0
#include <error.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_rlimit.h"

const char *cfg_pin_path = "/sys/fs/bpf/flow_dissector";
const char *cfg_map_name = "jmp_table";
bool cfg_attach = true;
char *cfg_section_name;
char *cfg_path_name;

static void load_and_attach_program(void)
{
	struct bpf_program *prog, *main_prog;
	struct bpf_map *prog_array;
	int i, fd, prog_fd, ret;
	struct bpf_object *obj;
	int prog_array_fd;

	ret = bpf_prog_load(cfg_path_name, BPF_PROG_TYPE_FLOW_DISSECTOR, &obj,
			    &prog_fd);
	if (ret)
		error(1, 0, "bpf_prog_load %s", cfg_path_name);

	main_prog = bpf_object__find_program_by_title(obj, cfg_section_name);
	if (!main_prog)
		error(1, 0, "bpf_object__find_program_by_title %s",
		      cfg_section_name);

	prog_fd = bpf_program__fd(main_prog);
	if (prog_fd < 0)
		error(1, 0, "bpf_program__fd");

	prog_array = bpf_object__find_map_by_name(obj, cfg_map_name);
	if (!prog_array)
		error(1, 0, "bpf_object__find_map_by_name %s", cfg_map_name);

	prog_array_fd = bpf_map__fd(prog_array);
	if (prog_array_fd < 0)
		error(1, 0, "bpf_map__fd %s", cfg_map_name);

	i = 0;
	bpf_object__for_each_program(prog, obj) {
		fd = bpf_program__fd(prog);
		if (fd < 0)
			error(1, 0, "bpf_program__fd");

		if (fd != prog_fd) {
			printf("%d: %s\n", i, bpf_program__title(prog, false));
			bpf_map_update_elem(prog_array_fd, &i, &fd, BPF_ANY);
			++i;
		}
	}

	ret = bpf_prog_attach(prog_fd, 0 /* Ignore */, BPF_FLOW_DISSECTOR, 0);
	if (ret)
		error(1, 0, "bpf_prog_attach %s", cfg_path_name);

	ret = bpf_object__pin(obj, cfg_pin_path);
	if (ret)
		error(1, 0, "bpf_object__pin %s", cfg_pin_path);

}

static void detach_program(void)
{
	char command[64];
	int ret;

	ret = bpf_prog_detach(0, BPF_FLOW_DISSECTOR);
	if (ret)
		error(1, 0, "bpf_prog_detach");

	/* To unpin, it is necessary and sufficient to just remove this dir */
	sprintf(command, "rm -r %s", cfg_pin_path);
	ret = system(command);
	if (ret)
		error(1, errno, command);
}

static void parse_opts(int argc, char **argv)
{
	bool attach = false;
	bool detach = false;
	int c;

	while ((c = getopt(argc, argv, "adp:s:")) != -1) {
		switch (c) {
		case 'a':
			if (detach)
				error(1, 0, "attach/detach are exclusive");
			attach = true;
			break;
		case 'd':
			if (attach)
				error(1, 0, "attach/detach are exclusive");
			detach = true;
			break;
		case 'p':
			if (cfg_path_name)
				error(1, 0, "only one prog name can be given");

			cfg_path_name = optarg;
			break;
		case 's':
			if (cfg_section_name)
				error(1, 0, "only one section can be given");

			cfg_section_name = optarg;
			break;
		}
	}

	if (detach)
		cfg_attach = false;

	if (cfg_attach && !cfg_path_name)
		error(1, 0, "must provide a path to the BPF program");

	if (cfg_attach && !cfg_section_name)
		error(1, 0, "must provide a section name");
}

int main(int argc, char **argv)
{
	parse_opts(argc, argv);
	if (cfg_attach)
		load_and_attach_program();
	else
		detach_program();
	return 0;
}
