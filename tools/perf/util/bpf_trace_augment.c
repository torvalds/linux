#include <assert.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <internal/xyarray.h>
#include <perf/threadmap.h>
#include <string.h>

#include "bpf_skel/augmented_raw_syscalls.skel.h"
#include "debug.h"
#include "evlist.h"
#include "parse-events.h"
#include "thread_map.h"
#include "trace_augment.h"

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
	 * Attach just the programs maintaining pids_to_trace now. sys_enter and
	 * sys_exit wait for augmented_syscalls__attach(), the rest are tail called.
	 */
	bpf_object__for_each_program(prog, skel->obj) {
		if (prog != skel->progs.sched_process_fork &&
		    prog != skel->progs.sched_process_exit &&
		    prog != skel->progs.sched_process_exec)
			bpf_program__set_autoattach(prog, /*autoattach=*/false);
	}

	err = augmented_raw_syscalls_bpf__load(skel);
	if (err < 0) {
		libbpf_strerror(err, buf, sizeof(buf));
		pr_debug("Failed to load augmented syscalls BPF skeleton: %s\n", buf);
		augmented_syscalls__cleanup();
		return err;
	}

	err = augmented_raw_syscalls_bpf__attach(skel);
	if (err < 0) {
		libbpf_strerror(err, buf, sizeof(buf));
		pr_debug("Failed to attach augmented syscalls BPF skeleton: %s\n", buf);
		augmented_syscalls__cleanup();
		return err;
	}
	return 0;
}

/* Attach sys_enter and sys_exit, once the maps they use are populated. */
int augmented_syscalls__attach(void)
{
	if (skel == NULL)
		return 0;

	skel->links.sys_enter = bpf_program__attach(skel->progs.sys_enter);
	if (skel->links.sys_enter == NULL)
		return -errno;

	skel->links.sys_exit = bpf_program__attach(skel->progs.sys_exit);
	if (skel->links.sys_exit == NULL)
		return -errno;

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
	unsigned int i;

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
				     xyarray__entry(bpf_output->core.fd, i, 0),
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

/* Trace just the target's tasks, or their processes, from their next exec if on_exec. */
void augmented_syscalls__set_target_pids(struct perf_thread_map *threads, bool inherit,
					 bool uses_tgid, bool on_exec)
{
	bool traced = !on_exec;
	pid_t last = -1;

	if (skel == NULL)
		return;

	skel->bss->uses_tgid = uses_tgid;
	/* Before seeding, so the forks of tasks already added are followed. */
	skel->bss->inherit = inherit;
	for (int i = 0; i < perf_thread_map__nr(threads); i++) {
		pid_t pid = perf_thread_map__pid(threads, i);

		/* A process's threads are adjacent, add it once, skipping exited threads. */
		if (uses_tgid) {
			pid = thread_map__tgid(threads, i);
			if (pid < 0 || pid == last)
				continue;
			last = pid;
		}
		/* Count a lost task atomically, as the BPF programs do too. */
		if (bpf_map__update_elem(skel->maps.pids_to_trace, &pid, sizeof(pid),
					 &traced, sizeof(traced), BPF_ANY))
			__atomic_fetch_add(&skel->bss->lost_tasks, 1, __ATOMIC_RELAXED);
	}
	skel->bss->has_pids_to_trace = true;
}

int augmented_syscalls__lost_tasks(void)
{
	if (skel == NULL)
		return 0;

	return skel->bss->lost_tasks;
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

struct bpf_program *augmented_syscalls__unaugmented_enter(void)
{
	return skel->progs.sys_enter_unaugmented;
}

struct bpf_program *augmented_syscalls__unaugmented_exit(void)
{
	return skel->progs.sys_exit_unaugmented;
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
	skel = NULL;
}
