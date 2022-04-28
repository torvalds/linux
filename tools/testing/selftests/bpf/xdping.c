// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved. */

#include <linux/bpf.h>
#include <linux/if_link.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "bpf/bpf.h"
#include "bpf/libbpf.h"

#include "xdping.h"
#include "testing_helpers.h"

static int ifindex;
static __u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;

static void cleanup(int sig)
{
	bpf_xdp_detach(ifindex, xdp_flags, NULL);
	if (sig)
		exit(1);
}

static int get_stats(int fd, __u16 count, __u32 raddr)
{
	struct pinginfo pinginfo = { 0 };
	char inaddrbuf[INET_ADDRSTRLEN];
	struct in_addr inaddr;
	__u16 i;

	inaddr.s_addr = raddr;

	printf("\nXDP RTT data:\n");

	if (bpf_map_lookup_elem(fd, &raddr, &pinginfo)) {
		perror("bpf_map_lookup elem");
		return 1;
	}

	for (i = 0; i < count; i++) {
		if (pinginfo.times[i] == 0)
			break;

		printf("64 bytes from %s: icmp_seq=%d ttl=64 time=%#.5f ms\n",
		       inet_ntop(AF_INET, &inaddr, inaddrbuf,
				 sizeof(inaddrbuf)),
		       count + i + 1,
		       (double)pinginfo.times[i]/1000000);
	}

	if (i < count) {
		fprintf(stderr, "Expected %d samples, got %d.\n", count, i);
		return 1;
	}

	bpf_map_delete_elem(fd, &raddr);

	return 0;
}

static void show_usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [OPTS] -I interface destination\n\n"
		"OPTS:\n"
		"    -c count		Stop after sending count requests\n"
		"			(default %d, max %d)\n"
		"    -I interface	interface name\n"
		"    -N			Run in driver mode\n"
		"    -s			Server mode\n"
		"    -S			Run in skb mode\n",
		prog, XDPING_DEFAULT_COUNT, XDPING_MAX_COUNT);
}

int main(int argc, char **argv)
{
	__u32 mode_flags = XDP_FLAGS_DRV_MODE | XDP_FLAGS_SKB_MODE;
	struct addrinfo *a, hints = { .ai_family = AF_INET };
	__u16 count = XDPING_DEFAULT_COUNT;
	struct pinginfo pinginfo = { 0 };
	const char *optstr = "c:I:NsS";
	struct bpf_program *main_prog;
	int prog_fd = -1, map_fd = -1;
	struct sockaddr_in rin;
	struct bpf_object *obj;
	struct bpf_map *map;
	char *ifname = NULL;
	char filename[256];
	int opt, ret = 1;
	__u32 raddr = 0;
	int server = 0;
	char cmd[256];

	while ((opt = getopt(argc, argv, optstr)) != -1) {
		switch (opt) {
		case 'c':
			count = atoi(optarg);
			if (count < 1 || count > XDPING_MAX_COUNT) {
				fprintf(stderr,
					"min count is 1, max count is %d\n",
					XDPING_MAX_COUNT);
				return 1;
			}
			break;
		case 'I':
			ifname = optarg;
			ifindex = if_nametoindex(ifname);
			if (!ifindex) {
				fprintf(stderr, "Could not get interface %s\n",
					ifname);
				return 1;
			}
			break;
		case 'N':
			xdp_flags |= XDP_FLAGS_DRV_MODE;
			break;
		case 's':
			/* use server program */
			server = 1;
			break;
		case 'S':
			xdp_flags |= XDP_FLAGS_SKB_MODE;
			break;
		default:
			show_usage(basename(argv[0]));
			return 1;
		}
	}

	if (!ifname) {
		show_usage(basename(argv[0]));
		return 1;
	}
	if (!server && optind == argc) {
		show_usage(basename(argv[0]));
		return 1;
	}

	if ((xdp_flags & mode_flags) == mode_flags) {
		fprintf(stderr, "-N or -S can be specified, not both.\n");
		show_usage(basename(argv[0]));
		return 1;
	}

	if (!server) {
		/* Only supports IPv4; see hints initiailization above. */
		if (getaddrinfo(argv[optind], NULL, &hints, &a) || !a) {
			fprintf(stderr, "Could not resolve %s\n", argv[optind]);
			return 1;
		}
		memcpy(&rin, a->ai_addr, sizeof(rin));
		raddr = rin.sin_addr.s_addr;
		freeaddrinfo(a);
	}

	/* Use libbpf 1.0 API mode */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	if (bpf_prog_test_load(filename, BPF_PROG_TYPE_XDP, &obj, &prog_fd)) {
		fprintf(stderr, "load of %s failed\n", filename);
		return 1;
	}

	main_prog = bpf_object__find_program_by_name(obj,
						     server ? "xdping_server" : "xdping_client");
	if (main_prog)
		prog_fd = bpf_program__fd(main_prog);
	if (!main_prog || prog_fd < 0) {
		fprintf(stderr, "could not find xdping program");
		return 1;
	}

	map = bpf_object__next_map(obj, NULL);
	if (map)
		map_fd = bpf_map__fd(map);
	if (!map || map_fd < 0) {
		fprintf(stderr, "Could not find ping map");
		goto done;
	}

	signal(SIGINT, cleanup);
	signal(SIGTERM, cleanup);

	printf("Setting up XDP for %s, please wait...\n", ifname);

	printf("XDP setup disrupts network connectivity, hit Ctrl+C to quit\n");

	if (bpf_xdp_attach(ifindex, prog_fd, xdp_flags, NULL) < 0) {
		fprintf(stderr, "Link set xdp fd failed for %s\n", ifname);
		goto done;
	}

	if (server) {
		close(prog_fd);
		close(map_fd);
		printf("Running server on %s; press Ctrl+C to exit...\n",
		       ifname);
		do { } while (1);
	}

	/* Start xdping-ing from last regular ping reply, e.g. for a count
	 * of 10 ICMP requests, we start xdping-ing using reply with seq number
	 * 10.  The reason the last "real" ping RTT is much higher is that
	 * the ping program sees the ICMP reply associated with the last
	 * XDP-generated packet, so ping doesn't get a reply until XDP is done.
	 */
	pinginfo.seq = htons(count);
	pinginfo.count = count;

	if (bpf_map_update_elem(map_fd, &raddr, &pinginfo, BPF_ANY)) {
		fprintf(stderr, "could not communicate with BPF map: %s\n",
			strerror(errno));
		cleanup(0);
		goto done;
	}

	/* We need to wait for XDP setup to complete. */
	sleep(10);

	snprintf(cmd, sizeof(cmd), "ping -c %d -I %s %s",
		 count, ifname, argv[optind]);

	printf("\nNormal ping RTT data\n");
	printf("[Ignore final RTT; it is distorted by XDP using the reply]\n");

	ret = system(cmd);

	if (!ret)
		ret = get_stats(map_fd, count, raddr);

	cleanup(0);

done:
	if (prog_fd > 0)
		close(prog_fd);
	if (map_fd > 0)
		close(map_fd);

	return ret;
}
