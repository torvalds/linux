/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018 Jesper Dangaard Brouer, Red Hat Inc.
 */
static const char *__doc__ =
	"Libbpf test program for loading BPF ELF object files";

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <bpf/libbpf.h>
#include <getopt.h>

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"debug",	no_argument,		NULL, 'D' },
	{"quiet",	no_argument,		NULL, 'q' },
	{0, 0, NULL,  0 }
};

static void usage(char *argv[])
{
	int i;

	printf("\nDOCUMENTATION:\n%s\n\n", __doc__);
	printf(" Usage: %s (options-see-below) BPF_FILE\n", argv[0]);
	printf(" Listing options:\n");
	for (i = 0; long_options[i].name != 0; i++) {
		printf(" --%-12s", long_options[i].name);
		printf(" short-option: -%c",
		       long_options[i].val);
		printf("\n");
	}
	printf("\n");
}

static bool debug = 0;
static int libbpf_debug_print(enum libbpf_print_level level,
			      const char *fmt, va_list args)
{
	if (level == LIBBPF_DEBUG && !debug)
		return 0;

	fprintf(stderr, "[%d] ", level);
	return vfprintf(stderr, fmt, args);
}

#define EXIT_FAIL_LIBBPF EXIT_FAILURE
#define EXIT_FAIL_OPTION 2

int test_walk_progs(struct bpf_object *obj, bool verbose)
{
	struct bpf_program *prog;
	int cnt = 0;

	bpf_object__for_each_program(prog, obj) {
		cnt++;
		if (verbose)
			printf("Prog (count:%d) section_name: %s\n", cnt,
			       bpf_program__title(prog, false));
	}
	return 0;
}

int test_walk_maps(struct bpf_object *obj, bool verbose)
{
	struct bpf_map *map;
	int cnt = 0;

	bpf_object__for_each_map(map, obj) {
		cnt++;
		if (verbose)
			printf("Map (count:%d) name: %s\n", cnt,
			       bpf_map__name(map));
	}
	return 0;
}

int test_open_file(char *filename, bool verbose)
{
	struct bpf_object *bpfobj = NULL;
	long err;

	if (verbose)
		printf("Open BPF ELF-file with libbpf: %s\n", filename);

	/* Load BPF ELF object file and check for errors */
	bpfobj = bpf_object__open(filename);
	err = libbpf_get_error(bpfobj);
	if (err) {
		char err_buf[128];
		libbpf_strerror(err, err_buf, sizeof(err_buf));
		if (verbose)
			printf("Unable to load eBPF objects in file '%s': %s\n",
			       filename, err_buf);
		return EXIT_FAIL_LIBBPF;
	}
	test_walk_progs(bpfobj, verbose);
	test_walk_maps(bpfobj, verbose);

	if (verbose)
		printf("Close BPF ELF-file with libbpf: %s\n",
		       bpf_object__name(bpfobj));
	bpf_object__close(bpfobj);

	return 0;
}

int main(int argc, char **argv)
{
	char filename[1024] = { 0 };
	bool verbose = 1;
	int longindex = 0;
	int opt;

	libbpf_set_print(libbpf_debug_print);

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "hDq",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'D':
			debug = 1;
			break;
		case 'q': /* Use in scripting mode */
			verbose = 0;
			break;
		case 'h':
		default:
			usage(argv);
			return EXIT_FAIL_OPTION;
		}
	}
	if (optind >= argc) {
		usage(argv);
		printf("ERROR: Expected BPF_FILE argument after options\n");
		return EXIT_FAIL_OPTION;
	}
	snprintf(filename, sizeof(filename), "%s", argv[optind]);

	return test_open_file(filename, verbose);
}
