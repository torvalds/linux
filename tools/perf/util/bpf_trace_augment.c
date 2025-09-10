#include <bpf/libbpf.h>
#include <internal/xyarray.h>

#include "util/debug.h"
#include "util/evlist.h"
#include "util/trace_augment.h"

#include "bpf_skel/augmented_raw_syscalls.skel.h"

static struct augmented_raw_syscalls_bpf *skel;
static struct evsel *bpf_output;

int augmented_syscalls__prepare(void)
{
	struct bpf_program *prog;
	char buf[128];
	int err;

	skel = augmented_raw_syscalls_bpf__open();
	if (!skel) {
		pr_debug("Failed to open augmented syscalls BPF skeleton\n");
		return -errno;
	}

	/*
	 * Disable attaching the BPF programs except for sys_enter and
	 * sys_exit that tail call into this as necessary.
	 */
	bpf_object__for_each_program(prog, skel->obj) {
		if (prog != skel->progs.sys_enter && prog != skel->progs.sys_exit)
			bpf_program__set_autoattach(prog, /*autoattach=*/false);
	}

	err = augmented_raw_syscalls_bpf__load(skel);
	if (err < 0) {
		libbpf_strerror(err, buf, sizeof(buf));
		pr_debug("Failed to load augmented syscalls BPF skeleton: %s\n", buf);
		return err;
	}

	augmented_raw_syscalls_bpf__attach(skel);
	return 0;
}

int augmented_syscalls__create_bpf_output(struct evlist *evlist)
{
	int err = parse_event(evlist, "bpf-output/no-inherit=1,name=__augmented_syscalls__/");

	if (err) {
		pr_err("ERROR: Setup BPF output event failed: %d\n", err);
		return err;
	}

	bpf_output = evlist__last(evlist);
	assert(evsel__name_is(bpf_output, "__augmented_syscalls__"));

	return 0;
}

void augmented_syscalls__setup_bpf_output(void)
{
	struct perf_cpu cpu;
	int i;

	if (bpf_output == NULL)
		return;

	/*
	 * Set up the __augmented_syscalls__ BPF map to hold for each
	 * CPU the bpf-output event's file descriptor.
	 */
	perf_cpu_map__for_each_cpu(cpu, i, bpf_output->core.cpus) {
		int mycpu = cpu.cpu;

		bpf_map__update_elem(skel->maps.__augmented_syscalls__,
				     &mycpu, sizeof(mycpu),
				     xyarray__entry(bpf_output->core.fd,
						    mycpu, 0),
				     sizeof(__u32), BPF_ANY);
	}
}

int augmented_syscalls__set_filter_pids(unsigned int nr, pid_t *pids)
{
	bool value = true;
	int err = 0;

	if (skel == NULL)
		return 0;

	for (size_t i = 0; i < nr; ++i) {
		err = bpf_map__update_elem(skel->maps.pids_filtered, &pids[i],
					   sizeof(*pids), &value, sizeof(value),
					   BPF_ANY);
		if (err)
			break;
	}
	return err;
}

int augmented_syscalls__get_map_fds(int *enter_fd, int *exit_fd, int *beauty_fd)
{
	if (skel == NULL)
		return -1;

	*enter_fd = bpf_map__fd(skel->maps.syscalls_sys_enter);
	*exit_fd  = bpf_map__fd(skel->maps.syscalls_sys_exit);
	*beauty_fd = bpf_map__fd(skel->maps.beauty_map_enter);

	if (*enter_fd < 0 || *exit_fd < 0 || *beauty_fd < 0) {
		pr_err("Error: failed to get syscall or beauty map fd\n");
		return -1;
	}

	return 0;
}

struct bpf_program *augmented_syscalls__unaugmented(void)
{
	return skel->progs.syscall_unaugmented;
}

struct bpf_program *augmented_syscalls__find_by_title(const char *name)
{
	struct bpf_program *pos;
	const char *sec_name;

	if (skel->obj == NULL)
		return NULL;

	bpf_object__for_each_program(pos, skel->obj) {
		sec_name = bpf_program__section_name(pos);
		if (sec_name && !strcmp(sec_name, name))
			return pos;
	}

	return NULL;
}

void augmented_syscalls__cleanup(void)
{
	augmented_raw_syscalls_bpf__destroy(skel);
}
