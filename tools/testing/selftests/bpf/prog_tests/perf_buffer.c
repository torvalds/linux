// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <test_progs.h>
#include "bpf/libbpf_internal.h"

/* AddressSanitizer sometimes crashes due to data dereference below, due to
 * this being mmap()'ed memory. Disable instrumentation with
 * no_sanitize_address attribute
 */
__attribute__((no_sanitize_address))
static void on_sample(void *ctx, int cpu, void *data, __u32 size)
{
	int cpu_data = *(int *)data, duration = 0;
	cpu_set_t *cpu_seen = ctx;

	if (cpu_data != cpu)
		CHECK(cpu_data != cpu, "check_cpu_data",
		      "cpu_data %d != cpu %d\n", cpu_data, cpu);

	CPU_SET(cpu, cpu_seen);
}

void test_perf_buffer(void)
{
	int err, prog_fd, on_len, nr_on_cpus = 0,  nr_cpus, i, duration = 0;
	const char *prog_name = "kprobe/sys_nanosleep";
	const char *file = "./test_perf_buffer.o";
	struct perf_buffer_opts pb_opts = {};
	struct bpf_map *perf_buf_map;
	cpu_set_t cpu_set, cpu_seen;
	struct bpf_program *prog;
	struct bpf_object *obj;
	struct perf_buffer *pb;
	struct bpf_link *link;
	bool *online;

	nr_cpus = libbpf_num_possible_cpus();
	if (CHECK(nr_cpus < 0, "nr_cpus", "err %d\n", nr_cpus))
		return;

	err = parse_cpu_mask_file("/sys/devices/system/cpu/online",
				  &online, &on_len);
	if (CHECK(err, "nr_on_cpus", "err %d\n", err))
		return;

	for (i = 0; i < on_len; i++)
		if (online[i])
			nr_on_cpus++;

	/* load program */
	err = bpf_prog_load(file, BPF_PROG_TYPE_KPROBE, &obj, &prog_fd);
	if (CHECK(err, "obj_load", "err %d errno %d\n", err, errno)) {
		obj = NULL;
		goto out_close;
	}

	prog = bpf_object__find_program_by_title(obj, prog_name);
	if (CHECK(!prog, "find_probe", "prog '%s' not found\n", prog_name))
		goto out_close;

	/* load map */
	perf_buf_map = bpf_object__find_map_by_name(obj, "perf_buf_map");
	if (CHECK(!perf_buf_map, "find_perf_buf_map", "not found\n"))
		goto out_close;

	/* attach kprobe */
	link = bpf_program__attach_kprobe(prog, false /* retprobe */,
					  SYS_NANOSLEEP_KPROBE_NAME);
	if (CHECK(IS_ERR(link), "attach_kprobe", "err %ld\n", PTR_ERR(link)))
		goto out_close;

	/* set up perf buffer */
	pb_opts.sample_cb = on_sample;
	pb_opts.ctx = &cpu_seen;
	pb = perf_buffer__new(bpf_map__fd(perf_buf_map), 1, &pb_opts);
	if (CHECK(IS_ERR(pb), "perf_buf__new", "err %ld\n", PTR_ERR(pb)))
		goto out_detach;

	/* trigger kprobe on every CPU */
	CPU_ZERO(&cpu_seen);
	for (i = 0; i < nr_cpus; i++) {
		if (i >= on_len || !online[i]) {
			printf("skipping offline CPU #%d\n", i);
			continue;
		}

		CPU_ZERO(&cpu_set);
		CPU_SET(i, &cpu_set);

		err = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set),
					     &cpu_set);
		if (err && CHECK(err, "set_affinity", "cpu #%d, err %d\n",
				 i, err))
			goto out_detach;

		usleep(1);
	}

	/* read perf buffer */
	err = perf_buffer__poll(pb, 100);
	if (CHECK(err < 0, "perf_buffer__poll", "err %d\n", err))
		goto out_free_pb;

	if (CHECK(CPU_COUNT(&cpu_seen) != nr_on_cpus, "seen_cpu_cnt",
		  "expect %d, seen %d\n", nr_on_cpus, CPU_COUNT(&cpu_seen)))
		goto out_free_pb;

out_free_pb:
	perf_buffer__free(pb);
out_detach:
	bpf_link__destroy(link);
out_close:
	bpf_object__close(obj);
	free(online);
}
