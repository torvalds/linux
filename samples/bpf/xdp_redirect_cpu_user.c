// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
static const char *__doc__ =
"XDP CPU redirect tool, using BPF_MAP_TYPE_CPUMAP\n"
"Usage: xdp_redirect_cpu -d <IFINDEX|IFNAME> -c 0 ... -c N\n"
"Valid specification for CPUMAP BPF program:\n"
"  --mprog-name/-e pass (use built-in XDP_PASS program)\n"
"  --mprog-name/-e drop (use built-in XDP_DROP program)\n"
"  --redirect-device/-r <ifindex|ifname> (use built-in DEVMAP redirect program)\n"
"  Custom CPUMAP BPF program:\n"
"    --mprog-filename/-f <filename> --mprog-name/-e <program>\n"
"    Optionally, also pass --redirect-map/-m and --redirect-device/-r together\n"
"    to configure DEVMAP in BPF object <filename>\n";

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <getopt.h>
#include <net/if.h>
#include <time.h>
#include <linux/limits.h>
#include <arpa/inet.h>
#include <linux/if_link.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "bpf_util.h"
#include "xdp_sample_user.h"
#include "xdp_redirect_cpu.skel.h"

static int map_fd;
static int avail_fd;
static int count_fd;

static int mask = SAMPLE_RX_CNT | SAMPLE_REDIRECT_ERR_MAP_CNT |
		  SAMPLE_CPUMAP_ENQUEUE_CNT | SAMPLE_CPUMAP_KTHREAD_CNT |
		  SAMPLE_EXCEPTION_CNT;

DEFINE_SAMPLE_INIT(xdp_redirect_cpu);

static const struct option long_options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "dev", required_argument, NULL, 'd' },
	{ "skb-mode", no_argument, NULL, 'S' },
	{ "progname", required_argument, NULL, 'p' },
	{ "qsize", required_argument, NULL, 'q' },
	{ "cpu", required_argument, NULL, 'c' },
	{ "stress-mode", no_argument, NULL, 'x' },
	{ "force", no_argument, NULL, 'F' },
	{ "interval", required_argument, NULL, 'i' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "stats", no_argument, NULL, 's' },
	{ "mprog-name", required_argument, NULL, 'e' },
	{ "mprog-filename", required_argument, NULL, 'f' },
	{ "redirect-device", required_argument, NULL, 'r' },
	{ "redirect-map", required_argument, NULL, 'm' },
	{}
};

static void print_avail_progs(struct bpf_object *obj)
{
	struct bpf_program *pos;

	printf(" Programs to be used for -p/--progname:\n");
	bpf_object__for_each_program(pos, obj) {
		if (bpf_program__is_xdp(pos)) {
			if (!strncmp(bpf_program__name(pos), "xdp_prognum",
				     sizeof("xdp_prognum") - 1))
				printf(" %s\n", bpf_program__name(pos));
		}
	}
}

static void usage(char *argv[], const struct option *long_options,
		  const char *doc, int mask, bool error, struct bpf_object *obj)
{
	sample_usage(argv, long_options, doc, mask, error);
	print_avail_progs(obj);
}

static int create_cpu_entry(__u32 cpu, struct bpf_cpumap_val *value,
			    __u32 avail_idx, bool new)
{
	__u32 curr_cpus_count = 0;
	__u32 key = 0;
	int ret;

	/* Add a CPU entry to cpumap, as this allocate a cpu entry in
	 * the kernel for the cpu.
	 */
	ret = bpf_map_update_elem(map_fd, &cpu, value, 0);
	if (ret < 0) {
		fprintf(stderr, "Create CPU entry failed: %s\n", strerror(errno));
		return ret;
	}

	/* Inform bpf_prog's that a new CPU is available to select
	 * from via some control maps.
	 */
	ret = bpf_map_update_elem(avail_fd, &avail_idx, &cpu, 0);
	if (ret < 0) {
		fprintf(stderr, "Add to avail CPUs failed: %s\n", strerror(errno));
		return ret;
	}

	/* When not replacing/updating existing entry, bump the count */
	ret = bpf_map_lookup_elem(count_fd, &key, &curr_cpus_count);
	if (ret < 0) {
		fprintf(stderr, "Failed reading curr cpus_count: %s\n",
			strerror(errno));
		return ret;
	}
	if (new) {
		curr_cpus_count++;
		ret = bpf_map_update_elem(count_fd, &key,
					  &curr_cpus_count, 0);
		if (ret < 0) {
			fprintf(stderr, "Failed write curr cpus_count: %s\n",
				strerror(errno));
			return ret;
		}
	}

	printf("%s CPU: %u as idx: %u qsize: %d cpumap_prog_fd: %d (cpus_count: %u)\n",
	       new ? "Add new" : "Replace", cpu, avail_idx,
	       value->qsize, value->bpf_prog.fd, curr_cpus_count);

	return 0;
}

/* CPUs are zero-indexed. Thus, add a special sentinel default value
 * in map cpus_available to mark CPU index'es not configured
 */
static int mark_cpus_unavailable(void)
{
	int ret, i, n_cpus = libbpf_num_possible_cpus();
	__u32 invalid_cpu = n_cpus;

	for (i = 0; i < n_cpus; i++) {
		ret = bpf_map_update_elem(avail_fd, &i,
					  &invalid_cpu, 0);
		if (ret < 0) {
			fprintf(stderr, "Failed marking CPU unavailable: %s\n",
				strerror(errno));
			return ret;
		}
	}

	return 0;
}

/* Stress cpumap management code by concurrently changing underlying cpumap */
static void stress_cpumap(void *ctx)
{
	struct bpf_cpumap_val *value = ctx;

	/* Changing qsize will cause kernel to free and alloc a new
	 * bpf_cpu_map_entry, with an associated/complicated tear-down
	 * procedure.
	 */
	value->qsize = 1024;
	create_cpu_entry(1, value, 0, false);
	value->qsize = 8;
	create_cpu_entry(1, value, 0, false);
	value->qsize = 16000;
	create_cpu_entry(1, value, 0, false);
}

static int set_cpumap_prog(struct xdp_redirect_cpu *skel,
			   const char *redir_interface, const char *redir_map,
			   const char *mprog_filename, const char *mprog_name)
{
	if (mprog_filename) {
		struct bpf_program *prog;
		struct bpf_object *obj;
		int ret;

		if (!mprog_name) {
			fprintf(stderr, "BPF program not specified for file %s\n",
				mprog_filename);
			goto end;
		}
		if ((redir_interface && !redir_map) || (!redir_interface && redir_map)) {
			fprintf(stderr, "--redirect-%s specified but --redirect-%s not specified\n",
				redir_interface ? "device" : "map", redir_interface ? "map" : "device");
			goto end;
		}

		/* Custom BPF program */
		obj = bpf_object__open_file(mprog_filename, NULL);
		if (!obj) {
			ret = -errno;
			fprintf(stderr, "Failed to bpf_prog_load_xattr: %s\n",
				strerror(errno));
			return ret;
		}

		ret = bpf_object__load(obj);
		if (ret < 0) {
			ret = -errno;
			fprintf(stderr, "Failed to bpf_object__load: %s\n",
				strerror(errno));
			return ret;
		}

		if (redir_map) {
			int err, redir_map_fd, ifindex_out, key = 0;

			redir_map_fd = bpf_object__find_map_fd_by_name(obj, redir_map);
			if (redir_map_fd < 0) {
				fprintf(stderr, "Failed to bpf_object__find_map_fd_by_name: %s\n",
					strerror(errno));
				return redir_map_fd;
			}

			ifindex_out = if_nametoindex(redir_interface);
			if (!ifindex_out)
				ifindex_out = strtoul(redir_interface, NULL, 0);
			if (!ifindex_out) {
				fprintf(stderr, "Bad interface name or index\n");
				return -EINVAL;
			}

			err = bpf_map_update_elem(redir_map_fd, &key, &ifindex_out, 0);
			if (err < 0)
				return err;
		}

		prog = bpf_object__find_program_by_name(obj, mprog_name);
		if (!prog) {
			ret = -errno;
			fprintf(stderr, "Failed to bpf_object__find_program_by_name: %s\n",
				strerror(errno));
			return ret;
		}

		return bpf_program__fd(prog);
	} else {
		if (mprog_name) {
			if (redir_interface || redir_map) {
				fprintf(stderr, "Need to specify --mprog-filename/-f\n");
				goto end;
			}
			if (!strcmp(mprog_name, "pass") || !strcmp(mprog_name, "drop")) {
				/* Use built-in pass/drop programs */
				return *mprog_name == 'p' ? bpf_program__fd(skel->progs.xdp_redirect_cpu_pass)
					: bpf_program__fd(skel->progs.xdp_redirect_cpu_drop);
			} else {
				fprintf(stderr, "Unknown name \"%s\" for built-in BPF program\n",
					mprog_name);
				goto end;
			}
		} else {
			if (redir_map) {
				fprintf(stderr, "Need to specify --mprog-filename, --mprog-name and"
					" --redirect-device with --redirect-map\n");
				goto end;
			}
			if (redir_interface) {
				/* Use built-in devmap redirect */
				struct bpf_devmap_val val = {};
				int ifindex_out, err;
				__u32 key = 0;

				if (!redir_interface)
					return 0;

				ifindex_out = if_nametoindex(redir_interface);
				if (!ifindex_out)
					ifindex_out = strtoul(redir_interface, NULL, 0);
				if (!ifindex_out) {
					fprintf(stderr, "Bad interface name or index\n");
					return -EINVAL;
				}

				if (get_mac_addr(ifindex_out, skel->bss->tx_mac_addr) < 0) {
					printf("Get interface %d mac failed\n", ifindex_out);
					return -EINVAL;
				}

				val.ifindex = ifindex_out;
				val.bpf_prog.fd = bpf_program__fd(skel->progs.xdp_redirect_egress_prog);
				err = bpf_map_update_elem(bpf_map__fd(skel->maps.tx_port), &key, &val, 0);
				if (err < 0)
					return -errno;

				return bpf_program__fd(skel->progs.xdp_redirect_cpu_devmap);
			}
		}
	}

	/* Disabled */
	return 0;
end:
	fprintf(stderr, "Invalid options for CPUMAP BPF program\n");
	return -EINVAL;
}

int main(int argc, char **argv)
{
	const char *redir_interface = NULL, *redir_map = NULL;
	const char *mprog_filename = NULL, *mprog_name = NULL;
	struct xdp_redirect_cpu *skel;
	struct bpf_map_info info = {};
	char ifname_buf[IF_NAMESIZE];
	struct bpf_cpumap_val value;
	__u32 infosz = sizeof(info);
	int ret = EXIT_FAIL_OPTION;
	unsigned long interval = 2;
	bool stress_mode = false;
	struct bpf_program *prog;
	const char *prog_name;
	bool generic = false;
	bool force = false;
	int added_cpus = 0;
	bool error = true;
	int longindex = 0;
	int add_cpu = -1;
	int ifindex = -1;
	int *cpu, i, opt;
	char *ifname;
	__u32 qsize;
	int n_cpus;

	n_cpus = libbpf_num_possible_cpus();

	/* Notice: Choosing the queue size is very important when CPU is
	 * configured with power-saving states.
	 *
	 * If deepest state take 133 usec to wakeup from (133/10^6). When link
	 * speed is 10Gbit/s ((10*10^9/8) in bytes/sec). How many bytes can
	 * arrive with in 133 usec at this speed: (10*10^9/8)*(133/10^6) =
	 * 166250 bytes. With MTU size packets this is 110 packets, and with
	 * minimum Ethernet (MAC-preamble + intergap) 84 bytes is 1979 packets.
	 *
	 * Setting default cpumap queue to 2048 as worst-case (small packet)
	 * should be +64 packet due kthread wakeup call (due to xdp_do_flush)
	 * worst-case is 2043 packets.
	 *
	 * Sysadm can configured system to avoid deep-sleep via:
	 *   tuned-adm profile network-latency
	 */
	qsize = 2048;

	skel = xdp_redirect_cpu__open();
	if (!skel) {
		fprintf(stderr, "Failed to xdp_redirect_cpu__open: %s\n",
			strerror(errno));
		ret = EXIT_FAIL_BPF;
		goto end;
	}

	ret = sample_init_pre_load(skel);
	if (ret < 0) {
		fprintf(stderr, "Failed to sample_init_pre_load: %s\n", strerror(-ret));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	if (bpf_map__set_max_entries(skel->maps.cpu_map, n_cpus) < 0) {
		fprintf(stderr, "Failed to set max entries for cpu_map map: %s",
			strerror(errno));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	if (bpf_map__set_max_entries(skel->maps.cpus_available, n_cpus) < 0) {
		fprintf(stderr, "Failed to set max entries for cpus_available map: %s",
			strerror(errno));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	cpu = calloc(n_cpus, sizeof(int));
	if (!cpu) {
		fprintf(stderr, "Failed to allocate cpu array\n");
		goto end_destroy;
	}

	prog = skel->progs.xdp_prognum5_lb_hash_ip_pairs;
	while ((opt = getopt_long(argc, argv, "d:si:Sxp:f:e:r:m:c:q:Fvh",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'd':
			if (strlen(optarg) >= IF_NAMESIZE) {
				fprintf(stderr, "-d/--dev name too long\n");
				goto end_cpu;
			}
			ifname = (char *)&ifname_buf;
			safe_strncpy(ifname, optarg, sizeof(ifname));
			ifindex = if_nametoindex(ifname);
			if (!ifindex)
				ifindex = strtoul(optarg, NULL, 0);
			if (!ifindex) {
				fprintf(stderr, "Bad interface index or name (%d): %s\n",
					errno, strerror(errno));
				usage(argv, long_options, __doc__, mask, true, skel->obj);
				goto end_cpu;
			}
			break;
		case 's':
			mask |= SAMPLE_REDIRECT_MAP_CNT;
			break;
		case 'i':
			interval = strtoul(optarg, NULL, 0);
			break;
		case 'S':
			generic = true;
			break;
		case 'x':
			stress_mode = true;
			break;
		case 'p':
			/* Selecting eBPF prog to load */
			prog_name = optarg;
			prog = bpf_object__find_program_by_name(skel->obj,
								prog_name);
			if (!prog) {
				fprintf(stderr,
					"Failed to find program %s specified by"
					" option -p/--progname\n",
					prog_name);
				print_avail_progs(skel->obj);
				goto end_cpu;
			}
			break;
		case 'f':
			mprog_filename = optarg;
			break;
		case 'e':
			mprog_name = optarg;
			break;
		case 'r':
			redir_interface = optarg;
			mask |= SAMPLE_DEVMAP_XMIT_CNT_MULTI;
			break;
		case 'm':
			redir_map = optarg;
			break;
		case 'c':
			/* Add multiple CPUs */
			add_cpu = strtoul(optarg, NULL, 0);
			if (add_cpu >= n_cpus) {
				fprintf(stderr,
				"--cpu nr too large for cpumap err (%d):%s\n",
					errno, strerror(errno));
				usage(argv, long_options, __doc__, mask, true, skel->obj);
				goto end_cpu;
			}
			cpu[added_cpus++] = add_cpu;
			break;
		case 'q':
			qsize = strtoul(optarg, NULL, 0);
			break;
		case 'F':
			force = true;
			break;
		case 'v':
			sample_switch_mode();
			break;
		case 'h':
			error = false;
		default:
			usage(argv, long_options, __doc__, mask, error, skel->obj);
			goto end_cpu;
		}
	}

	ret = EXIT_FAIL_OPTION;
	if (ifindex == -1) {
		fprintf(stderr, "Required option --dev missing\n");
		usage(argv, long_options, __doc__, mask, true, skel->obj);
		goto end_cpu;
	}

	if (add_cpu == -1) {
		fprintf(stderr, "Required option --cpu missing\n"
				"Specify multiple --cpu option to add more\n");
		usage(argv, long_options, __doc__, mask, true, skel->obj);
		goto end_cpu;
	}

	skel->rodata->from_match[0] = ifindex;
	if (redir_interface)
		skel->rodata->to_match[0] = if_nametoindex(redir_interface);

	ret = xdp_redirect_cpu__load(skel);
	if (ret < 0) {
		fprintf(stderr, "Failed to xdp_redirect_cpu__load: %s\n",
			strerror(errno));
		goto end_cpu;
	}

	ret = bpf_obj_get_info_by_fd(bpf_map__fd(skel->maps.cpu_map), &info, &infosz);
	if (ret < 0) {
		fprintf(stderr, "Failed bpf_obj_get_info_by_fd for cpumap: %s\n",
			strerror(errno));
		goto end_cpu;
	}

	skel->bss->cpumap_map_id = info.id;

	map_fd = bpf_map__fd(skel->maps.cpu_map);
	avail_fd = bpf_map__fd(skel->maps.cpus_available);
	count_fd = bpf_map__fd(skel->maps.cpus_count);

	ret = mark_cpus_unavailable();
	if (ret < 0) {
		fprintf(stderr, "Unable to mark CPUs as unavailable\n");
		goto end_cpu;
	}

	ret = sample_init(skel, mask);
	if (ret < 0) {
		fprintf(stderr, "Failed to initialize sample: %s\n", strerror(-ret));
		ret = EXIT_FAIL;
		goto end_cpu;
	}

	value.bpf_prog.fd = set_cpumap_prog(skel, redir_interface, redir_map,
					    mprog_filename, mprog_name);
	if (value.bpf_prog.fd < 0) {
		fprintf(stderr, "Failed to set CPUMAP BPF program: %s\n",
			strerror(-value.bpf_prog.fd));
		usage(argv, long_options, __doc__, mask, true, skel->obj);
		ret = EXIT_FAIL_BPF;
		goto end_cpu;
	}
	value.qsize = qsize;

	for (i = 0; i < added_cpus; i++) {
		if (create_cpu_entry(cpu[i], &value, i, true) < 0) {
			fprintf(stderr, "Cannot proceed, exiting\n");
			usage(argv, long_options, __doc__, mask, true, skel->obj);
			goto end_cpu;
		}
	}

	ret = EXIT_FAIL_XDP;
	if (sample_install_xdp(prog, ifindex, generic, force) < 0)
		goto end_cpu;

	ret = sample_run(interval, stress_mode ? stress_cpumap : NULL, &value);
	if (ret < 0) {
		fprintf(stderr, "Failed during sample run: %s\n", strerror(-ret));
		ret = EXIT_FAIL;
		goto end_cpu;
	}
	ret = EXIT_OK;
end_cpu:
	free(cpu);
end_destroy:
	xdp_redirect_cpu__destroy(skel);
end:
	sample_exit(ret);
}
