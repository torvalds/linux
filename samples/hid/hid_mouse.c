// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022 Benjamin Tissoires
 *
 * This is a pure HID-BPF example, and should be considered as such:
 * on the Etekcity Scroll 6E, the X and Y axes will be swapped and
 * inverted. On any other device... Not sure what this will do.
 *
 * This C main file is generic though. To adapt the code and test, users
 * must amend only the .bpf.c file, which this program will load any
 * eBPF program it finds.
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

#include "hid_mouse.skel.h"

static bool running = true;

static void int_exit(int sig)
{
	running = false;
	exit(0);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"%s: %s /sys/bus/hid/devices/0BUS:0VID:0PID:00ID\n\n",
		__func__, prog);
	fprintf(stderr,
		"This program will upload and attach a HID-BPF program to the given device.\n"
		"On the Etekcity Scroll 6E, the X and Y axis will be inverted, but on any other\n"
		"device, chances are high that the device will not be working anymore\n\n"
		"consider this as a demo and adapt the eBPF program to your needs\n"
		"Hit Ctrl-C to unbind the program and reset the device\n");
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

int main(int argc, char **argv)
{
	struct hid_mouse *skel;
	struct bpf_link *link;
	int err;
	const char *optstr = "";
	const char *sysfs_path;
	int opt, hid_id;

	while ((opt = getopt(argc, argv, optstr)) != -1) {
		switch (opt) {
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

	skel = hid_mouse__open();
	if (!skel) {
		fprintf(stderr, "%s  %s:%d", __func__, __FILE__, __LINE__);
		return -1;
	}

	hid_id = get_hid_id(sysfs_path);

	if (hid_id < 0) {
		fprintf(stderr, "can not open HID device: %m\n");
		return 1;
	}
	skel->struct_ops.mouse_invert->hid_id = hid_id;

	err = hid_mouse__load(skel);
	if (err < 0) {
		fprintf(stderr, "can not load HID-BPF program: %m\n");
		return 1;
	}

	link = bpf_map__attach_struct_ops(skel->maps.mouse_invert);
	if (!link) {
		fprintf(stderr, "can not attach HID-BPF program: %m\n");
		return 1;
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	while (running)
		sleep(1);

	hid_mouse__destroy(skel);

	return 0;
}
