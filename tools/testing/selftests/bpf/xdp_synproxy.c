// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <stdnoreturn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <linux/limits.h>

static unsigned int ifindex;
static __u32 attached_prog_id;
static bool attached_tc;

static void noreturn cleanup(int sig)
{
	LIBBPF_OPTS(bpf_xdp_attach_opts, opts);
	int prog_fd;
	int err;

	if (attached_prog_id == 0)
		exit(0);

	if (attached_tc) {
		LIBBPF_OPTS(bpf_tc_hook, hook,
			    .ifindex = ifindex,
			    .attach_point = BPF_TC_INGRESS);

		err = bpf_tc_hook_destroy(&hook);
		if (err < 0) {
			fprintf(stderr, "Error: bpf_tc_hook_destroy: %s\n", strerror(-err));
			fprintf(stderr, "Failed to destroy the TC hook\n");
			exit(1);
		}
		exit(0);
	}

	prog_fd = bpf_prog_get_fd_by_id(attached_prog_id);
	if (prog_fd < 0) {
		fprintf(stderr, "Error: bpf_prog_get_fd_by_id: %s\n", strerror(-prog_fd));
		err = bpf_xdp_attach(ifindex, -1, 0, NULL);
		if (err < 0) {
			fprintf(stderr, "Error: bpf_set_link_xdp_fd: %s\n", strerror(-err));
			fprintf(stderr, "Failed to detach XDP program\n");
			exit(1);
		}
	} else {
		opts.old_prog_fd = prog_fd;
		err = bpf_xdp_attach(ifindex, -1, XDP_FLAGS_REPLACE, &opts);
		close(prog_fd);
		if (err < 0) {
			fprintf(stderr, "Error: bpf_set_link_xdp_fd_opts: %s\n", strerror(-err));
			/* Not an error if already replaced by someone else. */
			if (err != -EEXIST) {
				fprintf(stderr, "Failed to detach XDP program\n");
				exit(1);
			}
		}
	}
	exit(0);
}

static noreturn void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [--iface <iface>|--prog <prog_id>] [--mss4 <mss ipv4> --mss6 <mss ipv6> --wscale <wscale> --ttl <ttl>] [--ports <port1>,<port2>,...] [--single] [--tc]\n",
		progname);
	exit(1);
}

static unsigned long parse_arg_ul(const char *progname, const char *arg, unsigned long limit)
{
	unsigned long res;
	char *endptr;

	errno = 0;
	res = strtoul(arg, &endptr, 10);
	if (errno != 0 || *endptr != '\0' || arg[0] == '\0' || res > limit)
		usage(progname);

	return res;
}

static void parse_options(int argc, char *argv[], unsigned int *ifindex, __u32 *prog_id,
			  __u64 *tcpipopts, char **ports, bool *single, bool *tc)
{
	static struct option long_options[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "iface", required_argument, NULL, 'i' },
		{ "prog", required_argument, NULL, 'x' },
		{ "mss4", required_argument, NULL, 4 },
		{ "mss6", required_argument, NULL, 6 },
		{ "wscale", required_argument, NULL, 'w' },
		{ "ttl", required_argument, NULL, 't' },
		{ "ports", required_argument, NULL, 'p' },
		{ "single", no_argument, NULL, 's' },
		{ "tc", no_argument, NULL, 'c' },
		{ NULL, 0, NULL, 0 },
	};
	unsigned long mss4, mss6, wscale, ttl;
	unsigned int tcpipopts_mask = 0;

	if (argc < 2)
		usage(argv[0]);

	*ifindex = 0;
	*prog_id = 0;
	*tcpipopts = 0;
	*ports = NULL;
	*single = false;

	while (true) {
		int opt;

		opt = getopt_long(argc, argv, "", long_options, NULL);
		if (opt == -1)
			break;

		switch (opt) {
		case 'h':
			usage(argv[0]);
			break;
		case 'i':
			*ifindex = if_nametoindex(optarg);
			if (*ifindex == 0)
				usage(argv[0]);
			break;
		case 'x':
			*prog_id = parse_arg_ul(argv[0], optarg, UINT32_MAX);
			if (*prog_id == 0)
				usage(argv[0]);
			break;
		case 4:
			mss4 = parse_arg_ul(argv[0], optarg, UINT16_MAX);
			tcpipopts_mask |= 1 << 0;
			break;
		case 6:
			mss6 = parse_arg_ul(argv[0], optarg, UINT16_MAX);
			tcpipopts_mask |= 1 << 1;
			break;
		case 'w':
			wscale = parse_arg_ul(argv[0], optarg, 14);
			tcpipopts_mask |= 1 << 2;
			break;
		case 't':
			ttl = parse_arg_ul(argv[0], optarg, UINT8_MAX);
			tcpipopts_mask |= 1 << 3;
			break;
		case 'p':
			*ports = optarg;
			break;
		case 's':
			*single = true;
			break;
		case 'c':
			*tc = true;
			break;
		default:
			usage(argv[0]);
		}
	}
	if (optind < argc)
		usage(argv[0]);

	if (tcpipopts_mask == 0xf) {
		if (mss4 == 0 || mss6 == 0 || wscale == 0 || ttl == 0)
			usage(argv[0]);
		*tcpipopts = (mss6 << 32) | (ttl << 24) | (wscale << 16) | mss4;
	} else if (tcpipopts_mask != 0) {
		usage(argv[0]);
	}

	if (*ifindex != 0 && *prog_id != 0)
		usage(argv[0]);
	if (*ifindex == 0 && *prog_id == 0)
		usage(argv[0]);
}

static int syncookie_attach(const char *argv0, unsigned int ifindex, bool tc)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	char xdp_filename[PATH_MAX];
	struct bpf_program *prog;
	struct bpf_object *obj;
	int prog_fd;
	int err;

	snprintf(xdp_filename, sizeof(xdp_filename), "%s_kern.bpf.o", argv0);
	obj = bpf_object__open_file(xdp_filename, NULL);
	err = libbpf_get_error(obj);
	if (err < 0) {
		fprintf(stderr, "Error: bpf_object__open_file: %s\n", strerror(-err));
		return err;
	}

	err = bpf_object__load(obj);
	if (err < 0) {
		fprintf(stderr, "Error: bpf_object__open_file: %s\n", strerror(-err));
		return err;
	}

	prog = bpf_object__find_program_by_name(obj, tc ? "syncookie_tc" : "syncookie_xdp");
	if (!prog) {
		fprintf(stderr, "Error: bpf_object__find_program_by_name: program was not found\n");
		return -ENOENT;
	}

	prog_fd = bpf_program__fd(prog);

	err = bpf_obj_get_info_by_fd(prog_fd, &info, &info_len);
	if (err < 0) {
		fprintf(stderr, "Error: bpf_obj_get_info_by_fd: %s\n", strerror(-err));
		goto out;
	}
	attached_tc = tc;
	attached_prog_id = info.id;
	signal(SIGINT, cleanup);
	signal(SIGTERM, cleanup);
	if (tc) {
		LIBBPF_OPTS(bpf_tc_hook, hook,
			    .ifindex = ifindex,
			    .attach_point = BPF_TC_INGRESS);
		LIBBPF_OPTS(bpf_tc_opts, opts,
			    .handle = 1,
			    .priority = 1,
			    .prog_fd = prog_fd);

		err = bpf_tc_hook_create(&hook);
		if (err < 0) {
			fprintf(stderr, "Error: bpf_tc_hook_create: %s\n",
				strerror(-err));
			goto fail;
		}
		err = bpf_tc_attach(&hook, &opts);
		if (err < 0) {
			fprintf(stderr, "Error: bpf_tc_attach: %s\n",
				strerror(-err));
			goto fail;
		}

	} else {
		err = bpf_xdp_attach(ifindex, prog_fd,
				     XDP_FLAGS_UPDATE_IF_NOEXIST, NULL);
		if (err < 0) {
			fprintf(stderr, "Error: bpf_set_link_xdp_fd: %s\n",
				strerror(-err));
			goto fail;
		}
	}
	err = 0;
out:
	bpf_object__close(obj);
	return err;
fail:
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	attached_prog_id = 0;
	goto out;
}

static int syncookie_open_bpf_maps(__u32 prog_id, int *values_map_fd, int *ports_map_fd)
{
	struct bpf_prog_info prog_info;
	__u32 map_ids[8];
	__u32 info_len;
	int prog_fd;
	int err;
	int i;

	*values_map_fd = -1;
	*ports_map_fd = -1;

	prog_fd = bpf_prog_get_fd_by_id(prog_id);
	if (prog_fd < 0) {
		fprintf(stderr, "Error: bpf_prog_get_fd_by_id: %s\n", strerror(-prog_fd));
		return prog_fd;
	}

	prog_info = (struct bpf_prog_info) {
		.nr_map_ids = 8,
		.map_ids = (__u64)map_ids,
	};
	info_len = sizeof(prog_info);

	err = bpf_obj_get_info_by_fd(prog_fd, &prog_info, &info_len);
	if (err != 0) {
		fprintf(stderr, "Error: bpf_obj_get_info_by_fd: %s\n", strerror(-err));
		goto out;
	}

	if (prog_info.nr_map_ids < 2) {
		fprintf(stderr, "Error: Found %u BPF maps, expected at least 2\n",
			prog_info.nr_map_ids);
		err = -ENOENT;
		goto out;
	}

	for (i = 0; i < prog_info.nr_map_ids; i++) {
		struct bpf_map_info map_info = {};
		int map_fd;

		err = bpf_map_get_fd_by_id(map_ids[i]);
		if (err < 0) {
			fprintf(stderr, "Error: bpf_map_get_fd_by_id: %s\n", strerror(-err));
			goto err_close_map_fds;
		}
		map_fd = err;

		info_len = sizeof(map_info);
		err = bpf_obj_get_info_by_fd(map_fd, &map_info, &info_len);
		if (err != 0) {
			fprintf(stderr, "Error: bpf_obj_get_info_by_fd: %s\n", strerror(-err));
			close(map_fd);
			goto err_close_map_fds;
		}
		if (strcmp(map_info.name, "values") == 0) {
			*values_map_fd = map_fd;
			continue;
		}
		if (strcmp(map_info.name, "allowed_ports") == 0) {
			*ports_map_fd = map_fd;
			continue;
		}
		close(map_fd);
	}

	if (*values_map_fd != -1 && *ports_map_fd != -1) {
		err = 0;
		goto out;
	}

	err = -ENOENT;

err_close_map_fds:
	if (*values_map_fd != -1)
		close(*values_map_fd);
	if (*ports_map_fd != -1)
		close(*ports_map_fd);
	*values_map_fd = -1;
	*ports_map_fd = -1;

out:
	close(prog_fd);
	return err;
}

int main(int argc, char *argv[])
{
	int values_map_fd, ports_map_fd;
	__u64 tcpipopts;
	bool firstiter;
	__u64 prevcnt;
	__u32 prog_id;
	char *ports;
	bool single;
	int err = 0;
	bool tc;

	parse_options(argc, argv, &ifindex, &prog_id, &tcpipopts, &ports,
		      &single, &tc);

	if (prog_id == 0) {
		if (!tc) {
			err = bpf_xdp_query_id(ifindex, 0, &prog_id);
			if (err < 0) {
				fprintf(stderr, "Error: bpf_get_link_xdp_id: %s\n",
					strerror(-err));
				goto out;
			}
		}
		if (prog_id == 0) {
			err = syncookie_attach(argv[0], ifindex, tc);
			if (err < 0)
				goto out;
			prog_id = attached_prog_id;
		}
	}

	err = syncookie_open_bpf_maps(prog_id, &values_map_fd, &ports_map_fd);
	if (err < 0)
		goto out;

	if (ports) {
		__u16 port_last = 0;
		__u32 port_idx = 0;
		char *p = ports;

		fprintf(stderr, "Replacing allowed ports\n");

		while (p && *p != '\0') {
			char *token = strsep(&p, ",");
			__u16 port;

			port = parse_arg_ul(argv[0], token, UINT16_MAX);
			err = bpf_map_update_elem(ports_map_fd, &port_idx, &port, BPF_ANY);
			if (err != 0) {
				fprintf(stderr, "Error: bpf_map_update_elem: %s\n", strerror(-err));
				fprintf(stderr, "Failed to add port %u (index %u)\n",
					port, port_idx);
				goto out_close_maps;
			}
			fprintf(stderr, "Added port %u\n", port);
			port_idx++;
		}
		err = bpf_map_update_elem(ports_map_fd, &port_idx, &port_last, BPF_ANY);
		if (err != 0) {
			fprintf(stderr, "Error: bpf_map_update_elem: %s\n", strerror(-err));
			fprintf(stderr, "Failed to add the terminator value 0 (index %u)\n",
				port_idx);
			goto out_close_maps;
		}
	}

	if (tcpipopts) {
		__u32 key = 0;

		fprintf(stderr, "Replacing TCP/IP options\n");

		err = bpf_map_update_elem(values_map_fd, &key, &tcpipopts, BPF_ANY);
		if (err != 0) {
			fprintf(stderr, "Error: bpf_map_update_elem: %s\n", strerror(-err));
			goto out_close_maps;
		}
	}

	if ((ports || tcpipopts) && attached_prog_id == 0 && !single)
		goto out_close_maps;

	prevcnt = 0;
	firstiter = true;
	while (true) {
		__u32 key = 1;
		__u64 value;

		err = bpf_map_lookup_elem(values_map_fd, &key, &value);
		if (err != 0) {
			fprintf(stderr, "Error: bpf_map_lookup_elem: %s\n", strerror(-err));
			goto out_close_maps;
		}
		if (firstiter) {
			prevcnt = value;
			firstiter = false;
		}
		if (single) {
			printf("Total SYNACKs generated: %llu\n", value);
			break;
		}
		printf("SYNACKs generated: %llu (total %llu)\n", value - prevcnt, value);
		prevcnt = value;
		sleep(1);
	}

out_close_maps:
	close(values_map_fd);
	close(ports_map_fd);
out:
	return err == 0 ? 0 : 1;
}
