#include "thread_map.h"
#include "evsel.h"
#include "debug.h"
#include "tests.h"

int test__open_syscall_event(void)
{
	int err = -1, fd;
	struct perf_evsel *evsel;
	unsigned int nr_open_calls = 111, i;
	struct thread_map *threads = thread_map__new(-1, getpid(), UINT_MAX);

	if (threads == NULL) {
		pr_debug("thread_map__new\n");
		return -1;
	}

	evsel = perf_evsel__newtp("syscalls", "sys_enter_open", 0);
	if (evsel == NULL) {
		pr_debug("is debugfs mounted on /sys/kernel/debug?\n");
		goto out_thread_map_delete;
	}

	if (perf_evsel__open_per_thread(evsel, threads) < 0) {
		pr_debug("failed to open counter: %s, "
			 "tweak /proc/sys/kernel/perf_event_paranoid?\n",
			 strerror(errno));
		goto out_evsel_delete;
	}

	for (i = 0; i < nr_open_calls; ++i) {
		fd = open("/etc/passwd", O_RDONLY);
		close(fd);
	}

	if (perf_evsel__read_on_cpu(evsel, 0, 0) < 0) {
		pr_debug("perf_evsel__read_on_cpu\n");
		goto out_close_fd;
	}

	if (evsel->counts->cpu[0].val != nr_open_calls) {
		pr_debug("perf_evsel__read_on_cpu: expected to intercept %d calls, got %" PRIu64 "\n",
			 nr_open_calls, evsel->counts->cpu[0].val);
		goto out_close_fd;
	}

	err = 0;
out_close_fd:
	perf_evsel__close_fd(evsel, 1, threads->nr);
out_evsel_delete:
	perf_evsel__delete(evsel);
out_thread_map_delete:
	thread_map__delete(threads);
	return err;
}
