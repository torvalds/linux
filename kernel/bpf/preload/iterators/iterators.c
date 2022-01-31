// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <sys/mount.h>
#include "iterators.skel.h"
#include "bpf_preload_common.h"

int to_kernel = -1;
int from_kernel = 0;

static int send_link_to_kernel(struct bpf_link *link, const char *link_name)
{
	struct bpf_preload_info obj = {};
	struct bpf_link_info info = {};
	__u32 info_len = sizeof(info);
	int err;

	err = bpf_obj_get_info_by_fd(bpf_link__fd(link), &info, &info_len);
	if (err)
		return err;
	obj.link_id = info.id;
	if (strlen(link_name) >= sizeof(obj.link_name))
		return -E2BIG;
	strcpy(obj.link_name, link_name);
	if (write(to_kernel, &obj, sizeof(obj)) != sizeof(obj))
		return -EPIPE;
	return 0;
}

int main(int argc, char **argv)
{
	struct iterators_bpf *skel;
	int err, magic;
	int debug_fd;

	debug_fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC);
	if (debug_fd < 0)
		return 1;
	to_kernel = dup(1);
	close(1);
	dup(debug_fd);
	/* now stdin and stderr point to /dev/console */

	read(from_kernel, &magic, sizeof(magic));
	if (magic != BPF_PRELOAD_START) {
		printf("bad start magic %d\n", magic);
		return 1;
	}
	/* libbpf opens BPF object and loads it into the kernel */
	skel = iterators_bpf__open_and_load();
	if (!skel) {
		/* iterators.skel.h is little endian.
		 * libbpf doesn't support automatic little->big conversion
		 * of BPF bytecode yet.
		 * The program load will fail in such case.
		 */
		printf("Failed load could be due to wrong endianness\n");
		return 1;
	}
	err = iterators_bpf__attach(skel);
	if (err)
		goto cleanup;

	/* send two bpf_link IDs with names to the kernel */
	err = send_link_to_kernel(skel->links.dump_bpf_map, "maps.debug");
	if (err)
		goto cleanup;
	err = send_link_to_kernel(skel->links.dump_bpf_prog, "progs.debug");
	if (err)
		goto cleanup;

	/* The kernel will proceed with pinnging the links in bpffs.
	 * UMD will wait on read from pipe.
	 */
	read(from_kernel, &magic, sizeof(magic));
	if (magic != BPF_PRELOAD_END) {
		printf("bad final magic %d\n", magic);
		err = -EINVAL;
	}
cleanup:
	iterators_bpf__destroy(skel);

	return err != 0;
}
