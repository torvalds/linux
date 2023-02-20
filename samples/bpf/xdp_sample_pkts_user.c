// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/perf_event.h>
#include <linux/bpf.h>
#include <net/if.h>
#include <errno.h>
#include <assert.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <libgen.h>
#include <linux/if_link.h>

#include "perf-sys.h"

static int if_idx;
static char *if_name;
static __u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;
static __u32 prog_id;
static struct perf_buffer *pb = NULL;

static int do_attach(int idx, int fd, const char *name)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	int err;

	err = bpf_xdp_attach(idx, fd, xdp_flags, NULL);
	if (err < 0) {
		printf("ERROR: failed to attach program to %s\n", name);
		return err;
	}

	err = bpf_prog_get_info_by_fd(fd, &info, &info_len);
	if (err) {
		printf("can't get prog info - %s\n", strerror(errno));
		return err;
	}
	prog_id = info.id;

	return err;
}

static int do_detach(int idx, const char *name)
{
	__u32 curr_prog_id = 0;
	int err = 0;

	err = bpf_xdp_query_id(idx, xdp_flags, &curr_prog_id);
	if (err) {
		printf("bpf_xdp_query_id failed\n");
		return err;
	}
	if (prog_id == curr_prog_id) {
		err = bpf_xdp_detach(idx, xdp_flags, NULL);
		if (err < 0)
			printf("ERROR: failed to detach prog from %s\n", name);
	} else if (!curr_prog_id) {
		printf("couldn't find a prog id on a %s\n", name);
	} else {
		printf("program on interface changed, not removing\n");
	}

	return err;
}

#define SAMPLE_SIZE 64

static void print_bpf_output(void *ctx, int cpu, void *data, __u32 size)
{
	struct {
		__u16 cookie;
		__u16 pkt_len;
		__u8  pkt_data[SAMPLE_SIZE];
	} __packed *e = data;
	int i;

	if (e->cookie != 0xdead) {
		printf("BUG cookie %x sized %d\n", e->cookie, size);
		return;
	}

	printf("Pkt len: %-5d bytes. Ethernet hdr: ", e->pkt_len);
	for (i = 0; i < 14 && i < e->pkt_len; i++)
		printf("%02x ", e->pkt_data[i]);
	printf("\n");
}

static void sig_handler(int signo)
{
	do_detach(if_idx, if_name);
	perf_buffer__free(pb);
	exit(0);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"%s: %s [OPTS] <ifname|ifindex>\n\n"
		"OPTS:\n"
		"    -F    force loading prog\n"
		"    -S    use skb-mode\n",
		__func__, prog);
}

int main(int argc, char **argv)
{
	const char *optstr = "FS";
	int prog_fd, map_fd, opt;
	struct bpf_program *prog;
	struct bpf_object *obj;
	struct bpf_map *map;
	char filename[256];
	int ret, err;

	while ((opt = getopt(argc, argv, optstr)) != -1) {
		switch (opt) {
		case 'F':
			xdp_flags &= ~XDP_FLAGS_UPDATE_IF_NOEXIST;
			break;
		case 'S':
			xdp_flags |= XDP_FLAGS_SKB_MODE;
			break;
		default:
			usage(basename(argv[0]));
			return 1;
		}
	}

	if (!(xdp_flags & XDP_FLAGS_SKB_MODE))
		xdp_flags |= XDP_FLAGS_DRV_MODE;

	if (optind == argc) {
		usage(basename(argv[0]));
		return 1;
	}

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	obj = bpf_object__open_file(filename, NULL);
	if (libbpf_get_error(obj))
		return 1;

	prog = bpf_object__next_program(obj, NULL);
	bpf_program__set_type(prog, BPF_PROG_TYPE_XDP);

	err = bpf_object__load(obj);
	if (err)
		return 1;

	prog_fd = bpf_program__fd(prog);

	map = bpf_object__next_map(obj, NULL);
	if (!map) {
		printf("finding a map in obj file failed\n");
		return 1;
	}
	map_fd = bpf_map__fd(map);

	if_idx = if_nametoindex(argv[optind]);
	if (!if_idx)
		if_idx = strtoul(argv[optind], NULL, 0);

	if (!if_idx) {
		fprintf(stderr, "Invalid ifname\n");
		return 1;
	}
	if_name = argv[optind];
	err = do_attach(if_idx, prog_fd, if_name);
	if (err)
		return err;

	if (signal(SIGINT, sig_handler) ||
	    signal(SIGHUP, sig_handler) ||
	    signal(SIGTERM, sig_handler)) {
		perror("signal");
		return 1;
	}

	pb = perf_buffer__new(map_fd, 8, print_bpf_output, NULL, NULL, NULL);
	err = libbpf_get_error(pb);
	if (err) {
		perror("perf_buffer setup failed");
		return 1;
	}

	while ((ret = perf_buffer__poll(pb, 1000)) >= 0) {
	}

	kill(0, SIGINT);
	return ret;
}
