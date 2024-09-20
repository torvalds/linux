// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022 Benjamin Tissoires
 *
 * This program will morph the Microsoft Surface Dial into a mouse,
 * and depending on the chosen resolution enable or not the haptic feedback:
 * - a resolution (-r) of 3600 will report 3600 "ticks" in one full rotation
 *   without haptic feedback
 * - any other resolution will report N "ticks" in a full rotation with haptic
 *   feedback
 *
 * A good default for low resolution haptic scrolling is 72 (1 "tick" every 5
 * degrees), and set to 3600 for smooth scrolling.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include <linux/bpf.h>
#include <linux/errno.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "hid_surface_dial.skel.h"

static bool running = true;

struct haptic_syscall_args {
	unsigned int hid;
	int retval;
};

static void int_exit(int sig)
{
	running = false;
	exit(0);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"%s: %s [OPTIONS] /sys/bus/hid/devices/0BUS:0VID:0PID:00ID\n\n"
		"  OPTIONS:\n"
		"    -r N\t set the given resolution to the device (number of ticks per 360°)\n\n",
		__func__, prog);
	fprintf(stderr,
		"This program will morph the Microsoft Surface Dial into a mouse,\n"
		"and depending on the chosen resolution enable or not the haptic feedback:\n"
		"- a resolution (-r) of 3600 will report 3600 'ticks' in one full rotation\n"
		"  without haptic feedback\n"
		"- any other resolution will report N 'ticks' in a full rotation with haptic\n"
		"  feedback\n"
		"\n"
		"A good default for low resolution haptic scrolling is 72 (1 'tick' every 5\n"
		"degrees), and set to 3600 for smooth scrolling.\n");
}

static int get_hid_id(const char *path)
{
	const char *str_id, *dir;
	char uevent[1024];
	int fd;

	memset(uevent, 0, sizeof(uevent));
	snprintf(uevent, sizeof(uevent) - 1, "%s/uevent", path);

	fd = open(uevent, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return -ENOENT;

	close(fd);

	dir = basename((char *)path);

	str_id = dir + sizeof("0003:0001:0A37.");
	return (int)strtol(str_id, NULL, 16);
}

static int set_haptic(struct hid_surface_dial *skel, int hid_id)
{
	struct haptic_syscall_args args = {
		.hid = hid_id,
		.retval = -1,
	};
	int haptic_fd, err;
	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, tattr,
			    .ctx_in = &args,
			    .ctx_size_in = sizeof(args),
	);

	haptic_fd = bpf_program__fd(skel->progs.set_haptic);
	if (haptic_fd < 0) {
		fprintf(stderr, "can't locate haptic prog: %m\n");
		return 1;
	}

	err = bpf_prog_test_run_opts(haptic_fd, &tattr);
	if (err) {
		fprintf(stderr, "can't set haptic configuration to hid device %d: %m (err: %d)\n",
			hid_id, err);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct hid_surface_dial *skel;
	const char *optstr = "r:";
	struct bpf_link *link;
	const char *sysfs_path;
	int err, opt, hid_id, resolution = 72;

	while ((opt = getopt(argc, argv, optstr)) != -1) {
		switch (opt) {
		case 'r':
			{
				char *endp = NULL;
				long l = -1;

				if (optarg) {
					l = strtol(optarg, &endp, 10);
					if (endp && *endp)
						l = -1;
				}

				if (l < 0) {
					fprintf(stderr,
						"invalid r option %s - expecting a number\n",
						optarg ? optarg : "");
					exit(EXIT_FAILURE);
				};

				resolution = (int) l;
				break;
			}
		default:
			usage(basename(argv[0]));
			return 1;
		}
	}

	if (optind == argc) {
		usage(basename(argv[0]));
		return 1;
	}

	sysfs_path = argv[optind];
	if (!sysfs_path) {
		perror("sysfs");
		return 1;
	}

	skel = hid_surface_dial__open();
	if (!skel) {
		fprintf(stderr, "%s  %s:%d", __func__, __FILE__, __LINE__);
		return -1;
	}

	hid_id = get_hid_id(sysfs_path);
	if (hid_id < 0) {
		fprintf(stderr, "can not open HID device: %m\n");
		return 1;
	}

	skel->struct_ops.surface_dial->hid_id = hid_id;

	err = hid_surface_dial__load(skel);
	if (err < 0) {
		fprintf(stderr, "can not load HID-BPF program: %m\n");
		return 1;
	}

	skel->data->resolution = resolution;
	skel->data->physical = (int)(resolution / 72);

	link = bpf_map__attach_struct_ops(skel->maps.surface_dial);
	if (!link) {
		fprintf(stderr, "can not attach HID-BPF program: %m\n");
		return 1;
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	set_haptic(skel, hid_id);

	while (running)
		sleep(1);

	hid_surface_dial__destroy(skel);

	return 0;
}
